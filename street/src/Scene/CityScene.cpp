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
#include <cstdio>
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
    : device_(device), renderer_(renderer), materials_(materials), models_(models),
      characters_(materials)
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

    // Every stage says what it is doing, to the log and to whoever is painting
    // the window. The fractions are measured shares of a build on this
    // machine, not equal slices: raising the buildings is a third of it and
    // lettering the shopfronts is a blink, and a bar that pretends otherwise
    // is a bar that stalls at 60 per cent.
    const auto stage = [this](const char* what, float fraction) {
        CNA::Logger::Info(std::string("cna-street: ") + what);
        if (progress_) progress_(what, fraction);
    };

    stage("generating materials", 0.02f);
    materials_.build(settings.seed);
    if (settings.nightLighting()) lightTheStreet(settings);

    stage("laying out the street", 0.10f);
    layout_.generate(settings.seed);
    buildStats_.plots = static_cast<int>(layout_.plots().size());
    // Where each plot is and what it is built as, so a viewpoint can be aimed
    // at a brick warehouse rather than at where one was hoped to be.
    for (std::size_t i = 0; i < layout_.plots().size(); ++i)
    {
        const Plot& plot = layout_.plots()[i];
        CNA::Logger::Debug("cna-street: plot " + std::to_string(i) + " style "
                           + std::to_string(static_cast<int>(plot.style)) + " x "
                           + std::to_string(plot.minX) + ".." + std::to_string(plot.maxX) + " z "
                           + std::to_string(plot.minZ) + ".." + std::to_string(plot.maxZ)
                           + (plot.hasShop ? " shop '" + plot.shopName + "'" : ""));
    }

    stage("building the highway", 0.14f);
    {
        GeometryCollector collector;
        Rng rng = Rng::derive(settings.seed, "highway");
        RoadBuilder roads(layout_, materials_);
        roads.build(collector, rng);
        crossings_ = roads.crossings();
        manholes_  = roads.manholes();
        publish(collector, 0.0f,
               std::min(settings.architectureShadowDistance, settings.shadowDistance));
    }

    stage("raising the buildings", 0.22f);
    {
        GeometryCollector collector;
        GeometryCollector interiors;
        Rng rng = Rng::derive(settings.seed, "buildings");
        BuildingBuilder buildings(materials_, layout_);
        heroPlot_ = chooseHeroPlot();
        heroProps_.clear();
        // Every shop may ask for scanned props; the hero plot asks for the
        // most, and -1 means no plot is the hero.
        buildings.setHeroShop(heroPlot_, &heroProps_);
        const std::vector<Plot>& plots = layout_.plots();
        for (std::size_t i = 0; i < plots.size(); ++i)
            buildings.build(plots[i], static_cast<int>(i), collector, interiors, rng, anchors_,
                            displays_);
        // Architecture used to have no distance policy of its own and cast at
        // the cascades' full reach: every window reveal, cornice and quoin on
        // the modelled street, every frame, however far down it the camera
        // was looking. See RenderSettings::architectureShadowDistance.
        publish(collector, 0.0f,
               std::min(settings.architectureShadowDistance, settings.shadowDistance));
        // The rooms behind the glass, on a short leash and casting nothing: a
        // shop interior is invisible from the far pavement and its shadow is
        // invisible from anywhere. 34 m rather than the 54 it started at,
        // because a room seen obliquely through its own mullions past the width
        // of the street contributes a smear, and it contributes it in seven
        // draw calls per shop.
        publish(interiors, 34.0f, 0.0f);
    }

    stage("dressing the windows", 0.46f);
    buildShopDisplays(settings);

    if (settings.streetFurniture)
    {
        stage("placing street furniture", 0.52f);
        Rng rng = Rng::derive(settings.seed, "furniture");
        buildStreetFurniture(rng, settings);
    }
    if (settings.vegetation)
    {
        stage("planting", 0.58f);
        Rng rng = Rng::derive(settings.seed, "vegetation");
        buildVegetation(rng, settings);
    }
    stage("lettering the shopfronts", 0.66f);
    {
        Rng rng = Rng::derive(settings.seed, "signage");
        buildSignage(rng, settings);
    }
    stage("signalling the junction", 0.68f);
    {
        Rng rng = Rng::derive(settings.seed, "signals");
        buildSignalsAndSigns(rng, settings);
    }
    stage("traffic and people", 0.71f);
    buildTrafficAndPeople(settings);
    stage("dressing the street", 0.86f);
    {
        Rng rng = Rng::derive(settings.seed, "dressing");
        buildDressing(rng, settings);
    }
    buildHeroShop(settings);

    // After the trees and the vehicles, because the district beyond the
    // frontage borrows both: the same far trees at the same pitch, and the
    // same distant car bodies parked along its kerbs.
    stage("closing the skyline", 0.92f);
    {
        GeometryCollector collector;
        GeometryCollector infill;
        Rng rng = Rng::derive(settings.seed, "context");
        buildContext(collector, infill, rng, settings);
        // The district beyond the modelled frontage had no shadow distance
        // cap at all -- silhouette blocks visible only as a haze were casters
        // at the cascades' full reach. See
        // RenderSettings::contextShadowDistance.
        publish(collector, 0.0f,
               std::min(settings.contextShadowDistance, settings.shadowDistance));
        // The rows behind the frontage cast only when the camera is nearly on
        // top of them. From the street they stand behind a row of buildings
        // and their shadows land where nothing can see them; from above, the
        // near ones still shade their neighbours' roofs.
        publish(infill, 0.0f, std::min(60.0f, settings.shadowDistance));
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
    stage("capturing reflection probes", 0.96f);
    renderer_.bakeReflectionProbes(probePositions(settings), settings);
}

void CityScene::buildContext(GeometryCollector& collector, GeometryCollector& infill,
                             Rng& rng, const RenderSettings& settings)
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
    auto roof = [&](GeometryCollector& into, float cx, float cz, float halfX, float halfZ,
                    float height, const Material* gableMaterial, float storey) {
        if (rng.chance(0.45f))
        {
            MeshBuilder& tiles = into.builder(&materials_.get(MaterialId::RoofTile));
            tiles.setTileSize(1.4f);
            const bool alongX = halfX >= halfZ;
            const float rise = std::min(alongX ? halfZ : halfX, 4.2f) * 0.85f;
            const float x0 = cx - halfX - 0.25f, x1 = cx + halfX + 0.25f;
            const float z0 = cz - halfZ - 0.25f, z1 = cz + halfZ + 0.25f;
            MeshBuilder& gable = into.builder(gableMaterial);
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
            MeshBuilder& stack = into.builder(&materials_.get(MaterialId::BrickRed));
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
            MeshBuilder& felt = into.builder(&materials_.get(MaterialId::RoofFelt));
            felt.setTileSize(4.0f);
            felt.addBox(Vector3(cx - halfX - 0.1f, height, cz - halfZ - 0.1f),
                        Vector3(cx + halfX + 0.1f, height + 0.9f, cz + halfZ + 0.1f),
                        BoxFaces::allButBottom());
            // Roof furniture: plant, a lift overrun, a couple of vents. Flat
            // roofs are never empty and from any camera above the eaves that is
            // the difference between a city and a set of boxes.
            MeshBuilder& plant = into.builder(&materials_.get(MaterialId::GalvanisedSteel));
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
    auto paintedBlock = [&](GeometryCollector& into, float cx, float cz, float halfX,
                            float halfZ, float height, int regionKey = -1, int wallPick = -1) {
        // A block of its own cell, or a caller's coarser one. The rows behind
        // the frontage are batched a whole strip at a time (see infillRows):
        // at thirty triangles a block, what they cost the frame is draw
        // calls, not geometry, and a 34 m cell per block would be one draw
        // per block per material.
        if (regionKey >= 0) into.setRegionKey(regionKey);
        else                into.setRegion(cx, cz);
        // A caller may narrow the choice of facade -- see infillRows -- so a
        // batch of blocks shares fewer materials and so fewer draws.
        const std::size_t pick = wallPick >= 0
                                     ? static_cast<std::size_t>(wallPick) % std::size(walls)
                                     : rng.index(std::size(walls));
        const Material* material = &materials_.get(walls[pick]);
        MeshBuilder& builder = into.builder(material);
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
        roof(into, cx, cz, halfX, halfZ, height, material, storey);
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

        roof(collector, cx, cz, halfX, halfZ, eaves, wallMaterial, storeyH);
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
        paintedBlock(collector, 0.0f, sign * 345.0f, 46.0f, 22.0f, rng.range(18.0f, 30.0f));
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
        paintedBlock(collector, sign * 224.0f, 0.0f, 20.0f, 40.0f, rng.range(15.0f, 26.0f));
    }

    // --- the blocks behind the frontage --------------------------------
    // Everything above faces a street; nothing yet stands *behind* it. A real
    // city block is not one plate thick, and without this the district was a
    // single row of buildings backed by bare ground -- a checkerboard of grass
    // and dirt reaching all the way to the scattered skyline at 240 m and
    // beyond, invisible from the footway but the first thing any elevated or
    // tilted view shows, including this project's own `06-above-the-junction`
    // screenshot. Two more rows behind the street-facing one, cheaper than it
    // -- `paintedBlock`, no window geometry, because nobody ever stands close
    // enough to one of these to ask for a real opening -- each separated from
    // its neighbour by a service lane's width so the massing still reads as
    // blocks and not a slab, and thinning to bigger, plainer boxes in the
    // second row so the skyline scatter beyond it is not a visible step up in
    // both height and density at once.
    auto infillRows = [&](bool alongMain) {
        const float streetHalfWidth = alongMain ? M::kMainStreetHalfWidth : M::kSideStreetHalfWidth;
        const float streetHalfLength = alongMain ? M::kMainStreetHalfLength : M::kSideStreetHalfLength;
        const float reach = alongMain ? 300.0f : 190.0f;
        // The street-facing row above is 11 m deep from the back of its own
        // footway; a lane, then two rows of increasing depth and size.
        const float frontageBack = streetHalfWidth + 22.0f + 2.0f;
        struct Row { float near, far, minSpan, maxSpan, minHeight, maxHeight; };
        const Row rows[] = {
            {frontageBack + 3.0f, frontageBack + 32.0f, 14.0f, 24.0f, 9.0f, 20.0f},
            {frontageBack + 38.0f, frontageBack + 78.0f, 20.0f, 34.0f, 10.0f, 26.0f},
        };
        int strip = 0;
        for (const float sign : {-1.0f, 1.0f})
            for (const float side : {-1.0f, 1.0f})
            {
                // Both rows of a strip in one batch per 150 m per material, in
                // a key space of its own above the ground plane's, and each
                // 150 m of strip drawing on three of the six facades rather
                // than all of them. A strip is either in view or not from
                // almost anywhere a camera stands, and its blocks are thirty
                // triangles apiece, so the cull granularity given up here
                // costs nothing measurable; the draws it saves are most of
                // what an extra row of buildings costs at all. Measured on the
                // flagship view: one 34 m cell per block was +156 draws, this
                // is +55.
                const int stripKey = 2000000 + (alongMain ? 0 : 64) + strip * 8;
                ++strip;
                for (const Row& row : rows)
                {
                    const float depth = (row.far - row.near) * 0.5f;
                    const float cxBase = side * (row.near + depth);
                    // Starting a little past the junction rather than at it,
                    // so the crossroads itself keeps an open corner instead of
                    // a block looming over the signal heads.
                    float pos = streetHalfLength * 0.35f;
                    while (pos < reach)
                    {
                        const float span = rng.range(row.minSpan, row.maxSpan);
                        const int segment = static_cast<int>(pos / 150.0f);
                        const int key = stripKey + segment;
                        const int wallPick = (stripKey + segment * 5) % 6 + rng.intRange(0, 2);
                        if (alongMain)
                            paintedBlock(infill, cxBase, sign * (pos + span * 0.5f), depth,
                                        span * 0.5f, rng.range(row.minHeight, row.maxHeight),
                                        key, wallPick);
                        else
                            paintedBlock(infill, sign * (pos + span * 0.5f), cxBase, span * 0.5f,
                                        depth, rng.range(row.minHeight, row.maxHeight),
                                        key, wallPick);
                        pos += span + rng.range(3.0f, 7.0f);
                    }
                }
            }
    };
    infillRows(true);
    infillRows(false);

    // A far skyline: a scatter of taller blocks well beyond the district, which
    // is what stops the horizon being a clean line of identical parapets.
    for (int i = 0; i < 90; ++i)
    {
        const float angle = rng.range(0.0f, 6.2831853f);
        const float radius = rng.range(240.0f, 430.0f);
        const float cx = std::cos(angle) * radius;
        const float cz = std::sin(angle) * radius * 1.35f;
        paintedBlock(collector, cx, cz, rng.range(9.0f, 26.0f), rng.range(9.0f, 26.0f),
                     rng.range(12.0f, 44.0f));
    }

    // --- what stands in the far street ------------------------------------
    // The same trees at the same pitch and the same parked cars, so the street
    // does not stop being a street where the modelling stops. Both are the far
    // levels of detail, instanced, and cast no shadow: at 140 m and beyond a
    // shadow is a texel.
    if (settings.vegetation && !farTrees_.empty())
    {
        // The same species as the street itself, at their far level of
        // detail, so the planting does not change generation at the point
        // where the modelling stops. Where no scan was fetched these are the
        // generated far copies, which is what they always were.
        std::vector<const PropMesh*> kinds;
        for (const PropMesh& scanned : farHeroTrees_)
            if (!scanned.empty()) kinds.push_back(&scanned);
        if (kinds.empty())
            for (const PropMesh& generated : farTrees_) kinds.push_back(&generated);
        std::vector<std::vector<Matrix>> treeAt(kinds.size());
        const float treeX = M::kMainCarriagewayWidth * 0.5f + 0.85f;
        for (const float sign : {-1.0f, 1.0f})
            for (float z = M::kMainStreetHalfLength + 8.0f; z < 312.0f; z += 12.0f)
            {
                if (!rng.chance(0.80f)) continue;
                for (const float side : {-1.0f, 1.0f})
                {
                    const std::size_t kind = rng.index(kinds.size());
                    treeAt[kind].push_back(
                        Matrix::CreateScale(kind < farHeroScale_.size() ? farHeroScale_[kind] : 1.0f)
                        * Place(side * treeX, M::kCurbHeight, sign * z,
                                rng.range(0.0f, MathHelper::TwoPi)));
                }
            }
        for (std::size_t i = 0; i < kinds.size(); ++i)
            placeProp(*kinds[i], treeAt[i], "context-tree", 300.0f, 0.0f, false);
    }
    if (settings.traffic)
    {
        // The same eight authored cars at their far level of detail, not the
        // lofts. It is a *cheaper* street as well as a better one: eight
        // models of four primitives is thirty-two draw calls where twelve
        // lofts of nine were a hundred and eight, and a far copy is fifteen
        // thousand triangles that only ever draws past a hundred and thirty
        // metres. The lofts stay for a tree that has fetched no models.
        std::vector<const PropMesh*> kinds;
        for (const HeroVehicleMesh& hero : heroVehicleMeshes_)
            if (!hero.far.empty()) kinds.push_back(&hero.far);
        if (kinds.empty())
            for (const VehicleMesh& loft : vehicleMeshes_) kinds.push_back(&loft.distantBody);
        if (!kinds.empty())
        {
            std::vector<std::vector<Matrix>> carAt(kinds.size());
            const float bayX = M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f;
            for (const float sign : {-1.0f, 1.0f})
                for (float z = M::kMainStreetHalfLength + 4.0f; z < 316.0f;
                     z += M::kParkingBayLength + 0.6f)
                {
                    for (const float side : {-1.0f, 1.0f})
                    {
                        if (!rng.chance(0.62f)) continue;
                        // Parked with the traffic on its side of the road.
                        const float yaw = side > 0.0f ? 0.0f : MathHelper::Pi;
                        carAt[rng.index(kinds.size())].push_back(
                            Place(side * bayX, 0.0f, sign * (z + rng.signed_(0.4f)), yaw));
                    }
                }
            for (std::size_t i = 0; i < kinds.size(); ++i)
                placeProp(*kinds[i], carAt[i], "context-car", 300.0f, 0.0f, false);
        }
    }
}

namespace {

BoundingBox TransformBounds(const BoundingBox& box, const Matrix& transform)
{
    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner((i & 1) ? box.Max.X : box.Min.X, (i & 2) ? box.Max.Y : box.Min.Y,
                             (i & 4) ? box.Max.Z : box.Min.Z);
        const Vector3 p = Vector3::Transform(corner, transform);
        lo = Vector3(std::min(lo.X, p.X), std::min(lo.Y, p.Y), std::min(lo.Z, p.Z));
        hi = Vector3(std::max(hi.X, p.X), std::max(hi.Y, p.Y), std::max(hi.Z, p.Z));
    }
    return BoundingBox(lo, hi);
}

void Grow(Vector3& lo, Vector3& hi, const BoundingBox& box)
{
    lo = Vector3(std::min(lo.X, box.Min.X), std::min(lo.Y, box.Min.Y), std::min(lo.Z, box.Min.Z));
    hi = Vector3(std::max(hi.X, box.Max.X), std::max(hi.Y, box.Max.Y), std::max(hi.Z, box.Max.Z));
}

}  // namespace

