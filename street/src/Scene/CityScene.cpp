// SPDX-License-Identifier: MIT
#include "CnaStreet/Scene/CityScene.hpp"

#include "CnaStreet/Assets/Noise.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "CnaStreet/Render/SkinnedGpuMesh.hpp"

#include "CnaStreet/Assets/SignFactory.hpp"
#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Props/BuildingBuilder.hpp"
#include "CnaStreet/Render/GpuMesh.hpp"
#include "CnaStreet/Render/SceneRenderer.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include "CNA/Logger.hpp"
#include "System/Diagnostics/Stopwatch.hpp"

#include <algorithm>
#include <cmath>
#include <set>

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using System::Diagnostics::Stopwatch;
using CnaStreet::Geometry::BoxFaces;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::Place;

namespace CnaStreet {

namespace M = Metrics;

CityScene::CityScene(GraphicsDevice& device, SceneRenderer& renderer, MaterialLibrary& materials,
                     ModelLibrary& models)
    : device_(device), renderer_(renderer), materials_(materials), models_(models)
{
}

CityScene::~CityScene() = default;

const GpuMesh* CityScene::upload(const Geometry::MeshData& data, const std::string& name)
{
    if (data.empty()) return nullptr;
    meshes_.push_back(std::make_unique<GpuMesh>(device_, data, name));
    buildStats_.triangles += static_cast<std::size_t>(meshes_.back()->triangleCount());
    buildStats_.meshBytes += meshes_.back()->gpuBytes();
    return meshes_.back().get();
}

void CityScene::publish(GeometryCollector& collector, float cullDistance, float shadowDistance)
{
    std::vector<GeometryCollector::Batch> batches = collector.take();
    int index = 0;
    for (GeometryCollector::Batch& batch : batches)
    {
        const std::string name = batch.material->name + "#" + std::to_string(batch.region) + "."
                                 + std::to_string(index++);
        const GpuMesh* mesh = upload(batch.mesh, name);
        if (mesh == nullptr) continue;

        SceneItem item;
        item.mesh           = mesh;
        item.material       = batch.material;
        item.cullDistance   = cullDistance;
        // The material's own cap wins where it has one: a window frame stops
        // casting long before the wall it is set into does.
        item.shadowDistance = batch.material->shadowDistance > 0.0f
                                  ? (shadowDistance > 0.0f
                                         ? std::min(shadowDistance,
                                                    batch.material->shadowDistance)
                                         : batch.material->shadowDistance)
                                  : shadowDistance;
        renderer_.addItem(item);
        ++buildStats_.staticBatches;
    }
}

void CityScene::build(const RenderSettings& settings)
{
    Stopwatch watch = Stopwatch::StartNew();

    CNA::Logger::Info("cna-street: generating materials");
    materials_.build(settings.seed);
    if (settings.nightLighting()) lightTheStreet(settings);

    CNA::Logger::Info("cna-street: laying out the street");
    layout_.generate(settings.seed);
    buildStats_.plots = static_cast<int>(layout_.plots().size());

    CNA::Logger::Info("cna-street: building the highway");
    {
        GeometryCollector collector;
        Rng rng = Rng::derive(settings.seed, "highway");
        RoadBuilder roads(layout_, materials_);
        roads.build(collector, rng);
        crossings_ = roads.crossings();
        publish(collector, 0.0f, settings.shadowDistance);
    }

    CNA::Logger::Info("cna-street: raising the buildings");
    {
        GeometryCollector collector;
        GeometryCollector interiors;
        Rng rng = Rng::derive(settings.seed, "buildings");
        BuildingBuilder buildings(materials_, layout_);
        const std::vector<Plot>& plots = layout_.plots();
        for (std::size_t i = 0; i < plots.size(); ++i)
            buildings.build(plots[i], static_cast<int>(i), collector, interiors, rng, anchors_,
                            displays_);
        publish(collector, 0.0f, settings.shadowDistance);
        // The rooms behind the glass, on a short leash and casting nothing: a
        // shop interior is invisible from the far pavement and its shadow is
        // invisible from anywhere. 34 m rather than the 54 it started at,
        // because a room seen obliquely through its own mullions past the width
        // of the street contributes a smear, and it contributes it in seven
        // draw calls per shop.
        publish(interiors, 34.0f, 0.0f);
    }

    CNA::Logger::Info("cna-street: dressing the windows");
    buildShopDisplays(settings);

    if (settings.streetFurniture)
    {
        CNA::Logger::Info("cna-street: placing street furniture");
        Rng rng = Rng::derive(settings.seed, "furniture");
        buildStreetFurniture(rng, settings);
    }
    if (settings.vegetation)
    {
        CNA::Logger::Info("cna-street: planting");
        Rng rng = Rng::derive(settings.seed, "vegetation");
        buildVegetation(rng, settings);
    }
    CNA::Logger::Info("cna-street: lettering the shopfronts");
    {
        Rng rng = Rng::derive(settings.seed, "signage");
        buildSignage(rng, settings);
    }
    CNA::Logger::Info("cna-street: signalling the junction");
    {
        Rng rng = Rng::derive(settings.seed, "signals");
        buildSignalsAndSigns(rng, settings);
    }
    CNA::Logger::Info("cna-street: traffic and people");
    buildTrafficAndPeople(settings);

    // After the trees and the vehicles, because the district beyond the
    // frontage borrows both: the same far trees at the same pitch, and the
    // same distant car bodies parked along its kerbs.
    CNA::Logger::Info("cna-street: closing the skyline");
    {
        GeometryCollector collector;
        Rng rng = Rng::derive(settings.seed, "context");
        buildContext(collector, rng, settings);
        publish(collector, 0.0f, 0.0f);
    }

    buildViewpoints();

    buildStats_.buildSeconds = static_cast<float>(watch.getElapsedTicksProperty()) / 1.0e7f;
    CNA::Logger::Info("cna-street: scene built in "
                      + std::to_string(buildStats_.buildSeconds) + " s -- "
                      + std::to_string(buildStats_.staticBatches) + " batches, "
                      + std::to_string(buildStats_.triangles) + " triangles, "
                      + std::to_string(buildStats_.meshBytes / (1024u * 1024u)) + " MiB");

    // Everything static is registered; capture what it looks like from the
    // carriageway, so the things that move through it have a street to reflect.
    // Timed and logged on its own, after the build it depends on.
    CNA::Logger::Info("cna-street: capturing reflection probes");
    renderer_.bakeReflectionProbes(probePositions(settings), settings);
}

void CityScene::buildContext(GeometryCollector& collector, Rng& rng,
                             const RenderSettings& settings)
{
    // Everything outside the modelled block. Two jobs: give the street a ground
    // to stand on so the horizon is not empty, and close the view down each arm
    // with more city, because a street that ends in sky at 130 m is a set.
    //
    // Two grades of it. The blocks that continue the two streets past the
    // modelled frontage are *near* context -- the first of them starts 136 m
    // from the junction and a viewer at the far end of the modelled street is
    // standing next to it -- and they carry real window openings, a shopfront
    // strip, a plinth and a cornice, because at forty metres a painted window
    // is a sticker and a recessed one is a window. The scatter of blocks on the
    // skyline beyond is 240 m away and stays painted.
    const Material* ground = &materials_.get(MaterialId::AsphaltWorn);
    const Material* grass  = &materials_.get(MaterialId::Grass);

    // The ground plane, in cells so it culls, and set a little below the road so
    // it can never win a depth fight with it.
    constexpr float kReach = 460.0f;
    constexpr float kStep  = 38.0f;
    for (float x = -kReach; x < kReach; x += kStep)
        for (float z = -kReach; z < kReach; z += kStep)
        {
            const float x1 = std::min(x + kStep, kReach);
            const float z1 = std::min(z + kStep, kReach);
            // Varied at 38 m and batched at 152 m. The variation has to be fine
            // or the surroundings read as a chequerboard of fields from any
            // camera above the roofline; the batching has to be coarse or the
            // ground plane alone is five hundred draw calls of two triangles
            // each.
            constexpr float kBatch = 152.0f;
            const int coarseX = static_cast<int>(std::floor((x + x1) * 0.5f / kBatch));
            const int coarseZ = static_cast<int>(std::floor((z + z1) * 0.5f / kBatch));
            collector.setRegionKey(1000000 + coarseX * 64 + coarseZ);
            // Mostly the hard surface a dense district actually is, with the
            // occasional green cell for a park or a courtyard, correlated so
            // green cells touch and read as two or three parks.
            const float greenNoise = Noise::fbm((x + kReach) * 0.011f, (z + kReach) * 0.011f, 64,
                                                2, 2.0f, 0.5f, 7717u);
            const Material* surface = greenNoise > 0.72f ? grass : ground;
            MeshBuilder& builder = collector.builder(surface);
            builder.setTileSize(8.0f);
            builder.addQuad(Vector3(x, -0.04f, z), Vector3(x, -0.04f, z1), Vector3(x1, -0.04f, z1),
                            Vector3(x1, -0.04f, z));
        }

    // --- the streets continue --------------------------------------------
    // Carriageway at road level, a footway at kerb height each side, and the
    // kerb face between them, out to where the far block closes the view. The
    // modelled highway stops at 130 m; without this the street beyond it was a
    // flat plane the buildings stood on, with no kerb line to carry the eye.
    const Material* asphalt = &materials_.get(MaterialId::AsphaltMain);
    const Material* paving  = &materials_.get(MaterialId::ConcretePaving);
    const Material* kerb    = &materials_.get(MaterialId::GraniteKerb);
    auto street = [&](bool alongZ, float from, float to, float halfRoad, float line) {
        const float sign = to > from ? 1.0f : -1.0f;
        for (float s0 = from; sign * (to - s0) > 0.5f; s0 += sign * GeometryCollector::kCellSize)
        {
            const float s1 = sign > 0.0f ? std::min(s0 + GeometryCollector::kCellSize, to)
                                         : std::max(s0 - GeometryCollector::kCellSize, to);
            const float a = std::min(s0, s1), b = std::max(s0, s1);
            collector.setRegion(alongZ ? 0.0f : (a + b) * 0.5f, alongZ ? (a + b) * 0.5f : 0.0f);
            MeshBuilder& road = collector.builder(asphalt);
            road.setTileSize(5.0f);
            MeshBuilder& slab = collector.builder(paving);
            slab.setTileSize(4.0f);
            MeshBuilder& stone = collector.builder(kerb);
            stone.setTileSize(1.0f, 0.35f);
            const float y = M::kCurbHeight;
            if (alongZ)
            {
                road.addQuad(Vector3(-halfRoad, 0.0f, a), Vector3(-halfRoad, 0.0f, b),
                             Vector3(halfRoad, 0.0f, b), Vector3(halfRoad, 0.0f, a));
                for (const float side : {-1.0f, 1.0f})
                {
                    const float x0 = side * halfRoad, x1 = side * line;
                    slab.addQuadFacing(Vector3(x0, y, a), Vector3(x0, y, b), Vector3(x1, y, b),
                                       Vector3(x1, y, a), Vector3::Up);
                    stone.addQuadFacing(Vector3(x0, 0.0f, a), Vector3(x0, 0.0f, b),
                                        Vector3(x0, y, b), Vector3(x0, y, a),
                                        Vector3(-side, 0.0f, 0.0f));
                }
            }
            else
            {
                road.addQuad(Vector3(a, 0.0f, -halfRoad), Vector3(a, 0.0f, halfRoad),
                             Vector3(b, 0.0f, halfRoad), Vector3(b, 0.0f, -halfRoad));
                for (const float side : {-1.0f, 1.0f})
                {
                    const float z0 = side * halfRoad, z1 = side * line;
                    slab.addQuadFacing(Vector3(a, y, z0), Vector3(b, y, z0), Vector3(b, y, z1),
                                       Vector3(a, y, z1), Vector3::Up);
                    stone.addQuadFacing(Vector3(a, 0.0f, z0), Vector3(b, 0.0f, z0),
                                        Vector3(b, y, z0), Vector3(a, y, z0),
                                        Vector3(0.0f, 0.0f, -side));
                }
            }
        }
    };
    const float mainHalfRoad = M::kMainCarriagewayWidth * 0.5f;
    const float sideHalfRoad = M::kSideCarriagewayWidth * 0.5f;
    for (const float sign : {-1.0f, 1.0f})
    {
        street(true, sign * M::kMainStreetHalfLength, sign * 322.0f, mainHalfRoad,
               M::kMainStreetHalfWidth);
        street(false, sign * M::kSideStreetHalfLength, sign * 203.0f, sideHalfRoad,
               M::kSideStreetHalfWidth);
    }

    // --- the blocks --------------------------------------------------------
    const MaterialId walls[] = {MaterialId::ContextFacade0, MaterialId::ContextFacade1,
                                MaterialId::ContextFacade2, MaterialId::ContextFacade3,
                                MaterialId::ContextFacade4, MaterialId::ContextFacade5};
    const MaterialId renders[] = {MaterialId::RenderCream, MaterialId::RenderOchre,
                                  MaterialId::RenderSage, MaterialId::RenderGrey,
                                  MaterialId::RenderTerracotta, MaterialId::BrickRed,
                                  MaterialId::BrickBuff, MaterialId::RenderWhite};

    // A roof for a block: pitched with stacks, or flat with plant. A district
    // of flat roofs seen from above is a district of grey rectangles; the
    // pitched ones are what give a roofscape its texture.
    auto roof = [&](float cx, float cz, float halfX, float halfZ, float height,
                    const Material* gableMaterial, float storey) {
        if (rng.chance(0.45f))
        {
            MeshBuilder& tiles = collector.builder(&materials_.get(MaterialId::RoofTile));
            tiles.setTileSize(1.4f);
            const bool alongX = halfX >= halfZ;
            const float rise = std::min(alongX ? halfZ : halfX, 4.2f) * 0.85f;
            const float x0 = cx - halfX - 0.25f, x1 = cx + halfX + 0.25f;
            const float z0 = cz - halfZ - 0.25f, z1 = cz + halfZ + 0.25f;
            MeshBuilder& gable = collector.builder(gableMaterial);
            gable.setTileSize(storey * 1.35f, storey);
            if (alongX)
            {
                const float mz = (z0 + z1) * 0.5f;
                tiles.addQuadFacing(Vector3(x0, height, z0), Vector3(x1, height, z0),
                                    Vector3(x1, height + rise, mz), Vector3(x0, height + rise, mz),
                                    Vector3::Up);
                tiles.addQuadFacing(Vector3(x0, height, z1), Vector3(x1, height, z1),
                                    Vector3(x1, height + rise, mz), Vector3(x0, height + rise, mz),
                                    Vector3::Up);
                gable.addTriangle(Vector3(x0, height, z0), Vector3(x0, height, z1),
                                  Vector3(x0, height + rise, mz));
                gable.addTriangle(Vector3(x1, height, z1), Vector3(x1, height, z0),
                                  Vector3(x1, height + rise, mz));
            }
            else
            {
                const float mx = (x0 + x1) * 0.5f;
                tiles.addQuadFacing(Vector3(x0, height, z0), Vector3(x0, height, z1),
                                    Vector3(mx, height + rise, z1), Vector3(mx, height + rise, z0),
                                    Vector3::Up);
                tiles.addQuadFacing(Vector3(x1, height, z0), Vector3(x1, height, z1),
                                    Vector3(mx, height + rise, z1), Vector3(mx, height + rise, z0),
                                    Vector3::Up);
                gable.addTriangle(Vector3(x0, height, z0), Vector3(x1, height, z0),
                                  Vector3(mx, height + rise, z0));
                gable.addTriangle(Vector3(x1, height, z1), Vector3(x0, height, z1),
                                  Vector3(mx, height + rise, z1));
            }
            // A stack or two, because a pitched roof without chimneys is a tent.
            MeshBuilder& stack = collector.builder(&materials_.get(MaterialId::BrickRed));
            stack.setTileSize(0.9f);
            const int stacks = rng.intRange(1, 3);
            for (int i = 0; i < stacks; ++i)
            {
                const float sx = cx + rng.signed_(halfX * 0.7f);
                const float sz = cz + rng.signed_(halfZ * 0.35f);
                stack.addBox(Vector3(sx - 0.45f, height, sz - 0.35f),
                             Vector3(sx + 0.45f, height + rise + rng.range(0.6f, 1.4f), sz + 0.35f),
                             BoxFaces::allButBottom());
            }
        }
        else
        {
            MeshBuilder& felt = collector.builder(&materials_.get(MaterialId::RoofFelt));
            felt.setTileSize(4.0f);
            felt.addBox(Vector3(cx - halfX - 0.1f, height, cz - halfZ - 0.1f),
                        Vector3(cx + halfX + 0.1f, height + 0.9f, cz + halfZ + 0.1f),
                        BoxFaces::allButBottom());
            // Roof furniture: plant, a lift overrun, a couple of vents. Flat
            // roofs are never empty and from any camera above the eaves that is
            // the difference between a city and a set of boxes.
            MeshBuilder& plant = collector.builder(&materials_.get(MaterialId::GalvanisedSteel));
            plant.setTileSize(1.2f);
            const int units = rng.intRange(1, 4);
            for (int i = 0; i < units; ++i)
            {
                const float px = cx + rng.signed_(halfX * 0.62f);
                const float pz = cz + rng.signed_(halfZ * 0.62f);
                const float pw = rng.range(0.9f, 2.6f);
                const float pd = rng.range(0.9f, 2.2f);
                plant.addBox(Vector3(px - pw * 0.5f, height + 0.55f, pz - pd * 0.5f),
                             Vector3(px + pw * 0.5f, height + 0.55f + rng.range(0.7f, 2.4f),
                                     pz + pd * 0.5f),
                             BoxFaces::allButBottom());
            }
        }
    };

    // A far block: a box carrying a tiling image of a storey.
    auto paintedBlock = [&](float cx, float cz, float halfX, float halfZ, float height) {
        collector.setRegion(cx, cz);
        const Material* material = &materials_.get(walls[rng.index(std::size(walls))]);
        MeshBuilder& builder = collector.builder(material);
        // One tile is one storey. Setting it to anything else is what makes a
        // painted façade read as wallpaper: the windows come out the wrong size
        // for the building and the eye finds it instantly.
        const float storey = rng.range(3.05f, 3.55f);
        builder.setTileSize(storey * 1.35f, storey);
        // Aligned so a tile boundary lands on the ground rather than wherever
        // the world origin happens to fall.
        builder.setUvOffset(Vector2(0.0f, 0.16f));
        builder.addBox(Vector3(cx - halfX, 0.0f, cz - halfZ),
                       Vector3(cx + halfX, height, cz + halfZ), BoxFaces::allButBottom());
        builder.setUvOffset(Vector2::Zero);
        roof(cx, cz, halfX, halfZ, height, material, storey);
    };

    // A near block: a rendered or brick box with real openings on the face that
    // fronts the street. Every window is a recess with reveals, a dark room
    // behind it and a pane in front, a shopfront runs along the ground floor
    // under a fascia, and a plinth and a cornice close the elevation top and
    // bottom. About six quads a window, which over the forty blocks that line
    // the two streets is twenty thousand triangles: less than one modelled
    // plot on the street itself.
    auto windowedBlock = [&](float cx, float cz, float halfX, float halfZ, float height,
                             const Vector3& streetNormal) {
        collector.setRegion(cx, cz);
        const Material* wallMaterial = &materials_.get(renders[rng.index(std::size(renders))]);
        const Material* trim = &materials_.get(rng.chance(0.5f) ? MaterialId::RenderWhite
                                                                : MaterialId::Ashlar);
        const Material* frameMaterial = &materials_.get(rng.chance(0.7f) ? MaterialId::FrameWhite
                                                                         : MaterialId::FrameDark);
        const Material* glass    = &materials_.get(MaterialId::Glazing);
        const Material* interior = &materials_.get(MaterialId::Interior);
        const Material* shopGlass = &materials_.get(MaterialId::ShopGlazing);
        const Material* screen   = &materials_.get(MaterialId::ShopScreen);
        const Material* fascia   = &materials_.get(MaterialId::ShopFascia);

        // The storey grid the elevation is set out on.
        const float groundFloor = 4.0f;
        const float storeyH = 3.15f;
        const int storeys = std::max(1, static_cast<int>((height - groundFloor) / storeyH));
        const float eaves = groundFloor + static_cast<float>(storeys) * storeyH;

        // The mass, minus its street face, which is built as panels around the
        // openings below.
        const Vector3 lo(cx - halfX, 0.0f, cz - halfZ);
        const Vector3 hi(cx + halfX, eaves, cz + halfZ);
        MeshBuilder& wall = collector.builder(wallMaterial);
        wall.setTileSize(2.0f);
        BoxFaces faces = BoxFaces::allButBottom();
        if (streetNormal.X > 0.5f) faces.posX = false;
        if (streetNormal.X < -0.5f) faces.negX = false;
        if (streetNormal.Z > 0.5f) faces.posZ = false;
        if (streetNormal.Z < -0.5f) faces.negZ = false;
        wall.addBox(lo, hi, faces);

        // A facade frame on the street face: u along it, v up, depth outward.
        FacadeFrame frame;
        frame.up = Vector3::Up;
        frame.out = streetNormal;
        frame.right = Vector3::Cross(frame.up, frame.out);
        const bool alongZ = std::fabs(streetNormal.X) > 0.5f;
        frame.width = alongZ ? halfZ * 2.0f : halfX * 2.0f;
        frame.height = eaves;
        // The frame's origin is the left end of the face seen from the street.
        const Vector3 faceCentre(cx + streetNormal.X * halfX, 0.0f, cz + streetNormal.Z * halfZ);
        frame.origin = faceCentre - frame.right * (frame.width * 0.5f);
        const auto at = [&](float u, float v, float d) { return frame.at(u, v, d); };

        // Plinth and cornice.
        MeshBuilder& trimBuilder = collector.builder(trim);
        trimBuilder.setTileSize(1.0f);
        trimBuilder.addQuadFacing(at(0.0f, 0.0f, 0.06f), at(frame.width, 0.0f, 0.06f),
                                  at(frame.width, 0.6f, 0.06f), at(0.0f, 0.6f, 0.06f), frame.out);
        trimBuilder.addQuadFacing(at(0.0f, eaves - 0.35f, 0.30f), at(frame.width, eaves - 0.35f, 0.30f),
                                  at(frame.width, eaves, 0.30f), at(0.0f, eaves, 0.30f), frame.out);
        trimBuilder.addQuadFacing(at(0.0f, eaves - 0.35f, 0.0f), at(frame.width, eaves - 0.35f, 0.0f),
                                  at(frame.width, eaves - 0.35f, 0.30f), at(0.0f, eaves - 0.35f, 0.30f),
                                  frame.up * -1.0f);
        trimBuilder.addQuadFacing(at(0.0f, eaves, 0.0f), at(frame.width, eaves, 0.0f),
                                  at(frame.width, eaves, 0.30f), at(0.0f, eaves, 0.30f), frame.up);

        // The ground floor: a shopfront on most, a plain wall with a door on the rest.
        const bool shops = rng.chance(0.7f);
        std::vector<Opening> openings;
        if (shops)
        {
            const float sill = 0.5f, head = groundFloor - 0.75f;
            openings.push_back(Opening{0.6f, 0.0f, frame.width - 0.6f, head + 0.05f});
            // A dark room behind the glass: a recessed panel two metres back
            // reads as an interior at this distance and costs one quad.
            MeshBuilder& dark = collector.builder(screen);
            dark.setTileSize(1.0f);
            dark.addQuadFacing(at(0.6f, 0.0f, -1.8f), at(frame.width - 0.6f, 0.0f, -1.8f),
                               at(frame.width - 0.6f, head, -1.8f), at(0.6f, head, -1.8f), frame.out);
            for (const float u : {0.6f, frame.width - 0.6f})
                dark.addQuadFacing(at(u, 0.0f, -1.8f), at(u, 0.0f, 0.0f), at(u, head, 0.0f),
                                   at(u, head, -1.8f), frame.right * (u < 1.0f ? 1.0f : -1.0f));
            dark.addQuadFacing(at(0.6f, head, -1.8f), at(frame.width - 0.6f, head, -1.8f),
                               at(frame.width - 0.6f, head, 0.0f), at(0.6f, head, 0.0f),
                               frame.up * -1.0f);
            MeshBuilder& pane = collector.builder(shopGlass);
            pane.setTileSize(2.4f);
            pane.addQuadFacing(at(0.6f, sill, -0.12f), at(frame.width - 0.6f, sill, -0.12f),
                               at(frame.width - 0.6f, head, -0.12f), at(0.6f, head, -0.12f), frame.out);
            MeshBuilder& riser = collector.builder(trim);
            riser.addQuadFacing(at(0.6f, 0.0f, -0.06f), at(frame.width - 0.6f, 0.0f, -0.06f),
                                at(frame.width - 0.6f, sill, -0.06f), at(0.6f, sill, -0.06f), frame.out);
            MeshBuilder& board = collector.builder(fascia);
            board.setTileSize(1.5f);
            board.addQuadFacing(at(0.4f, head + 0.05f, 0.10f), at(frame.width - 0.4f, head + 0.05f, 0.10f),
                                at(frame.width - 0.4f, groundFloor - 0.10f, 0.10f),
                                at(0.4f, groundFloor - 0.10f, 0.10f), frame.out);
            board.addQuadFacing(at(0.4f, groundFloor - 0.10f, 0.0f),
                                at(frame.width - 0.4f, groundFloor - 0.10f, 0.0f),
                                at(frame.width - 0.4f, groundFloor - 0.10f, 0.10f),
                                at(0.4f, groundFloor - 0.10f, 0.10f), frame.up);
            // Mullions.
            MeshBuilder& mullion = collector.builder(frameMaterial);
            mullion.setTileSize(0.5f);
            const int bays = std::max(1, static_cast<int>((frame.width - 1.2f) / 1.6f));
            for (int i = 0; i <= bays; ++i)
            {
                const float u = 0.6f + static_cast<float>(i) * (frame.width - 1.2f)
                                           / static_cast<float>(bays);
                mullion.addQuadFacing(at(u - 0.035f, sill, -0.06f), at(u + 0.035f, sill, -0.06f),
                                      at(u + 0.035f, head, -0.06f), at(u - 0.035f, head, -0.06f),
                                      frame.out);
            }
        }

        // The upper storeys: recessed windows on a regular bay grid.
        const float pitch = 2.9f;
        const int bays = std::max(1, static_cast<int>(std::round((frame.width - 0.8f) / pitch)));
        const float bayPitch = frame.width / static_cast<float>(bays);
        const float ww = std::min(1.15f, bayPitch - 1.2f), wh = 1.55f, reveal = 0.14f;
        MeshBuilder& sash = collector.builder(frameMaterial);
        sash.setTileSize(0.5f);
        MeshBuilder& panes = collector.builder(glass);
        panes.setTileSize(1.4f);
        MeshBuilder& rooms = collector.builder(interior);
        rooms.setUvMode(Geometry::UvMode::Explicit);
        for (int storey = 0; storey < storeys; ++storey)
        {
            const float v0 = groundFloor + static_cast<float>(storey) * storeyH + 0.95f;
            const float v1 = v0 + wh;
            for (int bay = 0; bay < bays; ++bay)
            {
                const float u0 = (static_cast<float>(bay) + 0.5f) * bayPitch - ww * 0.5f;
                const float u1 = u0 + ww;
                openings.push_back(Opening{u0, v0, u1, v1});
                // Reveals: the four sides of the recess, in the wall's material.
                wall.addQuad(at(u0, v0, 0.0f), at(u0, v0, -reveal), at(u0, v1, -reveal), at(u0, v1, 0.0f));
                wall.addQuad(at(u1, v0, -reveal), at(u1, v0, 0.0f), at(u1, v1, 0.0f), at(u1, v1, -reveal));
                wall.addQuad(at(u0, v1, 0.0f), at(u0, v1, -reveal), at(u1, v1, -reveal), at(u1, v1, 0.0f));
                wall.addQuad(at(u0, v0, -reveal), at(u0, v0, 0.0f), at(u1, v0, 0.0f), at(u1, v0, -reveal));
                // The room, one atlas cell, and the pane in front of it.
                const int cell = rng.intRange(0, 15);
                const std::size_t first = rooms.vertexCount();
                rooms.addQuadFacingUv(at(u0 + 0.05f, v0 + 0.05f, -reveal + 0.01f),
                                      at(u1 - 0.05f, v0 + 0.05f, -reveal + 0.01f),
                                      at(u1 - 0.05f, v1 - 0.05f, -reveal + 0.01f),
                                      at(u0 + 0.05f, v1 - 0.05f, -reveal + 0.01f), frame.out);
                rooms.offsetUv(first, Vector2(0.25f, 0.25f),
                               Vector2(static_cast<float>(cell % 4) * 0.25f,
                                       static_cast<float>(cell / 4) * 0.25f));
                panes.addQuadFacing(at(u0 + 0.03f, v0 + 0.03f, -reveal + 0.05f),
                                    at(u1 - 0.03f, v0 + 0.03f, -reveal + 0.05f),
                                    at(u1 - 0.03f, v1 - 0.03f, -reveal + 0.05f),
                                    at(u0 + 0.03f, v1 - 0.03f, -reveal + 0.05f), frame.out);
                // Frame: a cross, and the sill under it.
                const float gd = -reveal + 0.04f;
                sash.addQuadFacing(at(u0 + ww * 0.5f - 0.03f, v0, gd), at(u0 + ww * 0.5f + 0.03f, v0, gd),
                                   at(u0 + ww * 0.5f + 0.03f, v1, gd), at(u0 + ww * 0.5f - 0.03f, v1, gd),
                                   frame.out);
                sash.addQuadFacing(at(u0, v0 + wh * 0.7f - 0.03f, gd), at(u1, v0 + wh * 0.7f - 0.03f, gd),
                                   at(u1, v0 + wh * 0.7f + 0.03f, gd), at(u0, v0 + wh * 0.7f + 0.03f, gd),
                                   frame.out);
                trimBuilder.addQuadFacing(at(u0 - 0.05f, v0 - 0.07f, 0.0f), at(u1 + 0.05f, v0 - 0.07f, 0.0f),
                                          at(u1 + 0.05f, v0 - 0.07f, 0.05f), at(u0 - 0.05f, v0 - 0.07f, 0.05f),
                                          frame.up);
                trimBuilder.addQuadFacing(at(u0 - 0.05f, v0 - 0.07f, 0.05f), at(u1 + 0.05f, v0 - 0.07f, 0.05f),
                                          at(u1 + 0.05f, v0, 0.05f), at(u0 - 0.05f, v0, 0.05f), frame.out);
            }
        }

        // The wall itself, around the openings: a scanline decomposition, the
        // same one BuildingBuilder uses, in miniature.
        std::vector<float> levels{0.6f, eaves - 0.35f};
        for (const Opening& o : openings) { levels.push_back(o.v0); levels.push_back(o.v1); }
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end(),
                                 [](float a, float b) { return std::fabs(a - b) < 1e-3f; }),
                     levels.end());
        for (std::size_t band = 0; band + 1 < levels.size(); ++band)
        {
            const float v0 = levels[band], v1 = levels[band + 1];
            if (v1 - v0 < 1e-3f) continue;
            std::vector<std::pair<float, float>> spans;
            for (const Opening& o : openings)
                if (o.v0 <= v0 + 1e-3f && o.v1 >= v1 - 1e-3f) spans.emplace_back(o.u0, o.u1);
            std::sort(spans.begin(), spans.end());
            float cursor = 0.0f;
            for (const auto& span : spans)
            {
                if (span.first > cursor + 1e-3f)
                    wall.addQuadFacing(at(cursor, v0, 0.0f), at(span.first, v0, 0.0f),
                                       at(span.first, v1, 0.0f), at(cursor, v1, 0.0f), frame.out);
                cursor = std::max(cursor, span.second);
            }
            if (cursor < frame.width - 1e-3f)
                wall.addQuadFacing(at(cursor, v0, 0.0f), at(frame.width, v0, 0.0f),
                                   at(frame.width, v1, 0.0f), at(cursor, v1, 0.0f), frame.out);
        }

