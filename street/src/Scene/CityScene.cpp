// SPDX-License-Identifier: MIT
#include "CnaStreet/Scene/CityScene.hpp"

#include "CnaStreet/Props/BuildingBuilder.hpp"
#include "CnaStreet/Render/GpuMesh.hpp"
#include "CnaStreet/Render/SceneRenderer.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "CNA/Logger.hpp"
#include "System/Diagnostics/Stopwatch.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using System::Diagnostics::Stopwatch;
using CnaStreet::Geometry::BoxFaces;
using CnaStreet::Geometry::MeshBuilder;

namespace CnaStreet {

namespace M = Metrics;

CityScene::CityScene(GraphicsDevice& device, SceneRenderer& renderer,
                     MaterialLibrary& materials)
    : device_(device), renderer_(renderer), materials_(materials)
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
        item.shadowDistance = shadowDistance;
        renderer_.addItem(item);
        ++buildStats_.staticBatches;
    }
}

void CityScene::build(const RenderSettings& settings)
{
    Stopwatch watch = Stopwatch::StartNew();

    CNA::Logger::Info("cna-street: generating materials");
    materials_.build(settings.seed);

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
        Rng rng = Rng::derive(settings.seed, "buildings");
        BuildingBuilder buildings(materials_, layout_);
        const std::vector<Plot>& plots = layout_.plots();
        for (std::size_t i = 0; i < plots.size(); ++i)
            buildings.build(plots[i], static_cast<int>(i), collector, rng, anchors_);
        publish(collector, 0.0f, settings.shadowDistance);
    }

    CNA::Logger::Info("cna-street: closing the skyline");
    {
        GeometryCollector collector;
        Rng rng = Rng::derive(settings.seed, "context");
        buildContext(collector, rng);
        publish(collector, 0.0f, 0.0f);
    }

    buildViewpoints();

    buildStats_.buildSeconds = static_cast<float>(watch.getElapsedTicksProperty()) / 1.0e7f;
    CNA::Logger::Info("cna-street: scene built in "
                      + std::to_string(buildStats_.buildSeconds) + " s -- "
                      + std::to_string(buildStats_.staticBatches) + " batches, "
                      + std::to_string(buildStats_.triangles) + " triangles, "
                      + std::to_string(buildStats_.meshBytes / (1024u * 1024u)) + " MiB");
}

