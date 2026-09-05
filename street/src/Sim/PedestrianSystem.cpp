// SPDX-License-Identifier: MIT
#include "CnaStreet/Sim/PedestrianSystem.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <vector>
#include <cmath>
#include <iterator>

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
/// The lane a person aims for on the edge they are on: one for each
/// direction of travel. Keeping to a side is what a footway does, and it is
/// the cheapest crowd rule there is -- a fixed target rather than a mutual
/// push, so nothing can oscillate between two people who each keep moving
/// out of the other's way.
float LaneFor(const Pedestrian& person)
{
    return person.reversed ? PedestrianSystem::kLaneAway : PedestrianSystem::kLaneToward;
}

/// Beside a leader rather than inside them: a place on whichever side of the
/// leader keeps both on the footway.
float CompanionLateral(float leaderLateral)
{
    const float step = leaderLateral > 0.10f ? -PedestrianSystem::kPersonalSpace
                                             : PedestrianSystem::kPersonalSpace;
    return std::clamp(leaderLateral + step, -1.05f, 1.15f);
}

Vector2 BuildingSideOf(const WalkNode& a, const WalkNode& b)
{
    const float dx = b.position.X - a.position.X, dz = b.position.Y - a.position.Y;
    const Vector2 mid((a.position.X + b.position.X) * 0.5f, (a.position.Y + b.position.Y) * 0.5f);
    if (std::fabs(dz) > std::fabs(dx)) return Vector2(mid.X < 0.0f ? -1.0f : 1.0f, 0.0f);
    return Vector2(0.0f, mid.Y < 0.0f ? -1.0f : 1.0f);
}

void Pedestrian::queuePlace(int slot, float& back, float& across)
{
    // A loose cluster at the kerb, not a rank: five abreast at 0.72 m between
    // shoulders, a second row half a place offset behind them, a third behind
    // that. Filled from the middle out, so one person waits on the crossing's
    // centre line and a crowd spreads along the kerb rather than backing into
    // the shopfronts -- there are only 1.9 m of footway behind the walking
    // line on the main street and 1.3 m on the side street, which is what
    // caps the rows at three and their spacing at 0.62 m.
    static const struct { float across; int row; } kPlaces[] = {
        {+0.00f, 0}, {-0.72f, 0}, {+0.72f, 0}, {-1.44f, 0}, {+1.44f, 0},
        {-0.36f, 1}, {+0.36f, 1}, {-1.08f, 1}, {+1.08f, 1}, {-1.80f, 1},
        {+0.00f, 2}, {-0.72f, 2}, {+0.72f, 2}, {-1.44f, 2}, {+1.44f, 2},
    };
    constexpr int kCount = static_cast<int>(std::size(kPlaces));
    const int index = slot < 0 ? 0 : slot % kCount;
    const int wrap  = slot < 0 ? 0 : slot / kCount;
    across = kPlaces[index].across;
    // Past fifteen at one kerb -- which this street never reaches -- the
    // places repeat a row further back rather than on top of each other.
    back = 0.32f + static_cast<float>(kPlaces[index].row + 3 * wrap) * 0.62f;
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
    // The offset is the same on a crossing as on a footway. It used to be
    // scaled down to six tenths there, back when the band was wide enough to
    // walk somebody off the zebra; with the band the crowd rules keep people
    // in, 1.15 m either side of the centre line is well inside a 4 m
    // crossing, and the scale was quietly halving the gap between a pair
    // walking together -- 0.62 m of lateral separation arriving as 0.37 m of
    // actual daylight, which is two people in one coat.
    const float spread = lateral;
    Vector2 at(a.X + (b.X - a.X) * t + side.X * spread, a.Y + (b.Y - a.Y) * t + side.Y * spread);
    // Waiting for a green man: stand in the place at the kerb this person was
    // given rather than on the node. Every waiting person used to stand at
    // distance zero on the crossing edge, so four who arrived together stood
    // in one another.
    if (waiting && queueSlot >= 0 && e.length > 1e-4f)
    {
        float back = 0.0f, across = 0.0f;
        queuePlace(queueSlot, back, across);
        const Vector2 along((b.X - a.X) / e.length, (b.Y - a.Y) / e.length);
        at.X += -along.X * back + side.X * across;
        at.Y += -along.Y * back + side.Y * across;
    }
    return at;
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
        // Which of the two lanes this person walks in follows the direction
        // they are going, so two people meeting head on are already 0.6 m
        // apart when they meet. The personal offset on top is what keeps the
        // lane from being a painted line.
        person.lateralBias = manner.range(-0.16f, 0.16f);
        person.lateral = LaneFor(person) + person.lateralBias;
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
            person.lateralBias = leader.lateralBias;
            person.lateral   = CompanionLateral(leader.lateral);
            person.facing    = leader.facing;
            person.companionSide = person.lateral - leader.lateral;
        }
        people_.push_back(person);
    }

    // Nobody starts inside anybody. People are dealt onto the edges at
    // random, so a few pairs land on the same square metre, and the crowd
    // rules only ever *reduce* an overlap -- one that exists at the first
    // frame can persist for as long as the two walk together. A second of
    // separation before the first frame settles it.
    for (int i = 0; i < 30; ++i) separate(1.0f / 30.0f);
}

