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
    /// Optional; only the surfaces that glow fill it in.
    Image emissive;

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
/// Where a point sits across its own cell, 0..1. Used where something has to be
/// drawn *within* one slab or sett rather than across the tile.
float fu2(float u, float v, float cells)
{
    (void)v;
    const float su = u * cells;
    return su - std::floor(su);
}

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
            // 46 cells across a tile the road lays at six metres made every
            // stone 13 cm across: from the footway the carriageway looked like
            // bubble wrap. A 6 m tile in a 512 px map is 1.2 cm per texel, so
            // the largest aggregate that can be resolved at all is about 4 cm,
            // and that is what this draws.
            float second = 0.0f;
            // 200 cells across a 6 m tile is a 3 cm chip, which is the top of
            // the 8-16 mm grading once the binder is worn off it, and the
            // narrow smoothstep is the point: taken over 0.42 the cells merge
            // into rounded plateaus and the carriageway reads as loose gravel
            // or bubble wrap. Over 0.24 only the centre of each cell shows, so
            // what appears is chips *in* a matrix rather than stones sitting on
            // one. Rolled asphalt is a flush surface; the aggregate is visible
            // because the binder has worn off the top of it, not because the
            // stones stand proud.
            const float coarse = worley2(u * 170.0f, v * 170.0f, 170, seed + 11u, &second);
            const float stone  = smoothstep(0.30f, 0.07f, coarse);
            const float fine   = fbm(u * lattice * 4.0f, v * lattice * 4.0f,
                                     static_cast<int>(lattice * 4.0f), 4, 2.0f, 0.55f, seed + 23u);
            // The sand fraction between the chips, at the finest scale the map
            // can carry. Without it the matrix is flat and the chips read as
            // stuck-on decals.
            const float grit = fbm(u * 420.0f, v * 420.0f, 420, 2, 2.0f, 0.5f, seed + 29u);

            float base[3];
            MixRgb(base, fresh, aged, 0.25f + 0.6f * wear);
            // Exposed aggregate is lighter and greyer than the binder around
            // it, and by a good deal: a bitumen film is 3-5% reflectance and a
            // granite chip with the binder worn off it is 8-15%, so two or
            // three to one is right. What was wrong before was never the
            // contrast -- it was the *shape* of what carried it, and the
            // relief on top.
            const float exposure = stone * (0.22f + 0.55f * wear);
            for (int c = 0; c < 3; ++c)
                base[c] = base[c] * (1.0f - exposure) + (0.115f + 0.055f * fine) * exposure;
            for (int c = 0; c < 3; ++c) base[c] *= 0.90f + grit * 0.20f;

            // A tenth of the relief the chips used to carry, for the same
            // reason: they are rolled flush.
            float height = 0.46f + stone * 0.10f + fine * 0.09f + grit * 0.05f;
            float roughness = 0.86f - stone * 0.05f + (fine - 0.5f) * 0.08f;
            float occlusion = 1.0f - (1.0f - coarse) * 0.10f;

            // --- repair patches ----------------------------------------------
            // Cut-and-fill patches over a service trench: a slightly different
            // mix, a hard edge, and a raised lip where the joint was sealed.
            // Held down to a change of shade rather than a change of surface.
            // A real cut-and-fill patch is *rectangular* -- a trench reinstated
            // between saw cuts -- and a tiling texture cannot draw a rectangle
            // without drawing the same rectangle every six metres. Thresholded
            // noise at full strength gave organic blobs that read as oil spill
            // or mud; the rectangles belong in the road builder as decals, the
            // way the wheel tracks already do.
            const float patchField = fbm(u * 3.0f, v * 3.0f, 3, 3, 2.0f, 0.5f, seed + 71u);
            const float patch = smoothstep(0.56f, 0.62f, patchField) * (0.14f + 0.26f * wear);
            if (patch > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.72f + 0.012f, patch);
                roughness = Mix(roughness, 0.92f, patch);
                const float edge = 1.0f - std::fabs(patchField - 0.58f) * 40.0f;
                if (edge > 0.0f) height += edge * 0.05f * patch;
            }

            // Wheel tracks are not here, and the reason is worth writing down.
            // A tiling texture has no idea which way the road runs: the bands
            // this used to draw came out *across* the carriageway and repeated
            // every three metres, which is a road surface no vehicle has ever
            // made. Polished wheel tracks belong to a lane, not to a material,
            // and the road builder lays them as geometry.

            // --- cracking --------------------------------------------------------
            // Ridged noise thresholded near its crest gives branching lines that
            // meet at angles, which is what a fatigue crack actually does.
            // Far sparser than it was. A ridged field thresholded at 0.72 with a
            // quarter of the tile above it does not draw a crack, it draws a
            // continuous web -- crazed asphalt at the end of its life, on every
            // road in the city including the freshly surfaced ones. Threshold
            // near the crest and keep the transition tight, and what appears is
            // a few branching lines per tile, which is a road.
            const float crackField = ridged(u * 5.0f, v * 5.0f, 5, 4, seed + 137u);
            const float crack = smoothstep(0.925f - 0.055f * wear, 0.985f, crackField)
                                * (0.15f + 0.85f * wear);
            if (crack > 0.0f)
            {
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.008f, crack * 0.9f);
                height -= crack * 0.22f;
                occlusion -= crack * 0.45f;
                roughness = Mix(roughness, 0.95f, crack);
            }

            // --- staining ---------------------------------------------------------
            // Oil, and much less of it than there was. A drip is a hand's
            // breadth and it lands where cars stand still, so a tiling texture
            // that spreads it evenly at 1 m across and 55% strength does not
            // draw oil: it draws a cloudy grey wash over the whole
            // carriageway, which at a grazing angle reads as standing water.
            // Sparser, finer, and weaker -- the visible stains at a junction
            // belong in a decal pass over the lane that has them.
            const float oil = smoothstep(0.80f, 0.93f,
                                         fbm(u * 11.0f + 13.0f, v * 11.0f, 11, 4, 2.1f, 0.5f,
                                             seed + 211u));
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - oil * 0.30f);
            roughness -= oil * 0.16f;

            // The pale bloom where grit and rubber dust collect, at the coarsest
            // scale the tile has. This is the one large-scale variation worth
            // keeping: it is what stops six metres of road repeating visibly,
            // and unlike the oil it lightens, so it reads as dust rather than
            // as water.
            const float dust = fbm(u * 2.0f, v * 2.0f, 2, 3, 2.0f, 0.5f, seed + 307u);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 1.42f + 0.012f,
                                                      smoothstep(0.48f, 0.9f, dust) * 0.45f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(occlusion), saturate(roughness), 0.0f, 1.0f);
        }

    // A gentler normal than the rest of the catalogue, and gentler again than
    // it was. Tarmac has almost no relief at the scale a person walking past
    // resolves: at 0.75 with the old chip height the "Road surface" viewpoint
    // came out as a riverbed.
    return s.finish(0.42f);
}

