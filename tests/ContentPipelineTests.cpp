// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The mip chain the content pipeline depends on.
 *
 * `GenerateRgba8MipChain` is a change to CNA rather than to this project (see
 * docs/cna-findings.md, CNA-F12), and it ships with GTest cases in CNA's own
 * suite. It is exercised here as well, and deliberately: the street's compiled
 * content set is unusable without it -- a texture with one level aliases on
 * every surface seen at a grazing angle, which is most of a street -- so a
 * regression in it would silently make the content path worse than the
 * procedural one, and nothing in this repository's own tests would notice.
 */
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"

#include "TestSupport.hpp"

#include <cstdint>
#include <vector>

using CNA::Content::Cnb::CnbMipColorSpace;
using CNA::Content::Cnb::CnbTextureData;
using CNA::Content::Cnb::CnbTextureLevelDimensions;
using CNA::Content::Cnb::GenerateRgba8MipChain;
using CNA::Content::Cnb::MakeRgba8Texture2DData;

namespace {

CnbTextureData Solid(std::uint32_t width, std::uint32_t height, std::uint8_t value)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u, value);
    return MakeRgba8Texture2DData(width, height, std::move(pixels));
}

}  // namespace

int main()
{
    CASE("a chain runs all the way down to one texel, correctly sized");
    {
        CnbTextureData data = Solid(512u, 512u, 128u);
        GenerateRgba8MipChain(data);
        CHECK(data.mipCount == 10u);
        CHECK(data.representations.size() == 1u);
        CHECK(data.representations.front().levels.size() == 10u);
        for (std::uint32_t level = 0u; level < data.mipCount; ++level)
        {
            std::uint32_t w = 0u, h = 0u, d = 0u;
            CnbTextureLevelDimensions(data, level, w, h, d);
            CHECK(data.representations.front().levels[level].size()
                  == static_cast<std::size_t>(w) * h * 4u);
        }
    }

    CASE("non-square and non-power-of-two textures terminate");
    {
        const std::uint32_t sizes[][2] = {{1u, 16u}, {16u, 1u}, {6u, 3u}, {1u, 1u}, {512u, 128u}};
        for (const auto& size : sizes)
        {
            CnbTextureData data = Solid(size[0], size[1], 200u);
            GenerateRgba8MipChain(data);
            std::uint32_t w = 0u, h = 0u, d = 0u;
            CnbTextureLevelDimensions(data, data.mipCount - 1u, w, h, d);
            CHECK(w == 1u);
            CHECK(h == 1u);
            for (const std::uint8_t byte : data.representations.front().levels.back())
                CHECK(byte == 200u);
        }
    }

    CASE("colour maps average light, data maps average code");
    {
        // Half black, half white. In linear the answer is 128; averaged as light
        // and re-encoded it is 188. Using the wrong one is the classic mip
        // darkening artefact -- a wall that dims as it recedes.
        auto halfWhite = [] {
            std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0u);
            for (std::size_t i = 0u; i < 8u; ++i) pixels[i] = 255u;
            return MakeRgba8Texture2DData(2u, 2u, std::move(pixels));
        };

        CnbTextureData linear = halfWhite();
        GenerateRgba8MipChain(linear, CnbMipColorSpace::Linear);
        CHECK_NEAR(linear.representations.front().levels[1][0], 128.0, 1.0);

        CnbTextureData srgb = halfWhite();
        GenerateRgba8MipChain(srgb, CnbMipColorSpace::Srgb);
        CHECK_NEAR(srgb.representations.front().levels[1][0], 188.0, 2.0);
        // Alpha is coverage, never a colour, so it averages linearly either way.
        CHECK_NEAR(srgb.representations.front().levels[1][3], 128.0, 1.0);
    }

    CASE("a texture that already has a chain is refused rather than doubled");
    {
        CnbTextureData data = Solid(4u, 4u, 10u);
        GenerateRgba8MipChain(data);
        bool threw = false;
        try { GenerateRgba8MipChain(data); }
        catch (const std::exception&) { threw = true; }
        CHECK_MSG(threw, "generating a chain twice was accepted");
    }

    TEST_MAIN("content-pipeline");
}