void PedestrianSystem::update(float deltaSeconds, const TrafficSignalController& signals)
{
    if (lineup_) return;
    if (deltaSeconds <= 0.0f || edges_.empty()) return;
    const float dt = std::min(deltaSeconds, 0.1f);
    waitingCount_ = 0;
    // Where everybody is at the start of the step, for the rules that are
    // about people rather than about edges.
    positions_.resize(people_.size());
    for (std::size_t i = 0; i < people_.size(); ++i)
        positions_[i] = people_[i].position(nodes_, edges_);

    for (std::size_t index = 0; index < people_.size(); ++index)
    {
        Pedestrian& person = people_[index];
        const WalkEdge& edge = edges_[static_cast<std::size_t>(person.edge)];

        // The body turns toward the way it is going over about half a second.
        {
            const float target = person.heading(nodes_, edges_);
            const float delta  = std::remainder(target - person.facing, MathHelper::TwoPi);
            const float step   = std::clamp(delta, -3.6f * dt, 3.6f * dt);
            person.facing = std::remainder(person.facing + step, MathHelper::TwoPi);
        }

        // And eases across to the lane its direction of travel keeps to,
        // over about a second. Rate limited toward a fixed target, so a
        // person who turns at a corner crosses the footway rather than
        // stepping sideways, and two people cannot chase each other.
        if (!person.waiting && person.companion < 0)
        {
            const float want = LaneFor(person) + person.lateralBias;
            // Gently, and more gently than the sidestep in separate(): the
            // lane is a preference and getting out of somebody's way is not.
            person.lateral += std::clamp(want - person.lateral, -0.55f * dt, 0.55f * dt);
        }

        // A companion follows its leader rather than the graph.
        if (person.companion >= 0 && person.companion < static_cast<int>(people_.size()))
        {
            const Pedestrian& leader = people_[static_cast<std::size_t>(person.companion)];
            const bool wasWaiting = person.waiting;
            person.edge     = leader.edge;
            person.reversed = leader.reversed;
            person.waiting  = leader.waiting;
            person.waitTime = leader.waitTime + 1.3f;
            person.distance = std::max(0.0f, leader.distance - 0.3f);
            // Eased rather than set: a companion whose place beside its
            // leader is rewritten every frame can never be stepped aside by
            // the separation pass, and a pair walking through a third person
            // was most of what was left of the overlaps.
            if (!person.waiting)
            {
                const float want = std::clamp(leader.lateral + person.companionSide, -1.05f, 1.15f);
                // Gently, and more gently than the sidestep in separate(): the
            // lane is a preference and getting out of somebody's way is not.
            person.lateral += std::clamp(want - person.lateral, -0.55f * dt, 0.55f * dt);
            }
            // A companion queues at the kerb like anybody else, in a place of
            // its own beside its leader's rather than inside it.
            if (person.waiting && !wasWaiting)
                person.queueSlot = takeQueueSlot(index, leader.queueSlot);
            if (!person.waiting) { person.queueSlot = -1; person.stepOff = -1.0f; }
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
                float back = 0.0f, across = 0.0f;
                Pedestrian::queuePlace(person.queueSlot, back, across);
                // The front of the queue goes first. Half a second a row is
                // what a group at a crossing actually does, and it is what
                // stops six people who were standing in six places from
                // arriving on one point the instant the man turns green.
                if (person.stepOff < 0.0f) person.stepOff = person.waitTime + back * 0.55f;
                if (person.waitTime >= person.stepOff)
                {
                    // And they set off from where they were standing: the
                    // place across the crossing they had at the kerb becomes
                    // their offset on it, which the lane easing then draws
                    // back in over the width of the road.
                    person.lateral = std::clamp(across, -1.05f, 1.15f);
                    person.waiting = false;
                    person.waitTime = 0.0f;
                    person.queueSlot = -1;
                    person.stepOff = -1.0f;
                }
            }
            continue;
        }

        // Nobody walks through the back of the person in front. A following
        // rule, not a repulsion: the follower takes the leader's pace while
        // it is inside a personal space, which cannot oscillate because
        // nothing pushes back.
        const float pace = paceBehind(index, person.speed);
        person.distance += pace * dt;
        person.phase += pace * dt;

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
        {
            person.waiting = true;
            person.queueSlot = takeQueueSlot(index);
        }
    }

    separate(dt);

    // The last word, after everybody has stepped aside for everybody else: a
    // companion is never inside its leader. It is a floor on the companion
    // alone -- there is nothing here for the leader to react to -- and it is
    // applied here rather than before the sidestep because a third person
    // pushing a companion into its leader would otherwise stand for a frame.
    for (std::size_t i = 0; i < people_.size(); ++i)
    {
        Pedestrian& person = people_[i];
        if (person.pinned || person.companion < 0
            || person.companion >= static_cast<int>(people_.size()))
            continue;
        const Pedestrian& leader = people_[static_cast<std::size_t>(person.companion)];
        const Vector2 mine = person.position(nodes_, edges_);
        const Vector2 theirs = leader.position(nodes_, edges_);
        const float dx = mine.X - theirs.X, dz = mine.Y - theirs.Y;
        const float apart = std::sqrt(dx * dx + dz * dz);
        // Not inside them, and not across the footway from them either: a
        // companion stepped aside by a third person walks back to its
        // leader's side, it does not become a stranger.
        if (apart >= 0.46f && apart <= (person.waiting ? 2.8f : 1.10f)) continue;
        if (person.waiting)
        {
            // Standing in the leader's place at the kerb: take another. The
            // two of them can arrive in the same frame, and whichever asked
            // first had nobody to avoid.
            if (apart < 0.46f) person.queueSlot = takeQueueSlot(i, leader.queueSlot);
            continue;
        }
        if (apart > 1.10f)
        {
            person.lateral = std::clamp(person.lateral,
                                        leader.lateral - 1.00f, leader.lateral + 1.00f);
            if (i < positions_.size()) positions_[i] = person.position(nodes_, edges_);
            continue;
        }
        float side = person.companionSide < 0.0f ? -1.0f : 1.0f;
        if (leader.lateral + side * kPersonalSpace > 1.15f
            || leader.lateral + side * kPersonalSpace < -1.05f)
            side = -side;
        person.companionSide = side * kPersonalSpace;
        person.lateral = std::clamp(leader.lateral + person.companionSide, -1.05f, 1.15f);
        if (i < positions_.size()) positions_[i] = person.position(nodes_, edges_);
    }
}

