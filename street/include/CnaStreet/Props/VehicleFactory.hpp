// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

namespace CnaStreet {

/// The classes of vehicle on the street. Five is enough for a district: what
/// stops a row of parked cars looking like a row of clones is colour and
/// spacing at least as much as shape.
enum class VehicleType
{
    Hatchback,
    Saloon,
    Estate,
    Crossover,
    Van,
    Count
};

/// The real exterior dimensions of the class, in metres.
struct VehicleDimensions
{
    float length = 4.28f;
    float width  = 1.79f;
    float height = 1.46f;
    float wheelbase = 2.62f;
    float frontOverhang = 0.86f;
    float wheelRadius = 0.315f;
    float wheelWidth  = 0.215f;
    float sill      = 0.24f;   ///< underbody height
    float beltline  = 0.93f;   ///< top of the lower body / bottom of the glass
    /// The greenhouse, as fractions of the overall length measured from the
    /// centre of the vehicle. The bonnet is the difference between the nose at
    /// +0.5 and `cabinFront`, so 0.15 on a 4.3 m car is a 1.5 m bonnet: about
    /// right for a modern transverse-engined hatchback, where the first version's
    /// 0.06 gave it a two-metre bonnet and the profile of a 1974 saloon.
    float cabinFront = 0.150f;  ///< base of the windscreen
    float roofFront  = -0.010f; ///< top of the windscreen
    float roofRear   = -0.320f; ///< top of the backlight
    float cabinRear  = -0.440f; ///< base of the backlight
};

/**
 * @brief Builds vehicles.
 *
 * The body is a loft, not a stack of boxes: a set of cross-sections along the
 * length, each a rounded rectangle whose width and height vary, joined into a
 * surface. That is what a car body actually is, and it is the only way to get
 * the one thing that makes a vehicle read at a glance — the shoulder line
 * running from the front wing to the tail. A box with the corners knocked off
 * does not have one.
 *
 * The greenhouse is a second, narrower loft with the glass laid on top of it, so
 * the pillars are the body colour showing between the panes rather than
 * separately modelled sticks.
 */
class VehicleFactory
{
public:
    explicit VehicleFactory(const MaterialLibrary& materials);

    [[nodiscard]] static VehicleDimensions dimensionsFor(VehicleType type);
    [[nodiscard]] static const char* name(VehicleType type);

    /// Builds one vehicle at the origin, facing +Z, wheels on y = 0.
    /// @p bodyMaterial lets the caller pick a paint colour without the factory
    /// needing to know the palette.
    void build(GeometryCollector& collector, VehicleType type, const Material* bodyMaterial,
               Rng& rng) const;

private:
    const MaterialLibrary& materials_;
};

}  // namespace CnaStreet
