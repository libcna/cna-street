// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/SkySystem.hpp"

#include "CnaStreet/Render/RenderSettings.hpp"

#include "CNA/Graphics/AtmosphericSky.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::AtmosphericSky;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::FullscreenPass;

namespace CnaStreet {

namespace {

/// The fullscreen quad's vertex program. `FullscreenPass` draws through
/// `SpriteBatch`, so the attribute layout is SpriteBatch's, not ours.
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

/// Everything this project adds on top of CNA's scattering model: two animated
/// cloud layers, a sun disc with limb darkening, and a horizon that does not end
/// in a hard line. `cnaSkyRadiance` is prepended from
/// `AtmosphericSky::getModelGlsl()` rather than reimplemented.
constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform mat4  uInverseViewProjection;
uniform vec3  uSunDirection;
uniform float uTurbidity;
uniform float uIntensity;
uniform float uTime;
uniform float uCloudCoverage;
uniform float uCloudsEnabled;
uniform float uFlipV;

// --- value noise, matching the CPU generator's shape ----------------------
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p, int octaves) {
    float sum = 0.0, amplitude = 0.5, total = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= octaves) break;
        sum += valueNoise(p) * amplitude;
        total += amplitude;
        p = p * 2.03 + vec2(17.3, 9.1);
        amplitude *= 0.5;
    }
    return sum / total;
}

/// Density of a cloud deck at `height` metres, sampled where the view ray
/// crosses it. Coverage remaps the noise so that the *shape* of the clouds does
/// not change as the sky clears -- they thin and part instead, which is what
/// weather looks like.
float deck(vec3 direction, float height, float scale, float coverage, float sharpness,
           vec2 drift) {
    if (direction.y < 0.008) return 0.0;
    vec2 at = direction.xz * (height / direction.y) * scale + drift;
    float base = fbm(at, 5);
    float detail = fbm(at * 3.7 + vec2(4.2, 1.7), 4);
    float density = base * 0.78 + detail * 0.22;
    density = smoothstep(1.0 - coverage, 1.0 - coverage + sharpness, density);
    // Fade the deck out toward the horizon: a flat plane sampled at a grazing
    // angle stretches to infinity, and without this the horizon is a hard band
    // of solid cloud.
    return density * smoothstep(0.0, 0.16, direction.y);
}