void PedestrianSystem::separate(float dt)
{
    // Nobody stands inside anybody. The rule is deliberately one-directional:
    // a person gives way only to people *earlier in the list* and to anyone
    // waiting at a kerb, and never the other way round. A mutual push -- each
    // of a pair stepping out of the other's way -- is the version of this that
    // jitters, because both keep reacting to a neighbour who is reacting to
    // them. A strict order cannot cycle, so it settles in one pass, and it is
    // the same order every run, so the crowd stays a function of the seed.
    //
    // Only the lateral offset moves: a person stays on the edge they are
    // walking and inside the footway's band, and the sidestep is rate limited
    // to something a person could actually do.
    if (people_.empty()) return;
    std::vector<Vector2> at(people_.size());
    std::vector<Vector2> side(people_.size());
    for (std::size_t i = 0; i < people_.size(); ++i)
    {
        at[i] = people_[i].position(nodes_, edges_);
        side[i] = buildingSide(edges_[static_cast<std::size_t>(people_[i].edge)]);
    }
    positions_ = at;
    // The rate limit is a person's whole sidestep for the step, shared
    // between the passes rather than paid again in each of them.
    constexpr int kPasses = 3;
    const float step = 1.6f * dt / static_cast<float>(kPasses);
    const float give = 1.1f * dt / static_cast<float>(kPasses);
    for (int pass = 0; pass < kPasses; ++pass)
        for (std::size_t i = 0; i < people_.size(); ++i)
        {
            Pedestrian& person = people_[i];
            if (person.pinned || person.waiting) continue;
            const Vector2 forward(std::sin(person.facing), std::cos(person.facing));
            float sideways = 0.0f;
            float backward = 0.0f;
            for (std::size_t j = 0; j < people_.size(); ++j)
            {
                if (j == i) continue;
                // Give way to those ahead of you in the list, and to anyone
                // standing at a kerb whatever their place in it.
                if (j > i && !people_[j].waiting) continue;
                if (static_cast<int>(j) == person.companion) continue;
                const float dx = at[i].X - at[j].X;
                const float dz = at[i].Y - at[j].Y;
                const float d2 = dx * dx + dz * dz;
                if (d2 > kPersonalSpace * kPersonalSpace) continue;
                const float d = std::sqrt(std::max(d2, 1e-8f));
                const float strength = kPersonalSpace - d;
                const Vector2 theirs(std::sin(people_[j].facing), std::cos(people_[j].facing));
                if (!people_[j].waiting
                    && forward.X * theirs.X + forward.Y * theirs.Y < -0.3f)
                {
                    // Somebody walking at you. Backing off is no answer --
                    // they are coming -- so step aside: away from where they
                    // actually are if that is a direction at all, and
                    // otherwise toward your own side of the footway, which
                    // is the side they are not aiming for.
                    const float offset = dx * side[i].X + dz * side[i].Y;
                    sideways += strength
                                * (std::fabs(offset) > 0.05f
                                       ? (offset < 0.0f ? -1.0f : 1.0f)
                                       : (LaneFor(person) > person.lateral ? 1.0f : -1.0f));
                    continue;
                }
                // Otherwise push away in whatever direction the two of you
                // actually are, split into the two things a person on an edge
                // can do: step across it, and drop back along it. Never
                // forward: a shove that moves somebody up the pavement is a
                // teleport, and the person in front would feel it next.
                Vector2 dirX(dx / d, dz / d);
                if (d2 <= 1e-6f)
                    dirX = Vector2(side[i].X * ((i + j) % 2 == 0 ? 1.0f : -1.0f),
                                   side[i].Y * ((i + j) % 2 == 0 ? 1.0f : -1.0f));
                sideways += strength * (dirX.X * side[i].X + dirX.Y * side[i].Y);
                backward += strength
                            * std::max(0.0f, -(dirX.X * forward.X + dirX.Y * forward.Y));
            }
            if (backward > 0.0f)
            {
                const float back = std::min(backward, give);
                // `phase` goes with it, or the feet slide over the ground by
                // however far the body was moved.
                person.distance = std::max(0.0f, person.distance - back);
                person.phase = std::max(0.0f, person.phase - back);
            }
            if (sideways != 0.0f)
            {
                const float want = std::clamp(person.lateral + sideways, -1.05f, 1.15f);
                person.lateral += std::clamp(want - person.lateral, -step, step);
            }
            at[i] = person.position(nodes_, edges_);
            positions_[i] = at[i];
        }
}