void CityScene::buildContext(GeometryCollector& collector, Rng& rng)
{
    // Everything outside the modelled block. Two jobs: give the street a ground
    // to stand on so the horizon is not empty, and close the view down each arm
    // with more city, because a street that ends in sky at 130 m is a set.
    //
    // These are blocks, not buildings: no windows, no reveals, no roofs beyond a
    // parapet. They are 200 m away and behind everything, and detail there would
    // be geometry nobody can resolve.
    const Material* ground = &materials_.get(MaterialId::AsphaltWorn);
    const Material* grass  = &materials_.get(MaterialId::Grass);

    // The ground plane, in cells so it culls, and set a little below the road so
    // it can never win a depth fight with it.
    constexpr float kReach = 460.0f;
    constexpr float kStep  = 92.0f;
    for (float x = -kReach; x < kReach; x += kStep)
        for (float z = -kReach; z < kReach; z += kStep)
        {
            const float x1 = std::min(x + kStep, kReach);
            const float z1 = std::min(z + kStep, kReach);
            // Laid under the street rather than around it: leaving a hole where
            // the modelled block sits shows the sky's below-horizon haze through
            // every gap between a building and the kerb.
            collector.setRegion((x + x1) * 0.5f, (z + z1) * 0.5f);
            MeshBuilder& builder = collector.builder(rng.chance(0.35f) ? grass : ground);
            builder.setTileSize(8.0f);
            builder.addQuad(Vector3(x, -0.04f, z), Vector3(x, -0.04f, z1), Vector3(x1, -0.04f, z1),
                            Vector3(x1, -0.04f, z));
        }

    // The blocks that continue the street wall past the modelled plots, and the
    // ones that close each arm.
    const MaterialId walls[] = {MaterialId::RenderGrey, MaterialId::RenderCream,
                                MaterialId::BrickRed, MaterialId::ConcretePanel,
                                MaterialId::RenderOchre, MaterialId::BrickBuff};

    auto block = [&](float cx, float cz, float halfX, float halfZ, float height) {
        collector.setRegion(cx, cz);
        const Material* material = &materials_.get(walls[rng.index(std::size(walls))]);
        MeshBuilder& builder = collector.builder(material);
        builder.setTileSize(2.5f);
        builder.addBox(Vector3(cx - halfX, 0.0f, cz - halfZ),
                       Vector3(cx + halfX, height, cz + halfZ), BoxFaces::allButBottom());
        MeshBuilder& roof = collector.builder(&materials_.get(MaterialId::RoofFelt));
        roof.setTileSize(4.0f);
        roof.addBox(Vector3(cx - halfX - 0.1f, height, cz - halfZ - 0.1f),
                    Vector3(cx + halfX + 0.1f, height + 0.9f, cz + halfZ + 0.1f),
                    BoxFaces::allButBottom());
    };

    // Down both arms of the main street, past the modelled frontage.
    for (const float sign : {-1.0f, 1.0f})
    {
        float z = M::kMainStreetHalfLength + 6.0f;
        while (z < 330.0f)
        {
            const float depth = rng.range(16.0f, 30.0f);
            for (const float side : {-1.0f, 1.0f})
                block(side * (M::kMainStreetHalfWidth + 11.0f), sign * (z + depth * 0.5f), 11.0f,
                      depth * 0.5f, rng.range(13.0f, 26.0f));
            z += depth + rng.range(2.0f, 7.0f);
        }
        // The block that closes the view down the street.
        block(0.0f, sign * 345.0f, 46.0f, 22.0f, rng.range(18.0f, 30.0f));
    }
    // And down the side street.
    for (const float sign : {-1.0f, 1.0f})
    {
        float x = M::kSideStreetHalfLength + 5.0f;
        while (x < 210.0f)
        {
            const float depth = rng.range(15.0f, 26.0f);
            for (const float side : {-1.0f, 1.0f})
                block(sign * (x + depth * 0.5f), side * (M::kSideStreetHalfWidth + 10.0f),
                      depth * 0.5f, 10.0f, rng.range(11.0f, 21.0f));
            x += depth + rng.range(2.0f, 6.0f);
        }
        block(sign * 224.0f, 0.0f, 20.0f, 40.0f, rng.range(15.0f, 26.0f));
    }

    // A far skyline: a scatter of taller blocks well beyond the district, which
    // is what stops the horizon being a clean line of identical parapets.
    for (int i = 0; i < 90; ++i)
    {
        const float angle = rng.range(0.0f, 6.2831853f);
        const float radius = rng.range(240.0f, 430.0f);
        const float cx = std::cos(angle) * radius;
        const float cz = std::sin(angle) * radius * 1.35f;
        block(cx, cz, rng.range(9.0f, 26.0f), rng.range(9.0f, 26.0f), rng.range(12.0f, 44.0f));
    }
}

void CityScene::update(float deltaSeconds, const RenderSettings& settings)
{
    (void)deltaSeconds;
    (void)settings;
}

void CityScene::submit(const RenderSettings& settings)
{
    (void)settings;
}

float CityScene::groundHeight(float x, float z) const
{
    return layout_.groundHeight(x, z);
}

bool CityScene::isSolid(const Vector3& point) const
{
    return layout_.isSolid(point.X, point.Y, point.Z);
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

    viewpoints_.push_back(Viewpoint{"Footway looking south to the junction",
                                    Vector3(-7.4f, eye, 46.0f), kSouth, -0.035f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"On the crossing",
                                    Vector3(0.4f, eye, 19.0f), kSouth + 0.06f, 0.02f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"The corner block",
                                    Vector3(12.4f, eye, 6.6f), kWest - 0.55f, 0.06f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"Down the side street",
                                    Vector3(31.0f, eye, -4.4f), kWest, 0.01f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"Looking up at the facades",
                                    Vector3(-6.6f, eye, -16.0f), kEast, 0.60f, 1.22f});
    // High over the carriageway rather than over a roof: the point of this view
    // is the junction and the roofscape around it, and a camera inside a block
    // sees only the block it is inside.
    viewpoints_.push_back(Viewpoint{"Above the junction",
                                    Vector3(3.0f, 44.0f, 62.0f), 0.03f, -0.60f, 1.0996f});
    viewpoints_.push_back(Viewpoint{"The long view south",
                                    Vector3(2.6f, eye + 0.35f, 98.0f), kSouth, -0.015f, 0.95f});
    viewpoints_.push_back(Viewpoint{"Shopfronts, close",
                                    Vector3(-6.2f, eye, 62.0f), kWest, 0.09f, 1.15f});
}

}  // namespace CnaStreet
