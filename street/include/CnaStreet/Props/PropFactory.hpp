// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

namespace CnaStreet {


/// Which sign blank a post carries. The shape is the message in European
/// signage — round prohibits, triangular warns, rectangular informs — so it is
/// chosen here rather than being a property of the artwork.
enum class SignShape
{
    Disc,
    TriangleUp,
    Rectangle,
    Square
};

/**
 * @brief Builds the objects that stand on the footway.
 *
 * Each function generates one prop in its own local space, with the origin at
 * the point where it meets the ground and +Z pointing the way it faces. The
 * caller places copies with a transform, so a lamp column is one mesh and forty
 * matrices rather than forty meshes.
 *
 * Everything here is built from the real thing's dimensions (see
 * `StreetMetrics`), because street furniture is the scale reference a viewer
 * actually uses: a bollard that is 30 cm too tall makes the whole street feel
 * small, and nobody can say why.
 */
class PropFactory
{
public:
    PropFactory(const MaterialLibrary& materials);

    /// A lighting column with its outreach arm and luminaire.
    void streetLamp(GeometryCollector& collector, float height, float reach) const;
    /// A cast-iron-ended bench with timber slats.
    void bench(GeometryCollector& collector) const;
    /// A bollard, with the reflective band near the top.
    void bollard(GeometryCollector& collector) const;
    /// A litter bin on its post.
    void litterBin(GeometryCollector& collector) const;
    /// An above-ground pillar hydrant.
    void hydrant(GeometryCollector& collector) const;
    /// The grey cable-distribution cabinet every European street has.
    void utilityCabinet(GeometryCollector& collector, Rng& rng) const;
    /// A "Sheffield" bicycle stand.
    void bicycleStand(GeometryCollector& collector) const;
    /// A planter with a clipped shrub in it.
    void planter(GeometryCollector& collector, Rng& rng) const;
    /// A bus shelter: posts, roof, and a glazed back and side.
    void busShelter(GeometryCollector& collector) const;

    /// The street trees. Three species rather than one: a plane with a broad
    /// high crown, a lime with a denser upright one, and a young tree of the
    /// kind a council plants to replace a felled one. A row of identical trees
    /// down a footway is as obvious as a row of identical cars.
    enum class TreeSpecies
    {
        Plane,
        Lime,
        Young,
        Count
    };

    /// A street tree: a tapered trunk with a root flare, a recursive branch
    /// structure, and leaf clusters on the twigs. @p detail below 1 halves the
    /// branching depth and the card count, for the trees at the far end.
    /// @p foliage overrides the catalogue's leaf material, so one street can
    /// carry six trees in six slightly different greens.
    void tree(GeometryCollector& collector, Rng& rng, TreeSpecies species, float height,
              bool fullDetail = true, const Material* foliage = nullptr) const;
    /// A bicycle, standing on its wheels along local +X with the origin under
    /// the bottom bracket, leaning a few degrees to +Z the way one leans on a
    /// stand. @p frameMaterial is the paint.
    void bicycle(GeometryCollector& collector, const Material* frameMaterial) const;
    /// The pool a street lamp throws on the ground, as a horizontal quad of
    /// @p radius metres. Placed only when the lamps are burning.
    void lightPool(GeometryCollector& collector, float radius) const;

    /// The cast-iron grating around a tree pit.
    void treeGrate(GeometryCollector& collector) const;
    /// The scruff at the base of a wall or a tree pit: tufts of weed, a few
    /// fallen leaves, grass through a crack. Deliberately sparse -- realism is
    /// controlled imperfection, and a footway with weeds every metre is a
    /// derelict one.
    void groundScruff(GeometryCollector& collector, Rng& rng, float radius, int tufts) const;

    /// A three-aspect vehicle signal head on its backing board, with the hoods.
    /// The lenses are *not* included: they are drawn separately so the signal
    /// controller can light them.
    void signalHead(GeometryCollector& collector) const;
    /// One signal lens, at the origin facing +Z. Built once and placed three
    /// times per head.
    void signalLens(GeometryCollector& collector, MaterialId material, float radius) const;
    /// A two-aspect pedestrian head.
    void pedestrianSignalHead(GeometryCollector& collector) const;
    /// The post a signal head is mounted on.
    void signalPost(GeometryCollector& collector, float height) const;
    /// A mast with an outreach arm carrying a signal over the carriageway.
    void signalMast(GeometryCollector& collector, float height, float reach) const;

    /// A sign on its own post. The face material decides what it says.
    void trafficSign(GeometryCollector& collector, SignShape shape, MaterialId face,
                     float mountHeight) const;
    /// A street-name plate on a wall bracket. The face material carries the
    /// name, so the two streets are two materials over one mesh generator.
    void streetPlate(GeometryCollector& collector, MaterialId face) const;

private:
    const MaterialLibrary& materials_;
};

}  // namespace CnaStreet