        roof(cx, cz, halfX, halfZ, eaves, wallMaterial, storeyH);
    };

    // Down both arms of the main street, past the modelled frontage. Every
    // third gap between blocks is a cross street rather than a passage, so the
    // district beyond has a grid in it and not a row of boxes.
    for (const float sign : {-1.0f, 1.0f})
    {
        float z = M::kMainStreetHalfLength + 6.0f;
        int count = 0;
        while (z < 316.0f)
        {
            const float depth = rng.range(16.0f, 30.0f);
            for (const float side : {-1.0f, 1.0f})
                windowedBlock(side * (M::kMainStreetHalfWidth + 11.0f), sign * (z + depth * 0.5f),
                              11.0f, depth * 0.5f, rng.range(13.0f, 24.0f),
                              Vector3(-side, 0.0f, 0.0f));
            z += depth + ((count++ % 3 == 2) ? rng.range(12.0f, 16.0f) : rng.range(1.5f, 4.0f));
        }
        // The block that closes the view down the street.
        paintedBlock(0.0f, sign * 345.0f, 46.0f, 22.0f, rng.range(18.0f, 30.0f));
    }
    // And down the side street.
    for (const float sign : {-1.0f, 1.0f})
    {
        float x = M::kSideStreetHalfLength + 5.0f;
        int count = 0;
        while (x < 198.0f)
        {
            const float depth = rng.range(15.0f, 26.0f);
            for (const float side : {-1.0f, 1.0f})
                windowedBlock(sign * (x + depth * 0.5f), side * (M::kSideStreetHalfWidth + 10.0f),
                              depth * 0.5f, 10.0f, rng.range(11.0f, 20.0f),
                              Vector3(0.0f, 0.0f, -side));
            x += depth + ((count++ % 3 == 2) ? rng.range(10.0f, 14.0f) : rng.range(1.5f, 4.0f));
        }
        paintedBlock(sign * 224.0f, 0.0f, 20.0f, 40.0f, rng.range(15.0f, 26.0f));
    }

    // A far skyline: a scatter of taller blocks well beyond the district, which
    // is what stops the horizon being a clean line of identical parapets.
    for (int i = 0; i < 90; ++i)
    {
        const float angle = rng.range(0.0f, 6.2831853f);
        const float radius = rng.range(240.0f, 430.0f);
        const float cx = std::cos(angle) * radius;
        const float cz = std::sin(angle) * radius * 1.35f;
        paintedBlock(cx, cz, rng.range(9.0f, 26.0f), rng.range(9.0f, 26.0f), rng.range(12.0f, 44.0f));
    }

    // --- what stands in the far street ------------------------------------
    // The same trees at the same pitch and the same parked cars, so the street
    // does not stop being a street where the modelling stops. Both are the far
    // levels of detail, instanced, and cast no shadow: at 140 m and beyond a
    // shadow is a texel.
    if (settings.vegetation && !farTrees_.empty())
    {
        std::vector<std::vector<Matrix>> treeAt(farTrees_.size());
        const float treeX = M::kMainCarriagewayWidth * 0.5f + 0.85f;
        for (const float sign : {-1.0f, 1.0f})
            for (float z = M::kMainStreetHalfLength + 8.0f; z < 312.0f; z += 12.0f)
            {
                if (!rng.chance(0.80f)) continue;
                for (const float side : {-1.0f, 1.0f})
                    treeAt[rng.index(farTrees_.size())].push_back(
                        Place(side * treeX, M::kCurbHeight, sign * z,
                              rng.range(0.0f, MathHelper::TwoPi)));
            }
        for (std::size_t i = 0; i < farTrees_.size(); ++i)
            placeProp(farTrees_[i], treeAt[i], "context-tree", 0.0f, 0.0f, false);
    }
    if (settings.traffic && !vehicleMeshes_.empty())
    {
        std::vector<std::vector<Matrix>> carAt(vehicleMeshes_.size());
        const float bayX = M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f;
        for (const float sign : {-1.0f, 1.0f})
            for (float z = M::kMainStreetHalfLength + 4.0f; z < 316.0f; z += M::kParkingBayLength + 0.6f)
            {
                for (const float side : {-1.0f, 1.0f})
                {
                    if (!rng.chance(0.62f)) continue;
                    // Parked with the traffic on its side of the road.
                    const float yaw = side > 0.0f ? 0.0f : MathHelper::Pi;
                    carAt[rng.index(vehicleMeshes_.size())].push_back(
                        Place(side * bayX, 0.0f, sign * (z + rng.signed_(0.4f)), yaw));
                }
            }
        for (std::size_t i = 0; i < vehicleMeshes_.size(); ++i)
            placeProp(vehicleMeshes_[i].distantBody, carAt[i], "context-car", 0.0f, 0.0f, false);
    }
}