void CityScene::PropMesh::append(const PropMesh& other, const Matrix& at)
{
    if (other.empty()) return;
    Vector3 lo = empty() ? Vector3(1e30f, 1e30f, 1e30f) : bounds.Min;
    Vector3 hi = empty() ? Vector3(-1e30f, -1e30f, -1e30f) : bounds.Max;
    for (const Part& part : other.parts)
    {
        Part placed = part;
        placed.local = part.local * at;
        Grow(lo, hi, TransformBounds(part.mesh->bounds(), placed.local));
        parts.push_back(placed);
    }
    bounds = BoundingBox(lo, hi);
}

CityScene::PropMesh CityScene::importedProp(const std::string& asset, const Matrix& adjust,
                                            const std::function<bool(const std::string&)>& include)
{
    PropMesh prop;
    const ModelLibrary::Imported* model = models_.load(asset);
    if (model == nullptr) return prop;
    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    for (const ModelLibrary::Part& part : model->parts)
    {
        if (part.mesh == nullptr || part.material == nullptr) continue;
        if (include && !include(part.node)) continue;
        PropMesh::Part placed;
        placed.material = part.material;
        placed.mesh     = part.mesh;
        placed.local    = part.bone * adjust;
        Grow(lo, hi, TransformBounds(part.mesh->bounds(), placed.local));
        prop.parts.push_back(placed);
    }
    if (!prop.parts.empty()) prop.bounds = BoundingBox(lo, hi);
    return prop;
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
        // An imported part hangs from a node of its own; the placement is of
        // the prop, so the node's transform goes in front of every copy.
        if (part.local != Matrix::getIdentityProperty())
            for (Matrix& transform : group.transforms) transform = part.local * transform;
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

void CityScene::placeShadowProxy(const PropMesh& proxy, const std::vector<Matrix>& transforms,
                                 const std::string& name, float shadowDistance)
{
    if (proxy.empty() || transforms.empty()) return;
    for (std::size_t i = 0; i < proxy.parts.size(); ++i)
    {
        const PropMesh::Part& part = proxy.parts[i];
        InstanceGroup group;
        group.mesh           = part.mesh;
        group.material       = part.material;
        group.transforms     = transforms;
        if (part.local != Matrix::getIdentityProperty())
            for (Matrix& transform : group.transforms) transform = part.local * transform;
        group.shadowDistance = shadowDistance;
        group.castsShadow    = true;
        group.shadowOnly     = true;
        group.name           = name;
        renderer_.addInstances(std::move(group));
        ++buildStats_.instanceGroups;
    }
}

void CityScene::submitProp(const PropMesh& prop, const Matrix& transform,
                           const Material* overrideMaterial, bool shadowOnly, DrawFamily family)
{
    for (const PropMesh::Part& part : prop.parts)
        renderer_.submitDynamic(part.mesh,
                                overrideMaterial != nullptr ? overrideMaterial : part.material,
                                part.local * transform, shadowOnly, family);
}

void CityScene::update(float deltaSeconds, const RenderSettings& settings)
{
    elapsedSeconds_ += deltaSeconds;
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
        const std::vector<Vehicle>& fleet = traffic_.vehicles();
        for (std::size_t v = 0; v < fleet.size(); ++v)
        {
            const Vehicle& vehicle = fleet[v];
            const Matrix world = vehicle.transform(traffic_.lanes());
            const Vector2 at   = vehicle.groundPosition(traffic_.lanes());
            const float distance = std::sqrt((at.X - eye.X) * (at.X - eye.X)
                                             + (at.Y - eye.Z) * (at.Y - eye.Z));
            if (distance > settings.propCullDistance) continue;

            // Somebody at the wheel, while the cabin is close enough to see
            // into. Past thirty metres a windscreen is a reflection and a
            // driver is four pixels, and the car has switched to its welded
            // far copy anyway. Before the parked-hero test below, so the
            // line-up -- where every car is parked and every car has somebody
            // in it -- can show a driver inside an authored cabin.
            if (distance < 30.0f && (!vehicle.parked || lineup_))
                submitDriver(v, vehicle, world, distance);

            // A parked loft a hero model stands in for is registered as a
            // static prop instead, so it is in the reflection probes too.
            if (v < vehicleReplaced_.size() && vehicleReplaced_[v]) continue;

            // A moving vehicle drawn as an authored car: the body, and four
            // wheels rolled from the odometer at their own radius and steered
            // on the front axle. Past the switch the far copy carries its
            // wheels welded on, as the lofts' does.
            if (v < heroForVehicle_.size() && heroForVehicle_[v] >= 0)
            {
                const HeroVehicleMesh& hero =
                    heroVehicleMeshes_[static_cast<std::size_t>(heroForVehicle_[v])];
                // The far copy from thirty-two metres, nearer than the
                // parked copies switch: a moving car is looked at less
                // hard than a parked one, and its wheels are eight to
                // twelve draws a frame the welded copy does not pay.
                if (distance >= 32.0f && !hero.far.empty())
                {
                    submitProp(hero.far, world, nullptr, false, DrawFamily::Vehicle);
                    continue;
                }
                submitProp(hero.body, world, nullptr, false, DrawFamily::Vehicle);
                const float rolled = vehicle.odometer / hero.wheelRadius;
                for (const HeroVehicleMesh::Wheel& wheel : hero.wheels)
                {
                    const Matrix axle = Matrix::CreateTranslation(wheel.centre) * world;
                    Matrix steer = Matrix::getIdentityProperty();
                    if (wheel.steered && vehicle.steerAngle != 0.0f)
                        steer = Matrix::CreateRotationY(vehicle.steerAngle);
                    submitProp(wheel.mesh, Matrix::CreateRotationX(rolled) * steer * axle,
                               nullptr, false, DrawFamily::Vehicle);
                    // The caliper, the upright and the arch liner turn with
                    // the stub axle and stand still otherwise.
                    if (!wheel.hub.empty())
                        submitProp(wheel.hub, steer * axle, nullptr, false, DrawFamily::Vehicle);
                }
                continue;
            }

            const int variant = std::clamp(vehicle.variant, 0,
                                           static_cast<int>(vehicleMeshes_.size()) - 1);
            const VehicleMesh& mesh = vehicleMeshes_[static_cast<std::size_t>(variant)];

            // One switch for the whole vehicle. Below 38 m the body is the full
            // mesh with its shut lines, mirrors and interior; past it the same
            // silhouette at a third of the stations, which is three pixels of
            // difference and two thirds of the triangles.
            const bool near = distance < 38.0f;
            submitProp(near ? mesh.body : mesh.distantBody, world, nullptr, false,
                       DrawFamily::Vehicle);
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
                submitProp(wheel, local * Matrix::CreateTranslation(place.centre) * world,
                           nullptr, false, DrawFamily::Vehicle);
            }
        }
    }

    // --- people -------------------------------------------------------------
    submitPeople(settings);
}

