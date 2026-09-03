// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/TextureFactory.hpp"

#include "CnaStreet/Assets/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace CnaStreet::Assets {

using namespace CnaStreet::Noise;

namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Builds the three maps at once from a per-pixel lambda.
///
/// The lambda receives normalised coordinates and writes linear albedo, a height
/// value, roughness, metallic and occlusion. Deriving the normal map from the
/// height afterwards -- rather than having each generator invent one -- is what
/// keeps every surface's bumps agreeing with its shading.
struct Surface
{
    Image albedo;
    Image height;
    Image orm;

    explicit Surface(int size)
        : albedo(size, size, 0.5f, 0.5f, 0.5f, 1.0f),
          height(size, size, 0.5f, 0.5f, 0.5f, 1.0f),
          orm(size, size, 1.0f, 0.8f, 0.0f, 1.0f)
    {
    }

    SurfaceMaps finish(float normalStrength)
    {
        SurfaceMaps maps;
        maps.normal = height.toNormalMap(normalStrength);
        maps.albedo = std::move(albedo);
        maps.orm    = std::move(orm);
        return maps;
    }
};

float Mix(float a, float b, float t) { return a + (b - a) * t; }

void MixRgb(float out[3], const float a[3], const float b[3], float t)
{
    for (int i = 0; i < 3; ++i) out[i] = Mix(a[i], b[i], t);
}

/// sRGB -> linear. Every colour in this file is quoted the way a paint chart or
/// a material reference quotes it -- as an sRGB byte triple -- and this is the
/// one place it becomes the linear value the renderer actually works in.
float SrgbToLinearExact(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

void Srgb8(float out[3], int r, int g, int b)
{
    out[0] = SrgbToLinearExact(static_cast<float>(r) / 255.0f);
    out[1] = SrgbToLinearExact(static_cast<float>(g) / 255.0f);
    out[2] = SrgbToLinearExact(static_cast<float>(b) / 255.0f);
}

/// Distance to the border of a cell, used for joints and bevels.
float CellEdge(float u, float v, float cellsX, float cellsY, float& cellId, float rowOffset,
               std::uint32_t seed)
{
    const float sv = v * cellsY;
    const int   row = static_cast<int>(std::floor(sv));
    const float su = u * cellsX + static_cast<float>(row) * rowOffset;
    const int   col = static_cast<int>(std::floor(su));
    cellId = static_cast<float>(hash2(col, row, seed)) * (1.0f / 4294967296.0f);
    const float fu = su - static_cast<float>(col);
    const float fv = sv - static_cast<float>(row);
    return std::min(std::min(fu, 1.0f - fu) * cellsY / cellsX, std::min(fv, 1.0f - fv));
}

}  // namespace

Image TextureFactory::white(int size)
{
    return Image(size, size, 1.0f, 1.0f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Asphalt
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::asphalt(int size, std::uint32_t seed, float wear)
{
    Surface s(size);
    wear = std::clamp(wear, 0.0f, 1.0f);

    // Fresh bitumen is nearly black (about 5 % reflectance); it greys as the
    // binder wears off the top of the aggregate. Quoting both endpoints and
    // interpolating is more honest than picking one "asphalt grey".
    float fresh[3], aged[3];
    Srgb8(fresh, 42, 42, 45);
    Srgb8(aged, 96, 95, 92);

    const float lattice = static_cast<float>(size) / 4.0f;

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // --- aggregate ---------------------------------------------------
            // Two cellular scales: the coarse 8–16 mm stones you can see, and a
            // fine sand fraction between them.
            float second = 0.0f;
            const float coarse = worley2(u * 46.0f, v * 46.0f, 46, seed + 11u, &second);
            const float stone  = smoothstep(0.42f, 0.05f, coarse);
            const float fine   = fbm(u * lattice * 4.0f, v * lattice * 4.0f,
                                     static_cast<int>(lattice * 4.0f), 4, 2.0f, 0.55f, seed + 23u);

            float base[3];
            MixRgb(base, fresh, aged, 0.25f + 0.6f * wear);
            // Exposed aggregate is lighter and greyer than the binder around it.
            const float exposure = stone * (0.25f + 0.65f * wear);
            for (int c = 0; c < 3; ++c)
                base[c] = base[c] * (1.0f - exposure) + (0.14f + 0.05f * fine) * exposure;

            float height = 0.42f + stone * 0.30f + fine * 0.12f;
            float roughness = 0.86f - stone * 0.08f + (fine - 0.5f) * 0.10f;
            float occlusion = 1.0f - (1.0f - coarse) * 0.16f;

            // --- repair patches ----------------------------------------------
            // Cut-and-fill patches over a service trench: a slightly different
            // mix, a hard edge, and a raised lip where the joint was sealed.
            const float patchField = fbm(u * 3.0f, v * 3.0f, 3, 3, 2.0f, 0.5f, seed + 71u);
            const float patch = smoothstep(0.56f, 0.60f, patchField) * (0.35f + 0.65f * wear);
            if (patch > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.72f + 0.012f, patch);
                roughness = Mix(roughness, 0.92f, patch);
                const float edge = 1.0f - std::fabs(patchField - 0.58f) * 40.0f;
                if (edge > 0.0f) height += edge * 0.05f * patch;
            }

            // --- wheel tracks --------------------------------------------------
            // Two polished bands where every tyre runs, one metre either side of
            // the lane centre. Rubber fills the surface voids, so the track is
            // darker *and* smoother than the asphalt beside it.
            const float trackCentre = std::fabs(std::fmod(v * 2.0f + 0.5f, 1.0f) - 0.5f);
            const float track = smoothstep(0.16f, 0.03f, trackCentre) * (0.30f + 0.5f * wear);
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - track * 0.30f);
            roughness -= track * 0.22f;
            height -= track * 0.05f;

            // --- cracking --------------------------------------------------------
            // Ridged noise thresholded near its crest gives branching lines that
            // meet at angles, which is what a fatigue crack actually does.
            const float crackField = ridged(u * 9.0f, v * 9.0f, 9, 4, seed + 137u);
            const float crack = smoothstep(0.78f - 0.14f * wear, 0.96f, crackField)
                                * (0.15f + 0.85f * wear);
            if (crack > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.008f, crack * 0.9f);
                height -= crack * 0.34f;
                occlusion -= crack * 0.45f;
                roughness = Mix(roughness, 0.95f, crack);
            }

            // --- staining ---------------------------------------------------------
            const float oil = smoothstep(0.70f, 0.86f,
                                         fbm(u * 6.0f + 13.0f, v * 6.0f, 6, 4, 2.1f, 0.5f,
                                             seed + 211u));
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - oil * 0.55f);
            roughness -= oil * 0.30f;

            const float dust = fbm(u * 2.0f, v * 2.0f, 2, 3, 2.0f, 0.5f, seed + 307u);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.35f + 0.010f,
                                                      smoothstep(0.55f, 0.9f, dust) * 0.35f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(1.5f);
}