SurfaceMaps TextureFactory::wheelTrack(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Across the band: full in the middle, gone at the edges. The
            // threshold is broken up by noise so the boundary looks worn rather
            // than cut, which is what lets an alpha *mask* stand in for a blend
            // and saves the sorting a transparent decal on a road would need.
            const float across = std::fabs(v - 0.5f) * 2.0f;
            const float fray = fbm(u * 44.0f, v * 20.0f, 44, 4, 2.0f, 0.55f, seed + 3u);
            const float speckle = value2(u * 150.0f, v * 60.0f, 150, seed + 29u);
            // A wide, noisy ramp rather than an edge. The mask is binary per
            // texel, so what makes the boundary read as wear rather than as a
            // stencil is that the *density* of kept texels falls off gradually
            // and the mip chain averages them.
            const float coverage = smoothstep(0.92f, 0.10f,
                                              across + (fray - 0.5f) * 0.26f
                                                  + (speckle - 0.5f) * 0.70f);

            // Rubber in the surface voids. This is mostly a roughness
            // difference, not a colour one: a wheel track that is markedly
            // darker than the road reads as a spill, not as polish.
            const float polish = fbm(u * 30.0f, v * 9.0f, 30, 3, 2.0f, 0.5f, seed + 17u);
            const float shade = 0.036f + polish * 0.014f;
            s.albedo.set(x, y, shade, shade, shade * 1.03f, coverage);
            s.height.setRgb(x, y, 0.5f, 0.0f, 0.0f);
            s.orm.set(x, y, 1.0f, 0.62f + polish * 0.10f, 0.0f, 1.0f);
        }
    return s.finish(0.2f);
}

// ---------------------------------------------------------------------------
// Concrete paving
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::concretePaving(int size, std::uint32_t seed, float cells)
{
    Surface s(size);
    // 8x8 slabs to the tile. At the material's 4 m tile size that keeps each
    // slab 50 cm square -- the standard German Gehwegplatte -- while giving the
    // footway sixty-four *different* slabs instead of nine.
    //
    // Nine was the whole problem. The slab size was right and the per-slab tone
    // variation was right, and the footway still read as a drawn grid, because
    // a 1.5 m tile repeats the same three-by-three block of slabs every 1.5 m:
    // across a 4 m footway that is three copies side by side and down the
    // street it is a hundred and seventy. What the eye picks up is not the slab
    // but the *group*, and a group repeating every metre and a half is a
    // pattern. Sixty-four slabs on a 4 m tile is a rhythm nobody counts.
    // A parameter rather than a constant, because three surfaces share this
    // generator and they are not the same object: a footway wants eight 50 cm
    // slabs to a 4 m tile, a precast cladding panel wants three large ones so
    // its joints stay sparse, and a shop floor wants eight small tiles to 2.4 m.
    // With the count fixed, moving it for the footway silently changed the
    // joint spacing on every concrete-clad building in the city.
    const float kCells = std::max(1.0f, cells);
    const float kJoint = 0.084f / kCells;

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

            // Lippage: each slab sits a couple of millimetres above or below
            // its neighbours, because they are laid on sand by hand. It is a
            // step at every joint rather than a groove, and it is what stops a
            // paved footway reading as a printed sheet -- the grazing light
             // along a footway finds a 2 mm step from thirty metres away.
            float height = 0.66f + grain * 0.05f + (cellId - 0.5f) * 0.085f;
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
            const float grime = smoothstep(0.44f, 0.90f,
                                           fbm(u * 3.0f, v * 3.0f, 3, 4, 2.0f, 0.55f, seed + 59u));
            for (int c = 0; c < 3; ++c) base[c] *= (1.0f - grime * 0.34f);
            roughness = Mix(roughness, 0.88f, grime * 0.5f);

            // A cracked slab, on about one in twenty. A hairline across one
            // Gehwegplatte is the sort of thing nobody notices and everybody
            // would notice the absence of, and unlike the asphalt's cracking it
            // is bounded to a single cell, so it cannot spread into a web.
            if (cellId > 0.95f)
            {
                const float across = std::fabs(fu2(u, v, kCells) - 0.5f);
                const float line = 1.0f - smoothstep(0.0f, 0.035f, across);
                for (int c = 0; c < 3; ++c) base[c] *= 1.0f - line * 0.45f;
                height -= line * 0.10f;
                occlusion -= line * 0.30f;
            }

            // The dark line where a slab meets a wall or a kerb has already
            // been handled by the joint; what is left is the chewing gum and
            // the ground-in grit that collect in the middle of a walking line.
            const float trodden = smoothstep(0.62f, 0.95f,
                                             fbm(u * 24.0f, v * 24.0f, 24, 2, 2.0f, 0.5f,
                                                 seed + 83u));
            for (int c = 0; c < 3; ++c) base[c] *= 1.0f - trodden * 0.16f;

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
            // Unit metalness in the blue channel, and the *material* decides
            // how much of it to take. This one surface drives every painted and
            // plated thing in the city -- a white window frame, a green bollard,
            // a galvanised post, an alloy wheel -- and they differ in metalness
            // as much as in colour. A map that wrote its own metalness would
            // multiply every one of those declarations by the same number; a
            // map that writes 1.0 lets a factor of 0.0 mean paint and 0.85 mean
            // machined alloy, which is what the tint table is trying to say.
            // The rust chips dip below it because iron oxide is a dielectric.
            s.orm.set(x, y, 1.0f,
                      saturate(roughness + (peel - 0.5f) * 0.06f + chalk * 0.08f + chip * 0.35f),
                      1.0f - chip * 0.85f, 1.0f);
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
            //
            // The peel is the thing to get right, and the first version had it
            // an order of magnitude too strong: real orange peel is a
            // millimetre across and a few microns deep, so at any distance a
            // human looks at a car it is a *sheen* variation, not a shape. At
            // 0.35 of relief over a 2 m tile it became a quilt crawling over
            // every panel and it was the single most obvious thing about the
            // fleet.
            const float peel  = fbm(u * 140.0f, v * 140.0f, 140, 2, 2.0f, 0.5f, seed + 5u);
            const float flake = value2(u * 512.0f, v * 512.0f, 512, seed + 19u);
            const float dust  = fbm(u * 7.0f, v * 11.0f, 7, 3, 2.0f, 0.5f, seed + 41u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.995f + (peel - 0.5f) * 0.014f)
                          + metallic * (flake - 0.5f) * 0.035f;
            // Road film gathers along the lower body and behind the wheels.
            const float film = smoothstep(0.66f, 0.99f, dust) * 0.16f;
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], base[c] * 0.72f + 0.015f, film);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.5f + (peel - 0.5f) * 0.9f), 0.0f, 0.0f);
            // 0.085 is a clearcoat, not a mirror: at 0.13 the reflection of a
            // 96 px environment cube is a hard bright blob, and at 0.05 every
            // parked car has a white disc of sky on its roof.
            s.orm.set(x, y, 1.0f, saturate(0.085f + film * 0.42f + (peel - 0.5f) * 0.02f),
                      std::clamp(metallic, 0.0f, 1.0f) * 0.78f, 1.0f);
        }
    // Relief in the thousandths, which is what a paint film has.
    return s.finish(0.015f);
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