CityScene::PropMesh CityScene::makeProp(const std::string& name,
                                        const std::function<void(GeometryCollector&)>& build)
{
    GeometryCollector collector;
    collector.setRegionKey(0);
    build(collector);

    PropMesh prop;
    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    int index = 0;
    for (GeometryCollector::Batch& batch : collector.take())
    {
        const GpuMesh* mesh = upload(batch.mesh, name + "." + std::to_string(index++));
        if (mesh == nullptr) continue;
        prop.parts.push_back(PropMesh::Part{batch.material, mesh});
        lo = Vector3(std::min(lo.X, mesh->bounds().Min.X), std::min(lo.Y, mesh->bounds().Min.Y),
                     std::min(lo.Z, mesh->bounds().Min.Z));
        hi = Vector3(std::max(hi.X, mesh->bounds().Max.X), std::max(hi.Y, mesh->bounds().Max.Y),
                     std::max(hi.Z, mesh->bounds().Max.Z));
    }
    prop.bounds = prop.parts.empty() ? BoundingBox(Vector3::Zero, Vector3::Zero)
                                     : BoundingBox(lo, hi);
    return prop;
}

void CityScene::placeProp(const PropMesh& prop, const std::vector<Matrix>& transforms,
                          const std::string& name, float cullDistance, float shadowDistance,
                          bool castsShadow, const PropMesh* distant, float lodDistance)
{
    if (prop.empty() || transforms.empty()) return;
    for (std::size_t i = 0; i < prop.parts.size(); ++i)
    {
        const PropMesh::Part& part = prop.parts[i];
        InstanceGroup group;
        group.mesh           = part.mesh;
        group.material       = part.material;
        group.transforms     = transforms;
        group.cullDistance   = cullDistance;
        group.shadowDistance = shadowDistance;
        group.castsShadow    = castsShadow;
        group.name           = name;
        // The cheaper mesh, matched part for part. Matching by index rather
        // than by material is safe because both are built by the same generator
        // in the same order from the same seed; a mismatch in count means one
        // of them dropped a material, and the near mesh is then used at every
        // distance rather than the wrong geometry being swapped in.
        if (distant != nullptr && distant->parts.size() == prop.parts.size() && lodDistance > 0.0f)
        {
            group.lodMesh     = distant->parts[i].mesh;
            group.lodDistance = lodDistance;
        }
        renderer_.addInstances(std::move(group));
        ++buildStats_.instanceGroups;
    }
    buildStats_.instances += static_cast<int>(transforms.size());
}

void CityScene::submitProp(const PropMesh& prop, const Matrix& transform,
                           const Material* overrideMaterial, bool shadowOnly)
{
    for (const PropMesh::Part& part : prop.parts)
        renderer_.submitDynamic(part.mesh,
                                overrideMaterial != nullptr ? overrideMaterial : part.material,
                                transform, shadowOnly);
}

void CityScene::update(float deltaSeconds, const RenderSettings& settings)
{
    signals_.update(deltaSeconds);
    if (settings.traffic) traffic_.update(deltaSeconds, signals_);
    if (settings.pedestrians) pedestrians_.update(deltaSeconds, signals_);
}

void CityScene::submit(const RenderSettings& settings, const Vector3& eye)
{
    cameraPosition_ = eye;

    // --- signal lenses ------------------------------------------------------
    // Drawn per frame rather than instanced, because which of them is lit
    // changes: a lit lens and a dark one are the same mesh with a different
    // material, and the controller decides which every frame.
    auto lens = [&](const PropMesh& mesh, const Matrix& at, int colour, bool lit) {
        if (mesh.empty()) return;
        const std::vector<const Material*>& set = lit ? lensLit_ : lensDark_;
        const Material* material = colour < static_cast<int>(set.size())
                                       ? set[static_cast<std::size_t>(colour)]
                                       : nullptr;
        submitProp(mesh, at, material);
    };

    for (const SignalHead& head : signalHeads_)
    {
        if (head.pedestrian)
        {
            const bool walk = signals_.pedestrianGreen(head.axis);
            const float pitch = M::kPedSignalHousingHeight * 0.5f;
            const float front = M::kSignalHousingDepth * 0.85f * 0.5f + 0.046f;
            lens(lensWalkRed_,
                 Matrix::CreateTranslation(0.0f, pitch * 1.5f, front) * head.transform, 3, !walk);
            lens(lensWalkGreen_,
                 Matrix::CreateTranslation(0.0f, pitch * 0.5f, front) * head.transform, 4, walk);
            continue;
        }

        const SignalAspect aspect = signals_.vehicleAspect(head.axis);
        const float pitch = M::kSignalHousingHeight / 3.0f;
        const float front = M::kSignalHousingDepth * 0.5f + 0.058f;
        lens(lensRed_,
             Matrix::CreateTranslation(0.0f, M::kSignalHousingHeight - pitch * 0.5f, front)
                 * head.transform,
             0, aspect == SignalAspect::Red || aspect == SignalAspect::RedAmber);
        lens(lensAmber_,
             Matrix::CreateTranslation(0.0f, M::kSignalHousingHeight - pitch * 1.5f, front)
                 * head.transform,
             1, aspect == SignalAspect::Amber || aspect == SignalAspect::RedAmber);
        lens(lensGreen_,
             Matrix::CreateTranslation(0.0f, M::kSignalHousingHeight - pitch * 2.5f, front)
                 * head.transform,
             2, aspect == SignalAspect::Green);
    }

    // --- vehicles -----------------------------------------------------------
    if (settings.traffic && !vehicleMeshes_.empty())
    {
        for (const Vehicle& vehicle : traffic_.vehicles())
        {
            const int variant = std::clamp(vehicle.variant, 0,
                                           static_cast<int>(vehicleMeshes_.size()) - 1);
            const VehicleMesh& mesh = vehicleMeshes_[static_cast<std::size_t>(variant)];
            const Matrix world = vehicle.transform(traffic_.lanes());
            const Vector2 at   = vehicle.groundPosition(traffic_.lanes());
            const float distance = std::sqrt((at.X - eye.X) * (at.X - eye.X)
                                             + (at.Y - eye.Z) * (at.Y - eye.Z));
            if (distance > settings.propCullDistance) continue;

            // One switch for the whole vehicle. Below 38 m the body is the full
            // mesh with its shut lines, mirrors and interior; past it the same
            // silhouette at a third of the stations, which is three pixels of
            // difference and two thirds of the triangles.
            const bool near = distance < 38.0f;
            submitProp(near ? mesh.body : mesh.distantBody, world);
            if (vehicle.braking && brakeLit_ != nullptr && distance < 90.0f)
                submitProp(mesh.brakeLamps, world, brakeLit_);

            // Only the near mesh has wheels of its own; the far one carries
            // them welded into the body.
            if (!near) continue;
            const PropMesh& wheel = mesh.wheel;
            if (wheel.empty()) continue;
            for (const WheelPlacement& place : mesh.wheels)
            {
                // Rolling first, then steer, then the placement. A steered wheel
                // that rolls about the *steered* axis walks sideways out of its
                // arch, which is the classic version of this bug.
                Matrix local = Matrix::CreateRotationX(vehicle.wheelAngle * place.side);
                if (place.steered && vehicle.steerAngle != 0.0f)
                    local = local * Matrix::CreateRotationY(vehicle.steerAngle);
                if (place.side < 0.0f) local = local * Matrix::CreateScale(-1.0f, 1.0f, 1.0f);
                submitProp(wheel,
                           local * Matrix::CreateTranslation(place.centre) * world);
            }
        }
    }

    // --- people -------------------------------------------------------------
    submitPeople(settings);
}

