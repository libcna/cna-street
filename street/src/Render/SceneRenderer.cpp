// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/SceneRenderer.hpp"

#include "CnaStreet/Render/GpuMesh.hpp"
#include "CnaStreet/Render/SkinnedGpuMesh.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Graphics/TransparencyMode.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/Diagnostics/Stopwatch.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_map>
#include <cmath>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::GpuTimer;
using CNA::Graphics::InstancedRendererEXT;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;
using CNA::Graphics::TransparencyMode;
using System::Diagnostics::Stopwatch;

namespace CnaStreet {

namespace {

ShadowQuality ToShadowQuality(int level)
{
    switch (std::clamp(level, 0, 3))
    {
        case 0:  return ShadowQuality::Low;
        case 1:  return ShadowQuality::Medium;
        case 2:  return ShadowQuality::High;
        default: return ShadowQuality::Ultra;
    }
}

TonemappingMode ToTonemappingMode(int mode)
{
    switch (std::clamp(mode, 0, 3))
    {
        case 0:  return TonemappingMode::None;
        case 1:  return TonemappingMode::Reinhard;
        case 2:  return TonemappingMode::Aces;
        default: return TonemappingMode::Filmic;
    }
}

RenderQuality ToRenderQuality(int shadowQuality)
{
    switch (std::clamp(shadowQuality, 0, 3))
    {
        case 0:  return RenderQuality::Low;
        case 1:  return RenderQuality::Medium;
        case 2:  return RenderQuality::High;
        default: return RenderQuality::Ultra;
    }
}

/// .NET ticks are 100 ns, so this is what turns a Stopwatch reading into the
/// milliseconds the overlay shows.
float Milliseconds(const Stopwatch& watch)
{
    return static_cast<float>(watch.getElapsedTicksProperty()) / 10000.0f;
}

float DistanceToBox(const Vector3& point, const BoundingBox& box)
{
    const float dx = std::max({box.Min.X - point.X, 0.0f, point.X - box.Max.X});
    const float dy = std::max({box.Min.Y - point.Y, 0.0f, point.Y - box.Max.Y});
    const float dz = std::max({box.Min.Z - point.Z, 0.0f, point.Z - box.Max.Z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

BoundingBox TransformBox(const BoundingBox& box, const Matrix& transform)
{
    // The eight corners transformed and re-bounded. A cheaper centre+extent
    // method exists, but it is only correct for an axis-aligned rotation and
    // this runs once per object at build time, not per frame.
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

Vector3 BoxCentre(const BoundingBox& box)
{
    return Vector3((box.Min.X + box.Max.X) * 0.5f, (box.Min.Y + box.Max.Y) * 0.5f,
                   (box.Min.Z + box.Max.Z) * 0.5f);
}

/// sRGB byte to linear, for reading a probe capture back.
float DecodeSrgb(int byte)
{
    const float s = static_cast<float>(byte) / 255.0f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

/// Linear radiance to the 8-bit cube texel the sky cube uses: `L / scale`,
/// quantised. The same function `SkySystem` encodes with, so a probe's texels
/// and the sky's mean the same thing under one `ImageBasedLightEXT::Intensity`.
std::uint8_t EncodeCube(float linear, float scale)
{
    const float mapped = linear / std::max(scale, 1e-4f);
    return static_cast<std::uint8_t>(std::lround(std::clamp(mapped, 0.0f, 1.0f) * 255.0f));
}

/// The camera that looks out of one cube face. `forward` is the face's axis;
/// `up` is chosen so that the rendered image's rows run the way the face's
/// texel rows do in `SkySystem::cubeDirection`. The columns come out mirrored
/// -- a cube map is addressed from inside the cube, a camera image from
/// outside -- and the capture flips them when it copies the face in.
}  // namespace

void SceneRenderer::probeFaceBasis(CubeMapFace face, Vector3& forward, Vector3& up)
{
    switch (face)
    {
        case CubeMapFace::PositiveX: forward = Vector3(1.0f, 0.0f, 0.0f);  up = Vector3(0.0f, 1.0f, 0.0f);  return;
        case CubeMapFace::NegativeX: forward = Vector3(-1.0f, 0.0f, 0.0f); up = Vector3(0.0f, 1.0f, 0.0f);  return;
        case CubeMapFace::PositiveY: forward = Vector3(0.0f, 1.0f, 0.0f);  up = Vector3(0.0f, 0.0f, -1.0f); return;
        case CubeMapFace::NegativeY: forward = Vector3(0.0f, -1.0f, 0.0f); up = Vector3(0.0f, 0.0f, 1.0f);  return;
        case CubeMapFace::PositiveZ: forward = Vector3(0.0f, 0.0f, 1.0f);  up = Vector3(0.0f, 1.0f, 0.0f);  return;
        case CubeMapFace::NegativeZ: forward = Vector3(0.0f, 0.0f, -1.0f); up = Vector3(0.0f, 1.0f, 0.0f);  return;
    }
    forward = Vector3(0.0f, 0.0f, 1.0f);
    up      = Vector3(0.0f, 1.0f, 0.0f);
}

SceneRenderer::SceneRenderer(GraphicsDevice& device, MaterialLibrary& materials)
    : device_(device), materials_(materials), sky_(device)
{
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::initialise(const RenderSettings& settings)
{
    limitations_.clear();

    if (!device_.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        limitations_.emplace_back("the renderer has no 3D pipeline; nothing will be drawn");

    effect_ = std::make_unique<PbrEffect>(device_);

    // The skinned sibling of the same material model, for the pedestrians.
    // Probed like everything else: a renderer with a PbrEffect but no skinning
    // shader would otherwise draw every character in its bind pose at the
    // origin, which is a far worse failure than not drawing them.
    try
    {
        skinnedEffect_ = std::make_unique<SkinnedPbrEffect>(device_);
        skinnedEffect_->setWeightsPerVertexProperty(4);
    }
    catch (const std::exception& failure)
    {
        limitations_.emplace_back(std::string("no GPU skinning: ") + failure.what());
        skinnedEffect_.reset();
    }

    pipeline_ = std::make_unique<RenderPipeline>(device_);
    pipeline_->resize(std::max(1, width_), std::max(1, height_));

    if (settings.shadows)
    {
        if (device_.SupportsShadowSamplingEXT())
        {
            // Two is the floor, not one: CascadedShadowMap requires 2..4 and
            // throws otherwise, and a settings file asking for one cascade
            // should cost a cascade rather than the whole street.
            try
            {
                shadows_ = std::make_unique<CascadedShadowMap>(
                    device_, ToShadowQuality(settings.shadowQuality),
                    std::clamp(settings.shadowCascades, 2, CascadedShadowMap::kMaxCascades));
            }
            catch (const std::exception& failure)
            {
                limitations_.emplace_back(std::string("no shadow map: ") + failure.what());
                shadows_.reset();
            }
            if (shadows_ != nullptr && !shadows_->isSupported())
            {
                limitations_.emplace_back("cascaded shadow maps are unavailable on this renderer");
                shadows_.reset();
            }
        }
        else
        {
            limitations_.emplace_back("the renderer cannot sample a shadow map");
        }
    }

    if (settings.ssao)
    {
        prepass_ = std::make_unique<DepthNormalPrepass>(device_, std::max(1, width_),
                                                        std::max(1, height_));
        if (prepass_->getPrepassEffect() == nullptr
            || !prepass_->getPrepassEffect()->IsEffectValid())
        {
            limitations_.emplace_back("the depth/normal prepass did not compile, so SSAO is off");
            prepass_.reset();
        }
    }

    gpuTimer_ = std::make_unique<GpuTimer>(device_);
    if (!gpuTimer_->isSupported())
    {
        // Not a limitation worth showing the user: the overlay simply reports
        // CPU time only.
        CNA::Logger::Info("cna-street: GPU timing unavailable -- "
                          + gpuTimer_->getUnsupportedReason());
        gpuTimer_.reset();
    }

    if (!device_.SupportsCapability(CNA::GraphicsCapability::Instancing))
        limitations_.emplace_back("no hardware instancing; repeated props fall back to a loop");
    if (!device_.SupportsCapability(CNA::GraphicsCapability::FloatRenderTargets)
        && !device_.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets))
        limitations_.emplace_back("no float render targets; HDR resolves straight to the back "
                                  "buffer");

    sky_.build(settings);
    if (!sky_.isSupported()) limitations_.emplace_back(sky_.unsupportedReason());
    if (!sky_.hasImageBasedLighting())
        limitations_.emplace_back("no image based lighting; ambient is a hemisphere term");

    applySettings(settings);

    for (const std::string& limitation : limitations_)
        CNA::Logger::Warn("cna-street: " + limitation);
}

void SceneRenderer::applySettings(const RenderSettings& settings)
{
    if (pipeline_ == nullptr) return;
    auto& p = pipeline_->getSettings();

    p.setHDREnabled(settings.hdr);
    p.setExposure(settings.exposure);
    p.setTonemappingMode(ToTonemappingMode(settings.tonemap));
    p.setRenderQuality(ToRenderQuality(settings.shadowQuality));

    p.setBloomEnabled(settings.bloom);
    p.setBloomIntensity(settings.bloomIntensity);
    p.setBloomThreshold(settings.bloomThreshold);

    p.setSSAOEnabled(settings.ssao && prepass_ != nullptr);
    p.setSSAORadius(settings.ssaoRadius);
    p.setSSAOIntensity(settings.ssaoIntensity);

    p.setFXAAEnabled(settings.fxaa);
    p.setSSREnabled(settings.ssr);
    p.setDOFEnabled(settings.depthOfField);

    p.setHeightFogDensity(settings.heightFog ? settings.fogDensity : 0.0f);
    p.setHeightFogFalloff(settings.fogFalloff);
    p.setHeightFogBaseHeight(0.0f);

    // Light shafts, tuned to be shafts rather than ghosts. The pass has a fixed
    // sample count, so a decay of 0.965 spreads those samples over most of the
    // screen and each one lands as a discrete smeared copy of whatever seeded
    // it: at 0.82 the threshold caught the bright window frames of a sunlit
    // façade, and a row of ghost windows climbed diagonally across the sky. The
    // sun disc and the hottest speculars are what should seed a shaft, and the
    // shaft should be short enough for the samples to overlap.
    p.setLightShaftIntensity(settings.lightShafts ? 0.32f : 0.0f);
    p.setLightShaftThreshold(0.995f);
    p.setLightShaftDecay(0.86f);

    // Sorted rather than order-independent: the transparent set here iswindow glass
    // and car glazing, which is convex, sparse and already sorted well by
    // distance. Weighted-blended would cost two more targets to solve a problem
    // this scene does not have.
    p.setTransparencyMode(TransparencyMode::Sorted);
    p.setShadowsEnabled(settings.shadows && shadows_ != nullptr);
    p.setShadowQuality(ToShadowQuality(settings.shadowQuality));

    if (shadows_ != nullptr)
    {
        shadows_->setSplitLambda(settings.shadowSplitLambda);
        shadows_->setBlendBand(settings.shadowBlendBand);
        shadows_->setDebugTintEnabled(settings.shadowDebugTint);
    }
}

void SceneRenderer::resize(int width, int height)
{
    width_  = std::max(1, width);
    height_ = std::max(1, height);
    if (pipeline_ != nullptr) pipeline_->resize(width_, height_);
    if (prepass_ != nullptr)
    {
        prepass_ = std::make_unique<DepthNormalPrepass>(device_, width_, height_);
        if (prepass_->getPrepassEffect() == nullptr
            || !prepass_->getPrepassEffect()->IsEffectValid())
            prepass_.reset();
    }
}

void SceneRenderer::addItem(SceneItem item)
{
    if (item.mesh == nullptr || item.material == nullptr) return;
    item.worldBounds = TransformBox(item.mesh->bounds(), item.world);
    item.worldSphere = BoundingSphere::CreateFromBoundingBox(item.worldBounds);
    geometryBytes_ += item.mesh->gpuBytes();
    items_.push_back(item);
    sceneSorted_ = false;
}

void SceneRenderer::addInstances(InstanceGroup group)
{
    if (group.mesh == nullptr || group.material == nullptr || group.transforms.empty()) return;
    group.spheres.clear();
    group.spheres.reserve(group.transforms.size());
    for (const Matrix& transform : group.transforms)
        group.spheres.push_back(
            BoundingSphere::CreateFromBoundingBox(TransformBox(group.mesh->bounds(), transform)));
    geometryBytes_ += group.mesh->gpuBytes();
    groups_.push_back(std::move(group));
}

void SceneRenderer::clearScene()
{
    items_.clear();
    groups_.clear();
    dynamic_.clear();
    geometryBytes_ = 0;
}

void SceneRenderer::beginFrame()
{
    dynamic_.clear();
    skinned_.clear();
}

void SceneRenderer::submitDynamic(const GpuMesh* mesh, const Material* material,
                                  const Matrix& world, bool shadowOnly)
{
    if (mesh == nullptr || material == nullptr) return;
    SceneItem item;
    item.shadowOnly  = shadowOnly;
    item.mesh        = mesh;
    item.material    = material;
    item.world       = world;
    item.worldBounds = TransformBox(mesh->bounds(), world);
    item.worldSphere = BoundingSphere::CreateFromBoundingBox(item.worldBounds);
    // A mover reflects wherever it happens to be this frame.
    item.probe       = shadowOnly ? nullptr : nearestProbe(BoxCentre(item.worldBounds));
    dynamic_.push_back(item);
}

void SceneRenderer::submitSkinned(SkinnedItem item)
{
    if (item.mesh == nullptr || item.material == nullptr || item.bones == nullptr) return;
    item.worldSphere = BoundingSphere::CreateFromBoundingBox(
        TransformBox(item.mesh->bounds(), item.world));
    skinned_.push_back(std::move(item));
}

void SceneRenderer::cull(const Camera& camera, const RenderSettings& settings)
{
    if (!sceneSorted_)
    {
        // Sort by material so that a run of draws shares its textures and
        // uniforms. The pointer identity is the sort key, which is stable within
        // a run and is all a batching sort needs.
        std::stable_sort(items_.begin(), items_.end(),
                         [](const SceneItem& a, const SceneItem& b) {
                             return a.material < b.material;
                         });
        sceneSorted_ = true;
    }

    const BoundingFrustum& frustum = camera.frustum();
    const Vector3& eye = camera.position();

    visibleOpaque_.clear();
    visibleTransparent_.clear();
    visibleDynamic_.clear();
    visibleSkinned_.clear();
    for (std::size_t i = 0; i < skinned_.size(); ++i)
    {
        const SkinnedItem& item = skinned_[i];
        // Skinned items are people, and people have a cull distance of their
        // own: each one costs three draw calls with a bone palette apiece and
        // cannot be instanced, so the far end of the street is the most
        // expensive place in the scene to populate. CityScene applies the same
        // limit when it submits; this is the backstop.
        if (Vector3::Distance(eye, item.worldSphere.Center) - item.worldSphere.Radius
            > settings.pedestrianCullDistance)
            continue;
        if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
        visibleSkinned_.push_back(i);
    }
    stats_.visibleCharacters = static_cast<int>(visibleSkinned_.size());
    stats_.visibleItems = 0;
    stats_.totalItems = static_cast<int>(items_.size() + dynamic_.size());

    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const SceneItem& item = items_[i];
        const float limit = item.cullDistance > 0.0f
                                ? std::min(item.cullDistance, settings.propCullDistance)
                                : 0.0f;
        if (limit > 0.0f && DistanceToBox(eye, item.worldBounds) > limit) continue;
        if (frustum.Contains(item.worldBounds) == ContainmentType::Disjoint) continue;
        if (item.material->isBlended()) visibleTransparent_.push_back(i);
        else                            visibleOpaque_.push_back(i);
        ++stats_.visibleItems;
    }

    // Movers are culled by distance as well as by frustum. A person at 200 m is
    // three pixels tall and costs the same three draw calls as a person at
    // three metres; without this the population of the street is bounded by
    // what the far end of it can afford rather than by what the near end needs.
    const float moverDistance = settings.propCullDistance > 0.0f
                                    ? settings.propCullDistance
                                    : 0.0f;
    for (std::size_t i = 0; i < dynamic_.size(); ++i)
    {
        if (dynamic_[i].shadowOnly) continue;
        const SceneItem& item = dynamic_[i];
        if (moverDistance > 0.0f && DistanceToBox(eye, item.worldBounds) > moverDistance) continue;
        if (frustum.Contains(item.worldBounds) == ContainmentType::Disjoint) continue;
        visibleDynamic_.push_back(i);
        ++stats_.visibleItems;
    }

    // Transparent surfaces are drawn far to near, so blending composites in the
    // right order without a depth pre-sort.
    std::sort(visibleTransparent_.begin(), visibleTransparent_.end(),
              [&](std::size_t a, std::size_t b) {
                  return DistanceToBox(eye, items_[a].worldBounds)
                         > DistanceToBox(eye, items_[b].worldBounds);
              });

    visibleGroupTransforms_.resize(groups_.size());
    visibleGroupMesh_.resize(groups_.size());
    stats_.visibleInstances = 0;
    stats_.totalInstances = 0;
    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        const InstanceGroup& group = groups_[g];
        std::vector<Matrix>& visible = visibleGroupTransforms_[g];
        visible.clear();
        stats_.totalInstances += static_cast<int>(group.transforms.size());
        visibleGroupMesh_[g] = group.mesh;

        const float limit = group.cullDistance > 0.0f
                                ? std::min(group.cullDistance, settings.propCullDistance)
                                : settings.propCullDistance;
        float nearest = 1e30f;
        for (std::size_t i = 0; i < group.transforms.size(); ++i)
        {
            const BoundingSphere& sphere = group.spheres[i];
            const float distance = Vector3::Distance(eye, sphere.Center) - sphere.Radius;
            if (distance > limit) continue;
            if (frustum.Contains(sphere) == ContainmentType::Disjoint) continue;
            visible.push_back(group.transforms[i]);
            nearest = std::min(nearest, distance);
        }
        // One LOD decision for the whole batch rather than per instance: a batch
        // is a single draw, and splitting it in two to give the near half more
        // triangles costs more than the triangles saved.
        if (group.lodMesh != nullptr && group.lodDistance > 0.0f && nearest > group.lodDistance)
            visibleGroupMesh_[g] = group.lodMesh;
        stats_.visibleInstances += static_cast<int>(visible.size());
    }

}

void SceneRenderer::applyLighting(const RenderSettings& settings)
{
    PbrEffect& effect = *effect_;
    effect.setLightingEnabledProperty(true);

    auto& sun = effect.getDirectionalLight0Property();
    sun.setEnabledProperty(true);
    const Vector3 direction = sky_.lightDirection();
    sun.setDirectionProperty(direction);
    sun.setDiffuseColorProperty(sky_.sunColour() * lightScale_);
    sun.setSpecularColorProperty(sky_.sunColour() * lightScale_);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);

    effect.setAmbientLightColorProperty(sky_.ambientColour() * lightScale_);

    // The sky's environment to start with; a draw with a probe swaps its own in.
    environmentBound_ = false;
    applyEnvironment(nullptr, settings);

    // Distance fog matched to the sky's own horizon haze, so the far end of the
    // street fades into the same colour the sky is that direction rather than
    // into a fog colour picked by hand. Not while a probe is being captured:
    // the capture is a picture of the surfaces, and fogging them would put
    // haze into every reflection at every distance.
    if (settings.heightFog && !capturingProbe_)
    {
        effect.setFogEnabledProperty(true);
        const Vector3 haze = sky_.ambientColour();
        effect.setFogColorProperty(haze);
        effect.setFogStartProperty(settings.farPlane * 0.30f);
        effect.setFogEndProperty(settings.farPlane * 0.98f);
    }
    else
    {
        effect.setFogEnabledProperty(false);
    }

    if (shadows_ != nullptr && settings.shadows)
    {
        shadows_->applyToReceiver(effect);
        effect.setShadowsEnabledEXT(true);
        effect.setShadowDepthBiasEXT(settings.shadowDepthBias);
    }
    else
    {
        effect.setShadowsEnabledEXT(false);
    }
}

void SceneRenderer::applyEnvironment(const ReflectionProbe* probe, const RenderSettings& settings)
{
    if (environmentBound_ && probe == boundProbe_) return;
    environmentBound_ = true;
    boundProbe_       = probe;

    PbrEffect& effect = *effect_;
    if (!sky_.hasImageBasedLighting() || !settings.imageBasedLighting)
    {
        effect.setImageBasedLightEXT(ImageBasedLightEXT{});
        return;
    }

    ImageBasedLightEXT environment;
    // The sky's products unless the probe has its own. Both cubes are stored
    // at the sky's scale, so one Intensity serves whichever fills each slot.
    const bool local = probe != nullptr && probe->prefiltered != nullptr;
    environment.PrefilteredSpecular = local ? probe->prefiltered.get() : sky_.prefiltered();
    environment.PrefilteredMipCount = local ? probe->prefilteredMips : sky_.prefilteredMipCount();
    environment.Irradiance = local && settings.probeIrradiance && probe->irradiance != nullptr
                                 ? probe->irradiance.get()
                                 : sky_.irradiance();
    environment.BrdfLut = sky_.brdfLut();
    // The cube holds radiance divided by this; the effect multiplies it back.
    // Leaving it at 1 is what makes an HDR sky light a scene as if it were a
    // photograph of one.
    environment.Intensity = sky_.environmentScale() * settings.iblIntensity * lightScale_;
    effect.setImageBasedLightEXT(environment);
}

void SceneRenderer::applyMaterial(const Material& material, const Matrix& world,
                                  const Matrix& view, const Matrix& projection,
                                  const RenderSettings& settings, const ReflectionProbe* probe)
{
    PbrEffect& effect = *effect_;

    effect.setWorldProperty(world);
    effect.setViewProperty(view);
    effect.setProjectionProperty(projection);

    // A room is not lit by the sun. Switching the light off for the draw, rather
    // than leaving it to the shadow map, is what keeps the cascade atlas's
    // 8-bit acne off the back wall of every shop; the ambient and the baked
    // emissive are the whole of an interior's light, as they should be.
    const Vector3 sunColour = material.sunlit ? sky_.sunColour() * lightScale_ : Vector3::Zero;
    auto& sun = effect.getDirectionalLight0Property();
    sun.setDiffuseColorProperty(sunColour);
    sun.setSpecularColorProperty(sunColour);

    applyEnvironment(probe, settings);

    effect.setDiffuseColorProperty(material.baseColour);
    effect.setAlphaProperty(material.alpha);
    effect.setMetallicFactorProperty(material.metallic);
    effect.setRoughnessFactorProperty(material.roughness);
    effect.setEmissiveFactorProperty(material.emissiveFactor);

    effect.setTextureProperty(material.albedo);
    effect.setNormalMapProperty(material.normal);
    effect.setMetallicRoughnessMapProperty(material.orm);
    effect.setOcclusionMapProperty(material.orm);
    effect.setEmissiveMapProperty(material.emissive);

    // Base colour and emissive are authored as sRGB; the normal map and the ORM
    // map are not, and telling the effect otherwise would gamma-decode a
    // roughness value.
    effect.setBaseColorTextureIsSrgbEXTProperty(true);
    effect.setEmissiveTextureIsSrgbEXTProperty(true);
    effect.setNormalScaleEXTProperty(material.normalScale);
    effect.setOcclusionStrengthEXTProperty(material.occlusionStrength);
    effect.setIorEXTProperty(material.ior);
    effect.setSpecularFactorEXTProperty(material.specular);

    // PbrEffect sRGB-encodes its own output by default, which is right when a
    // shaded fragment lands straight in the back buffer and badly wrong when it
    // lands in a float scene target that a tonemapper will read as linear
    // radiance: every dark surface is lifted (asphalt at 0.05 becomes 0.25) and
    // the whole frame flattens. Encode only when nothing downstream will.
    // See docs/cna-findings.md CNA-F8.
    effect.setEncodeOutputToSrgbEXTProperty(!usingSceneTarget_);

    effect.setAlphaModeEXTProperty(material.alphaMode);
    effect.setAlphaCutoffEXTProperty(material.alphaCutoff);
    effect.setDoubleSidedEXTProperty(material.doubleSided);

    // KHR_texture_transform on every slot: the mesh UVs already carry the
    // physical tiling, so this is atlas addressing — which cell of the interior
    // atlas this particular window looks into.
    TextureTransformEXT transform;
    transform.Scale  = material.uvScale;
    transform.Offset = material.uvOffset;
    for (int slot = 0; slot < 5; ++slot) effect.setTextureTransformEXTProperty(slot, transform);

    device_.setRasterizerStateProperty(material.doubleSided
                                           ? RasterizerState::CullNone
                                           : RasterizerState::CullCounterClockwise);
    // Glass composites as reflection plus attenuated background; everything
    // else that blends is a coloured filter. See Material::premultipliedBlend.
    if (material.isBlended())
        device_.setBlendStateProperty(material.premultipliedBlend ? BlendState::AlphaBlend
                                                                  : BlendState::NonPremultiplied);
    effect.Apply();
}

void SceneRenderer::drawShadows(const Camera& camera, const RenderSettings& settings)
{
    stats_.drewShadows = false;
    if (shadows_ == nullptr || !settings.shadows) return;

    DirectionalLightEXT light;
    light.Direction = sky_.lightDirection();
    light.Color     = sky_.sunColour();

    // Fit the cascades to a *shorter* frustum than the camera's: a 620 m far
    // plane would spread the cascades over ground nobody can resolve a shadow
    // on, and the near cascade is what the eye actually judges.
    const Matrix shadowProjection =
        camera.projectionForRange(settings.nearPlane, std::min(settings.shadowDistance,
                                                               settings.farPlane));
    shadows_->update(light, camera.view(), shadowProjection);

    ShaderEffect* caster = shadows_->getCasterEffect();
    if (caster == nullptr) return;

    if (!loggedCascades_)
    {
        loggedCascades_ = true;
        std::string splits;
        for (int i = 0; i < shadows_->getCascadeCount(); ++i)
            splits += std::to_string(shadows_->getSplitDistance(i)) + " ";
        CNA::Logger::Info("cna-street: shadow cascades " + std::to_string(shadows_->getCascadeCount())
                          + " at " + std::to_string(shadows_->getCascadeSize()) + " px, splits "
                          + splits);
    }

    const Vector3& eye = camera.position();
    const float propShadowLimit = settings.propShadowDistance;

    // State first, then the pass: CNA's own cascade example sets the render
    // states before begin(), and applying the caster effect is the last thing
    // begin() does.
    device_.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);

    for (int cascade = 0; cascade < shadows_->getCascadeCount(); ++cascade)
    {
        shadows_->begin(cascade);
        // Each cascade covers a distance band; anything past its split is drawn
        // by a coarser cascade and would only cost fill here.
        drawCasters(eye, shadows_->getSplitDistance(cascade), propShadowLimit);
        shadows_->end();
    }

    stats_.drewShadows = stats_.shadowDrawCalls > 0;
}

void SceneRenderer::drawCasters(const Vector3& eye, float split, float propShadowLimit)
{
    ShaderEffect* caster = shadows_->getCasterEffect();
    if (caster == nullptr) return;

    for (const SceneItem& item : items_)
    {
        if (!item.material->castsShadow) continue;
        const float distance = DistanceToBox(eye, item.worldBounds);
        if (distance > split + item.worldSphere.Radius) continue;
        if (item.shadowDistance > 0.0f && distance > item.shadowDistance) continue;
        caster->SetUniformMat4("uWorld", &item.world.M11);
        item.mesh->draw(device_);
        ++stats_.shadowDrawCalls;
    }

    for (const SceneItem& item : dynamic_)
    {
        if (!item.material->castsShadow) continue;
        const float distance = DistanceToBox(eye, item.worldBounds);
        if (distance > split + item.worldSphere.Radius) continue;
        caster->SetUniformMat4("uWorld", &item.world.M11);
        item.mesh->draw(device_);
        ++stats_.shadowDrawCalls;
    }

    // Instanced props are drawn one at a time here. CNA's shadow caster
    // program takes its world matrix from a uniform and has no instanced
    // variant, so a batch cannot be cast in one call; see
    // docs/cna-findings.md CNA-F6. The distance limit keeps that honest:
    // past ~70 m a bollard's shadow is a pixel.
    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        const InstanceGroup& group = groups_[g];
        if (!group.castsShadow || !group.material->castsShadow) continue;
        const float limit = std::min(group.shadowDistance > 0.0f ? group.shadowDistance
                                                                 : propShadowLimit,
                                     split);
        for (std::size_t i = 0; i < group.transforms.size(); ++i)
        {
            const BoundingSphere& sphere = group.spheres[i];
            if (Vector3::Distance(eye, sphere.Center) - sphere.Radius > limit) continue;
            caster->SetUniformMat4("uWorld", &group.transforms[i].M11);
            group.mesh->draw(device_);
            ++stats_.shadowDrawCalls;
        }
    }
}

void SceneRenderer::drawSkinned(const Camera& camera, const RenderSettings& settings)
{
    if (skinnedEffect_ == nullptr || visibleSkinned_.empty()) return;
    SkinnedPbrEffect& effect = *skinnedEffect_;

    // The same lighting the rest of the frame has. Set once, because the whole
    // crowd shares it and a per-character upload of the environment cube would
    // be the single most expensive thing in the pass.
    effect.setLightingEnabledProperty(true);
    auto& sun = effect.getDirectionalLight0Property();
    sun.setEnabledProperty(true);
    sun.setDirectionProperty(sky_.lightDirection());
    sun.setDiffuseColorProperty(sky_.sunColour());
    sun.setSpecularColorProperty(sky_.sunColour());
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setAmbientLightColorProperty(sky_.ambientColour());
    if (sky_.hasImageBasedLighting() && settings.imageBasedLighting)
    {
        ImageBasedLightEXT environment;
        environment.Irradiance          = sky_.irradiance();
        environment.PrefilteredSpecular = sky_.prefiltered();
        environment.BrdfLut             = sky_.brdfLut();
        environment.PrefilteredMipCount = sky_.prefilteredMipCount();
        environment.Intensity           = sky_.environmentScale() * settings.iblIntensity;
        effect.setImageBasedLightEXT(environment);
    }
    if (settings.heightFog)
    {
        effect.setFogEnabledProperty(true);
        effect.setFogColorProperty(sky_.ambientColour());
        effect.setFogStartProperty(settings.farPlane * 0.30f);
        effect.setFogEndProperty(settings.farPlane * 0.98f);
    }
    else
    {
        effect.setFogEnabledProperty(false);
    }
    if (shadows_ != nullptr && settings.shadows)
    {
        shadows_->applyToReceiver(effect);
        effect.setShadowsEnabledEXT(true);
        effect.setShadowDepthBiasEXT(settings.shadowDepthBias);
    }
    else
    {
        effect.setShadowsEnabledEXT(false);
    }
    effect.setViewProperty(camera.view());
    effect.setProjectionProperty(camera.projection());
    effect.setEncodeOutputToSrgbEXTProperty(!usingSceneTarget_);
    effect.setBaseColorTextureIsSrgbEXTProperty(true);
    effect.setEmissiveTextureIsSrgbEXTProperty(true);

    device_.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);