SurfaceMaps TextureFactory::vehicleGlass(int size, std::uint32_t seed)
{
    Surface s(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Automotive glazing is *not* window glass. It is tinted, it is
            // laminated, and what you see of it from outside is almost entirely
            // reflection -- so its base colour is nearly black and the thing
            // that varies is where the wiper has and has not been. Reusing the
            // shop-window generator here gave every car a pale, milky
            // greenhouse: that image is 214,226,220, which is a *white* surface
            // once the sky reflects off it too.
            const float wiperArc = std::fabs(v - 0.34f) * 2.4f
                                   + std::fabs(u - 0.5f) * std::fabs(u - 0.5f) * 1.6f;
            const float wiped = 1.0f - smoothstep(0.30f, 0.62f, wiperArc);
            const float film = fbm(u * 26.0f, v * 26.0f, 26, 3, 2.0f, 0.5f, seed + 11u);
            const float edge = (1.0f - smoothstep(0.0f, 0.06f, std::min(u, 1.0f - u)))
                               + (1.0f - smoothstep(0.0f, 0.05f, std::min(v, 1.0f - v)));
            const float grime = saturate((film * 0.5f + 0.12f) * (1.0f - wiped * 0.75f)
                                         + edge * 0.35f);

            float base[3];
            Srgb8(base, 16, 19, 20);
            for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.34f, grime * 0.30f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, 0.5f, 0.0f, 0.0f);
            // A swept screen is a mirror; the unswept band at the edge is not.
            s.orm.set(x, y, 1.0f, saturate(0.035f + grime * 0.24f), 0.0f, 1.0f);
        }
    return s.finish(0.02f);
}

