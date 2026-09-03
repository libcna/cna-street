// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/PropFactory.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using CnaStreet::Geometry::BoxFaces;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::Place;
using CnaStreet::Geometry::UvMode;

namespace CnaStreet {

namespace M = Metrics;

namespace {

/// A box given as a centre and half-extents, which is how most of these parts
/// are easiest to describe.
void Box(MeshBuilder& builder, float cx, float cy, float cz, float hx, float hy, float hz,
         const BoxFaces& faces = BoxFaces{})
{
    builder.addBox(Vector3(cx - hx, cy - hy, cz - hz), Vector3(cx + hx, cy + hy, cz + hz), faces);
}

}  // namespace

PropFactory::PropFactory(const MaterialLibrary& materials) : materials_(materials) {}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------
void PropFactory::streetLamp(GeometryCollector& collector, float height, float reach) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelDark));
    steel.setTileSize(0.5f);

    // The base flange, then the column, tapered the way a spun steel column is.
    steel.addCylinder(Vector3::Zero, 0.135f, 0.115f, 0.28f, 12, false, false);
    steel.addCylinder(Vector3(0.0f, 0.28f, 0.0f), M::kLampColumnRadiusBase,
                      M::kLampColumnRadiusTop, height - 0.28f, 12, false, false);
    // The door for the cut-out, which every column has at about knee height.
    Box(steel, 0.0f, 0.62f, M::kLampColumnRadiusBase * 0.92f, 0.045f, 0.20f, 0.012f);

    // The outreach arm: a straight rise then a shallow curve out over the kerb.
    const Vector3 top(0.0f, height, 0.0f);
    constexpr int kArcSegments = 6;
    Vector3 previous = top;
    for (int i = 1; i <= kArcSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kArcSegments);
        // A quarter-cosine bend, which is the shape a real outreach arm has.
        const Vector3 next(0.0f, height + 0.42f * std::sin(t * MathHelper::PiOver2),
                           reach * (1.0f - std::cos(t * MathHelper::PiOver2)));
        steel.addCylinderBetween(previous, next, M::kLampColumnRadiusTop * 0.86f, 8);
        previous = next;
    }

    // The luminaire: a shallow wedge with a glazed underside.
    const Vector3 lamp = previous + Vector3(0.0f, -0.06f, M::kLuminaireLength * 0.42f);
    steel.addBox(Vector3(lamp.X - M::kLuminaireWidth * 0.5f, lamp.Y - 0.02f,
                         lamp.Z - M::kLuminaireLength * 0.5f),
                 Vector3(lamp.X + M::kLuminaireWidth * 0.5f, lamp.Y + M::kLuminaireHeight,
                         lamp.Z + M::kLuminaireLength * 0.5f),
                 BoxFaces::allButBottom());

    MeshBuilder& glass = collector.builder(&materials_.get(MaterialId::LampGlass));
    glass.setUvMode(UvMode::Explicit);
    glass.addQuadUnitUv(
        Vector3(lamp.X - M::kLuminaireWidth * 0.44f, lamp.Y - 0.025f,
                lamp.Z - M::kLuminaireLength * 0.44f),
        Vector3(lamp.X + M::kLuminaireWidth * 0.44f, lamp.Y - 0.025f,
                lamp.Z - M::kLuminaireLength * 0.44f),
        Vector3(lamp.X + M::kLuminaireWidth * 0.44f, lamp.Y - 0.025f,
                lamp.Z + M::kLuminaireLength * 0.44f),
        Vector3(lamp.X - M::kLuminaireWidth * 0.44f, lamp.Y - 0.025f,
                lamp.Z + M::kLuminaireLength * 0.44f));
}

