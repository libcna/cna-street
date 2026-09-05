// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The walk graph and the people on it.
 */
#include "CnaStreet/Props/RoadBuilder.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"
#include "CnaStreet/Sim/PedestrianSystem.hpp"

#include "TestSupport.hpp"

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace CnaStreet;
using Microsoft::Xna::Framework::Vector2;

namespace M = CnaStreet::Metrics;

namespace {

/// The crossings the road builder produces, without building any geometry: the
/// pedestrian graph only needs where they are.
std::vector<Crossing> crossingsFor()
{
    const float mainSetback = M::kSideStreetHalfWidth + 1.4f + M::kZebraDepth * 0.5f;
    const float sideSetback = M::kMainStreetHalfWidth + 1.2f + M::kZebraDepth * 0.5f;
    const float mainHalf = M::kMainCarriagewayWidth * 0.5f;
    const float sideHalf = M::kSideCarriagewayWidth * 0.5f;
    return {
        Crossing{Vector2(0.0f, mainSetback), Vector2(1.0f, 0.0f), mainHalf,
                 M::kZebraDepth * 0.5f, true},
        Crossing{Vector2(0.0f, -mainSetback), Vector2(1.0f, 0.0f), mainHalf,
                 M::kZebraDepth * 0.5f, true},
        Crossing{Vector2(sideSetback, 0.0f), Vector2(0.0f, 1.0f), sideHalf,
                 M::kZebraDepth * 0.5f, false},
        Crossing{Vector2(-sideSetback, 0.0f), Vector2(0.0f, 1.0f), sideHalf,
                 M::kZebraDepth * 0.5f, false},
    };
}

}  // namespace

