// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Props/BuildingBuilder.hpp"
#include "CnaStreet/Props/PedestrianFactory.hpp"
#include "CnaStreet/Props/PropFactory.hpp"
#include "CnaStreet/Props/RoadBuilder.hpp"
#include "CnaStreet/Props/VehicleFactory.hpp"
#include "CnaStreet/Sim/PedestrianSystem.hpp"
#include "CnaStreet/Sim/TrafficSystem.hpp"
#include "CnaStreet/Render/CameraController.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CnaStreet {

class GpuMesh;
class SceneRenderer;

/**
 * @brief The city: builds it once, then keeps it alive.
 *
 * Owns the layout, the materials, every uploaded mesh, and the simulation
 * systems that move things around in it. Deliberately not a scene graph:
 * everything static is baked into per-material batches at build time and handed
 * to the renderer, because a street does not move and paying a transform
 * hierarchy for it every frame buys nothing.
 */
class CityScene
{
public:
    CityScene(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
              SceneRenderer& renderer, MaterialLibrary& materials);
    ~CityScene();

    CityScene(const CityScene&) = delete;
    CityScene& operator=(const CityScene&) = delete;

    /// Generates and uploads everything. Reports each stage through the log so a
    /// slow start-up can be attributed.
    void build(const RenderSettings& settings);

    void update(float deltaSeconds, const RenderSettings& settings);
    /// Submits this frame's moving objects to the renderer. @p eye decides which
    /// level of detail each vehicle and each pedestrian is drawn at, which is
    /// the renderer's business everywhere else -- but a dynamic object is
    /// submitted per frame rather than registered as an instance group, so the
    /// choice has to be made here, before the draw exists.
    void submit(const RenderSettings& settings,
                const Microsoft::Xna::Framework::Vector3& eye);

    [[nodiscard]] const CityLayout& layout() const { return layout_; }
    [[nodiscard]] MaterialLibrary& materials() { return materials_; }
    [[nodiscard]] const MaterialLibrary& materialsConst() const { return materials_; }
    [[nodiscard]] const std::vector<Viewpoint>& viewpoints() const { return viewpoints_; }
    [[nodiscard]] const TrafficSignalController& signals() const { return signals_; }
    [[nodiscard]] const TrafficSystem& traffic() const { return traffic_; }
    [[nodiscard]] const PedestrianSystem& pedestrians() const { return pedestrians_; }

    struct BuildStats
    {
        int   plots = 0;
        int   staticBatches = 0;
        int   instanceGroups = 0;
        int   instances = 0;
        int   vehicles = 0;
        int   people = 0;
        int   trees = 0;
        int   signals = 0;
        std::size_t triangles = 0;
        std::size_t meshBytes = 0;
        float buildSeconds = 0.0f;
    };
    [[nodiscard]] const BuildStats& buildStats() const { return buildStats_; }

    /// Height of the walkable surface, for the walking camera.
    [[nodiscard]] float groundHeight(float x, float z) const;
    /// Whether a point is inside something solid.
    [[nodiscard]] bool isSolid(const Microsoft::Xna::Framework::Vector3& point) const;

private:
    /// Uploads one collector's batches and registers them with the renderer.
    void publish(GeometryCollector& collector, float cullDistance, float shadowDistance);
    /// Uploads one mesh and keeps it alive.
    const GpuMesh* upload(const Geometry::MeshData& data, const std::string& name);

    void buildContext(GeometryCollector& collector, Rng& rng);
    void buildViewpoints();

    /// A prop built once and placed many times: one GPU mesh per material it
    /// uses, plus the bounds of the whole thing for culling.
    struct PropMesh
    {
        struct Part
        {
            const Material* material = nullptr;
            const GpuMesh*  mesh     = nullptr;
        };
        std::vector<Part> parts;
        Microsoft::Xna::Framework::BoundingBox bounds;
        [[nodiscard]] bool empty() const { return parts.empty(); }
    };

    /// Runs a generator into a fresh collector and uploads what it produced.
    PropMesh makeProp(const std::string& name, const std::function<void(GeometryCollector&)>& build);
    /// Registers a prop's parts as instance groups sharing one transform list.
    void placeProp(const PropMesh& prop,
                   const std::vector<Microsoft::Xna::Framework::Matrix>& transforms,
                   const std::string& name, float cullDistance, float shadowDistance,
                   bool castsShadow = true);
    /// Submits one placed copy of a prop for this frame. `overrideMaterial`
    /// replaces every part's material, which is how a signal lens is drawn lit
    /// or dark from one mesh.
    void submitProp(const PropMesh& prop, const Microsoft::Xna::Framework::Matrix& transform,
                    const Material* overrideMaterial = nullptr);

    void buildStreetFurniture(Rng& rng, const RenderSettings& settings);
    void buildVegetation(Rng& rng, const RenderSettings& settings);
    void buildSignalsAndSigns(Rng& rng, const RenderSettings& settings);
    /// Shop fascias and house numbers, on the anchors the façade generator
    /// left behind while it was building the elevations.
    void buildSignage(Rng& rng, const RenderSettings& settings);
    void buildTrafficAndPeople(const RenderSettings& settings);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    SceneRenderer&  renderer_;
    MaterialLibrary& materials_;
    CityLayout      layout_;
    std::vector<Crossing> crossings_;
    std::vector<FacadeAnchor> anchors_;

    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::vector<Viewpoint> viewpoints_;

    // --- simulation -------------------------------------------------------
    TrafficSignalController signals_;
    TrafficSystem           traffic_;
    PedestrianSystem        pedestrians_;

    /// Where each signal head is, so its lenses can be lit each frame.
    struct SignalHead
    {
        Microsoft::Xna::Framework::Matrix transform;
        SignalAxis axis = SignalAxis::Main;
        bool pedestrian = false;
    };
    std::vector<SignalHead> signalHeads_;

    // --- prop meshes ------------------------------------------------------
    PropMesh lensRed_, lensAmber_, lensGreen_, lensWalkRed_, lensWalkGreen_;
    /// One entry per paint variant: the body at two levels of detail, the wheel
    /// it runs on, where its four wheels sit, and the pair of lamp lenses that
    /// light up when it brakes.
    struct VehicleMesh
    {
        PropMesh body;
        PropMesh distantBody;
        PropMesh wheel;
        PropMesh distantWheel;
        PropMesh brakeLamps;
        std::vector<WheelPlacement> wheels;
    };
    std::vector<VehicleMesh> vehicleMeshes_;
    std::vector<PropMesh> pedestrianMeshes_;   ///< variant-major, then pose
    int pedestrianPoseCount_ = 0;
    /// Lit and dark variants of each lens colour, in the order red, amber,
    /// green, pedestrian red, pedestrian green.
    std::vector<const Material*> lensLit_, lensDark_;
    /// The rear lamp, lit. One material shared by the whole fleet.
    const Material* brakeLit_ = nullptr;

    Microsoft::Xna::Framework::Vector3 cameraPosition_{0.0f, 1.7f, 0.0f};
    bool lineup_ = false;

    BuildStats buildStats_;
};

}  // namespace CnaStreet
