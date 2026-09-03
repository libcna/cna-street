// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/RoadBuilder.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using CnaStreet::Geometry::AlongFrame;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::UvMode;

namespace CnaStreet {

namespace M = Metrics;

namespace {

constexpr float kMainHalfRoad = M::kMainCarriagewayWidth * 0.5f;   // 5.50
constexpr float kSideHalfRoad = M::kSideCarriagewayWidth * 0.5f;   // 3.25
constexpr float kMainLine     = M::kMainStreetHalfWidth;           // 9.30
constexpr float kSideLine     = M::kSideStreetHalfWidth;           // 5.85
constexpr float kMainEnd      = M::kMainStreetHalfLength;
constexpr float kSideEnd      = M::kSideStreetHalfLength;
/// Kerb radius at the junction corners. Small enough to fit inside both footway
/// widths, large enough that a bus could physically make the turn.
constexpr float kCornerRadius = 2.40f;
constexpr float kPaintY       = M::kMarkingLift;
constexpr float kFootwayY     = M::kCurbHeight;

/// A horizontal rectangle at height @p y, wound so its normal is +Y.
void Slab(MeshBuilder& builder, float x0, float z0, float x1, float z1, float y)
{
    builder.addQuad(Vector3(x0, y, z0), Vector3(x0, y, z1), Vector3(x1, y, z1),
                    Vector3(x1, y, z0));
}

/// A painted stripe from one point to another, with explicit UVs so the paint
/// texture runs *along* the line rather than being projected across it.
void Stripe(MeshBuilder& builder, float x0, float z0, float x1, float z1, float width, float y)
{
    const float dx = x1 - x0, dz = z1 - z0;
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < 1e-4f) return;
    const float ux = dx / length, uz = dz / length;
    // Left normal on the ground plane.
    const float px = uz * width * 0.5f, pz = -ux * width * 0.5f;
    const float v1 = length / 0.60f;   // one texture repeat per 60 cm of line

    // Wound so the face points up whichever way the line runs: the corner order
    // here comes from the direction of travel, and half the markings on the
    // street would otherwise face into the ground.
    builder.addQuadUv(Vector3(x0 - px, y, z0 - pz), Vector3(x1 - px, y, z1 - pz),
                      Vector3(x1 + px, y, z1 + pz), Vector3(x0 + px, y, z0 + pz),
                      Vector2(0.0f, 0.0f), Vector2(0.0f, v1), Vector2(1.0f, v1),
                      Vector2(1.0f, 0.0f));
}

/// A broken line: `mark` metres painted, `gap` metres blank.
void BrokenLine(MeshBuilder& builder, float x0, float z0, float x1, float z1, float width,
                float mark, float gap, float y)
{
    const float dx = x1 - x0, dz = z1 - z0;
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < 1e-4f) return;
    const float ux = dx / length, uz = dz / length;
    for (float t = 0.0f; t < length; t += mark + gap)
    {
        const float end = std::min(t + mark, length);
        Stripe(builder, x0 + ux * t, z0 + uz * t, x0 + ux * end, z0 + uz * end, width, y);
    }
}

/// The kerb's cross-section, counter-clockwise in the extrusion's local XY.
/// Local +X points at the footway, +Y is up, and the profile starts at the kerb
/// face. The 20 mm chamfer along the top arris is what catches the light and
/// makes a kerb read as a stone rather than as a step in the mesh.
std::vector<Vector2> KerbProfile()
{
    return {Vector2(0.00f, 0.00f), Vector2(M::kCurbStoneWidth, 0.00f),
            Vector2(M::kCurbStoneWidth, M::kCurbHeight), Vector2(0.022f, M::kCurbHeight),
            Vector2(0.00f, M::kCurbHeight - 0.022f)};
}

std::vector<Vector2> DroppedKerbProfile()
{
    return {Vector2(0.00f, 0.00f), Vector2(M::kCurbStoneWidth, 0.00f),
            Vector2(M::kCurbStoneWidth, M::kCurbDropHeight),
            Vector2(0.014f, M::kCurbDropHeight), Vector2(0.00f, M::kCurbDropHeight - 0.014f)};
}

}  // namespace

RoadBuilder::RoadBuilder(const CityLayout& layout, const MaterialLibrary& materials)
    : layout_(layout), materials_(materials)
{
}

