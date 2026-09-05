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
uniform float uEncodeSrgb;

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

    // Civil twilight. With the sun a few degrees under the horizon the
    // scattering model is left with its reddest, dimmest term everywhere, and
    // the whole sky came out the same dark brown. A real twilight sky is deep
    // blue overhead -- ozone absorption, which a single-scattering model has
    // no term for -- with the warm band kept to the horizon on the sun's side.
    // So the blue is added by hand, weighted by how far the sun is down and
    // how far this direction is from it, and the model keeps the glow where
    // the glow is.
    float dusk = clamp(-toSun.y * 9.0, 0.0, 1.0);
    if (dusk > 0.0) {
        float up = clamp(direction.y, 0.0, 1.0);
        vec3 zenithBlue = vec3(0.010, 0.020, 0.058);
        vec3 horizonBlue = vec3(0.030, 0.038, 0.070);
        vec3 twilight = mix(horizonBlue, zenithBlue, pow(up, 0.6)) * uIntensity;
        // The band of afterglow around the sun's azimuth, low on the horizon.
        float towards = clamp(dot(normalize(vec3(direction.x, 0.0, direction.z)),
                                  normalize(vec3(toSun.x, 0.0, toSun.z))), 0.0, 1.0);
        float glow = pow(towards, 3.0) * (1.0 - smoothstep(0.0, 0.28, direction.y));
        vec3 afterglow = vec3(0.34, 0.14, 0.05) * uIntensity * glow;
        sky = mix(sky, twilight + afterglow + sky * 0.35, dusk);
    }

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
        // staying white at dusk -- and once the sun is under the horizon they
        // are lit by nothing but the sky around them. The first version kept
        // a third of their daylight brightness at night, which hung pale
        // brown blobs in a sky that was otherwise dark: a cloud after dusk is
        // a slightly lighter patch of the sky's own colour, no more.
        float daylight = clamp(toSun.y * 3.0 + 0.08, 0.0, 1.0);
        cloudColour *= uIntensity * (0.35 + 0.75 * clamp(toSun.y, 0.0, 1.0));
        cloudColour = mix(sky * 1.25 + vec3(0.002), cloudColour, daylight);

        sky = mix(sky, cloudColour, clamp(lower, 0.0, 1.0) * 0.96);

        // --- cirrus ------------------------------------------------------
        float high = deck(direction, 6200.0, 0.00019, uCloudCoverage * 0.55 + 0.10, 0.55,
                          drift * 2.4);
        vec3 cirrus = mix(sky * 1.15, vec3(1.02, 1.01, 1.03) * uIntensity, daylight);
        sky = mix(sky, cirrus, high * 0.32);
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

    if (uEncodeSrgb > 0.5) {
        // Into an 8-bit capture, the way PbrEffect writes its own output there:
        // the probe reader decodes both with one curve.
        vec3 c = clamp(sky, 0.0, 1.0);
        sky = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
    }
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

}  // namespace

Vector3 SkySystem::cubeDirection(CubeMapFace face, float u, float v)
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