    for (std::size_t index : visibleSkinned_)
    {
        const SkinnedItem& item = skinned_[index];
        const Material& material = *item.material;
        effect.setWorldProperty(item.world);
        effect.setDiffuseColorProperty(material.baseColour);
        effect.setAlphaProperty(material.alpha);
        effect.setMetallicFactorProperty(material.metallic);
        effect.setRoughnessFactorProperty(material.roughness);
        effect.setEmissiveFactorProperty(material.emissiveFactor);
        effect.setTextureProperty(material.albedo);
        effect.setNormalMapProperty(material.normal);
        effect.setMetallicRoughnessMapProperty(material.orm);
        effect.setOcclusionMapProperty(material.orm);
        effect.setEmissiveMapProperty(material.emissive);
        effect.setNormalScaleEXTProperty(material.normalScale);
        effect.setOcclusionStrengthEXTProperty(material.occlusionStrength);
        effect.setIorEXTProperty(material.ior);
        effect.setSpecularFactorEXTProperty(material.specular);
        effect.setAlphaModeEXTProperty(material.alphaMode);
        effect.setAlphaCutoffEXTProperty(material.alphaCutoff);
        effect.setDoubleSidedEXTProperty(material.doubleSided);
        effect.SetBoneTransforms(*item.bones);
        effect.Apply();
        item.mesh->draw(device_);
        ++stats_.skinnedDrawCalls;
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }
}

void SceneRenderer::drawPrepass(const Camera& camera, const RenderSettings& settings)
{
    if (prepass_ == nullptr || pipeline_ == nullptr || !settings.ssao) return;

    ShaderEffect* prepassEffect = prepass_->getPrepassEffect();
    if (prepassEffect == nullptr || !prepassEffect->IsEffectValid()) return;

    for (int pass = 0; pass < prepass_->getPassCount(); ++pass)
    {
        prepass_->begin(pass, camera.view(), camera.projection(), settings.nearPlane,
                        settings.farPlane);
        device_.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        device_.setBlendStateProperty(BlendState::Opaque);

        // Only the large opaque surfaces. The prepass program takes its world
        // matrix from a uniform, so instanced props would all land at the
        // origin; SSAO's job here is the contact darkening where façades meet
        // the footway, which these carry.
        for (std::size_t index : visibleOpaque_)
        {
            const SceneItem& item = items_[index];
            if (!item.material->writesDepth) continue;
            prepassEffect->setWorldProperty(item.world);
            prepassEffect->Apply();
            item.mesh->draw(device_);
        }
        prepass_->end();
    }
    pipeline_->setDepthNormalInputs(prepass_->getDepthTexture(), prepass_->getNormalTexture());
}

void SceneRenderer::drawOpaque(const Camera& camera, const RenderSettings& settings)
{
    usingSceneTarget_ = pipeline_ != nullptr && pipeline_->isUsingSceneTarget();
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
    applyLighting(settings);

    const Matrix& view = camera.view();
    const Matrix& projection = camera.projection();
    for (std::size_t index : visibleOpaque_)
    {
        const SceneItem& item = items_[index];
        applyMaterial(*item.material, item.world, view, projection, settings, item.probe);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }

    for (std::size_t index : visibleDynamic_)
    {
        const SceneItem& item = dynamic_[index];
        if (item.material->isBlended()) continue;
        applyMaterial(*item.material, item.world, view, projection, settings, item.probe);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }

    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        const std::vector<Matrix>& visible = visibleGroupTransforms_[g];
        if (visible.empty()) continue;
        const InstanceGroup& group = groups_[g];
        if (group.material->isBlended()) continue;
        const GpuMesh* mesh = visibleGroupMesh_[g];
        if (mesh == nullptr) continue;

        applyMaterial(*group.material, Matrix::getIdentityProperty(), view, projection, settings,
                      nullptr);
        InstancedRendererEXT instanced(device_, mesh->part());
        instanced.setFallbackEnabled(true);
        instanced.setInstances(visible);
        instanced.draw(*effect_);
        stats_.instancedDrawCalls += instanced.getLastDrawCallCount();
        stats_.drawCalls += instanced.getLastDrawCallCount();
        stats_.triangles += static_cast<std::size_t>(mesh->triangleCount()) * visible.size();
    }

