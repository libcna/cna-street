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
    /// Which of the character's walks and which of its ways of standing
    /// this person uses, so a crowd of eight figures does not carry eight
    /// copies of one gait.
    int   walkStyle = 0;
    int   idleStyle = 0;
    /// One stride, in metres: a tall person's is longer, a brisk walker's
    /// longer again. The walk cycle's clock is distance over this.
    float stride = 1.42f;
    /// How far off the footway's walking line this person keeps, in metres,
    /// positive toward the buildings. Nobody walks the centre line; a crowd
    /// that does is a queue.
    float lateral = 0.0f;
    /// Where the body is pointed, smoothed toward the edge's heading: a
    /// person turns a corner over half a second rather than snapping
    /// through a right angle at a node.
    float facing = 0.0f;
    /// The person this one is walking with, or -1. A companion keeps to the
    /// leader's edge, speed and pace, a step behind and to the side.
    int   companion = -1;
    /// Development line-up only: stand here, facing this way, and do not move.
    bool  pinned = false;
    Microsoft::Xna::Framework::Vector2 pinnedAt{0.0f, 0.0f};
    float pinnedHeading = 0.0f;

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
    /// One of every appearance variant, standing still on a known pitch. The
    /// development companion to TrafficSystem::buildLineup, and for the same
    /// reason: a figure that walks off before the shutter opens cannot be
    /// looked at.
    void buildLineup(const CityLayout& layout, const std::vector<Crossing>& crossings,
                     std::uint32_t seed);
    [[nodiscard]] static Microsoft::Xna::Framework::Vector2 lineupPlace(int index);
    /// Whether this system is in line-up mode, in which nobody moves.
    [[nodiscard]] bool isLineup() const { return lineup_; }
    void update(float deltaSeconds, const TrafficSignalController& signals);

    [[nodiscard]] const std::vector<Pedestrian>& people() const { return people_; }
    [[nodiscard]] const std::vector<WalkNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<WalkEdge>& edges() const { return edges_; }
    [[nodiscard]] int waitingCount() const { return waitingCount_; }

    /// World transform for one person.
    [[nodiscard]] Microsoft::Xna::Framework::Matrix transform(const Pedestrian& person,
                                                              float groundHeight) const;
    /// How many complete stride cycles this person has walked. Drives the
    /// animation clock directly, so the feet keep up with the ground: a walk
    /// cycle advanced by wall-clock time instead slides on every slope and at
    /// every speed the simulation gives someone.
    [[nodiscard]] static float cyclesWalked(const Pedestrian& person);
    /// One stride, in metres, for a person of average height at an ordinary
    /// pace; each person's own is scaled from it.
    static constexpr float kStrideLength = 1.42f;
    /// The side of an edge the buildings are on: the unit direction a
    /// positive `Pedestrian::lateral` moves along.
    [[nodiscard]] Microsoft::Xna::Framework::Vector2 buildingSide(const WalkEdge& edge) const;

private:
    int addNode(const Microsoft::Xna::Framework::Vector2& position);
    void addEdge(int from, int to, bool crossing, SignalAxis axis);
    void buildGraph(const CityLayout& layout, const std::vector<Crossing>& crossings);

    std::vector<WalkNode>   nodes_;
    std::vector<WalkEdge>   edges_;
    std::vector<Pedestrian> people_;
    Rng                     rng_{1u};
    int waitingCount_ = 0;
    bool lineup_ = false;
};

}  // namespace CnaStreet