SurfaceMaps TextureFactory::interiorAtlas(int size, std::uint32_t seed)
{
    Surface s(size);
    // A 4x4 atlas of rooms seen through a window. Each window on the street
    // picks one cell through its material's texture transform, and may mirror it,
    // so sixteen rooms cover a façade of sixty windows without an obvious repeat.
    //
    // What makes a window read as a window is not the room -- it is that the
    // room is *much darker than the wall around it* and has a few bright,
    // legible things in it. Everything below is built around that: a very dark
    // base, a lighter ceiling, and one or two of curtain / blind / lamp /
    // furniture on top.
    constexpr int kCells = 4;
    const int cell = size / kCells;

    float wallColour[3], ceilingColour[3], floorColour[3];
    Srgb8(wallColour, 104, 98, 90);
    Srgb8(ceilingColour, 176, 172, 166);
    Srgb8(floorColour, 92, 74, 58);

    for (int cy = 0; cy < kCells; ++cy)
        for (int cx = 0; cx < kCells; ++cx)
        {
            const std::uint32_t cellSeed = hash2(cx * 31 + 7, cy * 17 + 3, seed);
            auto roll = [&](int index) {
                return static_cast<float>(hashInt(cellSeed + static_cast<std::uint32_t>(index)
                                                  * 2654435761u))
                       * (1.0f / 4294967296.0f);
            };

            const float dressing = roll(1);
            const bool  hasCurtain = dressing < 0.34f;
            const bool  hasBlind   = !hasCurtain && dressing < 0.55f;
            const bool  hasLamp    = roll(2) < 0.30f;
            const bool  hasFurniture = roll(3) < 0.62f;
            const float curtainDraw = 0.22f + roll(4) * 0.55f;
            const bool  curtainLeft = roll(5) < 0.5f;
            const float blindDrop   = 0.22f + roll(6) * 0.48f;
            const float lampX       = 0.22f + roll(7) * 0.56f;
            const float lampY       = 0.16f + roll(8) * 0.26f;
            const float furnitureTop = 0.62f + roll(9) * 0.22f;
            const float furnitureL  = roll(10) * 0.35f;
            const float furnitureR  = 0.55f + roll(11) * 0.45f;
            const float roomTone    = 0.55f + roll(12) * 0.9f;

            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x)
                {
                    const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(cell);
                    const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(cell);

                    // The back wall, lit only by what gets past the glass: dark,
                    // and darker toward the top where the daylight does not reach.
                    const float lightFall = smoothstep(0.0f, 0.85f, fv);
                    float base[3];
                    for (int c = 0; c < 3; ++c)
                        base[c] = wallColour[c] * roomTone * (0.020f + lightFall * 0.075f);

                    // Ceiling: the brightest thing in an unlit room, because it
                    // is the surface facing the window's own skylight.
                    const float ceiling = smoothstep(0.16f, 0.0f, fv);
                    for (int c = 0; c < 3; ++c)
                        base[c] = Mix(base[c], ceilingColour[c] * roomTone * 0.115f, ceiling);

                    // Floor, glimpsed at the very bottom.
                    const float floor = smoothstep(0.90f, 1.0f, fv);
                    for (int c = 0; c < 3; ++c)
                        base[c] = Mix(base[c], floorColour[c] * roomTone * 0.10f, floor * 0.8f);

                    // The reveal: the window opening's own sides, which are in
                    // deep shadow and frame everything else.
                    const float reveal = std::max(smoothstep(0.075f, 0.0f, fu),
                                                  smoothstep(0.925f, 1.0f, fu));
                    for (int c = 0; c < 3; ++c) base[c] *= 1.0f - reveal * 0.72f;

                    float emissive = 0.0f;

                    if (hasFurniture && fv > furnitureTop && fu > furnitureL && fu < furnitureR)
                    {
                        // A wardrobe, a sofa back, a desk: a hard-edged dark mass
                        // low in the opening. Silhouette, not detail -- through
                        // glass at 8 metres that is all there is.
                        const float edge = smoothstep(furnitureTop, furnitureTop + 0.03f, fv);
                        for (int c = 0; c < 3; ++c) base[c] *= Mix(1.0f, 0.35f, edge);
                    }

                    if (hasLamp)
                    {
                        const float dx = (fu - lampX) * 1.35f;
                        const float dy = fv - lampY;
                        const float d = std::sqrt(dx * dx + dy * dy);
                        const float core = smoothstep(0.075f, 0.02f, d);
                        const float halo = smoothstep(0.42f, 0.05f, d);
                        float warm[3];
                        Srgb8(warm, 255, 206, 148);
                        for (int c = 0; c < 3; ++c)
                            base[c] = Mix(base[c], warm[c] * 0.55f, saturate(halo * 0.55f + core));
                        emissive = core * 2.4f + halo * halo * 0.5f;
                    }

                    if (hasCurtain)
                    {
                        // Gathered fabric: the folds are what read as cloth, and
                        // the daylight coming through makes it the brightest
                        // thing in the opening.
                        const float across = curtainLeft ? fu : 1.0f - fu;
                        const float mask = smoothstep(curtainDraw + 0.05f, curtainDraw - 0.05f,
                                                      across);
                        if (mask > 0.0f)
                        {
                            const float fold = 0.5f
                                               + 0.5f * std::sin(across * kPi * 26.0f
                                                                 + roll(13) * 6.0f);
                            float fabricColour[3];
                            Srgb8(fabricColour, 226, 216, 198);
                            const float tint = roll(14);
                            if (tint > 0.72f) Srgb8(fabricColour, 196, 176, 152);
                            else if (tint < 0.18f) Srgb8(fabricColour, 178, 190, 196);
                            for (int c = 0; c < 3; ++c)
                                base[c] = Mix(base[c],
                                              fabricColour[c] * (0.16f + fold * 0.20f)
                                                  * (0.7f + lightFall * 0.5f),
                                              mask);
                        }
                    }

                    if (hasBlind && fv < blindDrop)
                    {
                        const float slat = 0.5f + 0.5f * std::sin(fv * kPi
                                                                  * static_cast<float>(cell) * 0.22f);
                        float blindColour[3];
                        Srgb8(blindColour, 216, 210, 198);
                        const float edge = smoothstep(blindDrop, blindDrop - 0.02f, fv);
                        for (int c = 0; c < 3; ++c)
                            base[c] = Mix(base[c], blindColour[c] * (0.10f + slat * 0.13f), edge);
                    }

                    // A little grain so a large window is not a flat gradient.
                    const float grain = fbm(fu * 9.0f, fv * 9.0f, 9, 3, 2.0f, 0.5f, cellSeed);
                    for (int c = 0; c < 3; ++c) base[c] *= 0.86f + grain * 0.28f;

                    const int px = cx * cell + x;
                    const int py = cy * cell + y;
                    s.albedo.set(px, py, base[0], base[1], base[2], 1.0f);
                    s.height.set(px, py, 0.5f, emissive, 0.0f, 1.0f);
                    s.orm.set(px, py, 1.0f, 0.88f, 0.0f, 1.0f);
                }
        }

    SurfaceMaps maps = s.finish(0.1f);
    maps.emissive = Image(size, size, 0.0f, 0.0f, 0.0f, 1.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float e = s.height.at(x, y)[1];
            if (e <= 0.0f) continue;
            float warm[3];
            Srgb8(warm, 255, 198, 138);
            maps.emissive.setRgb(x, y, warm[0] * e, warm[1] * e, warm[2] * e);
        }
    return maps;
}

