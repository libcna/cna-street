// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/CharacterFactory.hpp"

#include "CnaStreet/Geometry/MeshBuilder.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::MeshData;
using CnaStreet::Geometry::Skeleton;
using CnaStreet::Geometry::SurfacePatch;

namespace CnaStreet {

namespace {

/// One cross-section of a swept limb or body part: where the centre is, how
/// wide and how deep, and how far the section is rotated about the sweep.
struct Ring
{
    Vector3 centre;
    float   halfWidth = 0.05f;   ///< across the figure (x, before the frame)
    float   halfDepth = 0.05f;   ///< front to back (z, before the frame)
    float   roll = 0.0f;
};

Vector3 Normalise(const Vector3& v, const Vector3& fallback)
{
    const float lengthSquared = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (lengthSquared < 1e-10f) return fallback;
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return Vector3(v.X * inverse, v.Y * inverse, v.Z * inverse);
}

/**
 * @brief Sweeps an elliptical section along a spine and emits it smoothly.
 *
 * The frame at each ring comes from the direction to its neighbours, so a limb
 * that bends does not shear: the section stays perpendicular to the spine. The
 * reference used to build the frame is world +X where the spine is not close to
 * it, which for a standing figure is every part of it.
 */
void Sweep(MeshBuilder& builder, const std::vector<Ring>& rings, int sides, bool capStart,
           bool capEnd, float smoothing = 1.25f)
{
    if (rings.size() < 2 || sides < 3) return;

    SurfacePatch patch;
    patch.resize(static_cast<int>(rings.size()), sides);
    patch.wrapCols       = true;
    patch.smoothingAngle = smoothing;

    for (std::size_t r = 0; r < rings.size(); ++r)
    {
        const Vector3 previous = rings[r == 0 ? 0 : r - 1].centre;
        const Vector3 next     = rings[std::min(r + 1, rings.size() - 1)].centre;
        const Vector3 along    = Normalise(next - previous, Vector3::Up);
        // Keep the section's own "width" axis pointing across the figure. A
        // Frenet frame would twist as the spine curves and put the seam of an
        // arm somewhere different at every ring.
        const Vector3 reference = std::fabs(along.X) > 0.9f ? Vector3::Up : Vector3::Right;
        const Vector3 depth = Normalise(Vector3::Cross(along, reference), Vector3::Forward);
        const Vector3 width = Vector3::Cross(depth, along);

        const float c = std::cos(rings[r].roll), s = std::sin(rings[r].roll);
        const Vector3 axisW = width * c + depth * s;
        const Vector3 axisD = depth * c - width * s;

        for (int i = 0; i < sides; ++i)
        {
            const float a = MathHelper::TwoPi * static_cast<float>(i) / static_cast<float>(sides);
            patch.at(static_cast<int>(r), i) = rings[r].centre
                                               + axisW * (std::cos(a) * rings[r].halfWidth)
                                               + axisD * (std::sin(a) * rings[r].halfDepth);
        }
    }
    builder.addSurfacePatch(patch);

    // Caps, as a fan about the ring's own centre rather than a flat polygon:
    // the top of a head and the end of a sleeve are domed, not cut off.
    const auto cap = [&](std::size_t index, float sign) {
        const Vector3 towards = index == 0 ? rings[0].centre - rings[1].centre
                                           : rings[index].centre - rings[index - 1].centre;
        const Vector3 apex = rings[index].centre
                             + Normalise(towards, Vector3::Up)
                                   * (std::min(rings[index].halfWidth, rings[index].halfDepth)
                                      * 0.85f);
        std::vector<Vector3> ring;
        ring.reserve(static_cast<std::size_t>(sides));
        for (int i = 0; i < sides; ++i)
            ring.push_back(patch.at(static_cast<int>(index), i));
        for (int i = 0; i < sides; ++i)
        {
            const Vector3& a = ring[static_cast<std::size_t>(i)];
            const Vector3& b = ring[static_cast<std::size_t>((i + 1) % sides)];
            if (sign > 0.0f) builder.addTriangle(a, b, apex);
            else             builder.addTriangle(b, a, apex);
        }
    };
    if (capStart) cap(0, -1.0f);
    if (capEnd) cap(rings.size() - 1, 1.0f);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

/// The proportions everything is built from, in metres, for one figure.
struct Figure
{
    float height = 1.75f;
    float hip = 0.0f, spine = 0.0f, chest = 0.0f, neck = 0.0f, headCentre = 0.0f, crown = 0.0f;
    float shoulderHalf = 0.0f, hipHalf = 0.0f;
    float shoulderY = 0.0f, elbowY = 0.0f, wristY = 0.0f;
    float kneeY = 0.0f, ankleY = 0.0f;
    float torsoHalfWidth = 0.0f, torsoHalfDepth = 0.0f;
    float limb = 1.0f;   ///< limb-thickness multiplier from the build
};

Figure Proportions(const CharacterLook& look)
{
    const float h = look.height;
    Figure f;
    f.height = h;
    // Canonical adult proportions: the hip at 0.53 of the stature, the shoulder
    // at 0.81, the crown at 1.0. A figure whose head is the wrong fraction of
    // its height reads as a child or a giant however tall it is.
    f.hip        = h * 0.530f;
    f.spine      = h * 0.600f;
    f.chest      = h * 0.700f;
    f.shoulderY  = h * 0.812f;
    f.neck       = h * 0.828f;
    f.headCentre = h * 0.905f;
    f.crown      = h * 0.998f;
    f.elbowY     = h * 0.630f;
    f.wristY     = h * 0.468f;
    f.kneeY      = h * 0.285f;
    f.ankleY     = h * 0.040f;

    // Breadths from adult anthropometry, as fractions of stature: biacromial
    // 0.225, chest 0.172, waist 0.160, hip 0.190, chest depth 0.125. The first
    // version had the chest at 0.116 of stature *half* width -- a 40 cm chest --
    // and the arms hanging outside it, so every figure had wings.
    const float bulk = 0.88f + look.build * 0.26f;
    f.shoulderHalf   = h * 0.1125f * look.shoulders * (0.96f + look.build * 0.08f);
    f.hipHalf        = h * 0.0545f * bulk;
    f.torsoHalfWidth = h * 0.0930f * bulk;
    f.torsoHalfDepth = h * 0.0620f * bulk;
    f.limb           = bulk;
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------

CharacterFactory::CharacterFactory(const MaterialLibrary& materials) : materials_(materials) {}

CharacterLook CharacterFactory::look(Rng& rng, int variant) const
{
    CharacterLook out;
    // Real adult stature is close to normal with a standard deviation of about
    // 7 cm around 1.71 m; two uniforms added is near enough and cannot produce
    // the 2.1 m outlier a single uniform over the whole range eventually does.
    out.height = Metrics::kPersonHeightMin
                 + (rng.range(0.0f, 1.0f) + rng.range(0.0f, 1.0f)) * 0.5f
                       * (Metrics::kPersonHeightMax - Metrics::kPersonHeightMin);
    out.build      = rng.range(0.15f, 0.9f);
    out.shoulders  = rng.range(0.90f, 1.10f);
    out.hairLength = rng.range(0.0f, 1.0f) < 0.42f ? rng.range(0.45f, 1.0f) : rng.range(0.0f, 0.18f);
    out.carriesBag = rng.range(0.0f, 1.0f) < 0.34f;
    out.wearsCoat  = rng.range(0.0f, 1.0f) < 0.72f;
    (void)variant;
    return out;
}

CharacterFactory::Character CharacterFactory::build(const CharacterLook& look,
                                                    bool fullDetail) const
{
    const Figure f = Proportions(look);
    const float h  = f.height;
    const int   sides = fullDetail ? 12 : 7;

    Character out;

    // --- the skeleton -------------------------------------------------------
    // Nineteen bones, in topological order because everything downstream
    // requires it. Influence radii are hand-set: they decide how far a bone's
    // weight reaches, and a limb whose radius covers the other leg produces a
    // figure whose knees are tied together.
    Skeleton& s = out.skeleton;
    const int pelvis = s.add(BoneName::kPelvis, -1, Vector3(0.0f, f.hip, 0.0f), h * 0.100f);
    const int spine  = s.add(BoneName::kSpine, pelvis, Vector3(0.0f, f.spine, 0.0f), h * 0.130f);
    const int chest  = s.add(BoneName::kChest, spine, Vector3(0.0f, f.chest, 0.0f), h * 0.145f);
    const int neck   = s.add(BoneName::kNeck, chest, Vector3(0.0f, f.neck, 0.0f), h * 0.050f);
    s.add(BoneName::kHead, neck, Vector3(0.0f, f.headCentre, 0.0f), h * 0.110f);

    struct Side { float sign; const char* suffix; };
    for (const Side& side : {Side{1.0f, ".R"}, Side{-1.0f, ".L"}})
    {
        const int clavicle =
            s.add(std::string(BoneName::kClavicle) + side.suffix, chest,
                  Vector3(side.sign * h * 0.030f, f.shoulderY, 0.0f), h * 0.075f);
        const int upper =
            s.add(std::string(BoneName::kUpperArm) + side.suffix, clavicle,
                  Vector3(side.sign * f.shoulderHalf, f.shoulderY - h * 0.014f, 0.0f), h * 0.052f);
        const int fore =
            s.add(std::string(BoneName::kForearm) + side.suffix, upper,
                  Vector3(side.sign * f.shoulderHalf, f.elbowY, 0.0f), h * 0.042f);
        s.add(std::string(BoneName::kHand) + side.suffix, fore,
              Vector3(side.sign * f.shoulderHalf, f.wristY, 0.0f), h * 0.060f);
    }
    for (const Side& side : {Side{1.0f, ".R"}, Side{-1.0f, ".L"}})
    {
        const int thigh = s.add(std::string(BoneName::kThigh) + side.suffix, pelvis,
                                Vector3(side.sign * f.hipHalf, f.hip - h * 0.010f, 0.0f),
                                h * 0.072f);
        const int shin  = s.add(std::string(BoneName::kShin) + side.suffix, thigh,
                                Vector3(side.sign * f.hipHalf, f.kneeY, 0.0f), h * 0.062f);
        s.add(std::string(BoneName::kFoot) + side.suffix, shin,
              Vector3(side.sign * f.hipHalf, f.ankleY, 0.0f), h * 0.090f);
    }

    // --- the geometry -------------------------------------------------------
    const Material* skin  = look.skin != nullptr ? look.skin : &materials_.get(MaterialId::Skin);
    const Material* coat  = look.coat != nullptr ? look.coat : &materials_.get(MaterialId::Clothing);
    const Material* legs  = look.trousers != nullptr ? look.trousers : coat;
    const Material* hairM = look.hair != nullptr ? look.hair
                                                 : &materials_.get(MaterialId::PaintedSteelBlack);
    const Material* shoeM = look.shoes != nullptr ? look.shoes
                                                  : &materials_.get(MaterialId::PaintedSteelBlack);

    GeometryCollector collector;
    collector.setRegionKey(0);
    MeshBuilder& body   = collector.builder(coat);
    MeshBuilder& flesh  = collector.builder(skin);
    MeshBuilder& lower  = collector.builder(legs);
    MeshBuilder& hair   = collector.builder(hairM);
    MeshBuilder& shoes  = collector.builder(shoeM);
    body.setTileSize(0.55f);
    flesh.setTileSize(0.30f);
    lower.setTileSize(0.55f);
    hair.setTileSize(0.20f);
    shoes.setTileSize(0.18f);

    // Torso. The section is an ellipse that is wider than it is deep, narrows at
    // the waist and widens again at the chest, and the shoulders are a separate
    // widening at the very top so the deltoids read as mass on the outside of
    // the trunk rather than as the arms starting at the neck.
    const float coatFlare = look.wearsCoat ? 1.10f : 1.0f;
    const float hem       = look.wearsCoat ? f.hip - h * 0.075f : f.hip - h * 0.010f;
    std::vector<Ring> torso = {
        {Vector3(0.0f, hem, 0.0f), f.hipHalf * 1.62f * coatFlare, f.torsoHalfDepth * 1.02f * coatFlare},
        {Vector3(0.0f, f.hip + h * 0.010f, 0.0f), f.torsoHalfWidth * 0.98f * coatFlare,
         f.torsoHalfDepth * 0.98f * coatFlare},
        {Vector3(0.0f, f.hip + h * 0.075f, -h * 0.004f), f.torsoHalfWidth * 0.90f,
         f.torsoHalfDepth * 0.92f},
        {Vector3(0.0f, f.chest - h * 0.020f, -h * 0.002f), f.torsoHalfWidth * 1.04f,
         f.torsoHalfDepth * 1.12f},
        {Vector3(0.0f, f.chest + h * 0.055f, 0.0f), f.torsoHalfWidth * 1.10f,
         f.torsoHalfDepth * 1.10f},
        {Vector3(0.0f, f.shoulderY - h * 0.014f, -h * 0.004f), f.shoulderHalf * 0.98f,
         f.torsoHalfDepth * 0.96f},
        // The trapezius: the top of the trunk is not a flat lid at shoulder
        // width, it slopes in and up to the neck, and that slope is most of what
        // makes a pair of shoulders read as shoulders.
        {Vector3(0.0f, f.shoulderY + h * 0.014f, -h * 0.006f), f.shoulderHalf * 0.62f,
         f.torsoHalfDepth * 0.80f},
        {Vector3(0.0f, f.neck + h * 0.004f, -h * 0.006f), f.shoulderHalf * 0.38f,
         f.torsoHalfDepth * 0.56f},
    };
    Sweep(body, torso, sides, true, true, 1.45f);

    // Neck and head. The head is not a sphere: the cranium is widest above the
    // ears and the jaw tapers forward and down to a chin, and that taper is the
    // whole silhouette from any distance a street is seen at.
    const float headHalf = h * 0.0442f;
    std::vector<Ring> head = {
        {Vector3(0.0f, f.neck - h * 0.020f, -h * 0.004f), h * 0.0330f, h * 0.0345f},
        {Vector3(0.0f, f.neck + h * 0.026f, h * 0.001f), h * 0.0272f, h * 0.0288f},
        {Vector3(0.0f, f.headCentre - h * 0.048f, h * 0.008f), h * 0.0330f, h * 0.0395f},
        {Vector3(0.0f, f.headCentre - h * 0.024f, h * 0.004f), h * 0.0410f, h * 0.0480f},
        {Vector3(0.0f, f.headCentre + h * 0.004f, 0.0f), headHalf, h * 0.0530f},
        {Vector3(0.0f, f.headCentre + h * 0.034f, -h * 0.004f), headHalf * 0.95f, h * 0.0500f},
        {Vector3(0.0f, f.crown - h * 0.012f, -h * 0.008f), headHalf * 0.66f, h * 0.0345f},
    };
    Sweep(flesh, head, sides, true, true, 1.55f);
    // The nose. Two centimetres of geometry that costs nothing and is the
    // difference between a head and an egg in profile.
    flesh.addTriangle(Vector3(-h * 0.007f, f.headCentre + h * 0.006f, h * 0.052f),
                      Vector3(h * 0.007f, f.headCentre + h * 0.006f, h * 0.052f),
                      Vector3(0.0f, f.headCentre - h * 0.008f, h * 0.062f));
    flesh.addTriangle(Vector3(h * 0.007f, f.headCentre + h * 0.006f, h * 0.052f),
                      Vector3(0.0f, f.headCentre - h * 0.020f, h * 0.050f),
                      Vector3(0.0f, f.headCentre - h * 0.008f, h * 0.062f));
    flesh.addTriangle(Vector3(0.0f, f.headCentre - h * 0.020f, h * 0.050f),
                      Vector3(-h * 0.007f, f.headCentre + h * 0.006f, h * 0.052f),
                      Vector3(0.0f, f.headCentre - h * 0.008f, h * 0.062f));

    // Eyes. Four flattened discs -- iris and brow, twice -- which is nothing at
    // arm's length and everything at five metres: a head with two dark marks in
    // the right place reads as a face, and a head without them reads as an egg
    // however well it is modelled.
    {
        MeshBuilder& eyes = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
        eyes.setTileSize(0.05f);
        for (const float side : {-1.0f, 1.0f})
        {
            const Vector3 at(side * h * 0.0172f, f.headCentre + h * 0.0030f, h * 0.0500f);
            eyes.addDiscFacing(at, Vector3(side * 0.30f, -0.10f, 1.0f), h * 0.0040f, 5);
            // A brow, as a short bar rather than a second eye: the shadow under
            // a brow ridge is what puts an expression on a face at ten metres,
            // and a disc there gives the figure four eyes.
            const Vector3 brow(at.X, at.Y + h * 0.0115f, at.Z - h * 0.0040f);
            eyes.addBox(Vector3(brow.X - h * 0.0105f, brow.Y - h * 0.0016f, brow.Z),
                        Vector3(brow.X + h * 0.0105f, brow.Y + h * 0.0016f, brow.Z + h * 0.0035f));
        }
    }

    // Hair: a cap over the cranium, thicker at the back, plus a mass at the
    // nape when it is long.
    std::vector<Ring> cap = {
        {Vector3(0.0f, f.headCentre - h * 0.020f, -h * 0.006f), headHalf * 1.045f, h * 0.0510f},
        {Vector3(0.0f, f.headCentre + h * 0.010f, -h * 0.006f), headHalf * 1.055f, h * 0.0545f},
        {Vector3(0.0f, f.headCentre + h * 0.040f, -h * 0.010f), headHalf * 1.00f, h * 0.0510f},
        {Vector3(0.0f, f.crown - h * 0.008f, -h * 0.012f), headHalf * 0.68f, h * 0.0350f},
    };
    Sweep(hair, cap, sides, false, true, 1.35f);
    if (look.hairLength > 0.30f)
    {
        const float drop = Lerp(f.headCentre - h * 0.030f, f.shoulderY - h * 0.010f,
                                look.hairLength);
        std::vector<Ring> tail = {
            {Vector3(0.0f, f.headCentre + h * 0.020f, -h * 0.016f), headHalf * 0.94f, h * 0.0330f},
            {Vector3(0.0f, f.headCentre - h * 0.020f, -h * 0.026f), headHalf * 0.98f, h * 0.0290f},
            {Vector3(0.0f, drop, -h * 0.030f), headHalf * 0.86f, h * 0.0215f},
        };
        Sweep(hair, tail, sides, false, true, 1.35f);
    }

    for (const float side : {-1.0f, 1.0f})
    {
        // Arm. The deltoid, then the taper to the elbow, then the forearm's own
        // belly and the narrow wrist. Sleeve to the wrist when a coat is worn,
        // so the skin starts at the hand.
        // The arm hangs *inside* the acromion, not on it.
        const float armX = side * f.shoulderHalf * 0.83f;
        const float upperR = h * 0.0300f * f.limb;
        std::vector<Ring> arm = {
            {Vector3(armX * 0.62f, f.shoulderY + h * 0.026f, 0.0f), upperR * 1.02f, upperR * 1.06f},
            {Vector3(armX * 0.90f, f.shoulderY + h * 0.004f, 0.0f), upperR * 1.36f, upperR * 1.34f},
            {Vector3(armX * 1.03f, f.shoulderY - h * 0.044f, 0.0f), upperR * 1.18f, upperR * 1.22f},
            {Vector3(armX * 1.01f, Lerp(f.shoulderY, f.elbowY, 0.55f), h * 0.002f),
             upperR * 0.98f, upperR * 1.02f},
            {Vector3(armX * 0.98f, f.elbowY - h * 0.010f, h * 0.004f), upperR * 0.84f,
             upperR * 0.90f},
        };
        Sweep(look.wearsCoat ? body : flesh, arm, sides, true, false, 1.55f);

        const float foreR = h * 0.0242f * f.limb;
        // A hanging forearm turns slightly in and forward -- nobody stands with
        // their arms pinned to their sides like a plank.
        std::vector<Ring> forearm = {
            {Vector3(armX * 0.98f, f.elbowY + h * 0.014f, h * 0.004f), foreR * 1.22f,
             foreR * 1.28f},
            {Vector3(armX * 0.94f, Lerp(f.elbowY, f.wristY, 0.35f), h * 0.010f), foreR * 1.10f,
             foreR * 1.14f},
            {Vector3(armX * 0.90f, f.wristY + h * 0.012f, h * 0.018f), foreR * 0.72f,
             foreR * 0.80f},
        };
        Sweep(look.wearsCoat ? body : flesh, forearm, sides, true, false, 1.55f);

        // The hand: a flattened mitt with a thumb mass. At the three to six
        // metres these are seen from, a mitt with a thumb is a hand and five
        // fingers are four hundred wasted triangles.
        std::vector<Ring> hand = {
            {Vector3(armX * 0.90f, f.wristY + h * 0.010f, h * 0.018f), foreR * 0.74f,
             foreR * 0.82f},
            {Vector3(armX * 0.89f, f.wristY - h * 0.012f, h * 0.020f), foreR * 0.96f,
             foreR * 1.24f},
            {Vector3(armX * 0.88f, f.wristY - h * 0.046f, h * 0.022f), foreR * 0.84f,
             foreR * 1.20f},
            {Vector3(armX * 0.87f, f.wristY - h * 0.068f, h * 0.020f), foreR * 0.46f,
             foreR * 0.68f},
        };
        Sweep(flesh, hand, sides, true, true, 1.35f);

        // Leg. A thigh that is thickest just below the hip, a knee, a calf with
        // its belly at the top third, and an ankle.
        const float legX = side * f.hipHalf;
        const float thighR = h * 0.0472f * f.limb;
        std::vector<Ring> thigh = {
            {Vector3(legX, f.hip - h * 0.014f, 0.0f), thighR * 1.16f, thighR * 1.20f},
            {Vector3(legX, Lerp(f.hip, f.kneeY, 0.30f), 0.0f), thighR * 1.04f, thighR * 1.08f},
            {Vector3(legX, Lerp(f.hip, f.kneeY, 0.72f), 0.0f), thighR * 0.84f, thighR * 0.90f},
            {Vector3(legX, f.kneeY - h * 0.008f, h * 0.002f), thighR * 0.76f, thighR * 0.80f},
        };
        Sweep(lower, thigh, sides, true, false, 1.55f);

        const float calfR = h * 0.0345f * f.limb;
        std::vector<Ring> calf = {
            {Vector3(legX, f.kneeY + h * 0.020f, h * 0.002f), calfR * 1.10f, calfR * 1.14f},
            {Vector3(legX, Lerp(f.kneeY, f.ankleY, 0.28f), -h * 0.004f), calfR * 1.06f,
             calfR * 1.18f},
            {Vector3(legX, Lerp(f.kneeY, f.ankleY, 0.66f), -h * 0.002f), calfR * 0.76f,
             calfR * 0.84f},
            {Vector3(legX, f.ankleY + h * 0.014f, 0.0f), calfR * 0.62f, calfR * 0.66f},
        };
        Sweep(lower, calf, sides, true, false, 1.55f);

        // The shoe: a sole, a toe box that tapers, and a heel. A cuboid is what
        // made the old figures look like they were standing in two boxes.
        const float shoeLength = h * 0.150f;
        std::vector<Ring> shoe = {
            {Vector3(legX, f.ankleY + h * 0.022f, -shoeLength * 0.30f), calfR * 0.66f,
             calfR * 0.60f},
            {Vector3(legX, f.ankleY - h * 0.004f, -shoeLength * 0.24f), calfR * 0.70f,
             calfR * 0.72f},
            {Vector3(legX, h * 0.017f, -shoeLength * 0.12f), calfR * 0.74f, calfR * 0.52f},
            {Vector3(legX, h * 0.016f, shoeLength * 0.28f), calfR * 0.76f, calfR * 0.60f},
            {Vector3(legX, h * 0.013f, shoeLength * 0.60f), calfR * 0.60f, calfR * 0.40f},
        };
        Sweep(shoes, shoe, sides, true, true, 1.30f);
    }

    if (look.carriesBag)
    {
        // A shoulder bag on the right, hanging at the hip. Anything a person is
        // carrying breaks the symmetry that makes a crowd read as clones.
        const float bx = f.shoulderHalf * 0.92f;
        MeshBuilder& bag = collector.builder(&materials_.get(MaterialId::Timber));
        bag.setTileSize(0.4f);
        std::vector<Ring> body2 = {
            {Vector3(bx * 0.94f, f.hip + h * 0.042f, h * 0.014f), h * 0.022f, h * 0.052f},
            {Vector3(bx * 0.99f, f.hip - h * 0.014f, h * 0.012f), h * 0.028f, h * 0.062f},
            {Vector3(bx * 0.99f, f.hip - h * 0.050f, h * 0.010f), h * 0.023f, h * 0.054f},
        };
        Sweep(bag, body2, sides, true, true, 1.20f);
        const Vector3 shoulderPoint(bx * 0.40f, f.shoulderY + h * 0.012f, -h * 0.010f);
        const Vector3 bagTop(bx * 0.94f, f.hip + h * 0.042f, h * 0.012f);
        bag.addCylinderBetween(shoulderPoint, bagTop, h * 0.0065f, 5);
        bag.addCylinderBetween(Vector3(shoulderPoint.X, shoulderPoint.Y, shoulderPoint.Z + h * 0.030f),
                               Vector3(bagTop.X, bagTop.Y, bagTop.Z + h * 0.024f), h * 0.0065f, 5);
    }

    for (GeometryCollector::Batch& batch : collector.take())
    {
        if (batch.mesh.empty()) continue;
        out.parts.push_back(Character::Part{batch.material, out.skeleton.bind(batch.mesh)});
    }
    return out;
}

// ---------------------------------------------------------------------------

namespace {

using System::TimeSpan;

TimeSpan Seconds(float value)
{
    return TimeSpan::FromTicks(static_cast<std::int64_t>(static_cast<double>(value) * 1.0e7));
}

/// One bone track from a function of the cycle phase. Twelve keys a cycle: the
/// player interpolates between them, and a walk sampled any finer is a walk
/// nobody can tell from this one.
///
/// A keyframe *replaces* the bone's whole local transform, translation
/// included, so a track that leaves the translation at zero collapses its bone
/// onto its parent. The first version of this did exactly that and produced a
/// figure with no legs and one arm growing out of its neck. So the default here
/// is the bone's own bind-pose offset, and a track that does not care about
/// translation still holds the skeleton together.
BoneTrackEXT Track(const Skeleton& skeleton, int bone, float duration, int keys,
                   const std::function<Quaternion(float)>& rotation,
                   const std::function<Vector3(float)>& translation = nullptr)
{
    BoneTrackEXT track;
    track.BoneIndex = bone;
    if (bone < 0) return track;
    const Vector3 rest = skeleton[bone].head
                         - (skeleton[bone].parent < 0 ? Vector3::Zero
                                                      : skeleton[skeleton[bone].parent].head);
    for (int i = 0; i <= keys; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(keys);
        KeyframeEXT key;
        key.Time        = Seconds(duration * t);
        key.Rotation    = rotation ? rotation(t) : Quaternion::Identity;
        key.Translation = translation ? translation(t) : rest;
        track.Keys.push_back(key);
    }
    return track;
}

Quaternion PitchQ(float radians) { return Quaternion::CreateFromAxisAngle(Vector3::Right, radians); }

}  // namespace

CharacterFactory::Clips CharacterFactory::clips(const Skeleton& skeleton, float height,
                                                 float strideSeconds)
{
    Clips out;
    const float cycle = std::max(strideSeconds, 0.3f);
    constexpr int kKeys = 12;

    const auto bone = [&](const char* base, const char* suffix) {
        return skeleton.find(std::string(base) + suffix);
    };

    // --- walk ---------------------------------------------------------------
    // One sine through the cycle, two legs in opposite phase, arms opposite the
    // leg on the same side. Everything else is a detail on top of that: the
    // knee only bends backwards, the ankle rolls through the step, and the
    // pelvis rises twice per cycle at the middle of each step.
    out.walk.Duration = Seconds(cycle);
    for (int side = 0; side < 2; ++side)
    {
        const char* suffix = side == 0 ? ".R" : ".L";
        const float offset = side == 0 ? 0.0f : 0.5f;
        const auto phase = [offset](float t) { return (t + offset) * MathHelper::TwoPi; };

        out.walk.Tracks.push_back(Track(skeleton, bone(BoneName::kThigh, suffix), cycle, kKeys,
                                        [&](float t) { return PitchQ(std::sin(phase(t)) * 0.44f); }));
        out.walk.Tracks.push_back(
            Track(skeleton, bone(BoneName::kShin, suffix), cycle, kKeys, [&](float t) {
                // The knee bends when the leg is behind the body and straightens
                // as it swings through. A knee that bends forward is the most
                // recognisable animation error there is.
                const float swing = std::sin(phase(t));
                const float lift  = std::max(0.0f, -std::cos(phase(t)));
                return PitchQ(-std::max(0.0f, -swing) * 0.55f - lift * 0.62f);
            }));
        out.walk.Tracks.push_back(
            Track(skeleton, bone(BoneName::kFoot, suffix), cycle, kKeys, [&](float t) {
                const float swing = std::sin(phase(t));
                return PitchQ(0.10f + swing * 0.30f);
            }));

        const char* armSuffix = side == 0 ? ".L" : ".R";
        out.walk.Tracks.push_back(
            Track(skeleton, bone(BoneName::kUpperArm, armSuffix), cycle, kKeys,
                  [&](float t) { return PitchQ(std::sin(phase(t)) * 0.30f); }));
        out.walk.Tracks.push_back(
            Track(skeleton, bone(BoneName::kForearm, armSuffix), cycle, kKeys, [&](float t) {
                return PitchQ(-0.16f - std::max(0.0f, std::sin(phase(t))) * 0.34f);
            }));
    }
    // The pelvis: two rises per cycle, and a roll onto the supporting leg.
    {
        const int pelvis = skeleton.find(BoneName::kPelvis);
        const Vector3 pelvisRest = pelvis >= 0 ? skeleton[pelvis].head : Vector3::Zero;
        BoneTrackEXT track;
        track.BoneIndex = pelvis;
        for (int i = 0; i <= kKeys; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kKeys);
            KeyframeEXT key;
            key.Time = Seconds(cycle * t);
            key.Translation = pelvisRest
                              + Vector3(0.0f,
                                        std::fabs(std::cos(t * MathHelper::TwoPi)) * height * 0.014f
                                            - height * 0.008f,
                                        0.0f);
            key.Rotation = Quaternion::CreateFromAxisAngle(
                Vector3::Forward, std::sin(t * MathHelper::TwoPi) * 0.045f);
            track.Keys.push_back(key);
        }
        out.walk.Tracks.push_back(track);
        // A little counter-rotation through the trunk, which is what stops a
        // walk looking like a rigid body being carried along.
        out.walk.Tracks.push_back(
            Track(skeleton, skeleton.find(BoneName::kChest), cycle, kKeys, [](float t) {
                return Quaternion::CreateFromAxisAngle(Vector3::Up,
                                                       -std::sin(t * MathHelper::TwoPi) * 0.075f);
            }));
    }

    // --- idle ---------------------------------------------------------------
    // Standing is not standing still. A four-second sway with a breath in it,
    // and the two people either side of a crossing on different offsets, which
    // between them are the whole difference between a queue of waiting
    // pedestrians and a row of statues.
    constexpr float kIdle = 4.0f;
    out.idle.Duration = Seconds(kIdle);
    {
        const int pelvis = skeleton.find(BoneName::kPelvis);
        const Vector3 rest = pelvis >= 0 ? skeleton[pelvis].head : Vector3::Zero;
        out.idle.Tracks.push_back(
            Track(skeleton, pelvis, kIdle, 8, nullptr, [height, rest](float t) {
                return rest
                       + Vector3(0.0f, std::sin(t * MathHelper::TwoPi) * height * 0.0035f, 0.0f);
            }));
    }
    out.idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kChest), kIdle, 8, [](float t) {
        return Quaternion::CreateFromAxisAngle(Vector3::Up,
                                               std::sin(t * MathHelper::TwoPi + 0.7f) * 0.030f);
    }));
    for (const char* suffix : {".R", ".L"})
    {
        out.idle.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, suffix), kIdle, 8, [](float t) {
            return PitchQ(0.05f + std::sin(t * MathHelper::TwoPi) * 0.020f);
        }));
        out.idle.Tracks.push_back(
            Track(skeleton, bone(BoneName::kForearm, suffix), kIdle, 8,
                  [](float) { return PitchQ(-0.12f); }));
    }
    return out;
}

}  // namespace CnaStreet
