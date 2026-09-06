// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Render/SkySystem.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class PbrEffect;
    class RenderTarget2D;
    class SkinnedPbrEffect;
    class TextureCube;
}

namespace CNA::Graphics {
    class CascadedShadowMap;
    class DepthNormalPrepass;
    class GpuTimer;
    class InstancedRendererEXT;
    class RenderPipeline;
}

namespace CnaStreet {

class GpuMesh;
class SkinnedGpuMesh;
class MaterialLibrary;
struct Material;

/// One skinned figure, submitted per frame with its own bone palette.
///
/// The palette is carried by value rather than by pointer into the animation
/// player, because the player is advanced once per *pose* and shared by every
/// pedestrian at that pose: keeping a pointer would draw them all in whatever
/// pose the last one to be updated happened to be in.
struct SkinnedItem
{
    const SkinnedGpuMesh* mesh     = nullptr;
    const Material*       material = nullptr;
    Microsoft::Xna::Framework::Matrix world =
        Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    Microsoft::Xna::Framework::BoundingSphere worldSphere;
    /// Bone-space-to-model-space for every bone, as
    /// `AnimationPlayer::GetSkinTransforms()` produces it.
    const std::vector<Microsoft::Xna::Framework::Matrix>* bones = nullptr;
    /// Whether this figure is on the footway or behind a windscreen. The two
    /// are the same mesh and the same clip machinery and cost the frame
    /// differently, so the profile counts them apart.
    bool driver = false;
};

/**
 * @brief The street as seen from one point, in the forms `PbrEffect` samples.
 *
 * The sky cube lights every surface in the scene as though it stood on an
 * empty plain under an open sky. Most of what a car door or a shop window
 * actually reflects is the street: the facade opposite, the kerb, the parked
 * cars, a strip of sky between the eaves. A probe is that view, captured once
 * at scene build by rendering the static scene six ways from a point on the
 * carriageway, convolved by `EnvironmentProcessor` exactly as the sky is, and
 * handed to every draw near it through `ImageBasedLightEXT` -- the same field
 * the sky arrives through, so the effect never knows the difference.
 *
 * Its texels are stored at the sky cube's own scale, so the one `Intensity`
 * on the bundle is right whichever cube fills each slot.
 */
struct ReflectionProbe
{
    Microsoft::Xna::Framework::Vector3 position;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment;
    /// The same capture multiplied by `RenderSettings::probeBounceGain`, which
    /// the irradiance is convolved from: a capture holds one bounce of light
    /// and a street canyon's ambient is several.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> bounced;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> prefiltered;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance;
    int prefilteredMips = 5;
};

/// Which part of the street issued a draw, so a frame's cost can be
/// attributed to content and not only to stages. A stage table says the
/// opaque pass costs 27 ms; this says how much of that is cars.
enum class DrawFamily : std::uint8_t
{
    Other,
    Vehicle,
};

/// One piece of static geometry in the world.
struct SceneItem
{
    const GpuMesh*  mesh     = nullptr;
    const Material* material = nullptr;
    Microsoft::Xna::Framework::Matrix world =
        Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    Microsoft::Xna::Framework::BoundingBox    worldBounds;
    Microsoft::Xna::Framework::BoundingSphere worldSphere;
    /// Past this, the item is not drawn at all. 0 means "always".
    float cullDistance = 0.0f;
    /// Past this, the item stops being written into the shadow map.
    float shadowDistance = 0.0f;
    /// Written into the shadow map and nowhere else. The stand-in a skinned
    /// character casts with, because CNA's cascade caster has no bone palette.
    bool shadowOnly = false;
    /// The local environment this item reflects, or null for the sky's.
    const ReflectionProbe* probe = nullptr;
    DrawFamily family = DrawFamily::Other;
};

/// A set of copies of one mesh, drawn with one instanced call.
struct InstanceGroup
{
    const GpuMesh*  mesh     = nullptr;
    const Material* material = nullptr;
    std::vector<Microsoft::Xna::Framework::Matrix>          transforms;
    std::vector<Microsoft::Xna::Framework::BoundingSphere>  spheres;
    /// Optional cheaper mesh used past @ref lodDistance.
    const GpuMesh* lodMesh = nullptr;
    float lodDistance   = 0.0f;
    float cullDistance  = 0.0f;
    /// Nearer than this an instance is not drawn. 0 means "from the eye".
    /// The other half of a per-instance level of detail: a group of near
    /// copies culled at a distance and a group of far copies culled inside
    /// it draw each instance at the detail its own distance deserves, where
    /// @ref lodMesh decides once for the whole group. Shadows ignore it: a
    /// group that casts, casts every copy, so the far group of such a pair
    /// is the one that carries the shadow.
    float minDistance   = 0.0f;
    float shadowDistance = 0.0f;
    bool  castsShadow   = true;
    /// This group exists only to be a shadow proxy for another one -- see
    /// `CityScene::placeShadowProxy`. It draws nowhere the opaque or
    /// transparent pass looks, and it exists because a lower level of detail
    /// often does not share its higher one's part count (a merged shadow
    /// proxy is fewer materials, not the same materials at fewer triangles),
    /// so @ref lodMesh's part-for-part matching cannot swap it in.
    bool  shadowOnly    = false;
    std::string name;
};

/**
 * @brief Draws the city.
 *
 * Owns the frame: the cascaded shadow pass, the depth/normal prepass SSAO needs,
 * the sky, the HDR scene target and post-process chain, and the sorted opaque
 * and transparent draws in between. Everything it draws was registered before
 * the first frame (static geometry and instance groups) or submitted this frame
 * (vehicles and pedestrians, which move).
 *
 * Every EXT subsystem is optional and is probed rather than assumed: on a
 * renderer without float render targets the pipeline resolves straight to the
 * back buffer, without shadow sampling the cascades are skipped, without
 * instancing `InstancedRendererEXT` falls back to a loop. The scene still
 * renders; it renders with less.
 */
class SceneRenderer
{
public:
    struct Stats
    {
        int drawCalls = 0;
        int shadowDrawCalls = 0;
        int instancedDrawCalls = 0;
        int visibleItems = 0;
        int totalItems = 0;
        int visibleInstances = 0;
        int totalInstances = 0;
        int skinnedDrawCalls = 0;
        int visibleCharacters = 0;
        std::size_t triangles = 0;
        int postPasses = 0;
        bool usedSceneTarget = false;
        bool drewShadows = false;
        /// Non-overlapping slices of one frame, in the order they run. They sum
        /// to `frameMs` by construction, which is the only way a breakdown is
        /// worth showing at all.
        float cullMs = 0.0f;
        float shadowMs = 0.0f;
        float prepassMs = 0.0f;
        float skyMs = 0.0f;
        float opaqueMs = 0.0f;
        float postMs = 0.0f;
        /// The renderer's own wall-clock frame time. `GameTime` reports the game
        /// step, which is not the same thing and is a constant under a fixed
        /// time step.
        float frameMs = 0.0f;
        double gpuFrameMs = -1.0;