// ---------------------------------------------------------------------------
// Concrete paving
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::concretePaving(int size, std::uint32_t seed)
{
    Surface s(size);
    // 3x3 slabs to the tile. At the material's 1.5 m tile size that makes each
    // slab 50 cm square, which is the standard German Gehwegplatte.
    constexpr float kCells = 3.0f;
    constexpr float kJoint = 0.028f;

    float pale[3], dark[3];
    Srgb8(pale, 186, 182, 174);
    Srgb8(dark, 132, 129, 124);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            float cellId = 0.0f;
            const float edge = CellEdge(u, v, kCells, kCells, cellId, 0.0f, seed + 5u);
            const float joint = 1.0f - smoothstep(kJoint * 0.35f, kJoint, edge);

            // Each slab is cast from a slightly different batch.
            float base[3];
            MixRgb(base, dark, pale, 0.35f + cellId * 0.55f);

            // Fine exposed aggregate, and the float marks left by the trowel.
            const float grain = fbm(u * 90.0f, v * 90.0f, 90, 3, 2.0f, 0.5f, seed + 17u);
            const float swirl = fbm(u * 7.0f, v * 7.0f, 7, 3, 2.0f, 0.6f, seed + 41u);
            for (int c = 0; c < 3; ++c)
                base[c] *= 0.90f + grain * 0.16f + (swirl - 0.5f) * 0.09f;

            float height = 0.66f + grain * 0.05f;
            float roughness = 0.72f + grain * 0.10f;
            float occlusion = 1.0f;

            // Chamfered slab edge, then the mortar joint below it.
            const float chamfer = smoothstep(kJoint, kJoint * 2.6f, edge);
            height = Mix(height - 0.10f, height, chamfer);
            if (joint > 0.0f)
            {
                float jointColour[3];
                Srgb8(jointColour, 92, 90, 86);
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], jointColour[c], joint);
                height -= joint * 0.28f;
                occlusion -= joint * 0.42f;
                roughness = Mix(roughness, 0.92f, joint);
            }

            // Weathering: the whole footway is dirtier near the kerb and under
            // the buildings, but at this scale it is just patchy grime.
            const float grime = smoothstep(0.48f, 0.90f,
                                           fbm(u * 3.0f, v * 3.0f, 3, 4, 2.0f, 0.55f, seed + 59u));
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - grime * 0.30f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(2.6f);
}

// ---------------------------------------------------------------------------
// Granite setts and kerbs
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::graniteSetts(int size, std::uint32_t seed)
{
    Surface s(size);
    constexpr float kCells = 8.0f;   // 8 setts across a 0.8 m tile -> 10 cm setts

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            float cellId = 0.0f;
            // Every other course is offset by half a sett, as they are laid.
            const float edge = CellEdge(u, v, kCells, kCells, cellId, 0.5f, seed + 3u);
            const float joint = 1.0f - smoothstep(0.02f, 0.075f, edge);

            // Grey granite, with the batch-to-batch colour spread real setts have.
            float light[3], deep[3];
            Srgb8(light, 152, 148, 143);
            Srgb8(deep, 88, 86, 88);
            float base[3];
            MixRgb(base, deep, light, 0.25f + cellId * 0.7f);

            // Mineral speckle: quartz and feldspar grains, biotite flecks.
            const float grain = worley2(u * 150.0f, v * 150.0f, 150, seed + 29u);
            const float mica  = smoothstep(0.86f, 0.98f,
                                           value2(u * 210.0f, v * 210.0f, 210, seed + 31u));
            for (int c = 0; c < 3; ++c)
                base[c] = base[c] * (0.86f + smoothstep(0.35f, 0.0f, grain) * 0.42f);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.02f, mica * 0.7f);

            // Setts are domed by a century of traffic.
            const float dome = smoothstep(0.0f, 0.30f, edge);
            float height = 0.35f + dome * 0.42f;
            float roughness = 0.55f + (1.0f - dome) * 0.18f;
            float occlusion = 0.55f + dome * 0.45f;

            if (joint > 0.0f)
            {
                float sand[3];
                Srgb8(sand, 74, 70, 62);
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], sand[c], joint);
                height -= joint * 0.30f;
                occlusion -= joint * 0.35f;
                roughness = Mix(roughness, 0.94f, joint);
            }

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(3.0f);
}

SurfaceMaps TextureFactory::graniteKerb(int size, std::uint32_t seed)
{
    Surface s(size);
    float stone[3];
    Srgb8(stone, 128, 126, 124);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Kerb stones are about a metre long; the joint runs across the run.
            const float alongJoint = std::fabs(std::fmod(u * 2.0f, 1.0f) - 0.5f) * 2.0f;
            const float joint = 1.0f - smoothstep(0.90f, 0.985f, alongJoint);

            const float grain = worley2(u * 120.0f, v * 120.0f, 120, seed + 7u);
            const float tone  = fbm(u * 4.0f, v * 4.0f, 4, 3, 2.0f, 0.5f, seed + 13u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = stone[c] * (0.80f + tone * 0.30f)
                          * (0.88f + smoothstep(0.34f, 0.0f, grain) * 0.34f);

            // The kerb face is scuffed white by tyres and kerb-side parking.
            const float scuff = smoothstep(0.62f, 0.95f,
                                           fbm(u * 14.0f, v * 3.0f, 14, 3, 2.0f, 0.5f, seed + 91u));
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.44f, scuff * 0.35f);

            float height = 0.6f + grain * 0.06f + tone * 0.05f;
            float roughness = 0.60f + grain * 0.10f - scuff * 0.10f;
            float occlusion = 1.0f;
            if (joint > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] *= (1.0f - joint * 0.45f);
                height -= joint * 0.22f;
                occlusion -= joint * 0.35f;
            }

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(2.2f);
}