// ---------------------------------------------------------------------------
// Timber and vegetation
// ---------------------------------------------------------------------------
SurfaceMaps TextureFactory::facadeGrid(int size, std::uint32_t seed, int bays,
                                       const float wall[3])
{
    Surface s(size);
    const int columns = std::max(1, bays);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            const int cx = static_cast<int>(u * static_cast<float>(columns));
            const float fx = u * static_cast<float>(columns) - static_cast<float>(cx);

            // One storey per tile vertically: the opening sits in the upper two
            // thirds with a spandrel below it, which is what a storey is.
            const bool inWindowX = fx > 0.24f && fx < 0.76f;
            const bool inWindowY = v > 0.16f && v < 0.74f;
            const bool window = inWindowX && inWindowY;

            // Render, with the same plaster mottle the modelled walls have so
            // that near and far buildings do not read as two materials.
            const float mottle = fbm(u * 26.0f, v * 26.0f, 26, 3, 2.0f, 0.55f, seed + 5u);
            const float grime  = smoothstep(0.55f, 1.0f, v) * 0.10f
                                 + smoothstep(0.80f, 0.30f, v) * 0.06f;
            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = wall[c] * (0.90f + mottle * 0.20f) * (1.0f - grime);

            float roughness = 0.72f + mottle * 0.16f;
            float height = 0.62f + mottle * 0.12f;

            if (window)
            {
                // What is behind one pane, decided once per bay and per tile so
                // a wall of them is a wall of *different* windows: some dark,
                // some curtained, some catching the sky.
                // Sampled per *cell* with a direct hash, not with value noise.
                // Value noise at a half-integer lattice point is the mean of
                // four randoms, so it clusters hard around 0.5: every bay came
                // out the same brightness and `lit > 0.72` almost never fired.
                // A wall of identical windows was exactly what this was written
                // to avoid.
                const float pick = Noise::random2(cx, 0, seed + 31u);
                const float lit  = Noise::random2(cx, 1, seed + 47u);
                float glass = 0.055f + 0.075f * pick;
                if (lit > 0.72f) glass = 0.34f + 0.18f * pick;          // curtain or blind
                // The sky reflected in the upper part of the pane, which is what
                // stops a far window reading as a black hole.
                const float sky = smoothstep(0.72f, 0.30f, (v - 0.16f) / 0.58f);
                base[0] = Mix(glass, 0.30f, sky * 0.55f);
                base[1] = Mix(glass, 0.34f, sky * 0.55f);
                base[2] = Mix(glass, 0.42f, sky * 0.55f);
                roughness = 0.10f;
                height    = 0.18f;   // set back in the wall

                // The frame: a light cross through the opening.
                const float mullion = 1.0f - smoothstep(0.0f, 0.020f, std::fabs(fx - 0.50f));
                const float transom = 1.0f - smoothstep(0.0f, 0.020f, std::fabs(v - 0.52f));
                const float bar = std::max(mullion, transom);
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.78f, bar);
                roughness = Mix(roughness, 0.55f, bar);
                height    = Mix(height, 0.55f, bar);
            }
            else if (inWindowX && v > 0.10f && v <= 0.16f)
            {
                // The sill, and the dirt that runs off its ends.
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.80f, 0.55f);
                height = 0.92f;
            }

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(height), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(window ? 0.72f : 1.0f), saturate(roughness), 0.0f, 1.0f);
        }
    // Enough relief that the reveals catch a shadow at a grazing sun, and not
    // so much that a 200 m wall shimmers.
    return s.finish(0.55f);
}