void main() {
    // FullscreenPass draws through SpriteBatch, whose texture coordinate origin
    // is the *top* left of the destination rectangle, while clip space has +1 at
    // the top. Without this flip the sky is rendered upside down -- the ground
    // haze ends up overhead and the zenith underfoot. CNA's own AtmosphericSky
    // has the same omission; see docs/cna-findings.md CNA-F7.
    vec2 screen = vec2(TexCoord.x, mix(TexCoord.y, 1.0 - TexCoord.y, uFlipV));
    vec4 ray = uInverseViewProjection * vec4(screen * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(ray.xyz / ray.w);

    vec3 sky = cnaSkyRadiance(direction, uSunDirection, uTurbidity) * uIntensity;

    // --- the sun itself ---------------------------------------------------
    // uSunDirection is the direction the light *travels*, which is CNA's own
    // convention for cnaSkyRadiance and for DirectionalLightEXT; the vector
    // pointing at the sun is its negation.
    vec3 toSun = -normalize(uSunDirection);

    // The scattering model gives the glow around the sun but not its disc.
    // 0.5 degrees across, with limb darkening, and a small forward-scattering
    // halo so it sits in the sky rather than on it.
    float cosAngle = dot(direction, toSun);
    float disc = smoothstep(0.99987, 0.99994, cosAngle);
    float limb = sqrt(max(1.0 - pow(max(1.0 - cosAngle, 0.0) / 0.00013, 2.0), 0.0));
    vec3 sunColour = vec3(1.0, 0.94, 0.86);
    sky += sunColour * disc * (0.55 + 0.45 * limb) * 42.0 * uIntensity;
    sky += sunColour * pow(max(cosAngle, 0.0), 480.0) * 1.6 * uIntensity;

    if (uCloudsEnabled > 0.5) {
        // --- cumulus deck -------------------------------------------------
        vec2 drift = vec2(uTime * 0.9, uTime * 0.35);
        float lower = deck(direction, 1500.0, 0.00042, uCloudCoverage, 0.30, drift);
        // A second sample offset toward the sun approximates how much cloud the
        // light had to pass through, which is what gives a cumulus its bright
        // rim and dark base for the cost of one more noise fetch.
        vec3 toward = normalize(direction + toSun * 0.16);
        float shadowed = deck(toward, 1500.0, 0.00042, uCloudCoverage, 0.30, drift);
        float thickness = clamp(shadowed * 1.15, 0.0, 1.0);

        vec3 lit = vec3(1.06, 1.04, 1.02);
        vec3 shade = vec3(0.44, 0.47, 0.55);
        vec3 cloudColour = mix(lit, shade, thickness * 0.85);
        // Silver lining: forward scattering through a thin edge.
        float rim = clamp(lower - thickness, 0.0, 1.0);
        cloudColour += sunColour * rim * pow(max(cosAngle, 0.0), 6.0) * 0.9;
        // Clouds are lit by the same sun, so they dim with it rather than
        // staying white at dusk.
        cloudColour *= uIntensity * (0.35 + 0.75 * clamp(toSun.y, 0.0, 1.0));

        sky = mix(sky, cloudColour, clamp(lower, 0.0, 1.0) * 0.96);

        // --- cirrus ------------------------------------------------------
        float high = deck(direction, 6200.0, 0.00019, uCloudCoverage * 0.55 + 0.10, 0.55,
                          drift * 2.4);
        sky = mix(sky, vec3(1.02, 1.01, 1.03) * uIntensity, high * 0.32);
    }

    // --- horizon ------------------------------------------------------------
    // Below the horizon there is a city, not a mirrored sky. Fade to a hazy
    // ground tone so that a camera tilted down at the skyline does not show the
    // sky continuing underneath it.
    float below = smoothstep(0.02, -0.06, direction.y);
    vec3 haze = cnaSkyRadiance(vec3(direction.x, 0.03, direction.z), uSunDirection, uTurbidity)
                * uIntensity;
    vec3 ground = mix(haze, vec3(0.10, 0.098, 0.095) * uIntensity, 0.55);
    sky = mix(sky, ground, below);

    FragColor = vec4(sky, 1.0);
}
)";

Vector3 Normalise(const Vector3& v, const Vector3& fallback)
{
    const float lengthSquared = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (lengthSquared <= 1e-12f) return fallback;
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return Vector3(v.X * inverse, v.Y * inverse, v.Z * inverse);
}

Matrix RotationOnly(const Matrix& view)
{
    Matrix rotation = view;
    rotation.M41 = 0.0f;
    rotation.M42 = 0.0f;
    rotation.M43 = 0.0f;
    return rotation;
}

/// World direction for a texel of a cube face, following the standard cube map
/// convention CNA's `TextureCube` uses.
Vector3 CubeDirection(CubeMapFace face, float u, float v)
{
    const float a = u * 2.0f - 1.0f;
    const float b = v * 2.0f - 1.0f;
    switch (face)
    {
        case CubeMapFace::PositiveX: return Normalise(Vector3(1.0f, -b, -a), Vector3::Right);
        case CubeMapFace::NegativeX: return Normalise(Vector3(-1.0f, -b, a), Vector3::Left);
        case CubeMapFace::PositiveY: return Normalise(Vector3(a, 1.0f, b), Vector3::Up);
        case CubeMapFace::NegativeY: return Normalise(Vector3(a, -1.0f, -b), Vector3::Down);
        case CubeMapFace::PositiveZ: return Normalise(Vector3(a, -b, 1.0f), Vector3::Backward);
        case CubeMapFace::NegativeZ: return Normalise(Vector3(-a, -b, -1.0f), Vector3::Forward);
    }
    return Vector3::Up;
}