SurfaceMaps TextureFactory::tactilePaving(int size, std::uint32_t seed)
{
    Surface s(size);
    // 5 x 5 blisters to the 0.4 m tile: 25 mm domes at 66 mm centres.
    constexpr float kBlisters = 5.0f;

    float slab[3];
    Srgb8(slab, 196, 176, 122);   // the buff colour used to contrast with paving

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            const float bu = std::fmod(u * kBlisters, 1.0f) - 0.5f;
            const float bv = std::fmod(v * kBlisters, 1.0f) - 0.5f;
            const float r  = std::sqrt(bu * bu + bv * bv) * 2.0f;
            const float dome = smoothstep(0.62f, 0.34f, r);

            const float grain = fbm(u * 80.0f, v * 80.0f, 80, 3, 2.0f, 0.5f, seed + 3u);
            float base[3];
            for (int c = 0; c < 3; ++c) base[c] = slab[c] * (0.88f + grain * 0.20f);
            // The tops of the blisters are polished by feet; the field is not.
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - dome * 0.10f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.35f + dome * 0.55f + grain * 0.04f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(1.0f - (1.0f - dome) * 0.12f),
                      saturate(0.80f - dome * 0.16f + grain * 0.08f), 0.0f, 1.0f);
        }

    return s.finish(4.0f);
}

// ---------------------------------------------------------------------------
// Masonry
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::brick(int size, std::uint32_t seed, float hue, float grime)
{
    Surface s(size);
    // A 1 m tile holds 4 courses of 240x71 mm bricks with 10 mm joints, laid in
    // running bond. 4 courses x 4 bricks keeps the tile square-ish and hides the
    // repeat better than 3 would.
    constexpr float kCols = 4.0f;
    constexpr float kRows = 12.0f;
    constexpr float kJointU = 0.024f;
    constexpr float kJointV = 0.075f;

    float red[3], buff[3], mortar[3];
    Srgb8(red, 142, 74, 52);
    Srgb8(buff, 176, 152, 116);
    Srgb8(mortar, 168, 164, 155);
    float family[3];
    MixRgb(family, red, buff, std::clamp(hue, 0.0f, 1.0f));

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            const float sv  = v * kRows;
            const int   row = static_cast<int>(std::floor(sv));
            const float su  = u * kCols + static_cast<float>(row) * 0.5f;
            const int   col = static_cast<int>(std::floor(su));
            const float fu  = su - static_cast<float>(col);
            const float fv  = sv - static_cast<float>(row);

            const float jointU = 1.0f - smoothstep(kJointU * 0.4f, kJointU,
                                                   std::min(fu, 1.0f - fu));
            const float jointV = 1.0f - smoothstep(kJointV * 0.4f, kJointV,
                                                   std::min(fv, 1.0f - fv));
            const float joint = std::max(jointU, jointV);

            const float id = static_cast<float>(hash2(col, row, seed)) * (1.0f / 4294967296.0f);
            const float id2 = static_cast<float>(hash2(col + 977, row, seed))
                              * (1.0f / 4294967296.0f);

            // Each brick fired slightly differently, plus a few much darker
            // "flared" headers, which is what stops brickwork looking printed.
            float base[3];
            for (int c = 0; c < 3; ++c) base[c] = family[c] * (0.72f + id * 0.55f);
            if (id2 > 0.90f)
                for (int c = 0; c < 3; ++c) base[c] *= 0.55f;
            else if (id2 < 0.06f)
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.5f + 0.03f, 0.6f);

            // Face texture: sanded, with the odd blown face.
            const float face = fbm(u * 120.0f, v * 120.0f, 120, 3, 2.0f, 0.55f, seed + 61u);
            for (int c = 0; c < 3; ++c) base[c] *= 0.90f + face * 0.20f;

            float height = 0.70f + face * 0.06f;
            float roughness = 0.80f + face * 0.10f;
            float occlusion = 1.0f;

            if (joint > 0.0f)
            {
                const float grit = fbm(u * 200.0f, v * 200.0f, 200, 2, 2.0f, 0.5f, seed + 73u);
                for (int c = 0; c < 3; ++c)
                    base[c] = Mix(base[c], mortar[c] * (0.82f + grit * 0.30f), joint);
                // Struck joint: recessed about 5 mm.
                height -= joint * 0.36f;
                occlusion -= joint * 0.45f;
                roughness = Mix(roughness, 0.93f, joint);
            }

            // A century of soot, heaviest where rain does not wash the wall.
            const float soot = smoothstep(0.35f, 0.95f,
                                          fbm(u * 2.5f, v * 1.6f, 3, 4, 2.0f, 0.55f, seed + 151u));
            const float streak = smoothstep(0.55f, 1.0f,
                                            fbm(u * 40.0f, v * 1.2f, 40, 3, 2.0f, 0.5f, seed + 163u));
            const float dirt = std::clamp(grime, 0.0f, 1.0f) * (soot * 0.7f + streak * 0.4f);
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - dirt * 0.45f);
            roughness = Mix(roughness, 0.95f, dirt * 0.5f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(3.2f);
}

SurfaceMaps TextureFactory::plaster(int size, std::uint32_t seed, const float colour[3],
                                     float grime)
{
    Surface s(size);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Rough-cast render: a fine stipple over a slow patchiness left by
            // the float, plus the faintest suggestion of the coats underneath.
            const float stipple = fbm(u * 160.0f, v * 160.0f, 160, 3, 2.0f, 0.5f, seed + 3u);
            const float patch   = fbm(u * 5.0f, v * 5.0f, 5, 4, 2.1f, 0.55f, seed + 19u);
            const float broad   = fbm(u * 1.6f, v * 1.6f, 2, 3, 2.0f, 0.6f, seed + 37u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.90f + stipple * 0.14f + (patch - 0.5f) * 0.10f
                                       + (broad - 0.5f) * 0.07f);

            float height = 0.55f + stipple * 0.18f + (patch - 0.5f) * 0.10f;
            float roughness = 0.84f + stipple * 0.10f;
            float occlusion = 1.0f;

            // Hairline cracks, and the odd blown patch where the render has come
            // away and shows darker backing.
            const float crack = smoothstep(0.90f, 0.99f, ridged(u * 7.0f, v * 7.0f, 7, 3,
                                                                seed + 53u));
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.42f, crack);
            height -= crack * 0.25f;
            occlusion -= crack * 0.3f;

            const float blown = smoothstep(0.88f, 0.97f,
                                           fbm(u * 3.5f, v * 3.5f, 4, 3, 2.0f, 0.5f, seed + 97u));
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.55f + 0.02f, blown);
            height -= blown * 0.18f;

            // Rain-washed above, dirty below — vertical streaking is the single
            // most recognisable thing about a real painted façade.
            const float run = smoothstep(0.40f, 0.98f,
                                         fbm(u * 55.0f, v * 1.3f, 55, 3, 2.0f, 0.45f, seed + 131u));
            const float dirt = std::clamp(grime, 0.0f, 1.0f) * (run * 0.65f + patch * 0.35f);
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - dirt * 0.34f);
            roughness = Mix(roughness, 0.95f, dirt * 0.4f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(1.9f);
}