        /// What the **GPU** spent on each stage, in the same order and with
        /// the same boundaries as the CPU numbers above, or -1 where the
        /// renderer has no timer query.
        ///
        /// Two clocks rather than one, because a frame this size has two
        /// possible shapes and the CPU numbers alone cannot tell them apart.
        /// If the GPU times are far below the CPU times the frame is bound by
        /// *submission* -- the driver taking the calls -- and the answer is
        /// fewer draws. If they match, the frame is bound by the GPU and the
        /// answer is less work per pixel or per vertex. Every optimisation in
        /// the last two passes was chosen on the first hypothesis without
        /// anybody measuring the second.
        double gpuShadowMs = -1.0;
        double gpuPrepassMs = -1.0;
        double gpuSkyMs = -1.0;
        double gpuOpaqueMs = -1.0;
        double gpuPostMs = -1.0;
        /// One entry per post-process pass, from the pipeline's own timers.
        std::vector<std::pair<std::string, double>> gpuPostPasses;

        /// What the shadow pass did, per cascade: how many draws went into
        /// it, how many triangles they carried, and how far down the camera's
        /// own view the cascade reaches. A cascade full of window frames whose
        /// shadows land on nothing is invisible in a total and obvious here.
        struct CascadeWork
        {
            int       draws = 0;
            long long triangles = 0;
            float     split = 0.0f;
            float     radius = 0.0f;
        };
        std::vector<CascadeWork> cascades;

