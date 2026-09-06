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
#include "CnaStreet/Render/SceneRenderer.hpp"
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

    /// Called at every stage of @ref build with what it is doing and roughly
    /// how far through it is. The build takes the better part of half a
    /// minute and the window used to be black for all of it, which from
    /// outside is a hung program; whoever sets this paints something instead.
    void setProgressReporter(std::function<void(const std::string&, float)> reporter)
    {
        progress_ = std::move(reporter);
    }

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
    /// Where the street trees stand, for whatever wants to be in one -- the
    /// birds, at present.
    [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Vector3>& treePositions() const
    {
        return treePositions_;
    }

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
    /// Whether a point is inside something solid: a building, a vehicle, or
    /// one of the things in the footway with enough mass to stop somebody.
    [[nodiscard]] bool isSolid(const Microsoft::Xna::Framework::Vector3& point) const;
    /// The nearest point outside any vehicle @p point is inside. The walking
    /// camera cannot walk into a car; a car can still drive into it, and this
    /// is how it gets its space back rather than being carried down the road.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 pushOutOfSolids(
        const Microsoft::Xna::Framework::Vector3& point) const;
    /// The walking camera's own radius, which is what the solids above are
    /// grown by. Half a shoulder plus a little: 0.32 m is the figure the
    /// camera controller has always used for a building.
    static constexpr float kWalkerRadius = 0.32f;

