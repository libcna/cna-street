// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace CnaStreet {

/// What one signal head is showing.
enum class SignalAspect
{
    Red,
    RedAmber,   ///< the "about to go green" combination used across much of Europe
    Green,
    Amber
};

/// Which arm of the junction a head faces.
enum class SignalAxis
{
    Main,   ///< the north–south street
    Side    ///< the east–west street
};

/**
 * @brief The junction's signal controller.
 *
 * A fixed-time four-stage cycle, which is what a small isolated junction like
 * this actually runs: main-street green, main amber, all-red, side-street green,
 * side amber, all-red, repeat. The all-red interclearance is not decoration —
 * it is the reason vehicles from the two arms are never both moving through the
 * box, and the traffic simulation depends on it being there.
 *
 * Pedestrian phases run *against* the vehicle phase on the same axis: people
 * cross the main street while the side street has the green, and the pedestrian
 * green ends a clearance interval before the vehicle green returns.
 *
 * Deliberately free of any rendering or scene dependency, so the whole cycle can
 * be stepped and asserted in a unit test.
 */
class TrafficSignalController
{
public:
    struct Timing
    {
        float mainGreen  = 15.0f;
        float sideGreen  = 9.0f;
        float amber      = 3.0f;
        float redAmber   = 1.4f;
        float allRed     = 2.0f;
        /// How long before the vehicle green returns the pedestrian light stops.
        float pedestrianClearance = 4.0f;
    };

    TrafficSignalController();
    explicit TrafficSignalController(const Timing& timing);

    void setTiming(const Timing& timing);
    [[nodiscard]] const Timing& timing() const { return timing_; }

    void reset();
    void update(float deltaSeconds);

    [[nodiscard]] SignalAspect vehicleAspect(SignalAxis axis) const;
    /// Whether a vehicle on this axis may enter the junction. Amber counts as
    /// "stop if you can", which the traffic model resolves by distance.
    [[nodiscard]] bool vehicleMayProceed(SignalAxis axis) const;
    /// Whether people crossing the street *of this axis* have a green man.
    /// Crossing the main street happens while the side street is running.
    [[nodiscard]] bool pedestrianGreen(SignalAxis crossedAxis) const;

    /// Seconds since the start of the current cycle, and the cycle's length.
    [[nodiscard]] float phaseTime() const { return phaseTime_; }
    [[nodiscard]] float cycleLength() const;
    [[nodiscard]] const char* stageName() const;

private:
    enum class Stage
    {
        MainGreen,
        MainAmber,
        AllRedAfterMain,
        SideRedAmber,
        SideGreen,
        SideAmber,
        AllRedAfterSide,
        MainRedAmber
    };

    [[nodiscard]] float stageLength(Stage stage) const;
    void advance();

    Timing timing_;
    Stage  stage_ = Stage::MainGreen;
    float  phaseTime_ = 0.0f;
};

}  // namespace CnaStreet