        /// Draw calls by the family that issued them, so the frame budget can
        /// be attributed to content rather than to stages alone.
        int vehicleDrawCalls = 0;
        int vehicleShadowDrawCalls = 0;
        int driverDrawCalls = 0;
        int characterShadowDrawCalls = 0;
        std::size_t vehicleTriangles = 0;
        std::size_t characterTriangles = 0;

        /// The opaque pass's CPU time split between the two things it does
        /// per draw: setting the material on the effect and applying it, and
        /// issuing the draw. Only measured while @ref setDrawTimingEnabled is
        /// on; -1 otherwise. The split is what says whether a material state
        /// cache on this side of the framework could buy anything: if the
        /// setters are a tenth of the draw, skipping them saves a tenth.
        float opaqueApplyMs = -1.0f;
        float opaqueDrawMs  = -1.0f;
        float skinnedMs     = -1.0f;
        /// How many times the PBR effect was applied this frame across the
        /// opaque and transparent passes, and how many of those were for the
        /// same material and environment as the call before -- the ones a
        /// state cache could have collapsed if the framework did not re-upload
        /// every parameter per draw regardless.
        int materialApplies = 0;
        int repeatedMaterialApplies = 0;
        /// Casters the per-cascade slice test left out of a cascade this
        /// frame, and the ones the texel floor did. See drawCasters.
        int shadowSliceSkips = 0;
        int shadowTexelSkips = 0;
    };

    SceneRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                  MaterialLibrary& materials);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Creates the subsystems the settings ask for and reports what the renderer
    /// could not provide. Call once after the device exists, and again whenever
    /// a setting that changes an allocation (cascade count, shadow quality)
    /// moves.
    void initialise(const RenderSettings& settings);
    void applySettings(const RenderSettings& settings);
    void resize(int width, int height);

    [[nodiscard]] SkySystem& sky() { return sky_; }
    [[nodiscard]] const SkySystem& sky() const { return sky_; }

    // --- scene registration -------------------------------------------------
    void addItem(SceneItem item);
    void addInstances(InstanceGroup group);
    void clearScene();

    // --- per-frame ----------------------------------------------------------
    /// Clears the dynamic list. Call before submitting this frame's movers.
    void beginFrame();
    void submitDynamic(const GpuMesh* mesh, const Material* material,
                       const Microsoft::Xna::Framework::Matrix& world, bool shadowOnly = false,
                       DrawFamily family = DrawFamily::Other);
    void submitSkinned(SkinnedItem item);

    void render(const Camera& camera, const RenderSettings& settings, float timeSeconds);

    [[nodiscard]] const Stats& stats() const { return stats_; }
    /// A human-readable list of what the renderer could not do, for the overlay.
    [[nodiscard]] const std::vector<std::string>& limitations() const { return limitations_; }
    [[nodiscard]] std::size_t geometryBytes() const { return geometryBytes_; }

    /// Writes the cascade atlas to a PNG. The only way to tell an empty shadow
    /// map from a correct one that is being sampled wrongly, and worth keeping.
    void dumpShadowAtlas(const std::string& path) const;