    // The crowd, last among the opaque draws: a different effect, so it costs
    // one program switch rather than one per person interleaved with the rest.
    drawSkinned(camera, settings);
}

void SceneRenderer::drawTransparent(const Camera& camera, const RenderSettings& settings)
{
    applyLighting(settings);
    const Matrix& view = camera.view();
    const Matrix& projection = camera.projection();
    for (std::size_t index : visibleTransparent_)
    {
        const SceneItem& item = items_[index];
        applyMaterial(*item.material, item.world, view, projection, settings, item.probe);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }
    for (std::size_t index : visibleDynamic_)
    {
        const SceneItem& item = dynamic_[index];
        if (!item.material->isBlended()) continue;
        applyMaterial(*item.material, item.world, view, projection, settings, item.probe);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }
    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        const std::vector<Matrix>& visible = visibleGroupTransforms_[g];
        if (visible.empty()) continue;
        const InstanceGroup& group = groups_[g];
        if (!group.material->isBlended()) continue;
        const GpuMesh* mesh = visibleGroupMesh_[g];
        if (mesh == nullptr) continue;
        applyMaterial(*group.material, Matrix::getIdentityProperty(), view, projection, settings,
                      nullptr);
        InstancedRendererEXT instanced(device_, mesh->part());
        instanced.setFallbackEnabled(true);
        instanced.setInstances(visible);
        instanced.draw(*effect_);
        stats_.drawCalls += instanced.getLastDrawCallCount();
    }
}