SurfaceMaps TextureFactory::shopStock(int size, std::uint32_t seed)
{
    Surface s(size);
    // A grid of packets, eight across and ten down, stocked the way a shelf is:
    // in *facings* -- three or four of the same product side by side, then the
    // next line. The first version picked a saturated hue per cell off a
    // six-hue wheel, and through the glass a shelf of it was confetti: a paint
    // chart, not packaging. Packaging is printed on white card, and most of a
    // shelf is white card. So most cells are pale with one coloured label band
    // across them, a few are a coloured box, a few are dark, and the colour of
    // a run is shared by the run.
    constexpr int kCols = 8;
    constexpr int kRows = 10;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            const int cx = static_cast<int>(u * kCols);
            const int cy = static_cast<int>(v * kRows);
            const int group = cx / 3 + cy * 7;
            // Per-cell hashes rather than value noise: sampled at a half-integer
            // lattice point, value noise returns the mean of four randoms and
            // lands within a tenth of 0.5 almost every time.
            const float pick   = Noise::random2(group, cy, seed + 3u);
            const float kind   = Noise::random2(group, cy + 101, seed + 5u);
            const float bright = 0.62f + 0.30f * Noise::random2(cx, cy, seed + 11u);

            // Six hues, and the label colour of the run.
            const float hue = std::floor(pick * 6.0f) / 6.0f;
            const float r = saturate(std::fabs(hue * 6.0f - 3.0f) - 1.0f);
            const float g = saturate(2.0f - std::fabs(hue * 6.0f - 2.0f));
            const float b = saturate(2.0f - std::fabs(hue * 6.0f - 4.0f));
            const float label[3] = {Mix(0.30f, r * 0.55f, 0.75f), Mix(0.30f, g * 0.55f, 0.75f),
                                    Mix(0.30f, b * 0.55f, 0.75f)};

            const float fx = u * static_cast<float>(kCols) - static_cast<float>(cx);
            const float fy = v * static_cast<float>(kRows) - static_cast<float>(cy);
            float base[3];
            if (kind < 0.62f)
            {
                // White card with a label band across the middle third, on
                // about half of them; the rest are plain card with a small
                // coloured mark, because a shelf where every packet has a
                // stripe across it is a shelf of stripes.
                const float card = bright;
                base[0] = base[1] = base[2] = card * 0.92f;
                base[2] *= 0.96f;
                const bool striped = Noise::random2(group, cy + 303, seed + 9u) < 0.5f;
                const float band = striped
                                       ? smoothstep(0.30f, 0.36f, fy) * (1.0f - smoothstep(0.62f, 0.68f, fy))
                                       : smoothstep(0.30f, 0.34f, fy) * (1.0f - smoothstep(0.44f, 0.48f, fy))
                                             * smoothstep(0.30f, 0.34f, fx) * (1.0f - smoothstep(0.62f, 0.66f, fx));
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], label[c] * 1.3f, band);
            }
            else if (kind < 0.86f)
            {
                // A coloured box with a pale panel on it.
                for (int c = 0; c < 3; ++c) base[c] = label[c] * bright * 1.15f;
                const float panel = smoothstep(0.18f, 0.24f, fy) * (1.0f - smoothstep(0.48f, 0.54f, fy))
                                    * smoothstep(0.12f, 0.18f, fx) * (1.0f - smoothstep(0.82f, 0.88f, fx));
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.70f, panel * 0.85f);
            }
            else
            {
                // A dark box: a bottle, a tin, a black carton.
                for (int c = 0; c < 3; ++c) base[c] = 0.045f + label[c] * 0.10f;
                const float band = smoothstep(0.40f, 0.46f, fy) * (1.0f - smoothstep(0.58f, 0.64f, fy));
                for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.55f, band * 0.6f);
            }

            // The seam between packets, and a small text block that reads as
            // print without being print.
            const float edge = std::min(std::min(fx, 1.0f - fx), std::min(fy, 1.0f - fy));
            const float seam = 1.0f - smoothstep(0.0f, 0.045f, edge);
            const float text = smoothstep(0.74f, 0.77f, fy) * (1.0f - smoothstep(0.84f, 0.87f, fy))
                               * smoothstep(0.20f, 0.24f, fx) * (1.0f - smoothstep(0.60f, 0.66f, fx))
                               * (0.5f + 0.5f * std::sin(fy * kPi * 20.0f));
            for (int c = 0; c < 3; ++c)
            {
                base[c] = Mix(base[c], base[c] * 0.35f, text * 0.7f);
                base[c] = Mix(base[c], base[c] * 0.22f, seam);
            }

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.72f - seam * 0.60f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(1.0f - seam * 0.45f), saturate(0.42f + (kind > 0.86f ? -0.18f : 0.0f)),
                      0.0f, 1.0f);
        }
    return s.finish(0.35f);
}

SurfaceMaps TextureFactory::posters(int size, std::uint32_t seed)
{
    Surface s(size);
    constexpr int kCells = 2;
    const int cell = size / kCells;
    // Poster papers: a strong colour, a dark one, two pale ones.
    float papers[6][3];
    Srgb8(papers[0], 214, 64, 52);
    Srgb8(papers[1], 42, 66, 122);
    Srgb8(papers[2], 236, 228, 208);
    Srgb8(papers[3], 248, 244, 236);
    Srgb8(papers[4], 232, 182, 44);
    Srgb8(papers[5], 40, 42, 46);

    for (int cy = 0; cy < kCells; ++cy)
        for (int cx = 0; cx < kCells; ++cx)
        {
            const std::uint32_t cellSeed = hash2(cx * 13 + 5, cy * 29 + 11, seed);
            auto roll = [&](int index) {
                return static_cast<float>(hashInt(cellSeed + static_cast<std::uint32_t>(index)
                                                  * 2654435761u))
                       * (1.0f / 4294967296.0f);
            };
            const int paper = static_cast<int>(roll(1) * 6.0f) % 6;
            const int ink   = (paper + 2 + static_cast<int>(roll(2) * 3.0f)) % 6;
            const bool dark = paper == 1 || paper == 5;
            const float pictureTop = 0.14f + roll(3) * 0.12f;
            const float pictureBottom = pictureTop + 0.30f + roll(4) * 0.18f;
            const int lines = 2 + static_cast<int>(roll(5) * 3.0f);
            const bool roundel = roll(6) < 0.55f;
            const float roundelX = 0.68f + roll(7) * 0.16f;
            const float roundelY = 0.70f + roll(8) * 0.12f;

            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x)
                {
                    const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(cell);
                    const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(cell);
                    float base[3] = {papers[paper][0], papers[paper][1], papers[paper][2]};
                    // The paper, with the slight unevenness of print.
                    const float grain = fbm(fu * 40.0f, fv * 40.0f, 40, 2, 2.0f, 0.5f, cellSeed);
                    for (int c = 0; c < 3; ++c) base[c] *= 0.94f + grain * 0.12f;

                    // A picture block: a second colour with a gradient across it,
                    // which is what a photograph is at this size.
                    const bool inPicture = fu > 0.10f && fu < 0.90f && fv > pictureTop
                                           && fv < pictureBottom;
                    if (inPicture)
                    {
                        const float t = (fv - pictureTop) / (pictureBottom - pictureTop);
                        for (int c = 0; c < 3; ++c)
                            base[c] = Mix(papers[ink][c] * (0.6f + 0.5f * t),
                                          base[c] * 0.5f, 0.25f);
                        const float shape = fbm(fu * 6.0f + 3.0f, fv * 6.0f, 6, 3, 2.0f, 0.5f,
                                                cellSeed + 7u);
                        for (int c = 0; c < 3; ++c) base[c] *= 0.72f + shape * 0.55f;
                    }

                    // Headline and body text as bars of ink.
                    const float inkTone = dark ? 0.86f : 0.06f;
                    float text = 0.0f;
                    const float headTop = 0.04f, headBottom = 0.11f;
                    if (fv > headTop && fv < headBottom && fu > 0.10f && fu < 0.10f + 0.55f + roll(9) * 0.3f)
                        text = 0.5f + 0.5f * std::sin(fu * kPi * 14.0f);
                    for (int line = 0; line < lines; ++line)
                    {
                        const float ly = pictureBottom + 0.05f + static_cast<float>(line) * 0.065f;
                        const float len = 0.45f + roll(20 + line) * 0.42f;
                        if (fv > ly && fv < ly + 0.028f && fu > 0.10f && fu < 0.10f + len)
                            text = std::max(text, 0.5f + 0.5f * std::sin(fu * kPi * 40.0f));
                    }
                    for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], inkTone, smoothstep(0.35f, 0.75f, text));

                    if (roundel)
                    {
                        const float d = std::sqrt((fu - roundelX) * (fu - roundelX)
                                                  + (fv - roundelY) * (fv - roundelY));
                        const float disc = 1.0f - smoothstep(0.075f, 0.085f, d);
                        float red[3];
                        Srgb8(red, 200, 36, 30);
                        for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], red[c], disc);
                        const float digits = (1.0f - smoothstep(0.045f, 0.055f, d))
                                             * (0.5f + 0.5f * std::sin(fu * kPi * 36.0f));
                        for (int c = 0; c < 3; ++c) base[c] = Mix(base[c], 0.92f, smoothstep(0.4f, 0.8f, digits));
                    }

                    const int px = cx * cell + x;
                    const int py = cy * cell + y;
                    s.albedo.set(px, py, base[0], base[1], base[2], 1.0f);
                    s.height.setRgb(px, py, 0.5f, 0.0f, 0.0f);
                    s.orm.set(px, py, 1.0f, 0.52f, 0.0f, 1.0f);
                }
        }
    return s.finish(0.0f);
}

