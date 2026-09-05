// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/VehicleFactory.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Microsoft::Xna::Framework;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::SurfacePatch;
using CnaStreet::Geometry::UvMode;

namespace CnaStreet {

namespace M = Metrics;

// ---------------------------------------------------------------------------
// Curve

Curve::Curve(std::initializer_list<Vector2> knots) : knots_(knots)
{
    std::sort(knots_.begin(), knots_.end(),
              [](const Vector2& a, const Vector2& b) { return a.X < b.X; });
    const std::size_t n = knots_.size();
    slopes_.assign(n, 0.0f);
    if (n < 2) return;

    // Secants first, then the Fritsch–Carlson limiter. The limiter is the whole
    // reason this is not a Catmull-Rom: it clamps each knot's tangent into the
    // circle of radius 3 around the two neighbouring secants, which is exactly
    // the condition under which the cubic cannot leave the interval between the
    // knots it joins.
    std::vector<float> secant(n - 1, 0.0f);
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        const float dx = knots_[i + 1].X - knots_[i].X;
        secant[i] = dx > 1e-6f ? (knots_[i + 1].Y - knots_[i].Y) / dx : 0.0f;
    }
    slopes_[0]     = secant.front();
    slopes_[n - 1] = secant.back();
    for (std::size_t i = 1; i + 1 < n; ++i)
    {
        if (secant[i - 1] * secant[i] <= 0.0f) { slopes_[i] = 0.0f; continue; }
        slopes_[i] = (secant[i - 1] + secant[i]) * 0.5f;
    }
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        if (std::fabs(secant[i]) < 1e-8f)
        {
            slopes_[i] = slopes_[i + 1] = 0.0f;
            continue;
        }
        const float a = slopes_[i] / secant[i];
        const float b = slopes_[i + 1] / secant[i];
        const float h = std::sqrt(a * a + b * b);
        if (h > 3.0f)
        {
            slopes_[i]     = 3.0f / h * a * secant[i];
            slopes_[i + 1] = 3.0f / h * b * secant[i];
        }
    }
}

float Curve::at(float x) const
{
    if (knots_.empty()) return 0.0f;
    if (knots_.size() == 1 || x <= knots_.front().X) return knots_.front().Y;
    if (x >= knots_.back().X) return knots_.back().Y;

    std::size_t i = 0;
    while (i + 2 < knots_.size() && knots_[i + 1].X < x) ++i;
    const float h = knots_[i + 1].X - knots_[i].X;
    if (h <= 1e-6f) return knots_[i].Y;
    const float t  = (x - knots_[i].X) / h;
    const float t2 = t * t;
    const float t3 = t2 * t;
    // Hermite basis.
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * knots_[i].Y + (t3 - 2.0f * t2 + t) * h * slopes_[i]
           + (-2.0f * t3 + 3.0f * t2) * knots_[i + 1].Y + (t3 - t2) * h * slopes_[i + 1];
}