void SceneRenderer::dumpShadowAtlas(const std::string& path) const
{
    if (shadows_ == nullptr) { CNA::Logger::Warn("cna-street: no shadow map to dump"); return; }
    Texture2D* atlas = shadows_->getShadowTexture();
    if (atlas == nullptr) { CNA::Logger::Warn("cna-street: the shadow atlas is null"); return; }
    // Read it back rather than SaveAsPng: a render target holds no CPU-side copy
    // of what the GPU wrote into it, so the direct save reports there is nothing
    // to write.
    const int width = atlas->getWidthProperty();
    const int height = atlas->getHeightProperty();
    std::vector<Color> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                              Color::Black);
    atlas->GetData(pixels.data(), static_cast<int>(pixels.size()));

    int darkest = 255, brightest = 0;
    double mean = 0.0;
    for (const Color& pixel : pixels)
    {
        const int red = static_cast<int>(pixel.getRProperty());
        darkest = std::min(darkest, red);
        brightest = std::max(brightest, red);
        mean += static_cast<double>(red);
    }
    mean /= static_cast<double>(pixels.size());

    std::vector<std::uint8_t> rgba(pixels.size() * 4u);
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
        rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
        rgba[i * 4 + 3] = 255;
    }
    Texture2D copy = Texture2D::CreateFromPixels(device_, width, height, rgba);
    copy.SaveAsPng(path);
    CNA::Logger::Info("cna-street: shadow atlas " + std::to_string(width) + "x"
                      + std::to_string(height) + " -> " + path + "  range "
                      + std::to_string(darkest) + ".." + std::to_string(brightest) + ", mean "
                      + std::to_string(mean));
}

