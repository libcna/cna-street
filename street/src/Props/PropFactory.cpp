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
void PropFactory::tree(GeometryCollector& collector, Rng& rng, TreeSpecies species, float height,
                       bool fullDetail) const
{
    MeshBuilder& bark = collector.builder(&materials_.get(MaterialId::Bark));
    bark.setTileSize(0.55f);

    // What distinguishes one street tree from another, in the four numbers that
    // matter: how high the first branch is, how wide the crown gets, how far
    // each branch turns away from its parent, and how many times it splits.
    struct Shape
    {
        float clearStem;    ///< fraction of the height to the first branch
        float trunk;        ///< trunk radius at the base, metres
        float spread;       ///< crown radius as a fraction of the height
        float divergence;   ///< branch angle from its parent, radians
        int   levels;       ///< how many times a branch splits
        float leaf;         ///< leaf card size, metres
    };
    Shape shape{0.42f, M::kTreeTrunkRadius, 0.42f, 0.62f, 5, 0.95f};
    switch (species)
    {
        // A plane: pollarded high, so a bare stem and then a wide flat crown.
        case TreeSpecies::Plane: shape = {0.46f, 0.170f, 0.44f, 0.68f, 5, 1.05f}; break;
        // A lime: branches lower, denser, and the crown is taller than it is wide.
        case TreeSpecies::Lime:  shape = {0.34f, 0.135f, 0.36f, 0.52f, 5, 0.86f}; break;
        // A replacement planted last winter: thin, short, still staked.
        case TreeSpecies::Young: shape = {0.44f, 0.055f, 0.24f, 0.58f, 4, 0.60f}; break;
        case TreeSpecies::Count: break;
    }
    // One level fewer at distance, with correspondingly larger foliage cards, so
    // the far tree has a quarter of the geometry and the same silhouette. The
    // number of *distinct* cluster positions is what a crown's density is made
    // of -- three cards through one point project to the area of one card -- so
    // dropping a level is the only economy that actually saves anything.
    const int levels = fullDetail ? shape.levels : std::max(3, shape.levels - 1);

    const float clearStem = height * shape.clearStem * rng.aboutOne(0.10f);
    const float trunk     = shape.trunk * rng.aboutOne(0.16f);
    const Vector3 lean(rng.signed_(0.09f), 0.0f, rng.signed_(0.09f));

    // The trunk, as a swept tube rather than a cylinder: a street tree has a
    // root flare at the pavement, thins as it rises, and leans. A cylinder has
    // none of those, and a cylinder with leaves on is a lamp post with leaves on.
    {
        Geometry::SurfacePatch stem;
        const int sides = fullDetail ? 9 : 6;
        const float steps[] = {0.0f, 0.05f, 0.14f, 0.40f, 0.72f, 1.0f};
        const float radii[] = {1.55f, 1.24f, 1.08f, 0.94f, 0.84f, 0.74f};
        stem.resize(static_cast<int>(std::size(steps)), sides);
        stem.wrapCols = true;
        stem.smoothingAngle = 1.4f;
        for (int r = 0; r < stem.rows; ++r)
        {
            const float t = steps[r];
            const Vector3 centre = lean * (t * t) + Vector3(0.0f, clearStem * t, 0.0f);
            for (int c = 0; c < sides; ++c)
            {
                const float a = MathHelper::TwoPi * static_cast<float>(c)
                                / static_cast<float>(sides);
                // A trunk is not round. A few per cent of radial wobble, held
                // constant up the stem so it reads as a ridge rather than noise.
                const float wobble = 1.0f + 0.11f * std::sin(a * 3.0f + static_cast<float>(c));
                stem.at(r, c) = centre
                                + Vector3(std::cos(a) * trunk * radii[r] * wobble, 0.0f,
                                          std::sin(a) * trunk * radii[r] * wobble);
            }
        }
        bark.addSurfacePatch(stem);
    }

    MeshBuilder& leaves = collector.builder(&materials_.get(MaterialId::Foliage));
    leaves.setUvMode(UvMode::Explicit);

    // Where the crown sits, and how big it is. Both are needed below: the first
    // to shade the canopy as a volume, the second to size the branches so the
    // tree ends up the width its species says it is.
    const float crownReach  = height * shape.spread;
    const Vector3 crownAxis = lean + Vector3(0.0f, clearStem + (height - clearStem) * 0.55f, 0.0f);

    // One leaf card. Four rotations of the same image, picked per card, so a
    // crown of eighty cards is not eighty copies of one photograph -- which is
    // exactly what the eye finds first in a procedural tree.
    //
    // The normals are the other half of it. A flat card takes a flat card's
    // lighting: every leaf on one quad is equally lit, the quad next to it at a
    // different angle is a different flat tone, and a canopy built that way
    // reads as a stack of painted plates. Real foliage shades like a *ball* --
    // bright at the top and on the sun side, dark in the middle -- so each
    // vertex normal is bent most of the way from the card's own facing toward
    // the direction out of the crown's centre. It costs four normalisations per
    // card and it is the difference between foliage and bunting.
    const auto card = [&](const Vector3& centre, float size, float yaw, float pitch, int spin) {
        const Matrix frame = Matrix::CreateRotationZ(static_cast<float>(spin) * 1.5707963f)
                             * Matrix::CreateRotationX(pitch) * Matrix::CreateRotationY(yaw)
                             * Matrix::CreateTranslation(centre);
        const float h = size * 0.5f;
        const std::size_t first = leaves.vertexCount();
        if (!leaves.addQuadUv(Vector3::Transform(Vector3(-h, -h, 0.0f), frame),
                              Vector3::Transform(Vector3(h, -h, 0.0f), frame),
                              Vector3::Transform(Vector3(h, h, 0.0f), frame),
                              Vector3::Transform(Vector3(-h, h, 0.0f), frame), Vector2(0.0f, 1.0f),
                              Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f)))
            return;
        auto& vertices = leaves.mesh().vertices;
        for (std::size_t v = first; v < vertices.size(); ++v)
        {
            Vector3 out = vertices[v].Position - crownAxis;
            // Flattened vertically: a crown is wider than it is deep, and using
            // the true radial direction makes the underside of a wide canopy
            // shade as if it faced straight down.
            out.Y *= 0.55f;
            const float length = std::sqrt(out.X * out.X + out.Y * out.Y + out.Z * out.Z);
            if (length < 1e-3f) continue;
            out = out * (1.0f / length);
            const Vector3 blended = vertices[v].Normal * 0.28f + out * 0.72f;
            const float scale = std::sqrt(blended.X * blended.X + blended.Y * blended.Y
                                          + blended.Z * blended.Z);
            if (scale > 1e-4f) vertices[v].Normal = blended * (1.0f / scale);
        }
    };

    // A cluster of foliage at a twig tip: three cards through the same point at
    // different angles. Three rather than two because a crossed pair vanishes
    // edge-on from one direction, and a canopy that thins as you walk past it is
    // the other way a tree gives itself away.
    const int cardsPerCluster = fullDetail ? 3 : 2;
    const auto cluster = [&](const Vector3& at, float size) {
        const float yaw = rng.range(0.0f, MathHelper::TwoPi);
        card(at, size, yaw, rng.signed_(0.35f), rng.intRange(0, 3));
        card(at, size * 0.92f, yaw + 1.047f, rng.signed_(0.35f), rng.intRange(0, 3));
        if (cardsPerCluster > 2)
            card(at, size * 0.86f, yaw + 2.094f, rng.signed_(0.35f), rng.intRange(0, 3));
    };
    // Cards are bigger on the far tree, because it has fewer of them and a
    // crown that thins with distance is worse than one that coarsens.
    const float cardSize = shape.leaf * (fullDetail ? 1.0f : 1.45f);

    // The branch structure, recursively. Each branch splits into two or three,
    // each turning away from its parent by roughly the divergence angle and
    // spun around it; a twig at the last level carries a leaf cluster. This is
    // what the old generator's four straight limbs were standing in for, and
    // the difference is the whole silhouette: real foliage sits at the *ends*
    // of a structure, and the structure is visible through it.
    struct Branch { Vector3 from; Vector3 direction; float length; float radius; int level; };
    std::vector<Branch> queue;
    const Vector3 crotch = lean + Vector3(0.0f, clearStem, 0.0f);
    const int firstSplit = rng.intRange(3, 4);
    // The first limb's length, chosen so the crown ends up the width the species
    // asks for. A chain of `levels` branches shortening by about 0.7 each time
    // reaches roughly 2.9 times the first one, of which around half is
    // horizontal once the divergence angles are averaged in -- so a first limb
    // of about 0.68 of the target radius lands the outermost twigs on it.
    // `spread` was previously declared and never read, which is why every tree
    // came out the same narrow shape regardless of species.
    const float limb = crownReach * rng.range(0.62f, 0.74f);
    for (int i = 0; i < firstSplit; ++i)
    {
        const float a = MathHelper::TwoPi * static_cast<float>(i) / static_cast<float>(firstSplit)
                        + rng.signed_(0.45f);
        const float tilt = shape.divergence * rng.aboutOne(0.25f);
        queue.push_back(Branch{crotch,
                               Vector3(std::cos(a) * std::sin(tilt), std::cos(tilt),
                                       std::sin(a) * std::sin(tilt)),
                               limb * rng.aboutOne(0.14f), trunk * 0.60f, 1});
    }

    while (!queue.empty())
    {
        const Branch branch = queue.back();
        queue.pop_back();

        // Branches droop as they lengthen: gravity is most of what tells a real
        // tree apart from a diagram of one.
        const float droop = 0.10f * static_cast<float>(branch.level);
        const Vector3 direction(branch.direction.X, branch.direction.Y - droop,
                                branch.direction.Z);
        const float scale = 1.0f / std::max(0.2f, std::sqrt(direction.X * direction.X
                                                            + direction.Y * direction.Y
                                                            + direction.Z * direction.Z));
        const Vector3 to = branch.from + direction * (branch.length * scale);

        bark.addCylinderBetween(branch.from, to, branch.radius,
                                branch.level <= 1 ? (fullDetail ? 7 : 5) : (branch.level <= 2 ? 5 : 4),
                                false);

        if (branch.level >= levels)
        {
            cluster(to, cardSize * rng.aboutOne(0.22f));
            // Two more clusters back along the twig. This is where a crown's
            // density actually comes from: three cards through one point cover
            // one card's worth of sky, so what fills a canopy is the number of
            // *separate* places foliage hangs from, not the number of quads.
            cluster(branch.from + direction * (branch.length * scale * 0.62f),
                    cardSize * rng.aboutOne(0.25f) * 0.90f);
            cluster(branch.from + direction * (branch.length * scale * 0.28f),
                    cardSize * rng.aboutOne(0.25f) * 0.82f);
            continue;
        }
        // Foliage on the two levels above the twigs as well. Without it the
        // crown is a hollow shell with daylight through the middle of it, which
        // is exactly what a tree seen from underneath does not have.
        if (branch.level >= levels - 2 && rng.chance(fullDetail ? 0.80f : 0.5f))
            cluster(branch.from + direction * (branch.length * scale * rng.range(0.45f, 0.95f)),
                    cardSize * rng.aboutOne(0.25f) * 0.94f);

        const int children = rng.intRange(2, 3);
        for (int i = 0; i < children; ++i)
        {
            // Turn away from the parent by the divergence angle, spun about it.
            const float spin = MathHelper::TwoPi * static_cast<float>(i)
                                   / static_cast<float>(children)
                               + rng.signed_(0.7f);
            const float tilt = shape.divergence * rng.aboutOne(0.35f);
            // An orthonormal frame about the parent's direction.
            const Vector3 up = std::fabs(direction.Y) > 0.92f ? Vector3::Right : Vector3::Up;
            Vector3 side = Vector3::Cross(direction, up);
            const float sideLength = std::sqrt(side.X * side.X + side.Y * side.Y
                                               + side.Z * side.Z);
            if (sideLength < 1e-4f) continue;
            side = side * (1.0f / sideLength);
            const Vector3 other = Vector3::Cross(direction, side);
            const Vector3 turn = side * std::cos(spin) + other * std::sin(spin);
            const Vector3 childDirection = direction * (std::cos(tilt) * scale)
                                           + turn * std::sin(tilt);
            queue.push_back(Branch{to, childDirection, branch.length * rng.range(0.64f, 0.78f),
                                   branch.radius * 0.62f, branch.level + 1});
        }
    }

    if (species == TreeSpecies::Young)
    {
        // The stake and tie a newly planted street tree is held up by, which is
        // the detail that says somebody planted this one on purpose.
        MeshBuilder& stake = collector.builder(&materials_.get(MaterialId::Timber));
        stake.setTileSize(0.4f);
        const float sx = trunk * 3.4f;
        stake.addCylinder(Vector3(sx, 0.0f, 0.0f), 0.028f, 0.024f, 1.55f, 6, false, true);
        MeshBuilder& tie = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
        tie.addCylinderBetween(Vector3(sx, 1.42f, 0.0f), Vector3(0.0f, 1.42f, 0.0f), 0.014f, 5);
    }
}