namespace {

/// Quantises one channel of the environment cube.
///
/// **Linearly, and scaled** -- and getting that wrong is what made the whole
/// street look washed out for the first version of this project. `TextureCube`
/// is 8-bit, so a sky whose horizon radiance is 1.25 cannot be stored in it
/// directly. The first attempt compressed it with a Reinhard knee and then an
/// sRGB curve; `EnvironmentProcessor` reads the texels as *linear radiance*
/// (see its `ToColor`, which clamps and scales and nothing else), so the values
/// that came back were `srgb(reinhard(L))` where the split sum wanted `L`. That
/// function lifts every dark direction and crushes every bright one -- a zenith
/// at 0.36 arrived as 0.56 and a horizon at 1.25 arrived as 0.77 -- so the
/// ambient light had almost no gradient left and every surface in shade was
/// lit by the same flat grey.
///
/// `ImageBasedLightEXT::Intensity` is the field CNA provides for exactly this:
/// the cube holds `L / scale` and the effect multiplies by `scale` again.
std::uint8_t Encode(float linear, float scale)
{
    const float mapped = linear / std::max(scale, 1e-4f);
    return static_cast<std::uint8_t>(std::lround(std::clamp(mapped, 0.0f, 1.0f) * 255.0f));
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
    // Sun colour: atmospheric *transmittance* along the solar path, which is
    // what is left of the disc after the sky has taken its share.
    //
    // The first version read the sky model's radiance a fraction of a degree off
    // the sun and normalised that. It is the wrong quantity, and wrong in the
    // one direction that matters: the sky near the sun is bright because it is
    // *scattered* light, and scattering goes as 1/λ⁴, so that colour is blue.
    // The demo therefore lit the street with a blue sun under a blue sky, every
    // surface came back the same cold grey, and no amount of exposure could put
    // warmth back into a frame that never had any. Sunlight is what survives the
    // same scattering, so it is its complement: warm, and warmer the lower the
    // sun.
    //
    // Rayleigh coefficients at 680/550/440 nm and a 8 km scale height give the
    // vertical optical depth; Kasten-Young's air mass, which does not diverge at
    // the horizon the way 1/sin does, scales it along the path. Mie is nearly
    // grey and carries the turbidity.
    const float elevationDeg = std::clamp(settings.sunElevationDegrees, -3.0f, 89.0f);
    const float airMass = 1.0f
                          / (std::max(std::sin(MathHelper::ToRadians(elevationDeg)), 0.0f)
                             + 0.50572f * std::pow(elevationDeg + 6.07995f, -1.6364f));
    const Vector3 rayleighDepth(0.0465f, 0.1085f, 0.2646f);
    const float   mieDepth = 0.0252f * std::max(turbidity_ - 1.0f, 0.2f);
    Vector3 transmittance(
        std::exp(-rayleighDepth.X * airMass - mieDepth * airMass),
        std::exp(-rayleighDepth.Y * airMass - mieDepth * airMass),
        std::exp(-rayleighDepth.Z * airMass - mieDepth * airMass));
    // Normalised on its brightest channel, so `sunIntensity` means the same
    // thing at every hour and only the *hue* moves.
    const float peak = std::max(std::max(transmittance.X, transmittance.Y),
                                std::max(transmittance.Z, 1e-4f));
    // How much of the disc is above the horizon, so that the light goes out
    // smoothly at dusk instead of snapping off.
    const float altitude = std::clamp(
        std::sin(MathHelper::ToRadians(elevationDeg)) * 3.2f + 0.06f, 0.0f, 1.0f);
    sunColour_ = Vector3(transmittance.X / peak, transmittance.Y / peak, transmittance.Z / peak)
                 * settings.sunIntensity * altitude;

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
    // 96 px faces. The irradiance integral is low-frequency and 64 was plenty
    // for it, but the *prefiltered specular* is not: its top mip is what a shop
    // window and a car roof reflect, and at 64 px the sky's horizon gradient
    // arrives as four bands. This is the source both are convolved from.
    constexpr int kFaceSize = 96;
    environment_ = std::make_unique<TextureCube>(device_, kFaceSize, false, SurfaceFormat::Color);

    // Radiance first, scale second, quantise third. The scale has to be known
    // before any texel is written, and it cannot be guessed: at low sun the sky
    // peaks near 3 and at high sun near 1.4, and a fixed divisor would either
    // clip the horizon or throw away three quarters of the precision.
    std::vector<Vector3> faces(6 * static_cast<std::size_t>(kFaceSize) * kFaceSize);
    float peak = 1e-3f;
    for (int index = 0; index < 6; ++index)
    {
        const CubeMapFace which = static_cast<CubeMapFace>(index);
        for (int y = 0; y < kFaceSize; ++y)
            for (int x = 0; x < kFaceSize; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kFaceSize);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kFaceSize);
                Vector3 direction = SkySystem::cubeDirection(which, u, v);

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
                    radiance = up * (0.17f + 0.05f * (1.0f - fade));
                }