void CityScene::submitPeople(const RenderSettings& settings)
{
    if (!settings.pedestrians || characterMeshes_.empty()) return;

    const std::vector<Pedestrian>& crowd = pedestrians_.people();
    if (walkers_.size() != crowd.size())
    {
        walkers_.clear();
        walkers_.reserve(crowd.size());
        for (const Pedestrian& person : crowd)
        {
            const std::size_t variant = static_cast<std::size_t>(
                std::clamp(person.variant, 0, static_cast<int>(characterMeshes_.size()) - 1));
            walkers_.push_back(std::make_unique<Graphics::AnimationPlayer>(
                characterMeshes_[variant]->skinning));
        }
    }

    const Vector3& eye = cameraPosition_;
    for (std::size_t i = 0; i < crowd.size(); ++i)
    {
        const Pedestrian& person = crowd[i];
        const std::size_t variant = static_cast<std::size_t>(
            std::clamp(person.variant, 0, static_cast<int>(characterMeshes_.size()) - 1));
        const CharacterMesh& character = *characterMeshes_[variant];

        const Vector2 at = person.position(pedestrians_.nodes(), pedestrians_.edges());
        const float distance = std::sqrt((at.X - eye.X) * (at.X - eye.X)
                                         + (at.Y - eye.Z) * (at.Y - eye.Z));
        if (distance > settings.pedestrianCullDistance) continue;

        // Scale *before* the placement: XNA composes row-vector-first, so the
        // other order scales the person's position in the world rather than the
        // person.
        const Matrix world =
            Matrix::CreateScale(person.height / character.height)
            * pedestrians_.transform(person, layout_.groundHeight(at.X, at.Y));

        Graphics::AnimationPlayer& player = *walkers_[i];
        // Absolute time rather than a delta, and taken from how far this person
        // has actually walked: a stride is 1.42 m, so the cycle follows the
        // ground speed and the feet do not slide. Someone waiting at a kerb runs
        // the idle clip on their own offset, which is what stops a queue of
        // pedestrians breathing in unison.
        if (person.waiting)
        {
            const auto& clip = character.skinning.AnimationClips.at("idle");
            if (player.getCurrentClipProperty() != &clip) player.StartClip(clip);
            player.Update(System::TimeSpan::FromTicks(static_cast<std::int64_t>(
                              static_cast<double>(person.waitTime
                                                  + static_cast<float>(i) * 0.53f)
                              * 1.0e7)),
                          false, true);
        }
        else
        {
            const auto& clip = character.skinning.AnimationClips.at("walk");
            if (player.getCurrentClipProperty() != &clip) player.StartClip(clip);
            const float cycles = PedestrianSystem::cyclesWalked(person);
            player.Update(System::TimeSpan::FromTicks(static_cast<std::int64_t>(
                              static_cast<double>(cycles * 1.06f) * 1.0e7)),
                          false, true);
        }

        // A figure is the thing a viewer looks at hardest, and the near mesh
        // is where the shoes, the eyes and the bag are. The switch is about the
        // width of this street, so everybody on the near footway and everybody
        // crossing in front of the camera is fully detailed and everybody down
        // the road is three draws instead of six.
        const std::vector<CharacterMesh::Part>& parts =
            distance < settings.pedestrianDetailDistance || character.farParts.empty()
                ? character.parts
                : character.farParts;
        for (const CharacterMesh::Part& part : parts)
        {
            SkinnedItem item;
            item.mesh     = part.mesh.get();
            item.material = part.material;
            item.world    = world;
            item.bones    = &player.GetSkinTransforms();
            renderer_.submitSkinned(std::move(item));
        }
        // The rigid stand-in, for the shadow pass only.
        if (distance < settings.propShadowDistance)
            submitProp(character.shadowProxy, world, nullptr, true);
    }
}

std::vector<Vector3> CityScene::probePositions(const RenderSettings& settings) const
{
    // Where the reflective things are. Parked cars stand in the two parking
    // lanes at x = +/-4.4 and the shop glass is five metres behind them at the
    // building line, so a row of probes over each parking lane -- at the eye
    // height of the surfaces that read them, a car's flank and a pane at
    // shoulder height -- serves both: a car reflects the facade it is parked
    // outside and a window reflects the cars parked in front of it. The side
    // street gets a row over each travel lane, and the junction one in the
    // middle. Everything on the far footway picks the row on its own side.
    std::vector<Vector3> positions;
    const float spacing = std::max(8.0f, settings.probeSpacing);
    const float eye = 1.55f;
    const float mainX = M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f;
    const float sideZ = M::kSideCarriagewayWidth * 0.25f;
    positions.emplace_back(0.0f, eye, 0.0f);
    for (float z = spacing * 0.5f; z < M::kMainStreetHalfLength - 2.0f; z += spacing)
        for (const float sign : {-1.0f, 1.0f})
        {
            if (sign * z > -M::kSideStreetHalfWidth - 1.0f
                && sign * z < M::kSideStreetHalfWidth + 1.0f)
                continue;
            positions.emplace_back(-mainX, eye, sign * z);
            positions.emplace_back(mainX, eye, sign * z);
        }
    for (float x = M::kMainStreetHalfWidth + spacing * 0.5f; x < M::kSideStreetHalfLength - 2.0f;
         x += spacing)
        for (const float sign : {-1.0f, 1.0f})
        {
            positions.emplace_back(sign * x, eye, -sideZ);
            positions.emplace_back(sign * x, eye, sideZ);
        }
    return positions;
}

float CityScene::groundHeight(float x, float z) const
{
    return layout_.groundHeight(x, z);
}

bool CityScene::isSolid(const Vector3& point) const
{
    return layout_.isSolid(point.X, point.Y, point.Z);
}

// ---------------------------------------------------------------------------
// Street furniture, planting and signalling
// ---------------------------------------------------------------------------
namespace {

/// The yaw that turns a prop's local +Z onto a horizontal direction given as
/// (x, z). Every prop is modelled facing +Z, so this is the whole of "face the
/// road" or "face the oncoming traffic".
[[nodiscard]] float YawTowards(const Vector2& direction)
{
    return std::atan2(direction.X, direction.Y);
}

/// The direction 90° to the left of a heading on the ground plane.
[[nodiscard]] Vector2 LeftOf(const Vector2& direction)
{
    return Vector2(direction.Y, -direction.X);
}

/**
 * @brief Where the lamps and the trees stand along one footway run.
 *
 * Both live here because they have to agree. A lamp column and a tree planted
 * in the same square metre is exactly the kind of mistake that survives every
 * unit test and then dominates a screenshot, and it is what happens when two
 * placement loops each pick their own spacing. Lamps land on a 24 m beat and
 * trees on the 12 m half-beat offset by 6 m, so the closest a tree ever gets to
 * a column is 6 m — about right for a real street, where the lighting engineer
 * and the tree officer are also obliged to talk to each other.
 */
struct FootwayRhythm
{
    float first = 8.0f;
    float lampSpacing = 24.0f;
    float treeSpacing = 12.0f;

    [[nodiscard]] float lampAt(int index) const
    {
        return first + lampSpacing * static_cast<float>(index);
    }
    [[nodiscard]] float treeAt(int index) const
    {
        return first + 6.0f + treeSpacing * static_cast<float>(index);
    }
};

/// The rhythm for one run, phased by where the run starts so the four arms are
/// not in lockstep. Deterministic: it reads the run's own coordinates rather
/// than drawing from a generator, so adding a system between two others cannot
/// move the lamps.
[[nodiscard]] FootwayRhythm RhythmFor(const FootwayRun& run)
{
    FootwayRhythm rhythm;
    const float phase = std::fabs(std::fmod(run.start.X * 2.7f + run.start.Y * 1.3f, 4.5f));
    rhythm.first       = (run.main ? 8.5f : 6.5f) + phase;
    rhythm.lampSpacing = run.main ? 24.0f : 21.0f;
    return rhythm;
}

/// The furniture zone: the strip beside the kerb where everything that is not a
/// pedestrian belongs. Measured from the run's centre line toward the kerb, so
/// a positive value is closer to the road.
[[nodiscard]] float FurnitureBand(const FootwayRun& run)
{
    return run.width * 0.5f - (run.main ? 0.85f : 0.62f);
}

}  // namespace

void CityScene::buildStreetFurniture(Rng& rng, const RenderSettings& settings)
{
    const PropFactory props(materials_);
    const float cull   = settings.propCullDistance;
    const float shade  = settings.propShadowDistance;
    const float ground = M::kCurbHeight;

    const PropMesh lampMain = makeProp("lamp-main", [&](GeometryCollector& c) {
        props.streetLamp(c, M::kLampMainHeight, M::kLampArmReach);
    });
    const PropMesh lampSide = makeProp("lamp-side", [&](GeometryCollector& c) {
        props.streetLamp(c, M::kLampSideHeight, M::kLampArmReach * 0.8f);
    });
    const PropMesh bench   = makeProp("bench", [&](GeometryCollector& c) { props.bench(c); });
    const PropMesh bollard = makeProp("bollard", [&](GeometryCollector& c) { props.bollard(c); });
    const PropMesh bin     = makeProp("litter-bin", [&](GeometryCollector& c) { props.litterBin(c); });
    const PropMesh hydrant = makeProp("hydrant", [&](GeometryCollector& c) { props.hydrant(c); });
    const PropMesh cabinet = makeProp("cabinet", [&](GeometryCollector& c) {
        props.utilityCabinet(c, rng);
    });
    const PropMesh bikeStand = makeProp("bike-stand", [&](GeometryCollector& c) {
        props.bicycleStand(c);
    });
    // Three bicycles in three paints, chained to the stands.
    static const Vector3 kBikePaints[] = {Vector3(0.05f, 0.06f, 0.07f), Vector3(0.36f, 0.08f, 0.07f),
                                          Vector3(0.10f, 0.18f, 0.30f)};
    std::vector<PropMesh> bikes;
    for (std::size_t i = 0; i < std::size(kBikePaints); ++i)
    {
        const Material* paint = materials_.deriveTinted("bike-paint-" + std::to_string(i),
                                                        MaterialId::PaintedSteelDark, kBikePaints[i]);
        bikes.push_back(makeProp("bicycle-" + std::to_string(i), [&](GeometryCollector& c) {
            props.bicycle(c, paint);
        }));
    }
    std::vector<std::vector<Matrix>> bikeOnStand(bikes.size());
    const PropMesh shelter = makeProp("bus-shelter", [&](GeometryCollector& c) {
        props.busShelter(c);
    });

    std::vector<Matrix> lampMainAt, lampSideAt, benchAt, bollardAt, binAt, hydrantAt, cabinetAt,
        bikeAt, shelterAt;

    const std::vector<FootwayRun>& runs = layout_.footways();
    for (std::size_t index = 0; index < runs.size(); ++index)
    {
        const FootwayRun& run = runs[index];
        const float dx = run.end.X - run.start.X;
        const float dz = run.end.Y - run.start.Y;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < 8.0f) continue;

        const Vector2 along(dx / length, dz / length);
        const Vector2 kerb = run.toKerb;
        const float band = FurnitureBand(run);
        const float wall = -(run.width * 0.5f - 0.70f);
        const float faceRoad = YawTowards(kerb);
        const FootwayRhythm rhythm = RhythmFor(run);

        // A point `s` metres along the run and `lateral` metres toward the kerb
        // from its centre line.
        auto at = [&](float s, float lateral) {
            return Vector3(run.start.X + along.X * s + kerb.X * lateral, ground,
                           run.start.Y + along.Y * s + kerb.Y * lateral);
        };

        // --- lighting -------------------------------------------------------
        // The columns come first and everything else fits around them, because
        // the lighting layout is the one thing on a footway that is not
        // negotiable: the spacing is set by the luminaire's throw.
        int lamp = 0;
        for (float s = rhythm.lampAt(0); s < length - 4.0f; s = rhythm.lampAt(++lamp))
        {
            const Vector3 p = at(s, band);
            (run.main ? lampMainAt : lampSideAt)
                .push_back(Place(p.X, p.Y, p.Z, faceRoad));

            // A bin at every third column, and a bike stand or two on the main
            // street where there is width for them.
            if (lamp % 3 == 1)
            {
                const Vector3 b = at(s + 1.9f, band);
                binAt.push_back(Place(b.X, b.Y, b.Z, faceRoad));
            }
            if (run.main && lamp % 3 == 2)
            {
                const int stands = rng.intRange(2, 3);
                for (int i = 0; i < stands; ++i)
                {
                    const Vector3 b = at(s + 2.6f + static_cast<float>(i) * 0.95f, band + 0.1f);
                    bikeAt.push_back(Place(b.X, b.Y, b.Z, faceRoad));
                    // About half the stands have a bicycle at them, on one side
                    // or the other, never both: a full rack is a bike shop.
                    if (rng.chance(0.55f))
                    {
                        const float flank = rng.chance(0.5f) ? 0.34f : -0.34f;
                        const float yaw = faceRoad + (flank > 0.0f ? 0.0f : MathHelper::Pi);
                        bikeOnStand[rng.index(bikes.size())].push_back(
                            Matrix::CreateTranslation(0.0f, 0.0f, flank) * Place(b.X, b.Y, b.Z, yaw));
                    }
                }
            }
        }

        // --- seating --------------------------------------------------------
        // Benches face the street, backs to the shopfronts, which is both how
        // they are actually installed and the arrangement that keeps the seated
        // figure out of the walking line.
        if (run.main)
        {
            int seat = 0;
            for (float s = rhythm.first + 15.0f; s < length - 8.0f; s += 33.0f, ++seat)
            {
                if (!rng.chance(0.7f)) continue;
                const Vector3 p = at(s + rng.signed_(2.0f), band - 0.45f);
                benchAt.push_back(Place(p.X, p.Y, p.Z, faceRoad));
            }
        }

        // --- the services nobody notices until they are missing --------------
        // A cabinet stands back against the building line; a hydrant stands at
        // the kerb where a hose can reach it.
        if (rng.chance(run.main ? 0.75f : 0.45f))
        {
            const Vector3 p = at(rng.range(18.0f, std::max(19.0f, length - 12.0f)), wall);
            cabinetAt.push_back(Place(p.X, p.Y, p.Z, faceRoad));
        }
        for (float s = rhythm.first + 26.0f; s < length - 10.0f; s += 58.0f)
        {
            const Vector3 p = at(s + rng.signed_(4.0f), band + 0.35f);
            hydrantAt.push_back(Place(p.X, p.Y, p.Z, faceRoad));
        }
    }

    // --- the bus stop -------------------------------------------------------
    // One shelter, on the *east* footway of the northern arm, set in the
    // furniture zone with the walking width kept clear behind it. It was on the
    // west footway, six metres in front of the first viewpoint, where a
    // four-metre glass box is the whole picture: a shelter is scenery seen from
    // across the street and an obstruction seen from underneath.
    if (runs.size() > 3)
    {
        const FootwayRun& run = runs[3];
        const float lateral = run.width * 0.5f - 1.05f;
        const Vector3 p(run.start.X + run.toKerb.X * lateral, ground,
                        run.start.Y + 34.0f + run.toKerb.Y * lateral);
        shelterAt.push_back(Place(p.X, p.Y, p.Z, YawTowards(run.toKerb)));
    }

    // --- bollards flanking the crossings ------------------------------------
    // Their job on a real street is to stop a delivery van parking across the
    // dropped kerb, and they do the same job in the picture: they mark where the
    // footway ends without a fence.
    for (const Crossing& crossing : crossings_)
    {
        const Vector2 walk = crossing.walkDirection;
        const Vector2 side = LeftOf(walk);
        for (const float end : {-1.0f, 1.0f})
        {
            const float lateral = end * (crossing.halfLength + 0.62f);
            // One each side of the crossing, not a fence: four bollards per
            // crossing is what a junction actually has, and eight made the
            // corner look like a car park barrier.
            for (const float flank : {-1.0f, 1.0f})
            {
                const float offset = flank * (crossing.halfDepth + 0.85f);
                bollardAt.push_back(Place(
                    crossing.centre.X + walk.X * lateral + side.X * offset, ground,
                    crossing.centre.Y + walk.Y * lateral + side.Y * offset, 0.0f));
            }
        }
    }

    placeProp(lampMain, lampMainAt, "lamp-main", cull, shade);
    placeProp(lampSide, lampSideAt, "lamp-side", cull, shade);

    // The pools they throw, at night only. Placed from the same transform list
    // as the columns so a pool cannot end up where a lamp is not, and dropped
    // to the ground: the column's transform has its base on the footway, which
    // is where the pool wants to be.
    if (settings.nightLighting())
    {
        const PropMesh poolMain = makeProp("light-pool-main", [&](GeometryCollector& c) {
            props.lightPool(c, 5.4f);
        });
        const PropMesh poolSide = makeProp("light-pool-side", [&](GeometryCollector& c) {
            props.lightPool(c, 3.8f);
        });
        // A main-street luminaire is on a 9 m column with a 1.6 m outreach over
        // the carriageway, so its pool is not centred on its column. The side
        // street's is a smaller lantern on the footway.
        std::vector<Matrix> poolMainAt, poolSideAt;
        poolMainAt.reserve(lampMainAt.size());
        for (const Matrix& at : lampMainAt)
            poolMainAt.push_back(Matrix::CreateTranslation(0.0f, 0.0f, 1.6f) * at);
        poolSideAt = lampSideAt;
        placeProp(poolMain, poolMainAt, "light-pool-main", 105.0f, 0.0f, false);
        placeProp(poolSide, poolSideAt, "light-pool-side", 85.0f, 0.0f, false);
    }
    placeProp(bench, benchAt, "bench", cull * 0.5f, shade * 0.6f);
    placeProp(bollard, bollardAt, "bollard", cull * 0.4f, shade * 0.4f);
    placeProp(bin, binAt, "litter-bin", cull * 0.5f, shade * 0.6f);
    placeProp(hydrant, hydrantAt, "hydrant", cull * 0.4f, shade * 0.4f);
    placeProp(cabinet, cabinetAt, "cabinet", cull * 0.7f, shade);
    placeProp(bikeStand, bikeAt, "bike-stand", cull * 0.4f, shade * 0.5f);
    for (std::size_t i = 0; i < bikes.size(); ++i)
        placeProp(bikes[i], bikeOnStand[i], "bicycle", cull * 0.35f, shade * 0.4f);
    placeProp(shelter, shelterAt, "bus-shelter", cull, shade);
}