// ---------------------------------------------------------------------------
// Reflection probes
// ---------------------------------------------------------------------------

const ReflectionProbe* SceneRenderer::nearestProbe(const Vector3& at) const
{
    const ReflectionProbe* best = nullptr;
    float bestDistance = 1e30f;
    for (const auto& probe : probes_)
    {
        // Horizontal distance only: every probe stands at eye height, and a
        // window on the fourth floor is still nearest the probe below it.
        const float dx = probe->position.X - at.X;
        const float dz = probe->position.Z - at.Z;
        const float distance = dx * dx + dz * dz;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = probe.get();
        }
    }
    return best;
}

void SceneRenderer::drawProbeFace(const Vector3& eye, const Matrix& view, const Matrix& projection,
                                  int size, const RenderSettings& settings)
{
    const BoundingFrustum frustum(view * projection);

    // The sky first, depth writes off, scaled and encoded the way the geometry
    // will be so the reader decodes both with one curve.
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setBlendStateProperty(BlendState::Opaque);
    sky_.draw(view, projection, size, size, 0.0f, lightScale_, true);

    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
    // Into an 8-bit target directly, so the effect's own sRGB encode is wanted.
    usingSceneTarget_ = false;
    applyLighting(settings);

    // What a probe sees is decided from where it stands, not from the camera:
    // a shop interior 34 m away is culled here exactly as it would be from a
    // viewer at the probe.
    auto visibleFrom = [&](const SceneItem& item) {
        if (item.shadowOnly) return false;
        const float limit = item.cullDistance > 0.0f
                                ? std::min(item.cullDistance, settings.propCullDistance)
                                : 0.0f;
        if (limit > 0.0f && DistanceToBox(eye, item.worldBounds) > limit) return false;
        return frustum.Contains(item.worldBounds) != ContainmentType::Disjoint;
    };

    for (const SceneItem& item : items_)
    {
        if (item.material->isBlended() || !visibleFrom(item)) continue;
        applyMaterial(*item.material, item.world, view, projection, settings, nullptr);
        item.mesh->draw(device_);
    }

    std::vector<Matrix> visible;
    for (const InstanceGroup& group : groups_)
    {
        if (group.material->isBlended()) continue;
        const float limit = group.cullDistance > 0.0f
                                ? std::min(group.cullDistance, settings.propCullDistance)
                                : settings.propCullDistance;
        visible.clear();
        float nearest = 1e30f;
        for (std::size_t i = 0; i < group.transforms.size(); ++i)
        {
            const BoundingSphere& sphere = group.spheres[i];
            const float distance = Vector3::Distance(eye, sphere.Center) - sphere.Radius;
            if (distance > limit) continue;
            if (frustum.Contains(sphere) == ContainmentType::Disjoint) continue;
            visible.push_back(group.transforms[i]);
            nearest = std::min(nearest, distance);
        }
        if (visible.empty()) continue;
        const GpuMesh* mesh = group.lodMesh != nullptr && group.lodDistance > 0.0f
                                      && nearest > group.lodDistance
                                  ? group.lodMesh
                                  : group.mesh;
        applyMaterial(*group.material, Matrix::getIdentityProperty(), view, projection, settings,
                      nullptr);
        InstancedRendererEXT instanced(device_, mesh->part());
        instanced.setFallbackEnabled(true);
        instanced.setInstances(visible);
        instanced.draw(*effect_);
    }

    // The glass, over the top. It reflects the sky here rather than a probe --
    // a probe capturing probes would be a second bake -- which for a pane seen
    // in another pane's reflection is exactly right.
    for (const SceneItem& item : items_)
    {
        if (!item.material->isBlended() || !visibleFrom(item)) continue;
        applyMaterial(*item.material, item.world, view, projection, settings, nullptr);
        item.mesh->draw(device_);
    }
    device_.setBlendStateProperty(BlendState::Opaque);
}