    // --- reflection probes --------------------------------------------------
    /// Captures the registered static scene from each of @p positions and
    /// assigns every registered item the nearest one. Call once the static
    /// scene is complete and before the first frame; costs a few seconds.
    /// Does nothing, and clears any earlier set, when the settings turn probes
    /// off or the renderer has no image-based lighting to feed them into.
    void bakeReflectionProbes(std::vector<Microsoft::Xna::Framework::Vector3> positions,
                              const RenderSettings& settings);
    /// Called once per probe during a bake with the fraction done, so a
    /// loading screen can move while seven seconds pass.
    void setBakeProgress(std::function<void(float)> reporter)
    {
        bakeProgress_ = std::move(reporter);
    }
    /// Captures the same positions again -- after the sun has moved.
    void rebakeReflectionProbes(const RenderSettings& settings);
    [[nodiscard]] const ReflectionProbe* nearestProbe(
        const Microsoft::Xna::Framework::Vector3& at) const;
    [[nodiscard]] std::size_t probeCount() const { return probes_.size(); }
    /// How long the last bake took, for the overlay and the log.
    [[nodiscard]] float probeBakeSeconds() const { return probeBakeSeconds_; }
    /// Writes each probe's environment cube as a strip of six faces, so a
    /// capture that came out mirrored or upside down can be seen to be.
    void dumpReflectionProbes(const std::string& directory) const;
    /// The camera that looks out of one cube face during a capture: its
    /// forward axis and its up. Public so a test can pin the convention
    /// against `SkySystem::cubeDirection`, because a face captured mirrored
    /// or upside down is not an error anything reports -- it is a reflection
    /// of the wrong side of the street.
    static void probeFaceBasis(Microsoft::Xna::Framework::Graphics::CubeMapFace face,
                               Microsoft::Xna::Framework::Vector3& forward,
                               Microsoft::Xna::Framework::Vector3& up);

    /// What the scene costs, broken down by the name each batch was registered
    /// under, heaviest first. A frame time says the scene got slower; this says
    /// which part of it did, which is the difference between tuning and
    /// guessing. Counted over the registered scene rather than one frame's
    /// visible set, so it does not depend on where the camera happens to be.
    struct BatchCost
    {
        std::string name;
        int         batches   = 0;
        int         copies    = 0;   ///< instances, for an instanced group
        long long   triangles = 0;   ///< as drawn, copies included
        bool        castsShadow = true;
        float       cullDistance = 0.0f;
    };
    /// @p limit 0 means every family.
    [[nodiscard]] std::vector<BatchCost> costReport(std::size_t limit) const;

    /// Turns on the by-name shadow-draw breakdown @ref shadowReport reads.
    /// Off by default: it is a hash-map insert per shadow draw call, which is
    /// not something a frame anybody is timing should pay for free.
    void setShadowReportEnabled(bool enabled) { shadowReportEnabled_ = enabled; }
    /// Turns on the apply-versus-draw split in @ref Stats. Two clock reads per
    /// opaque draw; a profiling run pays it, an ordinary frame does not.
    void setDrawTimingEnabled(bool enabled) { drawTimingEnabled_ = enabled; }

    /// Whether a caster has to be written into one cascade at all.
    ///
    /// The receiver picks its cascade by view depth, so a caster only needs
    /// to be in the cascade covering `[nearDepth, farDepth]` if something it
    /// can shade lies at those depths. Everything a caster can shade lies in
    /// the sphere swept from it along the light until the whole sphere has
    /// passed below the lowest receiver (@p groundY): the caster's own
    /// position, the ground its shadow lands on, and every wall and roof in
    /// between. This tests that swept volume's view-depth range against the
    /// cascade's, padded by @p margin for the receiver's blend band. It is
    /// what stops the far cascade -- whose fit sphere contains most of the
    /// near street -- being written with every bollard, person and parked car
    /// beside the camera, none of whose shadows it will ever be asked for.
    /// A sun near the horizon throws every shadow to the edge of the world,
    /// and the test says yes to everything. Public and pure so it can be
    /// checked against hand-worked cases.
    [[nodiscard]] static bool casterShadowReachesSlice(
        const Microsoft::Xna::Framework::Vector3& eye,
        const Microsoft::Xna::Framework::Vector3& forward, float nearDepth, float farDepth,
        const Microsoft::Xna::Framework::Vector3& lightDirection,
        const Microsoft::Xna::Framework::Vector3& centre, float radius, float groundY,
        float margin);
    /// What the *last frame's shadow pass* drew, by the name each caster was
    /// registered under, heaviest first -- across every cascade, since a
    /// caster near the camera is often written into more than one. This is
    /// the table that answers "where do the shadow pass's triangles actually
    /// come from", which a per-cascade total cannot: a cascade's triangle
    /// count says how much a slice of the frustum cost, not which content
    /// family is paying for it.
    [[nodiscard]] std::vector<BatchCost> shadowReport(std::size_t limit) const;