void CityScene::lightTheStreet(const RenderSettings& settings)
{
    // Everything that is a *lamp* rather than a surface, switched on.
    //
    // Done by editing the catalogue rather than by building a second set of
    // props, because the objects are identical at noon and at midnight -- only
    // their emission differs -- and a parallel set of night meshes would be a
    // second thing to keep in step with the first. `PbrEffect` adds the
    // emissive term after everything else, so a material with an emissive
    // factor is a light source that costs nothing per frame and cannot be
    // shadowed, which is exactly right for a lamp seen from outside.
    //
    // What this does *not* do is illuminate anything. There is one punctual
    // light per draw in `PbrEffect` and this street has forty lamps, so the
    // pools of light on the ground are geometry (see `PropFactory::lightPool`)
    // and the rooms behind the glass carry theirs baked into their own emissive
    // maps. That is a light map, which is what a renderer without a many-light
    // path has always used, and at civil twilight it reads.
    (void)settings;

    // Sodium-white, and hot enough to bloom: a luminaire seen directly is the
    // brightest thing in a night frame by two orders of magnitude, and one that
    // merely goes pale grey reads as switched off.
    materials_.mutableGet(MaterialId::LampGlass).emissiveFactor =
        Vector3(5.60f, 5.05f, 3.90f);

    // Dipped beams and the sidelights around them.
    materials_.mutableGet(MaterialId::CarLightFront).emissiveFactor =
        Vector3(3.30f, 3.20f, 2.90f);
    materials_.mutableGet(MaterialId::CarLightRear).emissiveFactor =
        Vector3(2.10f, 0.13f, 0.08f);

    // The rooms behind the glass, which at night are the light in the street.
    // The ceiling strips go up and the surfaces they light go up with them,
    // because the baked bounce is the only thing carrying that light.
    materials_.mutableGet(MaterialId::ShopCeilingLight).emissiveFactor =
        Vector3(4.20f, 3.85f, 3.20f);
    // The fittings take the boost; the room's own big surfaces barely do.
    //
    // A shop's *walls* are the largest emissive area in the scene once the sun
    // is down, and a room lit from a strip in its own ceiling is brightest on
    // what stands under the strip -- not on four metres of plasterboard. At a
    // uniform 1.55 the side wall of the nearest unit, seen almost edge-on from
    // along the footway, was the brightest thing in a night frame: a white
    // panel with no windows in it, next to a street.
    for (const MaterialId id : {MaterialId::ShopFitting, MaterialId::ShopStock,
                                MaterialId::ShopTimber})
        materials_.mutableGet(id).emissiveFactor =
            materials_.get(id).emissiveFactor * 1.70f;
    for (const MaterialId id : {MaterialId::ShopWall, MaterialId::ShopFloor})
        materials_.mutableGet(id).emissiveFactor =
            materials_.get(id).emissiveFactor * 1.08f;

    // And the flats above them. The interior atlas is already emissive -- it is
    // what stops a window reading as a black hole in daylight -- so at night it
    // only wants turning up, and unevenly: a building where every window is lit
    // is an office block at six o'clock, not a street of flats at nine.
    materials_.mutableGet(MaterialId::Interior).emissiveFactor = Vector3(2.35f, 2.15f, 1.80f);
}

void CityScene::buildVegetation(Rng& rng, const RenderSettings& settings)
{
    const PropFactory props(materials_);
    const float cull  = settings.propCullDistance;
    const float shade = settings.propShadowDistance;
    const float ground = M::kCurbHeight;

    // Six trees over three species. A row of identical trees is as obvious as a
    // row of identical windows, and a street planted in one season with one
    // species still has trees of visibly different ages in it.
    constexpr int kTreeVariants = 6;
    std::vector<PropMesh> trees;
    std::vector<PropMesh> distantTrees;
    trees.reserve(kTreeVariants);
    distantTrees.reserve(kTreeVariants);
    for (int i = 0; i < kTreeVariants; ++i)
    {
        const auto species = static_cast<PropFactory::TreeSpecies>(
            i % static_cast<int>(PropFactory::TreeSpecies::Count));
        const float height =
            species == PropFactory::TreeSpecies::Young
                ? M::kTreeHeightMin * 0.62f
                : M::kTreeHeightMin
                      + (M::kTreeHeightMax - M::kTreeHeightMin) * (static_cast<float>(i) + 0.5f)
                            / static_cast<float>(kTreeVariants);
        const std::string tag = std::to_string(i);
        // Six greens for six trees. No two trees in a row are the same colour:
        // one is yellower, one darker, one has had a drier summer -- and a row
        // in one exact green is what says "the same texture six times" even
        // when the shapes differ.
        static const Vector3 kFoliageTints[kTreeVariants] = {
            Vector3(1.00f, 1.00f, 1.00f), Vector3(0.90f, 1.02f, 0.84f),
            Vector3(1.06f, 0.98f, 0.76f), Vector3(0.84f, 0.94f, 0.88f),
            Vector3(0.96f, 1.04f, 0.80f), Vector3(1.02f, 0.92f, 0.72f),
        };
        const Material* foliage = materials_.deriveTinted(
            "foliage-" + tag, MaterialId::Foliage, kFoliageTints[i]);
        trees.push_back(makeProp("tree-" + tag, [&](GeometryCollector& c) {
            Rng own = Rng::derive(settings.seed, "tree-" + tag);
            props.tree(c, own, species, height, true, foliage);
        }));
        distantTrees.push_back(makeProp("tree-far-" + tag, [&](GeometryCollector& c) {
            // The same tree from the same seed, so the far version is the near
            // one with fewer twigs rather than a different tree that pops when
            // the camera crosses the switch distance.
            Rng own = Rng::derive(settings.seed, "tree-" + tag);
            props.tree(c, own, species, height, false, foliage);
        }));
    }
    farTrees_ = distantTrees;
    const PropMesh scruff = makeProp("ground-scruff", [&](GeometryCollector& c) {
        props.groundScruff(c, rng, 0.72f, 7);
    });
    const PropMesh grate = makeProp("tree-grate", [&](GeometryCollector& c) {
        props.treeGrate(c);
    });
    const PropMesh planter = makeProp("planter", [&](GeometryCollector& c) {
        props.planter(c, rng);
    });

    std::vector<std::vector<Matrix>> treeAt(kTreeVariants);
    std::vector<Matrix> grateAt, planterAt, scruffAt;

    for (const FootwayRun& run : layout_.footways())
    {
        const float dx = run.end.X - run.start.X;
        const float dz = run.end.Y - run.start.Y;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < 8.0f) continue;
        const Vector2 along(dx / length, dz / length);
        const Vector2 kerb = run.toKerb;
        const FootwayRhythm rhythm = RhythmFor(run);

        auto at = [&](float s, float lateral) {
            return Vector3(run.start.X + along.X * s + kerb.X * lateral, ground,
                           run.start.Y + along.Y * s + kerb.Y * lateral);
        };

        if (run.main)
        {
            // Street trees only on the main street: the side street's 2.6 m
            // footway cannot take a 1.6 m tree pit and still be a footway, which
            // is exactly why real narrow streets have no trees on them.
            int tree = 0;
            for (float s = rhythm.treeAt(0); s < length - 5.0f; s = rhythm.treeAt(++tree))
            {
                if (!rng.chance(0.82f)) continue;   // the gaps where one died
                const Vector3 p = at(s, FurnitureBand(run) + 0.15f);
                const int variant = rng.intRange(0, kTreeVariants - 1);
                treeAt[static_cast<std::size_t>(variant)].push_back(
                    Place(p.X, p.Y, p.Z, rng.range(0.0f, MathHelper::TwoPi)));
                treePositions_.push_back(p);
                // The grate sits flush with the paving, not on top of it.
                grateAt.push_back(Place(p.X, p.Y - 0.012f, p.Z,
                                        rng.chance(0.5f) ? 0.0f : MathHelper::PiOver2));
                // Weeds around a tree pit, on about half of them. Every pit
                // would be a derelict street; none would be a rendering.
                if (rng.chance(0.55f))
                    scruffAt.push_back(Place(p.X, p.Y + 0.002f, p.Z,
                                             rng.range(0.0f, MathHelper::TwoPi)));
            }
        }

        // Planters, on both streets, tucked against the building line where a
        // shop has put one out.
        for (float s = rhythm.first + 11.0f; s < length - 6.0f; s += 27.0f)
        {
            if (!rng.chance(0.45f)) continue;
            const Vector3 p = at(s + rng.signed_(3.0f), -(run.width * 0.5f - 0.55f));
            planterAt.push_back(Place(p.X, p.Y, p.Z, YawTowards(kerb)));
        }

        // And the scruff along the building line: grass through the joint where
        // a wall meets a pavement is one of the details a street has and a
        // rendering of one never does.
        for (float s = 4.0f; s < length - 4.0f; s += 5.5f)
        {
            if (!rng.chance(0.26f)) continue;
            const Vector3 p = at(s + rng.signed_(2.0f), -(run.width * 0.5f - 0.16f));
            scruffAt.push_back(Place(p.X, p.Y + 0.002f, p.Z, rng.range(0.0f, MathHelper::TwoPi)));
        }
    }

    for (int i = 0; i < kTreeVariants; ++i)
    {
        placeProp(trees[static_cast<std::size_t>(i)], treeAt[static_cast<std::size_t>(i)],
                  "tree-" + std::to_string(i), cull, shade,
                  /*castsShadow=*/true, &distantTrees[static_cast<std::size_t>(i)], 52.0f);
        buildStats_.trees += static_cast<int>(treeAt[static_cast<std::size_t>(i)].size());
    }
    placeProp(grate, grateAt, "tree-grate", cull * 0.35f, 0.0f, false);
    placeProp(planter, planterAt, "planter", cull * 0.5f, shade * 0.6f);
    placeProp(scruff, scruffAt, "ground-scruff", 42.0f, 0.0f, false);
}