void PropFactory::groundScruff(GeometryCollector& collector, Rng& rng, float radius,
                               int tufts) const
{
    MeshBuilder& weed = collector.builder(&materials_.get(MaterialId::Foliage));
    weed.setUvMode(UvMode::Explicit);
    for (int i = 0; i < tufts; ++i)
    {
        const float a = rng.range(0.0f, MathHelper::TwoPi);
        const float r = radius * std::sqrt(rng.range(0.15f, 1.0f));
        const Vector3 at(std::cos(a) * r, 0.0f, std::sin(a) * r);
        const float size = rng.range(0.10f, 0.26f);
        // Two crossed cards standing on the ground, leaning a little. A tuft of
        // weed at a wall base is four triangles and it is the difference between
        // a pavement and a floor.
        for (int c = 0; c < 2; ++c)
        {
            const float yaw = rng.range(0.0f, MathHelper::TwoPi) + static_cast<float>(c) * 1.571f;
            const Matrix frame = Matrix::CreateRotationZ(rng.signed_(0.30f))
                                 * Matrix::CreateRotationY(yaw)
                                 * Matrix::CreateTranslation(at + Vector3(0.0f, size * 0.48f, 0.0f));
            const float h = size * 0.5f;
            weed.addQuadUv(Vector3::Transform(Vector3(-h, -h, 0.0f), frame),
                           Vector3::Transform(Vector3(h, -h, 0.0f), frame),
                           Vector3::Transform(Vector3(h, h, 0.0f), frame),
                           Vector3::Transform(Vector3(-h, h, 0.0f), frame), Vector2(0.0f, 1.0f),
                           Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f));
        }
    }
}

void PropFactory::lightPool(GeometryCollector& collector, float radius) const
{
    MeshBuilder& pool = collector.builder(&materials_.get(MaterialId::LightPool));
    pool.setUvMode(UvMode::Explicit);
    // A centimetre and a half off the ground. Enough to clear the paving and
    // the road markings without floating: the material does not write depth, so
    // what decides whether a pool is in front of a kerb is the sorted
    // transparent pass, and being *at* ground level makes that call correctly.
    const float y = 0.015f;
    // Facing stated rather than implied. Those four corners in that order wind
    // clockwise seen from above, so the quad's derived normal points at the
    // ground and every pool was back-face culled: forty invisible lights.
    pool.addQuadFacingUv(Vector3(-radius, y, -radius), Vector3(radius, y, -radius),
                         Vector3(radius, y, radius), Vector3(-radius, y, radius),
                         Vector3::Up);
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