SurfaceMaps TextureFactory::ashlar(int size, std::uint32_t seed, float grime)
{
    Surface s(size);
    // Coursed ashlar: 3 courses to a 2 m tile, blocks about 90 cm long.
    constexpr float kCols = 2.0f;
    constexpr float kRows = 3.0f;

    float stoneLight[3], stoneDark[3];
    Srgb8(stoneLight, 202, 190, 166);
    Srgb8(stoneDark, 158, 146, 124);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            float cellId = 0.0f;
            const float edge = CellEdge(u, v, kCols, kRows, cellId, 0.5f, seed + 5u);
            const float joint = 1.0f - smoothstep(0.006f, 0.020f, edge);

            float base[3];
            MixRgb(base, stoneDark, stoneLight, 0.25f + cellId * 0.6f);

            // Tooled face: fine parallel chisel marks, plus the bedding grain.
            const float tooling = fbm(u * 220.0f, v * 26.0f, 220, 2, 2.0f, 0.5f, seed + 11u);
            const float bedding = fbm(u * 6.0f, v * 30.0f, 6, 3, 2.0f, 0.5f, seed + 23u);
            for (int c = 0; c < 3; ++c)
                base[c] *= 0.92f + tooling * 0.12f + (bedding - 0.5f) * 0.08f;

            float height = 0.68f + tooling * 0.05f;
            float roughness = 0.78f + tooling * 0.09f;
            float occlusion = 1.0f;

            if (joint > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] *= (1.0f - joint * 0.35f);
                height -= joint * 0.30f;
                occlusion -= joint * 0.45f;
            }

            // Limestone blackens where it is sheltered and bleaches where the
            // rain hits it, which is the opposite way round from brick soot.
            const float shelter = fbm(u * 2.0f, v * 2.6f, 2, 4, 2.0f, 0.55f, seed + 89u);
            const float dirt = std::clamp(grime, 0.0f, 1.0f) * smoothstep(0.42f, 0.85f, shelter);
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - dirt * 0.52f);
            const float wash = std::clamp(grime, 0.0f, 1.0f)
                               * smoothstep(0.70f, 0.98f,
                                            fbm(u * 30.0f, v * 1.1f, 30, 2, 2.0f, 0.5f, seed + 101u));
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.25f + 0.02f, wash * 0.5f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    return s.finish(2.4f);
}

// ---------------------------------------------------------------------------
// Roofing
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::roofTile(int size, std::uint32_t seed)
{
    Surface s(size);
    // Pantiles: 6 across and 5 courses down a 2 m tile.
    constexpr float kCols = 6.0f;
    constexpr float kRows = 5.0f;

    float terracotta[3], weathered[3];
    Srgb8(terracotta, 150, 78, 48);
    Srgb8(weathered, 108, 84, 68);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            const float sv  = v * kRows;
            const int   row = static_cast<int>(std::floor(sv));
            const float su  = u * kCols + static_cast<float>(row % 2) * 0.5f;
            const int   col = static_cast<int>(std::floor(su));
            const float fu  = su - static_cast<float>(col);
            const float fv  = sv - static_cast<float>(row);

            const float id = static_cast<float>(hash2(col, row, seed)) * (1.0f / 4294967296.0f);

            // The S-profile: a roll on one side, a pan on the other.
            const float roll = std::sin(fu * kPi);
            const float profile = 0.30f + std::pow(roll, 0.6f) * 0.55f;
            // The head lap step at the top of each course.
            const float lap = smoothstep(0.0f, 0.10f, fv);

            float base[3];
            MixRgb(base, weathered, terracotta, 0.30f + id * 0.60f);
            const float grain = fbm(u * 100.0f, v * 100.0f, 100, 3, 2.0f, 0.5f, seed + 13u);
            for (int c = 0; c < 3; ++c) base[c] *= 0.86f + grain * 0.24f;

            // Lichen and moss collect in the pans and along the laps.
            float moss[3];
            Srgb8(moss, 116, 122, 84);
            const float mossField = smoothstep(0.55f, 0.92f,
                                               fbm(u * 8.0f, v * 8.0f, 8, 4, 2.0f, 0.55f,
                                                   seed + 47u))
                                    * (1.0f - roll * 0.7f);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], moss[c], mossField * 0.55f);

            const float shadow = (1.0f - lap) * 0.55f;
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - shadow);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(profile * lap + 0.10f + grain * 0.04f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(0.55f + lap * 0.45f - mossField * 0.2f),
                      saturate(0.72f + grain * 0.10f + mossField * 0.18f), 0.0f, 1.0f);
        }

    return s.finish(2.8f);
}

SurfaceMaps TextureFactory::roofFelt(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Bitumen sheet with a mineral chipping finish, laid in 1 m widths
            // with a lapped seam every roll.
            const float chip = worley2(u * 130.0f, v * 130.0f, 130, seed + 3u);
            const float seam = 1.0f - smoothstep(0.0f, 0.03f,
                                                 std::fabs(std::fmod(v * 2.0f, 1.0f) - 0.5f));
            const float patch = fbm(u * 4.0f, v * 4.0f, 4, 3, 2.0f, 0.5f, seed + 29u);

            float base[3];
            Srgb8(base, 74, 72, 70);
            const float chipping = smoothstep(0.38f, 0.06f, chip);
            for (int c = 0; c < 3; ++c)
                base[c] = base[c] * (0.85f + patch * 0.25f) * (1.0f - chipping * 0.25f)
                          + chipping * 0.10f;

            // Ponded water leaves a pale rim.
            const float pond = smoothstep(0.62f, 0.80f, patch);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.5f + 0.02f, pond * 0.35f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.55f + chipping * 0.20f - seam * 0.25f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(1.0f - seam * 0.3f), saturate(0.88f - pond * 0.30f), 0.0f,
                      1.0f);
        }
    return s.finish(1.8f);
}