namespace {

/// The section, as named rows. Thirteen points from the bottom of the visible
/// body up over the centre line; the other half is this one mirrored.
///
/// The indices are used by name everywhere below, because "the daylight opening
/// is rows 6 to 9" is a statement about a car and "the daylight opening is rows
/// 6 to 9" written as literals in four places is a bug waiting for the section
/// to change.
constexpr int kRowBelt     = 5;   ///< widest point, and the shoulder crease
constexpr int kRowDloBase  = 6;   ///< top of the belt strip
constexpr int kRowShoulder = 9;   ///< where the crown begins
constexpr int kRowCentre   = 12;  ///< on the centre line
constexpr int kSectionCols = 25;  ///< thirteen points a side, closed by the floor

/// Half-index of a section column: columns past the centre mirror back down.
int HalfIndex(int col) { return col <= kRowCentre ? col : (kSectionCols - 1 - col); }

/// Which band of the section a quad starting at @p col belongs to, or -1 for
/// the flat floor that closes the loop.
int BandOf(int col)
{
    if (col <= kRowCentre - 1) return col;
    if (col <= kSectionCols - 2) return kSectionCols - 2 - col;
    return -1;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

/// The longitudinal zone a station falls in. Which zone decides whether the
/// crown is roof or windscreen and whether the daylight opening is glass or
/// bodywork, and nothing else has to know about z.
enum class Zone
{
    Tail,
    Backlight,
    Cabin,
    Windscreen,
    Nose
};

Zone ZoneAt(const VehicleDimensions& d, float z)
{
    if (z < d.backlightBaseZ) return Zone::Tail;
    if (z < d.backlightTopZ) return Zone::Backlight;
    if (z < d.screenTopZ) return Zone::Cabin;
    if (z < d.screenBaseZ) return Zone::Windscreen;
    return Zone::Nose;
}

/// The height at which the wheel arch cuts the flank at this station, or the
/// rocker where no arch reaches. The arch is a circle about the axle, so this is
/// the circle where the circle is higher than the rocker, and the rocker
/// elsewhere -- which makes the arch a real opening in the body rather than a
/// dark tube laid over an uncut flank.
float BottomAt(const VehicleDimensions& d, float z)
{
    float y = d.rocker.at(z);
    // The opening is a little wider than it is tall and its ends drop below the
    // axle, which is what a wheel arch is: about 200 degrees of it, not 180. A
    // clean semicircle ending exactly on the axle line reads as a hole punched
    // in a slab.
    const float halfSpan = d.archRadius * 1.10f;
    const float lowest   = d.wheelRadius - d.archRadius * 0.16f;
    const float top      = d.wheelRadius + d.archRadius;
    for (const float axle : {d.rearAxleZ(), d.frontAxleZ()})
    {
        const float dz = z - axle;
        const float r2 = halfSpan * halfSpan - dz * dz;
        if (r2 <= 0.0f) continue;
        // Centred on the *axle*, which is the whole point. Centring it on the
        // road instead -- which the first version did, by writing
        // `wheelRadius + sqrt(r2) - wheelRadius` -- puts the top of the opening
        // at 0.40 m on a car whose tyre reaches 0.65 m, so the upper half of
        // every wheel was buried behind the flank and the fleet looked like it
        // was riding on castors.
        y = std::max(y, lowest + (top - lowest) * std::sqrt(r2) / halfSpan);
    }
    return y;
}

/// The x of the flank at one height, matching what @ref Section builds. Anything
/// laid *on* the body -- a shut line, a door handle, a rubbing strip -- has to
/// come from the same function the surface does, or it floats off the side of
/// the car at the bottom of its run. The first version used the plan half-width
/// at every height, and the panel gaps on every van in the scene hung a
/// centimetre clear of the bodywork.
float FlankX(const VehicleDimensions& d, float z, float y)
{
    const float toEnd  = std::min(z - d.rearZ(), d.frontZ() - z);
    const float t      = 1.0f - std::clamp(toEnd / 0.095f, 0.0f, 1.0f);
    const float plan   = 0.34f + 0.66f * std::sqrt(std::max(0.0f, 1.0f - t * t));
    const float halfW  = d.halfWidth.at(z) * plan;
    const float rockY  = d.rocker.at(z);
    const float beltY  = d.belt.at(z);
    const float h      = std::clamp((y - rockY) / std::max(beltY - rockY, 1e-3f), 0.0f, 1.0f);
    const float tuck   = d.cladding ? 0.80f : 0.865f;
    return halfW * (tuck + (1.0f - tuck) * std::pow(h, 0.55f));
}

/// One section, evaluated into `kSectionCols` points in the XY plane.
void Section(const VehicleDimensions& d, float z, Vector2* out)
{
    const float roofY  = d.roof.at(z);
    const float beltY  = d.belt.at(z);
    // Plan-view corner rounding at the two ends. A bumper corner has a radius of
    // about nine centimetres; the curves carry the *bumper* width and this puts
    // the radius on it, in one place, rather than as three more knots in each of
    // two curves in each of six tables -- which is where the first version's
    // melted noses came from.
    const float toEnd  = std::min(z - d.rearZ(), d.frontZ() - z);
    const float t      = 1.0f - std::clamp(toEnd / 0.095f, 0.0f, 1.0f);
    const float corner = std::sqrt(std::max(0.0f, 1.0f - t * t));
    const float plan   = 0.34f + 0.66f * corner;

    const float halfW  = d.halfWidth.at(z) * plan;
    const float topW   = std::min(d.topHalf.at(z) * plan, halfW - 0.004f);
    const float rockY  = d.rocker.at(z);
    const float bottomY = BottomAt(d, z);

    // Where the arch has eaten into the flank, the section starts partway up it
    // rather than at the rocker -- so the arch lip lies exactly on the flank
    // surface instead of near it.
    const float span = std::max(beltY - rockY, 1e-3f);
    const float h0   = std::clamp((bottomY - rockY) / span, 0.0f, 0.985f);

    // The rocker tuck: the body is not at its widest at the sill. 0.86 on a car,
    // less on something with a high floor, and it is the difference between a
    // body that sits on the road and one that hovers over it.
    const float tuck = d.cladding ? 0.80f : 0.865f;
    const auto flankX = [&](float h) {
        return halfW * (tuck + (1.0f - tuck) * std::pow(std::clamp(h, 0.0f, 1.0f), 0.55f));
    };

    // Rows 0..5: the flank, bottom to shoulder.
    for (int r = 0; r <= kRowBelt; ++r)
    {
        const float u = static_cast<float>(r) / static_cast<float>(kRowBelt);
        const float h = Lerp(h0, 1.0f, u);
        out[r] = Vector2(flankX(h), Lerp(rockY, beltY, h));
    }
    // The character line. Fourteen millimetres of step across one row, which is
    // ten degrees of normal change -- not a crease the smoothing threshold will
    // keep, and not meant to be: on a glossy panel a soft ridge like this reads
    // as a highlight running the length of the car, and a flank without one is
    // a slab. Faded out at the two ends, where the body is closing in and a
    // ridge would sit proud of the bumper.
    const float ridgeFade = std::clamp(std::min(z - d.rearZ(), d.frontZ() - z) / 0.55f, 0.0f, 1.0f);
    out[3].X += 0.0140f * ridgeFade;
    out[4].X += 0.0032f * ridgeFade;

    // Rows 6..9: the daylight opening -- glass in the cabin, the top of a wing
    // over the bonnet, the boot shoulder behind. One parameterisation, because
    // they really are the same surface on a real car.
    // Just under the centre-line top. Clamped above the beltline because at the
    // extreme nose and tail the two curves meet, and an unclamped shoulder puts
    // the crown *below* the widest point -- which turns the section inside out
    // and produces a black wedge at each bumper.
    const float shoulderY = std::max(roofY - (roofY - beltY) * 0.06f - 0.012f, beltY + 0.008f);
    static constexpr float kDlo[4] = {0.10f, 0.44f, 0.76f, 1.0f};
    for (int r = kRowDloBase; r <= kRowShoulder; ++r)
    {
        const float v = kDlo[r - kRowDloBase];
        out[r] = Vector2(Lerp(halfW, topW, std::pow(v, 0.82f)),
                         Lerp(beltY, shoulderY, std::pow(v, 1.22f)));
    }
    // The shoulder crease: the belt strip steps in a few millimetres before the
    // glass starts, which is what puts a dark line under every side window.
    out[kRowDloBase].X -= 0.012f;

    // Rows 9..12: the crown. A superellipse quarter, so the roof is flat in the
    // middle and turns down at the edges rather than being a circular dome.
    static constexpr float kCrown[4] = {0.0f, 0.36f, 0.68f, 1.0f};
    const float exponent = d.panelSides ? 0.62f : 0.77f;  // a van's roof is flatter still
    for (int r = kRowShoulder; r <= kRowCentre; ++r)
    {
        const float w     = kCrown[r - kRowShoulder];
        const float theta = w * MathHelper::PiOver2;
        out[r] = Vector2(topW * std::pow(std::max(std::cos(theta), 0.0f), exponent),
                         shoulderY + (roofY - shoulderY)
                                         * std::pow(std::max(std::sin(theta), 0.0f), exponent));
    }
    out[kRowCentre] = Vector2(0.0f, roofY);

    // The mirror.
    for (int c = kRowCentre + 1; c < kSectionCols; ++c)
    {
        const Vector2& src = out[HalfIndex(c)];
        out[c] = Vector2(-src.X, src.Y);
    }
}

/// The stations the body is sampled at: every place the silhouette changes
/// direction, plus enough between them that the surface is smooth. Uniform
/// sampling would need three times as many to resolve the cowl.
std::vector<float> Stations(const VehicleDimensions& d, VehicleFactory::Detail detail)
{
    const float front = d.frontZ();
    const float rear  = d.rearZ();
    std::vector<float> keys{rear, rear + 0.05f, rear + 0.16f, d.backlightBaseZ, d.backlightTopZ,
                            d.screenTopZ, d.screenBaseZ, d.bPillarZ - d.bPillarHalf,
                            d.bPillarZ + d.bPillarHalf, d.dloRearZ, d.dloFrontZ,
                            front - 0.16f, front - 0.05f, front};
    for (const float axle : {d.rearAxleZ(), d.frontAxleZ()})
        for (int i = 0; i <= 8; ++i)
            keys.push_back(axle + d.archRadius * (static_cast<float>(i) / 4.0f - 1.0f));

    std::sort(keys.begin(), keys.end());
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [&](float z) { return z < rear - 1e-4f || z > front + 1e-4f; }),
               keys.end());
    keys.erase(std::unique(keys.begin(), keys.end(),
                           [](float a, float b) { return std::fabs(a - b) < 0.012f; }),
               keys.end());

    const float maxGap = detail == VehicleFactory::Detail::Full ? 0.155f : 0.44f;
    std::vector<float> out;
    out.reserve(keys.size() * 3);
    for (std::size_t i = 0; i + 1 < keys.size(); ++i)
    {
        out.push_back(keys[i]);
        const float gap = keys[i + 1] - keys[i];
        const int   sub = static_cast<int>(std::ceil(gap / maxGap)) - 1;
        for (int k = 1; k <= sub; ++k)
            out.push_back(keys[i] + gap * static_cast<float>(k) / static_cast<float>(sub + 1));
    }
    out.push_back(keys.back());
    return out;
}

/// Which material a quad of the body surface carries. Everything about the
/// vehicle's appearance that is not its shape is decided here.
enum class Panel
{
    Paint,
    Glass,
    Pillar,
    Cladding,
    Underbody
};

Panel PanelFor(const VehicleDimensions& d, int band, float z)
{
    if (band < 0) return Panel::Underbody;

    const Zone zone = ZoneAt(d, z);
    const bool inCabinLength = z > d.dloRearZ && z < d.dloFrontZ;

    if (band < kRowBelt)  // the flank
    {
        if (d.cladding && band == 0) return Panel::Cladding;
        return Panel::Paint;
    }
    if (band == kRowBelt) return Panel::Paint;  // the belt strip

    if (band < kRowShoulder)  // the daylight opening
    {
        if (d.panelSides && z < d.bPillarZ) return Panel::Paint;  // a van's cargo flank
        if (!inCabinLength) return Panel::Paint;                  // wing tops, boot shoulder
        if (std::fabs(z - d.bPillarZ) < d.bPillarHalf) return Panel::Pillar;
        return Panel::Glass;
    }

    // The crown. The outermost band stays bodywork: that is the A pillar at the
    // front, the roof rail down the side and the C pillar at the back, and
    // glazing it is what makes a cheap car model look like a bubble.
    if (band == kRowShoulder) return Panel::Paint;
    if (zone == Zone::Windscreen || zone == Zone::Backlight) return Panel::Glass;
    return Panel::Paint;
}

