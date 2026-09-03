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
#include <set>
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

    TEST_MAIN("pedestrians");
}
