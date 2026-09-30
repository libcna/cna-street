// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class DynamicVertexBuffer;
    class Effect;
    class GraphicsDevice;
    class ModelMeshPart;
    class VertexDeclaration;
}

namespace CnaStreet {

/**
 * @brief Draws one mesh part many times in a single instanced call.
 *
 * The transforms go into a second vertex stream at instance frequency 1, four
 * `Vector4` texture coordinates 1..4 -- the layout CNA's stock effects read an
 * instance world matrix from -- and the draw is XNA's own
 * `DrawInstancedPrimitives`. Where the renderer cannot instance, each copy is
 * drawn on its own with the effect's world matrix set to it.
 *
 */
class InstancedMesh
{
public:
    InstancedMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                  Microsoft::Xna::Framework::Graphics::ModelMeshPart* part);
    ~InstancedMesh();

    InstancedMesh(const InstancedMesh&) = delete;
    InstancedMesh& operator=(const InstancedMesh&) = delete;

    /// Uploads this frame's transforms. The instance buffer is kept across
    /// frames and only reallocated when it has to grow.
    void setInstances(const std::vector<Microsoft::Xna::Framework::Matrix>& transforms);

    /// Draws every instance with @p effect, which is applied here.
    void draw(Microsoft::Xna::Framework::Graphics::Effect& effect);

    /// How many draw calls the last draw issued: one instanced, or one per copy.
    [[nodiscard]] int lastDrawCallCount() const { return lastDrawCallCount_; }

    /// The per-instance vertex layout: a world matrix as texture coordinates 1..4.
    [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration&
    instanceDeclaration();

private:
    [[nodiscard]] bool instancingSupported() const;

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    Microsoft::Xna::Framework::Graphics::ModelMeshPart* part_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> instanceBuffer_;
    std::vector<Microsoft::Xna::Framework::Matrix> transforms_;
    int instanceCount_ = 0;
    int instanceCapacity_ = 0;
    int lastDrawCallCount_ = 0;
};

}  // namespace CnaStreet