void RoadBuilder::build(GeometryCollector& collector, Rng& rng)
{
    // The four crossings, one on each arm, set back from the junction box so a
    // stopped car does not sit on them.
    crossings_.clear();
    const float mainSetback = kSideLine + 1.4f + M::kZebraDepth * 0.5f;
    const float sideSetback = kMainLine + 1.2f + M::kZebraDepth * 0.5f;
    crossings_.push_back(Crossing{Vector2(0.0f, mainSetback), Vector2(1.0f, 0.0f), kMainHalfRoad,
                                  M::kZebraDepth * 0.5f, true});
    crossings_.push_back(Crossing{Vector2(0.0f, -mainSetback), Vector2(1.0f, 0.0f), kMainHalfRoad,
                                  M::kZebraDepth * 0.5f, true});
    crossings_.push_back(Crossing{Vector2(sideSetback, 0.0f), Vector2(0.0f, 1.0f), kSideHalfRoad,
                                  M::kZebraDepth * 0.5f, false});
    crossings_.push_back(Crossing{Vector2(-sideSetback, 0.0f), Vector2(0.0f, 1.0f), kSideHalfRoad,
                                  M::kZebraDepth * 0.5f, false});

    buildCarriageway(collector, rng);
    buildKerbs(collector);
    buildFootways(collector, rng);
    buildMarkings(collector);
    buildCrossings(collector);
    buildIronwork(collector, rng);
}