void CityScene::buildSignalsAndSigns(Rng& rng, const RenderSettings& settings)
{
    const PropFactory props(materials_);
    const float cull   = settings.propCullDistance;
    const float shade  = settings.propShadowDistance;
    const float ground = M::kCurbHeight;

    // --- lens materials -----------------------------------------------------
    // One mesh per lens, two materials: the dark one is the catalogue entry, the
    // lit one adds an emissive factor bright enough to survive tone mapping in
    // daylight. A signal that is merely a brighter shade of its own colour does
    // not read as lit, and the whole point of the state machine is that it does.
    struct LensSpec { MaterialId id; const char* name; Vector3 emissive; };
    static const LensSpec kLenses[] = {
        {MaterialId::LensRed,       "red",        Vector3(2.40f, 0.20f, 0.12f)},
        {MaterialId::LensAmber,     "amber",      Vector3(2.55f, 1.20f, 0.16f)},
        {MaterialId::LensGreen,     "green",      Vector3(0.22f, 2.20f, 0.85f)},
        {MaterialId::LensWalkRed,   "walk-red",   Vector3(2.30f, 0.22f, 0.15f)},
        {MaterialId::LensWalkGreen, "walk-green", Vector3(0.25f, 2.10f, 0.88f)},
    };
    lensLit_.clear();
    lensDark_.clear();
    for (const LensSpec& spec : kLenses)
    {
        lensDark_.push_back(&materials_.get(spec.id));
        lensLit_.push_back(materials_.deriveTinted(
            std::string("lens-lit-") + spec.name, spec.id,
            materials_.get(spec.id).baseColour * 2.4f, spec.emissive));
    }

    lensRed_ = makeProp("lens-red", [&](GeometryCollector& c) {
        props.signalLens(c, MaterialId::LensRed, M::kSignalLensRadius);
    });
    lensAmber_ = makeProp("lens-amber", [&](GeometryCollector& c) {
        props.signalLens(c, MaterialId::LensAmber, M::kSignalLensRadius);
    });
    lensGreen_ = makeProp("lens-green", [&](GeometryCollector& c) {
        props.signalLens(c, MaterialId::LensGreen, M::kSignalLensRadius);
    });
    lensWalkRed_ = makeProp("lens-walk-red", [&](GeometryCollector& c) {
        props.signalLens(c, MaterialId::LensWalkRed, M::kSignalLensRadius * 0.92f);
    });
    lensWalkGreen_ = makeProp("lens-walk-green", [&](GeometryCollector& c) {
        props.signalLens(c, MaterialId::LensWalkGreen, M::kSignalLensRadius * 0.92f);
    });

    const PropMesh head    = makeProp("signal-head", [&](GeometryCollector& c) {
        props.signalHead(c);
    });
    const PropMesh pedHead = makeProp("signal-head-ped", [&](GeometryCollector& c) {
        props.pedestrianSignalHead(c);
    });
    const PropMesh post    = makeProp("signal-post", [&](GeometryCollector& c) {
        props.signalPost(c, M::kSignalPoleHeight);
    });
    const PropMesh pedPost = makeProp("signal-post-ped", [&](GeometryCollector& c) {
        props.signalPost(c, M::kPedSignalMountHeight + M::kPedSignalHousingHeight + 0.28f);
    });
    const PropMesh mast    = makeProp("signal-mast", [&](GeometryCollector& c) {
        props.signalMast(c, M::kSignalMastHeight, M::kSignalMastReach);
    });

    std::vector<Matrix> headAt, pedHeadAt, postAt, pedPostAt, mastAt;
    signalHeads_.clear();

    // Where the stop lines are. These come from the same numbers the traffic
    // model uses, so a vehicle stops at the line its own signal stands on.
    const float mainStop = M::kSideStreetHalfWidth + 1.4f + M::kZebraDepth + 1.0f;   // 12.25
    const float sideStop = M::kMainStreetHalfWidth + 1.2f + M::kZebraDepth + 1.0f;   // 15.50
    const float mainKerb = M::kMainCarriagewayWidth * 0.5f;
    const float sideKerb = M::kSideCarriagewayWidth * 0.5f;

    /// One approach to the junction: where its stop line is, which way its
    /// signals look, and which kerb they stand on.
    struct Approach
    {
        Vector2 postAt;      ///< near-side post, at the stop line
        Vector2 repeatAt;    ///< the far-side repeater, across the junction
        Vector2 mastAt;      ///< the mast base, or the post position when unused
        Vector2 facing;      ///< the direction the lenses look
        Vector2 reach;       ///< the way the mast arm goes, out over the road
        SignalAxis axis;
        bool     mast;
    };
    // Right-hand traffic, so each approach's near kerb is on its right.
    const Approach approaches[] = {
        // Northbound: east kerb, stopping south of the junction.
        {Vector2(mainKerb + 0.60f, -mainStop), Vector2(mainKerb + 0.60f, 6.60f),
         Vector2(mainKerb + 0.75f, -mainStop - 1.6f), Vector2(0.0f, -1.0f),
         Vector2(-1.0f, 0.0f), SignalAxis::Main, true},
        // Southbound: west kerb, stopping north of the junction.
        {Vector2(-mainKerb - 0.60f, mainStop), Vector2(-mainKerb - 0.60f, -6.60f),
         Vector2(-mainKerb - 0.75f, mainStop + 1.6f), Vector2(0.0f, 1.0f),
         Vector2(1.0f, 0.0f), SignalAxis::Main, true},
        // Eastbound: south kerb, stopping west of the junction.
        {Vector2(-sideStop, -sideKerb - 0.60f), Vector2(11.6f, -sideKerb - 0.60f),
         Vector2(-sideStop, -sideKerb - 0.60f), Vector2(-1.0f, 0.0f),
         Vector2(0.0f, 1.0f), SignalAxis::Side, false},
        // Westbound: north kerb, stopping east of the junction.
        {Vector2(sideStop, sideKerb + 0.60f), Vector2(-11.6f, sideKerb + 0.60f),
         Vector2(sideStop, sideKerb + 0.60f), Vector2(1.0f, 0.0f),
         Vector2(0.0f, -1.0f), SignalAxis::Side, false},
    };

    const float mount = ground + M::kSignalMountHeight;
    const float standoff = M::kSignalPoleRadius + M::kSignalHousingDepth * 0.5f;

    for (const Approach& approach : approaches)
    {
        const float yaw = YawTowards(approach.facing);
        for (const Vector2& where : {approach.postAt, approach.repeatAt})
        {
            postAt.push_back(Place(where.X, ground, where.Y, yaw));
            const Matrix at = Place(where.X + approach.facing.X * standoff, mount,
                                    where.Y + approach.facing.Y * standoff, yaw);
            headAt.push_back(at);
            signalHeads_.push_back(SignalHead{at, approach.axis, false});
        }

        if (!approach.mast) continue;
        // The mast puts a head over the middle of the approach lane, which is
        // what a driver at the stop line can actually see: the near-side head is
        // above their windscreen line by the time they are stopped at it.
        const float mastYaw = YawTowards(approach.reach);
        mastAt.push_back(Place(approach.mastAt.X, ground, approach.mastAt.Y, mastYaw));
        const Vector2 tip(approach.mastAt.X + approach.reach.X * M::kSignalMastReach,
                          approach.mastAt.Y + approach.reach.Y * M::kSignalMastReach);
        const float hang = ground + M::kSignalMastHeight + 0.55f - 0.05f - M::kSignalHousingHeight;
        const Matrix at = Place(tip.X, hang, tip.Y, yaw);
        headAt.push_back(at);
        signalHeads_.push_back(SignalHead{at, approach.axis, false});
    }

    // --- pedestrian heads ---------------------------------------------------
    // One at each end of each crossing, facing across it: the signal you read is
    // the one on the far kerb, which is why each head looks back over the road
    // it protects rather than out along the footway.
    const float pedMount = ground + M::kPedSignalMountHeight;
    const float pedStandoff = M::kSignalPoleRadius + M::kSignalHousingDepth * 0.85f * 0.5f;
    for (const Crossing& crossing : crossings_)
    {
        const Vector2 walk = crossing.walkDirection;
        Vector2 side = LeftOf(walk);
        // Put the post on the junction side of the crossing, where the people
        // waiting to cross actually stand.
        if (side.X * -crossing.centre.X + side.Y * -crossing.centre.Y < 0.0f)
            side = Vector2(-side.X, -side.Y);

        for (const float end : {-1.0f, 1.0f})
        {
            const Vector2 facing(-end * walk.X, -end * walk.Y);
            const float lateral = end * (crossing.halfLength + 0.62f);
            const float offset = crossing.halfDepth + 0.55f;
            const Vector2 where(crossing.centre.X + walk.X * lateral + side.X * offset,
                                crossing.centre.Y + walk.Y * lateral + side.Y * offset);
            const float yaw = YawTowards(facing);
            pedPostAt.push_back(Place(where.X, ground, where.Y, yaw));
            const Matrix at = Place(where.X + facing.X * pedStandoff, pedMount,
                                    where.Y + facing.Y * pedStandoff, yaw);
            pedHeadAt.push_back(at);
            signalHeads_.push_back(SignalHead{
                at, crossing.crossesMain ? SignalAxis::Main : SignalAxis::Side, true});
        }
    }

    placeProp(post, postAt, "signal-post", cull, shade);
    placeProp(pedPost, pedPostAt, "signal-post-ped", cull, shade);
    placeProp(mast, mastAt, "signal-mast", cull, shade);
    placeProp(head, headAt, "signal-head", cull, shade * 0.7f);
    placeProp(pedHead, pedHeadAt, "signal-head-ped", cull, shade * 0.7f);
    buildStats_.signals = static_cast<int>(signalHeads_.size());

    // --- signs --------------------------------------------------------------
    struct SignKind { SignShape shape; MaterialId face; float mount; };
    const SignKind speedLimit{SignShape::Disc, MaterialId::SignFaceProhibition,
                              M::kSignMountHeight};
    const SignKind priority{SignShape::Square, MaterialId::SignFacePriority, M::kSignMountHeight};
    const SignKind crossingSign{SignShape::Square, MaterialId::SignFaceInformation,
                                M::kSignMountHeight};
    const SignKind parking{SignShape::Rectangle, MaterialId::SignFaceParking,
                           M::kSignMountHeight};
    const SignKind children{SignShape::TriangleUp, MaterialId::SignFaceWarning,
                            M::kSignMountHeight};

    std::vector<const SignKind*> kinds{&speedLimit, &priority, &crossingSign, &parking, &children};
    std::vector<PropMesh> signMeshes;
    std::vector<std::vector<Matrix>> signAt(kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
        const SignKind& kind = *kinds[i];
        signMeshes.push_back(makeProp("sign-" + std::to_string(i), [&](GeometryCollector& c) {
            props.trafficSign(c, kind.shape, kind.face, kind.mount);
        }));
    }
    auto sign = [&](std::size_t kind, float x, float z, const Vector2& facing) {
        signAt[kind].push_back(Place(x, ground, z, YawTowards(facing)));
    };

    // A speed limit on the way into each arm, and the priority-road plate on the
    // main street where the side street gives way to it.
    sign(0, mainKerb + 0.75f, -46.0f, Vector2(0.0f, -1.0f));
    sign(0, -mainKerb - 0.75f, 46.0f, Vector2(0.0f, 1.0f));
    sign(0, -34.0f, -sideKerb - 0.70f, Vector2(-1.0f, 0.0f));
    sign(0, 34.0f, sideKerb + 0.70f, Vector2(1.0f, 0.0f));
    sign(1, mainKerb + 0.75f, -20.5f, Vector2(0.0f, -1.0f));
    sign(1, -mainKerb - 0.75f, 20.5f, Vector2(0.0f, 1.0f));

    // A crossing sign on the approach side of each crossing.
    for (const Crossing& crossing : crossings_)
    {
        const Vector2 walk = crossing.walkDirection;
        const Vector2 road = LeftOf(walk);
        for (const float end : {-1.0f, 1.0f})
        {
            // The sign faces the traffic that is about to reach the crossing,
            // and stands on the kerb that traffic passes.
            const Vector2 facing(-road.X * end, -road.Y * end);
            const float lateral = -end * (crossing.halfLength + 0.72f);
            const float offset = end * (crossing.halfDepth + 0.35f);
            sign(2, crossing.centre.X + walk.X * lateral + road.X * offset,
                 crossing.centre.Y + walk.Y * lateral + road.Y * offset, facing);
        }
    }

    // Parking restrictions along the kerbside lanes, and a warning triangle on
    // the side street where it narrows.
    sign(3, mainKerb + 0.72f, 30.0f, Vector2(0.0f, -1.0f));
    sign(3, -mainKerb - 0.72f, -30.0f, Vector2(0.0f, 1.0f));
    sign(3, mainKerb + 0.72f, -68.0f, Vector2(0.0f, -1.0f));
    sign(3, -mainKerb - 0.72f, 68.0f, Vector2(0.0f, 1.0f));
    sign(4, -22.0f, sideKerb + 0.70f, Vector2(1.0f, 0.0f));
    sign(4, 22.0f, -sideKerb - 0.70f, Vector2(-1.0f, 0.0f));

    for (std::size_t i = 0; i < kinds.size(); ++i)
        placeProp(signMeshes[i], signAt[i], "sign-" + std::to_string(i), cull * 0.6f, shade * 0.6f);

    // --- street-name plates -------------------------------------------------
    const PropMesh plateMain = makeProp("street-plate-main", [&](GeometryCollector& c) {
        props.streetPlate(c, MaterialId::SignFaceStreetName);
    });
    const PropMesh plateSide = makeProp("street-plate-side", [&](GeometryCollector& c) {
        props.streetPlate(c, MaterialId::SignFaceStreetNameSide);
    });
    std::vector<Matrix> plateMainAt, plateSideAt;

    // On the corner buildings, above the shop fascia where there is one. A plate
    // at the standard 2.85 m would be behind a shop window on half of these
    // corners, and a street sign you cannot read is worse than none.
    auto plotAt = [&](float x, float z) -> const Plot* {
        for (const Plot& plot : layout_.plots())
            if (x >= plot.minX && x <= plot.maxX && z >= plot.minZ && z <= plot.maxZ)
                return &plot;
        return nullptr;
    };
    int corner = 0;
    for (const float sx : {-1.0f, 1.0f})
        for (const float sz : {-1.0f, 1.0f})
        {
            const float wallX = sx * M::kMainStreetHalfWidth;
            const float alongZ = sz * (M::kSideStreetHalfWidth + 2.2f);
            const Plot* plot = plotAt(wallX + sx * 0.6f, alongZ);
            const float height = ground
                                 + (plot != nullptr && plot->hasShop
                                        ? plot->groundFloorHeight + 0.42f
                                        : M::kStreetPlateMount);
            const Vector2 outward(-sx, 0.0f);
            std::vector<Matrix>& target = (corner++ % 2 == 0) ? plateMainAt : plateSideAt;
            // The side-street plate goes on the return elevation of the same
            // corner, which is where the two names actually meet.
            if (&target == &plateSideAt)
            {
                const float wallZ = sz * M::kSideStreetHalfWidth;
                const float alongX = sx * (M::kMainStreetHalfWidth + 2.2f);
                target.push_back(Place(alongX, height, wallZ,
                                       YawTowards(Vector2(0.0f, -sz))));
            }
            else
            {
                target.push_back(Place(wallX, height, alongZ, YawTowards(outward)));
            }
        }
    placeProp(plateMain, plateMainAt, "street-plate-main", cull * 0.5f, 0.0f, false);
    placeProp(plateSide, plateSideAt, "street-plate-side", cull * 0.5f, 0.0f, false);

    (void)rng;
}

