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
#include <vector>

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
            // The greenhouse runs front to back in order: the bottom of the
            // backlight is behind its top, which is behind the top of the
            // windscreen, which is behind its base.
            CHECK(d.backlightBaseZ < d.backlightTopZ);
            CHECK(d.backlightTopZ < d.screenTopZ);
            CHECK(d.screenTopZ < d.screenBaseZ);
            // The side glass lies inside the pillars it is bounded by.
            CHECK(d.dloRearZ >= d.backlightBaseZ - 0.05f);
            CHECK(d.dloFrontZ <= d.screenBaseZ + 0.05f);
            // The axles are inside the vehicle, and the overhangs are overhangs.
            CHECK(d.rearAxleZ() > d.rearZ() && d.frontAxleZ() < d.frontZ());
            CHECK(d.frontZ() - d.frontAxleZ() > 0.4f);
            // The arch clears the wheel, and does not swallow it.
            CHECK(d.archRadius > d.wheelRadius + 0.02f);
            CHECK(d.archRadius < d.wheelRadius + 0.14f);
            // The bonnet is a bonnet, not a runway.
            const float bonnet = d.frontZ() - d.screenBaseZ;
            CHECK_MSG(bonnet > 0.6f && bonnet < 2.2f,
                      std::string("implausible bonnet length ") + std::to_string(bonnet));
            // The roof is above the shoulder is above the rocker, everywhere.
            for (float z = d.rearZ() + 0.05f; z < d.frontZ() - 0.05f; z += 0.10f)
            {
                CHECK_MSG(d.roof.at(z) > d.belt.at(z) + 0.02f,
                          std::string("roof below the beltline at z=") + std::to_string(z));
                CHECK_MSG(d.belt.at(z) > d.rocker.at(z) + 0.10f,
                          std::string("beltline below the rocker at z=") + std::to_string(z));
                CHECK_MSG(d.halfWidth.at(z) > d.topHalf.at(z),
                          std::string("no tumblehome at z=") + std::to_string(z));
                CHECK_MSG(d.halfWidth.at(z) <= d.width * 0.5f + 1e-4f,
                          std::string("wider than the vehicle at z=") + std::to_string(z));
                CHECK_MSG(d.roof.at(z) <= d.height + 1e-3f,
                          std::string("taller than the vehicle at z=") + std::to_string(z));
            }
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

    CASE("the odometer counts every metre driven and never wraps; a length can be set");
    {
        TrafficSystem traffic;
        traffic.build(9u, 12, 8);
        TrafficSignalController signals;
        std::vector<float> last(traffic.vehicles().size(), 0.0f);
        for (int i = 0; i < 2400; ++i)
        {
            traffic.update(1.0f / 60.0f, signals);
            for (std::size_t v = 0; v < traffic.vehicles().size(); ++v)
            {
                const Vehicle& vehicle = traffic.vehicles()[v];
                CHECK(vehicle.odometer >= last[v] - 1e-5f);
                last[v] = vehicle.odometer;
                if (vehicle.parked) CHECK(vehicle.odometer == 0.0f);
            }
        }
        bool someoneDrove = false;
        for (const Vehicle& vehicle : traffic.vehicles())
            if (!vehicle.parked && vehicle.odometer > 50.0f) someoneDrove = true;
        CHECK(someoneDrove);
        // An authored van drawn over a loft tells the simulation its length.
        traffic.setVehicleLength(0, 5.91f);
        CHECK_NEAR(traffic.vehicles()[0].length, 5.91, 1e-4);
        traffic.setVehicleLength(0, 0.5f);   // nonsense is ignored
        CHECK_NEAR(traffic.vehicles()[0].length, 5.91, 1e-4);
        traffic.setVehicleLength(9999, 4.0f); // as is an index off the end
    }

    TEST_MAIN("traffic-system");
}
