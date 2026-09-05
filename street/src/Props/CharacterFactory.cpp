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
#include <initializer_list>
#include <utility>
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
    out.wearsHat   = rng.range(0.0f, 1.0f) < 0.15f;
    (void)variant;
    return out;
}

CharacterFactory::Character CharacterFactory::build(const CharacterLook& look,
                                                    bool fullDetail) const
{
    const Figure f = Proportions(look);
    const float h  = f.height;
    // Fourteen, not twelve. A limb is the one place a low section count shows
    // as a *silhouette*: a dodecagonal upper arm at three metres has visible
    // flats down its edge, and the two extra sections cost about 250 triangles
    // on a figure that now costs three draw calls at distance instead of six.
    const int   sides = fullDetail ? 14 : 7;

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
    // At distance the material set collapses.
    //
    // Each distinct material on a skinned figure is a draw call of its own --
    // bone palettes cannot be instanced -- so a six-material person costs six
    // draws whether they are three metres away or eighty. Fifty-eight of them
    // was three hundred and fifty draw calls, the largest single item in the
    // frame.
    // What survives at twenty metres is head, coat and legs: a figure is fifty
    // pixels tall there and the shoes are three of them. So the far figure puts
    // its shoes and its bag on the trouser material, its eyes on the hair, and
    // its hair on the trousers -- hair is brown or black and trousers are navy,
    // grey or brown, so dark stays dark. Three draws rather than six for a
    // difference nobody can resolve. The near figure keeps all of them, because
    // at three metres the shoes are the thing that says these are clothes
    // rather than a paint job.
    const Material* farShoes = fullDetail ? shoeM : legs;
    const Material* farBag   = fullDetail ? &materials_.get(MaterialId::Timber) : legs;
    const Material* farHair  = fullDetail ? hairM : legs;
    const Material* eyeM     = fullDetail ? &materials_.get(MaterialId::PaintedSteelBlack) : legs;

    GeometryCollector collector;
    collector.setRegionKey(0);
    MeshBuilder& body   = collector.builder(coat);
    MeshBuilder& flesh  = collector.builder(skin);
    MeshBuilder& lower  = collector.builder(legs);
    MeshBuilder& hair   = collector.builder(farHair);
    MeshBuilder& shoes  = collector.builder(farShoes);
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
        MeshBuilder& eyes = collector.builder(eyeM);
        eyes.setTileSize(0.05f);
        for (const float side : {-1.0f, 1.0f})
        {
            const Vector3 at(side * h * 0.0195f, f.headCentre + h * 0.0030f, h * 0.0500f);
            eyes.addDiscFacing(at, Vector3(side * 0.30f, -0.10f, 1.0f), h * 0.0038f, 5);
            // A brow, as a short bar rather than a second eye: the shadow under
            // a brow ridge is what puts an expression on a face at ten metres,
            // and a disc there gives the figure four eyes.
            //
            // Short. A brow half as long as the face is wide leaves a 2 cm gap
            // between the pair, and two dark bars 2 cm apart on a 15 cm head
            // are not two brows -- they are one band across the eyes, which is
            // a welding mask. A real brow is about 4.5 cm on a 15 cm face, so
            // the pair covers a third of it and the gap is as wide as either.
            const Vector3 brow(at.X, at.Y + h * 0.0118f, at.Z - h * 0.0040f);
            eyes.addBox(Vector3(brow.X - h * 0.0080f, brow.Y - h * 0.0014f, brow.Z),
                        Vector3(brow.X + h * 0.0080f, brow.Y + h * 0.0014f, brow.Z + h * 0.0032f));
        }
    }

    // Hair: a cap over the cranium, thicker at the back, plus a mass at the
    // nape when it is long.
    // The hairline. Its lowest ring used to sit at headCentre - 0.020h with
    // enough depth to reach the front of the face, so what a horizontal ring
    // drew there was a dark band straight across the eyes: every pedestrian in
    // the city was wearing a visor. A hairline is *above* the brow at the front
    // and comes down over the temples at the sides, and a ring cannot say both
    // -- so the lowest ring is raised to brow height and pulled back, which
    // covers the temples and leaves the face clear. Getting the front edge
    // right matters more than the sides, because the front is the part with a
    // face under it.
    //
    // The other half is thickness. At 1.045 times the cranium the cap was a
    // 3 mm shell, which is what a hair *fibre* is and not what a head of hair
    // is: from the front it disappeared inside the skull and left a bald egg
    // with a dark rim at the temples. Hair stands 1 to 2 cm off a cranium, and
    // at 1.14 it protrudes 11 mm at the temples and passes in front of the
    // forehead above the hairline, which is where hair is.
    std::vector<Ring> cap = {
        {Vector3(0.0f, f.headCentre + h * 0.014f, -h * 0.008f), headHalf * 1.14f, h * 0.0520f},
        {Vector3(0.0f, f.headCentre + h * 0.038f, -h * 0.008f), headHalf * 1.15f, h * 0.0555f},
        {Vector3(0.0f, f.crown - h * 0.006f, -h * 0.010f), headHalf * 0.78f, h * 0.0400f},
    };
    Sweep(hair, cap, sides, false, true, 1.35f);
    if (look.hairLength > 0.30f)
    {
        const float drop = Lerp(f.headCentre - h * 0.030f, f.shoulderY - h * 0.010f,
                                look.hairLength);
        std::vector<Ring> tail = {
            {Vector3(0.0f, f.headCentre + h * 0.022f, -h * 0.022f), headHalf * 0.96f, h * 0.0340f},
            {Vector3(0.0f, f.headCentre - h * 0.018f, -h * 0.030f), headHalf * 1.00f, h * 0.0300f},
            {Vector3(0.0f, drop, -h * 0.034f), headHalf * 0.88f, h * 0.0225f},
        };
        Sweep(hair, tail, sides, false, true, 1.35f);
    }

    for (const float side : {-1.0f, 1.0f})
    {
        // Arm. The deltoid, then the taper to the elbow, then the forearm's own
        // belly and the narrow wrist. Sleeve to the wrist when a coat is worn,
        // so the skin starts at the hand.
        // The arm hangs *inside* the acromion, not on it.
        const float armX = side * f.shoulderHalf * 0.86f;
        const float upperR = h * 0.0300f * f.limb;
        // The arm starts at the acromion and goes *down*. It used to start
        // 2.6 cm above the shoulder line and 12 cm from the centreline -- which
        // is on top of the trapezius and beside the neck -- and then sweep out
        // and down. Two things went wrong with that and both were visible from
        // across the street.
        //
        // The shape: a capped section rising above the shoulder next to the neck
        // and tapering outward is a wing. Every figure on the street had a pair
        // of pointed shoulder pads reaching to ear height, which is the single
        // loudest thing a human silhouette can get wrong -- and it made the
        // shoulders read as pinched, which in turn made the head look enormous.
        //
        // The frame: `Sweep` picks its reference axis from the spine direction
        // and switches when the spine is within 25 degrees of world X. From a
        // ring above the shoulder to one outboard of it, the spine *is* nearly
        // horizontal, so the deltoid's section was built on a different frame
        // from the rest of the arm and rotated a quarter turn against it. Now
        // every segment of the arm descends more than it spreads, so the whole
        // limb shares one frame.
        //
        // The joint itself is covered by the trunk: the torso's top rings
        // already carry a shoulder cap and a trapezius slope, and the arm's
        // start cap sits inside them.
        std::vector<Ring> arm = {
            {Vector3(armX * 0.88f, f.shoulderY - h * 0.006f, 0.0f), upperR * 1.22f,
             upperR * 1.26f},
            // A third ring across the deltoid, because two describe a cone and
            // a shoulder is not one: the transition read as a pair of flat
            // plates set at an angle to each other.
            {Vector3(armX * 0.97f, f.shoulderY - h * 0.022f, 0.0f), upperR * 1.32f,
             upperR * 1.33f},
            {Vector3(armX * 1.02f, f.shoulderY - h * 0.040f, 0.0f), upperR * 1.34f,
             upperR * 1.32f},
            {Vector3(armX * 1.02f, Lerp(f.shoulderY, f.elbowY, 0.50f), h * 0.002f),
             upperR * 1.00f, upperR * 1.04f},
            {Vector3(armX * 0.99f, f.elbowY - h * 0.010f, h * 0.004f), upperR * 0.84f,
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
        // Flattened the right way round. A hand hanging beside a thigh presents
        // its edge to the viewer: about 3.5 cm across the figure and 10 cm from
        // knuckles to heel of the palm. The old sections were 8 cm across and
        // 10 cm deep, which is a boxing glove, and a pale boxing glove at each
        // hip was one of the two things that made these figures read as
        // mannequins.
        std::vector<Ring> hand = {
            {Vector3(armX * 0.90f, f.wristY + h * 0.010f, h * 0.018f), foreR * 0.66f,
             foreR * 0.80f},
            {Vector3(armX * 0.89f, f.wristY - h * 0.012f, h * 0.022f), foreR * 0.54f,
             foreR * 1.20f},
            {Vector3(armX * 0.88f, f.wristY - h * 0.046f, h * 0.024f), foreR * 0.50f,
             foreR * 1.16f},
            {Vector3(armX * 0.87f, f.wristY - h * 0.068f, h * 0.020f), foreR * 0.32f,
             foreR * 0.66f},
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

    if (look.wearsCoat)
    {
        // A collar: a short ring standing off the neck, turned back. It is
        // what separates a coat from a painted torso at the neckline, which is
        // where the eye lands after the face.
        std::vector<Ring> collar = {
            {Vector3(0.0f, f.neck - h * 0.006f, -h * 0.004f), f.shoulderHalf * 0.36f,
             f.torsoHalfDepth * 0.58f},
            {Vector3(0.0f, f.neck + h * 0.026f, -h * 0.010f), f.shoulderHalf * 0.40f,
             f.torsoHalfDepth * 0.66f},
            {Vector3(0.0f, f.neck + h * 0.034f, -h * 0.012f), f.shoulderHalf * 0.34f,
             f.torsoHalfDepth * 0.56f},
        };
        Sweep(body, collar, sides, false, false, 1.4f);
    }

    if (look.wearsHat)
    {
        // A flat cap: a crown a little wider than the head, sitting low, and a
        // peak forward. A hat is a silhouette change from any distance, which
        // is exactly what a crowd of otherwise similar heads needs.
        // In the trouser cloth, not the hair colour: a cap the colour of the
        // hair under it is a bigger head.
        MeshBuilder& hat = collector.builder(legs);
        std::vector<Ring> crown = {
            {Vector3(0.0f, f.headCentre + h * 0.030f, -h * 0.004f), headHalf * 1.18f, h * 0.0560f},
            {Vector3(0.0f, f.headCentre + h * 0.052f, -h * 0.006f), headHalf * 1.20f, h * 0.0570f},
            {Vector3(0.0f, f.crown + h * 0.006f, -h * 0.010f), headHalf * 0.80f, h * 0.0400f},
        };
        Sweep(hat, crown, sides, false, true, 1.35f);
        hat.addBox(Vector3(-h * 0.040f, f.headCentre + h * 0.028f, h * 0.045f),
                   Vector3(h * 0.040f, f.headCentre + h * 0.034f, h * 0.085f));
    }

    if (look.carriesBag)
    {
        // A shoulder bag on the right, hanging at the hip. Anything a person is
        // carrying breaks the symmetry that makes a crowd read as clones.
        const float bx = f.shoulderHalf * 0.92f;
        MeshBuilder& bag = collector.builder(farBag);
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

namespace {

/// A curve through keypoints over one cycle, sampled with a cosine ease
/// between them, and periodic: the last key is the first. This is how a
/// gait is described -- a knee is flexed so much at heel strike, so much at
/// mid-swing -- rather than as a sine, which has one shape and a walk has
/// several.
class GaitCurve
{
public:
    GaitCurve(std::initializer_list<std::pair<float, float>> keys) : keys_(keys) {}
    [[nodiscard]] float at(float t) const
    {
        t -= std::floor(t);
        for (std::size_t i = 0; i + 1 < keys_.size(); ++i)
        {
            const auto& [t0, v0] = keys_[i];
            const auto& [t1, v1] = keys_[i + 1];
            if (t >= t0 && t <= t1)
            {
                const float u = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
                const float e = 0.5f - 0.5f * std::cos(u * MathHelper::Pi);
                return v0 + (v1 - v0) * e;
            }
        }
        return keys_.back().second;
    }

private:
    std::vector<std::pair<float, float>> keys_;
};

Quaternion YawQ(float radians) { return Quaternion::CreateFromAxisAngle(Vector3::Up, radians); }
/// About +Z, the figure's own forward, rather than XNA's `Forward`, which is
/// -Z: a positive roll carries a hanging limb's far end toward +X, the
/// figure's left.
Quaternion RollQ(float radians) { return Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), radians); }

/// One walk cycle. `t` runs 0..1 from the right heel strike to the next;
/// the left leg is the same half a cycle later. Signs follow the skeleton:
/// a positive pitch swings a bone's far end backward, so the thigh pitches
/// negative to come forward, the shin positive to bend the knee (the foot
/// goes back), the foot positive to point the toes down; a positive yaw
/// turns to the left.
struct WalkStyle
{
    float stride = 1.0f;    ///< thigh swing, as a factor of the plain walk's
    float arm    = 0.28f;   ///< upper-arm swing amplitude, radians
    float lean   = 0.04f;   ///< forward lean of the trunk, radians
    float bob    = 1.0f;    ///< vertical bob, as a factor
    float lag    = 0.03f;   ///< how far the arms trail the legs, in cycles
};

AnimationClip WalkClip(const Skeleton& skeleton, float height, float cycle, const WalkStyle& style)
{
    constexpr int kKeys = 24;
    AnimationClip walk;
    walk.Duration = Seconds(cycle);
    const auto bone = [&](const char* base, const char* suffix) {
        return skeleton.find(std::string(base) + suffix);
    };

    // The thigh goes forward to heel strike, back through stance to toe-off
    // at six tenths of the cycle, and forward again through swing. The knee
    // is nearly straight at heel strike, takes the weight with a little
    // flex, straightens at mid-stance, and bends sixty degrees at mid-swing
    // to clear the ground. The ankle lands toes-up, flattens, and pushes
    // off toes-down.
    const GaitCurve thigh{{0.00f, 0.40f}, {0.30f, 0.05f}, {0.60f, -0.24f}, {0.85f, 0.30f},
                          {1.00f, 0.40f}};
    const GaitCurve knee{{0.00f, 0.10f}, {0.15f, 0.30f}, {0.42f, 0.08f}, {0.60f, 0.50f},
                         {0.74f, 1.00f}, {0.90f, 0.30f}, {1.00f, 0.10f}};
    const GaitCurve ankle{{0.00f, -0.14f}, {0.12f, 0.06f}, {0.40f, 0.00f}, {0.60f, 0.42f},
                          {0.75f, 0.10f}, {0.90f, -0.16f}, {1.00f, -0.14f}};
    // The arms swing against the legs and trail them a little, and the
    // elbow bends most when the arm is forward.
    const GaitCurve arm{{0.00f, 1.0f}, {0.50f, -1.0f}, {1.00f, 1.0f}};
    const GaitCurve elbow{{0.00f, 0.0f}, {0.50f, 1.0f}, {1.00f, 0.0f}};

    for (int side = 0; side < 2; ++side)
    {
        const char* suffix = side == 0 ? ".R" : ".L";
        const float offset = side == 0 ? 0.0f : 0.5f;
        walk.Tracks.push_back(Track(skeleton, bone(BoneName::kThigh, suffix), cycle, kKeys,
                                    [&](float t) { return PitchQ(-thigh.at(t + offset) * style.stride); }));
        walk.Tracks.push_back(Track(skeleton, bone(BoneName::kShin, suffix), cycle, kKeys,
                                    [&](float t) { return PitchQ(knee.at(t + offset) * (0.85f + 0.15f * style.stride)); }));
        walk.Tracks.push_back(Track(skeleton, bone(BoneName::kFoot, suffix), cycle, kKeys,
                                    [&](float t) { return PitchQ(ankle.at(t + offset)); }));

        // The arm on this side swings with the other leg: the right arm is
        // back when the right leg is forward. A hand's breadth of abduction
        // keeps the swinging arm off the hip, and it is the right arm that
        // rolls negative because the right is the figure's -X.
        const float abduct = side == 0 ? -0.09f : 0.09f;
        walk.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, suffix), cycle, kKeys,
                                    [&](float t) {
            const float swing = arm.at(t + offset - style.lag);
            return RollQ(abduct) * PitchQ(0.04f + swing * style.arm);
        }));
        walk.Tracks.push_back(Track(skeleton, bone(BoneName::kForearm, suffix), cycle, kKeys,
                                    [&](float t) {
            return PitchQ(-0.20f - elbow.at(t + offset - style.lag) * (0.12f + style.arm * 0.45f));
        }));
    }

    // The pelvis: two rises per cycle, highest at mid-stance; a sway onto the
    // supporting leg once per cycle; a yaw that carries the swinging hip
    // forward and a roll that drops the swinging side. The right is -X.
    {
        const int pelvis = skeleton.find(BoneName::kPelvis);
        const Vector3 pelvisRest = pelvis >= 0 ? skeleton[pelvis].head : Vector3::Zero;
        walk.Tracks.push_back(Track(skeleton, pelvis, cycle, kKeys,
            [&](float t) {
                return YawQ(0.06f * std::cos(t * MathHelper::TwoPi))
                       * RollQ(std::sin(t * MathHelper::TwoPi) * 0.04f);
            },
            [&](float t) {
                const float rise = std::fabs(std::cos(t * MathHelper::TwoPi)) * height * 0.013f
                                   - height * 0.0075f;
                return pelvisRest
                       + Vector3(-0.016f * std::sin(t * MathHelper::TwoPi), rise * style.bob, 0.0f);
            }));
        // The trunk counter-rotates and leans into the walk; the head turns
        // back against the trunk so it stays pointed where the person is
        // going, which is what a head does.
        walk.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kChest), cycle, kKeys, [&](float t) {
            return YawQ(-0.09f * std::cos(t * MathHelper::TwoPi)) * PitchQ(-style.lean);
        }));
        walk.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kNeck), cycle, kKeys, [&](float t) {
            return YawQ(0.03f * std::cos(t * MathHelper::TwoPi)) * PitchQ(style.lean * 0.5f);
        }));
        walk.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kHead), cycle, kKeys, [&](float t) {
            return YawQ(0.02f * std::cos(t * MathHelper::TwoPi)) * PitchQ(style.lean * 0.4f);
        }));
    }
    return walk;
}