// ---------------------------------------------------------------------------
// Seating and small furniture
// ---------------------------------------------------------------------------
void PropFactory::bench(GeometryCollector& collector) const
{
    MeshBuilder& iron = collector.builder(&materials_.get(MaterialId::PaintedSteelGreen));
    iron.setTileSize(0.4f);
    MeshBuilder& timber = collector.builder(&materials_.get(MaterialId::Timber));
    timber.setTileSize(0.7f);

    const float half = M::kBenchLength * 0.5f;
    // Two cast ends: a foot, an upright, and the scrolled arm.
    for (const float side : {-half, half})
    {
        Box(iron, side, 0.03f, 0.0f, 0.035f, 0.03f, M::kBenchDepth * 0.5f);
        Box(iron, side, M::kBenchSeatHeight * 0.5f, -M::kBenchDepth * 0.32f, 0.028f,
            M::kBenchSeatHeight * 0.5f, 0.028f);
        Box(iron, side, M::kBenchSeatHeight * 0.5f, M::kBenchDepth * 0.34f, 0.028f,
            M::kBenchSeatHeight * 0.5f, 0.028f);
        // The back stay, raked back the way a bench actually is.
        iron.addCylinderBetween(Vector3(side, M::kBenchSeatHeight, -M::kBenchDepth * 0.30f),
                                Vector3(side, M::kBenchBackHeight, -M::kBenchDepth * 0.46f),
                                0.026f, 6);
        // The arm rest.
        iron.addCylinderBetween(Vector3(side, M::kBenchSeatHeight + 0.20f, -M::kBenchDepth * 0.36f),
                                Vector3(side, M::kBenchSeatHeight + 0.20f, M::kBenchDepth * 0.34f),
                                0.024f, 6);
    }

    // Seat slats, then back slats, with the gaps a bench needs so rain runs off.
    for (int slat = 0; slat < 4; ++slat)
    {
        const float z = -M::kBenchDepth * 0.26f + static_cast<float>(slat) * 0.145f;
        Box(timber, 0.0f, M::kBenchSeatHeight, z, half - 0.02f, 0.021f, 0.060f);
    }
    for (int slat = 0; slat < 3; ++slat)
    {
        const float y = M::kBenchSeatHeight + 0.13f + static_cast<float>(slat) * 0.135f;
        const float z = -M::kBenchDepth * 0.30f
                        - (y - M::kBenchSeatHeight) * 0.28f;   // follows the rake
        Box(timber, 0.0f, y, z, half - 0.02f, 0.055f, 0.021f);
    }
}

void PropFactory::bollard(GeometryCollector& collector) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
    steel.setTileSize(0.35f);
    const float r = M::kBollardRadius;
    steel.addCylinder(Vector3::Zero, r * 1.25f, r, 0.10f, 12, false, false);
    steel.addCylinder(Vector3(0.0f, 0.10f, 0.0f), r, r, M::kBollardHeight - 0.16f, 12, false,
                      false);
    // A domed cap rather than a flat one; a flat-topped bollard reads as a pipe.
    steel.addEllipsoid(Vector3(0.0f, M::kBollardHeight - 0.06f, 0.0f),
                       Vector3(r, 0.075f, r), 12, 6);

    MeshBuilder& band = collector.builder(&materials_.get(MaterialId::SignBack));
    band.setTileSize(0.2f);
    band.addCylinder(Vector3(0.0f, M::kBollardHeight - 0.20f, 0.0f), r * 1.04f, r * 1.04f, 0.055f,
                     12, false, false);
}

