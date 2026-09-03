// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Props/RoadBuilder.hpp"
#include "CnaStreet/Sim/TrafficSignals.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <vector>

namespace CnaStreet {

class CityLayout;

/// A place a walking route can pass through.
struct WalkNode
{
    Microsoft::Xna::Framework::Vector2 position{0.0f, 0.0f};
    std::vector<int> edges;
};

/// A stretch of a route between two nodes.
struct WalkEdge
{
    int from = 0;
    int to = 0;
    float length = 0.0f;
    /// True when this edge is a carriageway crossing, which means waiting for a
    /// green man before entering it.
    bool crossing = false;
    /// Which street the crossing crosses; only meaningful when `crossing`.
    SignalAxis crossedAxis = SignalAxis::Main;
};

/// One person.
struct Pedestrian
{
    int   edge = 0;
    float distance = 0.0f;     ///< along the current edge
    float speed = 1.35f;
    float height = 1.75f;
    int   variant = 0;         ///< clothing/skin combination
    float phase = 0.0f;        ///< distance walked, which drives the walk cycle
    bool  waiting = false;
    float waitTime = 0.0f;
    bool  reversed = false;    ///< traversing the edge from `to` toward `from`

    [[nodiscard]] Microsoft::Xna::Framework::Vector2 position(
        const std::vector<WalkNode>& nodes, const std::vector<WalkEdge>& edges) const;
    [[nodiscard]] float heading(const std::vector<WalkNode>& nodes,
                                const std::vector<WalkEdge>& edges) const;
};

/**
 * @brief People on the footway.
 *
 * A small graph, not a navmesh. The footways are four straight runs and the
 * crossings link them, so the walkable world is a dozen nodes and the edges
 * between them; a pedestrian walks an edge, picks another at the node it
 * arrives at, and repeats. Choosing an edge at random with a strong preference
 * for *not* turning back is what makes the traffic on the pavement look like
 * people going somewhere rather than like a crowd milling about.
 *
 * The only rule beyond that is the one that matters visually: an edge marked as
 * a crossing may not be entered unless the pedestrian signal for that street is
 * green, and someone who arrives on red waits at the kerb. That is why the
 * junction reads as a junction.
 */
class PedestrianSystem
{
public:
    static constexpr int kVariantCount = 8;

    void build(const CityLayout& layout, const std::vector<Crossing>& crossings,
               std::uint32_t seed, int count);
    void update(float deltaSeconds, const TrafficSignalController& signals);

    [[nodiscard]] const std::vector<Pedestrian>& people() const { return people_; }
    [[nodiscard]] const std::vector<WalkNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<WalkEdge>& edges() const { return edges_; }
    [[nodiscard]] int waitingCount() const { return waitingCount_; }

    /// World transform for one person.
    [[nodiscard]] Microsoft::Xna::Framework::Matrix transform(const Pedestrian& person,
                                                              float groundHeight) const;
    /// Which walk-cycle pose to draw, given how many the factory built.
    [[nodiscard]] static int poseFor(const Pedestrian& person, int poseCount);

private:
    int addNode(const Microsoft::Xna::Framework::Vector2& position);
    void addEdge(int from, int to, bool crossing, SignalAxis axis);
    void buildGraph(const CityLayout& layout, const std::vector<Crossing>& crossings);

    std::vector<WalkNode>   nodes_;
    std::vector<WalkEdge>   edges_;
    std::vector<Pedestrian> people_;
    Rng                     rng_{1u};
    int waitingCount_ = 0;
};

}  // namespace CnaStreet
