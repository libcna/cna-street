// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief Lane geometry, car-following and parking.
 */
#include "CnaStreet/Scene/StreetMetrics.hpp"
#include "CnaStreet/Sim/TrafficSystem.hpp"

#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>

using namespace CnaStreet;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;

namespace M = CnaStreet::Metrics;

int main()
{
    CASE("the lanes are laid out for right-hand traffic");
    {
        TrafficSystem traffic;
        traffic.build(7u, 0, 0);
        const std::vector<Lane>& lanes = traffic.lanes();
        CHECK(lanes.size() == 4);
        for (const Lane& lane : lanes)
        {
            CHECK(lane.length > 100.0f);
            CHECK_NEAR(lane.direction.X * lane.direction.X + lane.direction.Y * lane.direction.Y,
                       1.0, 1e-4);
            CHECK(lane.stopLine > 0.0f);
            CHECK(lane.stopLine < lane.length);
        }
        // Northbound keeps to the east side, southbound to the west.
        const Lane& north = lanes[0];
        const Lane& south = lanes[1];
        CHECK(north.direction.Y > 0.5f && north.start.X > 0.0f);
        CHECK(south.direction.Y < -0.5f && south.start.X < 0.0f);
        // Every travel lane sits inside the carriageway.
        for (const Lane& lane : lanes)
            for (const float s : {0.0f, lane.length * 0.5f, lane.length})
            {
                const Vector2 at = lane.at(s);
                const bool onMain = std::fabs(lane.direction.Y) > 0.5f;
                const float across = onMain ? at.X : at.Y;
                const float half = onMain ? M::kMainCarriagewayWidth * 0.5f
                                          : M::kSideCarriagewayWidth * 0.5f;
                CHECK(std::fabs(across) < half);
            }
    }

    CASE("stop lines sit short of the junction box");
    {
        TrafficSystem traffic;
        traffic.build(7u, 0, 0);
        for (const Lane& lane : traffic.lanes())
        {
            const Vector2 at = lane.at(lane.stopLine);
            // The stop line must be outside the box the other street runs through.
            const bool onMain = std::fabs(lane.direction.Y) > 0.5f;
            if (onMain)
                CHECK(std::fabs(at.Y) > M::kSideCarriagewayWidth * 0.5f + M::kZebraDepth);
            else
                CHECK(std::fabs(at.X) > M::kMainCarriagewayWidth * 0.5f + M::kZebraDepth);
        }
    }

    CASE("a queue stops at a red light and stays behind the line");
    {
        TrafficSystem traffic;
        traffic.build(11u, 16, 0);
        TrafficSignalController::Timing timing;
        // A cycle that holds every arm at red for the whole test.
        timing.mainGreen = 0.0f;
        timing.sideGreen = 0.0f;
        timing.amber     = 0.0f;
        timing.redAmber  = 0.0f;
        timing.allRed    = 600.0f;
        TrafficSignalController signals(timing);
        // Step off the zero-length green the controller starts on, into the
        // all-red that lasts the whole test.
        signals.update(0.01f);
        CHECK(!signals.vehicleMayProceed(SignalAxis::Main));
        CHECK(!signals.vehicleMayProceed(SignalAxis::Side));

        // Long enough for every vehicle to reach the queue, including the ones
        // that were past the stop line when the clock started and have to go all
        // the way round the lane to come back to it.
        for (int i = 0; i < 24000; ++i) traffic.update(1.0f / 60.0f, signals);

        int stopped = 0;
        for (const Vehicle& vehicle : traffic.vehicles())
        {
            if (vehicle.parked) continue;
            const Lane& lane = traffic.lanes()[static_cast<std::size_t>(vehicle.lane)];
            if (vehicle.position < lane.stopLine)
            {
                // Anything still upstream of the line must have come to rest,
                // and must not have crept over it.
                CHECK_MSG(vehicle.speed < 0.35f, "vehicle still moving toward a red light");
                CHECK_MSG(vehicle.position <= lane.stopLine + 0.05f, "vehicle crossed a red light");
                ++stopped;
            }
        }
        CHECK(stopped > 0);
    }

    CASE("vehicles in a lane keep a gap and do not pass through each other");
    {
        TrafficSystem traffic;
        traffic.build(3u, 24, 0);
        TrafficSignalController signals;
        for (int i = 0; i < 4000; ++i) traffic.update(1.0f / 60.0f, signals);

        for (std::size_t a = 0; a < traffic.vehicles().size(); ++a)
            for (std::size_t b = a + 1; b < traffic.vehicles().size(); ++b)
            {
                const Vehicle& first = traffic.vehicles()[a];
                const Vehicle& second = traffic.vehicles()[b];
                if (first.parked || second.parked || first.lane != second.lane) continue;
                const float length = traffic.lanes()[static_cast<std::size_t>(first.lane)].length;
                float gap = std::fabs(first.position - second.position);
                gap = std::min(gap, length - gap);
                CHECK_MSG(gap > (first.length + second.length) * 0.5f - 0.35f,
                          "two vehicles occupy the same stretch of lane");
            }
    }

    CASE("speeds stay inside what the model allows");
    {
        TrafficSystem traffic;
        traffic.build(5u, 20, 0);
        TrafficSignalController signals;
        for (int i = 0; i < 2000; ++i)
        {
            traffic.update(1.0f / 60.0f, signals);
            for (const Vehicle& vehicle : traffic.vehicles())
            {
                if (vehicle.parked) continue;
                CHECK_MSG(vehicle.speed >= -1e-3f, "a vehicle reversed");
                CHECK_MSG(vehicle.speed <= vehicle.desiredSpeed + 0.5f,
                          "a vehicle exceeded its desired speed");
            }
        }
    }

    CASE("parked cars sit in the parking lane, spaced, and facing the right way");
    {
        TrafficSystem traffic;
        traffic.build(23u, 0, 40);
        const float bayCentre = M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f;
        int parked = 0;
        for (const Vehicle& vehicle : traffic.vehicles())
        {
            if (!vehicle.parked) continue;
            ++parked;
            CHECK_NEAR(std::fabs(vehicle.parkedAt.X), bayCentre, 0.25);
            // Facing along the street, one way on each side.
            const float heading = vehicle.parkedHeading;
            const bool northbound = std::fabs(heading) < 0.2f;
            const bool southbound = std::fabs(std::fabs(heading) - 3.14159265f) < 0.2f;
            CHECK(northbound || southbound);
            CHECK((vehicle.parkedAt.X > 0.0f) == northbound);
        }
        CHECK(parked == traffic.parkedCount());
        CHECK(parked > 20);

        // No two parked cars overlap.
        for (std::size_t a = 0; a < traffic.vehicles().size(); ++a)
            for (std::size_t b = a + 1; b < traffic.vehicles().size(); ++b)
            {
                const Vehicle& first = traffic.vehicles()[a];
                const Vehicle& second = traffic.vehicles()[b];
                if (!first.parked || !second.parked) continue;
                if (std::fabs(first.parkedAt.X - second.parkedAt.X) > 1.0f) continue;
                const float gap = std::fabs(first.parkedAt.Y - second.parkedAt.Y);
                CHECK_MSG(gap > (first.length + second.length) * 0.5f,
                          "two parked cars overlap");
            }
    }

    CASE("the same seed produces the same traffic");
    {
        TrafficSystem a, b;
        a.build(1234u, 18, 22);
        b.build(1234u, 18, 22);
        CHECK(a.vehicles().size() == b.vehicles().size());
        for (std::size_t i = 0; i < a.vehicles().size(); ++i)
        {
            CHECK(a.vehicles()[i].variant == b.vehicles()[i].variant);
            CHECK_NEAR(a.vehicles()[i].position, b.vehicles()[i].position, 1e-6);
            CHECK_NEAR(a.vehicles()[i].parkedAt.Y, b.vehicles()[i].parkedAt.Y, 1e-6);
        }

        TrafficSystem c;
        c.build(1235u, 18, 22);
        bool differs = false;
        for (std::size_t i = 0; i < a.vehicles().size() && i < c.vehicles().size(); ++i)
            differs = differs || std::fabs(a.vehicles()[i].position - c.vehicles()[i].position) > 1e-3f;
        CHECK_MSG(differs, "a different seed produced an identical street");
    }

    CASE("occupies() finds a vehicle and misses the rest of the road");
    {
        TrafficSystem traffic;
        traffic.build(9u, 0, 30);
        const Vehicle* parked = nullptr;
        for (const Vehicle& vehicle : traffic.vehicles())
            if (vehicle.parked) { parked = &vehicle; break; }
        CHECK(parked != nullptr);
        if (parked != nullptr)
        {
            CHECK(traffic.occupies(parked->parkedAt, 0.2f));
            CHECK(!traffic.occupies(Vector2(parked->parkedAt.X, parked->parkedAt.Y + 60.0f), 0.2f));
            // The footway is never occupied by a parked car.
            CHECK(!traffic.occupies(Vector2(M::kMainStreetHalfWidth - 0.5f, 40.0f), 0.3f));
        }
    }

    CASE("every variant maps to a class with sane dimensions");
    {
        for (int variant = 0; variant < TrafficSystem::kVariantCount; ++variant)
        {
            const VehicleType type = TrafficSystem::typeForVariant(variant);
            const VehicleDimensions d = VehicleFactory::dimensionsFor(type);
            CHECK(d.length > 3.5f && d.length < 7.0f);
            CHECK(d.width > 1.5f && d.width < 2.4f);
            CHECK(d.height > 1.2f && d.height < 2.8f);
            CHECK(d.wheelbase < d.length - 1.0f);
            CHECK(d.wheelRadius > 0.25f && d.wheelRadius < 0.40f);
            // The greenhouse runs front to back in order, and the windscreen is
            // ahead of the roof.
            CHECK(d.cabinRear < d.roofRear);
            CHECK(d.roofRear < d.roofFront);
            CHECK(d.roofFront < d.cabinFront);
            // The bonnet is a bonnet, not a runway.
            const float bonnet = (0.5f - d.cabinFront) * d.length;
            CHECK_MSG(bonnet > 0.6f && bonnet < 2.0f,
                      std::string("implausible bonnet length ") + std::to_string(bonnet));
        }
    }

    CASE("a vehicle's transform puts it on the road facing along its lane");
    {
        TrafficSystem traffic;
        traffic.build(2u, 8, 0);
        for (const Vehicle& vehicle : traffic.vehicles())
        {
            const Matrix world = vehicle.transform(traffic.lanes());
            CHECK_NEAR(world.M42, 0.0, 1e-5);   // wheels on the road
            const Lane& lane = traffic.lanes()[static_cast<std::size_t>(vehicle.lane)];
            // Local +Z, the nose, must point along the lane.
            CHECK_NEAR(world.M31, lane.direction.X, 1e-3);
            CHECK_NEAR(world.M33, lane.direction.Y, 1e-3);
        }
    }

    TEST_MAIN("traffic-system");
}