SurfaceMaps TextureFactory::sheetMetal(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Weathered zinc: a dull blue-grey with a chalky carbonate bloom and
            // the standing seams of the sheets.
            float zinc[3];
            Srgb8(zinc, 138, 142, 145);
            const float bloom = fbm(u * 9.0f, v * 9.0f, 9, 4, 2.0f, 0.55f, seed + 7u);
            const float scratch = fbm(u * 300.0f, v * 12.0f, 300, 2, 2.0f, 0.5f, seed + 17u);
            const float seam = 1.0f - smoothstep(0.0f, 0.02f,
                                                 std::fabs(std::fmod(u * 3.0f, 1.0f) - 0.5f));

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = zinc[c] * (0.82f + bloom * 0.28f) * (0.94f + scratch * 0.12f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.60f + seam * 0.35f + scratch * 0.03f), 0.0f, 0.0f);
            // Metallic, but a weathered sheet is a long way from a mirror.
            s.orm.set(x, y, 1.0f, saturate(0.42f + bloom * 0.34f), 0.85f, 1.0f);
        }
    return s.finish(2.0f);
}

SurfaceMaps TextureFactory::paintedMetal(int size, std::uint32_t seed, const float colour[3],
                                          float roughness)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Powder-coated steel: a very slight orange peel, a little chalking,
            // and rust bleeding from the odd chip.
            const float peel = fbm(u * 70.0f, v * 70.0f, 70, 2, 2.0f, 0.5f, seed + 3u);
            const float chalk = fbm(u * 5.0f, v * 5.0f, 5, 3, 2.0f, 0.5f, seed + 11u);
            const float chip = smoothstep(0.955f, 0.99f,
                                          value2(u * 40.0f, v * 40.0f, 40, seed + 23u));

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.93f + chalk * 0.13f) * (0.985f + peel * 0.03f);
            float rust[3];
            Srgb8(rust, 108, 58, 30);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], rust[c], chip * 0.85f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.55f + peel * 0.10f - chip * 0.25f), 0.0f, 0.0f);
            s.orm.set(x, y, 1.0f,
                      saturate(roughness + (peel - 0.5f) * 0.06f + chalk * 0.08f + chip * 0.35f),
                      chip * 0.6f, 1.0f);
        }
    return s.finish(1.1f);
}

SurfaceMaps TextureFactory::carPaint(int size, std::uint32_t seed, const float colour[3],
                                      float metallic)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Clear-coated automotive paint: near-mirror, with the faint orange
            // peel every real body panel has and a metallic flake if asked for.
            const float peel = fbm(u * 55.0f, v * 55.0f, 55, 2, 2.0f, 0.5f, seed + 5u);
            const float flake = value2(u * 400.0f, v * 400.0f, 400, seed + 19u);
            const float dust = fbm(u * 3.0f, v * 6.0f, 3, 3, 2.0f, 0.5f, seed + 41u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.97f + (peel - 0.5f) * 0.05f)
                          + metallic * (flake - 0.5f) * 0.06f;
            // Road film gathers along the lower body and behind the wheels.
            const float film = smoothstep(0.55f, 0.95f, dust) * 0.20f;
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.75f + 0.02f, film);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.5f + (peel - 0.5f) * 0.35f), 0.0f, 0.0f);
            s.orm.set(x, y, 1.0f, saturate(0.13f + film * 0.35f + (peel - 0.5f) * 0.04f),
                      std::clamp(metallic, 0.0f, 1.0f) * 0.75f, 1.0f);
        }
    return s.finish(0.35f);
}

// ---------------------------------------------------------------------------
// Glazing
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::windowGlass(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Glass itself is featureless; what makes a pane read as glass is
            // everything *on* it. Rain streaks running down, dust caught in the
            // corners, and the faint distortion of float glass.
            const float streak = smoothstep(0.55f, 0.95f,
                                            fbm(u * 90.0f, v * 2.0f, 90, 3, 2.0f, 0.45f, seed + 3u));
            const float dust = fbm(u * 6.0f, v * 6.0f, 6, 3, 2.0f, 0.5f, seed + 17u);
            const float corner = (1.0f - smoothstep(0.0f, 0.18f, std::min(u, 1.0f - u)))
                                 + (1.0f - smoothstep(0.0f, 0.14f, std::min(v, 1.0f - v)));
            const float grime = saturate(streak * 0.55f + dust * 0.25f + corner * 0.30f);
            const float ripple = fbm(u * 3.0f, v * 3.0f, 3, 2, 2.0f, 0.5f, seed + 29u);

            // Base colour is the slight green of soda-lime float glass.
            float base[3];
            Srgb8(base, 214, 226, 220);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.30f, grime * 0.55f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            // Almost no relief -- just enough ripple to break a perfect mirror.
            s.height.setRgb(x, y, saturate(0.5f + (ripple - 0.5f) * 0.22f + grime * 0.05f), 0.0f,
                            0.0f);
            s.orm.set(x, y, 1.0f, saturate(0.045f + grime * 0.30f), 0.0f, 1.0f);
        }
    return s.finish(0.25f);
}

