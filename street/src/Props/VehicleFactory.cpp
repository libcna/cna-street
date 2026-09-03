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
using CnaStreet::Geometry::UvMode;

namespace CnaStreet {

namespace M = Metrics;

namespace {

/// One cross-section of a lofted body.
struct Station
{
    float z;          ///< along the vehicle, rear negative
    float halfWidth;
    float bottom;
    float top;
    float roundness;  ///< 1 = ellipse, higher = squarer
};

/// A point on a superelliptical cross-section. `roundness` controls how square
/// it is: a car's section is nowhere near an ellipse, and nowhere near a box.
Vector3 SectionPoint(const Station& station, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    const float e = 2.0f / station.roundness;
    const float x = station.halfWidth * (c < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(c), e);
    const float centre = (station.bottom + station.top) * 0.5f;
    const float half   = (station.top - station.bottom) * 0.5f;
    const float y = centre + half * (s < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(s), e);
    return Vector3(x, y, station.z);
}

/// Half-width of a section at a given height. The body is a superellipse, so
/// the flank is not at `halfWidth` anywhere except the section's own centre
/// line: putting a door shut line or a handle at `halfWidth` buries it in the
/// bodywork at the top of the door and leaves it floating at the sill.
float FlankX(const Station& station, float y)
{
    const float centre = (station.bottom + station.top) * 0.5f;
    const float half   = (station.top - station.bottom) * 0.5f;
    if (half <= 1e-4f) return 0.0f;
    const float t = std::clamp(std::fabs(y - centre) / half, 0.0f, 1.0f);
    const float e = 2.0f / station.roundness;
    const float inner = std::max(0.0f, 1.0f - std::pow(t, 2.0f / e));
    return station.halfWidth * std::pow(inner, e * 0.5f);
}

/// The same, anywhere along the vehicle, by interpolating the two stations the
/// point falls between.
float FlankAt(const std::vector<Station>& stations, float z, float y)
{
    if (stations.empty()) return 0.0f;
    for (std::size_t i = 0; i + 1 < stations.size(); ++i)
    {
        const Station& a = stations[i];
        const Station& b = stations[i + 1];
        if (z < a.z || z > b.z) continue;
        const float t = (b.z - a.z) > 1e-5f ? (z - a.z) / (b.z - a.z) : 0.0f;
        return FlankX(a, y) * (1.0f - t) + FlankX(b, y) * t;
    }
    return FlankX(z < stations.front().z ? stations.front() : stations.back(), y);
}

/// Lofts a closed tube through the stations and caps both ends.
void Loft(MeshBuilder& builder, const std::vector<Station>& stations, int samples)
{
    if (stations.size() < 2) return;
    const float step = MathHelper::TwoPi / static_cast<float>(samples);

    for (std::size_t i = 0; i + 1 < stations.size(); ++i)
        for (int j = 0; j < samples; ++j)
        {
            const float a0 = step * static_cast<float>(j);
            const float a1 = step * static_cast<float>(j + 1);
            const Vector3 p00 = SectionPoint(stations[i], a0);
            const Vector3 p01 = SectionPoint(stations[i], a1);
            const Vector3 p10 = SectionPoint(stations[i + 1], a0);
            const Vector3 p11 = SectionPoint(stations[i + 1], a1);
            // Around the section first, along the vehicle second. The other
            // order winds the whole hull inside out, and an inside-out car does
            // not look like a bug: it looks like a car made of glass, because
            // every near-side panel is culled and what you see is the inside of
            // the far one.
            builder.addQuad(p00, p01, p11, p10);
        }

    // Caps, wound to face outward along the vehicle's axis.
    for (const bool front : {false, true})
    {
        const Station& station = front ? stations.back() : stations.front();
        std::vector<Vector3> ring;
        ring.reserve(static_cast<std::size_t>(samples));
        for (int j = 0; j < samples; ++j) ring.push_back(SectionPoint(station, step * static_cast<float>(j)));
        if (!front) std::reverse(ring.begin(), ring.end());
        builder.addPolygon(ring, Vector3(0.0f, 0.0f, front ? 1.0f : -1.0f));
    }
}

}  // namespace

VehicleFactory::VehicleFactory(const MaterialLibrary& materials) : materials_(materials) {}

const char* VehicleFactory::name(VehicleType type)
{
    switch (type)
    {
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
    VehicleDimensions d;
    switch (type)
    {
        case VehicleType::Hatchback:
            d.length = M::kCarHatchLength; d.width = M::kCarHatchWidth; d.height = M::kCarHatchHeight;
            d.wheelbase = 2.62f; d.frontOverhang = 0.86f;
            d.cabinFront = 0.150f; d.roofFront = -0.012f;
            d.roofRear = -0.320f; d.cabinRear = -0.442f;
            break;
        case VehicleType::Saloon:
            d.length = M::kCarSedanLength; d.width = M::kCarSedanWidth; d.height = M::kCarSedanHeight;
            d.wheelbase = 2.84f; d.frontOverhang = 0.90f; d.beltline = 0.90f;
            d.cabinFront = 0.155f; d.roofFront = 0.004f;
            d.roofRear = -0.262f; d.cabinRear = -0.404f;
            break;
        case VehicleType::Estate:
            d.length = M::kCarEstateLength; d.width = M::kCarEstateWidth; d.height = M::kCarEstateHeight;
            // An estate's roof runs all the way to the tailgate, which is the
            // only thing that distinguishes it from a saloon at a distance.
            d.wheelbase = 2.79f; d.frontOverhang = 0.92f; d.beltline = 0.95f;
            d.cabinFront = 0.148f; d.roofFront = 0.000f;
            d.roofRear = -0.438f; d.cabinRear = -0.478f;
            break;
        case VehicleType::Crossover:
            d.length = M::kCarSuvLength; d.width = M::kCarSuvWidth; d.height = M::kCarSuvHeight;
            d.wheelbase = 2.68f; d.frontOverhang = 0.88f; d.sill = 0.32f; d.beltline = 1.05f;
            d.wheelRadius = 0.345f; d.wheelWidth = 0.235f;
            d.cabinFront = 0.142f; d.roofFront = -0.006f;
            d.roofRear = -0.352f; d.cabinRear = -0.452f;
            break;
        case VehicleType::Van:
            d.length = M::kVanLength; d.width = M::kVanWidth; d.height = M::kVanHeight;
            d.wheelbase = 3.20f; d.frontOverhang = 0.92f; d.sill = 0.30f; d.beltline = 1.16f;
            // A panel van is a cab and a box: the windscreen is steep and close
            // to the nose, and the roof runs level to the back doors.
            d.wheelRadius = 0.340f; d.wheelWidth = 0.225f;
            d.cabinFront = 0.290f; d.roofFront = 0.196f;
            d.roofRear = -0.472f; d.cabinRear = -0.492f;
            break;
        case VehicleType::Count:
            break;
    }
    return d;
}

void VehicleFactory::build(GeometryCollector& collector, VehicleType type,
                           const Material* bodyMaterial, Rng& rng) const
{
    const VehicleDimensions d = dimensionsFor(type);
    const float halfLength = d.length * 0.5f;
    const float halfWidth  = d.width * 0.5f;

    MeshBuilder& body = collector.builder(bodyMaterial != nullptr
                                              ? bodyMaterial
                                              : &materials_.get(MaterialId::CarBody));
    body.setTileSize(2.0f);

    // --- lower body -------------------------------------------------------
    // The stations describe the shoulder line: full width through the wheel
    // arches, drawn in at both bumpers, with the sill rising a little at each
    // end so the car has an approach angle rather than a bulldozer blade.
    const bool boxy = type == VehicleType::Van;
    const float round = boxy ? 5.0f : 3.2f;
    std::vector<Station> lower = {
        {-halfLength,          halfWidth * 0.90f, d.sill + 0.06f, d.beltline - 0.10f, round},
        {-halfLength + 0.22f,  halfWidth * 0.97f, d.sill + 0.01f, d.beltline - 0.03f, round},
        {-halfLength + 0.70f,  halfWidth * 1.00f, d.sill,         d.beltline,         round},
        {-d.wheelbase * 0.5f,  halfWidth * 1.00f, d.sill - 0.02f, d.beltline,         round},
        {0.0f,                 halfWidth * 0.99f, d.sill - 0.03f, d.beltline + 0.01f, round},
        {d.wheelbase * 0.5f,   halfWidth * 1.00f, d.sill - 0.02f, d.beltline + 0.01f, round},
        {halfLength - 0.72f,   halfWidth * 1.00f, d.sill,         d.beltline - 0.01f, round},
        {halfLength - 0.26f,   halfWidth * 0.97f, d.sill + 0.02f, d.beltline - 0.07f, round},
        {halfLength,           halfWidth * 0.90f, d.sill + 0.08f, d.beltline - 0.14f, round},
    };
    Loft(body, lower, 16);

    // --- greenhouse -------------------------------------------------------
    const float cabinRearZ  = d.cabinRear * d.length;
    const float cabinFrontZ = d.cabinFront * d.length;
    const float roofRearZ   = d.roofRear * d.length;
    const float roofFrontZ  = d.roofFront * d.length;
    const float roofY       = d.height;
    const float glassBase   = d.beltline - 0.02f;

    // One width for the whole greenhouse, because the glass has to sit on the
    // *outside* of it and the two must not be able to disagree. They did: the
    // first version tapered the cabin from 0.84 to 0.90 of the half width and
    // put the glass at a flat 0.88, which buried the side windows inside the
    // pillar in the middle and left them floating in air at the ends. The result
    // was a car with a body-coloured plate for a roof and no windows at all.
    const float cabinHalf = halfWidth * (boxy ? 0.94f : 0.82f);
    std::vector<Station> cabin = {
        {cabinRearZ,  cabinHalf * 0.94f, glassBase, glassBase + 0.06f, round},
        {roofRearZ,   cabinHalf,         glassBase, roofY,             round},
        {(roofRearZ + roofFrontZ) * 0.5f, cabinHalf, glassBase, roofY + 0.012f, round},
        {roofFrontZ,  cabinHalf,         glassBase, roofY,             round},
        {cabinFrontZ, cabinHalf * 0.92f, glassBase, glassBase + 0.06f, round},
    };
    Loft(body, cabin, 14);

    // --- glass -------------------------------------------------------------
    // Laid on the greenhouse rather than cut into it, so the pillars are the
    // body colour showing between the panes.
    MeshBuilder& glass = collector.builder(&materials_.get(MaterialId::CarGlass));
    glass.setTileSize(1.5f);
    // Windscreen and backlight, raked between the beltline and the roof edge, and
    // stated as facing forward and back rather than wound by hand.
    glass.addQuadFacing(Vector3(-cabinHalf * 0.94f, glassBase + 0.03f, cabinFrontZ - 0.02f),
                        Vector3(cabinHalf * 0.94f, glassBase + 0.03f, cabinFrontZ - 0.02f),
                        Vector3(cabinHalf * 0.90f, roofY - 0.035f, roofFrontZ + 0.02f),
                        Vector3(-cabinHalf * 0.90f, roofY - 0.035f, roofFrontZ + 0.02f),
                        Vector3(0.0f, 0.45f, 1.0f));
    glass.addQuadFacing(Vector3(-cabinHalf * 0.94f, glassBase + 0.03f, cabinRearZ + 0.02f),
                        Vector3(cabinHalf * 0.94f, glassBase + 0.03f, cabinRearZ + 0.02f),
                        Vector3(cabinHalf * 0.90f, roofY - 0.045f, roofRearZ - 0.02f),
                        Vector3(-cabinHalf * 0.90f, roofY - 0.045f, roofRearZ - 0.02f),
                        Vector3(0.0f, 0.45f, -1.0f));

    // Side glass: two panes each side, split by a B-pillar, laid 8 mm proud of
    // the greenhouse so the pillars between them are body colour.
    const float pillarZ = (roofRearZ + roofFrontZ) * 0.5f + 0.05f;
    for (const float side : {-1.0f, 1.0f})
    {
        const float x = side * (cabinHalf + 0.008f);
        const Vector3 out(side, 0.0f, 0.0f);
        glass.addQuadFacing(Vector3(x, glassBase + 0.035f, roofFrontZ - 0.05f),
                            Vector3(x, glassBase + 0.035f, pillarZ + 0.045f),
                            Vector3(x, roofY - 0.065f, pillarZ + 0.045f),
                            Vector3(x, roofY - 0.075f, roofFrontZ - 0.11f), out);
        glass.addQuadFacing(Vector3(x, glassBase + 0.035f, pillarZ - 0.045f),
                            Vector3(x, glassBase + 0.035f, roofRearZ + 0.06f),
                            Vector3(x, roofY - 0.085f, roofRearZ + 0.12f),
                            Vector3(x, roofY - 0.065f, pillarZ - 0.045f), out);
    }

    // --- wheels -------------------------------------------------------------
    MeshBuilder& tyre = collector.builder(&materials_.get(MaterialId::CarTyre));
    tyre.setTileSize(0.4f);
    MeshBuilder& rim = collector.builder(&materials_.get(MaterialId::Aluminium));
    rim.setTileSize(0.3f);

    // Matte black for the arch lip. The first version used the glossy dark trim
    // the grille is made of, and under a bright sky a glossy 3 cm tube reads as a
    // chrome crescent over every wheel.
    MeshBuilder& arch = collector.builder(&materials_.get(MaterialId::CarTyre));
    arch.setTileSize(0.3f);

    for (const float sideSign : {-1.0f, 1.0f})
        for (const float axle : {-d.wheelbase * 0.5f, d.wheelbase * 0.5f})
        {
            const float x = sideSign * (halfWidth - d.wheelWidth * 0.5f - 0.035f);
            const Vector3 inner(x - sideSign * d.wheelWidth * 0.5f, d.wheelRadius, axle);
            const Vector3 outer(x + sideSign * d.wheelWidth * 0.5f, d.wheelRadius, axle);
            // Capped, unlike the first version: an uncapped tyre is a tube, and a
            // tube with a small bright rim disc inside it reads as a hole with a
            // hubcap floating in it, which is what every parked car in the first
            // screenshots had for wheels.
            tyre.addCylinderBetween(inner, outer, d.wheelRadius, 20, true);
            // The rim: a dark well set into the sidewall with five alloy spokes
            // across it. A flat bright disc at the right radius is still a
            // hubcap-shaped hole -- what makes a wheel read as a wheel at ten
            // metres is the shadow *between* the spokes, so the well behind them
            // is dark and the spokes are what catches the light.
            const float rimRadius = d.wheelRadius * 0.60f;
            const Vector3 face = outer - Vector3(sideSign * 0.014f, 0.0f, 0.0f);
            MeshBuilder& well = collector.builder(&materials_.get(MaterialId::CarTrim));
            well.setTileSize(0.2f);
            well.addCylinderBetween(face - Vector3(sideSign * 0.055f, 0.0f, 0.0f), face,
                                    rimRadius, 16, true);
            // The lip, and the spokes from the hub out to it.
            rim.addCylinderBetween(face - Vector3(sideSign * 0.020f, 0.0f, 0.0f),
                                   face + Vector3(sideSign * 0.004f, 0.0f, 0.0f),
                                   rimRadius, 16, false);
            const Vector3 hubAt = face + Vector3(sideSign * 0.002f, 0.0f, 0.0f);
            for (int spoke = 0; spoke < 5; ++spoke)
            {
                const float a = MathHelper::TwoPi * static_cast<float>(spoke) / 5.0f + 0.3f;
                rim.addCylinderBetween(hubAt,
                                       hubAt + Vector3(0.0f, std::sin(a) * rimRadius * 0.92f,
                                                       std::cos(a) * rimRadius * 0.92f),
                                       rimRadius * 0.15f, 6, true);
            }
            rim.addCylinderBetween(hubAt, hubAt + Vector3(sideSign * 0.014f, 0.0f, 0.0f),
                                   d.wheelRadius * 0.19f, 10, true);

            // The wheel arch: a dark lip over the top of the tyre, which is what
            // stops the wheel looking stuck onto a smooth flank.
            const Matrix frame =
                Matrix::CreateRotationY(sideSign > 0.0f ? MathHelper::PiOver2
                                                        : -MathHelper::PiOver2)
                * Matrix::CreateTranslation(sideSign * (halfWidth - 0.012f), d.wheelRadius, axle);
            arch.addTubeArc(frame, d.wheelRadius + 0.050f, 0.024f,
                            sideSign > 0.0f ? 0.20f : MathHelper::Pi - 2.74f, 2.74f, 12, 6);
        }

    // --- doors --------------------------------------------------------------
    // Two shut lines and a handle each side. This is the cheapest detail on the
    // whole vehicle and close to the most valuable: a smooth flank reads as a
    // block of colour, and a flank with a door line on it reads as a car.
    MeshBuilder& shut = collector.builder(&materials_.get(MaterialId::CarTrim));
    shut.setUvMode(UvMode::Explicit);
    const float doorRear  = cabinRearZ + 0.06f;
    const float doorSplit = pillarZ;
    const float doorFront = cabinFrontZ - 0.12f;
    for (const float side : {-1.0f, 1.0f})
    {
        for (const float z : {doorRear, doorSplit, doorFront})
        {
            const float y0 = d.sill + 0.10f;
            const float y1 = glassBase + 0.02f;
            constexpr int kSteps = 5;
            for (int i = 0; i < kSteps; ++i)
            {
                const float ya = y0 + (y1 - y0) * static_cast<float>(i) / kSteps;
                const float yb = y0 + (y1 - y0) * static_cast<float>(i + 1) / kSteps;
                const float xa = side * (FlankAt(lower, z, ya) + 0.004f);
                const float xb = side * (FlankAt(lower, z, yb) + 0.004f);
                shut.addQuadFacing(Vector3(xa, ya, z - 0.011f), Vector3(xa, ya, z + 0.011f),
                                   Vector3(xb, yb, z + 0.011f), Vector3(xb, yb, z - 0.011f),
                                   Vector3(side, 0.0f, 0.0f));
            }
        }
        // The handles, on the door side of each shut line.
        for (const float z : {(doorRear + doorSplit) * 0.5f, (doorSplit + doorFront) * 0.5f})
        {
            const float y = glassBase - 0.16f;
            const float x = side * (FlankAt(lower, z, y) + 0.008f);
            shut.addBox(Vector3(std::min(x, x - side * 0.03f), y - 0.026f, z - 0.075f),
                        Vector3(std::max(x, x - side * 0.03f), y + 0.026f, z + 0.075f));
        }
    }

    // --- lamps, mirrors, plates --------------------------------------------
    MeshBuilder& headlamp = collector.builder(&materials_.get(MaterialId::CarLightFront));
    headlamp.setTileSize(0.3f);
    MeshBuilder& taillamp = collector.builder(&materials_.get(MaterialId::CarLightRear));
    taillamp.setTileSize(0.3f);

    // Set into the ends rather than bolted onto them: the lamp's outer edge stops
    // inside the widest point of the bumper, and its top stops below the bonnet
    // line, so nothing sticks out of the silhouette.
    const float lampY = d.beltline - 0.24f;
    const float lampOuter = halfWidth * 0.80f;
    for (const float side : {-1.0f, 1.0f})
    {
        const float x = side * (lampOuter - 0.17f);
        headlamp.addBox(Vector3(x - 0.16f, lampY - 0.062f, halfLength - 0.09f),
                        Vector3(x + 0.16f, lampY + 0.062f, halfLength - 0.008f));
        taillamp.addBox(Vector3(x - 0.155f, lampY - 0.058f, -halfLength + 0.008f),
                        Vector3(x + 0.155f, lampY + 0.078f, -halfLength + 0.09f));
    }

    MeshBuilder& mirror = collector.builder(bodyMaterial != nullptr
                                                ? bodyMaterial
                                                : &materials_.get(MaterialId::CarBody));
    for (const float side : {-1.0f, 1.0f})
    {
        const Vector3 root(side * halfWidth * 0.86f, glassBase + 0.10f, cabinFrontZ - 0.28f);
        const Vector3 tip(side * (halfWidth + 0.10f), glassBase + 0.13f, cabinFrontZ - 0.34f);
        mirror.addCylinderBetween(root, tip, 0.022f, 6);
        mirror.addBox(tip - Vector3(0.055f, 0.055f, 0.028f), tip + Vector3(0.055f, 0.055f, 0.075f));
    }

    MeshBuilder& plate = collector.builder(&materials_.get(MaterialId::LicencePlate));
    plate.setUvMode(UvMode::Explicit);
    // The plate texture is a wide strip in the top quarter of a square image.
    const float plateHalf = 0.26f;
    plate.addQuadUv(Vector3(-plateHalf, d.sill + 0.20f, -halfLength - 0.012f),
                    Vector3(plateHalf, d.sill + 0.20f, -halfLength - 0.012f),
                    Vector3(plateHalf, d.sill + 0.32f, -halfLength - 0.012f),
                    Vector3(-plateHalf, d.sill + 0.32f, -halfLength - 0.012f),
                    Vector2(1.0f, 0.25f), Vector2(0.0f, 0.25f), Vector2(0.0f, 0.0f),
                    Vector2(1.0f, 0.0f));
    plate.addQuadUv(Vector3(plateHalf, d.sill + 0.16f, halfLength + 0.012f),
                    Vector3(-plateHalf, d.sill + 0.16f, halfLength + 0.012f),
                    Vector3(-plateHalf, d.sill + 0.28f, halfLength + 0.012f),
                    Vector3(plateHalf, d.sill + 0.28f, halfLength + 0.012f),
                    Vector2(1.0f, 0.25f), Vector2(0.0f, 0.25f), Vector2(0.0f, 0.0f),
                    Vector2(1.0f, 0.0f));

    // A grille and a lower air intake, which is most of what distinguishes the
    // front of a car from the back at a distance.
    MeshBuilder& grille = collector.builder(&materials_.get(MaterialId::CarTrim));
    grille.setTileSize(0.4f);
    grille.addBox(Vector3(-halfWidth * 0.42f, lampY - 0.05f, halfLength - 0.06f),
                  Vector3(halfWidth * 0.42f, lampY + 0.07f, halfLength - 0.004f));
    grille.addBox(Vector3(-halfWidth * 0.52f, d.sill + 0.06f, halfLength - 0.10f),
                  Vector3(halfWidth * 0.52f, d.sill + 0.20f, halfLength - 0.004f));
    // Bumper rubbing strips front and rear.
    grille.addBox(Vector3(-halfWidth * 0.86f, d.sill + 0.02f, -halfLength + 0.004f),
                  Vector3(halfWidth * 0.86f, d.sill + 0.14f, -halfLength + 0.13f));

    // An estate or a van gets roof rails; a crossover gets black wheel-arch trim.
    if (type == VehicleType::Estate || type == VehicleType::Van)
        for (const float side : {-1.0f, 1.0f})
            grille.addBox(Vector3(side * halfWidth * 0.66f - 0.028f, roofY + 0.005f,
                                  roofRearZ + 0.10f),
                          Vector3(side * halfWidth * 0.66f + 0.028f, roofY + 0.055f,
                                  roofFrontZ - 0.10f));
    (void)rng;
}

}  // namespace CnaStreet