/// A lamp that wraps around the corner of the wing. The difference between a
/// car and a box with stickers on it, and nearly free: the lamp follows the
/// same plan-view curve the body does, so it is a short strip of the section
/// rather than a rectangle bolted to the nose. @p surround, when given, takes a
/// dark bezel a centimetre outside the lens on every side; @p push is how far
/// the lens stands off the body, so a brake lens can sit two millimetres over
/// the tail lens it lights.
void LampStrip(const VehicleDimensions& d, MeshBuilder& lens, MeshBuilder* surround, float endZ,
               float depth, float centreY, float halfHeight, float outerFrac, float innerFrac,
               float side, float push)
{
    constexpr int kSteps = 5;
    Vector3 outerLo[kSteps + 1], outerHi[kSteps + 1];
    // Toward the middle of the car, whichever end this is. The first version
    // subtracted the depth at both ends, which wrapped the headlamps back
    // along the wing correctly and the tail lamps 26 cm *out* behind the
    // bumper into thin air -- which is why every car's rear cluster read as a
    // red slab bolted to the back of it.
    const float inward = endZ > 0.0f ? -1.0f : 1.0f;
    for (int i = 0; i <= kSteps; ++i)
    {
        const float f  = static_cast<float>(i) / kSteps;
        const float t  = Lerp(innerFrac, outerFrac, f);
        // Along the end face, then wrapping back down the flank as the plan
        // view closes in.
        const float z  = endZ + inward * depth * f * f;
        const float hw = d.halfWidth.at(z);
        const float x  = side * hw * t;
        const float dy = halfHeight * (1.0f - 0.35f * f * f);
        outerLo[i] = Vector3(x, centreY - dy, z);
        outerHi[i] = Vector3(x, centreY + dy, z);
    }
    const Vector3 facing(side * 0.4f, 0.0f, endZ > 0.0f ? 1.0f : -1.0f);
    for (int i = 0; i < kSteps; ++i)
    {
        const Vector3 out(0.0f, 0.0f, endZ > 0.0f ? push : -push);
        lens.addQuadFacing(outerLo[i] + out, outerLo[i + 1] + out, outerHi[i + 1] + out,
                           outerHi[i] + out, facing);
        if (surround == nullptr) continue;
        // A dark surround a centimetre outside the lens on every side.
        const Vector3 grow(0.0f, 0.022f, 0.0f);
        surround->addQuadFacing(outerLo[i] - grow, outerLo[i + 1] - grow, outerHi[i + 1] + grow,
                                outerHi[i] + grow, facing);
    }
}

/// Where the lamps sit on a class, so the body, the tail lenses and the brake
/// lenses that light over them are placed by one function and cannot drift.
struct LampLayout
{
    float headY, tailY, headHW, tailHW;
};

LampLayout LampsFor(const VehicleDimensions& d)
{
    const float noseZ = d.frontZ();
    const float tailZ = d.rearZ();
    LampLayout out;
    out.headY  = d.belt.at(noseZ - 0.30f) - (d.panelSides ? 0.30f : 0.20f);
    out.tailY  = d.belt.at(tailZ + 0.30f) - (d.panelSides ? 0.34f : 0.14f);
    out.headHW = d.halfWidth.at(noseZ - 0.12f);
    out.tailHW = d.halfWidth.at(tailZ + 0.12f);
    return out;
}

/// A ring of section points as a 3-D polygon at one station, for the end caps.
std::vector<Vector3> Ring(const Vector2* section, float z)
{
    std::vector<Vector3> ring;
    ring.reserve(kSectionCols);
    for (int c = 0; c < kSectionCols; ++c) ring.emplace_back(section[c].X, section[c].Y, z);
    return ring;
}

}  // namespace

// ---------------------------------------------------------------------------

VehicleFactory::VehicleFactory(const MaterialLibrary& materials) : materials_(materials) {}

const char* VehicleFactory::name(VehicleType type)
{
    switch (type)
    {
        case VehicleType::CityCar:    return "city car";
        case VehicleType::Hatchback:  return "hatchback";
        case VehicleType::Saloon:     return "saloon";
        case VehicleType::Estate:     return "estate";
        case VehicleType::Crossover:  return "crossover";
        case VehicleType::Van:        return "van";
        case VehicleType::Count:      break;
    }
    return "vehicle";
}

