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
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer>  indexBuffer_;
    Microsoft::Xna::Framework::BoundingBox    bounds_;
    Microsoft::Xna::Framework::BoundingSphere sphere_;
    int         triangleCount_ = 0;
    int         vertexCount_   = 0;
    std::size_t gpuBytes_      = 0;
};

}  // namespace CnaStreet
