// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include <cstdint>
#include <vector>

namespace CnaStreet::Geometry {

/**
 * @brief The one vertex format the city is built from.
 *
 * `VertexPositionNormalTangentTexture` and nothing else, because `PbrEffect`'s
 * vertex program binds a tangent at attribute location 2 (with glTF's
 * bitangent-handedness sign in `w`). A mesh built from
 * `VertexPositionNormalTexture` would hand it texture coordinates where it
 * expects a tangent frame, and every normal-mapped surface would be lit wrong in
 * a way that is very hard to see and impossible to unsee.
 */
using Vertex = Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;

/// CPU-side geometry. 32-bit indices throughout: the merged static batches run
/// well past 65 535 vertices and a silent wrap would be a nightmare to find.
struct MeshData
{
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const { return indices.empty(); }
    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox bounds() const;

    void clear()
    {
        vertices.clear();
        indices.clear();
    }
};

}  // namespace CnaStreet::Geometry
