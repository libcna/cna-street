// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Render/SkySystem.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class PbrEffect;
}

namespace CNA::Graphics {
    class CascadedShadowMap;
    class DepthNormalPrepass;
    class GpuTimer;
    class RenderPipeline;
}

namespace CnaStreet {

class GpuMesh;
class MaterialLibrary;
struct Material;

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
        std::size_t triangles = 0;
        int postPasses = 0;
        bool usedSceneTarget = false;
        bool drewShadows = false;
        float cullMs = 0.0f;
        float shadowMs = 0.0f;
        float opaqueMs = 0.0f;
        float postMs = 0.0f;
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
                       const Microsoft::Xna::Framework::Matrix& world);

    void render(const Camera& camera, const RenderSettings& settings, float timeSeconds);

    [[nodiscard]] const Stats& stats() const { return stats_; }
    /// A human-readable list of what the renderer could not do, for the overlay.
    [[nodiscard]] const std::vector<std::string>& limitations() const { return limitations_; }
    [[nodiscard]] std::size_t geometryBytes() const { return geometryBytes_; }

    /// Writes the cascade atlas to a PNG. The only way to tell an empty shadow
    /// map from a correct one that is being sampled wrongly, and worth keeping.
    void dumpShadowAtlas(const std::string& path) const;

private:
    void drawOpaque(const Camera& camera, const RenderSettings& settings);
    void drawTransparent(const Camera& camera, const RenderSettings& settings);
    void drawShadows(const Camera& camera, const RenderSettings& settings);
    void drawPrepass(const Camera& camera, const RenderSettings& settings);
    void cull(const Camera& camera, const RenderSettings& settings);
    void applyMaterial(const Material& material, const Microsoft::Xna::Framework::Matrix& world,
                       const Camera& camera, const RenderSettings& settings);
    void applyLighting(const RenderSettings& settings);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    MaterialLibrary& materials_;
    SkySystem sky_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::PbrEffect> effect_;
    std::unique_ptr<CNA::Graphics::RenderPipeline>     pipeline_;
    std::unique_ptr<CNA::Graphics::CascadedShadowMap>  shadows_;
    std::unique_ptr<CNA::Graphics::DepthNormalPrepass> prepass_;
    std::unique_ptr<CNA::Graphics::GpuTimer>           gpuTimer_;

    std::vector<SceneItem>    items_;
    std::vector<InstanceGroup> groups_;
    std::vector<SceneItem>    dynamic_;

    /// Indices into the lists above, refilled every frame by @ref cull.
    std::vector<std::size_t> visibleOpaque_;
    std::vector<std::size_t> visibleTransparent_;
    std::vector<std::size_t> visibleDynamic_;
    std::vector<std::vector<Microsoft::Xna::Framework::Matrix>> visibleGroupTransforms_;
    std::vector<const GpuMesh*> visibleGroupMesh_;

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