void PropFactory::litterBin(GeometryCollector& collector) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelDark));
    steel.setTileSize(0.35f);
    steel.addCylinder(Vector3::Zero, 0.075f, 0.05f, M::kBinPostHeight, 10, false, false);

    MeshBuilder& body = collector.builder(&materials_.get(MaterialId::BinBody));
    body.setTileSize(0.5f);
    const float base = M::kBinPostHeight - M::kBinHeight;
    // A slightly conical body, open at the top, with a hoop below the rim.
    body.addCylinder(Vector3(0.0f, base, 0.0f), M::kBinRadius * 0.86f, M::kBinRadius,
                     M::kBinHeight, 14, true, false);
    body.addCylinder(Vector3(0.0f, base + M::kBinHeight - 0.05f, 0.0f), M::kBinRadius * 1.06f,
                     M::kBinRadius * 1.06f, 0.045f, 14, false, false);
    // The liner, seen down the opening.
    body.addDisc(Vector3(0.0f, base + 0.10f, 0.0f), M::kBinRadius * 0.82f, 14, true);

    // The hood: a small canopy on one side, as on a "Stadtmöbel" bin.
    Box(steel, 0.0f, M::kBinPostHeight + 0.03f, -M::kBinRadius * 0.5f, M::kBinRadius * 1.1f,
        0.022f, M::kBinRadius * 0.7f);
}

void PropFactory::hydrant(GeometryCollector& collector) const
{
    MeshBuilder& body = collector.builder(&materials_.get(MaterialId::HydrantRed));
    body.setTileSize(0.35f);
    const float r = M::kHydrantRadius;
    body.addCylinder(Vector3::Zero, r * 1.5f, r * 1.35f, 0.09f, 12, false, false);
    body.addCylinder(Vector3(0.0f, 0.09f, 0.0f), r * 1.1f, r, M::kHydrantHeight - 0.24f, 12, false,
                     false);
    body.addEllipsoid(Vector3(0.0f, M::kHydrantHeight - 0.15f, 0.0f), Vector3(r, 0.10f, r), 12, 6);
    // The bonnet nut and the two outlets.
    body.addCylinder(Vector3(0.0f, M::kHydrantHeight - 0.06f, 0.0f), 0.032f, 0.028f, 0.055f, 6,
                     false, true);
    for (const float side : {-1.0f, 1.0f})
        body.addCylinderBetween(Vector3(0.0f, M::kHydrantHeight * 0.62f, 0.0f),
                                Vector3(side * (r + 0.075f), M::kHydrantHeight * 0.62f, 0.0f),
                                0.045f, 8);
}

void PropFactory::utilityCabinet(GeometryCollector& collector, Rng& rng) const
{
    MeshBuilder& grey = collector.builder(&materials_.get(MaterialId::CabinetGrey));
    grey.setTileSize(0.8f);
    const float w = M::kCabinetWidth * rng.aboutOne(0.12f);
    const float d = M::kCabinetDepth;
    const float h = M::kCabinetHeight * rng.aboutOne(0.10f);

    // A concrete kerb under it, the body, and a slightly overhanging lid.
    MeshBuilder& base = collector.builder(&materials_.get(MaterialId::ConcretePanel));
    base.setTileSize(0.6f);
    Box(base, 0.0f, 0.03f, 0.0f, w * 0.54f, 0.03f, d * 0.56f);
    Box(grey, 0.0f, 0.06f + h * 0.5f, 0.0f, w * 0.5f, h * 0.5f, d * 0.5f,
        BoxFaces::allButBottom());
    Box(grey, 0.0f, 0.06f + h + 0.018f, 0.0f, w * 0.53f, 0.018f, d * 0.53f);
    // The door's shadow gap and its lock, which is what makes it read as a door.
    Box(grey, 0.0f, 0.06f + h * 0.5f, d * 0.5f + 0.006f, w * 0.44f, h * 0.44f, 0.006f);
    MeshBuilder& lock = collector.builder(&materials_.get(MaterialId::GalvanisedSteel));
    lock.setTileSize(0.2f);
    Box(lock, w * 0.36f, 0.06f + h * 0.55f, d * 0.5f + 0.012f, 0.022f, 0.038f, 0.010f);
}