SurfaceMaps TextureFactory::grimeDecals(int size, std::uint32_t seed)
{
    Surface s(size);
    constexpr int kCells = 2;
    const int cell = size / kCells;
    for (int cy = 0; cy < kCells; ++cy)
        for (int cx = 0; cx < kCells; ++cx)
        {
            const int which = cy * kCells + cx;
            const std::uint32_t cellSeed = hash2(cx, cy, seed);
            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x)
                {
                    const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(cell);
                    const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(cell);
                    float alpha = 0.0f;
                    float tone  = 0.05f;
                    switch (which)
                    {
                        case 0:
                        {
                            // Rain run-off from a sill: streaks that start dense
                            // at the top edge and fan out and fade downwards,
                            // heaviest at the two ends where the sill's drip
                            // fails.
                            const float streak = fbm(fu * 18.0f, fv * 1.4f, 18, 3, 2.0f, 0.5f,
                                                     cellSeed + 3u);
                            const float ends = 0.55f + 0.45f * std::pow(std::fabs(fu - 0.5f) * 2.0f, 1.5f);
                            const float fall = std::pow(1.0f - fv, 1.6f);
                            alpha = smoothstep(0.42f, 0.80f, streak) * fall * ends;
                            alpha *= smoothstep(0.0f, 0.08f, fu) * smoothstep(1.0f, 0.92f, fu);
                            tone = 0.04f;
                            break;
                        }
                        case 1:
                        {
                            // Splash dirt at the foot of a wall: dense at the
                            // bottom, patchy, gone by half height.
                            const float patch = fbm(fu * 9.0f, fv * 5.0f, 9, 4, 2.0f, 0.55f,
                                                    cellSeed + 5u);
                            const float rise = std::pow(fv, 1.3f);
                            alpha = smoothstep(0.30f, 0.70f, patch * 0.6f + rise * 0.6f) * rise;
                            tone = 0.06f;
                            break;
                        }
                        case 2:
                        {
                            // Beside a downpipe: a soft dark band down the wall,
                            // wider towards the bottom, with green in it.
                            const float centre = std::fabs(fu - 0.5f) * 2.0f;
                            const float width = 0.25f + fv * 0.45f;
                            const float wobble = fbm(fu * 4.0f, fv * 6.0f, 4, 3, 2.0f, 0.5f,
                                                     cellSeed + 7u);
                            alpha = (1.0f - smoothstep(width * 0.4f, width, centre))
                                    * (0.35f + 0.65f * wobble) * smoothstep(0.0f, 0.15f, fv);
                            tone = 0.05f;
                            break;
                        }
                        default:
                        {
                            // A soot or damp patch: a soft blob with a ragged edge.
                            const float d = std::sqrt((fu - 0.5f) * (fu - 0.5f)
                                                      + (fv - 0.5f) * (fv - 0.5f)) * 2.0f;
                            const float ragged = fbm(fu * 7.0f, fv * 7.0f, 7, 4, 2.0f, 0.55f,
                                                     cellSeed + 11u);
                            alpha = (1.0f - smoothstep(0.35f, 0.95f, d + (ragged - 0.5f) * 0.5f))
                                    * (0.5f + 0.5f * ragged);
                            tone = 0.05f;
                            break;
                        }
                    }
                    const int px = cx * cell + x;
                    const int py = cy * cell + y;
                    // Slightly green-black: algae and soot together.
                    s.albedo.set(px, py, tone * 0.92f, tone, tone * 0.86f, saturate(alpha));
                    s.height.setRgb(px, py, 0.5f, 0.0f, 0.0f);
                    s.orm.set(px, py, 1.0f, 0.95f, 0.0f, 1.0f);
                }
        }
    return s.finish(0.0f);
}

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

