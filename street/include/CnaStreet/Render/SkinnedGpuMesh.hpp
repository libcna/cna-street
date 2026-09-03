// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/SkinnedMeshData.hpp"

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

/// One uploaded skinned mesh. The same shape as @ref GpuMesh, with the skinned
/// vertex declaration and no `ModelMeshPart`: nothing instances a character, so
/// the part the instanced renderer would want is never built.
class SkinnedGpuMesh
{
public:
    SkinnedGpuMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                   const Geometry::SkinnedMeshData& data, std::string name);

    /// Borrows an imported skinned mesh part rather than copying it.
    ///
    /// The buffers belong to the `Model` the `ModelLibrary` is holding open, and
    /// its skinned vertex declaration is byte-for-byte the one this application
    /// builds its own people from -- position, normal, tangent, one UV, four
    /// blend indices, four blend weights. That is not a coincidence worth
    /// glossing over: it means an imported rig drops into the same
    /// `SkinnedPbrEffect` draw path as a generated one, with the same lighting
    /// and the same shadows, and the demonstration is the framework's importer
    /// rather than a second one written beside it.
    SkinnedGpuMesh(Microsoft::Xna::Framework::Graphics::ModelMeshPart& part,
                   const Microsoft::Xna::Framework::BoundingBox& bounds, std::string name);
    ~SkinnedGpuMesh();

    SkinnedGpuMesh(const SkinnedGpuMesh&) = delete;
    SkinnedGpuMesh& operator=(const SkinnedGpuMesh&) = delete;

    void draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

    [[nodiscard]] const Microsoft::Xna::Framework::BoundingBox& bounds() const { return bounds_; }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingSphere& sphere() const
    {
        return sphere_;
    }
    [[nodiscard]] int triangleCount() const { return triangleCount_; }
    [[nodiscard]] std::size_t gpuBytes() const { return gpuBytes_; }
    [[nodiscard]] const std::string& name() const { return name_; }

private:
    std::string name_;
    /// Owned only when this mesh was built from `SkinnedMeshData`; null when it
    /// borrows an imported part.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> ownedVertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer>  ownedIndexBuffer_;
    Microsoft::Xna::Framework::Graphics::VertexBuffer* vertexBuffer_ = nullptr;
    Microsoft::Xna::Framework::Graphics::IndexBuffer*  indexBuffer_  = nullptr;
    int vertexOffset_ = 0;
    int startIndex_   = 0;
    Microsoft::Xna::Framework::BoundingBox    bounds_;
    Microsoft::Xna::Framework::BoundingSphere sphere_;
    int         triangleCount_ = 0;
    int         vertexCount_   = 0;
    std::size_t gpuBytes_      = 0;
};

}  // namespace CnaStreet