VehicleDimensions VehicleFactory::dimensionsFor(VehicleType type)
{
    // Every class below names its four glazing stations *first* and then uses
    // those same numbers as knots in the roofline. That is not tidiness: the
    // first version declared them separately, the windscreen zone landed on a
    // flat part of the roof and the raked part of the roof landed in the bonnet
    // zone, and the result was a fleet of cars with painted windscreens and
    // ramps for bonnets. A glazing bound and the roof knot it belongs to are one
    // fact, so they are written once.
    //
    //   cowlZ       base of the windscreen, where the bonnet ends
    //   roofFrontZ  top of the windscreen, where the roof begins
    //   roofRearZ   back of the roof, where the rear glass begins
    //   tailGlassZ  bottom of the rear glass, where the boot or tailgate begins
    VehicleDimensions d;
    float cowlZ = 0.0f, roofFrontZ = 0.0f, roofRearZ = 0.0f, tailGlassZ = 0.0f;

    switch (type)
    {
        // -- a two-box city car: 3.6 m, stubby bonnet, upright tail ----------
        case VehicleType::CityCar:
        {
            d.length = M::kCarCityLength; d.width = M::kCarCityWidth; d.height = M::kCarCityHeight;
            d.wheelbase = 2.42f; d.rearOverhang = 0.50f;
            d.wheelRadius = 0.292f; d.wheelWidth = 0.185f; d.archRadius = 0.360f;
            const float hw = d.width * 0.5f;
            cowlZ = 0.98f; roofFrontZ = 0.34f; roofRearZ = -1.40f; tailGlassZ = -1.66f;
            d.roof = {{-1.80f, 0.98f}, {-1.74f, 1.02f}, {tailGlassZ, 1.06f}, {roofRearZ, 1.440f}, {-1.05f, 1.485f},
                      {-0.20f, 1.512f}, {roofFrontZ, 1.500f}, {cowlZ, 1.120f}, {1.36f, 1.070f},
                      {1.66f, 0.985f}, {1.80f, 0.930f}};
            d.belt = {{-1.80f, 0.790f}, {-1.60f, 0.860f}, {-1.21f, 0.940f}, {0.0f, 0.952f},
                      {1.21f, 0.962f}, {1.58f, 0.930f}, {1.80f, 0.850f}};
            d.rocker = {{-1.80f, 0.335f}, {-1.52f, 0.290f}, {-0.6f, 0.258f}, {0.6f, 0.258f},
                        {1.50f, 0.300f}, {1.80f, 0.345f}};
            d.halfWidth = {{-1.80f, hw * 0.82f}, {-1.62f, hw * 0.93f}, {-1.21f, hw * 1.00f},
                           {-0.35f, hw * 0.985f}, {0.45f, hw * 0.985f}, {1.21f, hw * 1.00f},
                           {1.58f, hw * 0.93f}, {1.80f, hw * 0.84f}};
            d.topHalf = {{-1.80f, hw * 0.62f}, {-1.60f, hw * 0.76f}, {-1.32f, hw * 0.820f},
                         {-0.30f, hw * 0.805f}, {0.30f, hw * 0.810f}, {0.90f, hw * 0.86f},
                         {1.45f, hw * 0.88f}, {1.80f, hw * 0.66f}};
            d.bPillarZ = -0.46f; d.bPillarHalf = 0.062f;
            break;
        }

        // -- the C-segment hatchback that half of Europe drives --------------
        case VehicleType::Hatchback:
        {
            d.length = M::kCarHatchLength; d.width = M::kCarHatchWidth; d.height = M::kCarHatchHeight;
            d.wheelbase = 2.64f; d.rearOverhang = 0.74f;
            d.wheelRadius = 0.315f; d.wheelWidth = 0.215f; d.archRadius = 0.392f;
            const float hw = d.width * 0.5f;
            cowlZ = 1.22f; roofFrontZ = 0.42f; roofRearZ = -1.62f; tailGlassZ = -1.98f;
            d.roof = {{-2.14f, 0.960f}, {-2.08f, 1.005f}, {tailGlassZ, 1.06f}, {roofRearZ, 1.408f}, {-1.20f, 1.442f},
                      {-0.20f, 1.460f}, {roofFrontZ, 1.446f}, {cowlZ, 1.060f}, {1.66f, 1.020f},
                      {2.02f, 0.950f}, {2.14f, 0.905f}};
            d.belt = {{-2.14f, 0.790f}, {-1.94f, 0.870f}, {-1.32f, 0.925f}, {0.0f, 0.936f},
                      {1.32f, 0.946f}, {1.88f, 0.915f}, {2.14f, 0.840f}};
            d.rocker = {{-2.14f, 0.330f}, {-1.82f, 0.285f}, {-0.7f, 0.248f}, {0.7f, 0.248f},
                        {1.80f, 0.295f}, {2.14f, 0.340f}};
            d.halfWidth = {{-2.14f, hw * 0.82f}, {-1.96f, hw * 0.92f}, {-1.32f, hw * 1.00f},
                           {-0.40f, hw * 0.982f}, {0.50f, hw * 0.984f}, {1.32f, hw * 1.00f},
                           {1.92f, hw * 0.92f}, {2.14f, hw * 0.84f}};
            d.topHalf = {{-2.14f, hw * 0.62f}, {-1.92f, hw * 0.74f}, {-1.52f, hw * 0.800f},
                         {-0.30f, hw * 0.782f}, {0.42f, hw * 0.788f}, {1.05f, hw * 0.845f},
                         {1.70f, hw * 0.865f}, {2.14f, hw * 0.66f}};
            d.bPillarZ = -0.36f; d.bPillarHalf = 0.058f;
            break;
        }

        // -- three boxes: bonnet, cabin, boot ---------------------------------
        case VehicleType::Saloon:
        {
            d.length = M::kCarSedanLength; d.width = M::kCarSedanWidth; d.height = M::kCarSedanHeight;
            d.wheelbase = 2.84f; d.rearOverhang = 0.92f;
            d.wheelRadius = 0.325f; d.wheelWidth = 0.225f; d.archRadius = 0.402f;
            const float hw = d.width * 0.5f;
            cowlZ = 1.36f; roofFrontZ = 0.52f; roofRearZ = -0.60f; tailGlassZ = -1.30f;
            d.roof = {{-2.355f, 0.985f}, {-2.24f, 1.070f}, {-2.15f, 1.105f}, {-1.80f, 1.150f}, {tailGlassZ, 1.162f},
                      {roofRearZ, 1.432f}, {0.20f, 1.440f}, {roofFrontZ, 1.428f},
                      {cowlZ, 1.075f}, {1.80f, 1.042f}, {2.16f, 0.970f}, {2.355f, 0.918f}};
            d.belt = {{-2.355f, 0.820f}, {-2.10f, 0.875f}, {-1.42f, 0.918f}, {0.0f, 0.928f},
                      {1.42f, 0.938f}, {2.02f, 0.905f}, {2.355f, 0.845f}};
            d.rocker = {{-2.355f, 0.330f}, {-2.00f, 0.275f}, {-0.8f, 0.238f}, {0.8f, 0.238f},
                        {1.98f, 0.285f}, {2.355f, 0.335f}};
            d.halfWidth = {{-2.355f, hw * 0.82f}, {-2.14f, hw * 0.91f}, {-1.42f, hw * 1.00f},
                           {-0.40f, hw * 0.980f}, {0.60f, hw * 0.984f}, {1.42f, hw * 1.00f},
                           {2.06f, hw * 0.91f}, {2.355f, hw * 0.84f}};
            d.topHalf = {{-2.355f, hw * 0.62f}, {-2.05f, hw * 0.76f}, {-1.55f, hw * 0.845f},
                         {tailGlassZ, hw * 0.800f}, {-0.30f, hw * 0.782f}, {0.44f, hw * 0.790f},
                         {1.20f, hw * 0.855f}, {1.86f, hw * 0.865f}, {2.355f, hw * 0.66f}};
            d.bPillarZ = -0.26f; d.bPillarHalf = 0.056f;
            break;
        }

        // -- the roof runs to the tailgate, which is the whole point ----------
        case VehicleType::Estate:
        {
            d.length = M::kCarEstateLength; d.width = M::kCarEstateWidth; d.height = M::kCarEstateHeight;
            d.wheelbase = 2.86f; d.rearOverhang = 0.96f;
            d.wheelRadius = 0.325f; d.wheelWidth = 0.225f; d.archRadius = 0.402f;
            const float hw = d.width * 0.5f;
            cowlZ = 1.40f; roofFrontZ = 0.56f; roofRearZ = -2.02f; tailGlassZ = -2.26f;
            d.roof = {{-2.385f, 1.020f}, {-2.32f, 1.085f}, {tailGlassZ, 1.140f}, {roofRearZ, 1.478f},
                      {-1.60f, 1.502f}, {0.0f, 1.508f}, {roofFrontZ, 1.494f}, {cowlZ, 1.110f},
                      {1.82f, 1.072f}, {2.20f, 0.995f}, {2.385f, 0.930f}};
            d.belt = {{-2.385f, 0.840f}, {-2.16f, 0.900f}, {-1.43f, 0.948f}, {0.0f, 0.958f},
                      {1.43f, 0.968f}, {2.06f, 0.935f}, {2.385f, 0.860f}};
            d.rocker = {{-2.385f, 0.335f}, {-2.02f, 0.275f}, {-0.8f, 0.242f}, {0.8f, 0.242f},
                        {2.00f, 0.285f}, {2.385f, 0.340f}};
            d.halfWidth = {{-2.385f, hw * 0.84f}, {-2.18f, hw * 0.93f}, {-1.43f, hw * 1.00f},
                           {-0.40f, hw * 0.982f}, {0.60f, hw * 0.984f}, {1.43f, hw * 1.00f},
                           {2.10f, hw * 0.92f}, {2.385f, hw * 0.84f}};
            d.topHalf = {{-2.385f, hw * 0.72f}, {-2.20f, hw * 0.80f}, {-1.80f, hw * 0.818f},
                         {-0.30f, hw * 0.796f}, {0.46f, hw * 0.802f}, {1.24f, hw * 0.860f},
                         {1.92f, hw * 0.870f}, {2.385f, hw * 0.66f}};
            d.bPillarZ = -0.26f; d.bPillarHalf = 0.056f;
            d.roofRails = true;
            break;
        }

        // -- higher floor, bigger wheels, black cladding ----------------------
        case VehicleType::Crossover:
        {
            d.length = M::kCarSuvLength; d.width = M::kCarSuvWidth; d.height = M::kCarSuvHeight;
            d.wheelbase = 2.70f; d.rearOverhang = 0.86f;
            d.wheelRadius = 0.356f; d.wheelWidth = 0.245f; d.archRadius = 0.452f;
            d.archInset = 0.100f;
            const float hw = d.width * 0.5f;
            cowlZ = 1.24f; roofFrontZ = 0.40f; roofRearZ = -1.80f; tailGlassZ = -2.10f;
            d.roof = {{-2.265f, 1.130f}, {-2.20f, 1.190f}, {tailGlassZ, 1.240f}, {roofRearZ, 1.600f},
                      {-1.40f, 1.638f}, {-0.20f, 1.658f}, {roofFrontZ, 1.645f}, {cowlZ, 1.235f},
                      {1.70f, 1.190f}, {2.10f, 1.105f}, {2.265f, 1.045f}};
            d.belt = {{-2.265f, 0.960f}, {-2.06f, 1.020f}, {-1.35f, 1.078f}, {0.0f, 1.088f},
                      {1.35f, 1.098f}, {1.94f, 1.060f}, {2.265f, 0.985f}};
            d.rocker = {{-2.265f, 0.440f}, {-1.92f, 0.385f}, {-0.8f, 0.350f}, {0.8f, 0.350f},
                        {1.90f, 0.392f}, {2.265f, 0.448f}};
            d.halfWidth = {{-2.265f, hw * 0.84f}, {-2.06f, hw * 0.93f}, {-1.35f, hw * 1.00f},
                           {-0.40f, hw * 0.985f}, {0.50f, hw * 0.985f}, {1.35f, hw * 1.00f},
                           {1.98f, hw * 0.93f}, {2.265f, hw * 0.86f}};
            d.topHalf = {{-2.265f, hw * 0.70f}, {-2.04f, hw * 0.80f}, {-1.62f, hw * 0.828f},
                         {-0.30f, hw * 0.806f}, {0.44f, hw * 0.812f}, {1.12f, hw * 0.866f},
                         {1.80f, hw * 0.876f}, {2.265f, hw * 0.68f}};
            d.bPillarZ = -0.36f; d.bPillarHalf = 0.060f;
            d.cladding = true; d.roofRails = true;
            break;
        }

        // -- a cab with a box behind it ---------------------------------------
        case VehicleType::Van:
        {
            d.length = M::kVanLength; d.width = M::kVanWidth; d.height = M::kVanHeight;
            d.wheelbase = 3.26f; d.rearOverhang = 0.94f;
            d.wheelRadius = 0.352f; d.wheelWidth = 0.225f; d.archRadius = 0.436f;
            d.archInset = 0.070f;
            const float hw = d.width * 0.5f;
            // A panel van's rear doors are steel to the roof, so there is no
            // rear glass at all: the two bounds coincide and the zone is empty.
            cowlZ = 1.60f; roofFrontZ = 0.72f; roofRearZ = -2.655f; tailGlassZ = -2.66f;
            d.roof = {{-2.67f, 1.900f}, {-2.63f, 2.060f}, {-2.58f, 2.120f}, {-2.44f, 2.320f}, {-2.20f, 2.356f},
                      {0.20f, 2.360f}, {roofFrontZ, 2.320f}, {cowlZ, 1.440f}, {1.96f, 1.360f},
                      {2.42f, 1.205f}, {2.67f, 1.130f}};
            d.belt = {{-2.67f, 1.080f}, {-2.44f, 1.160f}, {-1.63f, 1.218f}, {0.0f, 1.228f},
                      {1.63f, 1.238f}, {2.34f, 1.190f}, {2.67f, 1.070f}};
            d.rocker = {{-2.67f, 0.430f}, {-2.30f, 0.362f}, {-0.9f, 0.332f}, {0.9f, 0.332f},
                        {2.28f, 0.372f}, {2.67f, 0.440f}};
            d.halfWidth = {{-2.67f, hw * 0.90f}, {-2.50f, hw * 0.97f}, {-1.63f, hw * 1.00f},
                           {-0.40f, hw * 0.995f}, {0.60f, hw * 0.995f}, {1.63f, hw * 1.00f},
                           {2.40f, hw * 0.95f}, {2.67f, hw * 0.88f}};
            d.topHalf = {{-2.67f, hw * 0.84f}, {-2.46f, hw * 0.92f}, {-1.80f, hw * 0.945f},
                         {0.20f, hw * 0.945f}, {0.90f, hw * 0.895f}, {1.60f, hw * 0.900f},
                         {2.20f, hw * 0.890f}, {2.67f, hw * 0.72f}};
            d.bPillarZ = 0.30f; d.bPillarHalf = 0.070f;
            d.panelSides = true;
            break;
        }

        case VehicleType::Count:
            break;
    }

    d.screenBaseZ = cowlZ;
    d.screenTopZ  = roofFrontZ;
    d.backlightTopZ  = roofRearZ;
    d.backlightBaseZ = tailGlassZ;
    // The side glass stops where the pillars start, and the pillars are exactly
    // the surfaces between the side glass and the front and rear screens. So
    // these are not free parameters either: an A pillar is what lies between
    // `roofFrontZ` and `cowlZ` in the daylight-opening band, and giving the side
    // glass its own bound would either eat the pillar or leave a gap.
    d.dloFrontZ = roofFrontZ;
    d.dloRearZ  = roofRearZ;
    return d;
}

