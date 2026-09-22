#version 450
// cna-street's sky, the Vulkan variant of SkySystem.cpp's kFragmentBody, compiled offline to SPIR-V by
// CNA's tools/shader_package/generate_shader_package.py (see package.json beside this file). The
// body below main()'s declarations is kFragmentBody verbatim, and the scattering model is CNA's own
// (AtmosphericSky::getModelGlsl()); keep all three in step. A renderer that runs SPIR-V rather than
// GLSL source addresses uniforms by type, not by name, so the named uniforms of the GLSL variant are
// the elements of three typed arrays here, in the order SkySystem::draw fills them.

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uSkyScalars[72];
};
layout(set = 1, binding = 14, std140) uniform Vec3Array
{
    vec3 uSkyVectors[72];
};
layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uSkyMatrices[72];
};

#define uTurbidity             uSkyScalars[0]
#define uIntensity             uSkyScalars[1]
#define uTime                  uSkyScalars[2]
#define uCloudCoverage         uSkyScalars[3]
#define uCloudsEnabled         uSkyScalars[4]
#define uFlipV                 uSkyScalars[5]
#define uEncodeSrgb            uSkyScalars[6]
#define uSunDirection          uSkyVectors[0]
#define uInverseViewProjection uSkyMatrices[0]

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

const vec3  kRayleigh        = vec3(0.0464, 0.1085, 0.2650);
const float kMiePerTurbidity = 0.021;
const float kMieG            = 0.76;
const float kSkyScale        = 24.0;

float cnaRayleighPhase(float cosAngle) {
    return 0.05968310365 * (1.0 + cosAngle * cosAngle);   // 3/(16*pi) * (1 + cos^2)
}

float cnaMiePhase(float cosAngle) {
    float gg = kMieG * kMieG;
    float d = 1.0 + gg - 2.0 * kMieG * cosAngle;
    return 0.07957747155 * (1.0 - gg) / max(pow(max(d, 1e-4), 1.5) * (2.0 + gg), 1e-4);
}

/// How much air a ray looking this way passes through, relative to straight up: Kasten and Young's
/// fit, which reaches about 38 at the horizon instead of the secant's infinity. The argument is the
/// sine of the elevation, so it is a direction's y component and nothing has to compute an angle.
float cnaAirMass(float upwards) {
    float up = clamp(upwards, 0.0, 1.0);
    float zenithDegrees = degrees(acos(up));
    return 1.0 / max(up + 0.50572 * pow(max(96.07995 - zenithDegrees, 1e-3), -1.6364), 1e-4);
}

// The scattering integral with the view path supplied rather than assumed. MOD-2141: the sky is
// this function with the path set to the whole atmosphere, and aerial perspective is the same
// function with the path set to however far the geometry is -- one model, called twice, rather than
// two models that agree until someone edits one of them.
vec3 cnaScatteringAlongPath(vec3 viewDirection, vec3 sunDirection, float turbidity,
                            float viewMass) {
    vec3 view  = normalize(viewDirection);
    vec3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);

    float sunMass = cnaAirMass(toSun.y);

    // Turbidity is the ratio of the whole atmosphere's optical thickness to the molecular part
    // alone, so 1 means air with no aerosol in it at all and the Mie term has to vanish there.
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    vec3 total     = kRayleigh + vec3(mie);
    vec3 scattered = kRayleigh * cnaRayleighPhase(cosAngle) + vec3(mie * cnaMiePhase(cosAngle));

    // Single scattering, integrated along the view ray in closed form. The two lengths do separate
    // jobs and must not be added together: the view path is how much air is *lit and looked
    // through*, so a longer one is brighter, while the sun path is what the light lost on the way
    // in, so a longer one is dimmer and redder. A sunset is that second term, not a tint.
    vec3 alongView = vec3(1.0) - exp(-total * viewMass);
    vec3 sunlight  = exp(-total * sunMass);
    return scattered / total * alongView * sunlight * kSkyScale;
}

vec3 cnaSkyRadiance(vec3 viewDirection, vec3 sunDirection, float turbidity) {
    return cnaScatteringAlongPath(viewDirection, sunDirection, turbidity,
                                  cnaAirMass(normalize(viewDirection).y));
}

/// What survives of a colour after this much air, per channel.
vec3 cnaAtmosphereTransmittance(float turbidity, float viewMass) {
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    return exp(-(kRayleigh + vec3(mie)) * viewMass);
}

/// The air masses a ray of this length looking this way passes through.
///
/// The model's coefficients are optical depth through **one vertical column**, so a length divided
/// by the scale height is already in the right units and no conversion is needed. The cap is not
/// cosmetic: without it a distant enough object accumulates more air than the whole sky behind it
/// has, and comes back hazier than the horizon -- which cannot happen.
float cnaAerialAirMass(vec3 viewDirection, float distance, float scaleHeight) {
    float full = cnaAirMass(normalize(viewDirection).y);
    return min(max(distance, 0.0) / max(scaleHeight, 1e-3), full);
}

/// Geometry seen through this much air: what is left of its own colour, plus what the air adds.
vec3 cnaAerialPerspective(vec3 colour, vec3 viewDirection, vec3 sunDirection, float turbidity,
                          float viewMass) {
    return colour * cnaAtmosphereTransmittance(turbidity, viewMass)
         + cnaScatteringAlongPath(viewDirection, sunDirection, turbidity, viewMass);
}

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
