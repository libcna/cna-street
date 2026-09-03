// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/SignFactory.hpp"

#include "CnaStreet/Assets/Canvas.hpp"
#include "CnaStreet/Assets/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CnaStreet::Assets {

using namespace CnaStreet::Noise;

namespace {

float ToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

void Srgb8(float out[3], int r, int g, int b)
{
    out[0] = ToLinear(static_cast<float>(r) / 255.0f);
    out[1] = ToLinear(static_cast<float>(g) / 255.0f);
    out[2] = ToLinear(static_cast<float>(b) / 255.0f);
}

/// The sign palette. Signage colours are specified by standard rather than
/// chosen, so they are named here once and reused.
struct Palette
{
    float white[3], red[3], blue[3], yellow[3], black[3], grey[3], green[3];
    Palette()
    {
        Srgb8(white, 244, 244, 240);
        Srgb8(red, 176, 32, 38);
        Srgb8(blue, 24, 62, 132);
        Srgb8(yellow, 240, 190, 30);
        Srgb8(black, 26, 26, 28);
        Srgb8(grey, 128, 128, 130);
        Srgb8(green, 22, 108, 68);
    }
};

/// Finishes a drawn face: adds the sheeting's micro-texture, the weathering, and
/// the ORM map. Retroreflective sheeting is smoother than paint and, crucially,
/// is *not* metallic — a sign rendered as metal looks like a mirror at dusk.
SurfaceMaps Finish(Canvas& canvas, std::uint32_t seed, float weathering)
{
    SurfaceMaps maps;
    Image& albedo = canvas.image();
    const int w = albedo.width(), h = albedo.height();

    Image height(w, h, 0.5f, 0.0f, 0.0f, 1.0f);
    maps.orm = Image(w, h, 1.0f, 0.34f, 0.0f, 1.0f);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(w);
            const float v = static_cast<float>(y) / static_cast<float>(h);

            // The prismatic sheeting's own fine structure.
            const float cells = worley2(u * 90.0f, v * 90.0f, 90, seed + 3u);
            const float grime = fbm(u * 5.0f, v * 5.0f, 5, 3, 2.0f, 0.5f, seed + 11u);
            const float streak = smoothstep(0.55f, 0.95f,
                                            fbm(u * 40.0f, v * 1.5f, 40, 2, 2.0f, 0.5f, seed + 23u));
            const float dirt = weathering * (grime * 0.5f + streak * 0.5f);

            float* p = albedo.at(x, y);
            for (int c = 0; c < 3; ++c) p[c] *= (1.0f - dirt * 0.24f) * (0.97f + cells * 0.06f);

            height.at(x, y)[0] = 0.5f + cells * 0.08f;
            float* orm = maps.orm.at(x, y);
            orm[0] = 1.0f;
            orm[1] = std::clamp(0.30f + dirt * 0.34f + cells * 0.06f, 0.0f, 1.0f);
            orm[2] = 0.0f;
        }

    maps.normal = height.toNormalMap(0.5f);
    maps.albedo = std::move(albedo);
    return maps;
}

}  // namespace

const char* SignFactory::faceName(SignFace face)
{
    switch (face)
    {
        case SignFace::SpeedLimit30:       return "speed-limit-30";
        case SignFace::NoEntry:            return "no-entry";
        case SignFace::NoParking:          return "no-parking";
        case SignFace::GiveWay:            return "give-way";
        case SignFace::PriorityRoad:       return "priority-road";
        case SignFace::PedestrianCrossing: return "pedestrian-crossing";
        case SignFace::ChildrenWarning:    return "children-warning";
        case SignFace::OneWay:             return "one-way";
        case SignFace::ParkingArea:        return "parking-area";
        case SignFace::DeadEnd:            return "dead-end";
        case SignFace::Count:              break;
    }
    return "unknown";
}