std::vector<WheelPlacement> VehicleFactory::wheelsFor(VehicleType type)
{
    const VehicleDimensions d = dimensionsFor(type);
    // The track: the tyre's outer wall sits just inside the widest point of the
    // body, which is what puts the wheel *in* its arch rather than under it.
    const float x = d.width * 0.5f - d.wheelWidth * 0.5f - 0.026f;
    std::vector<WheelPlacement> out;
    out.reserve(4);
    for (const float side : {-1.0f, 1.0f})
        for (const bool front : {false, true})
        {
            WheelPlacement w;
            w.centre  = Vector3(side * x, d.wheelRadius, front ? d.frontAxleZ() : d.rearAxleZ());
            w.side    = side;
            w.steered = front;
            out.push_back(w);
        }
    return out;
}

// ---------------------------------------------------------------------------

void VehicleFactory::buildWheel(GeometryCollector& collector, VehicleType type,
                                Detail detail) const
{
    const VehicleDimensions d = dimensionsFor(type);
    const bool  full     = detail == Detail::Full;
    const int   segments = full ? 22 : 10;
    const float R        = d.wheelRadius;
    const float halfW    = d.wheelWidth * 0.5f;
    const float rimR     = R * 0.635f;   // a 16" rim in a 205/55 tyre
    const float step     = MathHelper::TwoPi / static_cast<float>(segments);

    MeshBuilder& tyre = collector.builder(&materials_.get(MaterialId::CarTyre));
    tyre.setTileSize(0.28f);

    // The tyre as one closed surface of revolution: tread, both shoulders, both
    // sidewalls, down to the bead at the rim. A cylinder with two flat discs is
    // a wheel-shaped hole; what makes a tyre read is the shoulder radius
    // catching a highlight all the way round.
    struct Ring { float x, r; };
    const std::vector<Ring> rings =
        full ? std::vector<Ring>{{-halfW * 0.99f, rimR},        {-halfW * 1.04f, rimR * 1.30f},
                                 {-halfW * 1.06f, R * 0.90f},   {-halfW * 0.97f, R * 0.985f},
                                 {-halfW * 0.72f, R},           {halfW * 0.72f, R},
                                 {halfW * 0.97f, R * 0.985f},   {halfW * 1.06f, R * 0.90f},
                                 {halfW * 1.04f, rimR * 1.30f}, {halfW * 0.99f, rimR}}
             : std::vector<Ring>{{-halfW, rimR}, {-halfW, R}, {halfW, R}, {halfW, rimR}};

    SurfacePatch tread;
    tread.resize(static_cast<int>(rings.size()), segments);
    tread.wrapCols       = true;
    tread.smoothingAngle = 1.15f;
    for (int r = 0; r < tread.rows; ++r)
        for (int c = 0; c < segments; ++c)
        {
            const float a = step * static_cast<float>(c);
            tread.at(r, c) = Vector3(rings[static_cast<std::size_t>(r)].x,
                                     std::sin(a) * rings[static_cast<std::size_t>(r)].r,
                                     std::cos(a) * rings[static_cast<std::size_t>(r)].r);
        }
    tyre.addSurfacePatch(tread);

    // The brake disc and the dark of the wheel well behind the spokes. Without
    // it the gaps between the spokes show the sky.
    MeshBuilder& dark = collector.builder(&materials_.get(MaterialId::CarBrake));
    dark.setTileSize(0.2f);
    dark.addCylinderBetween(Vector3(-halfW * 0.35f, 0.0f, 0.0f), Vector3(halfW * 0.30f, 0.0f, 0.0f),
                            rimR * 0.96f, full ? 16 : 8, true);

    MeshBuilder& rim = collector.builder(&materials_.get(MaterialId::Aluminium));
    rim.setTileSize(0.22f);
    // The rim flange: a narrow band at the tyre bead, which is the bright ring
    // that separates the black of the tyre from the dish.
    rim.addCylinderBetween(Vector3(halfW * 0.86f, 0.0f, 0.0f), Vector3(halfW * 1.00f, 0.0f, 0.0f),
                           rimR, full ? 20 : 10, false);
    if (!full)
    {
        rim.addDiscFacing(Vector3(halfW * 0.86f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), rimR, 10);
        return;
    }

    // Five spokes, each a tapered wedge from the hub to the flange, set back
    // into the dish. What makes a wheel read as a wheel at ten metres is the
    // shadow *between* the spokes, so the well behind them stays dark and the
    // spokes are what catches the light.
    const float faceX = halfW * 0.80f;
    const float hubR  = rimR * 0.26f;
    for (int s = 0; s < 5; ++s)
    {
        const float a0 = MathHelper::TwoPi * static_cast<float>(s) / 5.0f;
        const float wideHalf = 0.20f, narrowHalf = 0.115f;
        const auto point = [&](float radius, float angle, float x) {
            return Vector3(x, std::sin(angle) * radius, std::cos(angle) * radius);
        };
        // Front face of the spoke, dished inward towards the hub.
        rim.addQuad(point(hubR, a0 - narrowHalf, faceX - 0.030f),
                    point(rimR * 0.97f, a0 - wideHalf, faceX),
                    point(rimR * 0.97f, a0 + wideHalf, faceX),
                    point(hubR, a0 + narrowHalf, faceX - 0.030f));
        // Its two sides, so the spoke has thickness rather than being a decal.
        for (const float sign : {-1.0f, 1.0f})
        {
            const float wide = a0 + sign * wideHalf, narrow = a0 + sign * narrowHalf;
            rim.addQuadFacing(point(hubR, narrow, faceX - 0.030f),
                              point(rimR * 0.97f, wide, faceX),
                              point(rimR * 0.97f, wide, faceX - 0.055f),
                              point(hubR, narrow, faceX - 0.075f),
                              Vector3(0.0f, std::cos(a0) * sign, -std::sin(a0) * sign));
        }
    }
    // The hub cap and the wheel nuts' recess.
    rim.addCylinderBetween(Vector3(faceX - 0.034f, 0.0f, 0.0f), Vector3(faceX - 0.014f, 0.0f, 0.0f),
                           hubR * 1.5f, 12, true);
    MeshBuilder& badge = collector.builder(&materials_.get(MaterialId::CarTrim));
    badge.addDiscFacing(Vector3(faceX - 0.012f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), hubR * 0.9f,
                        10);
}

