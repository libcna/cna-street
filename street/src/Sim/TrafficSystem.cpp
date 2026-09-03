// SPDX-License-Identifier: MIT
#include "CnaStreet/Sim/TrafficSystem.hpp"

#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;

namespace CnaStreet {

namespace M = Metrics;

namespace {

/// Comfortable deceleration for environmental traffic. Real emergency braking is
/// nearer 7 m/s²; this is what a driver who saw the light change would use.
constexpr float kBrake = 3.4f;
constexpr float kAccelerate = 1.9f;
/// Bumper-to-bumper gap when stopped.
constexpr float kStandingGap = 1.6f;
/// Extra gap per metre per second of speed — a following distance, not a
/// constant, which is what keeps a moving queue from concertinaing.
constexpr float kTimeGap = 0.85f;

float LaneHeading(const Vector2& direction)
{
    // Yaw about +Y such that +Z is heading 0, matching the camera's convention
    // and the way the vehicle meshes are built (nose toward +Z).
    return std::atan2(direction.X, direction.Y);
}

}  // namespace

float Lane::heading() const { return LaneHeading(direction); }

Matrix Vehicle::transform(const std::vector<Lane>& lanes) const
{
    if (parked)
        return Geometry::Place(parkedAt.X, 0.0f, parkedAt.Y, parkedHeading);
    const Lane& own = lanes[static_cast<std::size_t>(lane)];
    const Vector2 at = own.at(position);
    return Geometry::Place(at.X, 0.0f, at.Y, own.heading());
}

TrafficSystem::TrafficSystem() = default;

VehicleType TrafficSystem::typeForVariant(int variant)
{
    // Ten meshes over five classes: the mix a street actually has, with
    // hatchbacks commonest and one van.
    static const VehicleType kMix[kVariantCount] = {
        VehicleType::Hatchback, VehicleType::Hatchback, VehicleType::Hatchback,
        VehicleType::Saloon,    VehicleType::Saloon,
        VehicleType::Estate,    VehicleType::Estate,
        VehicleType::Crossover, VehicleType::Crossover,
        VehicleType::Van};
    return kMix[static_cast<std::size_t>(std::clamp(variant, 0, kVariantCount - 1))];
}

void TrafficSystem::buildLanes()
{
    lanes_.clear();

    const float mainHalf = M::kMainCarriagewayWidth * 0.5f;
    const float sideHalf = M::kSideCarriagewayWidth * 0.5f;
    // Lane centres: the travel lane sits between the parking lane and the centre
    // line, so its centre is half a lane width in from the parking lane's edge.
    const float mainLaneCentre = mainHalf - M::kParkingLaneWidth - M::kMainLaneWidth * 0.5f;
    const float sideLaneCentre = sideHalf * 0.5f;

    const float mainEnd = M::kMainStreetHalfLength;
    const float sideEnd = M::kSideStreetHalfLength;
    // Right-hand traffic: a vehicle travelling north keeps to the east side.
    // Northbound.
    lanes_.push_back(Lane{Vector2(mainLaneCentre, -mainEnd), Vector2(0.0f, 1.0f), 2.0f * mainEnd,
                          mainEnd - (M::kSideStreetHalfWidth + 1.4f + M::kZebraDepth + 1.0f),
                          mainEnd + M::kSideCarriagewayWidth * 0.5f + 1.0f, SignalAxis::Main});
    // Southbound.
    lanes_.push_back(Lane{Vector2(-mainLaneCentre, mainEnd), Vector2(0.0f, -1.0f), 2.0f * mainEnd,
                          mainEnd - (M::kSideStreetHalfWidth + 1.4f + M::kZebraDepth + 1.0f),
                          mainEnd + M::kSideCarriagewayWidth * 0.5f + 1.0f, SignalAxis::Main});
    // Eastbound keeps to the south side.
    lanes_.push_back(Lane{Vector2(-sideEnd, -sideLaneCentre), Vector2(1.0f, 0.0f), 2.0f * sideEnd,
                          sideEnd - (M::kMainStreetHalfWidth + 1.2f + M::kZebraDepth + 1.0f),
                          sideEnd + M::kMainCarriagewayWidth * 0.5f + 1.0f, SignalAxis::Side});
    // Westbound.
    lanes_.push_back(Lane{Vector2(sideEnd, sideLaneCentre), Vector2(-1.0f, 0.0f), 2.0f * sideEnd,
                          sideEnd - (M::kMainStreetHalfWidth + 1.2f + M::kZebraDepth + 1.0f),
                          sideEnd + M::kMainCarriagewayWidth * 0.5f + 1.0f, SignalAxis::Side});
}

void TrafficSystem::spawnMoving(Rng& rng, int count)
{
    if (lanes_.empty()) return;

    // Round-robin over the lanes, and along a lane on a jittered grid rather
    // than at uniform random. Random positions put two cars a metre apart often
    // enough to matter, and the car-following model cannot undo an overlap it
    // was handed: a pair spawned inside one another stays inside one another for
    // as long as anyone is watching.
    const int perLane = std::max(1, count / static_cast<int>(lanes_.size()) + 1);
    for (int i = 0; i < count; ++i)
    {
        Vehicle vehicle;
        vehicle.lane    = i % static_cast<int>(lanes_.size());
        vehicle.variant = rng.intRange(0, kVariantCount - 1);
        vehicle.type    = typeForVariant(vehicle.variant);
        vehicle.length  = VehicleFactory::dimensionsFor(vehicle.type).length;

        const float length = lanes_[static_cast<std::size_t>(vehicle.lane)].length;
        const float slot   = length / static_cast<float>(perLane);
        const int   index  = i / static_cast<int>(lanes_.size());
        vehicle.position = static_cast<float>(index) * slot
                           + rng.range(vehicle.length, std::max(vehicle.length + 0.1f, slot * 0.7f));
        vehicle.desiredSpeed = rng.range(7.0f, 10.5f);
        vehicle.speed = vehicle.desiredSpeed * rng.range(0.5f, 1.0f);
        vehicles_.push_back(vehicle);
        ++movingCount_;
    }
    // Sorted by lane then position, which is what makes "the vehicle ahead" the
    // next element rather than a search.
    std::sort(vehicles_.begin(), vehicles_.end(), [](const Vehicle& a, const Vehicle& b) {
        return a.lane != b.lane ? a.lane < b.lane : a.position < b.position;
    });
}

void TrafficSystem::spawnParked(Rng& rng, int count)
{
    const float mainHalf = M::kMainCarriagewayWidth * 0.5f;
    const float bayCentre = mainHalf - M::kParkingLaneWidth * 0.5f;
    const float from = M::kSideStreetHalfWidth + 14.0f;
    const float to   = M::kMainStreetHalfLength - 6.0f;

    // Bays down both parking lanes, both halves of the street. Some are left
    // empty: a parking lane with every space filled looks staged.
    struct Bay { float x; float z; float heading; };
    std::vector<Bay> bays;
    // Bay pitch, not bay length: the marked bay is 5.40 m but a van is 5.34 m
    // long, and two neighbours each nudged 35 cm toward each other parked
    // through one another.
    const float pitch = M::kParkingBayLength + 0.65f;
    for (const float side : {-1.0f, 1.0f})
        for (const float half : {-1.0f, 1.0f})
            for (float z = from; z < to; z += pitch)
            {
                // Parked vehicles face the direction of travel of the lane they
                // are beside, which on a right-hand-drive street means the two
                // sides face opposite ways.
                const float heading = side > 0.0f ? 0.0f : MathHelper::Pi;
                bays.push_back(Bay{side * bayCentre, half * (z + pitch * 0.5f), heading});
            }

    for (int i = 0; i < count && !bays.empty(); ++i)
    {
        const std::size_t pick = rng.index(bays.size());
        const Bay bay = bays[pick];
        bays.erase(bays.begin() + static_cast<std::ptrdiff_t>(pick));

        Vehicle vehicle;
        vehicle.parked  = true;
        vehicle.variant = rng.intRange(0, kVariantCount - 1);
        vehicle.type    = typeForVariant(vehicle.variant);
        vehicle.length  = VehicleFactory::dimensionsFor(vehicle.type).length;
        // A small lateral and angular error, because nobody parks perfectly.
        vehicle.parkedAt = Vector2(bay.x + rng.signed_(0.10f), bay.z + rng.signed_(0.18f));
        vehicle.parkedHeading = bay.heading + rng.signed_(0.035f);
        vehicles_.push_back(vehicle);
        ++parkedCount_;
    }
}

void TrafficSystem::build(std::uint32_t seed, int movingCount, int parkedCount)
{
    vehicles_.clear();
    movingCount_ = 0;
    parkedCount_ = 0;

    buildLanes();
    Rng rng = Rng::derive(seed, "traffic");
    spawnMoving(rng, movingCount);
    spawnParked(rng, parkedCount);
}

float TrafficSystem::gapAhead(const Vehicle& vehicle, std::size_t index) const
{
    float best = 1e9f;
    for (std::size_t i = 0; i < vehicles_.size(); ++i)
    {
        if (i == index) continue;
        const Vehicle& other = vehicles_[i];
        if (other.parked || other.lane != vehicle.lane) continue;
        float gap = other.position - vehicle.position;
        // Wrap: the lane is a loop, so the vehicle at the start is ahead of the
        // one at the end.
        const float length = lanes_[static_cast<std::size_t>(vehicle.lane)].length;
        if (gap < 0.0f) gap += length;
        gap -= (vehicle.length + other.length) * 0.5f;
        best = std::min(best, gap);
    }
    return best;
}

void TrafficSystem::update(float deltaSeconds, const TrafficSignalController& signals)
{
    if (deltaSeconds <= 0.0f) return;
    const float dt = std::min(deltaSeconds, 0.1f);

    for (std::size_t i = 0; i < vehicles_.size(); ++i)
    {
        Vehicle& vehicle = vehicles_[i];
        if (vehicle.parked) continue;
        const Lane& lane = lanes_[static_cast<std::size_t>(vehicle.lane)];

        // The two things that can require stopping: the vehicle ahead, and the
        // stop line when the signal is not green. Both become a *distance*, and
        // the speed follows from how hard it would have to brake to stop in it.
        float clearance = gapAhead(vehicle, i) - kStandingGap - vehicle.speed * kTimeGap;

        if (lane.stopLine > 0.0f && !signals.vehicleMayProceed(lane.axis))
        {
            const float toStopLine = lane.stopLine - vehicle.position - vehicle.length * 0.5f;
            // Only obey a stop line that is still ahead and close enough to be
            // this approach's: a vehicle that has already crossed keeps going,
            // which is what the all-red interclearance is for.
            if (toStopLine > -0.5f && toStopLine < 70.0f)
                clearance = std::min(clearance, toStopLine);
        }

        float target = vehicle.desiredSpeed;
        if (clearance < 60.0f)
        {
            // v = sqrt(2*a*d) is the fastest speed from which this distance is
            // still a comfortable stop.
            target = std::min(target, std::sqrt(std::max(0.0f, 2.0f * kBrake * clearance)));
        }

        if (target > vehicle.speed)
            vehicle.speed = std::min(target, vehicle.speed + kAccelerate * dt);
        else
            vehicle.speed = std::max(target, vehicle.speed - kBrake * 1.6f * dt);
        vehicle.speed = std::max(0.0f, vehicle.speed);

        vehicle.position += vehicle.speed * dt;
        if (vehicle.position > lane.length) vehicle.position -= lane.length;
    }
}

bool TrafficSystem::occupies(const Vector2& point, float radius) const
{
    for (const Vehicle& vehicle : vehicles_)
    {
        const Vector2 centre = vehicle.parked
                                   ? vehicle.parkedAt
                                   : lanes_[static_cast<std::size_t>(vehicle.lane)]
                                         .at(vehicle.position);
        const float dx = point.X - centre.X;
        const float dz = point.Y - centre.Y;
        const float reach = vehicle.length * 0.5f + radius;
        if (dx * dx + dz * dz < reach * reach) return true;
    }
    return false;
}

}  // namespace CnaStreet
