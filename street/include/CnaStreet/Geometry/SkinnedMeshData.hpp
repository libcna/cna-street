// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace CnaStreet::Geometry {

/// The vertex `SkinnedPbrEffect` needs: the same position/normal/tangent/UV as
/// everything else, plus four bone indices and four weights.
using SkinnedVertex = Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;

/// CPU-side skinned geometry.
struct SkinnedMeshData
{
    std::vector<SkinnedVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const { return indices.empty(); }
    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox bounds() const;
};

/**
 * @brief One joint of a generated skeleton.
 *
 * A bone is its parent and where it sits in the bind pose, and nothing else.
 * The bind rotation is identity for every bone here — the skeleton is built
 * standing, in the pose the mesh is generated in, so a clip's rotation about a
 * joint is the joint's rotation and needs no rest offset undone first. That is
 * worth the constraint: a rig whose bind rotations are not identity is a rig
 * where every animation value has to be read through a matrix nobody can see.
 */
struct Bone
{
    std::string name;
    int parent = -1;  ///< index of the parent bone, or -1 for the root
    /// Position in the bind pose, in model space, metres.
    Microsoft::Xna::Framework::Vector3 head{0.0f, 0.0f, 0.0f};
    /// How far this bone's influence reaches. Used to bind the skin.
    float influence = 0.10f;
};

/**
 * @brief A generated skeleton and the binding that attaches a mesh to it.
 *
 * Weights are bound by distance rather than declared per vertex, and that is
 * the decision that makes a *procedural* character practical at all. Every
 * piece of the figure is generated parametrically — a tube along the forearm, a
 * dome on the skull — so the alternative is for each generator to also state
 * which bone each of its rings belongs to, in two places that can disagree.
 * Binding from the bind-pose position instead means a new piece of clothing is
 * skinned correctly the moment it exists, without the generator knowing there
 * is a skeleton at all.
 *
 * The falloff is cubic in the distance to the bone's *segment* rather than to
 * its head, because a limb is a line and not a point: a point beside the middle
 * of a thigh must belong to the thigh, and a point-distance metric gives it to
 * the knee.
 */
class Skeleton
{
public:
    /// Adds a bone, returning its index. Parents must be added first: every
    /// consumer of `SkinningData` requires topological order.
    int add(std::string name, int parent, const Microsoft::Xna::Framework::Vector3& head,
            float influence);

    [[nodiscard]] int count() const { return static_cast<int>(bones_.size()); }
    [[nodiscard]] const Bone& operator[](int index) const
    {
        return bones_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] int find(const std::string& name) const;

    /// Bind-pose local transform for each bone, relative to its parent.
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Matrix> bindPose() const;
    /// Inverse of each bone's bind-pose global transform.
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Matrix> inverseBindPose() const;
    [[nodiscard]] std::vector<int> hierarchy() const;

    /// Attaches @p mesh to this skeleton, producing the skinned form. The mesh
    /// must be in the bind pose.
    [[nodiscard]] SkinnedMeshData bind(const MeshData& mesh) const;

private:
    std::vector<Bone> bones_;
};

}  // namespace CnaStreet::Geometry
