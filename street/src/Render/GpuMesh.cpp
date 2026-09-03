// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/GpuMesh.hpp"

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

GpuMesh::GpuMesh(GraphicsDevice& device, const Geometry::MeshData& data, std::string name)
    : name_(std::move(name))
{
    if (data.vertices.empty() || data.indices.empty())
        throw std::invalid_argument("GpuMesh '" + name_ + "': cannot upload an empty mesh");
    if (data.indices.size() % 3 != 0)
        throw std::invalid_argument("GpuMesh '" + name_ + "': index count is not a multiple of 3");

    vertexCount_   = static_cast<int>(data.vertices.size());
    triangleCount_ = static_cast<int>(data.indices.size() / 3);

    ownedVertexBuffer_ = std::make_unique<VertexBuffer>(
        device, Geometry::Vertex::getVertexDeclarationStatic(), vertexCount_,
        BufferUsage::WriteOnly);
    ownedVertexBuffer_->SetData(data.vertices.data(), vertexCount_);
    vertexBuffer_ = ownedVertexBuffer_.get();

    // 32-bit indices unconditionally. The merged façade batches run to hundreds
    // of thousands of vertices, and choosing per mesh would mean two code paths
    // for a saving that is a rounding error next to the vertex data itself.
    ownedIndexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits,
                                                      static_cast<int>(data.indices.size()),
                                                      BufferUsage::WriteOnly);
    ownedIndexBuffer_->SetData(data.indices.data(), static_cast<int>(data.indices.size()));
    indexBuffer_ = ownedIndexBuffer_.get();

    ownedPart_ = std::make_unique<ModelMeshPart>(vertexBuffer_, indexBuffer_, vertexCount_,
                                                 triangleCount_, 0, 0);
    part_ = ownedPart_.get();

    bounds_ = data.bounds();
    sphere_ = BoundingSphere::CreateFromBoundingBox(bounds_);
    gpuBytes_ = data.vertices.size() * sizeof(Geometry::Vertex)
                + data.indices.size() * sizeof(std::uint32_t);
}

GpuMesh::~GpuMesh() = default;

GpuMesh::GpuMesh(ModelMeshPart& part, const BoundingBox& bounds, std::string name)
    : name_(std::move(name))
{
    vertexBuffer_  = part.getVertexBufferProperty();
    indexBuffer_   = part.getIndexBufferProperty();
    vertexOffset_  = part.getVertexOffsetProperty();
    startIndex_    = part.getStartIndexProperty();
    vertexCount_   = part.getNumVerticesProperty();
    triangleCount_ = part.getPrimitiveCountProperty();
    part_          = &part;
    bounds_        = bounds;
    sphere_        = BoundingSphere::CreateFromBoundingBox(bounds_);
    // Borrowed: the memory belongs to the Model this part came from, and
    // counting it here would double it in the overlay.
    gpuBytes_ = 0;
    if (vertexBuffer_ == nullptr || indexBuffer_ == nullptr || triangleCount_ <= 0)
        throw std::invalid_argument("GpuMesh '" + name_ + "': the mesh part has no geometry");
}

void GpuMesh::draw(GraphicsDevice& device) const
{
    device.SetVertexBuffer(vertexBuffer_);
    device.SetIndexBuffer(indexBuffer_);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, vertexOffset_, 0, vertexCount_,
                                 startIndex_, triangleCount_);
}

}  // namespace CnaStreet