std::uint8_t Encode(float linear)
{
    // The environment cube is an ordinary 8-bit texture, so the sky's very wide
    // range has to be compressed into it. A plain sRGB curve after a Reinhard
    // knee keeps both the horizon and the deep zenith distinguishable, which is
    // what the irradiance integral needs; clipping at 1.0 instead would make
    // every ambient term come out the same grey.
    const float mapped = linear / (1.0f + linear);
    const float encoded = mapped <= 0.0031308f
                              ? mapped * 12.92f
                              : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

SkySystem::SkySystem(GraphicsDevice& device) : device_(device) {}

SkySystem::~SkySystem() = default;

Vector3 SkySystem::lightDirection() const
{
    return Vector3(-sunDirection_.X, -sunDirection_.Y, -sunDirection_.Z);
}

bool SkySystem::hasImageBasedLighting() const
{
    return irradiance_ != nullptr && prefiltered_ != nullptr && brdfLut_ != nullptr;
}

void SkySystem::build(const RenderSettings& settings)
{
    turbidity_     = settings.skyTurbidity;
    skyIntensity_  = settings.skyIntensity;
    cloudCoverage_ = settings.cloudCoverage;
    cloudSpeed_    = settings.cloudSpeed;
    cloudsEnabled_ = settings.clouds;

    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
        || !device_.ExecutesShaderEffectSourceEXT())
    {
        supported_ = false;
        reason_ = "the renderer does not execute shader-effect source, so the atmospheric sky "
                  "cannot be drawn";
        CNA::Logger::Warn("cna-street: " + reason_);
    }
    else
    {
        // `getModelGlsl()` is the model body only -- no version directive and no
        // precision qualifier, because it is meant to be pasted into a shader
        // that already has both. Supplying them here is the same two lines
        // AtmosphericSky itself uses.
        const std::string source = std::string("#version 300 es\nprecision highp float;\n")
                                   + AtmosphericSky::getModelGlsl() + kFragmentBody;
        effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, source);
        supported_ = effect_->IsEffectValid();
        if (!supported_)
        {
            reason_ = "the sky shader did not compile: " + effect_->GetCompileErrorEXT();
            CNA::Logger::Error("cna-street: " + reason_);
            effect_.reset();
        }
        else
        {
            fullscreen_ = std::make_unique<FullscreenPass>(device_);
            white_ = std::make_unique<Texture2D>(device_, 1, 1);
            const Color pixel = Color::White;
            white_->SetData(&pixel, 1);
        }
    }

    updateSun(settings);
}

void SkySystem::updateSun(const RenderSettings& settings)
{
    turbidity_     = settings.skyTurbidity;
    skyIntensity_  = settings.skyIntensity;
    cloudCoverage_ = settings.cloudCoverage;
    cloudSpeed_    = settings.cloudSpeed;
    cloudsEnabled_ = settings.clouds;

    const float elevation = MathHelper::ToRadians(std::clamp(settings.sunElevationDegrees,
                                                             -5.0f, 89.0f));
    const float azimuth = MathHelper::ToRadians(settings.sunAzimuthDegrees);
    // Azimuth measured clockwise from north (+Z), the surveying convention.
    sunDirection_ = Normalise(Vector3(std::sin(azimuth) * std::cos(elevation), std::sin(elevation),
                                      std::cos(azimuth) * std::cos(elevation)),
                              Vector3::Up);

    computeLighting(settings);
    bakeEnvironment(settings);
}