int PedestrianSystem::takeQueueSlot(std::size_t who, int beside) const
{
    // The lowest place at this kerb that leaves this person clear of everyone
    // already standing there -- tested as a *position*, not as a slot number.
    // The two crossings at a junction corner start from the same node and run
    // at right angles, so slot three of one and slot three of the other are
    // two different points that can still be half a metre apart; comparing
    // numbers would have let two people stand in one another there, which is
    // exactly the corner somebody watched four people pile up on.
    Pedestrian trial = people_[who];
    trial.waiting = true;
    // Somebody arriving with a companion tries the places either side of
    // theirs first, so a pair that walked here together waits here together.
    std::vector<int> order;
    order.reserve(24);
    if (beside >= 0)
        for (int step = 1; step <= 4; ++step)
            for (const int sign : {1, -1})
                if (beside + sign * step >= 0 && beside + sign * step < 24)
                    order.push_back(beside + sign * step);
    for (int slot = 0; slot < 24; ++slot)
        if (std::find(order.begin(), order.end(), slot) == order.end()) order.push_back(slot);
    for (const int slot : order)
    {
        trial.queueSlot = slot;
        const Vector2 at = trial.position(nodes_, edges_);
        bool clear = true;
        for (std::size_t i = 0; i < people_.size() && clear; ++i)
        {
            if (i == who || !people_[i].waiting) continue;
            const Vector2 other = people_[i].position(nodes_, edges_);
            const float dx = at.X - other.X, dz = at.Y - other.Y;
            clear = dx * dx + dz * dz >= kPersonalSpace * kPersonalSpace;
        }
        if (clear) return slot;
    }
    return 0;
}

