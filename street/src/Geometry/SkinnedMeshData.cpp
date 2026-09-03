// SPDX-License-Identifier: MIT
#include "CnaStreet/Geometry/SkinnedMeshData.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace Microsoft::Xna::Framework;

namespace CnaStreet::Geometry {

namespace {

/// Squared distance from a point to a segment, and where along it the nearest
/// point falls.
float DistanceToSegment(const Vector3& point, const Vector3& a, const Vector3& b)
{
    const Vector3 ab = b - a;
    const float lengthSquared = ab.X * ab.X + ab.Y * ab.Y + ab.Z * ab.Z;
    if (lengthSquared < 1e-9f) return Vector3::Distance(point, a);
    const Vector3 ap = point - a;
    const float t = std::clamp((ap.X * ab.X + ap.Y * ab.Y + ap.Z * ab.Z) / lengthSquared, 0.0f,
                               1.0f);
    return Vector3::Distance(point, a + ab * t);
}

}  // namespace

BoundingBox SkinnedMeshData::bounds() const
{
    if (vertices.empty()) return BoundingBox(Vector3::Zero, Vector3::Zero);
    float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float hi[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest()};
    for (const SkinnedVertex& v : vertices)
    {
        const float p[3] = {v.Position.X, v.Position.Y, v.Position.Z};
        for (int i = 0; i < 3; ++i)
        {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    return BoundingBox(Vector3(lo[0], lo[1], lo[2]), Vector3(hi[0], hi[1], hi[2]));
}

int Skeleton::add(std::string name, int parent, const Vector3& head, float influence)
{
    bones_.push_back(Bone{std::move(name), parent, head, influence});
    return static_cast<int>(bones_.size()) - 1;
}

int Skeleton::find(const std::string& name) const
{
    for (std::size_t i = 0; i < bones_.size(); ++i)
        if (bones_[i].name == name) return static_cast<int>(i);
    return -1;
}

std::vector<Matrix> Skeleton::bindPose() const
{
    std::vector<Matrix> out;
    out.reserve(bones_.size());
    for (const Bone& bone : bones_)
    {
        const Vector3 origin = bone.parent < 0
                                   ? Vector3::Zero
                                   : bones_[static_cast<std::size_t>(bone.parent)].head;
        out.push_back(Matrix::CreateTranslation(bone.head - origin));
    }
    return out;
}

std::vector<Matrix> Skeleton::inverseBindPose() const
{
    std::vector<Matrix> out;
    out.reserve(bones_.size());
    // Bind rotations are identity throughout, so the global bind transform of a
    // bone is a pure translation to its head and its inverse is the negation.
    for (const Bone& bone : bones_) out.push_back(Matrix::CreateTranslation(-bone.head));
    return out;
}

std::vector<int> Skeleton::hierarchy() const
{
    std::vector<int> out;
    out.reserve(bones_.size());
    for (const Bone& bone : bones_) out.push_back(bone.parent);
    return out;
}

SkinnedMeshData Skeleton::bind(const MeshData& mesh) const
{
    SkinnedMeshData out;
    out.indices = mesh.indices;
    out.vertices.reserve(mesh.vertices.size());
    if (bones_.empty()) return out;

    // What a bone *is*, for the purpose of catching flesh: the joint at its head
    // plus the limbs running out of it to its children. Not the limb running
    // *into* it from its parent -- which is what the first version measured,
    // and it is off by one in the worst possible way: the flesh of the upper
    // arm went to the forearm bone, the thigh's to the shin's, and every limb
    // bent about the wrong joint. The figures came out with arms hinged at the
    // elbow to the shoulder.
    std::vector<std::vector<Vector3>> limbs(bones_.size());
    for (std::size_t i = 0; i < bones_.size(); ++i)
    {
        const int parent = bones_[i].parent;
        if (parent >= 0) limbs[static_cast<std::size_t>(parent)].push_back(bones_[i].head);
    }

    const auto distanceTo = [&](const Vector3& point, std::size_t bone) {
        const std::vector<Vector3>& children = limbs[bone];
        if (children.empty()) return Vector3::Distance(point, bones_[bone].head);
        float best = std::numeric_limits<float>::max();
        for (const Vector3& child : children)
            best = std::min(best, DistanceToSegment(point, bones_[bone].head, child));
        return best;
    };

    struct Influence { int bone; float weight; };
    std::vector<Influence> found;
    found.reserve(bones_.size());

    for (const Vertex& v : mesh.vertices)
    {
        found.clear();
        for (std::size_t i = 0; i < bones_.size(); ++i)
        {
            const float distance = distanceTo(v.Position, i);
            if (distance >= bones_[i].influence) continue;
            const float t = 1.0f - distance / bones_[i].influence;
            found.push_back(Influence{static_cast<int>(i), t * t * t});
        }

        Vector4 weights(0.0f, 0.0f, 0.0f, 0.0f);
        std::array<std::uint8_t, 4> indices{0, 0, 0, 0};
        if (found.empty())
        {
            // Outside every bone's reach: give it to the nearest one whole,
            // rather than to bone zero, which would put a stray hair on the
            // pelvis and drag it around the street.
            int   best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < bones_.size(); ++i)
            {
                const float distance = distanceTo(v.Position, i);
                if (distance < bestDistance) { bestDistance = distance; best = static_cast<int>(i); }
            }
            indices[0] = static_cast<std::uint8_t>(best);
            weights.X  = 1.0f;
        }
        else
        {
            std::partial_sort(found.begin(),
                              found.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(
                                                  4, found.size())),
                              found.end(),
                              [](const Influence& a, const Influence& b) {
                                  return a.weight > b.weight;
                              });
            const std::size_t take = std::min<std::size_t>(4, found.size());
            float total = 0.0f;
            for (std::size_t i = 0; i < take; ++i) total += found[i].weight;
            if (total <= 0.0f) total = 1.0f;
            float* w[4] = {&weights.X, &weights.Y, &weights.Z, &weights.W};
            for (std::size_t i = 0; i < take; ++i)
            {
                indices[i] = static_cast<std::uint8_t>(found[i].bone);
                *w[i]      = found[i].weight / total;
            }
        }

        out.vertices.emplace_back(v.Position, v.Normal, v.Tangent, v.TextureCoordinate, weights,
                                  indices);
    }
    return out;
}

}  // namespace CnaStreet::Geometry