void SceneRenderer::captureProbe(ReflectionProbe& probe, RenderTarget2D& target, int size,
                                 const RenderSettings& settings)
{
    // Half brightness into the 8-bit target, so a sunlit facade at 1.6 and a
    // horizon at 1.3 both fit under 1.0 and only the sun disc clips. The
    // effect's sRGB encode on the way out and the decode below are what keep
    // the dark half of the range -- asphalt at 0.05 -- in more than a dozen
    // steps. Both are undone when the texel is written into the cube.
    lightScale_     = 0.5f;
    capturingProbe_ = true;

    // Shadows for the capture: the cascades are re-fitted to a camera looking
    // straight down at the probe from 55 m, whose frustum covers the block
    // around it. Every surface in the street then falls in the same one or two
    // cascades whichever face is being drawn, because the receiver selects a
    // cascade by depth along the *fitting* camera, not the drawing one -- and
    // that is what makes one shadow pass serve six faces. The sunlit side of
    // the street and the shaded side are the whole point of a reflection.
    if (shadows_ != nullptr && settings.shadows)
    {
        DirectionalLightEXT light;
        light.Direction = sky_.lightDirection();
        light.Color     = sky_.sunColour();
        const Vector3 above = probe.position + Vector3(0.0f, 55.0f, 0.0f);
        const Matrix fitView = Matrix::CreateLookAt(above, probe.position, Vector3(0.0f, 0.0f, -1.0f));
        const Matrix fitProjection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(100.0f), 1.0f, 1.0f, 72.0f);
        shadows_->update(light, fitView, fitProjection);
        device_.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        device_.setBlendStateProperty(BlendState::Opaque);
        for (int cascade = 0; cascade < shadows_->getCascadeCount(); ++cascade)
        {
            shadows_->begin(cascade);
            drawCasters(probe.position, 80.0f, std::min(settings.propShadowDistance, 60.0f));
            shadows_->end();
        }
    }

    probe.environment = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    // The capture again, brighter, for the irradiance alone. What the probe
    // saw is one bounce: the sunlit facade opposite, the sky between the
    // eaves, the shaded wall beside it lit by that sky. What that shaded wall
    // also receives -- the light the sunlit facade throws onto it, and the
    // light it throws back -- is not in any single capture, and in a canyon of
    // light render it is most of the ambient. The gain stands in for the
    // bounces that were not rendered; the specular keeps the plain capture,
    // because a reflection is a picture and a picture is not brightened by
    // the light behind the camera.
    const float bounceGain = std::clamp(settings.probeBounceGain, 1.0f, 3.0f);
    const bool  bounced    = bounceGain > 1.001f && settings.probeIrradiance;
    probe.bounced = bounced ? std::make_unique<TextureCube>(device_, size, false,
                                                             SurfaceFormat::Color)
                            : nullptr;
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    std::vector<Color> captured(count, Color::Black);
    std::vector<Color> face(count, Color::Black);
    std::vector<Color> bright(bounced ? count : 0, Color::Black);
    const float cubeScale = sky_.environmentScale();

    for (int f = 0; f < 6; ++f)
    {
        const CubeMapFace which = static_cast<CubeMapFace>(f);
        Vector3 forward, up;
        probeFaceBasis(which, forward, up);
        const Matrix view = Matrix::CreateLookAt(probe.position, probe.position + forward, up);
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 0.10f, 340.0f);

        device_.SetRenderTarget(&target);
        device_.Clear(Color::Black, 1.0f);
        drawProbeFace(probe.position, view, projection, size, settings);
        target.GetData(captured.data(), static_cast<int>(count));

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const Color& pixel = captured[static_cast<std::size_t>(y) * size
                                              + static_cast<std::size_t>(x)];
                // Mirrored in x: see ProbeFaceBasis.
                const std::size_t at = static_cast<std::size_t>(y) * size
                                       + static_cast<std::size_t>(size - 1 - x);
                const float r = DecodeSrgb(pixel.getRProperty()) / lightScale_;
                const float g = DecodeSrgb(pixel.getGProperty()) / lightScale_;
                const float b = DecodeSrgb(pixel.getBProperty()) / lightScale_;
                face[at] = Color(static_cast<int>(EncodeCube(r, cubeScale)),
                                 static_cast<int>(EncodeCube(g, cubeScale)),
                                 static_cast<int>(EncodeCube(b, cubeScale)), 255);
                if (bounced)
                    bright[at] = Color(static_cast<int>(EncodeCube(r * bounceGain, cubeScale)),
                                       static_cast<int>(EncodeCube(g * bounceGain, cubeScale)),
                                       static_cast<int>(EncodeCube(b * bounceGain, cubeScale)),
                                       255);
            }
        probe.environment->SetData(which, face.data(), static_cast<int>(count));
        if (bounced) probe.bounced->SetData(which, bright.data(), static_cast<int>(count));
    }

    lightScale_     = 1.0f;
    capturingProbe_ = false;
}

