// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CnaStreet {

/// Which way a façade faces. The street grid is orthogonal, so four is enough,
/// and an enum makes the placement code read like a description of the street.
enum class Facing
{
    NegX,   ///< faces west
    PosX,   ///< faces east
    NegZ,   ///< faces south
    PosZ    ///< faces north
};

/// The architectural families along the street. Chosen so the district reads as
/// one place built over about 120 years, which is what a real inner-city street
/// is: nothing here is a different city.
enum class BuildingStyle
{
    /// 1890s five-storey perimeter block: rendered façade, string courses,
    /// cornice, balconies, pitched roof. The backbone of the street.
    Gruenderzeit,
    /// Same period, brick rather than render — a former warehouse or school.
    BrickWarehouse,
    /// Post-war infill: plainer, four storeys, flat roof, wider windows.
    PostWar,
    /// Contemporary office: full-height glazing in a bronze frame, flat roof,
    /// deep reveals, set-back top floor.
    ModernOffice,
    /// A single-storey shop unit filling a gap in the street wall.
    ShopUnit,
    /// A masonry corner block, heavier and taller, that turns the junction.
    CornerBlock
};

enum class RoofStyle
{
    Flat,
    Pitched,
    Mansard,
    FlatWithPlant   ///< flat roof with plant, lift overrun and a parapet
};

/// One building plot: the footprint, what stands on it, and which faces the
/// street can see. Everything the building generator needs and nothing else.
struct Plot
{
    /// Footprint in world XZ, axis aligned.
    float minX = 0.0f, maxX = 0.0f;
    float minZ = 0.0f, maxZ = 0.0f;

    BuildingStyle style = BuildingStyle::Gruenderzeit;
    RoofStyle     roof  = RoofStyle::Pitched;

    int   storeys = 5;
    float groundFloorHeight = 4.2f;
    float upperFloorHeight  = 3.1f;
    /// Index into the façade colour palette; the generator turns it into a
    /// material so the same plot always gets the same colour.
    int   colourIndex = 0;
    /// 0..1 grime, so the street is not uniformly weathered.
    float weathering = 0.5f;
    /// Whether the ground floor is a shop rather than a residential entrance.
    bool  hasShop = false;
    /// Faces that front a street and therefore get the full façade treatment.
    bool  detailed[4] = {false, false, false, false};
    /// Which of those is the *primary* frontage, i.e. where the entrance goes.
    Facing primary = Facing::PosX;
    /// The trading name on the shop fascia, empty when there is no shop.
    std::string shopName;
    /// Seed for everything inside this building, so one plot's variation does
    /// not shift when a neighbour changes.
    std::uint32_t seed = 0;

    [[nodiscard]] float width() const { return maxX - minX; }
    [[nodiscard]] float depth() const { return maxZ - minZ; }
    [[nodiscard]] float height() const
    {
        return groundFloorHeight + static_cast<float>(storeys - 1) * upperFloorHeight;
    }
    [[nodiscard]] Microsoft::Xna::Framework::Vector2 centre() const
    {
        return Microsoft::Xna::Framework::Vector2((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
    }
    [[nodiscard]] bool isDetailed(Facing facing) const
    {
        return detailed[static_cast<int>(facing)];
    }
};

/// A stretch of footway, used to place furniture, trees and pedestrians along it.
struct FootwayRun
{
    /// Along the run, in world space.
    Microsoft::Xna::Framework::Vector2 start{0.0f, 0.0f};
    Microsoft::Xna::Framework::Vector2 end{0.0f, 0.0f};
    /// Which way the kerb is, as a unit vector in XZ pointing at the road.
    Microsoft::Xna::Framework::Vector2 toKerb{1.0f, 0.0f};
    float width = 3.8f;
    bool  main  = true;
};

/**
 * @brief The street plan: where the roads run and what stands beside them.
 *
 * A crossroads, not a T-junction: a four-way gives the traffic signals something
 * to actually control and gives the camera four different views down a street.
 * The main street runs north–south, the side street east–west, and the four
 * quadrants are filled with a perimeter block whose buildings front both.
 *
 * Everything is derived from `StreetMetrics` and one seed. Given the same seed
 * the same street comes out, which is what makes the screenshot viewpoints
 * comparable between builds.
 */
class CityLayout
{
public:
    void generate(std::uint32_t seed);

    [[nodiscard]] const std::vector<Plot>& plots() const { return plots_; }
    [[nodiscard]] const std::vector<FootwayRun>& footways() const { return footways_; }

    /// Height of the walkable surface at a point: road level inside the
    /// carriageway, kerb height on the footway, and the building floor level
    /// where a plot stands. Used by the walking camera and by the pedestrians.
    [[nodiscard]] float groundHeight(float x, float z) const;
    /// Whether a point is inside a building or another solid obstacle.
    [[nodiscard]] bool isSolid(float x, float y, float z) const;
    /// Whether a point is on a footway (as opposed to the carriageway or a plot).
    [[nodiscard]] bool isFootway(float x, float z) const;
    /// Whether a point is inside the junction box.
    [[nodiscard]] bool isJunction(float x, float z) const;

    [[nodiscard]] const std::string& mainStreetName() const { return mainStreetName_; }
    [[nodiscard]] const std::string& sideStreetName() const { return sideStreetName_; }

private:
    void generateFrontage(Rng& rng, Facing facing, float buildingLine, float from, float to,
                          bool alongZ, bool isMainStreet, int& plotCounter);

    std::vector<Plot>       plots_;
    std::vector<FootwayRun> footways_;
    std::string mainStreetName_ = "LINDENSTRASSE";
    std::string sideStreetName_ = "MARKTGASSE";
};

}  // namespace CnaStreet
