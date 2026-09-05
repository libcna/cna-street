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
#include "CnaStreet/Assets/CharacterLibrary.hpp"
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

    /// Where the compiled content is, for the things that are read from it
    /// directly rather than through `ContentManager`: the imported people.
    /// Set before @ref build; without it every variant is generated.
    void setContentRoot(const std::string& root) { characters_.setContentRoot(root); }

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

    /// Where the renderer captures its reflection probes: a row over each
    /// parking lane and each side-street lane, at the pitch the settings ask.
    /// Static, because it reads the street's dimensions and the settings and
    /// nothing else -- which is also what lets a test check it without a device.
    [[nodiscard]] static std::vector<Microsoft::Xna::Framework::Vector3> probePositions(
        const RenderSettings& settings);

    /// Height of the walkable surface, for the walking camera.
    [[nodiscard]] float groundHeight(float x, float z) const;
    /// Whether a point is inside something solid.
    [[nodiscard]] bool isSolid(const Microsoft::Xna::Framework::Vector3& point) const;

private:
    /// Uploads one collector's batches and registers them with the renderer.
    void publish(GeometryCollector& collector, float cullDistance, float shadowDistance);
    /// Uploads one mesh and keeps it alive.
    const GpuMesh* upload(const Geometry::MeshData& data, const std::string& name);

    void buildContext(GeometryCollector& collector, Rng& rng, const RenderSettings& settings);
    void buildViewpoints();

    /// A prop built once and placed many times: one GPU mesh per material it
    /// uses, plus the bounds of the whole thing for culling.
    struct PropMesh
    {
        struct Part
        {
            const Material* material = nullptr;
            const GpuMesh*  mesh     = nullptr;
            /// Where this part sits within the prop. Identity for everything
            /// generated here, which builds each prop about its own origin;
            /// an imported model hangs its parts from nodes with transforms
            /// of their own, and those are composed in front of the placement.
            Microsoft::Xna::Framework::Matrix local =
                Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        };
        std::vector<Part> parts;
        Microsoft::Xna::Framework::BoundingBox bounds;
        [[nodiscard]] bool empty() const { return parts.empty(); }
        /// Adds another prop's parts, offset by @p at: a shrub in a planter.
        void append(const PropMesh& other, const Microsoft::Xna::Framework::Matrix& at);
    };

    /// Runs a generator into a fresh collector and uploads what it produced.
    PropMesh makeProp(const std::string& name, const std::function<void(GeometryCollector&)>& build);
    /// A prop from an imported model, or an empty one when the model is not
    /// there -- which is the cue to build the generated stand-in instead. The
    /// parts whose node name @p include accepts (every part when it is null),
    /// each carrying its node transform and then @p adjust, which is how a
    /// file that ships two variants side by side yields one of them standing
    /// on the origin.
    PropMesh importedProp(const std::string& asset,
                          const Microsoft::Xna::Framework::Matrix& adjust =
                              Microsoft::Xna::Framework::Matrix::getIdentityProperty(),
                          const std::function<bool(const std::string&)>& include = nullptr);
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
    /// The scanned props that make the street look used: manhole covers, a
    /// covered car, pavement cafes, deliveries by the doors. After the traffic,
    /// because the car takes a bay no parked car did.
    void buildDressing(Rng& rng, const RenderSettings& settings);
    /// The hero vehicles: licensed, authored car models parked in the bays
    /// the showcase viewpoints look at, in place of the lofted ones the
    /// traffic system put there. The loft stays in the simulation -- it still
    /// occupies its bay and pedestrians still walk round it -- and is simply
    /// not drawn. Called from buildDressing, before the covered car takes a
    /// bay of its own.
    void buildHeroVehicles(Rng& rng, const RenderSettings& settings);
    /// Stands the hero shop's scanned props where its interior anchored them.
    void buildHeroShop(const RenderSettings& settings);
    /// Which plot is the hero shop: the shop on the west frontage the shop
    /// window and pavement cafe viewpoints look into.
    [[nodiscard]] int chooseHeroPlot() const;
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

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    SceneRenderer&  renderer_;
    MaterialLibrary& materials_;
    ModelLibrary&    models_;
    CharacterLibrary characters_;
    CityLayout      layout_;
    std::vector<Crossing> crossings_;
    std::vector<Microsoft::Xna::Framework::Vector3> manholes_;
    std::vector<FacadeAnchor> anchors_;
    std::vector<ShopDisplay>  displays_;
    /// The hero shop: the plot the close viewpoints look into, built as a
    /// composed bakery-cafe, and the scanned props its interior asked for.
    int heroPlot_ = -1;
    std::vector<HeroProp> heroProps_;

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
    /// One flag per vehicle in `traffic_`: true for a parked loft that a
    /// hero model stands in for, so `submit` leaves it out.
    std::vector<bool> vehicleReplaced_;
    int heroVehicles_ = 0;
    /// The far level of detail of each street tree, kept for the district
    /// beyond the modelled frontage, which plants the same trees at the same
    /// pitch and never gets close enough to want the near one.
    std::vector<PropMesh> farTrees_;
    /// Where the street trees stand, so a viewpoint can be aimed at one rather
    /// than at where one was expected to be.
    std::vector<Microsoft::Xna::Framework::Vector3> treePositions_;
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
    int importedPeople_ = 0;
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