SurfaceMaps TextureFactory::interiorAtlas(int size, std::uint32_t seed)
{
    Surface s(size);
    // A 4x4 atlas of room interiors seen through a window. Each cell is one
    // "room": a back wall in shadow, a ceiling catching some light, and
    // sometimes a lit fitting, a curtain or a blind. Which cell a window uses is
    // chosen per window by its UV transform, so no two panes on a façade show
    // the same room.
    constexpr int kCells = 4;

    for (int cy = 0; cy < kCells; ++cy)
        for (int cx = 0; cx < kCells; ++cx)
        {
            const std::uint32_t cellSeed = hash2(cx, cy, seed);
            const float roll = static_cast<float>(cellSeed) * (1.0f / 4294967296.0f);
            const float roll2 = static_cast<float>(hashInt(cellSeed)) * (1.0f / 4294967296.0f);

            enum class Kind { Empty, Curtain, Blind, Lit } kind = Kind::Empty;
            if (roll < 0.30f)      kind = Kind::Curtain;
            else if (roll < 0.46f) kind = Kind::Blind;
            else if (roll < 0.60f) kind = Kind::Lit;

            float wall[3];
            Srgb8(wall, 96, 92, 86);
            for (int c = 0; c < 3; ++c) wall[c] *= 0.55f + roll2 * 0.7f;

            const int x0 = cx * size / kCells, x1 = (cx + 1) * size / kCells;
            const int y0 = cy * size / kCells, y1 = (cy + 1) * size / kCells;

            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const float fu = static_cast<float>(x - x0) / static_cast<float>(x1 - x0);
                    const float fv = static_cast<float>(y - y0) / static_cast<float>(y1 - y0);

                    // A room falls off into darkness toward the top and the
                    // corners; that gradient alone is most of the depth cue.
                    float depth = smoothstep(0.0f, 0.75f, fv) * 0.75f + 0.25f;
                    depth *= 1.0f - smoothstep(0.55f, 1.0f, std::fabs(fu - 0.5f) * 2.0f) * 0.5f;

                    float base[3];
                    for (int c = 0; c < 3; ++c) base[c] = wall[c] * depth * 0.35f;
                    float emissive = 0.0f;
                    float roughness = 0.9f;

                    if (kind == Kind::Curtain)
                    {
                        // Gathered fabric: vertical folds, bright where the
                        // daylight goes through them.
                        const float fold = 0.5f + 0.5f * std::sin(fu * kPi * 14.0f
                                                                  + roll2 * 6.0f);
                        float curtain[3];
                        Srgb8(curtain, 214, 206, 190);
                        const float draw = 0.35f + roll2 * 0.6f;
                        const float mask = smoothstep(draw + 0.06f, draw - 0.06f, fu);
                        for (int c = 0; c < 3; ++c)
                            base[c] = Mix(base[c], curtain[c] * (0.45f + fold * 0.5f), mask);
                        roughness = 0.95f;
                    }
                    else if (kind == Kind::Blind)
                    {
                        const float drop = 0.30f + roll2 * 0.55f;
                        if (fv < drop)
                        {
                            const float slat = 0.5f + 0.5f * std::sin(fv * kPi * 46.0f);
                            float blind[3];
                            Srgb8(blind, 196, 190, 178);
                            for (int c = 0; c < 3; ++c) base[c] = blind[c] * (0.42f + slat * 0.45f);
                        }
                    }
                    else if (kind == Kind::Lit)
                    {
                        // A warm fitting somewhere behind the glass.
                        const float lx = 0.30f + roll2 * 0.4f;
                        const float ly = 0.18f + roll * 0.2f;
                        const float d = std::sqrt((fu - lx) * (fu - lx) + (fv - ly) * (fv - ly));
                        const float glow = smoothstep(0.55f, 0.0f, d);
                        float warm[3];
                        Srgb8(warm, 255, 208, 148);
                        for (int c = 0; c < 3; ++c)
                            base[c] = Mix(base[c], warm[c] * 0.55f, glow * 0.85f);
                        emissive = glow * glow * 1.4f;
                    }

                    // A hint of a ceiling and a floor edge.
                    base[1] += smoothstep(0.14f, 0.0f, fv) * 0.02f;

                    const float noise = fbm(static_cast<float>(x) * 0.09f,
                                            static_cast<float>(y) * 0.09f, size, 2, 2.0f, 0.5f,
                                            cellSeed);
                    for (int c = 0; c < 3; ++c) base[c] *= 0.88f + noise * 0.24f;

                    s.albedo.set(x, y, base[0], base[1], base[2], 1.0f);
                    s.height.setRgb(x, y, 0.5f, 0.0f, 0.0f);
                    s.orm.set(x, y, 1.0f, roughness, 0.0f, 1.0f);
                    // Stash the emissive strength in the height map's green
                    // channel; the caller lifts it out below.
                    s.height.at(x, y)[1] = emissive;
                }
        }

    SurfaceMaps maps = s.finish(0.1f);
    maps.emissive = Image(size, size, 0.0f, 0.0f, 0.0f, 1.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float e = s.height.at(x, y)[1];
            const float* a = maps.albedo.at(x, y);
            maps.emissive.setRgb(x, y, a[0] * e, a[1] * e, a[2] * e);
        }
    return maps;
}

// ---------------------------------------------------------------------------
// Timber and vegetation
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::paintedWood(int size, std::uint32_t seed, const float colour[3])
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Brush-painted joinery: the grain telegraphs through the paint, the
            // brush left marks along it, and the paint has crazed a little.
            const float grain = fbm(u * 4.0f, v * 90.0f, 4, 4, 2.0f, 0.55f, seed + 3u);
            const float brush = fbm(u * 8.0f, v * 200.0f, 8, 2, 2.0f, 0.5f, seed + 11u);
            const float craze = smoothstep(0.86f, 0.98f, ridged(u * 22.0f, v * 22.0f, 22, 3,
                                                                seed + 29u));

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.93f + grain * 0.10f + (brush - 0.5f) * 0.05f);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.6f, craze * 0.5f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.55f + grain * 0.10f - craze * 0.2f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(1.0f - craze * 0.2f), saturate(0.38f + grain * 0.14f
                                                                    + craze * 0.3f),
                      0.0f, 1.0f);
        }
    return s.finish(1.0f);
}

SurfaceMaps TextureFactory::hardwood(int size, std::uint32_t seed)
{
    Surface s(size);
    float light[3], dark[3];
    Srgb8(light, 154, 106, 62);
    Srgb8(dark, 96, 60, 34);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Growth rings: a fast-varying coordinate warped by a slow one, which
            // is what gives timber its cathedral figure rather than stripes.
            const float warp = fbm(u * 3.0f, v * 3.0f, 3, 3, 2.0f, 0.5f, seed + 7u);
            const float rings = 0.5f + 0.5f * std::sin((v * 26.0f + warp * 6.0f) * kPi);
            const float pore = fbm(u * 240.0f, v * 30.0f, 240, 2, 2.0f, 0.5f, seed + 19u);

            float base[3];
            MixRgb(base, dark, light, saturate(rings * 0.75f + pore * 0.25f));
            for (int c = 0; c < 3; ++c) base[c] *= 0.92f + pore * 0.16f;

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.5f + (pore - 0.5f) * 0.5f + (rings - 0.5f) * 0.15f),
                            0.0f, 0.0f);
            s.orm.set(x, y, 1.0f, saturate(0.48f + pore * 0.20f), 0.0f, 1.0f);
        }
    return s.finish(1.4f);
}