void PropFactory::bicycleStand(GeometryCollector& collector) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::GalvanisedSteel));
    steel.setTileSize(0.3f);
    const float w = M::kBikeRackWidth * 0.5f;
    const float h = M::kBikeRackHeight;
    const float r = M::kBikeRackRadius;
    steel.addCylinderBetween(Vector3(-w, 0.0f, 0.0f), Vector3(-w, h - r, 0.0f), r, 8);
    steel.addCylinderBetween(Vector3(w, 0.0f, 0.0f), Vector3(w, h - r, 0.0f), r, 8);
    // The bend across the top, as a short arc.
    constexpr int kSegments = 5;
    for (int i = 0; i < kSegments; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
        const auto point = [&](float t) {
            const float angle = MathHelper::Pi * t;
            return Vector3(-std::cos(angle) * w, h - r + std::sin(angle) * r, 0.0f);
        };
        steel.addCylinderBetween(point(t0), point(t1), r, 8);
    }
}

void PropFactory::planter(GeometryCollector& collector, Rng& rng) const
{
    MeshBuilder& concrete = collector.builder(&materials_.get(MaterialId::ConcretePanel));
    concrete.setTileSize(0.9f);
    const float w = 1.05f, d = 0.62f, h = 0.52f;
    // Four walls and a rim, so the box is hollow when seen from above.
    Box(concrete, 0.0f, h * 0.5f, -d * 0.5f + 0.035f, w * 0.5f, h * 0.5f, 0.035f);
    Box(concrete, 0.0f, h * 0.5f, d * 0.5f - 0.035f, w * 0.5f, h * 0.5f, 0.035f);
    Box(concrete, -w * 0.5f + 0.035f, h * 0.5f, 0.0f, 0.035f, h * 0.5f, d * 0.5f);
    Box(concrete, w * 0.5f - 0.035f, h * 0.5f, 0.0f, 0.035f, h * 0.5f, d * 0.5f);

    MeshBuilder& soil = collector.builder(&materials_.get(MaterialId::Soil));
    soil.setTileSize(0.5f);
    soil.addQuad(Vector3(-w * 0.46f, h - 0.09f, -d * 0.42f), Vector3(-w * 0.46f, h - 0.09f, d * 0.42f),
                 Vector3(w * 0.46f, h - 0.09f, d * 0.42f), Vector3(w * 0.46f, h - 0.09f, -d * 0.42f));

    // A clipped shrub: three overlapping ellipsoids, which reads as a mass of
    // foliage rather than as a ball.
    MeshBuilder& hedge = collector.builder(&materials_.get(MaterialId::Hedge));
    hedge.setTileSize(0.5f);
    for (int i = 0; i < 3; ++i)
        hedge.addEllipsoid(Vector3(rng.signed_(w * 0.28f), h + rng.range(0.10f, 0.24f),
                                   rng.signed_(d * 0.22f)),
                           Vector3(rng.range(0.26f, 0.38f), rng.range(0.18f, 0.28f),
                                   rng.range(0.20f, 0.30f)),
                           10, 6);
}

void PropFactory::busShelter(GeometryCollector& collector) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelDark));
    steel.setTileSize(0.5f);
    MeshBuilder& glass = collector.builder(&materials_.get(MaterialId::ShopGlazing));
    glass.setTileSize(1.6f);

    const float w = 3.9f, d = 1.45f, h = 2.42f;
    for (const float x : {-w * 0.5f, w * 0.5f})
        for (const float z : {-d * 0.5f, d * 0.5f})
            steel.addCylinderBetween(Vector3(x, 0.0f, z), Vector3(x, h, z), 0.045f, 8);
    // The roof: a shallow single pitch draining to the back.
    steel.addBox(Vector3(-w * 0.5f - 0.14f, h, -d * 0.5f - 0.16f),
                 Vector3(w * 0.5f + 0.14f, h + 0.09f, d * 0.5f + 0.10f));
    // Back and one end glazed; the other end left open to walk through.
    glass.addQuad(Vector3(-w * 0.5f, 0.28f, -d * 0.5f), Vector3(-w * 0.5f, h - 0.08f, -d * 0.5f),
                  Vector3(w * 0.5f, h - 0.08f, -d * 0.5f), Vector3(w * 0.5f, 0.28f, -d * 0.5f));
    glass.addQuad(Vector3(-w * 0.5f, 0.28f, d * 0.5f), Vector3(-w * 0.5f, 0.28f, -d * 0.5f),
                  Vector3(-w * 0.5f, h - 0.08f, -d * 0.5f), Vector3(-w * 0.5f, h - 0.08f, d * 0.5f));
    // A bench along the back.
    MeshBuilder& timber = collector.builder(&materials_.get(MaterialId::Timber));
    timber.setTileSize(0.7f);
    Box(timber, 0.0f, 0.46f, -d * 0.24f, w * 0.42f, 0.025f, 0.16f);
}