    /// The same breakdown over the set that survived the *last* frame's cull,
    /// which is the one that actually cost anything. `batches` is draw calls
    /// and `copies` is instances; the registered report says how heavy the
    /// scene is, this one says how heavy the view is.
    [[nodiscard]] std::vector<BatchCost> visibleReport(std::size_t limit) const;

private:
    void drawOpaque(const Camera& camera, const RenderSettings& settings);
    void drawSkinned(const Camera& camera, const RenderSettings& settings);
    void drawTransparent(const Camera& camera, const RenderSettings& settings);
    void drawShadows(const Camera& camera, const RenderSettings& settings);
    void drawPrepass(const Camera& camera, const RenderSettings& settings);
    void cull(const Camera& camera, const RenderSettings& settings);
    void applyMaterial(const Material& material, const Microsoft::Xna::Framework::Matrix& world,
                       const Microsoft::Xna::Framework::Matrix& view,
                       const Microsoft::Xna::Framework::Matrix& projection,
                       const RenderSettings& settings, const ReflectionProbe* probe);
    void applyLighting(const RenderSettings& settings);
    /// Binds @p probe's cubes -- or the sky's, for null -- as the effect's
    /// image-based light, skipping the upload when they are already bound.
    void applyEnvironment(const ReflectionProbe* probe, const RenderSettings& settings);
    /// The ground one cascade covers: the bounding sphere of a slice of the
    /// camera frustum, grown by how far a caster outside it can still reach
    /// into it. What a cascade needs is this, not a disc around the camera.
    struct CascadeVolume
    {
        Microsoft::Xna::Framework::Vector3 eye;
        Microsoft::Xna::Framework::Vector3 centre;
        float radius = 0.0f;
        /// How far down the camera's own view this cascade reaches. The
        /// sphere says *where*, this says *how far*, and a caster has to
        /// satisfy both: the sphere alone lets the near cascade take in
        /// everything beside the camera, which is where a street is densest.
        float split = 0.0f;
        /// Where the slice starts, and the camera's forward, for the
        /// per-cascade test in @ref casterShadowReachesSlice. `slice` is
        /// false for a probe capture, which is a sphere about a point and not
        /// a slice of anything.
        float nearSplit = 0.0f;
        Microsoft::Xna::Framework::Vector3 forward{0.0f, 0.0f, -1.0f};
        bool  slice = false;
        /// The ground one texel of this cascade covers, in metres. A caster
        /// narrower than one is under the map's resolution and is not drawn.
        float texel = 0.0f;
    };
    [[nodiscard]] CascadeVolume cascadeVolume(const Camera& camera, float nearSplit,
                                              float farSplit) const;
    /// Everything that casts into @p volume, written into the cascade
    /// currently open. Shared by the frame's shadow pass and the probe capture.
    void drawCasters(const CascadeVolume& volume, float propShadowLimit);
    /// One face of a probe: the sky, then the static scene, from @p view.
    void drawProbeFace(const Microsoft::Xna::Framework::Vector3& eye,
                       const Microsoft::Xna::Framework::Matrix& view,
                       const Microsoft::Xna::Framework::Matrix& projection, int size,
                       const RenderSettings& settings);
    void captureProbe(ReflectionProbe& probe,
                      Microsoft::Xna::Framework::Graphics::RenderTarget2D& target, int size,
                      const RenderSettings& settings);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    MaterialLibrary& materials_;
    SkySystem sky_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::PbrEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect> skinnedEffect_;
    std::unique_ptr<CNA::Graphics::RenderPipeline>     pipeline_;
    std::unique_ptr<CNA::Graphics::CascadedShadowMap>  shadows_;
    std::unique_ptr<CNA::Graphics::DepthNormalPrepass> prepass_;
    /// One timer per stage. They are opened and closed in sequence and never
    /// nested, because `GL_TIME_ELAPSED` allows exactly one open query at a
    /// time; the post-process chain measures its own passes, so the frame's
    /// post stage is the sum of those rather than a timer wrapped round them.
    enum class GpuStage { Shadow, Prepass, Sky, Opaque, Count };
    std::array<std::unique_ptr<CNA::Graphics::GpuTimer>,
               static_cast<std::size_t>(GpuStage::Count)> gpuStage_;
    std::array<double, static_cast<std::size_t>(GpuStage::Count)> gpuStageMs_{};
    bool gpuTimingAvailable_ = false;
    /// Triangles written into the cascade atlas this frame, for the per-
    /// cascade table: a cascade's cost is its draws and its triangles, and
    /// the two do not move together.
    long long shadowTriangles_ = 0;

