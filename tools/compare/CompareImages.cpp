// SPDX-License-Identifier: MIT
//
// Compares two PNGs and says how different they are.
//
// The other half of `cna-street --capture`. The viewpoints are fixed, the scene
// comes from a seed, and the capture writes the same frames every time, so a
// change to a generator or a shader shows up as a difference in a known image
// rather than as something somebody notices six commits later.
//
// The comparison is deliberately tolerant of the differences that do not
// matter. A GPU driver, a Mesa version and an anti-aliasing decision all move
// individual pixels by a few units without changing the picture, so the test is
// the *fraction of pixels that differ noticeably*, not exact equality: an image
// regression test that fails on a driver update is an image regression test
// somebody turns off.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// stb_image expects the C library to have been included already, and in a C++
// translation unit the <cstring> names live in std:: unless they are pulled in
// first. Including it after the standard headers is not a style choice.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace {

struct Loaded
{
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;

    ~Loaded() { if (pixels != nullptr) stbi_image_free(pixels); }
};

bool Load(const char* path, Loaded& out)
{
    int channels = 0;
    out.pixels = stbi_load(path, &out.width, &out.height, &channels, 4);
    if (out.pixels == nullptr)
    {
        std::fprintf(stderr, "compare-images: could not read '%s': %s\n", path,
                     stbi_failure_reason());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string reference, candidate;
    // A pixel counts as changed when any channel moves by more than this. Eight
    // of 255 is about the point at which a difference stops being dithering.
    int channelThreshold = 8;
    // And the images differ when more than this fraction of them changed.
    double tolerance = 0.02;
    bool quiet = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto value = [&](const char* what) -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "compare-images: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--tolerance") tolerance = std::atof(value("--tolerance"));
        else if (arg == "--channel-threshold") channelThreshold = std::atoi(value("--channel-threshold"));
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--help" || arg == "-h")
        {
            std::printf(
                "compare-images <reference.png> <candidate.png> [options]\n\n"
                "Exits 0 when the two images match within tolerance, 1 when they do not,\n"
                "and 2 when it could not compare them at all.\n\n"
                "  --tolerance <f>           fraction of pixels allowed to differ (default 0.02)\n"
                "  --channel-threshold <n>   per-channel difference that counts (default 8)\n"
                "  --quiet                   print nothing when the images match\n");
            return 0;
        }
        else if (reference.empty()) reference = arg;
        else if (candidate.empty()) candidate = arg;
        else
        {
            std::fprintf(stderr, "compare-images: unexpected argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (reference.empty() || candidate.empty())
    {
        std::fprintf(stderr, "compare-images: two image paths are required (try --help)\n");
        return 2;
    }

    Loaded a, b;
    if (!Load(reference.c_str(), a) || !Load(candidate.c_str(), b)) return 2;
    if (a.width != b.width || a.height != b.height)
    {
        std::fprintf(stderr, "compare-images: %dx%d and %dx%d are different sizes\n", a.width,
                     a.height, b.width, b.height);
        return 2;
    }

    const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
    std::size_t changed = 0;
    std::size_t worstAt = 0;
    int worst = 0;
    double total = 0.0;

    for (std::size_t i = 0; i < pixels; ++i)
    {
        int biggest = 0;
        for (int c = 0; c < 3; ++c)
        {
            const int delta = std::abs(static_cast<int>(a.pixels[i * 4 + static_cast<std::size_t>(c)])
                                       - static_cast<int>(b.pixels[i * 4 + static_cast<std::size_t>(c)]));
            biggest = std::max(biggest, delta);
            total += delta;
        }
        if (biggest > channelThreshold) ++changed;
        if (biggest > worst) { worst = biggest; worstAt = i; }
    }

    const double fraction = static_cast<double>(changed) / static_cast<double>(pixels);
    const double mean = total / (static_cast<double>(pixels) * 3.0);
    const bool matches = fraction <= tolerance;

    if (!matches || !quiet)
    {
        std::printf("%s %s: %.3f%% of pixels differ (mean %.2f, worst %d at %zu,%zu) -- %s\n",
                    matches ? "OK  " : "FAIL", candidate.c_str(), fraction * 100.0, mean, worst,
                    worstAt % static_cast<std::size_t>(a.width),
                    worstAt / static_cast<std::size_t>(a.width),
                    matches ? "within tolerance" : "TOO DIFFERENT");
    }
    return matches ? 0 : 1;
}