// ---------------------------------------------------------------------------
// Vegetation
// ---------------------------------------------------------------------------
void PropFactory::tree(GeometryCollector& collector, Rng& rng, float height) const
{
    MeshBuilder& bark = collector.builder(&materials_.get(MaterialId::Bark));
    bark.setTileSize(0.9f);

    const float clearStem = M::kTreeClearStem * rng.aboutOne(0.12f);
    const float trunkRadius = M::kTreeTrunkRadius * rng.aboutOne(0.18f);
    const float lean = rng.signed_(0.10f);

    // The trunk, in two lengths so it can lean slightly. A perfectly vertical
    // street tree looks like a lamp post with leaves on.
    bark.addCylinder(Vector3::Zero, trunkRadius * 1.35f, trunkRadius * 1.08f, 0.35f, 10, false,
                     false);
    bark.addCylinderBetween(Vector3(0.0f, 0.35f, 0.0f),
                            Vector3(lean * 0.4f, clearStem, lean * 0.25f), trunkRadius, 10, false);

    // Three or four main limbs rising into the crown.
    const int limbs = rng.intRange(3, 5);
    const float crownBase = clearStem;
    const float crownTop  = height;
    std::vector<Vector3> limbTips;
    for (int i = 0; i < limbs; ++i)
    {
        const float angle = MathHelper::TwoPi * static_cast<float>(i) / static_cast<float>(limbs)
                            + rng.signed_(0.5f);
        const float spread = rng.range(0.9f, 1.9f);
        const Vector3 from(lean * 0.4f, crownBase, lean * 0.25f);
        const Vector3 mid(from.X + std::cos(angle) * spread * 0.45f,
                          crownBase + (crownTop - crownBase) * 0.34f,
                          from.Z + std::sin(angle) * spread * 0.45f);
        const Vector3 tip(from.X + std::cos(angle) * spread,
                          crownBase + (crownTop - crownBase) * rng.range(0.55f, 0.75f),
                          from.Z + std::sin(angle) * spread);
        bark.addCylinderBetween(from, mid, trunkRadius * 0.62f, 7, false);
        bark.addCylinderBetween(mid, tip, trunkRadius * 0.34f, 6, false);
        limbTips.push_back(tip);
    }

    // The crown: alpha-masked cards, in crossed pairs around each limb tip plus
    // a few filling the middle. Cards rather than a solid shell because a
    // canopy has to be see-through at its edge -- a hard silhouette against the
    // sky is the single most obvious way a tree gives itself away.
    MeshBuilder& leaves = collector.builder(&materials_.get(MaterialId::Foliage));
    leaves.setUvMode(UvMode::Explicit);

    auto card = [&](const Vector3& centre, float size, float yaw, float pitch) {
        const Matrix frame = Matrix::CreateRotationX(pitch) * Matrix::CreateRotationY(yaw)
                             * Matrix::CreateTranslation(centre);
        const float h = size * 0.5f;
        leaves.addQuadUv(Vector3::Transform(Vector3(-h, -h, 0.0f), frame),
                         Vector3::Transform(Vector3(h, -h, 0.0f), frame),
                         Vector3::Transform(Vector3(h, h, 0.0f), frame),
                         Vector3::Transform(Vector3(-h, h, 0.0f), frame),
                         Vector2(0.0f, 1.0f), Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f),
                         Vector2(0.0f, 0.0f));
    };

    for (const Vector3& tip : limbTips)
    {
        const int clusters = rng.intRange(3, 5);
        for (int c = 0; c < clusters; ++c)
        {
            const Vector3 at = tip + Vector3(rng.signed_(0.9f), rng.range(-0.3f, 1.1f),
                                             rng.signed_(0.9f));
            const float size = rng.range(1.7f, 2.9f);
            const float yaw = rng.range(0.0f, MathHelper::TwoPi);
            card(at, size, yaw, rng.signed_(0.5f));
            card(at, size * 0.94f, yaw + MathHelper::PiOver2, rng.signed_(0.5f));
        }
    }
    const int fill = rng.intRange(5, 9);
    for (int i = 0; i < fill; ++i)
    {
        const Vector3 at(lean * 0.4f + rng.signed_(1.5f),
                         crownBase + (crownTop - crownBase) * rng.range(0.25f, 0.9f),
                         lean * 0.25f + rng.signed_(1.5f));
        const float size = rng.range(1.9f, 3.1f);
        const float yaw = rng.range(0.0f, MathHelper::TwoPi);
        card(at, size, yaw, rng.signed_(0.6f));
        card(at, size * 0.9f, yaw + MathHelper::PiOver2, rng.signed_(0.6f));
    }
}