SurfaceMaps SignFactory::face(SignFace face, int size, std::uint32_t seed)
{
    const Palette p;
    Canvas canvas(size, 0.0f, 0.0f, 0.0f, 0.0f);

    switch (face)
    {
        case SignFace::SpeedLimit30:
        {
            canvas.disc(0.5f, 0.5f, 0.470f, p.white);
            canvas.ring(0.5f, 0.5f, 0.404f, 0.116f, p.red);
            // Numerals set to about two thirds of the white field, at a stroke
            // weight near a seventh of the cap height -- the proportions a road
            // numeral is actually drawn to.
            // Set the two numerals individually: a stroke font has no kerning
            // pairs, and at signage weight a shared advance runs them together.
            canvas.text("3", 0.372f, 0.298f, 0.404f, 0.060f, p.black, true);
            canvas.text("0", 0.628f, 0.298f, 0.404f, 0.060f, p.black, true);
            break;
        }
        case SignFace::NoEntry:
        {
            canvas.disc(0.5f, 0.5f, 0.470f, p.red);
            canvas.roundedRect(0.185f, 0.418f, 0.815f, 0.582f, 0.012f, p.white);
            break;
        }
        case SignFace::NoParking:
        {
            canvas.disc(0.5f, 0.5f, 0.470f, p.blue);
            canvas.ring(0.5f, 0.5f, 0.400f, 0.140f, p.red);
            // The single diagonal bar of a no-waiting sign.
            canvas.line(0.235f, 0.765f, 0.765f, 0.235f, 0.098f, p.red);
            break;
        }
        case SignFace::GiveWay:
        {
            canvas.triangle(0.5f, 0.470f, 0.500f, false, 0.055f, p.red);
            canvas.triangle(0.5f, 0.462f, 0.352f, false, 0.040f, p.white);
            break;
        }
        case SignFace::PriorityRoad:
        {
            canvas.diamond(0.5f, 0.5f, 0.480f, 0.040f, p.white);
            canvas.diamond(0.5f, 0.5f, 0.330f, 0.028f, p.yellow);
            break;
        }
        case SignFace::PedestrianCrossing:
        {
            // Blue square with a white triangle and a walking figure crossing a
            // striped carriageway: the shape grammar of a crossing-ahead sign.
            canvas.roundedRect(0.045f, 0.045f, 0.955f, 0.955f, 0.045f, p.blue);
            canvas.triangle(0.5f, 0.520f, 0.400f, true, 0.030f, p.white);
            // Zebra stripes under the figure.
            for (int i = 0; i < 4; ++i)
            {
                const float x = 0.335f + static_cast<float>(i) * 0.088f;
                canvas.roundedRect(x, 0.690f, x + 0.046f, 0.760f, 0.004f, p.black);
            }
            // The figure: head, body, legs, arm.
            canvas.disc(0.512f, 0.386f, 0.043f, p.black);
            canvas.line(0.512f, 0.428f, 0.500f, 0.560f, 0.052f, p.black);
            canvas.line(0.500f, 0.556f, 0.452f, 0.672f, 0.040f, p.black);
            canvas.line(0.500f, 0.556f, 0.556f, 0.668f, 0.040f, p.black);
            canvas.line(0.508f, 0.462f, 0.566f, 0.532f, 0.032f, p.black);
            break;
        }
        case SignFace::ChildrenWarning:
        {
            canvas.triangle(0.5f, 0.530f, 0.500f, true, 0.055f, p.red);
            canvas.triangle(0.5f, 0.538f, 0.352f, true, 0.040f, p.white);
            // Two figures, one leading the other.
            canvas.disc(0.430f, 0.470f, 0.036f, p.black);
            canvas.line(0.430f, 0.506f, 0.420f, 0.610f, 0.044f, p.black);
            canvas.line(0.420f, 0.606f, 0.388f, 0.694f, 0.034f, p.black);
            canvas.line(0.420f, 0.606f, 0.456f, 0.694f, 0.034f, p.black);
            canvas.disc(0.560f, 0.508f, 0.030f, p.black);
            canvas.line(0.560f, 0.538f, 0.556f, 0.626f, 0.038f, p.black);
            canvas.line(0.556f, 0.622f, 0.528f, 0.696f, 0.030f, p.black);
            canvas.line(0.556f, 0.622f, 0.588f, 0.696f, 0.030f, p.black);
            canvas.line(0.446f, 0.548f, 0.548f, 0.560f, 0.026f, p.black);
            break;
        }
        case SignFace::OneWay:
        {
            canvas.roundedRect(0.030f, 0.300f, 0.970f, 0.700f, 0.025f, p.blue);
            canvas.arrow(0.180f, 0.500f, 0.830f, 0.500f, 0.120f, 0.300f, 0.230f, p.white);
            break;
        }
        case SignFace::ParkingArea:
        {
            canvas.roundedRect(0.055f, 0.055f, 0.945f, 0.945f, 0.055f, p.blue);
            canvas.text("P", 0.5f, 0.215f, 0.580f, 0.120f, p.white, true);
            break;
        }
        case SignFace::DeadEnd:
        {
            canvas.roundedRect(0.055f, 0.055f, 0.945f, 0.945f, 0.055f, p.blue);
            canvas.roundedRect(0.435f, 0.300f, 0.565f, 0.860f, 0.010f, p.white);
            canvas.roundedRect(0.240f, 0.245f, 0.760f, 0.330f, 0.010f, p.red);
            break;
        }
        case SignFace::Count:
            break;
    }

    return Finish(canvas, seed, 0.55f);
}