void RoadBuilder::buildCarriageway(GeometryCollector& collector, Rng& rng)
{
    const Material* main = &materials_.get(MaterialId::AsphaltMain);
    const Material* side = &materials_.get(MaterialId::AsphaltSide);
    const Material* worn = &materials_.get(MaterialId::AsphaltWorn);

    // The main street, cut into cells so the renderer can throw away the half
    // behind the camera. 34 m of asphalt is one draw call and one frustum test.
    const float step = GeometryCollector::kCellSize;
    for (float z = -kMainEnd; z < kMainEnd; z += step)
    {
        const float z0 = z;
        const float z1 = std::min(z + step, kMainEnd);
        // The junction box is built separately; skip the span it covers.
        if (z1 <= -kSideHalfRoad || z0 >= kSideHalfRoad)
        {
            collector.setRegion(0.0f, (z0 + z1) * 0.5f);
            MeshBuilder& builder = collector.builder(main);
            builder.setTileSize(6.0f);
            Slab(builder, -kMainHalfRoad, z0, kMainHalfRoad, z1, 0.0f);
        }
        else
        {
            // The two slivers either side of the junction box.
            if (z0 < -kSideHalfRoad)
            {
                collector.setRegion(0.0f, z0);
                MeshBuilder& builder = collector.builder(main);
                builder.setTileSize(6.0f);
                Slab(builder, -kMainHalfRoad, z0, kMainHalfRoad, -kSideHalfRoad, 0.0f);
            }
            if (z1 > kSideHalfRoad)
            {
                collector.setRegion(0.0f, z1);
                MeshBuilder& builder = collector.builder(main);
                builder.setTileSize(6.0f);
                Slab(builder, -kMainHalfRoad, kSideHalfRoad, kMainHalfRoad, z1, 0.0f);
            }
        }
    }

    // The side street.
    for (float x = -kSideEnd; x < kSideEnd; x += step)
    {
        const float x0 = x;
        const float x1 = std::min(x + step, kSideEnd);
        if (x1 <= -kMainHalfRoad || x0 >= kMainHalfRoad)
        {
            collector.setRegion((x0 + x1) * 0.5f, 0.0f);
            MeshBuilder& builder = collector.builder(side);
            builder.setTileSize(6.0f);
            Slab(builder, x0, -kSideHalfRoad, x1, kSideHalfRoad, 0.0f);
        }
        else
        {
            if (x0 < -kMainHalfRoad)
            {
                collector.setRegion(x0, 0.0f);
                MeshBuilder& builder = collector.builder(side);
                builder.setTileSize(6.0f);
                Slab(builder, x0, -kSideHalfRoad, -kMainHalfRoad, kSideHalfRoad, 0.0f);
            }
            if (x1 > kMainHalfRoad)
            {
                collector.setRegion(x1, 0.0f);
                MeshBuilder& builder = collector.builder(side);
                builder.setTileSize(6.0f);
                Slab(builder, kMainHalfRoad, -kSideHalfRoad, x1, kSideHalfRoad, 0.0f);
            }
        }
    }

    // The junction box, plus the four corner fillets that carry the kerb radius.
    // Worn asphalt here on purpose: a junction is where every vehicle brakes,
    // turns and stops, and it is visibly more polished and more patched than the
    // straight either side of it.
    collector.setRegion(0.0f, 0.0f);
    {
        MeshBuilder& builder = collector.builder(worn);
        builder.setTileSize(6.0f);
        Slab(builder, -kMainHalfRoad, -kSideHalfRoad, kMainHalfRoad, kSideHalfRoad, 0.0f);

        for (int corner = 0; corner < 4; ++corner)
        {
            const float sx = (corner & 1) ? 1.0f : -1.0f;
            const float sz = (corner & 2) ? 1.0f : -1.0f;
            const float cx = sx * (kMainHalfRoad + kCornerRadius);
            const float cz = sz * (kSideHalfRoad + kCornerRadius);

            // The fillet is the square corner minus a quarter disc: build it as a
            // fan from the square's outer corner out to the arc.
            constexpr int kSegments = 8;
            std::vector<Vector3> fan;
            fan.reserve(kSegments + 2);
            for (int i = 0; i <= kSegments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(kSegments);
                const float angle = t * MathHelper::PiOver2;
                const float px = cx - sx * kCornerRadius * std::cos(angle);
                const float pz = cz - sz * kCornerRadius * std::sin(angle);
                fan.emplace_back(px, 0.0f, pz);
            }
            fan.emplace_back(sx * kMainHalfRoad, 0.0f, sz * kSideHalfRoad);
            // Winding depends on the quadrant; addPolygon takes the normal, so
            // reverse where the fan would otherwise come out inside-out.
            if (sx * sz > 0.0f) std::reverse(fan.begin(), fan.end());
            builder.addPolygon(fan, Vector3::Up);
        }
    }

    // --- wheel tracks ------------------------------------------------------
    // A pair of polished bands down each travel lane, laid as decals along the
    // direction of travel. This cannot live in the road material: a tiling
    // texture has no idea which way the traffic runs, and the version that tried
    // put the bands across the carriageway and repeated them every three metres.
    {
        const Material* track = &materials_.get(MaterialId::WheelTrack);
        const float mainLane = kMainHalfRoad - M::kParkingLaneWidth - M::kMainLaneWidth * 0.5f;
        const float sideLane = kSideHalfRoad * 0.5f;
        // 1.65 m apart, which is about the track width of a European car.
        constexpr float kTrackGap  = 0.82f;
        constexpr float kTrackHalf = 0.36f;
        constexpr float kLift      = M::kMarkingLift * 0.35f;

        auto band = [&](float x0, float z0, float x1, float z1, bool alongZ) {
            for (float t = std::min(alongZ ? z0 : x0, alongZ ? z1 : x1);
                 t < std::max(alongZ ? z0 : x0, alongZ ? z1 : x1); t += 20.0f)
            {
                const float t1 = std::min(t + 20.0f, std::max(alongZ ? z0 : x0, alongZ ? z1 : x1));
                collector.setRegion(alongZ ? x0 : (t + t1) * 0.5f, alongZ ? (t + t1) * 0.5f : z0);
                MeshBuilder& builder = collector.builder(track);
                builder.setUvMode(UvMode::Explicit);
                // One repeat of the band across its width, and one every four
                // metres along it, so the fraying at the edge never lines up.
                const float length = (t1 - t) / 2.6f;
                if (alongZ)
                    builder.addQuadUv(Vector3(x0 - kTrackHalf, kLift, t),
                                      Vector3(x0 - kTrackHalf, kLift, t1),
                                      Vector3(x0 + kTrackHalf, kLift, t1),
                                      Vector3(x0 + kTrackHalf, kLift, t),
                                      Vector2(0.0f, 0.0f), Vector2(length, 0.0f),
                                      Vector2(length, 1.0f), Vector2(0.0f, 1.0f));
                else
                    builder.addQuadUv(Vector3(t, kLift, z0 + kTrackHalf),
                                      Vector3(t1, kLift, z0 + kTrackHalf),
                                      Vector3(t1, kLift, z0 - kTrackHalf),
                                      Vector3(t, kLift, z0 - kTrackHalf),
                                      Vector2(0.0f, 0.0f), Vector2(length, 0.0f),
                                      Vector2(length, 1.0f), Vector2(0.0f, 1.0f));
            }
        };

        for (const float side : {-1.0f, 1.0f})
            for (const float offset : {-kTrackGap, kTrackGap})
            {
                band(side * mainLane + offset, -kMainEnd, side * mainLane + offset, kMainEnd, true);
                band(-kSideEnd, side * sideLane + offset, kSideEnd, side * sideLane + offset, false);
            }
    }

    // Two resurfaced patches on the main street: a real street has been dug up.
    for (int i = 0; i < 3; ++i)
    {
        const float z = rng.range(-kMainEnd * 0.75f, kMainEnd * 0.75f);
        const float x = rng.range(-kMainHalfRoad + 1.2f, kMainHalfRoad - 1.2f);
        const float halfW = rng.range(0.9f, 1.8f);
        const float halfL = rng.range(1.6f, 4.5f);
        collector.setRegion(x, z);
        MeshBuilder& builder = collector.builder(worn);
        builder.setTileSize(3.0f);
        // 2 mm proud, which is what a hot-laid patch actually sits at.
        Slab(builder, x - halfW, z - halfL, x + halfW, z + halfL, 0.002f);
    }
}