int main()
{
    CityLayout layout;
    layout.generate(20260903u);
    const std::vector<Crossing> crossings = crossingsFor();

    CASE("the graph is connected in both directions and every edge has length");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 4u, 0);
        CHECK(people.nodes().size() >= 8);
        CHECK(people.edges().size() >= 8);
        for (const WalkEdge& edge : people.edges())
        {
            CHECK(edge.from >= 0 && edge.from < static_cast<int>(people.nodes().size()));
            CHECK(edge.to >= 0 && edge.to < static_cast<int>(people.nodes().size()));
            CHECK(edge.from != edge.to);
            CHECK(edge.length > 0.05f);
            const Vector2 a = people.nodes()[static_cast<std::size_t>(edge.from)].position;
            const Vector2 b = people.nodes()[static_cast<std::size_t>(edge.to)].position;
            const float dx = b.X - a.X, dz = b.Y - a.Y;
            CHECK_NEAR(edge.length, std::sqrt(dx * dx + dz * dz), 1e-3);
        }
        // Every node must be reachable: a walk graph with an island in it means
        // pedestrians accumulate somewhere and the rest of the street empties.
        std::set<int> seen{0};
        bool grew = true;
        while (grew)
        {
            grew = false;
            for (const WalkEdge& edge : people.edges())
            {
                if (seen.count(edge.from) && !seen.count(edge.to)) { seen.insert(edge.to); grew = true; }
                if (seen.count(edge.to) && !seen.count(edge.from)) { seen.insert(edge.from); grew = true; }
            }
        }
        CHECK_MSG(seen.size() == people.nodes().size(), "the walk graph has an unreachable island");
    }

    CASE("crossings are marked, and they cross a carriageway");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 4u, 0);
        int crossingEdges = 0;
        for (const WalkEdge& edge : people.edges())
        {
            if (!edge.crossing) continue;
            ++crossingEdges;
            const Vector2 a = people.nodes()[static_cast<std::size_t>(edge.from)].position;
            const Vector2 b = people.nodes()[static_cast<std::size_t>(edge.to)].position;
            if (edge.crossedAxis == SignalAxis::Main)
            {
                // Crossing the main street means going from one side of it to
                // the other.
                CHECK((a.X < 0.0f) != (b.X < 0.0f));
                CHECK(edge.length > M::kMainCarriagewayWidth * 0.9f);
            }
            else
            {
                CHECK((a.Y < 0.0f) != (b.Y < 0.0f));
                CHECK(edge.length > M::kSideCarriagewayWidth * 0.9f);
            }
        }
        CHECK(crossingEdges >= 4);
    }

    CASE("people stay on the graph and inside the district");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 12u, 40);
        CHECK(people.people().size() == 40);
        TrafficSignalController signals;
        for (int i = 0; i < 6000; ++i)
        {
            people.update(1.0f / 60.0f, signals);
            for (const Pedestrian& person : people.people())
            {
                CHECK(person.edge >= 0 && person.edge < static_cast<int>(people.edges().size()));
                const WalkEdge& edge = people.edges()[static_cast<std::size_t>(person.edge)];
                CHECK(person.distance >= -1e-3f);
                CHECK(person.distance <= edge.length + 1e-3f);
                const Vector2 at = person.position(people.nodes(), people.edges());
                CHECK(std::fabs(at.X) < M::kMainStreetHalfLength + 5.0f);
                CHECK(std::fabs(at.Y) < M::kMainStreetHalfLength + 5.0f);
            }
        }
    }

    CASE("nobody steps into a crossing against a red man");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 31u, 60);
        TrafficSignalController signals;
        // The rule is about *entering*, not about being on the crossing: someone
        // who set off on green may finish on red, and must, or the junction
        // fills up with people stranded mid-carriageway. So this watches for the
        // moment a person's edge changes to a crossing.
        std::vector<int> previousEdge;
        for (const Pedestrian& person : people.people()) previousEdge.push_back(person.edge);

        int entries = 0;
        for (int i = 0; i < 9000; ++i)
        {
            signals.update(1.0f / 60.0f);
            people.update(1.0f / 60.0f, signals);
            for (std::size_t p = 0; p < people.people().size(); ++p)
            {
                const Pedestrian& person = people.people()[p];
                if (person.edge == previousEdge[p]) continue;
                previousEdge[p] = person.edge;
                const WalkEdge& edge = people.edges()[static_cast<std::size_t>(person.edge)];
                if (!edge.crossing) continue;
                ++entries;
                // Either the man is green, or they are standing at the kerb on
                // the first millimetre of the crossing waiting for it.
                CHECK_MSG(signals.pedestrianGreen(edge.crossedAxis) || person.waiting,
                          "a pedestrian stepped out against a red man");
                if (!signals.pedestrianGreen(edge.crossedAxis))
                    CHECK_MSG(person.distance < 0.01f, "a waiting pedestrian is already in the road");
            }
        }
        CHECK_MSG(entries > 10, "nobody used a crossing at all");
    }

    CASE("people wait at the kerb rather than piling up in the road");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 77u, 50);
        TrafficSignalController signals;
        int maxWaiting = 0;
        for (int i = 0; i < 9000; ++i)
        {
            signals.update(1.0f / 60.0f);
            people.update(1.0f / 60.0f, signals);
            maxWaiting = std::max(maxWaiting, people.waitingCount());
            for (const Pedestrian& person : people.people())
                if (person.waiting)
                {
                    const WalkEdge& edge = people.edges()[static_cast<std::size_t>(person.edge)];
                    CHECK_MSG(!edge.crossing || person.distance < 0.2f,
                              "someone is waiting in the middle of the road");
                }
        }
        CHECK_MSG(maxWaiting > 0, "nobody ever waited at a crossing");
    }

    CASE("the walk cycle is driven by distance, not by the clock");
    {
        // The whole reason the animation clock is the distance walked: a cycle
        // advanced by wall-clock time slides the feet the moment two people
        // walk at different speeds, and they do.
        Pedestrian slow, quick;
        slow.phase  = 2.84f;
        quick.phase = 2.84f;
        CHECK_NEAR(PedestrianSystem::cyclesWalked(slow), 2.0, 1e-5);
        CHECK_NEAR(PedestrianSystem::cyclesWalked(quick), 2.0, 1e-5);
        quick.phase = 1.42f;
        CHECK_NEAR(PedestrianSystem::cyclesWalked(quick), 1.0, 1e-5);
        // Monotone, so the clip never runs backwards.
        float previous = -1.0f;
        for (int i = 0; i < 100; ++i)
        {
            Pedestrian person;
            person.phase = static_cast<float>(i) * 0.07f;
            const float cycles = PedestrianSystem::cyclesWalked(person);
            CHECK(cycles >= previous);
            previous = cycles;
        }
    }

    CASE("the same seed produces the same people");
    {
        PedestrianSystem a, b;
        a.build(layout, crossings, 808u, 30);
        b.build(layout, crossings, 808u, 30);
        CHECK(a.people().size() == b.people().size());
        for (std::size_t i = 0; i < a.people().size(); ++i)
        {
            CHECK(a.people()[i].edge == b.people()[i].edge);
            CHECK(a.people()[i].variant == b.people()[i].variant);
            CHECK_NEAR(a.people()[i].height, b.people()[i].height, 1e-6);
        }
    }

    CASE("everyone is adult-sized and walks at a walking pace");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 55u, 60);
        for (const Pedestrian& person : people.people())
        {
            CHECK(person.height >= M::kPersonHeightMin - 1e-3f);
            CHECK(person.height <= M::kPersonHeightMax + 1e-3f);
            CHECK(person.speed > 0.7f && person.speed < 2.2f);
            CHECK(person.variant >= 0 && person.variant < PedestrianSystem::kVariantCount);
        }
    }

    CASE("everyone has a gait, a stance, a stride for their height and a place on the footway");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 12u, 60);
        for (const Pedestrian& person : people.people())
        {
            CHECK(person.walkStyle >= 0 && person.walkStyle <= 2);
            CHECK(person.idleStyle >= 0 && person.idleStyle <= 2);
            CHECK(person.stride > 1.05f && person.stride < 2.0f);
            CHECK(person.lateral >= -1.06f && person.lateral <= 1.16f);
            CHECK(std::isfinite(person.facing));
        }
        // A taller person of the same gait takes a longer stride.
        for (const Pedestrian& a : people.people())
            for (const Pedestrian& b : people.people())
                if (a.walkStyle == b.walkStyle && a.height > b.height + 0.05f)
                    CHECK(a.stride > b.stride);
        // And the crowd is not all one gait.
        std::set<int> gaits;
        for (const Pedestrian& person : people.people()) gaits.insert(person.walkStyle);
        CHECK(gaits.size() == 3);
    }

    CASE("a companion keeps to its leader, beside it, and waits with it");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 12u, 60);
        TrafficSignalController signals;
        int companions = 0;
        for (int i = 0; i < 4000; ++i)
        {
            people.update(1.0f / 60.0f, signals);
            for (const Pedestrian& person : people.people())
            {
                if (person.companion < 0) continue;
                const Pedestrian& leader =
                    people.people()[static_cast<std::size_t>(person.companion)];
                CHECK(person.edge == leader.edge);
                CHECK(person.reversed == leader.reversed);
                CHECK(person.waiting == leader.waiting);
                const Vector2 a = person.position(people.nodes(), people.edges());
                const Vector2 b = leader.position(people.nodes(), people.edges());
                const float dx = a.X - b.X, dz = a.Y - b.Y;
                const float apart = std::sqrt(dx * dx + dz * dz);
                // Walking, a companion is a step behind and to the side.
                // Waiting, the pair is somewhere in the cluster at the kerb
                // and takes whichever two places are free -- they try to be
                // next to each other, and on a busy kerb they are not always
                // able to be.
                CHECK_MSG(apart < (person.waiting ? 2.8f : 1.6f),
                          "a companion has lost its leader: " + std::to_string(apart) + " m");
                CHECK_MSG(apart > 0.40f, "a companion is walking inside its leader");
                CHECK(person.lateral >= -1.06f && person.lateral <= 1.16f);
                if (i == 0) ++companions;
            }
        }
        CHECK(companions >= 6);
    }

    CASE("a body turns toward the way it is going, and gets there");
    {
        PedestrianSystem people;
        people.build(layout, crossings, 12u, 60);
        TrafficSignalController signals;
        for (int i = 0; i < 3000; ++i) people.update(1.0f / 60.0f, signals);
        int turning = 0;
        for (const Pedestrian& person : people.people())
        {
            const float target = person.heading(people.nodes(), people.edges());
            const float off = std::fabs(std::remainder(person.facing - target, 6.2831853f));
            if (off > 0.05f) ++turning;
            // Half a second turns a right angle, so nobody is ever more than
            // one node's worth of turn behind.
            CHECK(off < 3.2f);
        }
        // Most people have been on their edge for longer than a turn takes.
        CHECK(turning < static_cast<int>(people.people().size()) / 4);
    }

    CASE("the lineup stands eight, strides eight and cycles eight");
    {
        PedestrianSystem people;
        people.buildLineup(layout, crossings, 12u);
        CHECK(people.people().size() == 24);
        for (std::size_t i = 0; i < people.people().size(); ++i)
        {
            const Pedestrian& person = people.people()[i];
            CHECK(person.pinned);
            CHECK(person.waiting == (i < 8));
            if (i >= 8) CHECK(person.phase >= 0.0f && person.phase < person.stride);
        }
        // The striding row is all at heel strike, where the feet are furthest
        // apart: the pose an implausible stance shows in.
        for (std::size_t i = 8; i < 16; ++i) CHECK(people.people()[i].phase == 0.0f);
    }

    CASE("a queue at the kerb is a queue, not a heap");
    {
        // The places themselves: fifteen of them, none within a personal
        // space of another, none more than the footway is deep behind the
        // walking line.
        std::vector<Vector2> places;
        for (int slot = 0; slot < 15; ++slot)
        {
            float back = 0.0f, across = 0.0f;
            Pedestrian::queuePlace(slot, back, across);
            CHECK_MSG(back > 0.0f && back < 1.60f, "a waiting place is off the footway");
            CHECK(std::fabs(across) < 1.90f);
            places.emplace_back(across, back);
        }
        for (std::size_t i = 0; i < places.size(); ++i)
            for (std::size_t j = i + 1; j < places.size(); ++j)
            {
                const float dx = places[i].X - places[j].X;
                const float dz = places[i].Y - places[j].Y;
                CHECK_MSG(std::sqrt(dx * dx + dz * dz) >= PedestrianSystem::kPersonalSpace,
                          "two waiting places are inside one another");
            }
    }

    CASE("a crowd arriving at one crossing together does not stand in one another");
    {
        // The stress test: a hundred and twenty people on the graph, run for
        // four minutes of signal cycles, and at every step every pair of
        // people is checked for sharing a body volume. Before the kerb slots
        // existed, everybody waiting for a green man stood at distance zero
        // on the crossing edge and a dozen of them occupied one square metre.
        PedestrianSystem people;
        people.build(layout, crossings, 77u, 120);
        TrafficSignalController signals;
        signals.reset();
        int worstAtOneKerb = 0;
        float closest = 1e9f;
        float closestWaiting = 1e9f;
        long long pairs = 0, brushes = 0, overlaps = 0;
        for (int step = 0; step < 7200; ++step)
        {
            signals.update(1.0f / 30.0f);
            people.update(1.0f / 30.0f, signals);
            if (step % 15 != 0) continue;
            const std::vector<Pedestrian>& crowd = people.people();
            std::vector<Vector2> at;
            at.reserve(crowd.size());
            for (const Pedestrian& person : crowd)
                at.push_back(person.position(people.nodes(), people.edges()));
            for (std::size_t i = 0; i < crowd.size(); ++i)
                for (std::size_t j = i + 1; j < crowd.size(); ++j)
                {
                    const float dx = at[i].X - at[j].X;
                    const float dz = at[i].Y - at[j].Y;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    closest = std::min(closest, d);
                    // A shoulder is 0.44 m across. Two people nearer than
                    // that centre to centre are inside one another.
                    ++pairs;
                    if (d < 0.30f) ++brushes;
                    if (d < 0.20f) ++overlaps;
                }
            // How many are queued at the busiest kerb.
            std::map<std::pair<int, bool>, int> perKerb;
            for (std::size_t i = 0; i < crowd.size(); ++i)
            {
                if (!crowd[i].waiting) continue;
                ++perKerb[{crowd[i].edge, crowd[i].reversed}];
                // Two people standing at a kerb have all the time in the
                // world to be somewhere else: they get a whole shoulder
                // between them, not a graze.
                for (std::size_t j = i + 1; j < crowd.size(); ++j)
                {
                    if (!crowd[j].waiting) continue;
                    const float dx = at[i].X - at[j].X;
                    const float dz = at[i].Y - at[j].Y;
                    closestWaiting = std::min(closestWaiting, std::sqrt(dx * dx + dz * dz));
                }
            }
            for (const auto& entry : perKerb)
                worstAtOneKerb = std::max(worstAtOneKerb, entry.second);
        }
        NOTE("the busiest kerb held " + std::to_string(worstAtOneKerb)
             + " people; the closest two waiting came was " + std::to_string(closestWaiting)
             + " m, the closest two of anybody " + std::to_string(closest) + " m; "
             + std::to_string(brushes) + " of " + std::to_string(pairs)
             + " sampled pairs passed inside 0.30 m and " + std::to_string(overlaps)
             + " inside 0.20 m");
        CHECK_MSG(worstAtOneKerb >= 4, "the stress test never crowded a crossing");
        // Standing still is where a crowd has all the time in the world to be
        // somewhere else, and where the failure was reported: four people in
        // one body volume at a kerb. A whole shoulder between them, always.
        CHECK_MSG(closestWaiting > 0.55f,
                  "two people waiting at a kerb stood " + std::to_string(closestWaiting)
                      + " m apart");
        // Walking is different: two people passing head on at 0.35 m brush
        // shoulders, which is what people do, and a rule that forbade it
        // would make the footway read as a set of tramlines. What must not
        // happen is one walking *through* another. A body is 0.30 m deep, so
        // 0.20 m centre to centre is an intersection.
        // What is left is a person entering a footway at the same instant as
        // somebody already on it, at a node where the two of them have the
        // width of one footway between them: it lasts a few frames while the
        // sidestep works, and it is two thousandths of one per cent of the
        // pairs sampled. Before any of this it was a permanent state -- a
        // whole queue in one square metre. The bar is set an order of
        // magnitude above what the crowd does now, so it catches a return to
        // that and not the noise.
        CHECK_MSG(overlaps * 5000 < pairs,
                  std::to_string(overlaps) + " of " + std::to_string(pairs)
                      + " sampled pairs were inside one another");
        CHECK_MSG(brushes * 2000 < pairs,
                  "brushes are not rare: " + std::to_string(brushes) + " of "
                      + std::to_string(pairs) + " sampled pairs passed inside 0.30 m");
    }

    TEST_MAIN("pedestrians");
}