void CityScene::submitDriver(std::size_t index, const Vehicle& vehicle,
                             const Matrix& world, float distance)
{
    if (index >= driverForVehicle_.size()) return;
    const int variant = driverForVehicle_[index];
    if (variant < 0 || variant >= static_cast<int>(characterMeshes_.size())) return;
    if (index >= driverPlayers_.size() || driverPlayers_[index] == nullptr) return;
    const CharacterMesh& character = *characterMeshes_[static_cast<std::size_t>(variant)];

    const auto found = character.skinning.AnimationClips.find(
        CharacterFactory::Clips::kDriveName);
    if (found == character.skinning.AnimationClips.end()) return;
    const Graphics::AnimationClip& clip = found->second;

    Graphics::AnimationPlayer& player = *driverPlayers_[index];
    if (player.getCurrentClipProperty() != &clip) player.StartClip(clip);
    // Each car on its own phase, from the vehicle's index rather than a clock,
    // so two cars side by side at a red light are not one person twice.
    const float phase = static_cast<float>(index) * 1.37f;
    player.Update(System::TimeSpan::FromTicks(static_cast<std::int64_t>(
                      static_cast<double>(elapsedSeconds_ + phase) * 1.0e7)),
                  false, true);

    // Placed by the hips. `driverSeat` is the seat cushion in the car's own
    // frame; a figure's own origin is the soles, so it is lowered by the hip
    // height its bind pose actually has, scaled the same as the figure.
    const float scale = driverHeight_[index] / std::max(character.height, 0.1f);
    float drawnLength = 0.0f, drawnWidth = 0.0f, drawnHeight = 0.0f;
    if (index < heroForVehicle_.size() && heroForVehicle_[index] >= 0
        && static_cast<std::size_t>(heroForVehicle_[index]) < heroVehicleMeshes_.size())
    {
        const HeroVehicleMesh& drawn =
            heroVehicleMeshes_[static_cast<std::size_t>(heroForVehicle_[index])];
        drawnLength = drawn.length;
        drawnWidth  = drawn.width;
        drawnHeight = drawn.height;
    }
    const Matrix world3 =
        Matrix::CreateScale(scale)
        * Matrix::CreateTranslation(0.0f, -character.hipHeight * scale, 0.0f)
        * driverSeat(vehicle, steeringSideFor(index), drawnLength, drawnWidth,
                     drawnHeight) * world;

    // Always the collapsed set. A driver is seen through glass at a glancing
    // angle: the three-draw figure keeps the face texture, which is what says
    // there is a person there, and drops the parts that do not survive a
    // windscreen.
    const std::vector<CharacterMesh::Part>& parts =
        character.farParts.empty() ? character.parts : character.farParts;
    for (const CharacterMesh::Part& part : parts)
    {
        SkinnedItem item;
        item.mesh     = part.mesh.get();
        item.material = part.material;
        item.world    = world3;
        item.bones    = &player.GetSkinTransforms();
        item.driver   = true;
        renderer_.submitSkinned(std::move(item));
    }
    (void)distance;
}

float CityScene::steeringSideFor(std::size_t vehicle) const
{
    // Which side of its own centre line this vehicle's drawn model puts the
    // steering wheel on. A loft has no wheel modelled, and this street is
    // right-hand traffic, so the default is a left-hand-drive car.
    if (vehicle < heroForVehicle_.size() && heroForVehicle_[vehicle] >= 0
        && static_cast<std::size_t>(heroForVehicle_[vehicle]) < heroVehicleMeshes_.size())
        return heroVehicleMeshes_[static_cast<std::size_t>(heroForVehicle_[vehicle])].steeringSide;
    return 1.0f;
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
        // Which of the character's gaits and stances this person uses, and
        // the clip's own length as the clock's unit, so a brisk walk and an
        // easy one each play at the pace their stride was built for.
        const auto clipNamed = [&](const char* const* names, int style) -> const Graphics::AnimationClip& {
            const auto found = character.skinning.AnimationClips.find(names[std::clamp(style, 0, 2)]);
            return found != character.skinning.AnimationClips.end()
                       ? found->second
                       : character.skinning.AnimationClips.at(names[0]);
        };
        if (person.waiting)
        {
            const auto& clip = clipNamed(CharacterFactory::Clips::kIdleNames, person.idleStyle);
            if (player.getCurrentClipProperty() != &clip) player.StartClip(clip);
            player.Update(System::TimeSpan::FromTicks(static_cast<std::int64_t>(
                              static_cast<double>(person.waitTime
                                                  + static_cast<float>(i) * 0.53f)
                              * 1.0e7)),
                          false, true);
        }
        else
        {
            const auto& clip = clipNamed(CharacterFactory::Clips::kWalkNames, person.walkStyle);
            if (player.getCurrentClipProperty() != &clip) player.StartClip(clip);
            const float cycles = PedestrianSystem::cyclesWalked(person);
            player.Update(System::TimeSpan::FromTicks(static_cast<std::int64_t>(
                              static_cast<double>(cycles) * static_cast<double>(clip.Duration.getTicksProperty()))),
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
        // The rigid stand-in, for the shadow pass only, and only near: see
        // RenderSettings::pedestrianShadowDistance.
        if (distance < settings.pedestrianShadowDistance)
            submitProp(character.shadowProxy, world, nullptr, true);
    }
}

std::vector<Vector3> CityScene::probePositions(const RenderSettings& settings)
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
    if (layout_.isSolid(point.X, point.Y, point.Z)) return true;
    // Cars are solid. They were not, and walking through a parked Astra is
    // the kind of thing that unmakes a street in one step. The body radius
    // is the walking camera's own, applied here rather than in the probe so
    // the standoff is a car's and not a wall's -- you can stand against a
    // building and not against a wing mirror.
    if (traffic_.blocks(point, kWalkerRadius)) return true;
    // And so is anything standing in the footway with mass: a column, a
    // bollard, a bin, a bench, a tree.
    for (const Obstacle& obstacle : obstacles_)
    {
        if (point.Y < obstacle.base || point.Y > obstacle.top) continue;
        const float dx = point.X - obstacle.centre.X;
        const float dz = point.Z - obstacle.centre.Y;
        const float reach = obstacle.radius + kWalkerRadius;
        if (dx * dx + dz * dz < reach * reach) return true;
    }
    return false;
}

void CityScene::addObstacle(const Matrix& at, float radius, float height, float base)
{
    const Vector3 origin = at.getTranslationProperty();
    obstacles_.push_back(Obstacle{Vector2(origin.X, origin.Z), radius, origin.Y + base,
                                  origin.Y + height});
}

void CityScene::addObstacles(const std::vector<Matrix>& at, float radius, float height, float base)
{
    obstacles_.reserve(obstacles_.size() + at.size());
    for (const Matrix& one : at) addObstacle(one, radius, height, base);
}