SurfaceMaps TextureFactory::bark(int size, std::uint32_t seed)
{
    Surface s(size);
    float pale[3], deep[3];
    Srgb8(pale, 128, 118, 102);
    Srgb8(deep, 52, 44, 36);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Bark is vertical fissures that wander: ridged noise stretched
            // along the trunk and warped across it.
            const float warp = fbm(u * 5.0f, v * 2.0f, 5, 3, 2.0f, 0.5f, seed + 3u);
            const float fissure = ridged(u * 11.0f + warp * 1.5f, v * 3.5f, 11, 4, seed + 13u);
            const float plate = worley2(u * 14.0f, v * 5.0f, 14, seed + 23u);
            const float fine = fbm(u * 120.0f, v * 60.0f, 120, 3, 2.0f, 0.5f, seed + 31u);

            const float depth = saturate(fissure * 0.75f + smoothstep(0.4f, 0.0f, plate) * 0.35f);
            float base[3];
            MixRgb(base, deep, pale, saturate(0.25f + (1.0f - depth) * 0.65f + fine * 0.2f));

            // Moss on the north side is a nice touch but would need world data;
            // a general green tinge in the deepest fissures reads the same.
            float moss[3];
            Srgb8(moss, 84, 96, 62);
            for (int c = 0; c < 3; ++c)
                base[c] = Mix(base[c], moss[c], smoothstep(0.65f, 1.0f, depth) * 0.30f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(1.0f - depth * 0.9f + fine * 0.08f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(1.0f - depth * 0.5f), saturate(0.86f + fine * 0.10f), 0.0f,
                      1.0f);
        }
    return s.finish(3.4f);
}

SurfaceMaps TextureFactory::foliageCard(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            s.albedo.set(x, y, 0.0f, 0.0f, 0.0f, 0.0f);

    float leafLight[3], leafDark[3];
    Srgb8(leafLight, 108, 148, 62);
    Srgb8(leafDark, 44, 76, 34);

    // Scatter individual leaves as oriented ellipses with a midrib. Drawing the
    // leaves rather than thresholding noise is what makes the silhouette read as
    // foliage at the edge of the card instead of as a torn cloud.
    const int leaves = std::max(48, size * size / 900);
    for (int i = 0; i < leaves; ++i)
    {
        const std::uint32_t h = hash2(i, 0, seed);
        const float cx = static_cast<float>(h & 0xFFFFu) / 65536.0f;
        const float cy = static_cast<float>((h >> 16) & 0xFFFFu) / 65536.0f;
        const std::uint32_t h2 = hashInt(h);
        const float angle = static_cast<float>(h2 & 0xFFFFu) / 65536.0f * kPi;
        const float lengthPx = static_cast<float>(size) * (0.055f + (static_cast<float>((h2 >> 16)
                                                                     & 0xFFFFu) / 65536.0f) * 0.055f);
        const float widthPx = lengthPx * 0.46f;
        const float shade = static_cast<float>(hashInt(h2) & 0xFFFFu) / 65536.0f;

        const float ca = std::cos(angle), sa = std::sin(angle);
        const int   px = static_cast<int>(cx * static_cast<float>(size));
        const int   py = static_cast<int>(cy * static_cast<float>(size));
        const int   reach = static_cast<int>(lengthPx) + 2;

        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx)
            {
                const float lx = (static_cast<float>(dx) * ca + static_cast<float>(dy) * sa)
                                 / lengthPx;
                const float ly = (-static_cast<float>(dx) * sa + static_cast<float>(dy) * ca)
                                 / widthPx;
                // A leaf blade: pointed at both ends, widest in the middle.
                const float blade = 1.0f - (lx * lx + ly * ly * (1.0f + std::fabs(lx)));
                if (blade <= 0.0f) continue;

                const int tx = Noise::wrap(px + dx, size);
                const int ty = Noise::wrap(py + dy, size);
                float colour[3];
                MixRgb(colour, leafDark, leafLight, saturate(0.25f + shade * 0.6f
                                                             + blade * 0.25f));
                // The midrib and the veins.
                const float rib = 1.0f - smoothstep(0.0f, 0.10f, std::fabs(ly));
                for (int c = 0; c < 3; ++c) colour[c] = Mix(colour[c], colour[c] * 1.35f, rib * 0.5f);

                float* dst = s.albedo.at(tx, ty);
                const float alpha = smoothstep(0.0f, 0.18f, blade);
                if (alpha > dst[3])
                {
                    dst[0] = colour[0];
                    dst[1] = colour[1];
                    dst[2] = colour[2];
                    dst[3] = alpha;
                }
                float* h3 = s.height.at(tx, ty);
                h3[0] = std::max(h3[0], 0.35f + blade * 0.45f + rib * 0.15f);
            }
    }

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float* a = s.albedo.at(x, y);
            // Leaves are matte and slightly translucent; a low occlusion in the
            // gaps keeps the canopy from looking like a solid painted card.
            s.orm.set(x, y, saturate(0.55f + a[3] * 0.45f), 0.82f, 0.0f, 1.0f);
        }

    return s.finish(2.0f);
}

SurfaceMaps TextureFactory::grass(int size, std::uint32_t seed)
{
    Surface s(size);
    float bladeLight[3], bladeDark[3], soil[3];
    Srgb8(bladeLight, 116, 142, 66);
    Srgb8(bladeDark, 54, 76, 38);
    Srgb8(soil, 62, 50, 38);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            const float clumps = fbm(u * 12.0f, v * 12.0f, 12, 4, 2.0f, 0.55f, seed + 3u);
            const float blades = fbm(u * 190.0f, v * 90.0f, 190, 2, 2.0f, 0.5f, seed + 11u);
            const float bare = smoothstep(0.66f, 0.86f,
                                          fbm(u * 4.0f, v * 4.0f, 4, 3, 2.0f, 0.5f, seed + 23u));

            float base[3];
            MixRgb(base, bladeDark, bladeLight, saturate(clumps * 0.7f + blades * 0.4f));
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], soil[c], bare);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.4f + blades * 0.4f + clumps * 0.2f - bare * 0.3f),
                            0.0f, 0.0f);
            s.orm.set(x, y, saturate(0.7f + clumps * 0.3f), saturate(0.88f + bare * 0.08f), 0.0f,
                      1.0f);
        }
    return s.finish(2.2f);
}

