// SPDX-License-Identifier: MIT
/**
 * @file AppearanceTests.cpp
 * @brief Regression tests for the shapes and surfaces the visual overhaul broke.
 *
 * Every case here corresponds to a defect that was shipped, found by looking at
 * a rendering, and fixed. A pixel comparison would have caught none of them --
 * they are all *structural*, and each one has a number attached to it that says
 * whether a human silhouette is a human silhouette. That is the difference
 * between a test that guards behaviour and a test that guards a screenshot.
 */
#include "TestSupport.hpp"

#include "CnaStreet/Assets/Noise.hpp"
#include "CnaStreet/Assets/TextureFactory.hpp"
#include "CnaStreet/Geometry/MeshBuilder.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>

using namespace CnaStreet;
using namespace Microsoft::Xna::Framework;

namespace {

/// The mean of one ORM channel over a generated surface, which is the number
/// `MaterialLibrary` divides a declared factor by.
double meanChannel(const Assets::Image& image, int channel)
{
    double sum = 0.0;
    int    n   = 0;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = 0; x < image.width(); x += 2)
        {
            sum += static_cast<double>(image.at(x, y)[channel]);
            ++n;
        }
    return n == 0 ? 0.0 : sum / n;
}

}  // namespace

int main()
{
    // ---------------------------------------------------------------------
    // The PBR factor bug: `roughness = map.g * factor`, and the catalogue was
    // writing the intended value into both.
    // ---------------------------------------------------------------------
    CASE("a generator writes the roughness it was asked for, so the factor must be 1");
    {
        // If these two agree, then declaring the same number as the material's
        // factor squares it -- which is exactly what the whole catalogue was
        // doing. The test does not assert that they agree by accident: it
        // asserts the *contract* MaterialLibrary::applyNominals depends on, that
        // a generator handed a roughness puts that roughness in the map.
        const float asked[] = {0.35f, 0.62f, 0.88f};
        for (const float r : asked)
        {
            const float white[3] = {1.0f, 1.0f, 1.0f};
            const Assets::SurfaceMaps maps = Assets::TextureFactory::paintedMetal(64, 7u, white, r);
            CHECK_NEAR(meanChannel(maps.orm, 1), static_cast<double>(r), 0.10);
        }
    }

    CASE("painted metal carries unit metalness so a material's factor can mean something");
    {
        // One surface drives a white window frame, a green bollard, a galvanised
        // post and a machined alloy wheel, and they differ in metalness as much
        // as in colour. A map that writes its own metalness multiplies every one
        // of those declarations by the same number; every metal in the city came
        // out a dielectric.
        const float white[3] = {1.0f, 1.0f, 1.0f};
        const Assets::SurfaceMaps maps = Assets::TextureFactory::paintedMetal(64, 3u, white, 0.4f);
        CHECK_MSG(meanChannel(maps.orm, 2) > 0.80,
                  "painted metal's ORM blue channel must be near 1 so the material's metallic "
                  "factor survives the multiply");
    }

    // ---------------------------------------------------------------------
    // Per-cell randomness. Value noise sampled at a half-integer lattice point
    // is the mean of four randoms, so it never leaves the middle of its range:
    // a six-hue wheel quantised from it only ever produced two of them.
    // ---------------------------------------------------------------------
    CASE("a per-cell pick spans its range");
    {
        double lo = 1.0, hi = 0.0;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
            {
                const double v = Noise::random2(x, y, 3u);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        CHECK_MSG(lo < 0.12 && hi > 0.88,
                  "a per-cell hash must reach both ends of 0..1; value noise at a half-integer "
                  "lattice point does not, and that is why a shelf of assorted packaging came "
                  "out uniformly teal");
    }

    CASE("the shop stock texture uses more than two of its six hues");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::shopStock(128, 11u);
        // Count how many of the six hue sextants the image actually visits.
        bool seen[6] = {false, false, false, false, false, false};
        for (int y = 0; y < maps.albedo.height(); y += 3)
            for (int x = 0; x < maps.albedo.width(); x += 3)
            {
                const float* px = maps.albedo.at(x, y);
                const float max = std::max(px[0], std::max(px[1], px[2]));
                const float min = std::min(px[0], std::min(px[1], px[2]));
                if (max - min < 0.012f) continue;   // grey: no hue to speak of
                float hue = 0.0f;
                if (max == px[0])      hue = (px[1] - px[2]) / (max - min);
                else if (max == px[1]) hue = 2.0f + (px[2] - px[0]) / (max - min);
                else                   hue = 4.0f + (px[0] - px[1]) / (max - min);
                if (hue < 0.0f) hue += 6.0f;
                seen[std::min(5, static_cast<int>(hue))] = true;
            }
        int used = 0;
        for (const bool s : seen) used += s ? 1 : 0;
        CHECK_MSG(used >= 4, "a shelf of packaging should visit most of the hue wheel, not two "
                             "sextants of it");
    }

    // ---------------------------------------------------------------------
    // Winding. Four corners in the obvious order wind clockwise seen from
    // above, so every pool of lamplight faced the ground and was back-face
    // culled: forty invisible lights.
    // ---------------------------------------------------------------------
    CASE("a ground quad built the obvious way faces down, and addQuadFacing fixes it");
    {
        const float r = 2.0f;
        const Vector3 a(-r, 0.0f, -r), b(r, 0.0f, -r), c(r, 0.0f, r), d(-r, 0.0f, r);

        Geometry::MeshBuilder naive;
        naive.setUvMode(Geometry::UvMode::Explicit);
        naive.addQuadUnitUv(a, b, c, d);
        CHECK_MSG(naive.mesh().vertices.front().Normal.Y < -0.9f,
                  "those four corners in that order really do face the ground -- if this ever "
                  "flips, the light pools stop needing addQuadFacing and this test is the "
                  "record of why they did");

        Geometry::MeshBuilder stated;
        stated.setUvMode(Geometry::UvMode::Explicit);
        stated.addQuadFacingUv(a, b, c, d, Vector3::Up);
        CHECK_MSG(stated.mesh().vertices.front().Normal.Y > 0.9f,
                  "addQuadFacing must turn a ground quad the right way up");
    }

    // ---------------------------------------------------------------------
    // Paving. Nine slabs to a 1.5 m tile is a three-by-three block repeating
    // every metre and a half, and what the eye picks up is the block.
    // ---------------------------------------------------------------------
    CASE("the paving map holds enough different slabs to stop reading as a pattern");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::concretePaving(256, 5u, 8.0f);
        // Sample the centre of each cell and count distinct tones. Eight cells
        // across means sixty-four slabs; if the generator ever goes back to
        // three the count collapses and this fails.
        constexpr int kCells = 8;
        int distinct = 0;
        double previous = -1.0;
        double tones[kCells * kCells];
        for (int cy = 0; cy < kCells; ++cy)
            for (int cx = 0; cx < kCells; ++cx)
            {
                const int x = (cx * 256 + 128) / kCells;
                const int y = (cy * 256 + 128) / kCells;
                tones[cy * kCells + cx] = static_cast<double>(maps.albedo.at(x, y)[0]);
            }
        std::sort(std::begin(tones), std::end(tones));
        for (const double t : tones)
        {
            // 1.5 thousandths, which is well under the average gap between
            // sixty-four samples spread over the slab palette's range: the
            // threshold is there to reject *repeats*, not to measure spacing.
            if (t - previous > 0.0015) ++distinct;
            previous = t;
        }
        CHECK_MSG(distinct >= 32,
                  "sixty-four slabs should give a good spread of tones; three-by-three gave nine "
                  "and the footway read as a drawn grid");
        CHECK_MSG(tones[kCells * kCells - 1] - tones[0] > 0.12,
                  "and the palest slab should be visibly paler than the darkest");
    }

    CASE("the paving cell count is the caller's, because three surfaces share the generator");
    {
        // A footway, a precast cladding panel and a shop floor want different
        // joint spacings out of the same generator. Moving a constant for one of
        // them silently changed the others.
        const Assets::SurfaceMaps fine   = Assets::TextureFactory::concretePaving(128, 5u, 8.0f);
        const Assets::SurfaceMaps coarse = Assets::TextureFactory::concretePaving(128, 5u, 3.0f);
        // More cells means more joint, and the joint is darker than the slab.
        CHECK_MSG(meanChannel(fine.albedo, 0) < meanChannel(coarse.albedo, 0),
                  "eight cells to the tile must show more joint than three");
    }

    // ---------------------------------------------------------------------
    // Asphalt. The aggregate could not be resolved at 1.2 cm per texel, so
    // whatever the generator did, what reached the screen was the coarse noise
    // laid on top of it.
    // ---------------------------------------------------------------------
    CASE("asphalt is dark, and wear makes it lighter rather than louder");
    {
        const Assets::SurfaceMaps fresh = Assets::TextureFactory::asphalt(128, 2u, 0.05f);
        const Assets::SurfaceMaps worn  = Assets::TextureFactory::asphalt(128, 2u, 0.95f);
        const double freshTone = meanChannel(fresh.albedo, 0);
        const double wornTone  = meanChannel(worn.albedo, 0);
        CHECK_MSG(freshTone < 0.12, "fresh bitumen is about 5% reflectance, not a grey road");
        CHECK_MSG(wornTone > freshTone, "wear exposes lighter aggregate");
        CHECK_MSG(wornTone < 0.30, "worn asphalt is still asphalt");
    }

    CASE("the asphalt normal map is nearly flat, because rolled asphalt is");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::asphalt(128, 2u, 0.55f);
        // A tangent-space normal is stored as n * 0.5 + 0.5, so a flat surface
        // is 0.5, 0.5, 1.0. At the strength this map used to carry, the road
        // came out as a riverbed.
        double away = 0.0;
        int    n    = 0;
        for (int y = 0; y < maps.normal.height(); y += 2)
            for (int x = 0; x < maps.normal.width(); x += 2)
            {
                const float* px = maps.normal.at(x, y);
                away += std::fabs(px[0] - 0.5) + std::fabs(px[1] - 0.5);
                ++n;
            }
        CHECK_MSG(n > 0 && away / n < 0.10,
                  "asphalt's relief belongs in the thousandths, not in the tenths");
    }

    TEST_MAIN("appearance");
}
