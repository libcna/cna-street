// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Geometry/SkinnedMeshData.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"

#include <string>
#include <vector>

namespace CnaStreet {

/// Which bones the generated rig has, by name, so the clip builder and the mesh
/// builder cannot disagree about what "left forearm" means.
namespace BoneName {
inline constexpr const char* kPelvis   = "pelvis";
inline constexpr const char* kSpine    = "spine";
inline constexpr const char* kChest    = "chest";
inline constexpr const char* kNeck     = "neck";
inline constexpr const char* kHead     = "head";
inline constexpr const char* kClavicle = "clavicle";   ///< suffixed .L / .R
inline constexpr const char* kUpperArm = "upperarm";
inline constexpr const char* kForearm  = "forearm";
inline constexpr const char* kHand     = "hand";
inline constexpr const char* kThigh    = "thigh";
inline constexpr const char* kShin     = "shin";
inline constexpr const char* kFoot     = "foot";
}  // namespace BoneName

/// What one person looks like. Everything here is drawn from the seed, so a
/// crowd is deterministic and no two people are the same person.
struct CharacterLook
{
    float height = 1.75f;
    /// 0 for a slim build, 1 for a heavy one. Scales the torso and the limbs
    /// separately, because they do not vary together.
    float build = 0.5f;
    /// Shoulder width relative to the canonical quarter of the height.
    float shoulders = 1.0f;
    const Material* skin     = nullptr;
    const Material* coat     = nullptr;
    const Material* trousers = nullptr;
    const Material* hair     = nullptr;
    const Material* shoes    = nullptr;
    /// 0 short, 1 long. Long hair gets a mass at the nape rather than a cap.
    float hairLength = 0.0f;
    bool  carriesBag = false;
    bool  wearsCoat  = true;
    /// A cap or a hat, on about one person in seven.
    bool  wearsHat   = false;
};

/**
 * @brief Builds people: a mesh, a skeleton, and the clips that move them.
 *
 * The previous generator made a figure out of cylinders and ellipsoids at eight
 * baked phases of a stride. It read as a mannequin at any distance, for reasons
 * that are all silhouette: no neck, no hands, no shoes worth the name, a
 * cylinder for a torso and a sphere for a head. A human silhouette is the shape
 * a viewer knows best, and it is the one thing in a street picture nobody has
 * to be told is wrong.
 *
 * This one builds a surface. Every part is a swept elliptical tube whose
 * section varies along its length — the deltoid is wider than the arm below it,
 * the calf has a belly, the coat flares to a hem — emitted through
 * `SurfacePatch` so the normals are smooth across the whole figure. Then the
 * mesh is bound to a nineteen-bone skeleton by distance, and animated on the
 * GPU by `SkinnedPbrEffect` from clips this class also builds.
 *
 * That is one mesh per person instead of nine, continuous motion instead of
 * eight phases, and it exercises the skeletal path CNA actually has.
 */
class CharacterFactory
{
public:
    explicit CharacterFactory(const MaterialLibrary& materials);

    /// One person's worth of geometry, in the bind pose, standing at the origin
    /// facing +Z with the feet on y = 0.
    struct Character
    {
        Geometry::Skeleton skeleton;
        /// One entry per material the figure uses.
        struct Part
        {
            const Material* material = nullptr;
            Geometry::SkinnedMeshData mesh;
        };
        std::vector<Part> parts;
    };

    /// @p detail below 1 halves the ring counts, for the figures at the far end
    /// of the street.
    [[nodiscard]] Character build(const CharacterLook& look, bool fullDetail) const;

    /// Picks a look from a seed. Kept here so the crowd's variation and the
    /// figure's construction cannot drift apart.
    [[nodiscard]] CharacterLook look(Rng& rng, int variant) const;

    /// The clips a pedestrian plays. Built against a skeleton of the shape
    /// @ref build produces, so a clip and a rig always match.
    struct Clips
    {
        Microsoft::Xna::Framework::Graphics::AnimationClip walk;
        Microsoft::Xna::Framework::Graphics::AnimationClip idle;
    };
    /// @p strideSeconds is one complete cycle: two steps.
    [[nodiscard]] static Clips clips(const Geometry::Skeleton& skeleton, float height,
                                     float strideSeconds);

private:
    const MaterialLibrary& materials_;
};

}  // namespace CnaStreet