SurfaceMaps SignFactory::streetPlate(const std::string& name, int width, int height,
                                      std::uint32_t seed)
{
    const Palette p;
    // Drawn square then relied on to be sampled with the plate's own aspect —
    // Canvas is square because the SDF maths assumes square pixels, and a name
    // plate is a wide strip, so the design is laid out inside the top band and
    // the mesh UVs select it.
    const int size = std::max(width, height);
    Canvas canvas(size, p.blue[0], p.blue[1], p.blue[2], 1.0f);

    const float bandTop = 0.0f;
    const float bandBottom = static_cast<float>(height) / static_cast<float>(size);
    const float centreY = (bandTop + bandBottom) * 0.5f;

    canvas.rectOutline(0.020f, bandTop + 0.018f, 0.980f, bandBottom - 0.018f, 0.012f, 0.014f,
                       p.white);

    // Fit the name to the plate: long street names are set narrower, exactly as
    // a real enamel plate does.
    float textHeight = (bandBottom - bandTop) * 0.42f;
    float measured = canvas.measureText(name, textHeight);
    if (measured > 0.86f) textHeight *= 0.86f / measured;
    canvas.text(name, 0.5f, centreY - textHeight * 0.5f, textHeight, textHeight * 0.155f, p.white,
                true);

    // Below the plate area the image is unused; leave it the plate colour so a
    // sampling error shows as blue enamel rather than as a black band.
    return Finish(canvas, seed, 0.45f);
}

SurfaceMaps SignFactory::shopFascia(const std::string& name, const float boardColour[3],
                                     const float letterColour[3], int width, int height,
                                     std::uint32_t seed)
{
    const int size = std::max(width, height);
    Canvas canvas(size, boardColour[0], boardColour[1], boardColour[2], 1.0f);

    const float bandBottom = static_cast<float>(height) / static_cast<float>(size);
    const float centreY = bandBottom * 0.5f;

    float textHeight = bandBottom * 0.52f;
    float measured = canvas.measureText(name, textHeight);
    if (measured > 0.88f) textHeight *= 0.88f / measured;
    canvas.text(name, 0.5f, centreY - textHeight * 0.5f, textHeight, textHeight * 0.13f,
                letterColour, true);

    // A hairline under the lettering: most fascias have some rule or reveal, and
    // it is what keeps a plain painted board from looking like a coloured slab.
    canvas.roundedRect(0.10f, centreY + textHeight * 0.72f, 0.90f,
                       centreY + textHeight * 0.72f + bandBottom * 0.022f, 0.004f, letterColour,
                       0.7f);

    SurfaceMaps maps = Finish(canvas, seed, 0.35f);
    // Painted board, not sheeting: rougher and with no retroreflection.
    maps.orm.forEach([](int, int, float* p) { p[1] = std::clamp(p[1] + 0.28f, 0.0f, 1.0f); });
    return maps;
}

SurfaceMaps SignFactory::licencePlate(const std::string& registration, int width, int height)
{
    const Palette p;
    const int size = std::max(width, height);
    Canvas canvas(size, p.white[0], p.white[1], p.white[2], 1.0f);

    const float bandBottom = static_cast<float>(height) / static_cast<float>(size);
    // The blue identifier band down the left edge, as used across the EU.
    canvas.roundedRect(0.0f, 0.0f, 0.115f, bandBottom, 0.010f, p.blue);
    canvas.rectOutline(0.010f, 0.020f, 0.990f, bandBottom - 0.020f, 0.020f, 0.014f, p.black);

    float textHeight = bandBottom * 0.62f;
    float measured = canvas.measureText(registration, textHeight);
    if (measured > 0.80f) textHeight *= 0.80f / measured;
    canvas.text(registration, 0.565f, bandBottom * 0.5f - textHeight * 0.5f, textHeight,
                textHeight * 0.145f, p.black, true);

    return Finish(canvas, 991u, 0.30f);
}

}  // namespace CnaStreet::Assets