Vector3 CityScene::pushOutOfSolids(const Vector3& point) const
{
    // Only vehicles: a building does not drive into anybody, and a bollard
    // that somebody has walked into is a bollard they walked into on their
    // own. A car that has driven into the camera has to give it back.
    return traffic_.pushOut(point, kWalkerRadius);
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
    // Scanned props where the content root has them, generated ones where it
    // does not. A scanned hydrant, cabinet or bench is the single cheapest
    // realism there is at eye level: the eye judges a street by its furniture,
    // and a photogrammetry scan carries the dents, the paint runs and the
    // rust bloom that no generator here was ever going to.
    //
    // Poly Haven ships variants side by side in one file -- a fresh hydrant at
    // x = -0.3 and an aged one at x = +0.3 -- so one variant is taken by node
    // name and slid back onto the origin.
    PropMesh hydrant = importedProp("ph-fire-hydrant", Matrix::CreateTranslation(0.3f, 0.0f, 0.0f),
                                    [](const std::string& node) {
                                        return node.find("aged") == std::string::npos;
                                    });
    if (hydrant.empty())
        hydrant = makeProp("hydrant", [&](GeometryCollector& c) { props.hydrant(c); });
    // The street seating kit is a set of modules; the ones from x = -2.32 to 0
    // make one bench with a back and two arm rests, centred here.
    PropMesh bench = importedProp(
        "ph-street-seating", Matrix::CreateTranslation(1.16f, 0.0f, 0.0f),
        [](const std::string& node) {
            static const char* const kBenchNodes[] = {
                "legs_double", "legs_single", "crossbar", "back_support_r", "back_support_l",
                "arm_rest_01", "arm_rest_02", "seat", "seat_back", "suspended_support_01"};
            for (const char* wanted : kBenchNodes)
                if (node == wanted) return true;
            return false;
        });
    if (bench.empty()) bench = makeProp("bench", [&](GeometryCollector& c) { props.bench(c); });
    std::vector<PropMesh> cabinets;
    for (const char* asset : {"ph-utility-box-1", "ph-utility-box-2"})
    {
        PropMesh box = importedProp(asset);
        if (!box.empty()) cabinets.push_back(std::move(box));
    }
    if (cabinets.empty())
        cabinets.push_back(makeProp("cabinet", [&](GeometryCollector& c) {
            props.utilityCabinet(c, rng);
        }));
    const PropMesh bollard = makeProp("bollard", [&](GeometryCollector& c) { props.bollard(c); });
    // The litter bin: the scanned steel can where the content has it,
    // stood on its base and cut to a street bin's height, and the generated
    // one otherwise. The generated bin was the one object in the footway
    // viewpoint that was a plain grey cylinder among scans.
    // The scan ships two cans side by side, a clean one and a rusted one;
    // the clean one's four parts are the nodes without a suffix past .003.
    PropMesh bin = importedProp("ph-trash-can", Matrix::getIdentityProperty(),
                                [](const std::string& node) {
        return node == "Cylinder" || node == "Cylinder.001" || node == "Cylinder.002"
               || node == "Cylinder.003";
    });
    if (!bin.empty())
    {
        const Vector3 size = bin.bounds.Max - bin.bounds.Min;
        const float fit = 0.95f / std::max(size.Y, 0.05f);
        const Matrix stand = Matrix::CreateTranslation(-(bin.bounds.Min.X + bin.bounds.Max.X) * 0.5f,
                                                       -bin.bounds.Min.Y,
                                                       -(bin.bounds.Min.Z + bin.bounds.Max.Z) * 0.5f)
                             * Matrix::CreateScale(fit);
        for (PropMesh::Part& part : bin.parts) part.local = part.local * stand;
        bin.bounds = BoundingBox(Vector3(-size.X * 0.5f * fit, 0.0f, -size.Z * 0.5f * fit),
                                 Vector3(size.X * 0.5f * fit, size.Y * fit, size.Z * 0.5f * fit));
    }
    else
        bin = makeProp("litter-bin", [&](GeometryCollector& c) { props.litterBin(c); });
    // A refuse sack beside one bin in three, on collection day.
    const PropMesh sack = importedProp("ph-trash-bag");
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

    std::vector<Matrix> lampMainAt, lampSideAt, benchAt, bollardAt, binAt, hydrantAt, bikeAt,
        shelterAt, sackAt;
    std::vector<std::vector<Matrix>> cabinetAt(cabinets.size());

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
                if (!sack.empty() && rng.chance(0.34f))
                {
                    const Vector3 g = at(s + 2.45f, band + rng.signed_(0.08f));
                    sackAt.push_back(Place(g.X, g.Y, g.Z, rng.range(0.0f, MathHelper::TwoPi)));
                }
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
            cabinetAt[rng.index(cabinets.size())].push_back(Place(p.X, p.Y, p.Z, faceRoad));
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
    for (std::size_t i = 0; i < cabinets.size(); ++i)
        placeProp(cabinets[i], cabinetAt[i], "cabinet-" + std::to_string(i), cull * 0.7f, shade);
    placeProp(sack, sackAt, "refuse-sack", cull * 0.3f, shade * 0.3f);
    placeProp(bikeStand, bikeAt, "bike-stand", cull * 0.4f, shade * 0.5f);
    // What a walker cannot pass through. A column is a column whether or not
    // anything in the renderer knows it; before this the only solid on the
    // street was a building, and you could walk through a bench.
    addObstacles(lampMainAt, 0.10f, M::kLampMainHeight);
    addObstacles(lampSideAt, 0.09f, M::kLampSideHeight);
    addObstacles(benchAt, 0.62f, M::kBenchBackHeight);
    addObstacles(bollardAt, M::kBollardRadius + 0.03f, M::kBollardHeight);
    addObstacles(binAt, M::kBinRadius + 0.04f, M::kBinPostHeight + M::kBinHeight);
    addObstacles(hydrantAt, M::kHydrantRadius + 0.05f, M::kHydrantHeight);
    for (const std::vector<Matrix>& at : cabinetAt)
        addObstacles(at, M::kCabinetWidth * 0.5f, M::kCabinetHeight);
    addObstacles(bikeAt, 0.42f, M::kBikeRackHeight);
    for (std::size_t i = 0; i < bikes.size(); ++i)
        placeProp(bikes[i], bikeOnStand[i], "bicycle", cull * 0.35f, shade * 0.4f);
    placeProp(shelter, shelterAt, "bus-shelter", cull, shade);
    addObstacles(shelterAt, 1.05f, 2.4f);
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
    farHeroTrees_.clear();
    farHeroScale_.clear();
    const PropMesh scruff = makeProp("ground-scruff", [&](GeometryCollector& c) {
        props.groundScruff(c, rng, 0.72f, 7);
    });
    const PropMesh grate = makeProp("tree-grate", [&](GeometryCollector& c) {
        props.treeGrate(c);
    });
    // Scanned planter boxes with scanned shrubs in them, where the content
    // root has them; the generated trough with its clipped sphere otherwise.
    // Two shrub clumps sit in the soil a hand below the rim.
    std::vector<PropMesh> planters;
    {
        const PropMesh shrubs = importedProp(
            "ph-shrub-3", Matrix::CreateTranslation(-0.23f, 0.29f, 0.0f),
            [](const std::string& node) { return node == "shrub_03_a" || node == "shrub_03_b"; });
        for (const char* asset : {"ph-planter-1", "ph-planter-2"})
        {
            PropMesh box = importedProp(asset);
            if (box.empty()) continue;
            box.append(shrubs, Matrix::getIdentityProperty());
            planters.push_back(std::move(box));
        }
        if (planters.empty())
            planters.push_back(makeProp("planter", [&](GeometryCollector& c) {
                props.planter(c, rng);
            }));
    }
    // The hero tree: a scanned small tree cut to a street level of detail (see
    // scripts/blender-tree-lod.py), stood at the pits nearest the showcase
    // viewpoints -- the west footway north of the junction -- and scaled up
    // from its 4.6 m to a street tree's height. The generated trees stand
    // everywhere else, so the hero corridor gets the leaf-level realism and
    // the rest of the district stays cheap.
    // Three scanned species now, each with its far copy: the small tree from
    // the third pass, a broad multi-stemmed island tree, and a mature
    // jacaranda for the tallest pits. One species scaled with variation reads
    // as a planted row; three species read as a street planted over decades,
    // which is what the generated trees' six variants were trying to say
    // and could not.
    struct HeroSpecies
    {
        PropMesh near, far;
        float scaleMin, scaleMax;
        float lod;
        const char* name;
    };
    HeroSpecies heroSpecies[] = {
        {importedProp("ph-tree-small"), importedProp("ph-tree-small-far"), 1.55f, 1.85f, 55.0f,
         "tree-hero"},
        {importedProp("ph-island-tree-02"), importedProp("ph-island-tree-02-far"), 1.85f, 2.15f,
         44.0f, "tree-hero-island"},
        {importedProp("ph-jacaranda-tree"), importedProp("ph-jacaranda-tree-far"), 0.42f, 0.50f,
         40.0f, "tree-hero-jacaranda"},
    };
    const bool haveHero = !heroSpecies[0].near.empty();
    for (const HeroSpecies& kind : heroSpecies)
    {
        farHeroTrees_.push_back(kind.far);
        farHeroScale_.push_back((kind.scaleMin + kind.scaleMax) * 0.5f);
    }
    // Every pit on the main street gets a scanned tree, not just the ones in
    // the hero corridor. The generated trees were a different green and a
    // different silhouette -- a ball on a stick beside a scanned crown -- and
    // a row of them starting at eighty metres was the most legible thing in
    // the frame saying where the modelling stopped. They stay as the fallback
    // for a tree that has fetched no scans, and nowhere else.
    //
    // Two rings, for the reason the parked cars have four: a level of detail
    // is decided once per instance group, so one tree at three metres would
    // otherwise draw every tree on the street at its full sixty-seven
    // thousand triangles. Ring 0 is the hero corridor and carries the near
    // mesh with its far copy behind it; ring 1 is everything beyond and is
    // registered with the far mesh alone, since nothing gets close to it.
    auto heroPit = [](const Vector3& p) {
        return std::fabs(p.X) < M::kMainStreetHalfWidth + 1.0f;
    };
    auto pitRing = [](const Vector3& p) { return p.Z > -70.0f && p.Z < 84.0f ? 0 : 1; };
    // Which species a pit gets: the west footway north of the junction, where
    // the close viewpoints stand, keeps the small tree with an island tree
    // every third pit; the east footway alternates the two bigger species,
    // because across the carriageway a tall crown is what says "mature
    // street"; south of the junction the two smaller species alternate.
    auto speciesFor = [&](const Vector3& p, int ordinal) -> int {
        const bool west = p.X < 0.0f;
        if (p.Z > 0.0f && west) return ordinal % 3 == 2 ? 1 : 0;
        if (p.Z > 0.0f) return ordinal % 2 == 0 ? 2 : 1;
        return ordinal % 2 == 0 ? 1 : 0;
    };
    auto speciesReady = [&](int species) {
        return species < 3 && !heroSpecies[species].near.empty();
    };

    std::vector<std::vector<Matrix>> treeAt(kTreeVariants);
    std::vector<std::vector<Matrix>> planterAt(planters.size());
    std::vector<Matrix> grateAt, scruffAt;
    std::vector<Matrix> heroAt[3][2];
    int heroOrdinal[4] = {0, 0, 0, 0};

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
                const Vector3 p = at(s, FurnitureBand(run) + 0.15f);
                // The gaps where one died: decided from the pit's own position
                // rather than from the draw sequence, so adding a draw to the
                // loop cannot move every gap on the street -- which is how a
                // run of four empty pits once landed on the hero corridor and
                // the tree viewpoint was aimed at a facade. Nine in ten
                // planted: a street is replanted when a tree dies, mostly.
                const std::uint32_t pit = Noise::hash2(static_cast<int>(std::lround(p.X * 10.0f)),
                                                       static_cast<int>(std::lround(p.Z * 10.0f)),
                                                       settings.seed);
                if (static_cast<float>(pit & 0xFFFFu) / 65536.0f > 0.90f) continue;
                const int variant = rng.intRange(0, kTreeVariants - 1);
                const float yaw = rng.range(0.0f, MathHelper::TwoPi);
                const float scale = rng.range(0.0f, 1.0f);
                if (haveHero && heroPit(p))
                {
                    const int quadrant = (p.X < 0.0f ? 0 : 1) + (p.Z > 0.0f ? 0 : 2);
                    int species = speciesFor(p, heroOrdinal[quadrant]++);
                    if (!speciesReady(species)) species = 0;
                    const HeroSpecies& kind = heroSpecies[species];
                    heroAt[species][pitRing(p)].push_back(
                        Matrix::CreateScale(kind.scaleMin + (kind.scaleMax - kind.scaleMin) * scale)
                        * Place(p.X, p.Y, p.Z, yaw));
                }
                else
                    treeAt[static_cast<std::size_t>(variant)].push_back(Place(p.X, p.Y, p.Z, yaw));
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
            planterAt[rng.index(planters.size())].push_back(Place(p.X, p.Y, p.Z, YawTowards(kerb)));
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
    // The same tree at a quarter of the geometry past 55 m -- cut by the same
    // script from the same source, so the crown keeps its shape across the
    // switch rather than popping to a different tree.
    int planted[3] = {0, 0, 0};
    for (int species = 0; species < 3; ++species)
    {
        const HeroSpecies& kind = heroSpecies[species];
        placeProp(kind.near, heroAt[species][0], kind.name, cull, shade, /*castsShadow=*/true,
                  kind.far.empty() ? nullptr : &kind.far, kind.lod);
        // Beyond the corridor: the far copy and nothing else. It is the mesh
        // that would have been chosen there anyway, and registering it on its
        // own means a camera in the corridor cannot promote it.
        if (!kind.far.empty())
            placeProp(kind.far, heroAt[species][1], std::string(kind.name) + "-far", cull,
                      shade * 0.5f, /*castsShadow=*/true);
        planted[species] = static_cast<int>(heroAt[species][0].size()
                                            + heroAt[species][1].size());
        buildStats_.trees += planted[species];
    }
    CNA::Logger::Info("cna-street: scanned trees -- " + std::to_string(planted[0])
                      + " small, " + std::to_string(planted[1]) + " island, "
                      + std::to_string(planted[2]) + " jacaranda");
    placeProp(grate, grateAt, "tree-grate", cull * 0.35f, 0.0f, false);
    for (std::size_t i = 0; i < planters.size(); ++i)
        placeProp(planters[i], planterAt[i], "planter-" + std::to_string(i), cull * 0.5f,
                  shade * 0.6f);
    // A trunk stops a walker; a crown does not, and a solid the width of a
    // crown would close the footway. Up to the clear stem only.
    for (const Vector3& pit : treePositions_)
        obstacles_.push_back(Obstacle{Vector2(pit.X, pit.Z), M::kTreeTrunkRadius + 0.10f, pit.Y,
                                      pit.Y + M::kTreeClearStem});
    for (const std::vector<Matrix>& at : planterAt) addObstacles(at, 0.45f, 0.75f);
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
    // Right-hand traffic, so each approach's near kerb is the one on its
    // right -- which, with the left of a heading (ux,uz) at (uz,-ux), is
    // -X for the northbound carriageway and not +X. These four followed the
    // lanes when the lanes were laid out the other way round; they follow
    // them still. `traffic_system_tests` checks that each head stands on the
    // kerb its own approach passes.
    const Approach approaches[] = {
        // Northbound (+Z): the -X kerb, stopping south of the junction.
        {Vector2(-mainKerb - 0.60f, -mainStop), Vector2(-mainKerb - 0.60f, 6.60f),
         Vector2(-mainKerb - 0.75f, -mainStop - 1.6f), Vector2(0.0f, -1.0f),
         Vector2(1.0f, 0.0f), SignalAxis::Main, true},
        // Southbound (-Z): the +X kerb, stopping north of the junction.
        {Vector2(mainKerb + 0.60f, mainStop), Vector2(mainKerb + 0.60f, -6.60f),
         Vector2(mainKerb + 0.75f, mainStop + 1.6f), Vector2(0.0f, 1.0f),
         Vector2(-1.0f, 0.0f), SignalAxis::Main, true},
        // Eastbound (+X): the +Z kerb, stopping west of the junction.
        {Vector2(-sideStop, sideKerb + 0.60f), Vector2(11.6f, sideKerb + 0.60f),
         Vector2(-sideStop, sideKerb + 0.60f), Vector2(-1.0f, 0.0f),
         Vector2(0.0f, -1.0f), SignalAxis::Side, false},
        // Westbound (-X): the -Z kerb, stopping east of the junction.
        {Vector2(sideStop, -sideKerb - 0.60f), Vector2(-11.6f, -sideKerb - 0.60f),
         Vector2(sideStop, -sideKerb - 0.60f), Vector2(1.0f, 0.0f),
         Vector2(0.0f, 1.0f), SignalAxis::Side, false},
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
    addObstacles(postAt, M::kSignalPoleRadius + 0.04f, M::kSignalPoleHeight);
    addObstacles(pedPostAt, M::kSignalPoleRadius + 0.04f, M::kPedSignalMountHeight);
    addObstacles(mastAt, 0.11f, M::kSignalMastHeight);
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
    {
        placeProp(signMeshes[i], signAt[i], "sign-" + std::to_string(i), cull * 0.6f, shade * 0.6f);
        addObstacles(signAt[i], M::kSignPostRadius + 0.04f, M::kSignMountHeight);
    }

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
            text = anchor.plotIndex == heroPlot_ ? BuildingBuilder::heroShopName()
                                                 : plot.shopName;
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

void CityScene::buildDressing(Rng& rng, const RenderSettings& settings)
{
    // The scanned things that make a street look used rather than built:
    // manhole covers on the crown of the road, a car under a cover in one bay,
    // a cafe's tables and A-board out on the footway, a crate and a carton by
    // a shop door. None of them is structural and every one of them is a
    // photogrammetry scan, which is what lets them survive the close-ups the
    // showcase viewpoints take.
    const float cull  = settings.propCullDistance;
    const float shade = settings.propShadowDistance;
    const float ground = M::kCurbHeight;

    // --- manhole covers ------------------------------------------------------
    const PropMesh manhole = importedProp("ph-manhole");
    if (!manhole.empty())
    {
        std::vector<Matrix> at;
        for (const Vector3& p : manholes_)
            at.push_back(Place(p.X, 0.004f, p.Z, rng.range(0.0f, MathHelper::TwoPi)));
        placeProp(manhole, at, "manhole-cover", cull * 0.3f, 0.0f, false);
    }

    // --- the hero cars --------------------------------------------------------
    buildHeroVehicles(rng, settings);

    // --- a covered car in a hero bay ----------------------------------------
    // The west parking lane between the shop window and the kerbside
    // viewpoints, in the first bay no parked car took. A car under a cover is
    // the one vehicle in the scene that is a scan rather than a loft, and it
    // reads as one from the pavement.
    const PropMesh covered = importedProp("ph-covered-car");
    if (!covered.empty())
    {
        const float x = -(M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f);
        const float pitch = M::kParkingBayLength + 0.65f;
        const float from = M::kSideStreetHalfWidth + 14.0f;
        float best = 0.0f, bestScore = 1e30f;
        for (float z = from + pitch * 0.5f; z < 70.0f; z += pitch)
        {
            bool free = true;
            for (const Vehicle& other : traffic_.vehicles())
                if (other.parked && other.parkedAt.X < 0.0f
                    && std::fabs(other.parkedAt.Y - z) < pitch * 0.95f)
                    free = false;
            if (!free) continue;
            const float score = std::fabs(z - 44.0f);
            if (score < bestScore) { bestScore = score; best = z; }
        }
        if (bestScore < 1e29f)
        {
            placeProp(covered, {Place(x + 0.05f, 0.0f, best, MathHelper::Pi + 0.02f)},
                      "covered-car", cull * 0.6f, shade);
            CNA::Logger::Info("cna-street: covered car in the bay at z = " + std::to_string(best));
        }
    }

    // --- pavement cafes ------------------------------------------------------
    // A table and two chairs, and an A-board, on the footway outside each
    // bakery: against the building line, out of the walking line, turned a
    // little. The window display's transform gives the shop's position and
    // which way it faces; the footway is a metre and a half out from it.
    const PropMesh cafeSet = importedProp("ph-cafe-set");
    const PropMesh board   = importedProp("ph-chalkboard");
    std::vector<Matrix> cafeAt, boardAt;
    int cafes = 0;
    for (const ShopDisplay& display : displays_)
    {
        if (display.kind != ShopKind::Bakery) continue;
        const Vector3 out = Vector3::Normalize(Vector3::TransformNormal(Vector3::UnitZ,
                                                                        display.stand));
        const Vector3 along(-out.Z, 0.0f, out.X);
        const Vector3 base = display.stand.getTranslationProperty();
        // Only where the footway is wide enough for a table and a walking
        // line beside it, which is the main street.
        if (std::fabs(out.X) < 0.5f) continue;
        const float faceOut = std::atan2(out.X, out.Z);
        const Vector3 t = base + out * 1.55f + along * 0.9f;
        cafeAt.push_back(Place(t.X, ground, t.Z, faceOut + rng.signed_(0.5f)));
        if (rng.chance(0.6f))
        {
            const Vector3 t2 = base + out * 1.55f - along * 1.1f;
            cafeAt.push_back(Place(t2.X, ground, t2.Z, faceOut + rng.signed_(0.5f)));
        }
        const Vector3 b = base + out * 1.0f - along * 2.6f;
        boardAt.push_back(Place(b.X, ground, b.Z, faceOut + rng.signed_(0.25f)));
        ++cafes;
    }
    placeProp(cafeSet, cafeAt, "cafe-seating", cull * 0.4f, shade * 0.5f);
    placeProp(board, boardAt, "a-board", cull * 0.35f, shade * 0.4f);

    // --- deliveries ----------------------------------------------------------
    // A crate and a carton beside one shop door in five, against the wall.
    // The house-number anchors are beside the doors, so a door is where one
    // of them is.
    const PropMesh crate  = importedProp("ph-crate");
    const PropMesh carton = importedProp("ph-cardboard-box");
    std::vector<Matrix> crateAt, cartonAt;
    for (const FacadeAnchor& anchor : anchors_)
    {
        if (anchor.kind != FacadeAnchor::Kind::HouseNumber) continue;
        if (!rng.chance(0.2f)) continue;
        const Vector3 out = Vector3::Normalize(Vector3(anchor.normal.X, 0.0f, anchor.normal.Z));
        const Vector3 along(-out.Z, 0.0f, out.X);
        const Vector3 p = Vector3(anchor.position.X, ground, anchor.position.Z) + out * 0.34f
                          + along * rng.range(0.9f, 1.4f);
        const float yaw = std::atan2(out.X, out.Z) + rng.signed_(0.3f);
        if (rng.chance(0.5f))
        {
            crateAt.push_back(Place(p.X, p.Y, p.Z, yaw));
            if (rng.chance(0.5f))
                crateAt.push_back(Place(p.X, p.Y + 0.31f, p.Z, yaw + rng.signed_(0.2f)));
        }
        else
        {
            cartonAt.push_back(Place(p.X, p.Y, p.Z, yaw));
        }
    }
    placeProp(crate, crateAt, "crate", cull * 0.3f, shade * 0.3f);
    placeProp(carton, cartonAt, "carton", cull * 0.3f, shade * 0.3f);
    // A sack truck left against the wall by the first delivery in the hero
    // corridor: the tool the crates arrived on.
    const PropMesh truck = importedProp("ph-hand-truck");
    if (!truck.empty() && !crateAt.empty())
    {
        std::vector<Matrix> truckAt;
        for (const Matrix& at : crateAt)
        {
            const Vector3 p = at.getTranslationProperty();
            if (p.X > 0.0f || p.Z < 18.0f || p.Z > 68.0f || p.Y > ground + 0.1f) continue;
            truckAt.push_back(Matrix::CreateTranslation(0.0f, 0.0f, -0.9f) * at);
            break;
        }
        placeProp(truck, truckAt, "hand-truck", cull * 0.3f, shade * 0.3f);
    }

    // --- on the walls --------------------------------------------------------
    // A security camera over one shop door in two, and a condenser unit at
    // the anchors the flat elevations left beside their upper windows. Both
    // are scans, both are small, and both are the kind of thing a viewer
    // never notices and always misses.
    const PropMesh camera = importedProp("ph-security-camera-01");
    const PropMesh aircon = importedProp("ph-exterior-aircon-unit");
    std::vector<Matrix> cameraAt, airconAt;
    for (const FacadeAnchor& anchor : anchors_)
    {
        const Vector3 out = Vector3::Normalize(Vector3(anchor.normal.X, 0.0f, anchor.normal.Z));
        const float yaw = std::atan2(out.X, out.Z);
        if (anchor.kind == FacadeAnchor::Kind::DoorHead && !camera.empty())
        {
            if (!rng.chance(0.5f)) continue;
            // The scan stands upright on its base; hung under the door head
            // it is turned to look down the footway, its mount against the
            // wall.
            const float across = camera.bounds.Max.X - camera.bounds.Min.X;
            const float fit = 0.24f / std::max(across, 0.05f);
            const Vector3 p = Vector3(anchor.position.X, anchor.position.Y + 0.02f,
                                      anchor.position.Z) + out * 0.02f;
            cameraAt.push_back(Matrix::CreateScale(fit)
                               * Matrix::CreateRotationX(-0.35f)
                               * Place(p.X, p.Y, p.Z, yaw + (rng.chance(0.5f) ? 0.6f : -0.6f)));
        }
        else if (anchor.kind == FacadeAnchor::Kind::AirCon && !aircon.empty())
        {
            const float across = aircon.bounds.Max.X - aircon.bounds.Min.X;
            const float fit = 0.82f / std::max(across, 0.05f);
            const float depth = (aircon.bounds.Max.Z - aircon.bounds.Min.Z) * fit;
            const Vector3 p = Vector3(anchor.position.X, anchor.position.Y, anchor.position.Z)
                              + out * (depth * 0.5f + 0.02f);
            airconAt.push_back(Matrix::CreateScale(fit)
                               * Matrix::CreateTranslation(-(aircon.bounds.Min.X + aircon.bounds.Max.X) * 0.5f * fit,
                                                           -aircon.bounds.Min.Y * fit,
                                                           -(aircon.bounds.Min.Z + aircon.bounds.Max.Z) * 0.5f * fit)
                               * Place(p.X, p.Y, p.Z, yaw));
        }
    }
    placeProp(camera, cameraAt, "security-camera", cull * 0.25f, 0.0f, false);
    placeProp(aircon, airconAt, "aircon-unit", cull * 0.32f, shade * 0.4f);

    CNA::Logger::Info("cna-street: dressing -- " + std::to_string(manholes_.size())
                      + " manhole covers, " + std::to_string(cafes) + " pavement cafes, "
                      + std::to_string(crateAt.size() + cartonAt.size()) + " deliveries"
                      + (covered.empty() ? "" : ", one covered car"));
}

void CityScene::buildHeroVehicles(Rng& rng, const RenderSettings& settings)
{
    vehicleReplaced_.assign(traffic_.vehicles().size(), false);
    heroForVehicle_.assign(traffic_.vehicles().size(), -1);
    heroVehicles_  = 0;
    movingHeroes_  = 0;
    heroVehicleMeshes_.clear();

    // Eight authored cars under CC-BY, each normalised by
    // scripts/blender-vehicles.py to face +Z on y = 0 at its real length, with
    // its wheels split into nodes of their own and a far level of detail
    // beside it. The mix is a continental street's: two superminis, a
    // hatchback, two saloons, an estate, a small car and a van. The class
    // beside each is the loft it stands in for on the move, so a van is
    // drawn where the simulation put a van.
    struct Source { const char* asset; VehicleType nearest; };
    static const Source kHeroCars[] = {
        {"car-opel-astra-gtc", VehicleType::Hatchback},  {"car-fiat-punto-gt", VehicleType::CityCar},
        {"car-renault-logan", VehicleType::Saloon},      {"car-vaz-2104", VehicleType::Estate},
        {"car-honda-civic-ek", VehicleType::Hatchback},  {"car-small-price-car", VehicleType::Saloon},
        {"car-mini-cooper-s", VehicleType::CityCar},     {"car-mercedes-sprinter", VehicleType::Van},
    };
    static const char* const kWheelNodes[] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    for (const Source& source : kHeroCars)
    {
        HeroVehicleMesh hero;
        hero.name    = source.asset;
        hero.nearest = source.nearest;
        hero.whole   = importedProp(source.asset);
        if (hero.whole.empty()) continue;
        hero.far    = importedProp(std::string(source.asset) + "-far");
        hero.length = hero.whole.bounds.Max.Z - hero.whole.bounds.Min.Z;
        hero.width  = hero.whole.bounds.Max.X - hero.whole.bounds.Min.X;
        hero.height = hero.whole.bounds.Max.Y - hero.whole.bounds.Min.Y;
        // The body is every node that is not a wheel; a file from before the
        // wheels were split has one node and no wheels, and is parked only.
        hero.body = importedProp(source.asset, Matrix::getIdentityProperty(),
                                 [](const std::string& node) {
            return node.rfind("wheel_", 0) != 0;
        });
        float radius = 0.0f;
        for (const char* wheelNode : kWheelNodes)
        {
            // The importer names a part after its node with the primitive's
            // index appended -- "wheel_fl_0", "wheel_fl_1" -- so a prefix is
            // the match.
            PropMesh mesh = importedProp(source.asset, Matrix::getIdentityProperty(),
                                         [&](const std::string& node) {
                return node.rfind(wheelNode, 0) == 0;
            });
            if (mesh.empty()) break;
            HeroVehicleMesh::Wheel wheel;
            // The node's translation is the axle; the mesh is centred on it,
            // so with the placement stripped the part turns about its own
            // origin and the scene puts it back where the axle is.
            wheel.centre = mesh.parts.front().local.getTranslationProperty();

            // ...but only the parts of it that are actually round. A wheel
            // node as `scripts/blender-vehicles.py` leaves it is everything
            // the splitter found near that corner of the car, and on four of
            // these eight models that is more than the rim and the tyre: the
            // Mini's rear nodes carry an arch liner 20 cm above the axle, the
            // small price car's carry a mudflap 25 cm above it, the Logan's a
            // suspension arm, the VAZ's a brake caliper. Rolled with the
            // wheel every one of them orbits the axle once a revolution --
            // and a car whose arch liner goes round with the tyre is a car
            // with wobbling wheels, which is what this looked like.
            //
            // A part that turns with the road is a surface of revolution
            // about the axle: its own bounds are centred on the axle line and
            // as tall as they are long. Anything else is bolted to the car
            // and goes in the hub, which steers but does not roll.
            Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
            for (const PropMesh::Part& part : mesh.parts) Grow(lo, hi, part.mesh->bounds());
            const float diameter = std::max({hi.Y - lo.Y, hi.Z - lo.Z, 0.05f});

            PropMesh rolls, hub;
            Vector3 rollLo(1e30f, 1e30f, 1e30f), rollHi(-1e30f, -1e30f, -1e30f);
            for (PropMesh::Part& part : mesh.parts)
            {
                const BoundingBox& box = part.mesh->bounds();
                const Vector3 centre = (box.Min + box.Max) * 0.5f;
                const float spanY = box.Max.Y - box.Min.Y;
                const float spanZ = box.Max.Z - box.Min.Z;
                const bool onAxis = std::abs(centre.Y) < 0.08f * diameter
                                    && std::abs(centre.Z) < 0.08f * diameter;
                const bool round  = std::abs(spanY - spanZ)
                                    < 0.25f * std::max({spanY, spanZ, 1e-3f});
                part.local = Matrix::getIdentityProperty();
                if (onAxis && round)
                {
                    Grow(rollLo, rollHi, box);
                    rolls.parts.push_back(part);
                }
                else
                {
                    hub.parts.push_back(part);
                }
            }
            // A node with nothing round in it is a wheel the rule failed to
            // recognise rather than a car with no wheels: roll the lot, which
            // is what this did before, rather than draw a car whose wheels
            // never turn.
            if (rolls.parts.empty())
            {
                rolls.parts = std::move(hub.parts);
                hub.parts.clear();
                rollLo = lo; rollHi = hi;
            }
            rolls.bounds = BoundingBox(rollLo, rollHi);
            hub.bounds   = BoundingBox(lo, hi);
            mesh.bounds  = BoundingBox(lo, hi);
            wheel.mesh   = std::move(rolls);
            wheel.hub    = std::move(hub);
            // The rolling radius is the rolling part's, not the node's: a
            // mudflap in the bounds makes the tyre look bigger than it is and
            // the odometer then turns it too slowly, which is a wheel that
            // skates.
            radius       = std::max(radius, (rollHi.Y - rollLo.Y) * 0.5f);
            hero.wheels.push_back(std::move(wheel));
        }
        if (hero.wheels.size() == 4)
        {
            // The front pair steers: the two nearer the nose.
            float noseZ = -1e30f;
            for (const HeroVehicleMesh::Wheel& wheel : hero.wheels) noseZ = std::max(noseZ, wheel.centre.Z);
            for (HeroVehicleMesh::Wheel& wheel : hero.wheels) wheel.steered = wheel.centre.Z > noseZ - 0.6f;
            hero.wheelRadius = std::max(radius, 0.2f);
        }
        else
        {
            hero.wheels.clear();
        }
        {
            std::size_t rolling = 0, bolted = 0;
            for (const HeroVehicleMesh::Wheel& wheel : hero.wheels)
            {
                rolling += wheel.mesh.parts.size();
                bolted  += wheel.hub.parts.size();
            }
            char line[256];
            std::snprintf(line, sizeof(line),
                          "cna-street: %s  %.2f x %.2f x %.2f m, wheel r %.3f, "
                          "%zu wheels, %zu rolling part(s) and %zu bolted to the car",
                          hero.name.c_str(), hero.length, hero.width, hero.height,
                          hero.wheelRadius, hero.wheels.size(), rolling, bolted);
            CNA::Logger::Info(line);
        }
        heroVehicleMeshes_.push_back(std::move(hero));
    }
    if (heroVehicleMeshes_.empty()) return;
    std::vector<HeroVehicleMesh>& heroes = heroVehicleMeshes_;

    // Every parking bay a camera or a walker can get to, which is both
    // parking lanes of the main street for ninety-five metres either side of
    // the junction -- not, as it was, one stretch north of it.
    //
    // The rule this follows is that the weakest prominent car sets the
    // perceived quality of all of them: a lofted crossover parked between two
    // authored ones does not read as "a cheaper car", it reads as the moment
    // the rendering stops. Repeating eight good models is the lesser fault,
    // and the dealing below never puts the same one in two neighbouring bays.
    //
    // It costs almost nothing. The copies are instances of meshes already
    // uploaded, so more of them is more matrices and no more draw calls, and
    // past forty-five metres each one is its welded far copy at a tenth of
    // the triangles.
    const float reach = 126.0f;
    const std::vector<Vehicle>& fleet = traffic_.vehicles();
    std::vector<std::size_t> bays;
    for (std::size_t i = 0; i < fleet.size(); ++i)
    {
        const Vehicle& vehicle = fleet[i];
        if (!vehicle.parked) continue;
        if (std::fabs(vehicle.parkedAt.Y) > reach) continue;
        if (std::fabs(vehicle.parkedAt.X) > M::kMainCarriagewayWidth) continue;
        bays.push_back(i);
    }
    // Nearest the junction first, so the models the closest viewpoints stand
    // in front of are dealt before the deck starts repeating.
    std::sort(bays.begin(), bays.end(), [&](std::size_t a, std::size_t b) {
        return std::fabs(fleet[a].parkedAt.Y) < std::fabs(fleet[b].parkedAt.Y);
    });

    // Deal the models out in a seeded order rather than in list order, so the
    // two lanes do not read as the same eight cars twice, and never the same
    // model in two neighbouring bays.
    std::vector<std::size_t> deck;
    for (std::size_t h = 0; h < heroes.size(); ++h) deck.push_back(h);
    for (std::size_t i = deck.size(); i > 1; --i)
        std::swap(deck[i - 1], deck[rng.index(i)]);

    // One placement list per model *per ring of the street*, because a level
    // of detail is chosen once for a whole instance group -- one car three
    // metres away would otherwise draw every other copy of that model, the
    // length of the street, at its full hundred and fifty thousand
    // triangles. Four rings of about thirty-four metres: the camera upgrades
    // the ring it is standing in and no other.
    constexpr int kRings = 4;
    constexpr float kRingDepth = 34.0f;
    std::vector<std::vector<Matrix>> parkedAt(heroes.size() * kRings);
    const auto ringOf = [&](float z) {
        return std::min(kRings - 1, static_cast<int>(std::fabs(z) / kRingDepth));
    };
    std::size_t dealt = 0;
    std::size_t lastOnSide[4] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    for (const std::size_t index : bays)
    {
        const Vehicle& vehicle = fleet[index];
        // Four runs of bays, not two: the two parking lanes on each half of
        // the street. "Never the same model twice running" has to mean along
        // a run somebody walks down, and the two halves are dealt
        // interleaved now that the bays are sorted by distance.
        const bool west = vehicle.parkedAt.X < 0.0f;
        const int side = (west ? 0 : 1) + (vehicle.parkedAt.Y < 0.0f ? 0 : 2);
        // Room for it: the bay pitch minus both neighbours' errors, or the
        // gap to the nearest other parked car in the same lane.
        float room = 1e30f;
        for (std::size_t j = 0; j < fleet.size(); ++j)
        {
            if (j == index || !fleet[j].parked) continue;
            if ((fleet[j].parkedAt.X < 0.0f) != west) continue;
            const float gap = std::fabs(fleet[j].parkedAt.Y - vehicle.parkedAt.Y);
            room = std::min(room, gap * 2.0f - fleet[j].length);
        }
        std::size_t pick = SIZE_MAX;
        for (std::size_t attempt = 0; attempt < heroes.size(); ++attempt)
        {
            const std::size_t candidate = deck[(dealt + attempt) % deck.size()];
            if (candidate == lastOnSide[side]) continue;
            if (heroes[candidate].length + 0.45f > room) continue;
            pick = candidate;
            dealt += attempt + 1;
            break;
        }
        if (pick == SIZE_MAX) continue;
        lastOnSide[side] = pick;
        parkedAt[pick * kRings + static_cast<std::size_t>(ringOf(vehicle.parkedAt.Y))]
            .push_back(vehicle.transform(traffic_.lanes()));
        vehicleReplaced_[index] = true;
        // The loft is not drawn any more but it is still the solid the
        // walking camera meets, so it takes the size of the car that stands
        // in its bay.
        traffic_.setVehicleLength(index, heroes[pick].length);
        traffic_.setVehicleSize(index, heroes[pick].width, heroes[pick].height);
        ++heroVehicles_;
    }

    const float cull  = settings.propCullDistance;
    const float shade = settings.propShadowDistance;
    for (std::size_t h = 0; h < heroes.size(); ++h)
        for (int ring = 0; ring < kRings; ++ring)
        {
            std::vector<Matrix>& at = parkedAt[h * kRings + static_cast<std::size_t>(ring)];
            if (at.empty()) continue;
            // The near model's own shadow, off: an authored car's far copy
            // is a differently-merged model rather than the same materials at
            // fewer triangles (seven parts against three, on the Civic), so
            // placeProp's index-matched LOD swap cannot use it for the
            // *drawn* body -- but a shadow caster does not draw materials,
            // only positions, and the far copy's silhouette is the same car.
            // `--frames` measured the parked fleet's own near-mesh shadows at
            // over a million triangles a frame; the far copy underneath is a
            // few hundred thousand for the same shape.
            placeProp(heroes[h].whole, at,
                      "hero-" + heroes[h].name + "-r" + std::to_string(ring), cull * 0.75f, shade,
                      /*castsShadow=*/false, heroes[h].far.empty() ? nullptr : &heroes[h].far,
                      45.0f);
            if (!heroes[h].far.empty())
                placeShadowProxy(heroes[h].far, at,
                                 "hero-" + heroes[h].name + "-r" + std::to_string(ring)
                                     + "-shadow",
                                 shade);
        }

    // --- the moving traffic --------------------------------------------------
    // Every moving loft is drawn as an authored car of the nearest class: a
    // van where the simulation put a van, a small car where it put a small
    // car, and a hatchback or a saloon for the classes the eight do not
    // cover. Dealt in a seeded order per class so the same model does not
    // follow itself down a lane, and the simulation is told the model's
    // length so the queue at the lights is spaced for the car that is drawn.
    std::vector<std::vector<std::size_t>> byClass(static_cast<std::size_t>(VehicleType::Count));
    for (std::size_t h = 0; h < heroes.size(); ++h)
        if (heroes[h].drivable())
            byClass[static_cast<std::size_t>(heroes[h].nearest)].push_back(h);
    auto candidatesFor = [&](VehicleType type) -> const std::vector<std::size_t>& {
        static const std::vector<std::size_t> none;
        if (!byClass[static_cast<std::size_t>(type)].empty()) return byClass[static_cast<std::size_t>(type)];
        // The classes the eight do not cover borrow the nearest that they do.
        static const VehicleType kFallback[] = {VehicleType::Hatchback, VehicleType::Saloon,
                                                VehicleType::CityCar, VehicleType::Estate};
        for (const VehicleType fallback : kFallback)
            if (!byClass[static_cast<std::size_t>(fallback)].empty())
                return byClass[static_cast<std::size_t>(fallback)];
        return none;
    };
    std::vector<std::size_t> dealtPerClass(static_cast<std::size_t>(VehicleType::Count), 0);
    int lastOnLane[8];
    std::fill(std::begin(lastOnLane), std::end(lastOnLane), -1);
    for (std::size_t v = 0; v < fleet.size(); ++v)
    {
        const Vehicle& vehicle = fleet[v];
        if (vehicle.parked) continue;
        const std::vector<std::size_t>& candidates = candidatesFor(vehicle.type);
        if (candidates.empty()) continue;
        std::size_t& cursor = dealtPerClass[static_cast<std::size_t>(vehicle.type)];
        int pick = -1;
        for (std::size_t attempt = 0; attempt < candidates.size(); ++attempt)
        {
            const int candidate = static_cast<int>(candidates[(cursor + attempt) % candidates.size()]);
            const int lane = std::clamp(vehicle.lane, 0, 7);
            if (candidate == lastOnLane[lane] && candidates.size() > 1) continue;
            pick = candidate;
            cursor += attempt + 1 + rng.index(2);
            lastOnLane[lane] = candidate;
            break;
        }
        if (pick < 0) continue;
        heroForVehicle_[v] = pick;
        traffic_.setVehicleLength(v, heroes[static_cast<std::size_t>(pick)].length);
        traffic_.setVehicleSize(v, heroes[static_cast<std::size_t>(pick)].width,
                                heroes[static_cast<std::size_t>(pick)].height);
        ++movingHeroes_;
    }

    int drivable = 0;
    for (const HeroVehicleMesh& hero : heroes) drivable += hero.drivable() ? 1 : 0;
    CNA::Logger::Info("cna-street: " + std::to_string(heroVehicles_) + " hero vehicles over "
                      + std::to_string(heroes.size()) + " models parked in "
                      + std::to_string(bays.size()) + " hero bays; " + std::to_string(movingHeroes_)
                      + " of " + std::to_string(traffic_.movingCount()) + " moving vehicles drawn as "
                      + std::to_string(drivable) + " drivable models");
}

int CityScene::chooseHeroPlot() const
{
    // The shop the "Shop window" and "Pavement cafe" viewpoints stand in
    // front of: on the west frontage north of the junction, its front on the
    // building line, nearest z = 41, and wide enough for a counter and a
    // window. Chosen from the layout alone, so it is the same plot every run.
    int best = -1;
    float bestScore = 1e30f;
    const std::vector<Plot>& plots = layout_.plots();
    for (std::size_t i = 0; i < plots.size(); ++i)
    {
        const Plot& plot = plots[i];
        if (!plot.hasShop || plot.primary != Facing::PosX || plot.maxX > 0.0f) continue;
        if (plot.depth() < 8.0f) continue;
        const float centre = (plot.minZ + plot.maxZ) * 0.5f;
        if (centre < 28.0f || centre > 58.0f) continue;
        const float score = std::fabs(centre - 41.0f);
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

void CityScene::buildHeroShop(const RenderSettings& settings)
{
    if (heroProps_.empty()) return;
    // Group the anchors by asset, so a croissant that stands in three places
    // is one instance group of three rather than three groups of one.
    std::vector<std::string> assets;
    for (const HeroProp& want : heroProps_)
        if (std::find(assets.begin(), assets.end(), want.asset) == assets.end())
            assets.push_back(want.asset);
    int placed = 0;
    for (const std::string& asset : assets)
    {
        const PropMesh prop = importedProp(asset);
        if (prop.empty()) continue;
        const Vector3 size = prop.bounds.Max - prop.bounds.Min;
        // Stood on its base and centred on its footprint, whatever the
        // author's origin was.
        const Matrix stand = Matrix::CreateTranslation(
            -(prop.bounds.Min.X + prop.bounds.Max.X) * 0.5f, -prop.bounds.Min.Y,
            -(prop.bounds.Min.Z + prop.bounds.Max.Z) * 0.5f);
        std::vector<Matrix> at;
        float cull = 40.0f;
        for (const HeroProp& want : heroProps_)
        {
            if (want.asset != asset) continue;
            float scale = 1.0f;
            if (want.fitMetres > 0.0f)
            {
                const float extent = want.byWidth ? std::max(size.X, 1e-3f) : std::max(size.Y, 1e-3f);
                scale = want.fitMetres / extent;
            }
            at.push_back(stand * Matrix::CreateScale(scale) * want.at);
            cull = std::max(cull, want.cullDistance);
        }
        // No shadow pass for a prop inside a room: the sun reaches it only
        // through the window, and the shadow draws -- one per part per copy
        // -- were a third of the pass for nothing anyone could see.
        placeProp(prop, at, "hero-shop-" + asset, cull, 0.0f, /*castsShadow=*/false);
        placed += static_cast<int>(at.size());
    }
    CNA::Logger::Info("cna-street: shop props -- " + std::to_string(placed) + " of "
                      + std::to_string(heroProps_.size()) + " stood, the hero cafe on plot "
                      + std::to_string(heroPlot_));
}

void CityScene::buildDrivers(const RenderSettings& settings)
{
    driverForVehicle_.assign(traffic_.vehicles().size(), -1);
    driverHeight_.assign(traffic_.vehicles().size(), 1.75f);
    driverPlayers_.clear();
    if (!settings.traffic || characterMeshes_.empty()) return;

    // A moving car with nobody in it is the thing that gives the whole street
    // away once the cars themselves are good: through a windscreen at three
    // metres an empty seat reads instantly, and there are thirty of them.
    //
    // What used to sit there was a prop of its own -- an ellipsoid head with
    // no face, a bar for shoulders, two spheres for hands -- on the argument
    // that glass takes the detail away. It does not take away enough: the
    // figure read as a shop mannequin, which is worse than an empty seat
    // because an empty seat is at least not a person done badly.
    //
    // So a driver is now one of the crowd's own people. The same imported
    // MakeHuman figure with the same skin, the same derived face normals, the
    // same hair and clothes, on the same nineteen-bone skeleton, playing the
    // `drive` clip. It costs the collapsed three-draw material set instead of
    // two, inside thirty metres and in moving cars only -- and it costs no
    // mesh memory at all, because it is a mesh the crowd already uploaded.
    Rng deal = Rng::derive(settings.seed, "drivers");
    const std::vector<Vehicle>& fleet = traffic_.vehicles();
    driverPlayers_.resize(fleet.size());
    int seated = 0;
    for (std::size_t v = 0; v < fleet.size(); ++v)
    {
        // The line-up is the diagnostic: it parks one of every class in a row
        // with a front three-quarter viewpoint on each, and a driver in a
        // parked line-up car is the only way to look one in the face without
        // chasing traffic.
        if (fleet[v].parked && !settings.vehicleLineup) continue;
        const int variant = deal.intRange(0, static_cast<int>(characterMeshes_.size()) - 1);
        driverForVehicle_[v] = variant;
        // Seated height, not standing height. The crowd runs from 1.40 m to
        // 1.88 m as authored, and a 1.88 m figure folded into a Mini puts its
        // crown through the roof lining; a driver is drawn at a height the
        // cabin it is in can hold, which is what a person choosing a car does
        // too. The spread is still there -- 1.62 to 1.84 -- so a row of cars
        // is not a row of one person.
        driverHeight_[v] = deal.range(1.62f, 1.84f);
        driverPlayers_[v] = std::make_unique<Graphics::AnimationPlayer>(
            characterMeshes_[static_cast<std::size_t>(variant)]->skinning);
        ++seated;
    }
    CNA::Logger::Info("cna-street: " + std::to_string(seated) + " drivers, drawn as "
                      + std::to_string(characterMeshes_.size())
                      + " of the crowd's own figures");
}

Matrix CityScene::driverSeat(const Vehicle& vehicle, float steeringSide,
                             float drawnLength, float drawnWidth, float drawnHeight)
{
    // Where the driver's seat cushion is, in the car's own frame, and every
    // one of the three numbers here has been wrong for a different reason.
    //
    // **Across.** A car's left, with the nose at +Z and +Y up, is +X -- the
    // rule `Geometry::AlongFrame` states -- and all eight authored models put
    // their steering wheel there, which `vehicle-orientation.py` measures
    // rather than assumes. The seat used to sit at -X on the strength of a
    // comment, so every driver in the street was in the passenger seat with
    // the wheel beside them and nobody behind it. @p steeringSide carries the
    // model's own side, so a right-hand-drive model added later needs no
    // change here.
    //
    // **Along.** From the *drawn* model's length, not the class's. The two
    // are close for a moving car, which is dealt a model of its own class,
    // and nowhere near for the line-up: a van-class car drawn as a 4.26 m
    // hatchback put its driver 1.6 m forward of centre, which is outside the
    // windscreen. A driver sitting on the bonnet is what this looked like.
    //
    // **Up.** The *lower* of the two heights, which is the one rule that
    // survives both ways of being wrong. A bounding box is honest about
    // length and width and inflates height -- the Mini's box is 1.83 m
    // because of a roof aerial -- so the class caps the model; and a class
    // can be taller than the car actually drawn for it, so the model caps the
    // class. Taking the minimum is right in both directions, and a seat that
    // is a couple of centimetres low is a driver sitting in a car rather than
    // one wearing it.
    const VehicleDimensions d = VehicleFactory::dimensionsFor(vehicle.type);
    const bool van = vehicle.type == VehicleType::Van;
    const float length = drawnLength > 0.5f ? drawnLength : d.length;
    const float width  = drawnWidth  > 0.5f ? drawnWidth  : d.width;
    // 0.175 rather than 0.20 because a drawn width includes the door mirrors
    // and a body width does not; on these eight that is about 18 cm.
    const float across = width * 0.175f * (steeringSide >= 0.0f ? 1.0f : -1.0f);
    const float along  = length * (van ? 0.26f : 0.02f);
    const float height = drawnHeight > 0.5f ? std::min(drawnHeight, d.height) : d.height;
    const float seatY  = height * (van ? 0.40f : 0.38f);
    return Matrix::CreateTranslation(across, seatY, along);
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

    importedPeople_ = 0;
    for (int variant = 0; variant < PedestrianSystem::kVariantCount; ++variant)
    {
        const std::string suffix = std::to_string(variant);
        Rng pick = Rng::derive(settings.seed, "person-" + suffix);

        // An authored person for this variant, where the derived files exist:
        // a MakeHuman figure with real clothes, hair and a face, weighted onto
        // the same nineteen-bone skeleton and driven by the same clips. See
        // CharacterLibrary. The generated figure stands in otherwise.
        char personName[16];
        std::snprintf(personName, sizeof(personName), "person-%02d", variant + 1);
        if (const CharacterLibrary::Person* person = characters_.load(personName))
        {
            auto entry = std::make_unique<CharacterMesh>();
            entry->height = person->height;
            int index = 0;
            for (const CharacterLibrary::Part& part : person->near)
            {
                auto mesh = std::make_unique<SkinnedGpuMesh>(
                    device_, part.mesh, std::string(personName) + "." + std::to_string(index++));
                buildStats_.meshBytes += mesh->gpuBytes();
                buildStats_.triangles += static_cast<std::size_t>(mesh->triangleCount());
                entry->parts.push_back(CharacterMesh::Part{part.material, std::move(mesh)});
            }
            index = 0;
            for (const CharacterLibrary::Part& part : person->far)
            {
                auto mesh = std::make_unique<SkinnedGpuMesh>(
                    device_, part.mesh,
                    std::string(personName) + "-far." + std::to_string(index++));
                buildStats_.meshBytes += mesh->gpuBytes();
                buildStats_.triangles += static_cast<std::size_t>(mesh->triangleCount());
                entry->farParts.push_back(CharacterMesh::Part{part.material, std::move(mesh)});
            }
            entry->skinning.BoneCount         = person->skeleton.count();
            entry->skinning.SkeletonHierarchy = person->skeleton.hierarchy();
            entry->skinning.BindPose          = person->skeleton.bindPose();
            entry->skinning.InverseBindPose   = person->skeleton.inverseBindPose();
            // A base of support of this figure's own, so eight people are not
            // eight copies of one walk: a tenth either side of the plain
            // stance, dealt from the variant's own stream.
            const CharacterFactory::Clips clips = CharacterFactory::clips(
                person->skeleton, person->height, 1.06f, pick.range(0.88f, 1.14f));
            clips.install(entry->skinning.AnimationClips);
            // The rigid stand-in for the shadow pass, from the far copy in its
            // bind pose; see CNA-F14 below.
            entry->shadowProxy = makeProp(std::string(personName) + "-shadow",
                                          [&](GeometryCollector& c) {
                for (const CharacterLibrary::Part& part : person->far)
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
            ++importedPeople_;
            continue;
        }

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

        // Where this figure's hips are in its bind pose, so a seated one can
        // be placed by them: a driver sits on a cushion, not on the floor.
        {
            const int pelvis = full.skeleton.find(BoneName::kPelvis);
            if (pelvis >= 0) entry->hipHeight = full.skeleton[pelvis].head.Y;
        }

        // The skeleton, in the form AnimationPlayer wants. Held by unique_ptr
        // because every player holds a reference to it for its whole life.
        entry->skinning.BoneCount         = full.skeleton.count();
        entry->skinning.SkeletonHierarchy = full.skeleton.hierarchy();
        entry->skinning.BindPose          = full.skeleton.bindPose();
        entry->skinning.InverseBindPose   = full.skeleton.inverseBindPose();
        const CharacterFactory::Clips clips =
            CharacterFactory::clips(full.skeleton, look.height, 1.06f, pick.range(0.88f, 1.14f));
        clips.install(entry->skinning.AnimationClips);

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
    // Somebody at the wheel of every moving one. After the fleet exists,
    // because it is dealt per vehicle.
    buildDrivers(settings);
    CNA::Logger::Info("cna-street: " + std::to_string(importedPeople_) + " of "
                      + std::to_string(PedestrianSystem::kVariantCount)
                      + " crowd variants are imported people");

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
    // Aimed at a tree that is actually there -- the nearest to the middle of
    // the west footway's north run -- from five metres down the footway, with
    // the crown centre in the upper half of the frame. The first version of
    // this viewpoint was aimed at where a tree was expected and showed a
    // facade.
    {
        Vector3 tree(-6.2f, 0.0f, 35.7f);
        float best = 1e30f;
        for (const Vector3& p : treePositions_)
        {
            if (p.X > 0.0f || p.Z < 24.0f || p.Z > 60.0f) continue;
            const float d = std::fabs(p.Z - 40.0f);
            if (d < best) { best = d; tree = p; }
        }
        const Vector3 stand(tree.X - 1.4f, eye, tree.Z - 5.6f);
        const Vector3 aim(tree.X, 4.3f, tree.Z);
        const Vector3 to = aim - stand;
        const float yaw = std::atan2(to.X, -to.Z);
        const float pitch = std::atan2(to.Y, std::sqrt(to.X * to.X + to.Z * to.Z));
        viewpoints_.push_back(Viewpoint{"Street tree", stand, yaw, pitch, 1.15f});

    }
    // One bay of a façade filling the frame: reveal depth, sill, material scale.
    // Four metres north of where it stood: the jacaranda planted at the
    // pit there put its crown through the lens, and a camera inside a tree
    // is not a place a photograph is taken from.
    viewpoints_.push_back(Viewpoint{"Facade detail",
                                    Vector3(-6.2f, 3.10f, 56.0f), kWest + 0.18f, 0.30f, 0.80f});

    // --- the photographs ----------------------------------------------------
    // Two compositions a person with a camera would actually take, at the
    // heights and fields of view a camera has, rather than views chosen to
    // expose a weakness. A little below eye level, looking along the parked
    // cars with the shopfronts behind them, in a 35 mm field.
    viewpoints_.push_back(Viewpoint{"Kerbside",
                                    Vector3(-6.05f, 1.28f, 31.2f), kSouth + 0.30f, -0.03f, 0.92f});
    // Corner to corner across the junction, from the south-east footway,
    // with the signal head and the crossing in the foreground.
    viewpoints_.push_back(Viewpoint{"Corner to corner",
                                    Vector3(8.7f, eye + 0.1f, -10.4f), 3.90f, 0.05f, 1.05f});

    // --- the hero corridor ---------------------------------------------------
    // Two more photographs, both on the west footway north of the junction
    // where the scanned content is densest, each framed so that the things a
    // stranger's eye goes to first -- the hydrant at their feet, the tables
    // outside the shop, the car under its cover, the scanned tree over it --
    // are the things that survive being looked at.
    viewpoints_.push_back(Viewpoint{"Pavement cafe",
                                    Vector3(-6.3f, 1.40f, 40.5f), 3.58f, -0.05f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"Covered car",
                                    Vector3(-5.2f, 1.30f, 51.5f), 0.30f, -0.05f, 1.0996f});

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
        // And in through the driver's window, from the seat this class of car
        // actually has: the only way to look a driver in the face without
        // chasing moving traffic. Aimed from driverSeat, so a seat that moves
        // takes its viewpoint with it.
        {
            Vehicle sample;
            sample.type = TrafficSystem::typeForVariant(variant);
            const Vector3 seat = driverSeat(sample).getTranslationProperty();
            const Vector3 head(at.X + seat.X, seat.Y + 0.62f, at.Y + seat.Z);
            // Stood off the driver's own side of the car, whichever that is:
            // a viewpoint on the passenger side shows a driver through two
            // panes and a passenger seat, which is how the wrong-side driver
            // survived a whole pass of being looked at.
            const float side = seat.X >= 0.0f ? 1.0f : -1.0f;
            const Vector3 stand(head.X + side * 2.5f, head.Y + 0.32f, head.Z + 1.9f);
            const Vector3 look = head - stand;
            // The camera's forward is (sin yaw, sin pitch, -cos yaw): the
            // minus on Z is why a yaw taken as atan2(dx, dz) points a
            // viewpoint at the opposite side of the street.
            viewpoints_.push_back(Viewpoint{
                "Driver " + tag, stand, std::atan2(look.X, -look.Z),
                std::atan2(look.Y, std::sqrt(look.X * look.X + look.Z * look.Z)), 0.46f});
        }
    }
    // Three rows of eight: standing, frozen at heel strike, and spread over
    // the walk cycle. Every one gets a square front view, which is the view an
    // implausible leg spread shows in, and the striding row gets a side view
    // too, which is the view a knee shows in.
    static const char* const kRowName[3] = {"Person ", "Stride ", "Cycle "};
    for (int i = 0; i < PedestrianSystem::kVariantCount * 3; ++i)
    {
        const Vector2 at = PedestrianSystem::lineupPlace(i);
        const std::string tag = kRowName[i / PedestrianSystem::kVariantCount]
                                + std::to_string(i % PedestrianSystem::kVariantCount);
        // Square in front of a figure that is facing the road, far enough
        // back and wide enough that the whole figure is in frame -- the feet
        // most of all, since they are where a stance is read.
        viewpoints_.push_back(Viewpoint{tag, Vector3(at.X + 3.6f, 0.95f, at.Y),
                                        -kEast, 0.0f, 0.72f});
        // And a three-quarter of the striding row, which shows the knee as
        // well as the stance. Not a square side view: the figures stand 3.4 m
        // apart, so a camera abeam one of them is inside the next.
        if (i / PedestrianSystem::kVariantCount == 1)
            viewpoints_.push_back(Viewpoint{tag + " three-quarter",
                                            Vector3(at.X + 2.7f, 0.95f, at.Y - 2.7f),
                                            -kEast * 1.5f, 0.0f, 0.72f});
    }
}

}  // namespace CnaStreet