private:
    /// Uploads one collector's batches and registers them with the renderer.
    void publish(GeometryCollector& collector, float cullDistance, float shadowDistance);
    /// Uploads one mesh and keeps it alive.
    const GpuMesh* upload(const Geometry::MeshData& data, const std::string& name);

    /// @p infill takes the rows behind the street-facing blocks, which are
    /// published with a shadow policy of their own.
    void buildContext(GeometryCollector& collector, GeometryCollector& infill, Rng& rng,
                      const RenderSettings& settings);
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
            /// See ModelLibrary::Part::axle. (1,0,0) unless this is a wheel.
            Microsoft::Xna::Framework::Vector3 axle{1.0f, 0.0f, 0.0f};
            float axleSpread = 1.0f;
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
    /// @p minDistance leaves out the copies nearer than it -- see
    /// `InstanceGroup::minDistance`.
    void placeProp(const PropMesh& prop,
                   const std::vector<Microsoft::Xna::Framework::Matrix>& transforms,
                   const std::string& name, float cullDistance, float shadowDistance,
                   bool castsShadow = true, const PropMesh* distant = nullptr,
                   float lodDistance = 0.0f, float minDistance = 0.0f);
    /// The same prop with every part that shares a material merged into one
    /// mesh, the node transforms baked in, or the prop itself when its
    /// geometry cannot be read back. For a prop that never moves a part of
    /// itself: an authored car's parked copy carries its wheels as separate
    /// nodes so the moving copy can roll them, and on the Punto that is
    /// nineteen draws a copy for a car standing still. Merged it is one draw
    /// per material.
    PropMesh mergedByMaterial(const PropMesh& prop, const std::string& name);
    /// Registers @p proxy as what @p transforms casts a shadow with, drawn
    /// nowhere else. For a prop whose cheaper mesh does not match its
    /// detailed one part for part -- an authored car's far copy is a
    /// differently-merged model, not the same materials at fewer triangles,
    /// so @ref placeProp's own index-matched LOD swap cannot use it -- and
    /// whose detailed shadow is too expensive to keep: `--frames` measured
    /// the parked hero fleet's own near-mesh shadows at over a million
    /// triangles a frame for geometry a shadow can never resolve to begin
    /// with. The shadow effect reads only positions, so the proxy's own
    /// materials -- however many, however matched to the visible mesh's --
    /// are irrelevant; only its silhouette has to agree.
    void placeShadowProxy(const PropMesh& proxy,
                          const std::vector<Microsoft::Xna::Framework::Matrix>& transforms,
                          const std::string& name, float shadowDistance);
    /// Submits one placed copy of a prop for this frame. `overrideMaterial`
    /// replaces every part's material, which is how a signal lens is drawn lit
    /// or dark from one mesh.
    void submitProp(const PropMesh& prop, const Microsoft::Xna::Framework::Matrix& transform,
                    const Material* overrideMaterial = nullptr, bool shadowOnly = false,
                    DrawFamily family = DrawFamily::Other);

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
    /// The people in the moving cars. Rigid, one piece, and only drawn while
    /// the cabin is close enough to see into.
    void buildDrivers(const RenderSettings& settings);
    /// One person in one car: the crowd's own figure, posed to drive.
    void submitDriver(std::size_t index, const Vehicle& vehicle,
                      const Microsoft::Xna::Framework::Matrix& world, float distance);
    /// Which side of its centre line a vehicle's drawn model steers from.
    [[nodiscard]] float steeringSideFor(std::size_t vehicle) const;
    /// Where a driver sits in a vehicle of this class, in the car's own frame.
    /// @p steeringSide is +1 when the model's steering wheel is on the car's
    /// left (+X) and -1 when it is on its right, as measured off the model.
    /// @p drawnLength and @p drawnWidth are the model actually drawn for this
    /// vehicle, or 0 to fall back on the class's own dimensions.
    [[nodiscard]] static Microsoft::Xna::Framework::Matrix driverSeat(
        const Vehicle& vehicle, float steeringSide = 1.0f,
        float drawnLength = 0.0f, float drawnWidth = 0.0f, float drawnHeight = 0.0f);
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

    /// One thing in the street a walker cannot pass through, as an upright
    /// cylinder: a lamp column, a signal post, a bollard, a bin, a hydrant,
    /// a cabinet, a bench, a planter, a tree. Recorded where each is placed,
    /// because that is the only place that knows what a transform means.
    struct Obstacle
    {
        Microsoft::Xna::Framework::Vector2 centre{0.0f, 0.0f};
        float radius = 0.2f;
        float base = 0.0f;
        float top = 2.0f;
    };
    std::vector<Obstacle> obstacles_;
    /// Records one, from a placement transform.
    void addObstacle(const Microsoft::Xna::Framework::Matrix& at, float radius, float height,
                     float base = 0.0f);
    /// Records one for every copy in a placement list.
    void addObstacles(const std::vector<Microsoft::Xna::Framework::Matrix>& at, float radius,
                      float height, float base = 0.0f);

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
    /// An authored car, as the scene drives it: the whole model for a parked
    /// copy, the body alone and four wheels for a moving one -- each wheel
    /// exported by scripts/blender-vehicles.py as its own node, centred on
    /// its axle, so it can be rolled and steered the way the lofts' are --
    /// and the far copy with the wheels welded on.
    struct HeroVehicleMesh
    {
        struct Wheel
        {
            /// The parts that turn with the road: everything in the wheel's
            /// node that is a surface of revolution about the axle.
            PropMesh mesh;
            /// The parts that do not. A wheel node as the splitter leaves it
            /// often carries a brake caliper, a suspension upright, an arch
            /// liner or a mudflap as well as the wheel, and those are bolted
            /// to the car, not to the rim: rolled with it they orbit the axle
            /// once a revolution, which is exactly what a wobbling wheel is.
            /// They still steer, because a caliper turns with the stub axle.
            PropMesh hub;
            Microsoft::Xna::Framework::Vector3 centre{0.0f, 0.0f, 0.0f};
            /// The rotation that brings the wheel's measured axle onto the
            /// car's X, applied before the roll. Five of the eight models
            /// export their front wheels with ten to eighteen degrees of toe
            /// or camber baked into the mesh; rolled about X as they came,
            /// they wobbled by twice that every turn of the wheel, and stood
            /// visibly turned when the car was going straight. Identity for
            /// a wheel whose axle is X already.
            Microsoft::Xna::Framework::Matrix straighten =
                Microsoft::Xna::Framework::Matrix::getIdentityProperty();
            /// How far the axle was off X, in degrees, for the log.
            float tiltDegrees = 0.0f;
            bool steered = false;
        };
        std::string name;
        PropMesh whole;
        /// @ref whole with its parts merged by material, for the parked
        /// copies: nothing on a parked car moves, so nothing needs a node.
        PropMesh parked;
        PropMesh body;
        PropMesh far;
        std::vector<Wheel> wheels;
        float wheelRadius = 0.32f;
        float length = 4.4f;
        /// The model's own width and height, which is the solid a walker
        /// meets: a Sprinter is not a hatchback to walk into.
        float width  = 1.8f;
        float height = 1.5f;
        /// Which side of this model's own centre line the steering wheel is
        /// on: +1 for the car's left (+X, a left-hand-drive car), -1 for its
        /// right. Measured from the cabin's own geometry at load, not
        /// declared -- see CityScene::measureSteeringSide. A driver seated on
        /// the other side of it is a passenger, and a car with a passenger
        /// and no driver is what a viewer notices first.
        float steeringSide = 1.0f;
        VehicleType nearest = VehicleType::Hatchback;
        /// Only a model whose wheels came out of the file separately can be
        /// driven; one that did not stays parked.
        [[nodiscard]] bool drivable() const { return wheels.size() == 4 && !body.empty(); }
    };
    std::vector<HeroVehicleMesh> heroVehicleMeshes_;
    /// One entry per vehicle in `traffic_`: which authored model a moving
    /// vehicle is drawn as, or -1 for the loft.
    std::vector<int> heroForVehicle_;
    int movingHeroes_ = 0;
    /// The people in the cars. Not a prop of their own any more: each one is
    /// one of the crowd's own imported figures, on the crowd's own skeleton,
    /// playing the `drive` clip. @ref driverForVehicle_ is the character
    /// variant each moving vehicle carries, or -1 for a parked one, which is
    /// empty; @ref driverPlayers_ is one animation player per vehicle, so no
    /// two drivers in a frame hold the wheel at the same angle.
    std::vector<int> driverForVehicle_;
    std::vector<float> driverHeight_;
    /// Wall clock the scene's own animations run on -- the drivers' hands on
    /// the wheel, which are not driven by anything the simulation reports.
    float elapsedSeconds_ = 0.0f;
    std::function<void(const std::string&, float)> progress_;
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::AnimationPlayer>>
        driverPlayers_;

    /// The far level of detail of each street tree, kept for the district
    /// beyond the modelled frontage, which plants the same trees at the same
    /// pitch and never gets close enough to want the near one.
    std::vector<PropMesh> farTrees_;
    /// The far copy of each scanned species, and the scale it is planted at,
    /// so the district beyond the modelled frontage plants the same trees the
    /// street does rather than a different generation of them.
    std::vector<PropMesh> farHeroTrees_;
    std::vector<float>    farHeroScale_;
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
        /// How far the pelvis stands above the soles in the bind pose. A
        /// seated figure is placed by its hips -- the seat cushion is where
        /// the car puts it -- and a figure placed by its feet instead sits
        /// with its head through the roof.
        float hipHeight = 0.92f;
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
