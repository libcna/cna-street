// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/AtmosphereModel.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::Xna::Framework::Vector3;

namespace CnaStreet::Atmosphere {

namespace {

// Rayleigh's coefficients fall as the fourth power of wavelength, which is the
// whole reason a clear sky is blue; Mie's do not depend on wavelength at all,
// which is why haze is white. Both are optical depth through one vertical
// column of air, because everything downstream is measured in air masses.
constexpr const char* kModelGlsl = R"(
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

// Kasten and Young's air mass: about 38 at the horizon instead of the secant's infinity.
float cnaAirMass(float upwards) {
    float up = clamp(upwards, 0.0, 1.0);
    float zenithDegrees = degrees(acos(up));
    return 1.0 / max(up + 0.50572 * pow(max(96.07995 - zenithDegrees, 1e-3), -1.6364), 1e-4);
}

vec3 cnaScatteringAlongPath(vec3 viewDirection, vec3 sunDirection, float turbidity,
                            float viewMass) {
    vec3 view  = normalize(viewDirection);
    vec3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);

    float sunMass = cnaAirMass(toSun.y);

    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    vec3 total     = kRayleigh + vec3(mie);
    vec3 scattered = kRayleigh * cnaRayleighPhase(cosAngle) + vec3(mie * cnaMiePhase(cosAngle));

    vec3 alongView = vec3(1.0) - exp(-total * viewMass);
    vec3 sunlight  = exp(-total * sunMass);
    return scattered / total * alongView * sunlight * kSkyScale;
}

vec3 cnaSkyRadiance(vec3 viewDirection, vec3 sunDirection, float turbidity) {
    return cnaScatteringAlongPath(viewDirection, sunDirection, turbidity,
                                  cnaAirMass(normalize(viewDirection).y));
}
)";

float AirMass(float upwards)
{
    const float up = std::clamp(upwards, 0.0f, 1.0f);
    const float zenithDegrees = std::acos(up) * 57.2957795f;
    return 1.0f / std::max(up + 0.50572f * std::pow(std::max(96.07995f - zenithDegrees, 1e-3f),
                                                    -1.6364f),
                           1e-4f);
}

Vector3 Normalised(const Vector3& v)
{
    const float lengthSquared = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (lengthSquared <= 1e-12f) return Vector3(0.0f, 1.0f, 0.0f);
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return Vector3(v.X * inverse, v.Y * inverse, v.Z * inverse);
}

}  // namespace

Vector3 radiance(const Vector3& viewDirection, const Vector3& sunTravelDirection, float turbidity)
{
    const Vector3 view  = Normalised(viewDirection);
    const Vector3 toSun = Normalised(Vector3(-sunTravelDirection.X, -sunTravelDirection.Y,
                                             -sunTravelDirection.Z));
    const float cosAngle = view.X * toSun.X + view.Y * toSun.Y + view.Z * toSun.Z;

    const float viewMass = AirMass(view.Y);
    const float sunMass  = AirMass(toSun.Y);

    const float rayleigh[3] = {0.0464f, 0.1085f, 0.2650f};
    const float mie = 0.021f * std::max(turbidity - 1.0f, 0.0f);

    const float rayleighPhase = 0.05968310365f * (1.0f + cosAngle * cosAngle);
    const float gg = 0.76f * 0.76f;
    const float d  = 1.0f + gg - 2.0f * 0.76f * cosAngle;
    const float miePhase = 0.07957747155f * (1.0f - gg)
                           / std::max(std::pow(std::max(d, 1e-4f), 1.5f) * (2.0f + gg), 1e-4f);

    float out[3] = {};
    for (int channel = 0; channel < 3; ++channel)
    {
        const float total     = rayleigh[channel] + mie;
        const float scattered = rayleigh[channel] * rayleighPhase + mie * miePhase;
        const float alongView = 1.0f - std::exp(-total * viewMass);
        const float sunlight  = std::exp(-total * sunMass);
        out[channel] = scattered / total * alongView * sunlight * 24.0f;
    }
    return Vector3(out[0], out[1], out[2]);
}

const char* modelGlsl() { return kModelGlsl; }

}  // namespace CnaStreet::Atmosphere