SurfaceMaps TextureFactory::fabric(int size, std::uint32_t seed, const float colour[3])
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // A plain weave: warp and weft alternating, plus the fibre fuzz that
            // makes cloth read as cloth rather than as painted plastic.
            const float warp = 0.5f + 0.5f * std::sin(u * kPi * 96.0f);
            const float weft = 0.5f + 0.5f * std::sin(v * kPi * 96.0f);
            const float weave = std::max(warp, weft);
            const float fuzz = fbm(u * 200.0f, v * 200.0f, 200, 2, 2.0f, 0.5f, seed + 3u);
            const float fold = fbm(u * 6.0f, v * 6.0f, 6, 3, 2.0f, 0.55f, seed + 17u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.80f + weave * 0.18f + fuzz * 0.14f)
                          * (0.88f + fold * 0.24f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.4f + weave * 0.35f + fold * 0.2f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(0.8f + weave * 0.2f), saturate(0.90f + fuzz * 0.08f), 0.0f,
                      1.0f);
        }
    return s.finish(1.6f);
}

SurfaceMaps TextureFactory::skin(int size, std::uint32_t seed, const float colour[3])
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            const float pore = fbm(u * 220.0f, v * 220.0f, 220, 2, 2.0f, 0.5f, seed + 3u);
            const float tone = fbm(u * 7.0f, v * 7.0f, 7, 3, 2.0f, 0.5f, seed + 13u);

            float base[3];
            for (int c = 0; c < 3; ++c) base[c] = colour[c] * (0.92f + tone * 0.14f
                                                              + pore * 0.06f);
            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.5f + (pore - 0.5f) * 0.3f), 0.0f, 0.0f);
            // Skin is a dielectric with a broad, slightly wet-looking highlight.
            s.orm.set(x, y, 1.0f, saturate(0.52f + pore * 0.10f), 0.0f, 1.0f);
        }
    return s.finish(0.8f);
}

// ---------------------------------------------------------------------------
// Markings and ironwork
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::roadPaint(int size, std::uint32_t seed, float wear)
{
    Surface s(size);
    wear = std::clamp(wear, 0.0f, 1.0f);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Thermoplastic marking: bright white with glass beads in it, worn
            // through to the asphalt in the wheel paths.
            const float beads = worley2(u * 180.0f, v * 180.0f, 180, seed + 3u);
            const float scuff = fbm(u * 22.0f, v * 8.0f, 22, 3, 2.0f, 0.5f, seed + 17u);
            const float lift  = smoothstep(0.52f - wear * 0.30f, 0.86f, scuff);

            const float bead = smoothstep(0.35f, 0.0f, beads);
            float base[3];
            Srgb8(base, 236, 234, 226);
            for (int c = 0; c < 3; ++c) base[c] *= 0.90f + bead * 0.14f;
            // Grime settles into the paint's texture; a two-year-old line is
            // noticeably grey, not white.
            for (int c = 0; c < 3; ++c) base[c] *= 1.0f - wear * 0.22f;

            const float alpha = 1.0f - lift * (0.4f + wear * 0.6f);

            s.albedo.set(x, y, base[0], base[1], base[2], saturate(alpha));
            s.height.setRgb(x, y, saturate(0.6f + bead * 0.3f - lift * 0.4f), 0.0f, 0.0f);
            s.orm.set(x, y, 1.0f, saturate(0.62f + bead * 0.20f + lift * 0.2f), 0.0f, 1.0f);
        }
    return s.finish(1.2f);
}

SurfaceMaps TextureFactory::manholeCover(int size, std::uint32_t seed)
{
    Surface s(size);
    const float half = static_cast<float>(size) * 0.5f;

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float dx = (static_cast<float>(x) + 0.5f - half) / half;
            const float dy = (static_cast<float>(y) + 0.5f - half) / half;
            const float r  = std::sqrt(dx * dx + dy * dy);
            const float angle = std::atan2(dy, dx);

            // Cast iron, rust-brown where it is not polished by tyres.
            float iron[3], rust[3];
            Srgb8(iron, 76, 72, 70);
            Srgb8(rust, 96, 62, 42);
            const float rusting = fbm(dx * 4.0f + 2.0f, dy * 4.0f + 2.0f, 8, 3, 2.0f, 0.5f,
                                      seed + 3u);
            float base[3];
            MixRgb(base, iron, rust, saturate(rusting * 0.7f));

            // The cover pattern: a raised diamond grid inside a plain rim, with
            // the two lifting keyholes.
            float height = 0.35f;
            if (r < 0.94f)
            {
                height = 0.55f;
                if (r < 0.80f)
                {
                    const float gx = std::fmod(std::fabs(dx * 9.0f + dy * 9.0f), 1.0f);
                    const float gy = std::fmod(std::fabs(dx * 9.0f - dy * 9.0f), 1.0f);
                    const float ridge = std::min(std::min(gx, 1.0f - gx), std::min(gy, 1.0f - gy));
                    height = 0.55f + smoothstep(0.10f, 0.34f, ridge) * 0.35f;
                }
                // Radial rib just inside the rim.
                const float ring = 1.0f - smoothstep(0.0f, 0.02f, std::fabs(r - 0.86f));
                height = std::max(height, 0.55f + ring * 0.3f);
                // Keyholes.
                const float keyAngle = std::fabs(std::fmod(angle + kPi, kPi)) - kPi * 0.5f;
                const float key = (1.0f - smoothstep(0.0f, 0.06f, std::fabs(keyAngle)))
                                  * (1.0f - smoothstep(0.0f, 0.07f, std::fabs(r - 0.42f)));
                height -= key * 0.6f;
                for (int c = 0; c < 3; ++c) base[c] *= (1.0f - key * 0.7f);
            }
            else
            {
                // The seating ring: darker, and full of grit.
                for (int c = 0; c < 3; ++c) base[c] *= 0.6f;
            }

            const float polish = smoothstep(0.9f, 0.55f, r) * smoothstep(0.5f, 0.75f, height);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.5f + 0.02f, polish * 0.4f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(0.55f + height * 0.45f),
                      saturate(0.72f - polish * 0.30f + rusting * 0.15f), 0.75f, 1.0f);
        }
    return s.finish(3.0f);
}

SurfaceMaps TextureFactory::flat(int size, const float colour[3], float roughness, float metallic)
{
    SurfaceMaps maps;
    maps.albedo = Image(size, size, colour[0], colour[1], colour[2], 1.0f);
    maps.normal = Image(size, size, 0.5f, 0.5f, 1.0f, 1.0f);
    maps.orm    = Image(size, size, 1.0f, std::clamp(roughness, 0.02f, 1.0f),
                        std::clamp(metallic, 0.0f, 1.0f), 1.0f);
    return maps;
}

}  // namespace CnaStreet::Assets
