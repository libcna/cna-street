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

Vector2 Pedestrian::position(const std::vector<WalkNode>& nodes,
                             const std::vector<WalkEdge>& edges) const
{
    const WalkEdge& e = edges[static_cast<std::size_t>(edge)];
    const Vector2& a = nodes[static_cast<std::size_t>(reversed ? e.to : e.from)].position;
    const Vector2& b = nodes[static_cast<std::size_t>(reversed ? e.from : e.to)].position;
    const float t = e.length > 1e-4f ? std::clamp(distance / e.length, 0.0f, 1.0f) : 0.0f;
    return Vector2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
}

float Pedestrian::heading(const std::vector<WalkNode>& nodes,
                          const std::vector<WalkEdge>& edges) const
{
    const WalkEdge& e = edges[static_cast<std::size_t>(edge)];
    const Vector2& a = nodes[static_cast<std::size_t>(reversed ? e.to : e.from)].position;
    const Vector2& b = nodes[static_cast<std::size_t>(reversed ? e.from : e.to)].position;
    return std::atan2(b.X - a.X, b.Y - a.Y);
}

int PedestrianSystem::addNode(const Vector2& position)
{
    nodes_.push_back(WalkNode{position, {}});
    return static_cast<int>(nodes_.size()) - 1;
}

void PedestrianSystem::addEdge(int from, int to, bool crossing, SignalAxis axis)
{
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
        people_.push_back(person);
    }
}

void PedestrianSystem::update(float deltaSeconds, const TrafficSignalController& signals)
{
    if (deltaSeconds <= 0.0f || edges_.empty()) return;
    const float dt = std::min(deltaSeconds, 0.1f);
    waitingCount_ = 0;

    for (Pedestrian& person : people_)
    {
        const WalkEdge& edge = edges_[static_cast<std::size_t>(person.edge)];

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
    return Geometry::Place(at.X, groundHeight, at.Y, person.heading(nodes_, edges_));
}

int PedestrianSystem::poseFor(const Pedestrian& person, int poseCount)
{
    if (poseCount <= 0) return 0;
    if (person.waiting) return poseCount;   // the idle pose sits after the cycle
    // One stride is about 1.5 m, sampled at poseCount phases.
    const float stride = 1.5f;
    const float t = person.phase / stride;
    const int index = static_cast<int>(t * static_cast<float>(poseCount))
                      % poseCount;
    return index < 0 ? index + poseCount : index;
}

}  // namespace CnaStreet
