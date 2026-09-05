// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Render/SkySystem.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstddef>
#include <memory>
#include <string>
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
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> prefiltered;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance;
    int prefilteredMips = 5;
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
    float shadowDistance = 0.0f;
    bool  castsShadow   = true;
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
                       const Microsoft::Xna::Framework::Matrix& world, bool shadowOnly = false);
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
    /// Everything that casts, written into the cascade currently open, seen
    /// from @p eye. Shared by the frame's shadow pass and the probe capture.
    void drawCasters(const Microsoft::Xna::Framework::Vector3& eye, float split,
                     float propShadowLimit);
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
    std::unique_ptr<CNA::Graphics::GpuTimer>           gpuTimer_;

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
    /// Which environment the effect currently carries, so a run of draws
    /// sharing a probe uploads it once. Reset whenever the lighting is.
    const ReflectionProbe* boundProbe_ = nullptr;
    bool environmentBound_ = false;
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
};

}  // namespace CnaStreet