                // Urban inter-reflection. Half a street's ambient light is the
                // sunlit building opposite, not the sky: a façade in shade is
                // lit by a big warm grey reflector across the road, and without
                // it every shadowed elevation in the scene reads as flat blue.
                // Applied around the horizon, where the buildings are, and
                // scaled by how much sun there is to bounce.
                const float horizonBand = 1.0f - std::clamp(std::fabs(direction.Y) * 2.6f, 0.0f,
                                                            1.0f);
                if (horizonBand > 0.0f)
                {
                    const Vector3 facade(0.94f, 0.90f, 0.82f);   // sunlit render, roughly
                    const float lit = std::clamp(sunDirection_.Y, 0.0f, 1.0f);
                    const Vector3 bounce = Vector3(sunColour_.X * facade.X, sunColour_.Y * facade.Y,
                                                   sunColour_.Z * facade.Z)
                                           * (0.10f * lit);
                    radiance = radiance + bounce * horizonBand;
                }

                const std::size_t at = (static_cast<std::size_t>(index) * kFaceSize
                                        + static_cast<std::size_t>(y))
                                           * kFaceSize
                                       + static_cast<std::size_t>(x);
                faces[at] = radiance;
                peak = std::max(peak, std::max(radiance.X, std::max(radiance.Y, radiance.Z)));
            }
    }

    // A little headroom above the peak so the brightest texel is not pinned at
    // 255, and a floor so a night sky does not amplify its own quantisation.
    environmentScale_ = std::max(peak * 1.02f, 0.25f);

    std::vector<Color> face(static_cast<std::size_t>(kFaceSize) * kFaceSize);
    for (int index = 0; index < 6; ++index)
    {
        for (int y = 0; y < kFaceSize; ++y)
            for (int x = 0; x < kFaceSize; ++x)
            {
                const std::size_t at = (static_cast<std::size_t>(index) * kFaceSize
                                        + static_cast<std::size_t>(y))
                                           * kFaceSize
                                       + static_cast<std::size_t>(x);
                const Vector3& radiance = faces[at];
                face[static_cast<std::size_t>(y) * kFaceSize + static_cast<std::size_t>(x)] =
                    Color(static_cast<int>(Encode(radiance.X, environmentScale_)),
                          static_cast<int>(Encode(radiance.Y, environmentScale_)),
                          static_cast<int>(Encode(radiance.Z, environmentScale_)), 255);
            }
        environment_->SetData(static_cast<CubeMapFace>(index), face.data(),
                              static_cast<int>(face.size()));
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
    irradiance_  = processor.generateIrradiance(environment_.get(), 32, 64);
    // 96 px and six mips: the top mip is a mirror and the bottom is a
    // hemisphere, and the roughness ramp between them is what a car's paint,
    // a shop window and a wet kerb all read their reflection from.
    prefiltered_ = processor.generatePrefilteredSpecular(environment_.get(), 96,
                                                         prefilteredMips_, 64);
    brdfLut_     = processor.generateBrdfLut(96, 128);

    CNA::Logger::Info("cna-street: environment baked -- peak radiance "
                      + std::to_string(peak) + ", stored at 1/" + std::to_string(environmentScale_));
}

void SkySystem::draw(const Matrix& view, const Matrix& projection, int width, int height,
                     float timeSeconds, float intensityScale, bool encodeSrgb)
{
    if (!supported_ || effect_ == nullptr || fullscreen_ == nullptr) return;
    if (width <= 0 || height <= 0) return;

    const Matrix inverse = Matrix::Invert(RotationOnly(view) * projection);
    effect_->Apply();
    effect_->SetUniformMat4("uInverseViewProjection", &inverse.M11);
    const Vector3 travel = lightDirection();
    effect_->SetUniformVec3("uSunDirection", travel.X, travel.Y, travel.Z);
    effect_->SetUniformFloat("uTurbidity", turbidity_);
    effect_->SetUniformFloat("uIntensity", skyIntensity_ * intensityScale);
    effect_->SetUniformFloat("uTime", timeSeconds * cloudSpeed_ * 60.0f);
    effect_->SetUniformFloat("uCloudCoverage", cloudCoverage_);
    effect_->SetUniformFloat("uCloudsEnabled", cloudsEnabled_ ? 1.0f : 0.0f);
    effect_->SetUniformFloat("uFlipV", flipV_ ? 1.0f : 0.0f);
    effect_->SetUniformFloat("uEncodeSrgb", encodeSrgb ? 1.0f : 0.0f);

    fullscreen_->drawOverCurrentTarget(white_.get(), effect_.get(), width, height);
}

}  // namespace CnaStreet
