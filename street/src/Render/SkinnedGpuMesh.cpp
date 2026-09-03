// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/SkinnedGpuMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <stdexcept>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaStreet {

SkinnedGpuMesh::SkinnedGpuMesh(GraphicsDevice& device, const Geometry::SkinnedMeshData& data,
                               std::string name)
    : name_(std::move(name))
{
    if (data.vertices.empty() || data.indices.empty())
        throw std::invalid_argument("SkinnedGpuMesh '" + name_ + "': cannot upload an empty mesh");

    vertexCount_   = static_cast<int>(data.vertices.size());
    triangleCount_ = static_cast<int>(data.indices.size() / 3);

    ownedVertexBuffer_ = std::make_unique<VertexBuffer>(
        device, Geometry::SkinnedVertex::getVertexDeclarationStatic(), vertexCount_,
        BufferUsage::WriteOnly);
    ownedVertexBuffer_->SetData(data.vertices.data(), vertexCount_);
    vertexBuffer_ = ownedVertexBuffer_.get();

    ownedIndexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits,
                                                      static_cast<int>(data.indices.size()),
                                                      BufferUsage::WriteOnly);
    ownedIndexBuffer_->SetData(data.indices.data(), static_cast<int>(data.indices.size()));
    indexBuffer_ = ownedIndexBuffer_.get();

    // The bind-pose bounds, grown: a walking figure's arms and legs leave the
    // box the standing one occupies, and a bounding volume that culls a
    // pedestrian mid-stride is a pedestrian that flickers.
    bounds_ = data.bounds();
    const Vector3 grow(0.35f, 0.10f, 0.45f);
    bounds_ = BoundingBox(bounds_.Min - grow, bounds_.Max + grow);
    sphere_ = BoundingSphere::CreateFromBoundingBox(bounds_);
    gpuBytes_ = data.vertices.size() * sizeof(Geometry::SkinnedVertex)
                + data.indices.size() * sizeof(std::uint32_t);
}

SkinnedGpuMesh::SkinnedGpuMesh(ModelMeshPart& part, const BoundingBox& bounds, std::string name)
    : name_(std::move(name))
{
    vertexBuffer_  = part.getVertexBufferProperty();
    indexBuffer_   = part.getIndexBufferProperty();
    vertexOffset_  = part.getVertexOffsetProperty();
    startIndex_    = part.getStartIndexProperty();
    vertexCount_   = part.getNumVerticesProperty();
    triangleCount_ = part.getPrimitiveCountProperty();
    if (vertexBuffer_ == nullptr || indexBuffer_ == nullptr || triangleCount_ <= 0)
        throw std::invalid_argument("SkinnedGpuMesh '" + name_
                                    + "': imported part has no drawable geometry");

    // The caller's bounds, grown the same way a generated figure's are: these
    // are the bind pose and a walking figure leaves it.
    const Vector3 grow(0.35f, 0.10f, 0.45f);
    bounds_ = BoundingBox(bounds.Min - grow, bounds.Max + grow);
    sphere_ = BoundingSphere::CreateFromBoundingBox(bounds_);
    gpuBytes_ = 0;   // borrowed; the Model owns it
}

SkinnedGpuMesh::~SkinnedGpuMesh() = default;

void SkinnedGpuMesh::draw(GraphicsDevice& device) const
{
    device.SetVertexBuffer(vertexBuffer_);
    device.SetIndexBuffer(indexBuffer_);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, vertexOffset_, 0, vertexCount_,
                                 startIndex_, triangleCount_);
}

}  // namespace CnaStreet
