// SPDX-License-Identifier: MIT
//
// Bakes every procedurally generated surface to PNG.
//
// Two jobs, one program. It is the *source* stage of the asset pipeline: the
// PNGs it writes are what CNA's `cna_tool_source_to_cnb` compiles into the .cnb
// files the application loads, so the whole texture set is reproducible from
// source with one command and no network. And it is the only way to look at a
// generated texture without a GPU, which is what makes it possible to work on
// them in a headless environment.
//
// Deterministic: the same seed gives byte-identical PNGs.

#include "CnaStreet/Assets/Canvas.hpp"
#include "CnaStreet/Assets/SignFactory.hpp"
#include "CnaStreet/Assets/TextureFactory.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace CnaStreet::Assets;

namespace {

struct Options
{
    std::filesystem::path output = "assets/generated";
    std::uint32_t seed = 20260903u;
    int  size = 512;
    bool maps = true;    ///< also write the normal and ORM maps
    bool font = true;
};

int Written = 0;

void Write(const std::filesystem::path& directory, const std::string& name, const Image& image,
           bool srgb)
{
    if (image.empty()) return;
    const std::filesystem::path path = directory / (name + ".png");
    if (!image.writePng(path.string(), srgb))
    {
        std::fprintf(stderr, "bake-assets: could not write %s\n", path.string().c_str());
        std::exit(1);
    }
    ++Written;
    std::printf("  %-34s %4dx%-4d %s\n", (name + ".png").c_str(), image.width(), image.height(),
                srgb ? "sRGB" : "linear");
}

void WriteSurface(const Options& options, const std::string& name, const SurfaceMaps& maps)
{
    Write(options.output, name + "_albedo", maps.albedo, true);
    if (!options.maps) return;
    Write(options.output, name + "_normal", maps.normal, false);
    Write(options.output, name + "_orm", maps.orm, false);
    if (maps.hasEmissive()) Write(options.output, name + "_emissive", maps.emissive, true);
}

/// The overlay font atlas, baked the same way `DebugOverlay` bakes it so that
/// looking at this PNG tells you what the overlay will look like.
Image FontAtlas()
{
    constexpr int kAtlasSize   = 512;
    constexpr int kCell        = 32;
    constexpr int kColumns     = 16;
    constexpr int kGlyphPixels = 22;
    constexpr int kInsetX = 3, kInsetY = 4;

    Canvas canvas(kAtlasSize, 0.0f, 0.0f, 0.0f, 0.0f);
    const float white[3] = {1.0f, 1.0f, 1.0f};
    const float em = static_cast<float>(kGlyphPixels) / static_cast<float>(kAtlasSize);
    for (int code = 32; code <= 126; ++code)
    {
        const int index = code - 32;
        const int column = index % kColumns;
        const int row    = index / kColumns;
        canvas.text(std::string(1, static_cast<char>(code)),
                    static_cast<float>(column * kCell + kInsetX) / static_cast<float>(kAtlasSize),
                    static_cast<float>(row * kCell + kInsetY) / static_cast<float>(kAtlasSize), em,
                    2.0f / static_cast<float>(kAtlasSize), white);
    }
    return canvas.image();
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto value = [&](const char* what) -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "bake-assets: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--output" || arg == "-o") options.output = value("--output");
        else if (arg == "--seed")  options.seed = static_cast<std::uint32_t>(std::strtoul(value("--seed"), nullptr, 10));
        else if (arg == "--size")  options.size = std::atoi(value("--size"));
        else if (arg == "--no-maps") options.maps = false;
        else if (arg == "--no-font") options.font = false;
        else if (arg == "--help" || arg == "-h")
        {
            std::printf("bake-assets -- writes every generated surface as a PNG\n\n"
                        "  -o, --output <dir>   destination (default: assets/generated)\n"
                        "      --seed <n>       procedural seed (default: 20260903)\n"
                        "      --size <n>       base texture size (default: 512)\n"
                        "      --no-maps        albedo only, no normal/ORM\n"
                        "      --no-font        skip the overlay font atlas\n");
            return 0;
        }
        else
        {
            std::fprintf(stderr, "bake-assets: unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::filesystem::create_directories(options.output);
    const std::uint32_t s = options.seed;
    const int large  = options.size;
    const int medium = std::max(64, options.size / 2);
    const int small  = std::max(32, options.size / 4);

    std::printf("bake-assets: seed %u -> %s\n", s, options.output.string().c_str());

    const float white[3] = {1.0f, 1.0f, 1.0f};

    WriteSurface(options, "asphalt_main", TextureFactory::asphalt(large, s + 1u, 0.55f));
    WriteSurface(options, "asphalt_side", TextureFactory::asphalt(large, s + 2u, 0.32f));
    WriteSurface(options, "asphalt_worn", TextureFactory::asphalt(large, s + 3u, 0.92f));
    WriteSurface(options, "road_marking", TextureFactory::roadPaint(medium, s + 4u, 0.45f));
    WriteSurface(options, "concrete_paving", TextureFactory::concretePaving(large, s + 5u));
    WriteSurface(options, "tactile_paving", TextureFactory::tactilePaving(medium, s + 6u));
    WriteSurface(options, "granite_setts", TextureFactory::graniteSetts(large, s + 7u));
    WriteSurface(options, "granite_kerb", TextureFactory::graniteKerb(medium, s + 8u));
    WriteSurface(options, "manhole_iron", TextureFactory::manholeCover(medium, s + 9u));
    WriteSurface(options, "grass", TextureFactory::grass(medium, s + 10u));
    WriteSurface(options, "brick_red", TextureFactory::brick(large, s + 11u, 0.08f, 0.55f));
    WriteSurface(options, "brick_buff", TextureFactory::brick(large, s + 12u, 0.85f, 0.40f));
    WriteSurface(options, "render", TextureFactory::plaster(large, s + 13u, white, 0.60f));
    WriteSurface(options, "ashlar", TextureFactory::ashlar(large, s + 14u, 0.55f));
    WriteSurface(options, "roof_tile", TextureFactory::roofTile(medium, s + 16u));
    WriteSurface(options, "roof_felt", TextureFactory::roofFelt(medium, s + 17u));
    WriteSurface(options, "sheet_metal", TextureFactory::sheetMetal(medium, s + 18u));
    WriteSurface(options, "glazing", TextureFactory::windowGlass(medium, s + 19u));
    WriteSurface(options, "interior", TextureFactory::interiorAtlas(large, s + 20u));
    WriteSurface(options, "painted", TextureFactory::paintedMetal(small, s + 21u, white, 0.38f));
    WriteSurface(options, "hardwood", TextureFactory::hardwood(small, s + 22u));
    WriteSurface(options, "car_paint", TextureFactory::carPaint(medium, s + 24u, white, 0.6f));
    WriteSurface(options, "skin", TextureFactory::skin(small, s + 26u, white));
    WriteSurface(options, "clothing", TextureFactory::fabric(small, s + 27u, white));
    WriteSurface(options, "bark", TextureFactory::bark(medium, s + 28u));
    WriteSurface(options, "foliage", TextureFactory::foliageCard(medium, s + 29u));

    for (int face = 0; face < static_cast<int>(SignFace::Count); ++face)
    {
        const SignFace which = static_cast<SignFace>(face);
        WriteSurface(options, std::string("sign_") + SignFactory::faceName(which),
                     SignFactory::face(which, medium, s + 40u));
    }
    WriteSurface(options, "sign_street_plate",
                 SignFactory::streetPlate("LINDENSTRASSE", medium, medium / 4, s + 41u));
    const float board[3] = {0.06f, 0.05f, 0.05f};
    WriteSurface(options, "shop_fascia",
                 SignFactory::shopFascia("BÄCKEREI", board, white, medium, medium / 4, s + 42u));
    WriteSurface(options, "licence_plate", SignFactory::licencePlate("B MX 4271", medium, medium / 4));

    if (options.font) Write(options.output, "font_atlas", FontAtlas(), false);

    std::printf("bake-assets: wrote %d files\n", Written);
    return 0;
}