void RoadBuilder::buildKerbs(GeometryCollector& collector)
{
    const Material* kerb = &materials_.get(MaterialId::GraniteKerb);
    const std::vector<Vector2> profile = KerbProfile();
    const std::vector<Vector2> dropped = DroppedKerbProfile();

    // A kerb run, split at the crossings where it drops to almost flush.
    auto run = [&](float startX, float startZ, float dirX, float dirZ, float length,
                   bool crossesMain) {
        const float step = GeometryCollector::kCellSize;
        float travelled = 0.0f;
        while (travelled < length - 0.01f)
        {
            const float piece = std::min(step, length - travelled);
            const float x = startX + dirX * travelled;
            const float z = startZ + dirZ * travelled;

            // Is this piece inside a dropped-kerb zone?
            float remaining = piece;
            float local = 0.0f;
            while (local < piece - 0.01f)
            {
                const float px = x + dirX * local;
                const float pz = z + dirZ * local;
                bool dropZone = false;
                for (const Crossing& crossing : crossings_)
                {
                    if (crossing.crossesMain != crossesMain) continue;
                    const float alongMain = crossing.crossesMain ? pz : px;
                    const float centre = crossing.crossesMain ? crossing.centre.Y
                                                              : crossing.centre.X;
                    if (std::fabs(alongMain - centre) < crossing.halfDepth + 0.4f)
                    {
                        dropZone = true;
                        break;
                    }
                }
                const float chunk = std::min(1.0f, piece - local);
                collector.setRegion(px, pz);
                MeshBuilder& builder = collector.builder(kerb);
                builder.setTileSize(1.0f, 0.35f);
                builder.addExtrusion(dropZone ? dropped : profile,
                                     AlongFrame(Vector3(px, 0.0f, pz), dirX, dirZ), chunk, false,
                                     false);
                local += chunk;
            }
            (void)remaining;
            travelled += piece;
        }
    };

    // Main street: kerbs on both sides, north and south of the junction. Local
    // +X of the extrusion frame is to the *left* of the heading, so each run is
    // aimed so that its left is the footway.
    run(-kMainHalfRoad, kSideLine, 0.0f, 1.0f, kMainEnd - kSideLine, true);
    run(-kMainHalfRoad, -kSideLine, 0.0f, -1.0f, kMainEnd - kSideLine, true);
    run(kMainHalfRoad, kSideLine, 0.0f, 1.0f, kMainEnd - kSideLine, true);
    run(kMainHalfRoad, -kSideLine, 0.0f, -1.0f, kMainEnd - kSideLine, true);
    // Side street.
    run(kMainLine, kSideHalfRoad, 1.0f, 0.0f, kSideEnd - kMainLine, false);
    run(-kMainLine, kSideHalfRoad, -1.0f, 0.0f, kSideEnd - kMainLine, false);
    run(kMainLine, -kSideHalfRoad, 1.0f, 0.0f, kSideEnd - kMainLine, false);
    run(-kMainLine, -kSideHalfRoad, -1.0f, 0.0f, kSideEnd - kMainLine, false);

    // The four corner arcs. Built as short straight pieces around the fillet,
    // which at 8 segments over a 2.4 m radius is a 17 cm chord — below what the
    // eye resolves at street level and far cheaper than a swept surface.
    collector.setRegion(0.0f, 0.0f);
    for (int corner = 0; corner < 4; ++corner)
    {
        const float sx = (corner & 1) ? 1.0f : -1.0f;
        const float sz = (corner & 2) ? 1.0f : -1.0f;
        const float cx = sx * (kMainHalfRoad + kCornerRadius);
        const float cz = sz * (kSideHalfRoad + kCornerRadius);
        constexpr int kSegments = 8;
        for (int i = 0; i < kSegments; ++i)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
            const float a0 = t0 * MathHelper::PiOver2;
            const float a1 = t1 * MathHelper::PiOver2;
            const float x0 = cx - sx * kCornerRadius * std::cos(a0);
            const float z0 = cz - sz * kCornerRadius * std::sin(a0);
            const float x1 = cx - sx * kCornerRadius * std::cos(a1);
            const float z1 = cz - sz * kCornerRadius * std::sin(a1);
            float dx = x1 - x0, dz = z1 - z0;
            const float length = std::sqrt(dx * dx + dz * dz);
            if (length < 1e-4f) continue;
            dx /= length; dz /= length;
            // Orient each chord so its left side is the footway (outside the arc).
            const float leftX = dz, leftZ = -dx;
            const float outwardX = x0 - cx, outwardZ = z0 - cz;
            const bool leftIsOutside = leftX * outwardX + leftZ * outwardZ < 0.0f;
            MeshBuilder& builder = collector.builder(kerb);
            builder.setTileSize(1.0f, 0.35f);
            if (leftIsOutside)
                builder.addExtrusion(profile, AlongFrame(Vector3(x0, 0.0f, z0), dx, dz), length,
                                     false, false);
            else
                builder.addExtrusion(profile, AlongFrame(Vector3(x1, 0.0f, z1), -dx, -dz), length,
                                     false, false);
        }
    }
}