void CityScene::buildSignage(Rng& rng, const RenderSettings& settings)
{
    // The façade generator does not know what a shop is called until the plot
    // says so, and it should not be uploading textures in the middle of building
    // a wall, so it drops an anchor -- a position, a normal and a size -- and
    // this pass fills them in afterwards.
    if (anchors_.empty()) return;

    GeometryCollector collector;
    const std::vector<Plot>& plots = layout_.plots();

    // The board colours a shopping street actually has: dark green, oxblood,
    // navy, black and cream, with the lettering that goes with each.
    struct Board { Vector3 board; Vector3 letter; };
    static const Board kBoards[] = {
        {Vector3(0.055f, 0.115f, 0.075f), Vector3(0.90f, 0.87f, 0.74f)},
        {Vector3(0.170f, 0.045f, 0.048f), Vector3(0.93f, 0.90f, 0.84f)},
        {Vector3(0.040f, 0.062f, 0.130f), Vector3(0.92f, 0.92f, 0.90f)},
        {Vector3(0.048f, 0.048f, 0.052f), Vector3(0.88f, 0.84f, 0.60f)},
        {Vector3(0.760f, 0.735f, 0.660f), Vector3(0.13f, 0.12f, 0.11f)},
        {Vector3(0.105f, 0.105f, 0.098f), Vector3(0.86f, 0.88f, 0.90f)},
    };

    int signs = 0;
    for (const FacadeAnchor& anchor : anchors_)
    {
        const bool fascia = anchor.kind == FacadeAnchor::Kind::ShopFascia;
        if (!fascia && anchor.kind != FacadeAnchor::Kind::HouseNumber) continue;
        if (anchor.plotIndex < 0 || anchor.plotIndex >= static_cast<int>(plots.size())) continue;
        const Plot& plot = plots[static_cast<std::size_t>(anchor.plotIndex)];

        std::string text;
        const Board& board = kBoards[rng.index(std::size(kBoards))];
        if (fascia)
        {
            if (plot.shopName.empty()) continue;
            text = plot.shopName;
        }
        else
        {
            // House numbers run up each side of the street, odds one way and
            // evens the other, which is how a street is numbered everywhere.
            const int number = 1 + anchor.plotIndex * 2
                               + ((anchor.plotIndex % 2 == 0) ? 0 : 1);
            text = std::to_string(number);
        }

        // The texture is drawn at the board's own aspect ratio. A fixed 512x128
        // image stretched across a twelve-metre fascia turns the lettering into a
        // smear four times too wide, and every shop on the street then carries
        // the same illegible smear.
        const float aspect = anchor.height > 1e-3f ? anchor.width / anchor.height : 4.0f;
        const int bandHeight = fascia ? 128 : 160;
        const int boardWidth = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(bandHeight)
                                         * static_cast<double>(aspect))),
            96, 1024);

        const std::string name = (fascia ? "fascia-" : "number-") + text + "."
                                 + std::to_string(boardWidth) + "x" + std::to_string(bandHeight);
        const Material* material = materials_.find(name);
        if (material == nullptr)
        {
            const float boardRgb[3]  = {board.board.X, board.board.Y, board.board.Z};
            const float letterRgb[3] = {board.letter.X, board.letter.Y, board.letter.Z};
            Material sign;
            sign.roughness   = fascia ? 0.42f : 0.36f;
            sign.metallic    = 0.0f;
            sign.castsShadow = false;
            material = materials_.add(
                name,
                Assets::SignFactory::shopFascia(text, boardRgb, letterRgb, boardWidth, bandHeight,
                                                settings.seed + static_cast<std::uint32_t>(signs)),
                sign);
        }
        if (material == nullptr) continue;
        // `shopFascia` draws into a square canvas and fills only the top
        // `height / max(width, height)` of it, so only that band is sampled.
        const float band = static_cast<float>(bandHeight)
                           / static_cast<float>(std::max(boardWidth, bandHeight));

        // The board hangs on the wall: local +Z is the anchor's normal, local Y
        // is up, and local X is whichever way puts the lettering the right way
        // round on that elevation.
        const Vector3 up(0.0f, 1.0f, 0.0f);
        Vector3 right = Vector3::Cross(up, anchor.normal);
        if (right.LengthSquared() < 1e-6f) right = Vector3::Right;
        right = Vector3::Normalize(right);

        const float halfW = anchor.width * 0.5f;
        const float halfH = anchor.height * 0.5f;
        const Vector3 at = anchor.position;
        collector.setRegion(at.X, at.Z);
        MeshBuilder& builder = collector.builder(material);
        builder.setUvMode(Geometry::UvMode::Explicit);
        const Vector3 bl = at - right * halfW - up * halfH;
        const Vector3 br = at + right * halfW - up * halfH;
        const Vector3 tr = at + right * halfW + up * halfH;
        const Vector3 tl = at - right * halfW + up * halfH;
        const bool flip = Vector3::Dot(Vector3::Cross(br - bl, tr - bl), anchor.normal) < 0.0f;
        if (flip)
            builder.addQuadUv(br, bl, tl, tr, Vector2(1.0f, band), Vector2(0.0f, band),
                              Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f));
        else
            builder.addQuadUv(bl, br, tr, tl, Vector2(0.0f, band), Vector2(1.0f, band),
                              Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f));
        ++signs;
    }

    publish(collector, settings.propCullDistance, 0.0f);
    CNA::Logger::Info("cna-street: " + std::to_string(signs) + " shop signs and house numbers");
}

void CityScene::buildShopDisplays(const RenderSettings& settings)
{
    if (displays_.empty()) return;

    // What each kind of shop puts in its window, in preference order. The first
    // model that is actually present wins, so a tree that has fetched three of
    // the sixteen still dresses three windows properly rather than none.
    struct Choice { ShopKind kind; const char* assets[4]; };
    static const Choice kCatalogue[] = {
        {ShopKind::Bakery,      {"vase-flowers", "plant", nullptr, nullptr}},
        {ShopKind::Clothing,    {"corset", "sunglasses", "vase-flowers", nullptr}},
        {ShopKind::Convenience, {"water-bottle", "avocado", "boombox", nullptr}},
        {ShopKind::Electrical,  {"boombox", "camera", "water-bottle", nullptr}},
        {ShopKind::Florist,     {"vase-flowers", "plant", nullptr, nullptr}},
        {ShopKind::Optician,    {"sunglasses", "camera", nullptr, nullptr}},
        {ShopKind::Furniture,   {"chair-damask", "plant", "lantern", nullptr}},
        {ShopKind::Office,      {"plant", "lantern", nullptr, nullptr}},
    };

    constexpr int kDisplayTriangleBudget = 22000;
    Rng rng = Rng::derive(settings.seed, "displays");
    std::set<std::string> skippedForBudget;
    int dressed = 0;
    for (const ShopDisplay& display : displays_)
    {
        const Choice* choice = nullptr;
        for (const Choice& candidate : kCatalogue)
            if (candidate.kind == display.kind) { choice = &candidate; break; }
        if (choice == nullptr) continue;

        // Rotate the candidate list per plinth, so two windows of the same trade
        // are not the same window.
        //
        // And a triangle budget. The Khronos sample set is a set of *material*
        // showcases, authored to be filmed on a turntable rather than stood on
        // a 40 cm plinth behind a pane of glass, and their weights are: avocado
        // 682, water bottle 4 510, lantern 5 394, boombox 6 036, chair 9 984,
        // sunglasses 13 396, vase of flowers 14 036, corset 18 324, camera
        // 20 066 -- and the plant, 68 409, of which 54 000 are one part.
        //
        // 22 000 admits every one of them except the plant, which is the only
        // real outlier: a shrub carrying more geometry than the building it
        // stands in. A shop whose whole candidate list is over budget gets a
        // bare plinth, which is a thing real shops have.
        //
        // What this is *not* fixing is worth saying, because the first version
        // of this comment got it wrong. The batch report counts a family's
        // triangles with its copies included, so thirty-nine dressed windows
        // read as 850 000 -- but the copies share one mesh, so the geometry in
        // memory is one per model and the drawn cost is bounded by the 22 m
        // cull, which admits three or four at a time. The budget is here for
        // the outlier and for the 4K texture set that comes with it, not to
        // rescue a frame time that was never in danger.
        const int offset = rng.intRange(0, 3);
        const ModelLibrary::Imported* model = nullptr;
        for (int i = 0; i < 4 && model == nullptr; ++i)
        {
            const char* name = choice->assets[(i + offset) % 4];
            if (name == nullptr) continue;
            const ModelLibrary::Imported* candidate = models_.load(name);
            if (candidate == nullptr) continue;
            if (candidate->triangleCount() > kDisplayTriangleBudget)
            {
                skippedForBudget.insert(candidate->name);
                continue;
            }
            model = candidate;
        }
        if (model == nullptr) continue;

        // Sized to the plinth rather than to whatever the file was authored at,
        // and turned a little, because a row of props all square to the glass
        // reads as a catalogue page.
        const Matrix fit  = ModelLibrary::fitTo(*model, display.span);
        const Matrix turn = Matrix::CreateRotationY(rng.range(-0.6f, 0.6f));
        for (const ModelLibrary::Part& part : model->parts)
        {
            SceneItem item;
            item.mesh        = part.mesh;
            item.material    = part.material;
            item.world       = part.bone * fit * turn * display.stand;
            // A prop inside a shop is invisible well before the width of the
            // street runs out: it is behind glass, in shadow, and forty
            // centimetres across. 46 m was a guess and it cost real frame time
            // -- a draw call is worth about six hundred triangles on this
            // rasteriser, so a dozen invisible props behind glass at forty
            // metres is the most expensive nothing in the scene. 22 m is a
            // little further than the far footway, which is as far as anybody
            // can make out what is on a plinth.
            item.cullDistance   = 22.0f;
            item.shadowDistance = 0.001f;   // never a shadow caster
            renderer_.addItem(item);
            ++buildStats_.staticBatches;
            buildStats_.triangles += static_cast<std::size_t>(part.mesh->triangleCount());
        }
        ++dressed;
    }

    for (const std::string& name : skippedForBudget)
        CNA::Logger::Info("cna-street: imported '" + name + "' is over the "
                          + std::to_string(kDisplayTriangleBudget)
                          + "-triangle budget for a window display and was not used");
    CNA::Logger::Info("cna-street: " + std::to_string(dressed) + " of "
                      + std::to_string(displays_.size()) + " window displays dressed from "
                      + std::to_string(models_.loadedCount()) + " imported model(s)");
    for (const std::string& failure : models_.failures())
        CNA::Logger::Debug("cna-street: model unavailable -- " + failure);
}