/// Standing still, three ways. Every one of them keeps moving a little:
/// a person who is not moving at all is a mannequin, and a queue of them
/// on one offset is a row of mannequins.
enum class IdleKind { Look, Phone, Hands };

AnimationClip IdleClip(const Skeleton& skeleton, float height, IdleKind kind)
{
    constexpr float kIdle = 6.0f;
    constexpr int kKeys = 18;
    AnimationClip idle;
    idle.Duration = Seconds(kIdle);
    const auto bone = [&](const char* base, const char* suffix) {
        return skeleton.find(std::string(base) + suffix);
    };
    const auto slow = [](float t, float phase) { return std::sin(t * MathHelper::TwoPi + phase); };

    // The weight on the right leg: the pelvis over the right foot, the
    // left knee eased and the left foot a little forward, the trunk tilted
    // back over the standing leg. And the breath.
    {
        const int pelvis = skeleton.find(BoneName::kPelvis);
        const Vector3 rest = pelvis >= 0 ? skeleton[pelvis].head : Vector3::Zero;
        idle.Tracks.push_back(Track(skeleton, pelvis, kIdle, kKeys,
            [&](float t) { return RollQ(0.035f + 0.01f * slow(t, 0.0f)); },
            [&](float t) {
                return rest + Vector3(-0.030f + 0.006f * slow(t, 0.0f),
                                      std::sin(t * MathHelper::TwoPi * 1.5f) * height * 0.002f, 0.0f);
            }));
    }
    idle.Tracks.push_back(Track(skeleton, bone(BoneName::kThigh, ".L"), kIdle, kKeys,
                                [&](float) { return PitchQ(-0.08f) * RollQ(0.05f); }));
    idle.Tracks.push_back(Track(skeleton, bone(BoneName::kShin, ".L"), kIdle, kKeys,
                                [&](float) { return PitchQ(0.14f); }));
    idle.Tracks.push_back(Track(skeleton, bone(BoneName::kFoot, ".L"), kIdle, kKeys,
                                [&](float) { return PitchQ(-0.06f); }));
    idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kChest), kIdle, kKeys, [&](float t) {
        const float lookYaw = kind == IdleKind::Look ? 0.10f * slow(t, 0.7f) : 0.03f * slow(t, 0.7f);
        return YawQ(lookYaw) * RollQ(-0.03f) * PitchQ(kind == IdleKind::Phone ? -0.06f : 0.0f);
    }));

    switch (kind)
    {
        case IdleKind::Look:
            // Looking about: the head turns through a good part of a right
            // angle and back, slowly, and the arms hang with the elbows
            // eased.
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kNeck), kIdle, kKeys,
                                        [&](float t) { return YawQ(0.12f * slow(t, 0.9f)); }));
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kHead), kIdle, kKeys,
                                        [&](float t) {
                return YawQ(0.28f * slow(t, 0.9f)) * PitchQ(0.05f * slow(t * 0.5f, 2.0f));
            }));
            for (int side = 0; side < 2; ++side)
            {
                const char* suffix = side == 0 ? ".R" : ".L";
                const float abduct = side == 0 ? -0.07f : 0.07f;
                idle.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, suffix), kIdle, kKeys,
                                            [&](float t) {
                    return RollQ(abduct) * PitchQ(0.06f + 0.02f * slow(t, side == 0 ? 0.0f : 1.4f));
                }));
                idle.Tracks.push_back(Track(skeleton, bone(BoneName::kForearm, suffix), kIdle, kKeys,
                                            [&](float) { return PitchQ(side == 0 ? -0.18f : -0.26f); }));
            }
            break;
        case IdleKind::Phone:
            // Reading a phone: the right hand up in front of the chest, the
            // head bowed to it, the left arm hanging.
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kNeck), kIdle, kKeys,
                                        [&](float) { return PitchQ(0.20f); }));
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kHead), kIdle, kKeys,
                                        [&](float t) { return PitchQ(0.32f + 0.02f * slow(t, 0.4f)); }));
            idle.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, ".R"), kIdle, kKeys,
                                        [&](float t) {
                return RollQ(0.12f) * PitchQ(-0.30f + 0.015f * slow(t, 0.4f));
            }));
            idle.Tracks.push_back(Track(skeleton, bone(BoneName::kForearm, ".R"), kIdle, kKeys,
                                        [&](float) { return RollQ(0.30f) * PitchQ(-1.75f); }));
            idle.Tracks.push_back(Track(skeleton, bone(BoneName::kHand, ".R"), kIdle, kKeys,
                                        [&](float) { return PitchQ(-0.35f); }));
            idle.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, ".L"), kIdle, kKeys,
                                        [&](float t) { return RollQ(0.07f) * PitchQ(0.05f + 0.02f * slow(t, 1.4f)); }));
            idle.Tracks.push_back(Track(skeleton, bone(BoneName::kForearm, ".L"), kIdle, kKeys,
                                        [&](float) { return PitchQ(-0.20f); }));
            break;
        case IdleKind::Hands:
            // Both hands together in front, the way people wait: the upper
            // arms a little forward and turned in, the forearms across.
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kNeck), kIdle, kKeys,
                                        [&](float t) { return YawQ(0.06f * slow(t, 0.9f)); }));
            idle.Tracks.push_back(Track(skeleton, skeleton.find(BoneName::kHead), kIdle, kKeys,
                                        [&](float t) { return YawQ(0.16f * slow(t, 0.9f)) * PitchQ(0.06f); }));
            for (int side = 0; side < 2; ++side)
            {
                const char* suffix = side == 0 ? ".R" : ".L";
                const float in = side == 0 ? 0.16f : -0.16f;
                idle.Tracks.push_back(Track(skeleton, bone(BoneName::kUpperArm, suffix), kIdle, kKeys,
                                            [&](float t) {
                    return RollQ(in) * PitchQ(-0.12f + 0.015f * slow(t, 0.4f));
                }));
                idle.Tracks.push_back(Track(skeleton, bone(BoneName::kForearm, suffix), kIdle, kKeys,
                                            [&](float) { return RollQ(in * 2.2f) * PitchQ(-1.30f); }));
            }
            break;
    }
    return idle;
}

}  // namespace