void PropFactory::treeGrate(GeometryCollector& collector) const
{
    MeshBuilder& iron = collector.builder(&materials_.get(MaterialId::DrainGrate));
    iron.setTileSize(0.5f);
    const float half = M::kTreePitSize * 0.5f;
    // A square frame of bars with a hole in the middle for the trunk.
    for (int i = 0; i < 9; ++i)
    {
        const float t = -half + static_cast<float>(i) * (M::kTreePitSize / 8.0f);
        if (std::fabs(t) < 0.22f) continue;
        Box(iron, t, 0.012f, 0.0f, 0.028f, 0.012f, half);
        Box(iron, 0.0f, 0.012f, t, half, 0.012f, 0.028f);
    }
    for (const float side : {-half, half})
    {
        Box(iron, side, 0.014f, 0.0f, 0.045f, 0.014f, half);
        Box(iron, 0.0f, 0.014f, side, half, 0.014f, 0.045f);
    }
}

// ---------------------------------------------------------------------------
// Traffic signals
// ---------------------------------------------------------------------------
void PropFactory::signalHead(GeometryCollector& collector) const
{
    MeshBuilder& housing = collector.builder(&materials_.get(MaterialId::SignalHousing));
    housing.setTileSize(0.35f);

    const float w = M::kSignalHousingWidth;
    const float h = M::kSignalHousingHeight;
    const float d = M::kSignalHousingDepth;

    // The backing board: a yellow-bordered plate behind the head, which is what
    // makes a signal readable against a bright façade.
    MeshBuilder& board = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
    board.setTileSize(0.5f);
    Box(board, 0.0f, h * 0.5f, -d * 0.5f - 0.012f, w * 0.78f, h * 0.54f, 0.012f);

    Box(housing, 0.0f, h * 0.5f, 0.0f, w * 0.5f, h * 0.5f, d * 0.5f);
    // The three hoods.
    for (int aspect = 0; aspect < 3; ++aspect)
    {
        const float y = h - (static_cast<float>(aspect) + 0.5f) * (h / 3.0f);
        housing.addCylinderBetween(Vector3(0.0f, y, d * 0.5f),
                                   Vector3(0.0f, y, d * 0.5f + 0.055f),
                                   M::kSignalLensRadius + 0.022f, 12, false);
        // The peak over each lens: a half-collar that keeps the sun off it.
        Box(housing, 0.0f, y + M::kSignalLensRadius + 0.012f, d * 0.5f + 0.055f,
            M::kSignalLensRadius + 0.03f, 0.012f, 0.055f);
    }
    // The hinge and the fixing bracket at the back.
    housing.addCylinderBetween(Vector3(-w * 0.5f - 0.018f, 0.06f, 0.0f),
                               Vector3(-w * 0.5f - 0.018f, h - 0.06f, 0.0f), 0.018f, 6);
}