void SkySystem::computeLighting(const RenderSettings& settings)
{
    // Sun colour: the model's own radiance a fraction of a degree off the solar
    // direction, normalised so that a high sun is white and a low one reddens by
    // itself. Reading it from the model rather than choosing a colour is what
    // keeps the direct light and the sky agreeing at every sun angle.
    const Vector3 nearSun = Normalise(
        Vector3(sunDirection_.X, sunDirection_.Y + 0.02f, sunDirection_.Z), Vector3::Up);
    Vector3 radiance = AtmosphericSky::radiance(nearSun, lightDirection(), turbidity_);
    const float peak = std::max(std::max(radiance.X, radiance.Y), std::max(radiance.Z, 1e-4f));
    sunColour_ = Vector3(radiance.X / peak, radiance.Y / peak, radiance.Z / peak)
                 * settings.sunIntensity
                 * std::clamp(std::sin(MathHelper::ToRadians(settings.sunElevationDegrees)) * 1.15f
                                  + 0.10f,
                              0.0f, 1.0f);

    // Ambient: the hemisphere average of the same model, cosine weighted. This
    // is the fallback for renderers with no image based lighting; where IBL is
    // available `PbrEffect` uses the irradiance cube instead and this only sets
    // the floor.
    Vector3 sum = Vector3::Zero;
    float   weight = 0.0f;
    constexpr int kRings = 8, kSectors = 16;
    for (int ring = 0; ring < kRings; ++ring)
    {
        const float theta = (static_cast<float>(ring) + 0.5f) / static_cast<float>(kRings)
                            * MathHelper::PiOver2;
        const float cosTheta = std::cos(theta), sinTheta = std::sin(theta);
        for (int sector = 0; sector < kSectors; ++sector)
        {
            const float phi = (static_cast<float>(sector) + 0.5f) / static_cast<float>(kSectors)
                              * MathHelper::TwoPi;
            const Vector3 direction(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
            const Vector3 sky = AtmosphericSky::radiance(direction, lightDirection(), turbidity_);
            sum = sum + sky * (cosTheta * sinTheta);
            weight += cosTheta * sinTheta;
        }
    }
    if (weight > 0.0f) sum = sum * (1.0f / weight);
    ambientColour_ = sum * skyIntensity_;
    // Clouds both block sky light and bounce it around; a thin overcast is
    // *brighter* ambient than a clear sky, not darker.
    if (cloudsEnabled_)
        ambientColour_ = ambientColour_ * (1.0f + cloudCoverage_ * 0.35f);

    // Logged because the numbers here decide how the whole scene is exposed, and
    // a sky that comes back an order of magnitude off is otherwise indistinguishable
    // from a tonemapping mistake.
    const Vector3 zenith = AtmosphericSky::radiance(Vector3::Up, lightDirection(), turbidity_);
    const Vector3 horizon = AtmosphericSky::radiance(
        Normalise(Vector3(sunDirection_.X, 0.05f, sunDirection_.Z), Vector3::Forward),
        lightDirection(), turbidity_);
    CNA::Logger::Info(
        "cna-street: sky -- zenith " + std::to_string(zenith.X) + "," + std::to_string(zenith.Y)
        + "," + std::to_string(zenith.Z) + "  horizon " + std::to_string(horizon.X) + ","
        + std::to_string(horizon.Y) + "," + std::to_string(horizon.Z) + "  ambient "
        + std::to_string(ambientColour_.X) + "," + std::to_string(ambientColour_.Y) + ","
        + std::to_string(ambientColour_.Z) + "  sun " + std::to_string(sunColour_.X) + ","
        + std::to_string(sunColour_.Y) + "," + std::to_string(sunColour_.Z));
}

void SkySystem::bakeEnvironment(const RenderSettings& settings)
{
    (void)settings;
    // 64 px faces: the irradiance integral and the prefiltered specular are both
    // low-frequency, and a bigger source would only make the bake slower without
    // changing what the materials see.
    constexpr int kFaceSize = 64;
    environment_ = std::make_unique<TextureCube>(device_, kFaceSize, false, SurfaceFormat::Color);

    std::vector<Color> face(static_cast<std::size_t>(kFaceSize) * kFaceSize);
    for (int index = 0; index < 6; ++index)
    {
        const CubeMapFace which = static_cast<CubeMapFace>(index);
        for (int y = 0; y < kFaceSize; ++y)
            for (int x = 0; x < kFaceSize; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kFaceSize);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kFaceSize);
                Vector3 direction = CubeDirection(which, u, v);

                Vector3 radiance;
                if (direction.Y >= 0.0f)
                {
                    radiance = AtmosphericSky::radiance(direction, lightDirection(), turbidity_)
                               * skyIntensity_;
                    // A cloud deck seen from below is a diffuser: it raises the
                    // average and flattens the gradient. Baked here so that a
                    // cloudy sky lights the street the way a cloudy sky does.
                    if (cloudsEnabled_)
                    {
                                    const float lit = std::clamp(sunDirection_.Y, 0.0f, 1.0f);
                        const Vector3 cloud(0.72f + 0.5f * lit, 0.74f + 0.5f * lit,
                                            0.80f + 0.5f * lit);
                        const float mixAmount = cloudCoverage_ * 0.55f
                                                * std::clamp(direction.Y * 3.0f, 0.0f, 1.0f);
                        radiance = radiance * (1.0f - mixAmount) + cloud * mixAmount;
                    }
                }
                else
                {
                    // Downward: bounce off the street. Roughly the albedo of
                    // asphalt and paving lit by the sky above it, which is what
                    // fills in the underside of a car or a balcony.
                    const Vector3 up = AtmosphericSky::radiance(Vector3(direction.X, 0.15f,
                                                                        direction.Z),
                                                                lightDirection(), turbidity_)
                                       * skyIntensity_;
                    const float fade = std::clamp(-direction.Y * 2.2f, 0.0f, 1.0f);
                    radiance = up * (0.16f + 0.05f * (1.0f - fade));
                }

                face[static_cast<std::size_t>(y) * kFaceSize + static_cast<std::size_t>(x)] =
                    Color(static_cast<int>(Encode(radiance.X)), static_cast<int>(Encode(radiance.Y)),
                          static_cast<int>(Encode(radiance.Z)), 255);
            }
        environment_->SetData(which, face.data(), static_cast<int>(face.size()));
    }

    if (!device_.SupportsImageBasedLightingEXT())
    {
        irradiance_.reset();
        prefiltered_.reset();
        brdfLut_.reset();
        CNA::Logger::Info("cna-street: renderer has no image based lighting; falling back to a "
                          "hemisphere ambient term");
        return;
    }

    EnvironmentProcessor processor(device_);
    irradiance_  = processor.generateIrradiance(environment_.get(), 32, 48);
    prefiltered_ = processor.generatePrefilteredSpecular(environment_.get(), 64,
                                                         prefilteredMips_, 48);
    brdfLut_     = processor.generateBrdfLut(64, 96);
}

void SkySystem::draw(const Matrix& view, const Matrix& projection, int width, int height,
                     float timeSeconds)
{
    if (!supported_ || effect_ == nullptr || fullscreen_ == nullptr) return;
    if (width <= 0 || height <= 0) return;

    const Matrix inverse = Matrix::Invert(RotationOnly(view) * projection);
    effect_->Apply();
    effect_->SetUniformMat4("uInverseViewProjection", &inverse.M11);
    const Vector3 travel = lightDirection();
    effect_->SetUniformVec3("uSunDirection", travel.X, travel.Y, travel.Z);
    effect_->SetUniformFloat("uTurbidity", turbidity_);
    effect_->SetUniformFloat("uIntensity", skyIntensity_);
    effect_->SetUniformFloat("uTime", timeSeconds * cloudSpeed_ * 60.0f);
    effect_->SetUniformFloat("uCloudCoverage", cloudCoverage_);
    effect_->SetUniformFloat("uCloudsEnabled", cloudsEnabled_ ? 1.0f : 0.0f);
    effect_->SetUniformFloat("uFlipV", flipV_ ? 1.0f : 0.0f);

    fullscreen_->drawOverCurrentTarget(white_.get(), effect_.get(), width, height);
}

}  // namespace CnaStreet
