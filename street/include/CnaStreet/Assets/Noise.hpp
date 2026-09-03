// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace CnaStreet::Noise {

/**
 * @file Noise.hpp
 * @brief The noise the city's surfaces are made of.
 *
 * Header-only and hash-based rather than table-based on purpose: with no
 * permutation table to initialise there is no shared state, so the texture
 * generators can be run on several threads without any coordination, and the
 * result depends only on the coordinates and the seed. That is what makes an
 * asphalt texture byte-identical between a debug build, a release build and a
 * different machine.
 *
 * Every function here is *tileable* on a given period. A road surface that does
 * not tile is a road surface with a visible seam every few metres.
 */

/// 32-bit integer hash (a variant of Wang/Jenkins mixing). Cheap and well
/// distributed, which is all a value-noise lattice needs.
[[nodiscard]] inline std::uint32_t hashInt(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] inline std::uint32_t hash2(int x, int y, std::uint32_t seed)
{
    return hashInt(static_cast<std::uint32_t>(x) * 0x27D4EB2Du
                   ^ hashInt(static_cast<std::uint32_t>(y) * 0x165667B1u ^ seed));
}

[[nodiscard]] inline std::uint32_t hash3(int x, int y, int z, std::uint32_t seed)
{
    return hashInt(hash2(x, y, seed) ^ (static_cast<std::uint32_t>(z) * 0x9E3779B9u));
}

/// Uniform in [0,1) from a lattice cell.
[[nodiscard]] inline float random2(int x, int y, std::uint32_t seed)
{
    return static_cast<float>(hash2(x, y, seed)) * (1.0f / 4294967296.0f);
}

[[nodiscard]] inline float random3(int x, int y, int z, std::uint32_t seed)
{
    return static_cast<float>(hash3(x, y, z, seed)) * (1.0f / 4294967296.0f);
}

/// Quintic smoothstep — C2 continuous, so a normal map derived from it has no
/// visible lattice creases the way a cubic one does.
[[nodiscard]] inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

[[nodiscard]] inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/// Positive modulo, so a negative coordinate still lands in the tile.
[[nodiscard]] inline int wrap(int v, int period)
{
    if (period <= 0) return v;
    const int m = v % period;
    return m < 0 ? m + period : m;
}

/// Tileable 2-D value noise on an integer lattice with the given period.
[[nodiscard]] inline float value2(float x, float y, int period, std::uint32_t seed)
{
    const int   xi = static_cast<int>(std::floor(x));
    const int   yi = static_cast<int>(std::floor(y));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);

    const int x0 = wrap(xi, period), x1 = wrap(xi + 1, period);
    const int y0 = wrap(yi, period), y1 = wrap(yi + 1, period);

    const float u = fade(xf);
    const float v = fade(yf);
    return lerp(lerp(random2(x0, y0, seed), random2(x1, y0, seed), u),
                lerp(random2(x0, y1, seed), random2(x1, y1, seed), u), v);
}

/// Tileable gradient (Perlin-style) noise. Sharper features than value noise
/// and no axis-aligned bias, which matters for anything that is going to be
/// turned into a normal map.
[[nodiscard]] inline float gradient2(float x, float y, int period, std::uint32_t seed)
{
    const int   xi = static_cast<int>(std::floor(x));
    const int   yi = static_cast<int>(std::floor(y));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);

    auto dot = [&](int cx, int cy, float dx, float dy) {
        const float angle = random2(wrap(cx, period), wrap(cy, period), seed) * 6.28318530718f;
        return std::cos(angle) * dx + std::sin(angle) * dy;
    };

    const float u = fade(xf);
    const float v = fade(yf);
    const float n00 = dot(xi, yi, xf, yf);
    const float n10 = dot(xi + 1, yi, xf - 1.0f, yf);
    const float n01 = dot(xi, yi + 1, xf, yf - 1.0f);
    const float n11 = dot(xi + 1, yi + 1, xf - 1.0f, yf - 1.0f);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 0.5f + 0.5f;
}

/// Fractional Brownian motion over value noise. The general-purpose "surface
/// variation" function: dirt, staining, mottling, cloud cover.
[[nodiscard]] inline float fbm(float x, float y, int period, int octaves, float lacunarity,
                               float gain, std::uint32_t seed)
{
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
    float frequency = 1.0f;
    int   octavePeriod = period;
    for (int i = 0; i < octaves; ++i)
    {
        sum += value2(x * frequency, y * frequency, octavePeriod, seed + static_cast<std::uint32_t>(i) * 7919u)
               * amplitude;
        total += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
        octavePeriod = static_cast<int>(static_cast<float>(octavePeriod) * lacunarity);
    }
    return total > 0.0f ? sum / total : 0.0f;
}

/// Ridged multifractal — the sharp creases used for cracks and bark.
[[nodiscard]] inline float ridged(float x, float y, int period, int octaves, std::uint32_t seed)
{
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f, frequency = 1.0f;
    int   octavePeriod = period;
    for (int i = 0; i < octaves; ++i)
    {
        const float n = gradient2(x * frequency, y * frequency, octavePeriod,
                                  seed + static_cast<std::uint32_t>(i) * 6151u);
        const float r = 1.0f - std::fabs(n * 2.0f - 1.0f);
        sum += r * r * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
        octavePeriod *= 2;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

/// Worley / cellular noise. Returns the distance to the nearest feature point
/// (F1) and, through @p secondOut, to the second nearest — F2-F1 is what draws
/// the joints between cobbles, paving slabs and cracked mud.
[[nodiscard]] inline float worley2(float x, float y, int period, std::uint32_t seed,
                                   float* secondOut = nullptr)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    float best = 1e9f, second = 1e9f;

    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int cx = xi + dx, cy = yi + dy;
            const std::uint32_t h = hash2(wrap(cx, period), wrap(cy, period), seed);
            const float px = static_cast<float>(cx)
                             + static_cast<float>(h & 0xFFFFu) * (1.0f / 65536.0f);
            const float py = static_cast<float>(cy)
                             + static_cast<float>((h >> 16) & 0xFFFFu) * (1.0f / 65536.0f);
            const float ddx = px - x, ddy = py - y;
            const float d = std::sqrt(ddx * ddx + ddy * ddy);
            if (d < best) { second = best; best = d; }
            else if (d < second) { second = d; }
        }
    if (secondOut != nullptr) *secondOut = second;
    return best;
}

[[nodiscard]] inline float saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }

/**
 * @brief Hermite interpolation between two edges, in either direction.
 *
 * GLSL leaves `smoothstep` undefined when `edge0 >= edge1`; this one does not,
 * and that matters because a *descending* pair is how you write "1 where the
 * value is small". Rejecting it — returning a hard 0/1 step instead — silently
 * turns every such ramp in a texture generator into a binary mask, which is a
 * class of bug you can look straight at without seeing: the texture is still
 * plausible, just hard-edged everywhere it should be soft.
 */
[[nodiscard]] inline float smoothstep(float edge0, float edge1, float x)
{
    if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace CnaStreet::Noise
