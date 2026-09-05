// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Props/BuildingBuilder.hpp"
#include "CnaStreet/Props/CharacterFactory.hpp"
#include "CnaStreet/Props/PropFactory.hpp"
#include "CnaStreet/Props/RoadBuilder.hpp"
#include "CnaStreet/Props/VehicleFactory.hpp"
#include "CnaStreet/Sim/PedestrianSystem.hpp"
#include "CnaStreet/Sim/TrafficSystem.hpp"
#include "CnaStreet/Render/CameraController.hpp"
#include "CnaStreet/Assets/ModelLibrary.hpp"
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
class SkinnedGpuMesh;
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
              SceneRenderer& renderer, MaterialLibrary& materials, ModelLibrary& models);
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
                   bool castsShadow = true, const PropMesh* distant = nullptr,
                   float lodDistance = 0.0f);
    /// Submits one placed copy of a prop for this frame. `overrideMaterial`
    /// replaces every part's material, which is how a signal lens is drawn lit
    /// or dark from one mesh.
    void submitProp(const PropMesh& prop, const Microsoft::Xna::Framework::Matrix& transform,
                    const Material* overrideMaterial = nullptr, bool shadowOnly = false);

    void buildStreetFurniture(Rng& rng, const RenderSettings& settings);
    void buildVegetation(Rng& rng, const RenderSettings& settings);
    void buildSignalsAndSigns(Rng& rng, const RenderSettings& settings);
    /// Shop fascias and house numbers, on the anchors the façade generator
    /// left behind while it was building the elevations.
    void buildSignage(Rng& rng, const RenderSettings& settings);
    /// Stands one imported model on each display plinth the shopfronts left
    /// behind. Silently does nothing when the external assets have not been
    /// fetched, which is the same contract the compiled surfaces have.
    void buildShopDisplays(const RenderSettings& settings);
    void buildTrafficAndPeople(const RenderSettings& settings);
    /// Switches on everything in the catalogue that is a lamp rather than a
    /// surface. Called once, before anything is built, when the sun is down.
    void lightTheStreet(const RenderSettings& settings);
    /// Advances every pedestrian's animation and submits the crowd.
    void submitPeople(const RenderSettings& settings);
    /// Where the renderer captures its reflection probes: a row over each
    /// parking lane and each side-street lane, at the pitch the settings ask.
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Vector3> probePositions(
        const RenderSettings& settings) const;

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    SceneRenderer&  renderer_;
    MaterialLibrary& materials_;
    ModelLibrary&    models_;
    CityLayout      layout_;
    std::vector<Crossing> crossings_;
    std::vector<FacadeAnchor> anchors_;
    std::vector<ShopDisplay>  displays_;

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
        /// Only the near body has separate wheels; the distant one carries
        /// them baked in at the straight-ahead position, because twelve draw
        /// calls per car buys a rotation nobody can see at that range.
        PropMesh wheel;
        PropMesh brakeLamps;
        std::vector<WheelPlacement> wheels;
    };
    std::vector<VehicleMesh> vehicleMeshes_;
    /// One skinned figure per appearance variant: its parts, the skeleton the
    /// clips run on, and a rigid stand-in for the shadow pass.
    struct CharacterMesh
    {
        struct Part
        {
            const Material* material = nullptr;
            std::unique_ptr<SkinnedGpuMesh> mesh;
        };
        std::vector<Part> parts;
        /// The same figure at half the ring count with its small materials
        /// folded into its large ones: four draws instead of six, for a person
        /// who is thirty pixels tall. Same skeleton, same clips, so a figure
        /// crossing the switch distance changes its triangle count and nothing
        /// else.
        std::vector<Part> farParts;
        Microsoft::Xna::Framework::Graphics::SkinningData skinning;
        PropMesh shadowProxy;
        float height = 1.75f;
    };
    /// Held indirectly because every AnimationPlayer keeps a reference to its
    /// variant's SkinningData for its whole life, and a vector that reallocates
    /// would leave every player pointing at freed memory.
    std::vector<std::unique_ptr<CharacterMesh>> characterMeshes_;
    /// One player per person. Advanced to that person's own animation time
    /// every frame, so no two people in the crowd are in step.
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::AnimationPlayer>> walkers_;

    /// A glTF character imported through CNA's own pipeline: skeleton, bind
    /// pose, inverse bind pose, hierarchy and clip all crossing from the file
    /// to `SkinningData` without this application interpreting any of them.
    /// Loaded at start-up so the round trip is exercised and logged, and *not*
    /// placed in the crowd -- see docs/cna-findings.md GLTF-208 for why.
    const ModelLibrary::ImportedRig* importedWalker_ = nullptr;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::AnimationPlayer> importedPlayer_;
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