SurfaceMaps TextureFactory::lightPool(int size, std::uint32_t seed)
{
    Surface s(size);
    // The pool of light a luminaire throws on the ground.
    //
    // Geometry rather than a light, because `PbrEffect` carries one punctual
    // light per draw and this street has forty lamps. A lit road under a lamp
    // is a soft-edged ellipse with a hot core, and painting one is a
    // twenty-line texture; putting forty real lights through a clustered
    // forward path to draw the same ellipse is a different application.
    //
    // Alpha carries the falloff and the emissive carries the colour, so the
    // quad blends toward the lamp's own tint over whatever it is laid on and
    // fades to nothing at its rim -- no hard edge, and the tarmac's own
    // aggregate still shows through the middle of it.
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            const float r = std::sqrt(u * u + v * v);

            // Inverse-square-ish rather than linear: the ground under a lamp is
            // bright for a couple of metres and then falls away fast, and a
            // linear ramp reads as a painted circle.
            float fall = saturate(1.0f - r);
            fall = fall * fall * (0.35f + 0.65f * fall);
            // A little noise on the rim, because a luminaire's cut-off is not a
            // perfect circle and the surface under it is not flat.
            const float ragged = 0.86f + 0.28f * fbm(u * 3.0f + 4.0f, v * 3.0f + 4.0f, 0, 3,
                                                     2.0f, 0.5f, seed + 3u);
            fall = saturate(fall * ragged);

            s.albedo.set(x, y, 0.0f, 0.0f, 0.0f, fall * 0.72f);
            s.height.setRgb(x, y, 0.5f, 0.0f, 0.0f);
            s.orm.set(x, y, 1.0f, 1.0f, 0.0f, 1.0f);
        }
    s.emissive = Image(size, size, 0.0f, 0.0f, 0.0f, 1.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float a = s.albedo.at(x, y)[3];
            s.emissive.setRgb(x, y, a, a * 0.90f, a * 0.72f);
        }
    SurfaceMaps maps = s.finish(0.0f);
    maps.emissive = std::move(s.emissive);
    return maps;
}

SurfaceMaps TextureFactory::hardwood(int size, std::uint32_t seed)
{
    Surface s(size);
    // A narrower band between the growth rings than this used to have. 154/96
    // in sRGB is a 3:1 ratio once it is linear, and a 3:1 stripe with a
    // cathedral figure warping it is not oak -- it is animal fur, which is
    // exactly what four metres of shop worktop looked like through the glass.
    // Real timber's figure is a change of shade, not of material.
    float light[3], dark[3];
    Srgb8(light, 148, 108, 70);
    Srgb8(dark, 118, 82, 52);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);

            // Growth rings: a fast-varying coordinate warped by a slow one, which
            // is what gives timber its cathedral figure rather than stripes.
            const float warp = fbm(u * 3.0f, v * 3.0f, 3, 3, 2.0f, 0.5f, seed + 7u);
            // And a smaller warp: at six the rings fold back on themselves and
            // read as contour lines rather than as grain running along a board.
            const float rings = 0.5f + 0.5f * std::sin((v * 26.0f + warp * 2.6f) * kPi);
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
    // A card covers about 2.4 m of crown, so a leaf that is 10% of it is 24 cm
    // across -- a rhubarb, not a lime. The card wants a few hundred small leaves,
    // not seventy big ones, and this is the difference between a street tree and
    // a house plant hanging over the footway.
    const int leaves = std::max(220, size * size / 190);
    for (int i = 0; i < leaves; ++i)
    {
        const std::uint32_t h = hash2(i, 0, seed);
        const float cx = static_cast<float>(h & 0xFFFFu) / 65536.0f;
        const float cy = static_cast<float>((h >> 16) & 0xFFFFu) / 65536.0f;
        const std::uint32_t h2 = hashInt(h);
        const float angle = static_cast<float>(h2 & 0xFFFFu) / 65536.0f * kPi;
        const float lengthPx = static_cast<float>(size) * (0.021f + (static_cast<float>((h2 >> 16)
                                                                     & 0xFFFFu) / 65536.0f) * 0.024f);
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
            //
            // The weave has to be *small*. A coat's thread pitch is under a
            // millimetre, and the first version put ninety-six cells across a
            // 0.55 m tile -- a six-millimetre grid, three pixels across at the
            // distance a person is seen from, with a relief of 1.6. Every
            // pedestrian in the street was wearing chain mail.
            const float warp = 0.5f + 0.5f * std::sin(u * kPi * 260.0f);
            const float weft = 0.5f + 0.5f * std::sin(v * kPi * 260.0f);
            const float weave = std::max(warp, weft);
            const float fuzz = fbm(u * 220.0f, v * 220.0f, 220, 2, 2.0f, 0.5f, seed + 3u);
            const float fold = fbm(u * 5.0f, v * 5.0f, 5, 3, 2.0f, 0.55f, seed + 17u);

            float base[3];
            for (int c = 0; c < 3; ++c)
                base[c] = colour[c] * (0.94f + weave * 0.05f + fuzz * 0.06f)
                          * (0.90f + fold * 0.20f);

            s.albedo.setRgb(x, y, base[0], base[1], base[2]);
            s.height.setRgb(x, y, saturate(0.4f + weave * 0.30f + fold * 0.30f), 0.0f, 0.0f);
            s.orm.set(x, y, saturate(0.86f + weave * 0.12f), saturate(0.88f + fuzz * 0.10f), 0.0f,
                      1.0f);
        }
    return s.finish(0.30f);
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