void RoadBuilder::buildFootways(GeometryCollector& collector, Rng& rng)
{
    const Material* paving = &materials_.get(MaterialId::ConcretePaving);
    const Material* setts  = &materials_.get(MaterialId::GraniteSetts);
    const Material* tactile = &materials_.get(MaterialId::TactilePaving);

    // The gutter channel: two courses of setts laid flat against the kerb, which
    // is where the water runs and where the dirt collects.
    auto gutter = [&](float x0, float z0, float x1, float z1, float towardRoadX,
                      float towardRoadZ) {
        const float step = GeometryCollector::kCellSize;
        const float dx = x1 - x0, dz = z1 - z0;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < 1e-4f) return;
        const float ux = dx / length, uz = dz / length;
        for (float t = 0.0f; t < length; t += step)
        {
            const float end = std::min(t + step, length);
            const float ax = x0 + ux * t, az = z0 + uz * t;
            const float bx = x0 + ux * end, bz = z0 + uz * end;
            collector.setRegion((ax + bx) * 0.5f, (az + bz) * 0.5f);
            MeshBuilder& builder = collector.builder(setts);
            builder.setTileSize(0.8f);
            const float ox = towardRoadX * M::kGutterWidth, oz = towardRoadZ * M::kGutterWidth;
            builder.addQuadFacing(Vector3(ax, 0.004f, az), Vector3(bx, 0.004f, bz),
                                  Vector3(bx + ox, 0.004f, bz + oz),
                                  Vector3(ax + ox, 0.004f, az + oz), Vector3::Up);
        }
    };

    // Main street footways and their gutters.
    for (const float sign : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
        {
            const float inner = sign * kMainHalfRoad;
            const float outer = sign * kMainLine;
            const float from  = half * kSideLine;
            const float to    = half * kMainEnd;
            const float step  = GeometryCollector::kCellSize;
            for (float t = 0.0f; t < std::fabs(to - from); t += step)
            {
                const float z0 = from + half * t;
                const float z1 = from + half * std::min(t + step, std::fabs(to - from));
                collector.setRegion(inner, (z0 + z1) * 0.5f);
                MeshBuilder& builder = collector.builder(paving);
                builder.setTileSize(1.5f);
                Slab(builder, std::min(inner, outer), std::min(z0, z1), std::max(inner, outer),
                     std::max(z0, z1), kFootwayY);
            }
            gutter(inner, from, inner, to, -sign, 0.0f);
        }

    // Side street footways.
    for (const float sign : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
        {
            const float inner = sign * kSideHalfRoad;
            const float outer = sign * kSideLine;
            const float from  = half * kMainLine;
            const float to    = half * kSideEnd;
            const float step  = GeometryCollector::kCellSize;
            for (float t = 0.0f; t < std::fabs(to - from); t += step)
            {
                const float x0 = from + half * t;
                const float x1 = from + half * std::min(t + step, std::fabs(to - from));
                collector.setRegion((x0 + x1) * 0.5f, inner);
                MeshBuilder& builder = collector.builder(paving);
                builder.setTileSize(1.5f);
                Slab(builder, std::min(x0, x1), std::min(inner, outer), std::max(x0, x1),
                     std::max(inner, outer), kFootwayY);
            }
            gutter(from, inner, to, inner, 0.0f, -sign);
        }

    // The four corner footways: the square between the two building lines, minus
    // the quarter disc the kerb radius takes out of it.
    collector.setRegion(0.0f, 0.0f);
    for (int corner = 0; corner < 4; ++corner)
    {
        const float sx = (corner & 1) ? 1.0f : -1.0f;
        const float sz = (corner & 2) ? 1.0f : -1.0f;
        const float cx = sx * (kMainHalfRoad + kCornerRadius);
        const float cz = sz * (kSideHalfRoad + kCornerRadius);

        MeshBuilder& builder = collector.builder(paving);
        builder.setTileSize(1.5f);
        constexpr int kSegments = 8;
        std::vector<Vector3> outline;
        outline.reserve(kSegments + 4);
        for (int i = 0; i <= kSegments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kSegments);
            const float angle = t * MathHelper::PiOver2;
            outline.emplace_back(cx - sx * kCornerRadius * std::cos(angle), kFootwayY,
                                 cz - sz * kCornerRadius * std::sin(angle));
        }
        outline.emplace_back(sx * kMainHalfRoad, kFootwayY, sz * kSideLine);
        outline.emplace_back(sx * kMainLine, kFootwayY, sz * kSideLine);
        outline.emplace_back(sx * kMainLine, kFootwayY, sz * kSideHalfRoad);
        if (sx * sz < 0.0f) std::reverse(outline.begin(), outline.end());
        builder.addPolygon(outline, Vector3::Up);
    }

    // Tactile paving each side of every crossing, and a strip of setts where the
    // footway meets the dropped kerb.
    for (const Crossing& crossing : crossings_)
    {
        for (const float side : {-1.0f, 1.0f})
        {
            const float cx = crossing.centre.X
                             + crossing.walkDirection.X * side
                                   * (crossing.halfLength + M::kMainSidewalkWidth * 0.28f);
            const float cz = crossing.centre.Y
                             + crossing.walkDirection.Y * side
                                   * (crossing.halfLength + M::kMainSidewalkWidth * 0.28f);
            collector.setRegion(cx, cz);
            MeshBuilder& builder = collector.builder(tactile);
            builder.setTileSize(0.40f);
            const float halfAlong = crossing.halfDepth * 0.85f;
            const float halfAcross = 0.60f;
            const float ax = crossing.walkDirection.X, az = crossing.walkDirection.Y;
            // Across the walking direction is the crossing's own width.
            const float bx = -az, bz = ax;
            builder.addQuadFacing(
                Vector3(cx - ax * halfAcross - bx * halfAlong, kFootwayY + 0.004f,
                        cz - az * halfAcross - bz * halfAlong),
                Vector3(cx - ax * halfAcross + bx * halfAlong, kFootwayY + 0.004f,
                        cz - az * halfAcross + bz * halfAlong),
                Vector3(cx + ax * halfAcross + bx * halfAlong, kFootwayY + 0.004f,
                        cz + az * halfAcross + bz * halfAlong),
                Vector3(cx + ax * halfAcross - bx * halfAlong, kFootwayY + 0.004f,
                        cz + az * halfAcross - bz * halfAlong), Vector3::Up);
        }
    }
    (void)rng;
}