void CharacterFactory::Clips::install(std::unordered_map<std::string, AnimationClip>& into) const
{
    into[kWalkNames[0]] = walk;
    into[kWalkNames[1]] = walkBrisk;
    into[kWalkNames[2]] = walkEasy;
    into[kIdleNames[0]] = idle;
    into[kIdleNames[1]] = idlePhone;
    into[kIdleNames[2]] = idleHands;
}

CharacterFactory::Clips CharacterFactory::clips(const Skeleton& skeleton, float height,
                                                 float strideSeconds)
{
    Clips out;
    const float cycle = std::max(strideSeconds, 0.3f);
    // Three gaits. The brisk one strides longer and swings more and
    // carries a shade more lean; the easy one is shorter and quieter. The
    // scene scales each person's stride by Clips::kStrideScale to match, so
    // the feet keep to the ground in all three.
    WalkStyle plain;
    WalkStyle brisk;
    brisk.stride = 1.10f; brisk.arm = 0.40f; brisk.lean = 0.07f; brisk.bob = 1.15f;
    WalkStyle easy;
    easy.stride = 0.90f; easy.arm = 0.17f; easy.lean = 0.02f; easy.bob = 0.85f; easy.lag = 0.05f;
    out.walk      = WalkClip(skeleton, height, cycle, plain);
    out.walkBrisk = WalkClip(skeleton, height, cycle, brisk);
    out.walkEasy  = WalkClip(skeleton, height, cycle, easy);
    out.idle      = IdleClip(skeleton, height, IdleKind::Look);
    out.idlePhone = IdleClip(skeleton, height, IdleKind::Phone);
    out.idleHands = IdleClip(skeleton, height, IdleKind::Hands);
    return out;
}

}  // namespace CnaStreet