void VehicleFactory::buildBrakeLamps(GeometryCollector& collector, VehicleType type) const
{
    // The same two strips the body carries, three millimetres over them, drawn
    // with the lit material while the car brakes. They used to be a pair of
    // boxes 34 cm wide and 14 cm proud of the bumper, which lit up as two red
    // slabs bolted to the back of every braking car on the street.
    const VehicleDimensions d = dimensionsFor(type);
    const LampLayout lamps = LampsFor(d);
    MeshBuilder& lamp = collector.builder(&materials_.get(MaterialId::CarLightRear));
    lamp.setTileSize(0.32f);
    for (const float side : {-1.0f, 1.0f})
        LampStrip(d, lamp, nullptr, d.rearZ(), 0.26f, lamps.tailY, 0.072f, 0.955f, 0.60f, side,
                  0.013f);
}

// ---------------------------------------------------------------------------

void VehicleFactory::build(GeometryCollector& collector, VehicleType type,
                           const Material* bodyMaterial, Rng& rng, Detail detail) const
{
    const VehicleDimensions d = dimensionsFor(type);
    const bool  full  = detail == Detail::Full;
    const float halfW = d.width * 0.5f;

    const Material& paintMaterial =
        bodyMaterial != nullptr ? *bodyMaterial : materials_.get(MaterialId::CarBody);

    // At distance the material set collapses, the same way the character's
    // does and for the same reason: every material on a car is a draw call, a
    // car is submitted as its own object because it moves, and forty cars down
    // a street at eight materials each is three hundred draws. Past the switch
    // distance the trim, the underbody and the number plate are all the same
    // dark line under the same silhouette, so they share one builder. Paint and
    // glass stay apart at every distance -- a car's glazing is the second thing
    // after its shape that says "car", and it is a different colour from the
    // body at every angle.
    MeshBuilder& paint = collector.builder(&paintMaterial);
    paint.setTileSize(2.2f);
    MeshBuilder& glass = collector.builder(&materials_.get(MaterialId::CarGlass));
    glass.setTileSize(1.6f);
    MeshBuilder& trim = collector.builder(&materials_.get(
        full ? MaterialId::CarTrim : MaterialId::CarUnderbody));
    trim.setTileSize(full ? 0.5f : 0.8f);
    MeshBuilder& under = collector.builder(&materials_.get(MaterialId::CarUnderbody));
    under.setTileSize(0.8f);

    // --- the body surface ---------------------------------------------------
    const std::vector<float> stations = Stations(d, detail);
    SurfacePatch body;
    body.resize(static_cast<int>(stations.size()), kSectionCols);
    body.wrapCols = true;
    // 55°: enough to hold the shoulder crease, the arch lip and the bonnet shut
    // line, loose enough that the flank and the roof are one smooth panel.
    body.smoothingAngle = 0.96f;

    std::vector<Vector2> section(kSectionCols);
    for (std::size_t r = 0; r < stations.size(); ++r)
    {
        Section(d, stations[r], section.data());
        for (int c = 0; c < kSectionCols; ++c)
            body.at(static_cast<int>(r), c) =
                Vector3(section[static_cast<std::size_t>(c)].X,
                        section[static_cast<std::size_t>(c)].Y, stations[r]);
    }

    // Emit each run of quads that shares a material as one call, so the smoothed
    // normals are computed once over the whole surface and the seam between the
    // roof and the windscreen shades continuously.
    const int quadRows = body.rows - 1;
    for (int c = 0; c < kSectionCols; ++c)
    {
        const int band = BandOf(c);
        int r = 0;
        while (r < quadRows)
        {
            const float z = (stations[static_cast<std::size_t>(r)]
                             + stations[static_cast<std::size_t>(r) + 1])
                            * 0.5f;
            const Panel panel = PanelFor(d, band, z);
            int end = r + 1;
            while (end < quadRows)
            {
                const float z2 = (stations[static_cast<std::size_t>(end)]
                                  + stations[static_cast<std::size_t>(end) + 1])
                                 * 0.5f;
                if (PanelFor(d, band, z2) != panel) break;
                ++end;
            }
            MeshBuilder* target = &paint;
            switch (panel)
            {
                case Panel::Glass:     target = &glass; break;
                case Panel::Pillar:    target = &trim;  break;
                case Panel::Cladding:  target = &trim;  break;
                case Panel::Underbody: target = &under; break;
                case Panel::Paint:     break;
            }
            target->addSurfacePatch(body, r, end, c, c + 1);
            r = end;
        }
    }

    // The two end caps: the bumper faces. Flat is right -- a modern bumper *is*
    // a near-vertical surface -- and the rounding in plan comes from the
    // half-width curve closing in over the last 20 cm.
    {
        Section(d, d.rearZ(), section.data());
        std::vector<Vector3> ring = Ring(section.data(), d.rearZ());
        std::reverse(ring.begin(), ring.end());
        paint.addPolygon(ring, Vector3(0.0f, 0.0f, -1.0f));
        Section(d, d.frontZ(), section.data());
        paint.addPolygon(Ring(section.data(), d.frontZ()), Vector3(0.0f, 0.0f, 1.0f));
    }

    // --- the interior -------------------------------------------------------
    // Not optional. Every pane of glass on this body is a hole in the surface,
    // so without an interior a parked car is a coloured shell you can see the
    // building behind through -- which is worse than the solid greenhouse it
    // replaced.
    if (!d.panelSides || true)
    {
        MeshBuilder& cabin = collector.builder(&materials_.get(MaterialId::CarInterior));
        cabin.setTileSize(0.9f);
        const float floorY  = d.rocker.at(0.0f) + 0.10f;
        const float cabinW  = d.topHalf.at(d.bPillarZ) - 0.045f;
        const float rearZ   = d.panelSides ? d.bPillarZ - 0.30f : d.dloRearZ - 0.12f;
        const float frontZ  = d.screenBaseZ + 0.10f;
        const float dashY   = d.belt.at(frontZ) - 0.03f;

        // A dark box: floor, back, and the underside of the roof. Seen through
        // glass it is a darkness with a shape, which is all a cabin needs to be.
        cabin.addBox(Vector3(-cabinW, floorY, rearZ), Vector3(cabinW, floorY + 0.02f, frontZ));
        cabin.addQuadFacing(Vector3(-cabinW, floorY, rearZ), Vector3(cabinW, floorY, rearZ),
                            Vector3(cabinW, d.belt.at(rearZ) + 0.16f, rearZ),
                            Vector3(-cabinW, d.belt.at(rearZ) + 0.16f, rearZ),
                            Vector3(0.0f, 0.0f, 1.0f));
        // The dashboard and the parcel shelf, which give the glass something to
        // occlude near the bottom of the screen.
        cabin.addBox(Vector3(-cabinW, dashY - 0.20f, frontZ - 0.30f),
                     Vector3(cabinW, dashY, frontZ));
        if (!d.panelSides)
            cabin.addBox(Vector3(-cabinW, d.belt.at(d.dloRearZ) - 0.02f, d.dloRearZ - 0.30f),
                         Vector3(cabinW, d.belt.at(d.dloRearZ) + 0.04f, d.dloRearZ + 0.10f));

        // Seats. Two in front, a bench behind where there is one. A head
        // restraint is the single most recognisable thing inside a car seen
        // from outside, so it gets its own box.
        const float seatBackY = d.belt.at(d.bPillarZ) + 0.06f;
        const auto seat = [&](float x, float z) {
            cabin.addBox(Vector3(x - 0.235f, floorY + 0.02f, z - 0.22f),
                         Vector3(x + 0.235f, floorY + 0.24f, z + 0.24f));
            cabin.addBox(Vector3(x - 0.225f, floorY + 0.20f, z - 0.30f),
                         Vector3(x + 0.225f, seatBackY, z - 0.14f));
            cabin.addBox(Vector3(x - 0.115f, seatBackY, z - 0.28f),
                         Vector3(x + 0.115f, seatBackY + 0.15f, z - 0.16f));
        };
        const float frontSeatZ = d.bPillarZ + 0.28f;
        seat(-cabinW * 0.48f, frontSeatZ);
        seat(cabinW * 0.48f, frontSeatZ);
        if (!d.panelSides && d.dloRearZ < d.bPillarZ - 0.70f)
        {
            const float backSeatZ = d.bPillarZ - 0.72f;
            seat(-cabinW * 0.50f, backSeatZ);
            seat(cabinW * 0.50f, backSeatZ);
        }
        // The steering wheel: a ring on a stalk, on the left, because this is
        // continental Europe.
        const Matrix wheelFrame = Matrix::CreateRotationX(-0.42f)
                                  * Matrix::CreateTranslation(-cabinW * 0.48f, dashY + 0.11f,
                                                              frontSeatZ + 0.30f);
        cabin.addTubeArc(wheelFrame, 0.165f, 0.017f, 0.0f, MathHelper::TwoPi, full ? 12 : 6, 4);
    }

    // --- lamps --------------------------------------------------------------
    // Set into the ends rather than bolted on: the lamp stops inside the widest
    // point of the bumper and below the bonnet line, so nothing breaks the
    // silhouette. Each has a dark surround, which is what stops a headlamp
    // reading as a white sticker.
    MeshBuilder& head = collector.builder(&materials_.get(MaterialId::CarLightFront));
    head.setTileSize(0.32f);
    MeshBuilder& tail = collector.builder(&materials_.get(MaterialId::CarLightRear));
    tail.setTileSize(0.32f);

    const float noseZ  = d.frontZ();
    const float tailZ  = d.rearZ();
    const LampLayout lamps = LampsFor(d);
    const float headY  = lamps.headY;
    const float tailY  = lamps.tailY;
    const float headHW = lamps.headHW;
    const float tailHW = lamps.tailHW;

    for (const float side : {-1.0f, 1.0f})
    {
        // Headlamps: a strip wrapping the wing, and inside its outline a
        // smaller bright unit -- the reflector bowl -- set into a dark housing,
        // so the lamp reads as a lamp with a lens over it and not as a white
        // sticker.
        LampStrip(d, head, &trim, noseZ, 0.30f, headY, 0.072f, 0.965f, 0.44f, side, 0.010f);
        LampStrip(d, trim, nullptr, noseZ, 0.30f, headY, 0.052f, 0.93f, 0.50f, side, 0.014f);
        LampStrip(d, head, nullptr, noseZ, 0.30f, headY, 0.034f, 0.90f, 0.56f, side, 0.018f);
        // Tail lamps: shorter and shallower than they were. A rear lamp is a
        // horizontal cluster a hand tall; at 21 cm it filled the whole back
        // panel and the car read as a toy.
        LampStrip(d, tail, &trim, tailZ, 0.26f, tailY, 0.072f, 0.955f, 0.60f, side, 0.010f);
        // A pale reversing/indicator segment in the lower inboard corner.
        LampStrip(d, head, nullptr, tailZ, 0.10f, tailY - 0.030f, 0.026f, 0.72f, 0.62f, side,
                  0.013f);
    }

    // --- grille, intake, bumper --------------------------------------------
    // The bumper is the single most useful line on the front of a car: it puts
    // a horizontal edge across the nose at a known height, and without it the
    // whole end is one continuous curve with a lamp floating in it.
    const float grilleY  = headY + 0.015f;
    const float noseSill = d.rocker.at(noseZ - 0.35f);
    const float tailSill = d.rocker.at(tailZ + 0.35f);
    // The grille: a dark recess between the headlamps with bright horizontal
    // slats across it and a badge in the middle. A plain black rectangle read
    // as a mouth.
    const float grilleHalf = headHW * 0.38f;
    trim.addBox(Vector3(-grilleHalf, grilleY - 0.062f, noseZ - 0.13f),
                Vector3(grilleHalf, grilleY + 0.072f, noseZ - 0.010f));
    {
        MeshBuilder& bright = collector.builder(&materials_.get(MaterialId::Aluminium));
        bright.setTileSize(0.3f);
        for (int slat = 0; slat < 3; ++slat)
        {
            const float y = grilleY - 0.040f + static_cast<float>(slat) * 0.038f;
            bright.addBox(Vector3(-grilleHalf + 0.012f, y, noseZ - 0.012f),
                          Vector3(grilleHalf - 0.012f, y + 0.010f, noseZ + 0.002f));
        }
        // The frame around the grille and the badge.
        bright.addBox(Vector3(-grilleHalf - 0.008f, grilleY - 0.070f, noseZ - 0.012f),
                      Vector3(grilleHalf + 0.008f, grilleY - 0.062f, noseZ + 0.004f));
        bright.addBox(Vector3(-grilleHalf - 0.008f, grilleY + 0.072f, noseZ - 0.012f),
                      Vector3(grilleHalf + 0.008f, grilleY + 0.080f, noseZ + 0.004f));
        bright.addDiscFacing(Vector3(0.0f, grilleY + 0.006f, noseZ + 0.006f),
                             Vector3(0.0f, 0.0f, 1.0f), 0.036f, 12);
    }
    // The lower intake, wide and shallow, set into the bumper.
    trim.addBox(Vector3(-headHW * 0.62f, noseSill + 0.10f, noseZ - 0.15f),
                Vector3(headHW * 0.62f, noseSill + 0.27f, noseZ - 0.004f));
    // The roof aerial, a stub at the back of the roof on cars.
    if (!d.panelSides && full)
    {
        const float az = d.backlightTopZ + 0.18f;
        trim.addCylinder(Vector3(0.0f, d.roof.at(az) - 0.002f, az), 0.012f, 0.006f, 0.075f, 6,
                         false, true);
    }
    // The bumper's own lower edge, front and rear: a band of slightly duller
    // paint that catches a different amount of sky than the panel above it.
    under.addBox(Vector3(-headHW * 0.94f, noseSill - 0.02f, noseZ - 0.34f),
                 Vector3(headHW * 0.94f, noseSill + 0.055f, noseZ - 0.004f));
    under.addBox(Vector3(-tailHW * 0.94f, tailSill - 0.02f, tailZ + 0.004f),
                 Vector3(tailHW * 0.94f, tailSill + 0.055f, tailZ + 0.30f));
    trim.addBox(Vector3(-tailHW * 0.70f, tailSill + 0.07f, tailZ + 0.002f),
                Vector3(tailHW * 0.70f, tailSill + 0.20f, tailZ + 0.13f));
    // The exhaust: 4 cm of dark pipe under the rear bumper, on the near side.
    under.addCylinderBetween(Vector3(tailHW * 0.52f, d.rocker.at(tailZ + 0.4f) - 0.03f,
                                     tailZ + 0.18f),
                             Vector3(tailHW * 0.52f, d.rocker.at(tailZ + 0.4f) - 0.03f,
                                     tailZ - 0.02f),
                             0.028f, 8, true);

    if (d.cladding)
    {
        // Arch trim and a sill guard, the two things that say "crossover".
        for (const float side : {-1.0f, 1.0f})
            for (const float axle : {d.rearAxleZ(), d.frontAxleZ()})
            {
                const Matrix frame =
                    Matrix::CreateRotationY(side > 0.0f ? MathHelper::PiOver2 : -MathHelper::PiOver2)
                    * Matrix::CreateTranslation(side * (d.halfWidth.at(axle) - 0.008f),
                                                d.wheelRadius, axle);
                trim.addTubeArc(frame, d.archRadius + 0.008f, 0.030f,
                                side > 0.0f ? 0.10f : MathHelper::Pi - 2.94f, 2.94f,
                                full ? 14 : 7, 5);
            }
    }
    if (d.roofRails)
        for (const float side : {-1.0f, 1.0f})
        {
            const float railZ0 = d.backlightTopZ + (d.panelSides ? 0.0f : 0.10f);
            const float railZ1 = d.screenTopZ - 0.10f;
            const float x = side * d.topHalf.at(0.0f) * 0.82f;
            trim.addBox(Vector3(x - 0.030f, d.roof.at(0.0f) + 0.006f, railZ0),
                        Vector3(x + 0.030f, d.roof.at(0.0f) + 0.052f, railZ1));
        }

    if (!full)
    {
        (void)rng;
        return;
    }

    // --- shut lines, handles, mirrors, plates -------------------------------
    // The cheapest detail on the whole vehicle and close to the most valuable: a
    // smooth flank reads as a block of colour, and a flank with a door line on
    // it reads as a car.
    const float doorRear  = d.dloRearZ - 0.02f;
    const float doorFront = d.dloFrontZ + 0.04f;
    std::vector<float> shuts{doorFront, d.bPillarZ};
    if (!d.panelSides) shuts.push_back(doorRear);
    // The bonnet shut, across the top: two lines running back from the nose.
    for (const float side : {-1.0f, 1.0f})
    {
        for (const float z : shuts)
        {
            const float y0 = d.rocker.at(z) + 0.09f;
            const float y1 = d.belt.at(z) + 0.01f;
            constexpr int kSteps = 6;
            for (int i = 0; i < kSteps; ++i)
            {
                const float ta = static_cast<float>(i) / kSteps;
                const float tb = static_cast<float>(i + 1) / kSteps;
                const float ya = Lerp(y0, y1, ta), yb = Lerp(y0, y1, tb);
                const float xa = side * (FlankX(d, z, ya) + 0.019f);
                const float xb = side * (FlankX(d, z, yb) + 0.019f);
                trim.addQuadFacing(Vector3(xa, ya, z - 0.008f), Vector3(xa, ya, z + 0.008f),
                                   Vector3(xb, yb, z + 0.008f), Vector3(xb, yb, z - 0.008f),
                                   Vector3(side, 0.0f, 0.0f));
            }
        }
        // Handles, on the door side of each shut line.
        std::vector<float> handles{(d.bPillarZ + doorFront) * 0.5f};
        if (!d.panelSides) handles.push_back((doorRear + d.bPillarZ) * 0.5f);
        for (const float z : handles)
        {
            const float y = d.belt.at(z) - 0.13f;
            const float x = side * (FlankX(d, z, y) + 0.034f);
            paint.addBox(Vector3(std::min(x, x - side * 0.030f), y - 0.021f, z - 0.070f),
                         Vector3(std::max(x, x - side * 0.030f), y + 0.021f, z + 0.070f));
        }
        // The mirror: a stalk off the A pillar and a housing with glass in it.
        const float mz = d.screenBaseZ - (d.panelSides ? 0.02f : 0.14f);
        const Vector3 root(side * FlankX(d, mz, d.belt.at(mz)) * 0.97f, d.belt.at(mz) + 0.06f, mz);
        const Vector3 tip(side * (d.halfWidth.at(mz) + 0.115f), d.belt.at(mz) + 0.10f, mz - 0.06f);
        paint.addCylinderBetween(root, tip, 0.021f, 6);
        paint.addBox(tip - Vector3(0.058f, 0.052f, 0.028f), tip + Vector3(0.058f, 0.052f, 0.075f));
        glass.addQuadFacing(Vector3(tip.X - side * 0.058f, tip.Y - 0.040f, tip.Z - 0.020f),
                            Vector3(tip.X - side * 0.058f, tip.Y - 0.040f, tip.Z + 0.066f),
                            Vector3(tip.X - side * 0.058f, tip.Y + 0.040f, tip.Z + 0.066f),
                            Vector3(tip.X - side * 0.058f, tip.Y + 0.040f, tip.Z - 0.020f),
                            Vector3(-side, 0.0f, 0.30f));
    }

    // A wiper across the bottom of the windscreen. Two centimetres of black
    // rubber that says "this glass is glass".
    {
        const float wz = d.screenBaseZ - 0.05f;
        for (const float side : {-1.0f, 1.0f})
            trim.addCylinderBetween(
                Vector3(side * 0.03f, d.roof.at(wz) + 0.012f, wz),
                Vector3(side * d.topHalf.at(wz) * 0.72f, d.roof.at(wz + 0.10f) + 0.030f, wz + 0.16f),
                0.011f, 5);
    }

    // The number plate is 52 cm of lettering: legible at ten metres, four
    // pixels wide at forty, and a draw call of its own at both. The distant
    // body does without it and puts the recess it sits in on the dark
    // builder instead.
    if (!full)
    {
        const float recessHalf = 0.255f;
        const float rearY = d.rocker.at(tailZ + 0.25f) + 0.30f;
        trim.addQuadFacing(Vector3(-recessHalf, rearY, tailZ - 0.008f),
                           Vector3(recessHalf, rearY, tailZ - 0.008f),
                           Vector3(recessHalf, rearY + 0.115f, tailZ - 0.008f),
                           Vector3(-recessHalf, rearY + 0.115f, tailZ - 0.008f),
                           Vector3(0.0f, 0.0f, -1.0f));
        const float frontY = d.rocker.at(noseZ - 0.25f) + 0.26f;
        trim.addQuadFacing(Vector3(-recessHalf, frontY, noseZ + 0.008f),
                           Vector3(recessHalf, frontY, noseZ + 0.008f),
                           Vector3(recessHalf, frontY + 0.115f, noseZ + 0.008f),
                           Vector3(-recessHalf, frontY + 0.115f, noseZ + 0.008f),
                           Vector3(0.0f, 0.0f, 1.0f));
        (void)rng;
        (void)halfW;
        return;
    }

    MeshBuilder& plate = collector.builder(&materials_.get(MaterialId::LicencePlate));
    plate.setUvMode(UvMode::Explicit);
    // The plate image is a wide strip in the top quarter of a square texture.
    const float plateHalf = 0.255f;
    const float rearPlateY = d.rocker.at(tailZ + 0.25f) + 0.30f;
    plate.addQuadUv(Vector3(-plateHalf, rearPlateY, tailZ - 0.010f),
                    Vector3(plateHalf, rearPlateY, tailZ - 0.010f),
                    Vector3(plateHalf, rearPlateY + 0.115f, tailZ - 0.010f),
                    Vector3(-plateHalf, rearPlateY + 0.115f, tailZ - 0.010f),
                    Vector2(1.0f, 0.25f), Vector2(0.0f, 0.25f), Vector2(0.0f, 0.0f),
                    Vector2(1.0f, 0.0f));
    const float frontPlateY = d.rocker.at(noseZ - 0.25f) + 0.26f;
    plate.addQuadUv(Vector3(plateHalf, frontPlateY, noseZ + 0.010f),
                    Vector3(-plateHalf, frontPlateY, noseZ + 0.010f),
                    Vector3(-plateHalf, frontPlateY + 0.115f, noseZ + 0.010f),
                    Vector3(plateHalf, frontPlateY + 0.115f, noseZ + 0.010f),
                    Vector2(1.0f, 0.25f), Vector2(0.0f, 0.25f), Vector2(0.0f, 0.0f),
                    Vector2(1.0f, 0.0f));

    (void)rng;
    (void)halfW;
}

}  // namespace CnaStreet
