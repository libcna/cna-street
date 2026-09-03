// SPDX-License-Identifier: MIT
#include "CnaStreet/Scene/CityLayout.hpp"

#include "CnaStreet/Scene/StreetMetrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using Microsoft::Xna::Framework::Vector2;

namespace CnaStreet {

namespace M = Metrics;

namespace {

/// Shop names for the ground-floor fascias. Ordinary trades, in the language of
/// the district, because a street where every shop has an invented brand name
/// reads as a game level rather than as a place.
const std::array<const char*, 18> kShopNames = {
    "BÄCKEREI",  "APOTHEKE",  "BLUMEN",    "BUCHHANDLUNG", "CAFÉ CENTRAL",
    "FAHRRÄDER", "OPTIKER",   "SCHUHE",    "FEINKOST",     "KIOSK",
    "FRISEUR",   "WEINHAUS",  "PAPETERIE", "GALERIE",      "REISEBÜRO",
    "SCHNEIDER", "ELEKTRO",   "IMBISS"};

/// Corner plots are deliberately generous: a corner is where the street turns,
/// and a narrow one makes the junction feel like a gap.
constexpr float kCornerWidthMin = 15.0f;
constexpr float kCornerWidthMax = 21.0f;

}  // namespace

void CityLayout::generate(std::uint32_t seed)
{
    plots_.clear();
    footways_.clear();

    Rng rng = Rng::derive(seed, "layout");
    int plotCounter = 0;

    const float mainLine = M::kMainStreetHalfWidth;   // building line, both sides
    const float sideLine = M::kSideStreetHalfWidth;

    // --- the four main-street frontages -----------------------------------
    generateFrontage(rng, Facing::PosX, -mainLine, sideLine, M::kMainStreetHalfLength, true, true,
                     plotCounter);
    generateFrontage(rng, Facing::PosX, -mainLine, -sideLine, -M::kMainStreetHalfLength, true, true,
                     plotCounter);
    generateFrontage(rng, Facing::NegX, mainLine, sideLine, M::kMainStreetHalfLength, true, true,
                     plotCounter);
    generateFrontage(rng, Facing::NegX, mainLine, -sideLine, -M::kMainStreetHalfLength, true, true,
                     plotCounter);

    // --- the four side-street frontages -----------------------------------
    generateFrontage(rng, Facing::PosZ, -sideLine, mainLine, M::kSideStreetHalfLength, false, false,
                     plotCounter);
    generateFrontage(rng, Facing::PosZ, -sideLine, -mainLine, -M::kSideStreetHalfLength, false,
                     false, plotCounter);
    generateFrontage(rng, Facing::NegZ, sideLine, mainLine, M::kSideStreetHalfLength, false, false,
                     plotCounter);
    generateFrontage(rng, Facing::NegZ, sideLine, -mainLine, -M::kSideStreetHalfLength, false,
                     false, plotCounter);

    // --- footway runs ------------------------------------------------------
    const float mainKerb = M::kMainCarriagewayWidth * 0.5f;
    const float sideKerb = M::kSideCarriagewayWidth * 0.5f;
    const float mainCentre = (mainKerb + mainLine) * 0.5f;
    const float sideCentre = (sideKerb + sideLine) * 0.5f;

    // Main street, both sides, north and south of the junction.
    for (const float sign : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
        {
            FootwayRun run;
            run.start  = Vector2(sign * mainCentre, half > 0.0f ? sideLine : -sideLine);
            run.end    = Vector2(sign * mainCentre, half * M::kMainStreetHalfLength);
            run.toKerb = Vector2(-sign, 0.0f);
            run.width  = M::kMainSidewalkWidth;
            run.main   = true;
            footways_.push_back(run);
        }
    // Side street, both sides, east and west of the junction.
    for (const float sign : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
        {
            FootwayRun run;
            run.start  = Vector2(half > 0.0f ? mainLine : -mainLine, sign * sideCentre);
            run.end    = Vector2(half * M::kSideStreetHalfLength, sign * sideCentre);
            run.toKerb = Vector2(0.0f, -sign);
            run.width  = M::kSideSidewalkWidth;
            run.main   = false;
            footways_.push_back(run);
        }
    (void)sideKerb;
}

void CityLayout::generateFrontage(Rng& rng, Facing facing, float buildingLine, float from,
                                  float to, bool alongZ, bool isMainStreet, int& plotCounter)
{
    const float direction = to > from ? 1.0f : -1.0f;
    const float span = std::fabs(to - from);
    float travelled = 0.0f;
    bool  firstOnRun = true;

    while (travelled < span - M::kPlotWidthMin)
    {
        // The first plot on each main-street run is the corner block; the side
        // street picks up from where that corner already ends.
        const bool corner = firstOnRun && isMainStreet;
        float width = corner ? rng.range(kCornerWidthMin, kCornerWidthMax)
                             : rng.range(M::kPlotWidthMin, M::kPlotWidthMax);
        width = std::min(width, span - travelled);
        // A sliver left at the end is absorbed into the previous plot rather
        // than built as a 3 m building.
        if (span - travelled - width < M::kPlotWidthMin) width = span - travelled;

        const float depth = corner ? rng.range(19.0f, 24.0f) : rng.range(13.5f, 21.0f);

        Plot plot;
        plot.seed = static_cast<std::uint32_t>(rng.intRange(1, 1 << 28));
        const float a = from + direction * travelled;
        const float b = from + direction * (travelled + width);

        if (alongZ)
        {
            plot.minZ = std::min(a, b);
            plot.maxZ = std::max(a, b);
            if (facing == Facing::PosX) { plot.maxX = buildingLine; plot.minX = buildingLine - depth; }
            else                        { plot.minX = buildingLine; plot.maxX = buildingLine + depth; }
        }
        else
        {
            plot.minX = std::min(a, b);
            plot.maxX = std::max(a, b);
            if (facing == Facing::PosZ) { plot.maxZ = buildingLine; plot.minZ = buildingLine - depth; }
            else                        { plot.minZ = buildingLine; plot.maxZ = buildingLine + depth; }
        }

        plot.primary = facing;
        plot.detailed[static_cast<int>(facing)] = true;

        // A corner block is seen from both streets, so both its street faces get
        // the full treatment. Without this the side-street elevation of every
        // corner would be a blank wall, which is the single most obvious way a
        // procedural street gives itself away.
        if (corner)
        {
            const Facing other = alongZ ? (std::fabs(plot.minZ) < std::fabs(plot.maxZ)
                                               ? Facing::NegZ : Facing::PosZ)
                                        : (std::fabs(plot.minX) < std::fabs(plot.maxX)
                                               ? Facing::NegX : Facing::PosX);
            plot.detailed[static_cast<int>(other)] = true;
        }

        // --- what kind of building ----------------------------------------
        const float roll = rng.unit();
        if (corner)
        {
            plot.style   = BuildingStyle::CornerBlock;
            plot.storeys = rng.intRange(5, 6);
            plot.roof    = rng.chance(0.55f) ? RoofStyle::Mansard : RoofStyle::Pitched;
        }
        else if (roll < 0.40f)
        {
            plot.style   = BuildingStyle::Gruenderzeit;
            plot.storeys = rng.intRange(4, 6);
            plot.roof    = rng.chance(0.7f) ? RoofStyle::Pitched : RoofStyle::Mansard;
        }
        else if (roll < 0.56f)
        {
            plot.style   = BuildingStyle::BrickWarehouse;
            plot.storeys = rng.intRange(4, 5);
            plot.roof    = RoofStyle::Pitched;
        }
        else if (roll < 0.78f)
        {
            plot.style   = BuildingStyle::PostWar;
            plot.storeys = rng.intRange(3, 5);
            plot.roof    = RoofStyle::Flat;
        }
        else if (roll < 0.93f)
        {
            plot.style   = BuildingStyle::ModernOffice;
            plot.storeys = rng.intRange(4, 7);
            plot.roof    = RoofStyle::FlatWithPlant;
        }
        else
        {
            plot.style   = BuildingStyle::ShopUnit;
            plot.storeys = 1;
            plot.roof    = RoofStyle::Flat;
        }

        // A side street is quieter and lower: dropping a storey there is what
        // stops the whole district reading as one uniform height.
        if (!isMainStreet && plot.storeys > 3) plot.storeys -= 1;

        plot.groundFloorHeight = plot.style == BuildingStyle::ModernOffice
                                     ? M::kOfficeFloorHeight
                                     : M::kGroundFloorHeight;
        plot.upperFloorHeight = plot.style == BuildingStyle::ModernOffice
                                    ? M::kOfficeFloorHeight
                                    : M::kUpperFloorHeight * rng.aboutOne(0.035f);
        plot.colourIndex = rng.intRange(0, 6);
        plot.weathering  = rng.range(0.25f, 0.95f);

        // Shops line the main street and thin out along the side street.
        const float shopChance = isMainStreet ? 0.82f : 0.40f;
        plot.hasShop = plot.style != BuildingStyle::ModernOffice && rng.chance(shopChance);
        if (plot.style == BuildingStyle::ShopUnit) plot.hasShop = true;
        if (plot.hasShop)
            plot.shopName = kShopNames[rng.index(kShopNames.size())];

        plots_.push_back(plot);
        travelled += width;
        firstOnRun = false;
        ++plotCounter;
    }
}

float CityLayout::groundHeight(float x, float z) const
{
    const float mainKerb = M::kMainCarriagewayWidth * 0.5f;
    const float sideKerb = M::kSideCarriagewayWidth * 0.5f;

    const bool onMainCarriageway = std::fabs(x) <= mainKerb
                                   && std::fabs(z) <= M::kMainStreetHalfLength;
    const bool onSideCarriageway = std::fabs(z) <= sideKerb
                                   && std::fabs(x) <= M::kSideStreetHalfLength;
    if (onMainCarriageway || onSideCarriageway) return 0.0f;

    if (isFootway(x, z)) return M::kCurbHeight;

    // Inside a plot the floor is one step above the footway, which is what a
    // real threshold is; outside everything, it is still the footway level so
    // the camera never falls through the world.
    for (const Plot& plot : plots_)
        if (x >= plot.minX && x <= plot.maxX && z >= plot.minZ && z <= plot.maxZ)
            return M::kCurbHeight + 0.16f;

    return M::kCurbHeight;
}

bool CityLayout::isFootway(float x, float z) const
{
    const float mainKerb = M::kMainCarriagewayWidth * 0.5f;
    const float sideKerb = M::kSideCarriagewayWidth * 0.5f;

    const bool mainStrip = std::fabs(x) > mainKerb && std::fabs(x) <= M::kMainStreetHalfWidth
                           && std::fabs(z) <= M::kMainStreetHalfLength;
    const bool sideStrip = std::fabs(z) > sideKerb && std::fabs(z) <= M::kSideStreetHalfWidth
                           && std::fabs(x) <= M::kSideStreetHalfLength;
    return mainStrip || sideStrip;
}

bool CityLayout::isJunction(float x, float z) const
{
    return std::fabs(x) <= M::kMainStreetHalfWidth && std::fabs(z) <= M::kSideStreetHalfWidth;
}

bool CityLayout::isSolid(float x, float y, float z) const
{
    for (const Plot& plot : plots_)
    {
        if (x < plot.minX || x > plot.maxX || z < plot.minZ || z > plot.maxZ) continue;
        if (y < M::kCurbHeight - 0.4f) continue;
        if (y > plot.height() + 6.0f) continue;
        return true;
    }
    return false;
}

}  // namespace CnaStreet