void SceneRenderer::bakeReflectionProbes(std::vector<Vector3> positions,
                                         const RenderSettings& settings)
{
    probePositions_ = std::move(positions);
    probes_.clear();
    boundProbe_       = nullptr;
    environmentBound_ = false;
    for (SceneItem& item : items_) item.probe = nullptr;

    if (!settings.reflectionProbes || probePositions_.empty()) return;
    if (effect_ == nullptr) return;
    if (!sky_.hasImageBasedLighting() || !device_.SupportsImageBasedLightingEXT())
    {
        limitations_.emplace_back("no image based lighting, so no local reflection probes");
        return;
    }
    if (!device_.SupportsCapability(CNA::GraphicsCapability::ThreeD)) return;

    Stopwatch watch = Stopwatch::StartNew();
    const int size = std::clamp(settings.probeFaceSize, 16, 128);

    std::unique_ptr<RenderTarget2D> target;
    try
    {
        target = std::make_unique<RenderTarget2D>(device_, size, size, false, SurfaceFormat::Color,
                                                  DepthFormat::Depth24);
    }
    catch (const std::exception& failure)
    {
        limitations_.emplace_back(std::string("no render target for reflection probes: ")
                                  + failure.what());
        return;
    }

    // The static list has to be in draw order before the first capture, and
    // cull() only sorts it on the first frame.
    if (!sceneSorted_)
    {
        std::stable_sort(items_.begin(), items_.end(),
                         [](const SceneItem& a, const SceneItem& b) {
                             return a.material < b.material;
                         });
        sceneSorted_ = true;
    }

    EnvironmentProcessor processor(device_);
    // Fewer samples than the sky's convolution: a probe face is a quarter the
    // size and there are thirty of them, and the reflection of a parked car
    // does not need the sky's smoothness.
    const int prefilterSamples = size >= 64 ? 32 : 24;
    for (const Vector3& position : probePositions_)
    {
        auto probe = std::make_unique<ReflectionProbe>();
        probe->position = position;
        try
        {
            captureProbe(*probe, *target, size, settings);
            probe->prefiltered = processor.generatePrefilteredSpecular(
                probe->environment.get(), size, probe->prefilteredMips, prefilterSamples);
            if (settings.probeIrradiance)
                probe->irradiance = processor.generateIrradiance(
                    probe->bounced != nullptr ? probe->bounced.get() : probe->environment.get(),
                    16, 24);
        }
        catch (const std::exception& failure)
        {
            CNA::Logger::Warn(std::string("cna-street: reflection probe failed: ") + failure.what());
            lightScale_     = 1.0f;
            capturingProbe_ = false;
            continue;
        }
        probes_.push_back(std::move(probe));
    }
    device_.SetRenderTarget(nullptr);

    for (SceneItem& item : items_) item.probe = nearestProbe(BoxCentre(item.worldBounds));

    probeBakeSeconds_ = Milliseconds(watch) / 1000.0f;
    CNA::Logger::Info("cna-street: " + std::to_string(probes_.size()) + " reflection probes at "
                      + std::to_string(size) + " px baked in "
                      + std::to_string(probeBakeSeconds_) + " s");
}

void SceneRenderer::rebakeReflectionProbes(const RenderSettings& settings)
{
    if (probePositions_.empty()) return;
    bakeReflectionProbes(probePositions_, settings);
}

void SceneRenderer::dumpReflectionProbes(const std::string& directory) const
{
    if (probes_.empty())
    {
        CNA::Logger::Warn("cna-street: no reflection probes to dump");
        return;
    }
    std::filesystem::create_directories(directory);
    int index = 0;
    for (const auto& probe : probes_)
    {
        if (probe->environment == nullptr) continue;
        const int size = probe->environment->getSizeProperty();
        const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        std::vector<Color> face(count);
        // Six faces side by side, +X -X +Y -Y +Z -Z, in the order the cube
        // stores them.
        std::vector<std::uint8_t> strip(count * 6u * 4u, 0);
        for (int f = 0; f < 6; ++f)
        {
            probe->environment->GetData(static_cast<CubeMapFace>(f), face.data(),
                                        static_cast<int>(count));
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const Color& c = face[static_cast<std::size_t>(y) * size
                                          + static_cast<std::size_t>(x)];
                    const std::size_t at = (static_cast<std::size_t>(y) * size * 6u
                                            + static_cast<std::size_t>(f) * size
                                            + static_cast<std::size_t>(x))
                                           * 4u;
                    strip[at + 0] = static_cast<std::uint8_t>(c.getRProperty());
                    strip[at + 1] = static_cast<std::uint8_t>(c.getGProperty());
                    strip[at + 2] = static_cast<std::uint8_t>(c.getBProperty());
                    strip[at + 3] = 255;
                }
        }
        char name[32];
        std::snprintf(name, sizeof(name), "probe-%02d.png", index++);
        Texture2D image = Texture2D::CreateFromPixels(device_, size * 6, size, strip);
        image.SaveAsPng((std::filesystem::path(directory) / name).string());
    }
    CNA::Logger::Info("cna-street: wrote " + std::to_string(index) + " probe strips to " + directory);
}

