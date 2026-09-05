// SPDX-License-Identifier: MIT
#include "CnaStreet/Sim/PedestrianSystem.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;

namespace CnaStreet {

namespace M = Metrics;

namespace {

float Distance(const Vector2& a, const Vector2& b)
{
    const float dx = b.X - a.X, dz = b.Y - a.Y;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

/// The side of an edge the buildings are on: away from the street the edge
/// runs beside. The graph is axis-aligned, so an edge along z is a main
/// street footway and its buildings are outward in x, and an edge along x
/// is a side street footway or a crossing of the main street with its
/// "buildings" outward in z -- for a crossing that is simply along the
/// zebra, which is where the spread belongs.
Vector2 BuildingSideOf(const WalkNode& a, const WalkNode& b)
{
    const float dx = b.position.X - a.position.X, dz = b.position.Y - a.position.Y;
    const Vector2 mid((a.position.X + b.position.X) * 0.5f, (a.position.Y + b.position.Y) * 0.5f);
    if (std::fabs(dz) > std::fabs(dx)) return Vector2(mid.X < 0.0f ? -1.0f : 1.0f, 0.0f);
    return Vector2(0.0f, mid.Y < 0.0f ? -1.0f : 1.0f);
}

Vector2 Pedestrian::position(const std::vector<WalkNode>& nodes,
                             const std::vector<WalkEdge>& edges) const
{
    if (pinned) return pinnedAt;
    const WalkEdge& e = edges[static_cast<std::size_t>(edge)];
    const WalkNode& na = nodes[static_cast<std::size_t>(reversed ? e.to : e.from)];
    const WalkNode& nb = nodes[static_cast<std::size_t>(reversed ? e.from : e.to)];
    const Vector2& a = na.position;
    const Vector2& b = nb.position;
    const float t = e.length > 1e-4f ? std::clamp(distance / e.length, 0.0f, 1.0f) : 0.0f;
    const Vector2 side = BuildingSideOf(na, nb);
    const float spread = e.crossing ? lateral * 0.6f : lateral;
    return Vector2(a.X + (b.X - a.X) * t + side.X * spread, a.Y + (b.Y - a.Y) * t + side.Y * spread);
}

Vector2 PedestrianSystem::buildingSide(const WalkEdge& edge) const
{
    return BuildingSideOf(nodes_[static_cast<std::size_t>(edge.from)],
                          nodes_[static_cast<std::size_t>(edge.to)]);
}

float Pedestrian::heading(const std::vector<WalkNode>& nodes,
                          const std::vector<WalkEdge>& edges) const
{
    if (pinned) return pinnedHeading;
    const WalkEdge& e = edges[static_cast<std::size_t>(edge)];
    const Vector2& a = nodes[static_cast<std::size_t>(reversed ? e.to : e.from)].position;
    const Vector2& b = nodes[static_cast<std::size_t>(reversed ? e.from : e.to)].position;
    return std::atan2(b.X - a.X, b.Y - a.Y);
}

int PedestrianSystem::addNode(const Vector2& position)
{
    // Coincident nodes are merged rather than joined. The main-street and
    // side-street chains both start at the same four corners, and joining the
    // two copies with an edge produced four zero-length edges -- an edge with no
    // length has no direction, so anyone who stepped onto one faced nowhere in
    // particular until they stepped off it again.
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        if (Distance(nodes_[i].position, position) < 0.20f) return static_cast<int>(i);

    nodes_.push_back(WalkNode{position, {}});
    return static_cast<int>(nodes_.size()) - 1;
}

void PedestrianSystem::addEdge(int from, int to, bool crossing, SignalAxis axis)
{
    if (from == to) return;
    // Nor a duplicate: the corner joins would otherwise add a second edge
    // between two nodes the chains already connect, and a pedestrian choosing
    // between them would appear to dither.
    for (const WalkEdge& existing : edges_)
        if ((existing.from == from && existing.to == to)
            || (existing.from == to && existing.to == from))
            return;

    WalkEdge edge;
    edge.from = from;
    edge.to = to;
    edge.length = Distance(nodes_[static_cast<std::size_t>(from)].position,
                           nodes_[static_cast<std::size_t>(to)].position);
    edge.crossing = crossing;
    edge.crossedAxis = axis;
    const int index = static_cast<int>(edges_.size());
    edges_.push_back(edge);
    nodes_[static_cast<std::size_t>(from)].edges.push_back(index);
    nodes_[static_cast<std::size_t>(to)].edges.push_back(index);
}

void PedestrianSystem::buildGraph(const CityLayout& layout, const std::vector<Crossing>& crossings)
{
    nodes_.clear();
    edges_.clear();

    const float mainKerb = M::kMainCarriagewayWidth * 0.5f;
    const float sideKerb = M::kSideCarriagewayWidth * 0.5f;
    // Walking lines run down the middle of each footway.
    const float mainWalk = mainKerb + M::kMainSidewalkWidth * 0.5f;
    const float sideWalk = sideKerb + M::kSideSidewalkWidth * 0.5f;

    // Corner nodes, one at each of the four junction corners on both walking
    // lines, plus the ends of each footway run.
    struct Run { Vector2 a, b; };
    std::vector<int> mainCornerNodes;   // NW, NE, SW, SE on the main walking line

    // Down the main street: a chain of nodes so people have somewhere to go.
    for (const float side : {-1.0f, 1.0f})
    {
        for (const float half : {-1.0f, 1.0f})
        {
            const float from = half * sideWalk;
            const float to   = half * (M::kMainStreetHalfLength - 4.0f);
            int previous = addNode(Vector2(side * mainWalk, from));
            mainCornerNodes.push_back(previous);
            const int steps = 6;
            for (int i = 1; i <= steps; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const int node = addNode(Vector2(side * mainWalk, from + (to - from) * t));
                addEdge(previous, node, false, SignalAxis::Main);
                previous = node;
            }
        }
    }

    // Down the side street.
    std::vector<int> sideCornerNodes;
    for (const float side : {-1.0f, 1.0f})
    {
        for (const float half : {-1.0f, 1.0f})
        {
            const float from = half * mainWalk;
            const float to   = half * (M::kSideStreetHalfLength - 3.0f);
            int previous = addNode(Vector2(from, side * sideWalk));
            sideCornerNodes.push_back(previous);
            const int steps = 4;
            for (int i = 1; i <= steps; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const int node = addNode(Vector2(from + (to - from) * t, side * sideWalk));
                addEdge(previous, node, false, SignalAxis::Side);
                previous = node;
            }
        }
    }

    // Round each corner: the main-street and side-street chains each start at a
    // node in the same place, so the two are joined where their signs agree.
    for (const int mainNode : mainCornerNodes)
        for (const int sideNode : sideCornerNodes)
        {
            const Vector2& a = nodes_[static_cast<std::size_t>(mainNode)].position;
            const Vector2& b = nodes_[static_cast<std::size_t>(sideNode)].position;
            if ((a.X > 0.0f) == (b.X > 0.0f) && (a.Y > 0.0f) == (b.Y > 0.0f))
                addEdge(mainNode, sideNode, false, SignalAxis::Main);
        }

    // The crossings themselves.
    for (const Crossing& crossing : crossings)
    {
        const Vector2 dir = crossing.walkDirection;
        const float reach = crossing.halfLength + (crossing.crossesMain
                                                       ? M::kMainSidewalkWidth * 0.5f
                                                       : M::kSideSidewalkWidth * 0.5f);
        const Vector2 a(crossing.centre.X - dir.X * reach, crossing.centre.Y - dir.Y * reach);
        const Vector2 b(crossing.centre.X + dir.X * reach, crossing.centre.Y + dir.Y * reach);
        const int na = addNode(a);
        const int nb = addNode(b);
        addEdge(na, nb, true, crossing.crossesMain ? SignalAxis::Main : SignalAxis::Side);

        // Join each kerb node to the nearest footway node, so the crossing is
        // reachable from the walking line.
        for (const int kerbNode : {na, nb})
        {
            int nearest = -1;
            float best = 1e9f;
            for (int i = 0; i < static_cast<int>(nodes_.size()); ++i)
            {
                if (i == na || i == nb) continue;
                const float d = Distance(nodes_[static_cast<std::size_t>(i)].position,
                                         nodes_[static_cast<std::size_t>(kerbNode)].position);
                if (d < best) { best = d; nearest = i; }
            }
            if (nearest >= 0 && best < 14.0f)
                addEdge(kerbNode, nearest, false, SignalAxis::Main);
        }
    }
    (void)layout;
}

void PedestrianSystem::build(const CityLayout& layout, const std::vector<Crossing>& crossings,
                             std::uint32_t seed, int count)
{
    rng_ = Rng::derive(seed, "pedestrians");
    // The gaits, stances and spacing draw from a stream of their own, so
    // that adding them left every person where the earlier passes' seed
    // had put them -- which is what keeps a viewpoint aimed at a person
    // aimed at a person.
    Rng manner = Rng::derive(seed, "pedestrian-manner");
    people_.clear();
    buildGraph(layout, crossings);
    if (edges_.empty()) return;

    for (int i = 0; i < count; ++i)
    {
        Pedestrian person;
        // Never start anyone on a crossing: at t=0 the light may be red, and a
        // figure standing in the carriageway is the first thing anyone notices.
        int guard = 0;
        do
        {
            person.edge = static_cast<int>(rng_.index(edges_.size()));
        } while (edges_[static_cast<std::size_t>(person.edge)].crossing && guard++ < 32);

        person.reversed = rng_.chance(0.5f);
        person.distance = rng_.range(0.0f, edges_[static_cast<std::size_t>(person.edge)].length);
        person.speed    = M::kWalkSpeed * rng_.aboutOne(0.22f);
        person.height   = rng_.range(M::kPersonHeightMin, M::kPersonHeightMax);
        person.variant  = rng_.intRange(0, kVariantCount - 1);
        person.phase    = rng_.range(0.0f, 10.0f);
        // A gait and a way of standing, and a pace to go with the gait: the
        // brisk walkers are the quicker ones. The stride follows the height
        // and the gait, so the animation's clock -- distance over stride --
        // keeps the feet on the ground for all of them.
        person.walkStyle = manner.intRange(0, 2);
        person.idleStyle = manner.intRange(0, 2);
        static const float kPace[3]   = {1.0f, 1.12f, 0.88f};
        static const float kStride[3] = {1.0f, 1.10f, 0.90f};
        person.speed  *= kPace[person.walkStyle];
        person.stride  = kStrideLength * std::pow(person.height / 1.75f, 0.6f)
                         * kStride[person.walkStyle];
        person.lateral = manner.range(-0.35f, 0.75f);
        person.facing  = person.heading(nodes_, edges_);
        // One person in six walks with the one before: same edge, same way,
        // same pace, a step behind and to the side, and they wait together.
        if (i % 6 == 5 && !people_.empty())
        {
            const Pedestrian& leader = people_.back();
            person.companion = static_cast<int>(people_.size()) - 1;
            person.edge      = leader.edge;
            person.reversed  = leader.reversed;
            person.speed     = leader.speed;
            person.walkStyle = leader.walkStyle;
            person.stride    = kStrideLength * std::pow(person.height / 1.75f, 0.6f)
                               * kStride[person.walkStyle];
            person.distance  = std::max(0.0f, leader.distance - 0.3f);
            // Beside the leader on whichever side has the room, and never
            // off the footway's walking band.
            person.lateral   = std::clamp(leader.lateral > 0.2f ? leader.lateral - 0.62f
                                                                : leader.lateral + 0.62f,
                                          -0.35f, 0.75f);
            person.facing    = leader.facing;
        }
        people_.push_back(person);
    }
}

void PedestrianSystem::update(float deltaSeconds, const TrafficSignalController& signals)
{
    if (lineup_) return;
    if (deltaSeconds <= 0.0f || edges_.empty()) return;
    const float dt = std::min(deltaSeconds, 0.1f);
    waitingCount_ = 0;

    for (Pedestrian& person : people_)
    {
        const WalkEdge& edge = edges_[static_cast<std::size_t>(person.edge)];

        // The body turns toward the way it is going over about half a second.
        {
            const float target = person.heading(nodes_, edges_);
            const float delta  = std::remainder(target - person.facing, MathHelper::TwoPi);
            const float step   = std::clamp(delta, -3.6f * dt, 3.6f * dt);
            person.facing = std::remainder(person.facing + step, MathHelper::TwoPi);
        }

        // A companion follows its leader rather than the graph.
        if (person.companion >= 0 && person.companion < static_cast<int>(people_.size()))
        {
            const Pedestrian& leader = people_[static_cast<std::size_t>(person.companion)];
            person.edge     = leader.edge;
            person.reversed = leader.reversed;
            person.waiting  = leader.waiting;
            person.waitTime = leader.waitTime + 1.3f;
            person.distance = std::max(0.0f, leader.distance - 0.3f);
            if (person.waiting) ++waitingCount_;
            else person.phase += person.speed * dt;
            continue;
        }

        if (person.waiting)
        {
            ++waitingCount_;
            person.waitTime += dt;
            if (signals.pedestrianGreen(edge.crossedAxis))
            {
                person.waiting = false;
                person.waitTime = 0.0f;
            }
            continue;
        }

        person.distance += person.speed * dt;
        person.phase += person.speed * dt;

        if (person.distance < edge.length) continue;

        // Arrived at a node: pick the next edge.
        const int arrivedAt = person.reversed ? edge.from : edge.to;
        const WalkNode& node = nodes_[static_cast<std::size_t>(arrivedAt)];
        if (node.edges.empty())
        {
            // A dead end: turn round rather than stop, which is what a person
            // walking to the end of a street does.
            person.reversed = !person.reversed;
            person.distance = 0.0f;
            continue;
        }

        // Prefer anything but the edge just walked, so people go somewhere.
        std::vector<int> choices;
        choices.reserve(node.edges.size());
        for (const int candidate : node.edges)
            if (candidate != person.edge) choices.push_back(candidate);
        if (choices.empty()) choices = node.edges;

        const int next = choices[rng_.index(choices.size())];
        const WalkEdge& nextEdge = edges_[static_cast<std::size_t>(next)];
        person.edge = next;
        person.reversed = nextEdge.to == arrivedAt;
        person.distance = 0.0f;

        if (nextEdge.crossing && !signals.pedestrianGreen(nextEdge.crossedAxis))
            person.waiting = true;
    }
}

Matrix PedestrianSystem::transform(const Pedestrian& person, float groundHeight) const
{
    const Vector2 at = person.position(nodes_, edges_);
    return Geometry::Place(at.X, groundHeight, at.Y,
                           person.pinned ? person.pinnedHeading : person.facing);
}

Vector2 PedestrianSystem::lineupPlace(int index)
{
    return Vector2(-(M::kMainCarriagewayWidth * 0.5f + M::kMainSidewalkWidth * 0.55f),
                   26.0f + 3.4f * static_cast<float>(index));
}

void PedestrianSystem::buildLineup(const CityLayout& layout,
                                   const std::vector<Crossing>& crossings, std::uint32_t seed)
{
    // Sixteen: one of each variant standing, cycling through the three
    // stances, then one of each frozen at successive eighths of the walk
    // cycle -- the only sane way to look at a gait, since a person who
    // walks off before the shutter opens cannot be looked at, and a knee
    // that bends the wrong way is obvious in a row of eight and invisible
    // in a crowd.
    build(layout, crossings, seed, kVariantCount * 2);
    lineup_ = true;
    for (int i = 0; i < static_cast<int>(people_.size()); ++i)
    {
        Pedestrian& person = people_[static_cast<std::size_t>(i)];
        const bool walking   = i >= kVariantCount;
        person.variant       = i % kVariantCount;
        person.waiting       = !walking;
        person.waitTime      = static_cast<float>(i) * 0.31f;
        person.speed         = 0.0f;
        person.pinned        = true;
        person.pinnedAt      = lineupPlace(i);
        person.pinnedHeading = MathHelper::PiOver2;
        person.idleStyle     = i % 3;
        person.walkStyle     = walking ? (i - kVariantCount) % 3 : 0;
        person.stride        = kStrideLength;
        person.phase         = walking ? kStrideLength * static_cast<float>(i - kVariantCount)
                                             / static_cast<float>(kVariantCount)
                                       : static_cast<float>(i) * 0.19f;
        person.companion     = -1;
        person.lateral       = 0.0f;
    }
}

float PedestrianSystem::cyclesWalked(const Pedestrian& person)
{
    return person.phase / std::max(person.stride, 0.5f);
}

}  // namespace CnaStreet