void RoadBuilder::buildMarkings(GeometryCollector& collector)
{
    const Material* paint = &materials_.get(MaterialId::RoadMarking);

    auto builderAt = [&](float x, float z) -> MeshBuilder& {
        collector.setRegion(x, z);
        MeshBuilder& builder = collector.builder(paint);
        builder.setUvMode(UvMode::Explicit);
        return builder;
    };

    // Centre line on the main street: broken along the straight, solid for the
    // last 18 m into the junction where overtaking is prohibited.
    const float step = GeometryCollector::kCellSize;
    for (const float half : {-1.0f, 1.0f})
    {
        const float solidFrom = half * (kSideLine + 1.0f);
        const float solidTo   = half * (kSideLine + 18.0f);
        Stripe(builderAt(0.0f, (solidFrom + solidTo) * 0.5f), 0.0f, solidFrom, 0.0f, solidTo,
               M::kLaneMarkWidth, kPaintY);
        for (float z = std::fabs(solidTo); z < kMainEnd; z += step)
        {
            const float z0 = half * z;
            const float z1 = half * std::min(z + step, kMainEnd);
            BrokenLine(builderAt(0.0f, (z0 + z1) * 0.5f), 0.0f, z0, 0.0f, z1, M::kLaneMarkWidth,
                       M::kCentreLineMark, M::kCentreLineGap, kPaintY);
        }
    }

    // Parking-lane edge lines, and the bay ticks that divide them. The line
    // stops well short of the junction, which is why the turning lane exists.
    for (const float sign : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
        {
            const float x = sign * (kMainHalfRoad - M::kParkingLaneWidth);
            const float from = half * (kSideLine + 22.0f);
            const float to   = half * (kMainEnd - 4.0f);
            for (float t = 0.0f; t < std::fabs(to - from); t += step)
            {
                const float z0 = from + half * t;
                const float z1 = from + half * std::min(t + step, std::fabs(to - from));
                BrokenLine(builderAt(x, (z0 + z1) * 0.5f), x, z0, x, z1, M::kLaneMarkWidth, 1.0f,
                           1.0f, kPaintY);
            }
            for (float t = 0.0f; t < std::fabs(to - from); t += M::kParkingBayLength)
            {
                const float z = from + half * t;
                Stripe(builderAt(x, z), x, z, sign * kMainHalfRoad, z, M::kLaneMarkWidth * 0.8f,
                       kPaintY);
            }
        }

    // Stop lines on all four approaches, one lane wide each, one metre short of
    // the crossing.
    for (const Crossing& crossing : crossings_)
    {
        const float sign = crossing.crossesMain
                               ? (crossing.centre.Y > 0.0f ? 1.0f : -1.0f)
                               : (crossing.centre.X > 0.0f ? 1.0f : -1.0f);
        if (crossing.crossesMain)
        {
            const float z = crossing.centre.Y
                            + sign * (crossing.halfDepth + M::kStopLineSetback);
            // Only the approaching half of the carriageway carries a stop line.
            const float x0 = sign > 0.0f ? 0.0f : -kMainHalfRoad;
            const float x1 = sign > 0.0f ? kMainHalfRoad : 0.0f;
            Stripe(builderAt(0.0f, z), x0, z, x1, z, M::kStopLineWidth, kPaintY);
        }
        else
        {
            const float x = crossing.centre.X
                            + sign * (crossing.halfDepth + M::kStopLineSetback);
            const float z0 = sign > 0.0f ? -kSideHalfRoad : 0.0f;
            const float z1 = sign > 0.0f ? 0.0f : kSideHalfRoad;
            Stripe(builderAt(x, 0.0f), x, z0, x, z1, M::kStopLineWidth, kPaintY);
        }
    }

    // Side-street centre lines, short and broken.
    for (const float half : {-1.0f, 1.0f})
    {
        const float from = half * (kMainLine + 6.0f);
        const float to   = half * (kSideEnd - 3.0f);
        BrokenLine(builderAt((from + to) * 0.5f, 0.0f), from, 0.0f, to, 0.0f, M::kLaneMarkWidth,
                   2.0f, 3.0f, kPaintY);
    }
}

void RoadBuilder::buildCrossings(GeometryCollector& collector)
{
    const Material* paint = &materials_.get(MaterialId::RoadMarking);

    for (const Crossing& crossing : crossings_)
    {
        collector.setRegion(crossing.centre.X, crossing.centre.Y);
        MeshBuilder& builder = collector.builder(paint);
        builder.setUvMode(UvMode::Explicit);

        // Stripes run parallel to the traffic, i.e. across the walking
        // direction, repeated along it. 50 cm painted, 50 cm gap.
        const float ax = crossing.walkDirection.X, az = crossing.walkDirection.Y;
        const float bx = -az, bz = ax;   // along the road
        const float pitch = M::kZebraStripeWidth + M::kZebraStripeGap;
        const int   count = static_cast<int>(crossing.halfLength * 2.0f / pitch);
        const float span  = static_cast<float>(count) * pitch - M::kZebraStripeGap;
        const float start = -span * 0.5f + M::kZebraStripeWidth * 0.5f;

        for (int i = 0; i < count; ++i)
        {
            const float offset = start + static_cast<float>(i) * pitch;
            const float sx = crossing.centre.X + ax * offset;
            const float sz = crossing.centre.Y + az * offset;
            Stripe(builder, sx - bx * crossing.halfDepth, sz - bz * crossing.halfDepth,
                   sx + bx * crossing.halfDepth, sz + bz * crossing.halfDepth,
                   M::kZebraStripeWidth, kPaintY);
        }
    }
}

void RoadBuilder::buildIronwork(GeometryCollector& collector, Rng& rng)
{
    const Material* iron  = &materials_.get(MaterialId::ManholeIron);
    const Material* grate = &materials_.get(MaterialId::DrainGrate);

    // Manhole covers down the crown of the road, roughly where a sewer runs, and
    // a few off to the side for the services.
    for (float z = -kMainEnd + 12.0f; z < kMainEnd; z += rng.range(24.0f, 38.0f))
    {
        if (std::fabs(z) < kSideLine + 3.0f) continue;
        const float x = rng.range(-1.4f, 1.4f);
        collector.setRegion(x, z);
        MeshBuilder& builder = collector.builder(iron);
        builder.setUvMode(UvMode::Explicit);
        const float r = M::kManholeRadius;
        // A square patch carrying the round cover's texture: the alpha is
        // opaque everywhere, and a disc of geometry would need 20 triangles to
        // avoid a polygonal silhouette that the texture already draws for free.
        builder.addQuadUnitUv(Vector3(x - r, 0.006f, z - r), Vector3(x - r, 0.006f, z + r),
                              Vector3(x + r, 0.006f, z + r), Vector3(x + r, 0.006f, z - r));
    }

    // Gullies against the kerb, at the low point of each length.
    for (const float sign : {-1.0f, 1.0f})
        for (float z = -kMainEnd + 8.0f; z < kMainEnd; z += rng.range(16.0f, 26.0f))
        {
            if (std::fabs(z) < kSideLine + 2.0f) continue;
            const float x = sign * (kMainHalfRoad - M::kGullyDepth * 0.5f - 0.06f);
            collector.setRegion(x, z);
            MeshBuilder& builder = collector.builder(grate);
            builder.setUvMode(UvMode::Explicit);
            builder.addQuadUnitUv(
                Vector3(x - M::kGullyDepth * 0.5f, 0.005f, z - M::kGullyWidth * 0.5f),
                Vector3(x - M::kGullyDepth * 0.5f, 0.005f, z + M::kGullyWidth * 0.5f),
                Vector3(x + M::kGullyDepth * 0.5f, 0.005f, z + M::kGullyWidth * 0.5f),
                Vector3(x + M::kGullyDepth * 0.5f, 0.005f, z - M::kGullyWidth * 0.5f));
        }
}

}  // namespace CnaStreet