void SceneRenderer::render(const Camera& camera, const RenderSettings& settings, float timeSeconds)
{
    stats_.drawCalls = 0;
    stats_.shadowDrawCalls = 0;
    stats_.instancedDrawCalls = 0;
    stats_.triangles = 0;

    // One clock for the whole frame, and every stage a slice of it. The stages
    // used to be timed by separate stopwatches whose spans overlapped, so
    // "post" was really "everything after culling that is not the opaque pass",
    // it counted the shadow pass twice, and the numbers in the overlay summed to
    // five times the frame they were measuring.
    Stopwatch watch = Stopwatch::StartNew();

    cull(camera, settings);
    const float afterCull = Milliseconds(watch);
    drawShadows(camera, settings);
    const float afterShadow = Milliseconds(watch);
    drawPrepass(camera, settings);
    const float afterPrepass = Milliseconds(watch);

    if (gpuTimer_ != nullptr) pipeline_->setGpuTimingEnabledEXT(true);

    pipeline_->setTransparentScene([&] { drawTransparent(camera, settings); });
    pipeline_->begin(Color::Black);

    // The sky first, with depth writes off so every later draw covers it. Drawn
    // here rather than through the pipeline's own skybox hook because that hook
    // takes a cubemap and this sky is a shader.
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setBlendStateProperty(BlendState::Opaque);
    sky_.draw(camera.view(), camera.projection(), width_, height_, timeSeconds);

    const float afterSky = Milliseconds(watch);
    drawOpaque(camera, settings);
    const float afterOpaque = Milliseconds(watch);
    pipeline_->end();

    const auto pipelineStats = pipeline_->getStatistics();
    stats_.postPasses = pipelineStats.passesRun;
    stats_.usedSceneTarget = pipelineStats.usedSceneTarget;

    stats_.cullMs    = afterCull;
    stats_.shadowMs  = afterShadow - afterCull;
    stats_.prepassMs = afterPrepass - afterShadow;
    stats_.skyMs     = afterSky - afterPrepass;
    stats_.opaqueMs  = afterOpaque - afterSky;
    stats_.postMs    = Milliseconds(watch) - afterOpaque;
    stats_.frameMs   = Milliseconds(watch);
}


std::vector<SceneRenderer::BatchCost> SceneRenderer::costReport(std::size_t limit) const
{
    // Keyed on the batch's registered name, because that is the name a person
    // reading the report thinks in: "shop-interior", "tree-2", "display-plant".
    // Static items carry their mesh's name; instance groups carry the group's,
    // which is the same thing said once for the whole set.
    std::unordered_map<std::string, BatchCost> byName;
    const auto add = [&byName](const std::string& fullName, int copies, long long triangles,
                               bool castsShadow, float cullDistance) {
        // Up to the '#' only. A merged static batch is named for its material
        // and the plot it came from -- "shop-stock#61046.38" -- and forty of
        // those listed separately says nothing at all, where "shop-stock,
        // 39 batches, 190k triangles" says the whole thing.
        const std::size_t hash = fullName.find('#');
        const std::string name = hash == std::string::npos ? fullName : fullName.substr(0, hash);
        BatchCost& cost = byName[name];
        if (cost.name.empty())
        {
            cost.name         = name;
            cost.castsShadow  = castsShadow;
            cost.cullDistance = cullDistance;
        }
        // The longest leash in the family is the one that matters.
        cost.cullDistance = cullDistance <= 0.0f || cost.cullDistance <= 0.0f
                                ? 0.0f
                                : std::max(cost.cullDistance, cullDistance);
        cost.castsShadow = cost.castsShadow || castsShadow;
        ++cost.batches;
        cost.copies    += copies;
        cost.triangles += triangles;
    };

    for (const SceneItem& item : items_)
    {
        if (item.mesh == nullptr) continue;
        add(item.mesh->name(), 1, item.mesh->triangleCount(),
            item.material != nullptr && item.material->castsShadow, item.cullDistance);
    }
    for (const InstanceGroup& group : groups_)
    {
        if (group.mesh == nullptr) continue;
        const auto copies = static_cast<int>(group.transforms.size());
        add(group.name, copies,
            static_cast<long long>(group.mesh->triangleCount()) * copies,
            group.castsShadow, group.cullDistance);
    }

    std::vector<BatchCost> report;
    report.reserve(byName.size());
    for (auto& entry : byName) report.push_back(std::move(entry.second));
    std::sort(report.begin(), report.end(), [](const BatchCost& a, const BatchCost& b) {
        return a.triangles > b.triangles;
    });
    if (limit > 0 && report.size() > limit) report.resize(limit);
    return report;
}


std::vector<SceneRenderer::BatchCost> SceneRenderer::visibleReport(std::size_t limit) const
{
    std::unordered_map<std::string, BatchCost> byName;
    const auto add = [&byName](const std::string& fullName, int copies, long long triangles,
                               bool castsShadow, float cullDistance) {
        const std::size_t hash = fullName.find('#');
        const std::string name = hash == std::string::npos ? fullName : fullName.substr(0, hash);
        BatchCost& cost = byName[name];
        if (cost.name.empty())
        {
            cost.name         = name;
            cost.cullDistance = cullDistance;
        }
        ++cost.batches;
        cost.copies    += copies;
        cost.triangles += triangles;
        cost.castsShadow = cost.castsShadow || castsShadow;
    };

    for (const std::size_t i : visibleOpaque_)
        add(items_[i].mesh->name(), 1, items_[i].mesh->triangleCount(),
            items_[i].material->castsShadow, items_[i].cullDistance);
    for (const std::size_t i : visibleTransparent_)
        add(items_[i].mesh->name(), 1, items_[i].mesh->triangleCount(),
            items_[i].material->castsShadow, items_[i].cullDistance);
    for (const std::size_t i : visibleDynamic_)
        add(dynamic_[i].mesh->name() + "  [mover]", 1, dynamic_[i].mesh->triangleCount(),
            dynamic_[i].material->castsShadow, dynamic_[i].cullDistance);
    for (std::size_t g = 0; g < visibleGroupTransforms_.size() && g < groups_.size(); ++g)
    {
        const auto copies = static_cast<int>(visibleGroupTransforms_[g].size());
        if (copies == 0) continue;
        const GpuMesh* mesh = g < visibleGroupMesh_.size() && visibleGroupMesh_[g] != nullptr
                                  ? visibleGroupMesh_[g]
                                  : groups_[g].mesh;
        add(groups_[g].name, copies,
            static_cast<long long>(mesh->triangleCount()) * copies, groups_[g].castsShadow,
            groups_[g].cullDistance);
    }
    // One entry per visible skinned draw. The index is not needed -- a skinned
    // item's triangles are already counted in the stats and its name is the
    // same for all of them -- so this counts rather than iterates.
    for (std::size_t n = visibleSkinned_.size(); n > 0; --n)
        add("character  [skinned]", 1, 0, false, 0.0f);

    std::vector<BatchCost> report;
    report.reserve(byName.size());
    for (auto& entry : byName) report.push_back(std::move(entry.second));
    std::sort(report.begin(), report.end(), [](const BatchCost& a, const BatchCost& b) {
        return a.batches > b.batches;
    });
    if (limit > 0 && report.size() > limit) report.resize(limit);
    return report;
}

}  // namespace CnaStreet