void PropFactory::signalLens(GeometryCollector& collector, MaterialId material, float radius) const
{
    MeshBuilder& lens = collector.builder(&materials_.get(material));
    lens.setUvMode(UvMode::Explicit);
    // A shallow dome rather than a disc: a lens catches a highlight along its
    // curve even when it is not lit, and a flat one never does.
    lens.addEllipsoid(Vector3(0.0f, 0.0f, -radius * 0.55f), Vector3(radius, radius, radius * 0.75f),
                      14, 8);
}

void PropFactory::pedestrianSignalHead(GeometryCollector& collector) const
{
    MeshBuilder& housing = collector.builder(&materials_.get(MaterialId::SignalHousing));
    housing.setTileSize(0.35f);
    const float w = M::kSignalHousingWidth;
    const float h = M::kPedSignalHousingHeight;
    const float d = M::kSignalHousingDepth * 0.85f;

    Box(housing, 0.0f, h * 0.5f, 0.0f, w * 0.5f, h * 0.5f, d * 0.5f);
    for (int aspect = 0; aspect < 2; ++aspect)
    {
        const float y = h - (static_cast<float>(aspect) + 0.5f) * (h / 2.0f);
        housing.addCylinderBetween(Vector3(0.0f, y, d * 0.5f), Vector3(0.0f, y, d * 0.5f + 0.042f),
                                   M::kSignalLensRadius + 0.02f, 12, false);
    }
    // The push-button unit below the head.
    MeshBuilder& button = collector.builder(&materials_.get(MaterialId::PaintedSteelGrey));
    button.setTileSize(0.25f);
    Box(button, 0.0f, -0.30f, d * 0.35f, 0.055f, 0.075f, 0.035f);
}

void PropFactory::signalPost(GeometryCollector& collector, float height) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelGrey));
    steel.setTileSize(0.4f);
    steel.addCylinder(Vector3::Zero, M::kSignalPoleRadius * 1.7f, M::kSignalPoleRadius * 1.5f,
                      0.12f, 12, false, false);
    steel.addCylinder(Vector3(0.0f, 0.12f, 0.0f), M::kSignalPoleRadius, M::kSignalPoleRadius,
                      height - 0.12f, 12, false, true);
    // The controller cabinet's cable entry at the base.
    Box(steel, 0.0f, 0.42f, M::kSignalPoleRadius * 0.9f, 0.038f, 0.16f, 0.012f);
}

void PropFactory::signalMast(GeometryCollector& collector, float height, float reach) const
{
    MeshBuilder& steel = collector.builder(&materials_.get(MaterialId::PaintedSteelGrey));
    steel.setTileSize(0.5f);
    const float radius = M::kSignalPoleRadius * 1.9f;
    steel.addCylinder(Vector3::Zero, radius * 1.6f, radius * 1.4f, 0.22f, 14, false, false);
    steel.addCylinder(Vector3(0.0f, 0.22f, 0.0f), radius, radius * 0.82f, height - 0.22f, 14,
                      false, true);

    // The arm: a rise into a long shallow curve out over the carriageway.
    constexpr int kSegments = 7;
    Vector3 previous(0.0f, height, 0.0f);
    for (int i = 1; i <= kSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const Vector3 next(0.0f, height + 0.55f * std::sin(t * MathHelper::PiOver2),
                           reach * (1.0f - std::cos(t * MathHelper::PiOver2)));
        steel.addCylinderBetween(previous, next, radius * 0.62f, 10);
        previous = next;
    }
}