float PedestrianSystem::paceBehind(std::size_t who, float wanted) const
{
    // Nobody walks through the back of the person in front, wherever that
    // person is: this is in world space, not along one edge, because the two
    // places a crowd actually collides are a kerb -- where the people waiting
    // are on a *crossing* edge and the people arriving are on a footway one
    // -- and a corner, where four edges meet at one node.
    //
    // The rule gives way in one direction only: to anybody standing at a
    // kerb, and otherwise to people earlier in the list. A rule where both of
    // a pair yield is a rule where both of a pair can stop, and a pair of
    // pedestrians frozen a metre apart looking at each other is worse than a
    // pair that brushes shoulders.
    const Pedestrian& person = people_[who];
    const Vector2 forward(std::sin(person.facing), std::cos(person.facing));
    const Vector2 here = positions_[who];
    float pace = wanted;
    for (std::size_t i = 0; i < people_.size(); ++i)
    {
        if (i == who) continue;
        const Pedestrian& other = people_[i];
        if (other.pinned) continue;
        if (static_cast<int>(i) == person.companion) continue;
        const float dx = positions_[i].X - here.X;
        const float dz = positions_[i].Y - here.Y;
        const float ahead = dx * forward.X + dz * forward.Y;
        if (ahead <= 0.0f || ahead > 1.5f) continue;
        // Only somebody actually in the way: within half a stride of the
        // line this person is walking.
        const float beside = std::fabs(dx * forward.Y - dz * forward.X);
        if (beside > 0.52f) continue;
        if (!other.waiting)
        {
            // Slow only for somebody you are *following*: they are ahead of
            // you and you are behind them. A pair walking at each other are
            // each ahead of the other, and if both gave way both would stop
            // -- two pedestrians frozen a metre apart staring is a worse
            // failure than two who brush shoulders, and the sidestep in
            // separate() is what actually parts them. Only a follower
            // yielding is an order along each direction of travel, so no
            // group of people can wait on each other in a ring.
            const Vector2 theirs(std::sin(other.facing), std::cos(other.facing));
            if (-dx * theirs.X - dz * theirs.Y > -0.15f) continue;
        }
        if (ahead <= kPersonalSpace)
            pace = std::min(pace, other.waiting ? 0.0f : other.speed * 0.55f);
        else
        {
            const float ease = (ahead - kPersonalSpace) / (1.5f - kPersonalSpace);
            const float theirsPace = other.waiting ? 0.0f : other.speed;
            pace = std::min(pace, theirsPace + (wanted - theirsPace) * ease);
        }
    }
    return std::max(0.0f, pace);
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
    // Three rows of eight -- the only sane way to look at a gait, since a
    // person who walks off before the shutter opens cannot be looked at, and
    // a pose that is wrong on one body shape is invisible in a crowd:
    //
    //   stand  one of each variant waiting, cycling through the three stances;
    //   stride one of each variant frozen at heel strike, where the feet are
    //          furthest apart and an implausible spread is at its widest --
    //          the row that would have caught the skirt splitting into two
    //          cones over a walking woman;
    //   cycle  one of each variant at successive eighths of the walk, which
    //          is what caught a knee bending forward in the fifth pass.
    build(layout, crossings, seed, kVariantCount * 3);
    lineup_ = true;
    for (int i = 0; i < static_cast<int>(people_.size()); ++i)
    {
        Pedestrian& person = people_[static_cast<std::size_t>(i)];
        const int row        = i / kVariantCount;
        const int column     = i % kVariantCount;
        person.variant       = column;
        person.waiting       = row == 0;
        person.waitTime      = static_cast<float>(i) * 0.31f;
        person.speed         = 0.0f;
        person.pinned        = true;
        person.pinnedAt      = lineupPlace(i);
        person.pinnedHeading = MathHelper::PiOver2;
        person.idleStyle     = i % 3;
        person.walkStyle     = row == 0 ? 0 : column % 3;
        person.stride        = kStrideLength;
        person.phase         = row == 0   ? static_cast<float>(i) * 0.19f
                               : row == 1 ? 0.0f
                                          : kStrideLength * static_cast<float>(column)
                                                / static_cast<float>(kVariantCount);
        person.companion     = -1;
        person.lateral       = 0.0f;
    }
}

float PedestrianSystem::cyclesWalked(const Pedestrian& person)
{
    return person.phase / std::max(person.stride, 0.5f);
}

}  // namespace CnaStreet
