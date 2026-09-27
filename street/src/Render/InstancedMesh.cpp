// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/InstancedMesh.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaStreet {

const VertexDeclaration& InstancedMesh::instanceDeclaration()
{
    static const VertexDeclaration declaration{
        VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
        VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
        VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4)};
    return declaration;
}

InstancedMesh::InstancedMesh(GraphicsDevice& device, ModelMeshPart* part)
    : device_(device), part_(part)
{
    if (part_ == nullptr || part_->getVertexBufferProperty() == nullptr
        || part_->getIndexBufferProperty() == nullptr)
        throw std::invalid_argument("InstancedMesh: the mesh part needs a vertex and an index "
                                    "buffer");
}

InstancedMesh::~InstancedMesh() = default;

bool InstancedMesh::instancingSupported() const
{
    // The transforms are a second stream, so multi-stream input is as much a
    // requirement as instancing itself.
    return device_.SupportsCapability(CNA::GraphicsCapability::Instancing)
           && device_.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput);
}

void InstancedMesh::setInstances(const std::vector<Matrix>& transforms)
{
    transforms_ = transforms;
    instanceCount_ = static_cast<int>(transforms.size());
    if (instanceCount_ == 0 || !instancingSupported()) return;

    if (instanceBuffer_ == nullptr || instanceCount_ > instanceCapacity_)
    {
        instanceBuffer_ = std::make_unique<DynamicVertexBuffer>(
            device_, instanceDeclaration(), instanceCount_, BufferUsage::WriteOnly);
        instanceCapacity_ = instanceCount_;
    }
    instanceBuffer_->SetDataRaw(transforms.data(), instanceCount_,
                                static_cast<int>(sizeof(Matrix)));
}

void InstancedMesh::draw(Effect& effect)
{
    lastDrawCallCount_ = 0;
    if (instanceCount_ == 0) return;

    const int primitiveCount = part_->getPrimitiveCountProperty();
    const int numVertices    = part_->getNumVerticesProperty();
    const int startIndex     = part_->getStartIndexProperty();
    const int vertexOffset   = part_->getVertexOffsetProperty();

    // The draw borrows the device's bindings and gives them back. Leaving the
    // instance stream bound would make next frame's upload a write into a
    // buffer still set on the device, which XNA refuses.
    struct BindingRestore
    {
        GraphicsDevice& device;
        std::vector<VertexBufferBinding> vertices;
        const IndexBuffer* indices;
        ~BindingRestore()
        {
            try
            {
                device.SetVertexBuffers(vertices);
                device.setIndicesProperty(indices);
            }
            catch (...)
            {
            }
        }
    } restore{device_, device_.GetVertexBuffers(), device_.getIndicesProperty()};

    device_.setIndicesProperty(part_->getIndexBufferProperty());

    if (instancingSupported() && instanceBuffer_ != nullptr)
    {
        std::vector<VertexBufferBinding> bindings;
        bindings.emplace_back(part_->getVertexBufferProperty(), 0, 0);
        bindings.emplace_back(instanceBuffer_.get(), 0, 1);
        device_.SetVertexBuffers(bindings);
        effect.Apply();
        device_.DrawInstancedPrimitives(part_->getPrimitiveTypeEXTProperty(), vertexOffset, 0,
                                        numVertices, startIndex, primitiveCount, instanceCount_);
        lastDrawCallCount_ = 1;
        return;
    }

    auto* matrices = dynamic_cast<IEffectMatrices*>(&effect);
    if (matrices == nullptr)
        throw std::logic_error("InstancedMesh: drawing copies one by one needs an effect with a "
                               "world matrix");
    const Matrix world = matrices->getWorldProperty();
    device_.SetVertexBuffer(part_->getVertexBufferProperty());
    for (const Matrix& instance : transforms_)
    {
        matrices->setWorldProperty(instance * world);
        effect.Apply();
        device_.DrawIndexedPrimitives(part_->getPrimitiveTypeEXTProperty(), vertexOffset, 0,
                                      numVertices, startIndex, primitiveCount);
        ++lastDrawCallCount_;
    }
    matrices->setWorldProperty(world);
}

}  // namespace CnaStreet
