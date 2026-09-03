// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <vector>

namespace CnaStreet {

class CityLayout;
class MaterialLibrary;

/// Where a crossing is and which way people walk over it. Shared with the
/// traffic-signal controller and with the pedestrian routes, so all three agree
/// about where it is safe to cross.
struct Crossing
{
    Microsoft::Xna::Framework::Vector2 centre{0.0f, 0.0f};
    /// Unit vector along the direction pedestrians walk.
    Microsoft::Xna::Framework::Vector2 walkDirection{1.0f, 0.0f};
    /// Half the distance walked, i.e. half the carriageway width.
    float halfLength = 5.5f;
    /// Half the crossing's own depth along the road.
    float halfDepth = 2.0f;
    /// True when it crosses the main street.
    bool crossesMain = true;
};

/**
 * @brief Generates the carriageway, kerbs, footways and every marking on them.
 *
 * The whole highway is one pass: the road surfaces tile exactly rather than
 * overlapping (a junction box, four carriageway arms, four footway runs and four
 * rounded corner pieces, each meeting its neighbour edge to edge), because two
 * coplanar surfaces fighting for the same depth is the single ugliest artefact
 * an outdoor scene can have and no amount of bias reliably hides it.
 *
 * Markings sit 6 mm above the asphalt with depth writes off, which is a
 * comfortable margin at this depth range and much simpler than a decal pass.
 */
class RoadBuilder
{
public:
    RoadBuilder(const CityLayout& layout, const MaterialLibrary& materials);

    void build(GeometryCollector& collector, Rng& rng);

    [[nodiscard]] const std::vector<Crossing>& crossings() const { return crossings_; }

private:
    void buildCarriageway(GeometryCollector& collector, Rng& rng);
    void buildKerbs(GeometryCollector& collector);
    void buildFootways(GeometryCollector& collector, Rng& rng);
    void buildMarkings(GeometryCollector& collector);
    void buildCrossings(GeometryCollector& collector);
    void buildIronwork(GeometryCollector& collector, Rng& rng);

    const CityLayout&      layout_;
    const MaterialLibrary& materials_;
    std::vector<Crossing>  crossings_;
};

}  // namespace CnaStreet
