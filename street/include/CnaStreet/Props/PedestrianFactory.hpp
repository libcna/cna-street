// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

namespace CnaStreet {

/**
 * @brief Builds people.
 *
 * Animation without a skeleton. A walking figure is generated at a fixed number
 * of phases through one stride, and the simulation picks the phase that matches
 * how far the pedestrian has walked. Eight phases at 1.35 m/s is a new pose
 * every 90 milliseconds, which is smooth enough that nobody counts them — and
 * it costs two draw calls per person instead of the skinned pipeline's
 * per-bone state.
 *
 * That trade is deliberate and worth stating: CNA has skeletal animation
 * (`SkinnedEffect`, `AnimationClipEXT`), and with an authored rigged character
 * it would be the right tool. Generating a rigged mesh procedurally, and a walk
 * cycle to drive it, would be a much larger piece of work whose visible result
 * at the distance a street is seen from is the same figure moving the same way.
 *
 * Proportions are canonical: a 1.75 m adult is 7.5 heads tall, the shoulders are
 * a quarter of the height wide, and the hip is at 0.53 of it.
 */
class PedestrianFactory
{
public:
    /// How many poses one stride is sampled at.
    static constexpr int kPhaseCount = 8;

    explicit PedestrianFactory(const MaterialLibrary& materials);

    /// Builds one figure at the origin, facing +Z, feet on y = 0.
    /// @p phase in [0, kPhaseCount) selects the point in the walk cycle;
    /// @p standing builds the idle pose instead.
    void build(GeometryCollector& collector, float height, int phase, bool standing,
               const Material* skin, const Material* clothing, const Material* trousers,
               Rng& rng) const;

private:
    const MaterialLibrary& materials_;
};

}  // namespace CnaStreet
