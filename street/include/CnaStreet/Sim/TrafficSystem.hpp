// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Props/VehicleFactory.hpp"
#include "CnaStreet/Sim/TrafficSignals.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <vector>

namespace CnaStreet {

/// One straight travel lane. The street is a crossroads of straight segments, so
/// a lane is a start point, a direction and a length; a polyline would be
/// machinery for a shape this street does not have.
struct Lane
{
    Microsoft::Xna::Framework::Vector2 start{0.0f, 0.0f};
    Microsoft::Xna::Framework::Vector2 direction{0.0f, 1.0f};
    float length = 260.0f;
    /// Distance along the lane at which the stop line sits, or a negative number
    /// where the lane does not meet the junction.
    float stopLine = -1.0f;
    /// Distance along the lane at which the junction box ends.
    float junctionExit = -1.0f;
    SignalAxis axis = SignalAxis::Main;

    [[nodiscard]] Microsoft::Xna::Framework::Vector2 at(float s) const
    {
        return Microsoft::Xna::Framework::Vector2(start.X + direction.X * s,
                                                  start.Y + direction.Y * s);
    }
    [[nodiscard]] float heading() const;
};

/// A vehicle on a lane.
struct Vehicle
{
    int   lane = 0;
    float position = 0.0f;    ///< distance along the lane
    float speed = 0.0f;       ///< m/s
    float desiredSpeed = 8.5f;
    VehicleType type = VehicleType::Hatchback;
    int   variant = 0;        ///< which built mesh and paint this one uses
    float length = 4.3f;
    bool  parked = false;
    /// Where a parked vehicle sits, and which way it points.
    Microsoft::Xna::Framework::Vector2 parkedAt{0.0f, 0.0f};
    float parkedHeading = 0.0f;

    [[nodiscard]] Microsoft::Xna::Framework::Matrix transform(const std::vector<Lane>& lanes) const;
};

/**
 * @brief Moving and parked traffic.
 *
 * A one-dimensional car-following model per lane, which is the right level of
 * detail for environmental traffic: each vehicle brakes for the one ahead and
 * for a red light, accelerates back to its desired speed when the way is clear,
 * and wraps to the start of its lane when it runs off the end. Nobody changes
 * lane, turns, or overtakes, because on this street nobody would.
 *
 * The braking model is the one thing worth being careful about. A vehicle
 * decides its target speed from the *distance to the nearest constraint* and a
 * comfortable deceleration, rather than from a fixed "stop when closer than
 * X" rule: that is what produces a queue easing to a halt at a red light
 * instead of a line of cars stopping dead one after another.
 */
class TrafficSystem
{
public:
    TrafficSystem();

    /// Lays out the lanes from the street's own dimensions and populates them.
    void build(std::uint32_t seed, int movingCount, int parkedCount);
    void update(float deltaSeconds, const TrafficSignalController& signals);

    [[nodiscard]] const std::vector<Lane>& lanes() const { return lanes_; }
    [[nodiscard]] const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    [[nodiscard]] int movingCount() const { return movingCount_; }
    [[nodiscard]] int parkedCount() const { return parkedCount_; }

    /// Whether a point is inside any vehicle's footprint, so pedestrians and the
    /// walking camera do not walk through one.
    [[nodiscard]] bool occupies(const Microsoft::Xna::Framework::Vector2& point,
                                float radius) const;

    /// How many distinct vehicle meshes the scene has to build.
    static constexpr int kVariantCount = 10;
    [[nodiscard]] static VehicleType typeForVariant(int variant);

private:
    void buildLanes();
    void spawnMoving(Rng& rng, int count);
    void spawnParked(Rng& rng, int count);
    [[nodiscard]] float gapAhead(const Vehicle& vehicle, std::size_t index) const;

    std::vector<Lane>    lanes_;
    std::vector<Vehicle> vehicles_;
    int movingCount_ = 0;
    int parkedCount_ = 0;
};

}  // namespace CnaStreet