    std::vector<SceneItem>    items_;
    std::vector<InstanceGroup> groups_;
    std::vector<SceneItem>    dynamic_;
    std::vector<SkinnedItem>  skinned_;
    std::vector<std::size_t>  visibleSkinned_;

    /// Indices into the lists above, refilled every frame by @ref cull.
    std::vector<std::size_t> visibleOpaque_;
    std::vector<std::size_t> visibleTransparent_;
    std::vector<std::size_t> visibleDynamic_;
    std::vector<std::vector<Microsoft::Xna::Framework::Matrix>> visibleGroupTransforms_;
    std::vector<const GpuMesh*> visibleGroupMesh_;

    std::vector<std::unique_ptr<ReflectionProbe>> probes_;
    std::vector<Microsoft::Xna::Framework::Vector3> probePositions_;
    float probeBakeSeconds_ = 0.0f;
    std::function<void(float)> bakeProgress_;
    /// Which environment the effect currently carries, so a run of draws
    /// sharing a probe uploads it once. Reset whenever the lighting is.
    const ReflectionProbe* boundProbe_ = nullptr;
    bool environmentBound_ = false;
    /// The material and probe the last applyMaterial set, for the repeated-
    /// apply count in Stats. Reset with the lighting.
    const Material*        appliedMaterial_ = nullptr;
    const ReflectionProbe* appliedProbe_    = nullptr;
    /// Multiplies the sun, the ambient and the sky while a probe is being
    /// captured into an 8-bit target, and 1 for the frame. See captureProbe.
    float lightScale_ = 1.0f;
    bool  capturingProbe_ = false;

    int  width_  = 1280;
    int  height_ = 720;
    bool sceneSorted_ = false;
    /// Whether this frame renders into the pipeline's float scene target, which
    /// decides who owns the sRGB encode.
    bool usingSceneTarget_ = false;
    mutable bool loggedCascades_ = false;
    std::size_t geometryBytes_ = 0;

    Stats stats_;
    std::vector<std::string> limitations_;

    bool shadowReportEnabled_ = false;
    bool drawTimingEnabled_ = false;
    mutable std::unordered_map<std::string, BatchCost> shadowByName_;

    /// One instanced renderer per mesh, kept across frames. It used to be
    /// built on the stack for every group every frame, and a fresh one has
    /// no instance buffer: its first `setInstances` allocates a
    /// `DynamicVertexBuffer`, uploads into it, and the destructor frees it
    /// at the end of the loop body -- fifty-odd GPU buffer allocations and
    /// frees a frame for transform lists that mostly did not change size.
    /// Kept, the buffer is reused and only grows.
    [[nodiscard]] CNA::Graphics::InstancedRendererEXT& instancedFor(const GpuMesh* mesh);
    std::unordered_map<const GpuMesh*, std::unique_ptr<CNA::Graphics::InstancedRendererEXT>>
        instancedByMesh_;
};

}  // namespace CnaStreet
