// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class IndexBuffer;
    class ModelMeshPart;
    class VertexBuffer;
}

namespace CnaStreet {

/**
 * @brief One uploaded mesh: a vertex buffer, an index buffer and its bounds.
 *
 * The `ModelMeshPart` is kept because that is what `InstancedRendererEXT` draws
 * through; ordinary draws go through the device directly, which avoids the
 * `Model`/`ModelMesh` machinery for geometry that was never a model file.
 */
class GpuMesh
{
public:
    GpuMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const Geometry::MeshData& data, std::string name);

    /// Borrows the buffers of a mesh part that already exists on the GPU: one
    /// piece of a `Model` the content pipeline produced. The part and the model
    /// behind it must outlive this. Used for imported glTF, which arrives as
    /// `ModelMeshPart`s with exactly the vertex layout this project uses --
    /// stride 48, position/normal/tangent/UV -- so nothing has to be copied.
    GpuMesh(Microsoft::Xna::Framework::Graphics::ModelMeshPart& part,
            const Microsoft::Xna::Framework::BoundingBox& bounds, std::string name);
    ~GpuMesh();

    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    /// Binds the buffers and issues one indexed draw. The effect must already
    /// have been applied.
    void draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::ModelMeshPart* part() const
    {
        return part_;
    }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingBox& bounds() const { return bounds_; }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingSphere& sphere() const
    {
        return sphere_;
    }
    [[nodiscard]] int triangleCount() const { return triangleCount_; }
    [[nodiscard]] int vertexCount() const { return vertexCount_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    /// Bytes of GPU memory this mesh occupies, for the diagnostics overlay.
    [[nodiscard]] std::size_t gpuBytes() const { return gpuBytes_; }

private:
    std::string name_;
    /// Null when the buffers are borrowed from a `ModelMeshPart`.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer>  ownedVertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer>   ownedIndexBuffer_;
    Microsoft::Xna::Framework::Graphics::VertexBuffer* vertexBuffer_ = nullptr;
    Microsoft::Xna::Framework::Graphics::IndexBuffer*  indexBuffer_  = nullptr;
    int vertexOffset_ = 0;
    int startIndex_   = 0;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ModelMeshPart> ownedPart_;
    Microsoft::Xna::Framework::Graphics::ModelMeshPart* part_ = nullptr;
    Microsoft::Xna::Framework::BoundingBox    bounds_;
    Microsoft::Xna::Framework::BoundingSphere sphere_;
    int         triangleCount_ = 0;
    int         vertexCount_   = 0;
    std::size_t gpuBytes_      = 0;
};

}  // namespace CnaStreet