// ---------------------------------------------------------------------------
// Signs
// ---------------------------------------------------------------------------
void PropFactory::trafficSign(GeometryCollector& collector, SignShape shape, MaterialId face,
                              float mountHeight) const
{
    MeshBuilder& post = collector.builder(&materials_.get(MaterialId::GalvanisedSteel));
    post.setTileSize(0.4f);

    float plateHeight = M::kSignRoundDiameter;
    float plateWidth  = M::kSignRoundDiameter;
    switch (shape)
    {
        case SignShape::Disc:       plateWidth = plateHeight = M::kSignRoundDiameter; break;
        case SignShape::TriangleUp: plateWidth = plateHeight = M::kSignTriangleSide;  break;
        case SignShape::Rectangle:
            plateWidth  = M::kSignRectangleWidth;
            plateHeight = M::kSignRectangleHeight;
            break;
        case SignShape::Square:     plateWidth = plateHeight = M::kSignRectangleWidth; break;
    }

    const float top = mountHeight + plateHeight;
    post.addCylinder(Vector3::Zero, M::kSignPostRadius * 1.5f, M::kSignPostRadius, 0.09f, 10,
                     false, false);
    post.addCylinder(Vector3(0.0f, 0.09f, 0.0f), M::kSignPostRadius, M::kSignPostRadius,
                     top - 0.02f, 10, false, true);

    // The plate itself: the face carries the artwork on an alpha-masked quad, and
    // the back is a plain aluminium panel a few millimetres behind it.
    const float centreY = mountHeight + plateHeight * 0.5f;
    const float half = plateWidth * 0.5f;
    const float depth = M::kSignPostRadius + 0.008f;

    MeshBuilder& plate = collector.builder(&materials_.get(face));
    plate.setUvMode(UvMode::Explicit);
    plate.addQuadUnitUv(Vector3(-half, centreY - plateHeight * 0.5f, depth),
                        Vector3(half, centreY - plateHeight * 0.5f, depth),
                        Vector3(half, centreY + plateHeight * 0.5f, depth),
                        Vector3(-half, centreY + plateHeight * 0.5f, depth));

    MeshBuilder& back = collector.builder(&materials_.get(MaterialId::SignBack));
    back.setUvMode(UvMode::Explicit);
    back.addQuadUnitUv(Vector3(half, centreY - plateHeight * 0.5f, depth - 0.012f),
                       Vector3(-half, centreY - plateHeight * 0.5f, depth - 0.012f),
                       Vector3(-half, centreY + plateHeight * 0.5f, depth - 0.012f),
                       Vector3(half, centreY + plateHeight * 0.5f, depth - 0.012f));
    // The channel bracket clamping the plate to the post.
    back.setUvMode(UvMode::Planar);
    back.setTileSize(0.2f);
    Box(back, 0.0f, centreY, depth - 0.026f, 0.045f, plateHeight * 0.34f, 0.014f);
}

void PropFactory::streetPlate(GeometryCollector& collector, MaterialId face) const
{
    MeshBuilder& plate = collector.builder(&materials_.get(face));
    plate.setUvMode(UvMode::Explicit);
    const float w = M::kStreetPlateWidth * 0.5f;
    const float h = M::kStreetPlateHeight * 0.5f;
    // The plate's texture is laid out in the top quarter of a square image, so
    // only that band is sampled here.
    plate.addQuadUv(Vector3(-w, -h, 0.028f), Vector3(w, -h, 0.028f), Vector3(w, h, 0.028f),
                    Vector3(-w, h, 0.028f), Vector2(0.0f, 0.25f), Vector2(1.0f, 0.25f),
                    Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f));

    MeshBuilder& back = collector.builder(&materials_.get(MaterialId::SignBack));
    back.setTileSize(0.3f);
    Box(back, 0.0f, 0.0f, 0.012f, w * 1.02f, h * 1.05f, 0.014f);
    // Two stand-off brackets to the wall.
    for (const float side : {-w * 0.7f, w * 0.7f})
        back.addCylinderBetween(Vector3(side, 0.0f, 0.0f), Vector3(side, 0.0f, -0.05f), 0.011f, 6);
}

}  // namespace CnaStreet