void CityScene::buildTrafficAndPeople(const RenderSettings& settings)
{
    // --- the fleet ----------------------------------------------------------
    // Ten meshes, ten paints. The colours are the ones a European street park
    // actually has: more than half of it is white, black, grey or silver, and
    // the saturated cars are the exception that makes the row read as a row of
    // individual cars rather than a colour chart.
    static const Vector3 kPaints[TrafficSystem::kVariantCount] = {
        Vector3(0.62f, 0.63f, 0.65f),   // silver
        Vector3(0.045f, 0.048f, 0.052f),// black
        Vector3(0.70f, 0.70f, 0.69f),   // white
        Vector3(0.16f, 0.17f, 0.19f),   // graphite
        Vector3(0.09f, 0.13f, 0.30f),   // dark blue
        Vector3(0.42f, 0.44f, 0.45f),   // grey
        Vector3(0.30f, 0.06f, 0.07f),   // dark red
        Vector3(0.10f, 0.20f, 0.14f),   // British racing green
        Vector3(0.66f, 0.30f, 0.06f),   // copper
        Vector3(0.30f, 0.33f, 0.36f),   // slate
        Vector3(0.55f, 0.50f, 0.36f),   // sand
        Vector3(0.72f, 0.72f, 0.71f),   // white van
    };
    static_assert(std::size(kPaints) == static_cast<std::size_t>(TrafficSystem::kVariantCount),
                  "every vehicle variant needs a paint colour, or the last ones come out black");

    const VehicleFactory vehicles(materials_);
    vehicleMeshes_.clear();
    vehicleMeshes_.reserve(TrafficSystem::kVariantCount);
    // Wheels are built per class, not per paint variant: an alloy wheel is the
    // same object on a silver car and a red one, and building twelve copies of
    // it would cost twelve draw calls' worth of instance groups for nothing.
    std::vector<PropMesh> wheelByType(static_cast<std::size_t>(VehicleType::Count));
    for (int t = 0; t < static_cast<int>(VehicleType::Count); ++t)
    {
        const VehicleType type = static_cast<VehicleType>(t);
        wheelByType[static_cast<std::size_t>(t)] =
            makeProp(std::string("wheel-") + VehicleFactory::name(type),
                     [&](GeometryCollector& c) {
                vehicles.buildWheel(c, type, VehicleFactory::Detail::Full);
            });
    }

    brakeLit_ = materials_.deriveTinted("car-brake-lit", MaterialId::CarLightRear,
                                        Vector3(0.72f, 0.06f, 0.05f),
                                        Vector3(3.4f, 0.16f, 0.10f));

    for (int variant = 0; variant < TrafficSystem::kVariantCount; ++variant)
    {
        const std::string suffix = std::to_string(variant);
        const Material* paint = materials_.deriveTinted(
            "car-paint-" + suffix, MaterialId::CarBody,
            kPaints[static_cast<std::size_t>(variant)]);
        Rng rng = Rng::derive(settings.seed, "vehicle-" + suffix);
        const VehicleType type = TrafficSystem::typeForVariant(variant);

        VehicleMesh entry;
        entry.body = makeProp("vehicle-" + suffix, [&](GeometryCollector& c) {
            vehicles.build(c, type, paint, rng, VehicleFactory::Detail::Full);
        });
        entry.distantBody = makeProp("vehicle-far-" + suffix, [&](GeometryCollector& c) {
            Rng far = Rng::derive(settings.seed, "vehicle-far-" + suffix);
            vehicles.build(c, type, paint, far, VehicleFactory::Detail::Distant);
            // And its wheels, welded on.
            //
            // A wheel is three materials -- tyre, brake well, rim -- at four
            // corners, so a car submitted with separate wheels costs twelve
            // draw calls for the wheels alone. Forty cars down a street was
            // five hundred draws, which on this rasteriser is fifteen
            // milliseconds spent on the fact that wheels are round.
            //
            // Past the switch distance a wheel is eight pixels across and its
            // rotation is invisible, so the far body carries them baked in at
            // the straight-ahead position. Twelve draws become none, the
            // silhouette is identical, and the only thing lost is a rotation
            // nobody at that distance was ever going to see. Steering and
            // rolling stay on the near mesh, where they read.
            GeometryCollector scratch;
            vehicles.buildWheel(scratch, type, VehicleFactory::Detail::Distant);
            const std::vector<GeometryCollector::Batch> wheel = scratch.take();
            for (const WheelPlacement& place : VehicleFactory::wheelsFor(type))
            {
                Matrix local = Matrix::CreateTranslation(place.centre);
                if (place.side < 0.0f)
                    local = Matrix::CreateScale(-1.0f, 1.0f, 1.0f) * local;
                for (const GeometryCollector::Batch& batch : wheel)
                    c.builder(batch.material).append(batch.mesh, local);
            }
        });
        entry.wheel        = wheelByType[static_cast<std::size_t>(type)];
        entry.wheels       = VehicleFactory::wheelsFor(type);
        entry.brakeLamps   = makeProp("brake-" + suffix, [&](GeometryCollector& c) {
            vehicles.buildBrakeLamps(c, type);
        });
        vehicleMeshes_.push_back(std::move(entry));
    }

    // --- the imported rig ----------------------------------------------------
    // Loaded, and deliberately not placed. See docs/cna-findings.md GLTF-207
    // and GLTF-208: the skeleton and the clip come back out of the compiled
    // model correctly -- nineteen bones, one named clip, a well-formed
    // nineteen-matrix palette from `AnimationPlayer` -- and the mesh still
    // draws nothing through this application's skinned path, with a vertex
    // declaration that matches the effect's byte for byte. Loading it at
    // start-up keeps the round trip exercised and logged; standing it on the
    // pavement would put an invisible person on the street and a claim in the
    // documentation that the screenshots do not support.
    importedWalker_ = models_.loadRig("cesium-man");
    if (importedWalker_ != nullptr && importedWalker_->skinning != nullptr)
    {
        importedPlayer_ =
            std::make_unique<Graphics::AnimationPlayer>(*importedWalker_->skinning);
        std::string clips;
        for (const std::string& clip : importedWalker_->clips)
            clips += (clips.empty() ? "" : ", ") + clip;
        CNA::Logger::Info("cna-street: imported rig round-trip verified -- "
                          + std::to_string(importedWalker_->parts.size()) + " skinned part(s), "
                          + std::to_string(importedWalker_->skinning->BoneCount)
                          + " bones, clip(s): " + clips + "; not placed, see cna-findings GLTF-208");
    }

    // --- the people ---------------------------------------------------------
    static const Vector3 kSkinTones[] = {
        Vector3(0.76f, 0.60f, 0.50f), Vector3(0.60f, 0.44f, 0.34f),
        Vector3(0.42f, 0.29f, 0.22f), Vector3(0.27f, 0.18f, 0.13f),
    };
    static const Vector3 kCoatColours[] = {
        Vector3(0.13f, 0.14f, 0.17f), Vector3(0.32f, 0.12f, 0.14f),
        Vector3(0.10f, 0.20f, 0.31f), Vector3(0.52f, 0.47f, 0.38f),
        Vector3(0.20f, 0.24f, 0.20f), Vector3(0.62f, 0.61f, 0.60f),
        Vector3(0.44f, 0.20f, 0.32f), Vector3(0.16f, 0.34f, 0.33f),
    };
    static const Vector3 kTrouserColours[] = {
        Vector3(0.15f, 0.17f, 0.24f), Vector3(0.10f, 0.10f, 0.11f),
        Vector3(0.30f, 0.28f, 0.25f), Vector3(0.19f, 0.22f, 0.30f),
    };
    static const Vector3 kHairColours[] = {
        Vector3(0.035f, 0.030f, 0.028f), Vector3(0.075f, 0.052f, 0.038f),
        Vector3(0.135f, 0.088f, 0.052f), Vector3(0.235f, 0.175f, 0.098f),
        Vector3(0.330f, 0.315f, 0.300f), Vector3(0.145f, 0.075f, 0.045f),
    };

    // Every figure is one skinned mesh with a nineteen-bone rig, animated on the
    // GPU. The version this replaces baked eight poses of a stride plus a
    // standing one -- 72 meshes for eight people -- and the pose changed in
    // eight discrete steps. One mesh each and a clip is less geometry, smoother
    // motion, and it is the skeletal path CNA actually has.
    const CharacterFactory characters(materials_);
    characterMeshes_.clear();
    characterMeshes_.reserve(static_cast<std::size_t>(PedestrianSystem::kVariantCount));

    for (int variant = 0; variant < PedestrianSystem::kVariantCount; ++variant)
    {
        const std::string suffix = std::to_string(variant);
        Rng pick = Rng::derive(settings.seed, "person-" + suffix);

        CharacterLook look = characters.look(pick, variant);
        look.skin = materials_.deriveTinted("skin-" + suffix, MaterialId::Skin,
                                            kSkinTones[pick.index(std::size(kSkinTones))]);
        look.coat = materials_.deriveTinted(
            "coat-" + suffix, MaterialId::Clothing,
            kCoatColours[static_cast<std::size_t>(variant) % std::size(kCoatColours)]);
        look.trousers = materials_.deriveTinted(
            "trousers-" + suffix, MaterialId::Clothing,
            kTrouserColours[pick.index(std::size(kTrouserColours))]);
        look.hair = materials_.deriveTinted("hair-" + suffix, MaterialId::Clothing,
                                            kHairColours[pick.index(std::size(kHairColours))]);
        look.shoes = materials_.deriveTinted("shoes-" + suffix, MaterialId::Clothing,
                                             Vector3(0.030f, 0.030f, 0.034f));

        auto entry = std::make_unique<CharacterMesh>();
        entry->height = look.height;

        const CharacterFactory::Character full = characters.build(look, true);
        for (std::size_t part = 0; part < full.parts.size(); ++part)
        {
            auto mesh = std::make_unique<SkinnedGpuMesh>(
                device_, full.parts[part].mesh,
                "person-" + suffix + "." + std::to_string(part));
            buildStats_.meshBytes += mesh->gpuBytes();
            buildStats_.triangles += static_cast<std::size_t>(mesh->triangleCount());
            entry->parts.push_back(
                CharacterMesh::Part{full.parts[part].material, std::move(mesh)});
        }

        const CharacterFactory::Character far = characters.build(look, false);
        for (std::size_t part = 0; part < far.parts.size(); ++part)
        {
            auto mesh = std::make_unique<SkinnedGpuMesh>(
                device_, far.parts[part].mesh,
                "person-far-" + suffix + "." + std::to_string(part));
            buildStats_.meshBytes += mesh->gpuBytes();
            buildStats_.triangles += static_cast<std::size_t>(mesh->triangleCount());
            entry->farParts.push_back(
                CharacterMesh::Part{far.parts[part].material, std::move(mesh)});
        }

        // The skeleton, in the form AnimationPlayer wants. Held by unique_ptr
        // because every player holds a reference to it for its whole life.
        entry->skinning.BoneCount         = full.skeleton.count();
        entry->skinning.SkeletonHierarchy = full.skeleton.hierarchy();
        entry->skinning.BindPose          = full.skeleton.bindPose();
        entry->skinning.InverseBindPose   = full.skeleton.inverseBindPose();
        const CharacterFactory::Clips clips =
            CharacterFactory::clips(full.skeleton, look.height, 1.06f);
        entry->skinning.AnimationClips["walk"] = clips.walk;
        entry->skinning.AnimationClips["idle"] = clips.idle;

        // A rigid stand-in for the shadow pass. CNA's cascade caster takes its
        // world matrix from a uniform and knows nothing about bones, so a
        // skinned figure cannot cast its own shadow; see docs/cna-findings.md
        // CNA-F14. This is the same figure in its bind pose at half the ring
        // count, which at the sun angles a street is lit by is a long thin blob
        // on the pavement either way -- and a person with no shadow at all
        // floats.
        entry->shadowProxy = makeProp("person-shadow-" + suffix, [&](GeometryCollector& c) {
            const CharacterFactory::Character proxy = characters.build(look, false);
            for (const CharacterFactory::Character::Part& part : proxy.parts)
            {
                Geometry::MeshBuilder& builder = c.builder(part.material);
                Geometry::MeshData plain;
                plain.indices = part.mesh.indices;
                plain.vertices.reserve(part.mesh.vertices.size());
                for (const Geometry::SkinnedVertex& v : part.mesh.vertices)
                    plain.vertices.emplace_back(v.Position, v.Normal, v.Tangent,
                                                v.TextureCoordinate);
                builder.append(plain);
            }
        });

        characterMeshes_.push_back(std::move(entry));
    }

    // --- the simulations ----------------------------------------------------
    // Built whatever the settings say: the traffic and pedestrian switches turn
    // off updating and drawing, and rebuilding the whole population when one is
    // flicked back on would stall the frame for no reason.
    // The counts a shopping street of this size actually carries. They are
    // affordable because the renderer culls a mover by distance as well as by
    // frustum: what is on screen is a few dozen, whatever the population is.
    lineup_ = settings.vehicleLineup;
    if (settings.vehicleLineup)
        traffic_.buildLineup(settings.seed);
    else
        traffic_.build(settings.seed, 30, 44);
    if (settings.vehicleLineup)
        pedestrians_.buildLineup(layout_, crossings_, settings.seed);
    else
        pedestrians_.build(layout_, crossings_, settings.seed, 78);
    buildStats_.vehicles = static_cast<int>(traffic_.vehicles().size());
    buildStats_.people   = static_cast<int>(pedestrians_.people().size());

    // Which of them is the imported one. Chosen from the seed rather than fixed
    // at zero so it is not always the same route, and only once the crowd
    // exists so the index cannot point past the end of it.

}

void CityScene::buildViewpoints()
{
    // Eye-height viewpoints chosen to answer the question the README asks: would
    // a stranger take this for a photograph of a street? Each one is a normal
    // place to stand, not a place picked because it hides something.
    viewpoints_.clear();
    const float eye = M::kCurbHeight + M::kEyeHeight;
    // Yaw 0 looks along -Z (south, toward the junction from the north arm);
    // +pi/2 looks east, pi north, 3pi/2 west.
    constexpr float kEast  = 1.5707963f;
    constexpr float kSouth = 0.0f;
    constexpr float kWest  = 4.7123890f;

    // Every one of these stands where a person could stand: on a footway, on a
    // crossing, or high enough to be a window. Three of the first set did not --
    // one was inside a building, one in a traffic lane a metre from a parked car
    // -- and the screenshots showed the inside of a shop and the underside of a
    // bumper. A viewpoint that is not a place is not a view of the street.
    viewpoints_.push_back(Viewpoint{"Footway looking south to the junction",
                                    Vector3(-7.4f, eye, 46.0f), kSouth, -0.035f, 1.0996f});
    // On the centre line rather than in a lane. The lanes are at +/-1.65 and a
    // car is 1.84 m wide, so the metre and a half between them is the only place
    // in the carriageway a camera can stand without spending half its frames
    // inside a moving vehicle.
    viewpoints_.push_back(Viewpoint{"On the crossing",
                                    Vector3(0.0f, eye, 15.5f), kSouth + 0.02f, 0.02f, 1.0996f});
    // On the footway outside the corner block, looking across the junction at it.
    viewpoints_.push_back(Viewpoint{"The corner block",
                                    Vector3(-7.6f, eye, 12.6f), kEast - 0.62f, 0.10f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"Down the side street",
                                    Vector3(31.0f, eye, -4.4f), kWest, 0.01f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"Looking up at the facades",
                                    Vector3(-6.6f, eye, -16.0f), kEast, 0.60f, 1.22f});
    // High over the carriageway rather than over a roof: the point of this view
    // is the junction and the roofscape around it, and a camera inside a block
    // sees only the block it is inside.
    viewpoints_.push_back(Viewpoint{"Above the junction",
                                    Vector3(3.0f, 44.0f, 62.0f), 0.03f, -0.60f, 1.0996f});
    // The canyon shot, from the footway rather than from the middle of the road:
    // a camera in a running traffic lane spends most of its frames inside a car.
    viewpoints_.push_back(Viewpoint{"The long view south",
                                    Vector3(-7.1f, eye + 0.30f, 104.0f), kSouth - 0.035f,
                                    -0.012f, 0.95f});
    // Far enough back to see a whole shopfront rather than one pane of it.
    viewpoints_.push_back(Viewpoint{"Shopfronts, close",
                                    Vector3(-5.9f, eye, 58.0f), kWest + 0.42f, 0.05f, 1.15f});

    // --- the close-ups ------------------------------------------------------
    // The set above answers "does this look like a street". These answer the
    // harder question: does it survive being *looked at*. Each one is aimed
    // squarely at something that used to be a weakness, from the distance a
    // person would actually see it from, and none of them is a forgiving angle.

    // A parked car at three metres, three-quarter front. Parked bays run down
    // the parking lane at x = ±4.40, on a 6.05 m pitch from z = 26.65.
    viewpoints_.push_back(Viewpoint{"Car, three metres",
                                    Vector3(0.6f, 1.42f, 34.6f), kSouth + 0.36f, -0.075f, 0.90f});
    // A pedestrian at four metres on the far footway, from a normal eye height.
    // Off the building line and out from under the trees: at x = -6.4 the
    // camera stood 80 cm from a tree pit, and a plane tree's trunk at 80 cm
    // fills a third of a 66-degree frame. The subject of a viewpoint has to be
    // the thing it is named after.
    viewpoints_.push_back(Viewpoint{"Pedestrian, four metres",
                                    Vector3(-7.6f, eye, 22.6f), kSouth + 0.10f, -0.045f, 0.80f});
    // Close enough to a shop window to see the glass, what is behind it, and
    // what is reflected in it, all at once.
    viewpoints_.push_back(Viewpoint{"Shop window",
                                    Vector3(-6.5f, 1.55f, 40.6f), kWest + 0.30f, -0.02f, 0.86f});
    // A low camera along the asphalt: aggregate, markings, the kerb line and a
    // gully, in one frame, at the angle that exposes tiling worst.
    viewpoints_.push_back(Viewpoint{"Road surface",
                                    Vector3(1.6f, 0.42f, 30.0f), kSouth - 0.20f, -0.10f, 1.05f});
    // A street tree from under it: trunk, branch structure, leaf silhouette.
    viewpoints_.push_back(Viewpoint{"Street tree",
                                    Vector3(-4.6f, 1.50f, 39.0f), kWest - 0.55f, 0.42f, 1.15f});
    // One bay of a façade filling the frame: reveal depth, sill, material scale.
    viewpoints_.push_back(Viewpoint{"Facade detail",
                                    Vector3(-6.2f, 3.10f, 52.0f), kWest + 0.18f, 0.30f, 0.80f});

    if (!lineup_) return;
    // The development line-up: a square side view and a three-quarter front of
    // every variant, in variant order, so `--lineup --capture` produces a
    // contact sheet of the whole fleet.
    for (int variant = 0; variant < TrafficSystem::kVariantCount; ++variant)
    {
        const Vector2 at = TrafficSystem::lineupPlace(variant);
        const std::string tag = std::to_string(variant) + " "
                                + VehicleFactory::name(TrafficSystem::typeForVariant(variant));
        viewpoints_.push_back(Viewpoint{"Side " + tag, Vector3(at.X - 7.4f, 0.95f, at.Y),
                                        kEast, 0.0f, 0.42f});
        viewpoints_.push_back(Viewpoint{"Front " + tag,
                                        Vector3(at.X - 5.4f, 1.45f, at.Y + 5.6f),
                                        kEast + 0.72f, -0.16f, 0.62f});
    }
    for (int variant = 0; variant < PedestrianSystem::kVariantCount; ++variant)
    {
        const Vector2 at = PedestrianSystem::lineupPlace(variant);
        // Square in front of a figure that is facing the road, at three
        // metres: the distance a person on the far pavement is seen from.
        viewpoints_.push_back(Viewpoint{"Person " + std::to_string(variant),
                                        Vector3(at.X + 3.0f, 1.06f, at.Y),
                                        -kEast, 0.0f, 0.52f});
    }
}

}  // namespace CnaStreet
