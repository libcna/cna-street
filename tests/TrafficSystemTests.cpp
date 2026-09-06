// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief Lane geometry, car-following and parking.
 */
#include "CnaStreet/Scene/StreetMetrics.hpp"
#include "CnaStreet/Sim/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace CnaStreet;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

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
        // The rule, rather than four hand-written signs: every lane sits on
        // its own *right* of the street's centre line. It used to sit on its
        // left -- the lanes were laid out for left-hand traffic under a
        // comment saying right-hand -- so a street of left-hand-drive models
        // drove on the wrong side and every driver sat in the passenger seat.
        // Written through TrafficSystem::rightOf so the layout, the parking
        // bays, the signal kerbs and this check cannot drift apart again.
        for (const Lane& lane : lanes)
        {
            const Vector2 mid = lane.at(lane.length * 0.5f);
            const float along = mid.X * lane.direction.X + mid.Y * lane.direction.Y;
            const Vector2 offset(mid.X - lane.direction.X * along,
                                 mid.Y - lane.direction.Y * along);
            const Vector2 right = TrafficSystem::rightOf(lane.direction);
            CHECK_MSG(offset.X * right.X + offset.Y * right.Y > 0.5f,
                      "a travel lane sits on the wrong side of the centre line");
        }
        // And, spelled out for the one direction a reader will check by hand:
        // northbound is +Z, whose right is -X.
        const Lane& north = lanes[0];
        const Lane& south = lanes[1];
        CHECK(north.direction.Y > 0.5f && north.start.X < 0.0f);
        CHECK(south.direction.Y < -0.5f && south.start.X > 0.0f);
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
            // Facing along the street, one way on each side -- and the kerb
            // it is against is the one on its own right, because that is
            // which way round parking works where the traffic keeps right.
            const float heading = vehicle.parkedHeading;
            const bool northbound = std::fabs(heading) < 0.2f;
            const bool southbound = std::fabs(std::fabs(heading) - 3.14159265f) < 0.2f;
            CHECK(northbound || southbound);
            const Vector2 forward(std::sin(heading), std::cos(heading));
            const Vector2 right = TrafficSystem::rightOf(forward);
            CHECK_MSG(vehicle.parkedAt.X * right.X > 0.0f,
                      "a parked car is against the kerb on its left");
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

    CASE("a car is a solid: the walking camera cannot get inside one");
    {
        // The oriented box, in isolation and then over the whole fleet. A
        // vehicle used to be nothing at all to the walking camera -- the mode
        // was documented as "blocked by buildings and vehicles" and was
        // blocked by buildings.
        TrafficSystem traffic;
        traffic.build(31u, 8, 24);
        const std::vector<TrafficSystem::Solid> solids = traffic.solids();
        CHECK(solids.size() == traffic.vehicles().size());
        for (const TrafficSystem::Solid& solid : solids)
        {
            CHECK(solid.halfLength > 1.5f && solid.halfLength < 3.2f);
            CHECK(solid.halfWidth > 0.7f && solid.halfWidth < 1.3f);
            CHECK(solid.height > 1.2f && solid.height < 2.6f);
        }
        // The centre of every car is inside it; a point two car-widths to the
        // side is not; the sky over it is not; the tarmac under it is not.
        for (const TrafficSystem::Solid& solid : solids)
        {
            const Vector3 middle(solid.centre.X, solid.height * 0.5f, solid.centre.Y);
            CHECK_MSG(traffic.blocks(middle, 0.0f), "a car is not solid at its own centre");
            CHECK(!traffic.blocks(Vector3(middle.X, solid.height + 1.0f, middle.Z), 0.0f));
            CHECK(!traffic.blocks(Vector3(middle.X, -0.1f, middle.Z), 0.0f));
        }
        // Along the body and across it, so a box that is round or turned the
        // wrong way is caught: a point 1.2 m off the flank is clear, and one
        // 1.2 m along the nose is still inside a 4.3 m car.
        {
            const TrafficSystem::Solid& solid = solids.front();
            const float s = std::sin(solid.heading), c = std::cos(solid.heading);
            const auto at = [&](float across, float along) {
                return Vector3(solid.centre.X + across * c + along * s, 0.8f,
                               solid.centre.Y - across * s + along * c);
            };
            CHECK_MSG(traffic.blocks(at(0.0f, 1.2f), 0.0f), "a car is hollow along its length");
            CHECK_MSG(!traffic.blocks(at(1.6f, 0.0f), 0.0f), "a car is wider than it is");
        }
        // And out again, the way a car that has driven into somebody gives
        // them their space back: the pushed point is outside every car, and
        // the push is short.
        int pushed = 0;
        for (const TrafficSystem::Solid& solid : solids)
        {
            const Vector3 inside(solid.centre.X, 0.9f, solid.centre.Y);
            const Vector3 clear = traffic.pushOut(inside, 0.32f);
            const float moved = std::sqrt((clear.X - inside.X) * (clear.X - inside.X)
                                          + (clear.Z - inside.Z) * (clear.Z - inside.Z));
            CHECK_MSG(moved > 0.1f, "a point in the middle of a car was not pushed out");
            CHECK_MSG(moved < 3.5f, "a push out of a car threw somebody across the street");
            CHECK_MSG(!traffic.blocks(clear, 0.30f), "a pushed point is still inside a car");
            ++pushed;
        }
        CHECK(pushed == static_cast<int>(solids.size()));
        // A point in the clear is left exactly where it is: nobody is nudged
        // for standing on an empty pavement.
        const Vector3 free(0.0f, 1.0f, 300.0f);
        const Vector3 same = traffic.pushOut(free, 0.32f);
        CHECK(same.X == free.X && same.Z == free.Z);
        // An authored model's size reaches the solid.
        traffic.setVehicleSize(0, 2.00f, 2.36f);
        CHECK_NEAR(traffic.solids()[0].halfWidth, 1.0, 1e-4);
        CHECK_NEAR(traffic.solids()[0].height, 2.36, 1e-4);
        traffic.setVehicleSize(0, 0.1f, 0.1f);    // nonsense is ignored
        CHECK_NEAR(traffic.solids()[0].halfWidth, 1.0, 1e-4);
        traffic.setVehicleSize(9999, 2.0f, 2.0f); // as is an index off the end
    }

    TEST_MAIN("traffic-system");
}
