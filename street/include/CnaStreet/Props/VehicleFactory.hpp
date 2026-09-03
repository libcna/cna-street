// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <initializer_list>
#include <vector>

namespace CnaStreet {

/// The classes of vehicle on the street. Six shapes rather than one shape
/// scaled six ways: a city car and a panel van are not the same object at
/// different sizes, and a row of parked cars that *is* reads as a row of clones
/// however the paint is varied.
enum class VehicleType
{
    CityCar,
    Hatchback,
    Saloon,
    Estate,
    Crossover,
    Van,
    Count
};

/**
 * @brief A curve along the vehicle, sampled without overshoot.
 *
 * The body is described by five of these — roofline, beltline, rocker, plan
 * half-width and greenhouse half-width — and every one of them has to pass
 * exactly through the points that were measured off a real car. A Catmull-Rom
 * spline does not: between a flat roof and the top of the backlight it bulges
 * *above* the roof, and the result is a car with a blister on it. This is
 * Fritsch–Carlson monotone cubic interpolation, whose whole property is that it
 * never leaves the interval its two knots define.
 */
class Curve
{
public:
    Curve() = default;
    Curve(std::initializer_list<Microsoft::Xna::Framework::Vector2> knots);

    [[nodiscard]] float at(float x) const;
    [[nodiscard]] bool empty() const { return knots_.empty(); }

private:
    std::vector<Microsoft::Xna::Framework::Vector2> knots_;
    std::vector<float> slopes_;
};

/// The real exterior dimensions of the class, in metres, plus the curves that
/// make its silhouette.
struct VehicleDimensions
{
    float length = 4.28f;
    float width  = 1.79f;
    float height = 1.46f;
    float wheelbase = 2.62f;
    /// Distance from the rear bumper to the rear axle. The front overhang is
    /// whatever is left, so the three numbers cannot disagree.
    float rearOverhang = 0.80f;
    float wheelRadius = 0.315f;
    float wheelWidth  = 0.215f;
    /// Radius of the arch cut into the flank. Always larger than the wheel: the
    /// gap between tyre and arch lip is one of the things the eye measures a car
    /// by, and a wheel that fills its arch reads as a toy.
    float archRadius = 0.395f;
    /// How far the arch liner is set back from the widest point of the body.
    float archInset = 0.085f;

    Curve roof;       ///< centre-line top of the body against z
    Curve belt;       ///< shoulder line: top of the flank, bottom of the glass
    Curve rocker;     ///< bottom of the visible body
    Curve halfWidth;  ///< half width at the shoulder — the widest point
    Curve topHalf;    ///< half width where the crown begins: tumblehome

    // Glazing extents, in vehicle z. Everything between is cabin.
    float screenBaseZ = 0.6f;    ///< bottom of the windscreen (the cowl)
    float screenTopZ  = -0.1f;   ///< top of the windscreen = front of the roof
    float backlightTopZ  = -1.2f;///< back of the roof
    float backlightBaseZ = -1.8f;///< bottom of the backlight
    float bPillarZ = -0.3f;      ///< centre of the B pillar
    float bPillarHalf = 0.055f;
    /// Where the side glass stops, front and rear. Inside the pillars.
    float dloFrontZ = 0.5f;
    float dloRearZ  = -1.5f;

    bool  cladding = false;      ///< matt black sill and arch trim (crossover, van)
    bool  roofRails = false;
    /// A van's cargo box has no side glass behind the cab.
    bool  panelSides = false;

    [[nodiscard]] float frontZ() const { return length * 0.5f; }
    [[nodiscard]] float rearZ() const { return -length * 0.5f; }
    [[nodiscard]] float rearAxleZ() const { return rearZ() + rearOverhang; }
    [[nodiscard]] float frontAxleZ() const { return rearAxleZ() + wheelbase; }
};

/// Where one wheel sits on the vehicle, in vehicle space.
struct WheelPlacement
{
    Microsoft::Xna::Framework::Vector3 centre{0.0f, 0.0f, 0.0f};
    /// +1 for the right-hand side, -1 for the left. The wheel mesh is built for
    /// the right and mirrored, so the alloy dish faces outward on both sides.
    float side = 1.0f;
    bool  steered = false;
};

/**
 * @brief Builds vehicles.
 *
 * The first version of this lofted a superellipse through nine stations and put
 * a second, narrower superellipse on top of it for the greenhouse. It produced a
 * car-shaped object and nothing more: no wheel arches, because the flank was a
 * smooth tube the wheels intersected; no shoulder line, because a superellipse
 * has no feature lines; and flat shading, because every quad carried its own
 * face normal. At four metres it read as a lump.
 *
 * This one is built the way a car is drawn. Five curves give the side view and
 * the plan view; a fixed thirteen-point section is evaluated between them at
 * every station, so the rocker, the flank, the shoulder crease, the daylight
 * opening and the crown are *named rows* of one surface rather than separate
 * objects that can disagree. The wheel arch is a genuine cut — the bottom of the
 * flank follows the arch circle where the circle is above the rocker — and the
 * closed section means the floor becomes the wheel-well ceiling for free.
 *
 * Because the glass is part of the same surface, it fits exactly: a windscreen
 * is the middle columns of the crown between two stations, and a side window is
 * the middle rows of the daylight opening between two others. Nothing floats and
 * nothing is buried.
 *
 * Wheels are built separately and submitted with their own transform, so they
 * turn with the road and steer into corners.
 */
class VehicleFactory
{
public:
    explicit VehicleFactory(const MaterialLibrary& materials);

    [[nodiscard]] static VehicleDimensions dimensionsFor(VehicleType type);
    [[nodiscard]] static const char* name(VehicleType type);
    /// The four wheels, in vehicle space.
    [[nodiscard]] static std::vector<WheelPlacement> wheelsFor(VehicleType type);

    /// How much of the body to build. `Full` is the flagship mesh; `Distant` is
    /// the same silhouette at a quarter of the stations with no interior, no
    /// shut lines and no badges — swapped in past ~40 m, where the difference is
    /// three pixels and two thousand triangles.
    enum class Detail
    {
        Full,
        Distant
    };

    /// Builds one vehicle at the origin, facing +Z, wheel contact at y = 0,
    /// *without* its wheels. @p bodyMaterial lets the caller pick a paint colour
    /// without the factory needing to know the palette.
    void build(GeometryCollector& collector, VehicleType type, const Material* bodyMaterial,
               Rng& rng, Detail detail = Detail::Full) const;

    /// One wheel for @p type, centred at the origin, rotating about +X.
    void buildWheel(GeometryCollector& collector, VehicleType type,
                    Detail detail = Detail::Full) const;

    /// Just the two rear lenses, a couple of millimetres proud of the ones the
    /// body carries. Drawn over them with an emissive material while the
    /// vehicle is braking, which is how one mesh becomes a brake light without
    /// a second copy of the whole car.
    void buildBrakeLamps(GeometryCollector& collector, VehicleType type) const;

private:
    const MaterialLibrary& materials_;
};

}  // namespace CnaStreet
