// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The junction controller.
 *
 * The property that matters is a safety property, and it is the one a
 * screenshot can never check: at no point in the cycle may both arms be
 * invited into the box, and at no point may a pedestrian be sent across a
 * street whose traffic is moving. Both are asserted by stepping the whole cycle
 * at a fine time step and checking every sample.
 */
#include "CnaStreet/Sim/TrafficSignals.hpp"

#include "TestSupport.hpp"

using namespace CnaStreet;

namespace {

bool green(SignalAspect aspect) { return aspect == SignalAspect::Green; }

}  // namespace

int main()
{
    CASE("a fresh controller starts on the main street");
    {
        TrafficSignalController signals;
        CHECK(signals.vehicleAspect(SignalAxis::Main) == SignalAspect::Green);
        CHECK(signals.vehicleAspect(SignalAxis::Side) == SignalAspect::Red);
        CHECK(signals.vehicleMayProceed(SignalAxis::Main));
        CHECK(!signals.vehicleMayProceed(SignalAxis::Side));
    }

    CASE("the two arms are never green together, anywhere in the cycle");
    {
        TrafficSignalController signals;
        const float step = 0.02f;
        const int steps = static_cast<int>(signals.cycleLength() * 3.0f / step);
        int mainGreenSamples = 0;
        int sideGreenSamples = 0;
        for (int i = 0; i < steps; ++i)
        {
            signals.update(step);
            const bool m = green(signals.vehicleAspect(SignalAxis::Main));
            const bool s = green(signals.vehicleAspect(SignalAxis::Side));
            CHECK_MSG(!(m && s), "both arms green at t=" + std::to_string(i * step));
            mainGreenSamples += m ? 1 : 0;
            sideGreenSamples += s ? 1 : 0;
        }
        CHECK(mainGreenSamples > 0);
        CHECK(sideGreenSamples > 0);
        // The main street gets the longer green; that is the whole point of
        // calling it the main street.
        CHECK(mainGreenSamples > sideGreenSamples);
    }

    CASE("a pedestrian is never sent across a street whose traffic is moving");
    {
        TrafficSignalController signals;
        const float step = 0.02f;
        const int steps = static_cast<int>(signals.cycleLength() * 3.0f / step);
        for (int i = 0; i < steps; ++i)
        {
            signals.update(step);
            for (const SignalAxis axis : {SignalAxis::Main, SignalAxis::Side})
                if (signals.pedestrianGreen(axis))
                    CHECK_MSG(!signals.vehicleMayProceed(axis),
                              "green man across a street with a running vehicle phase");
        }
    }

    CASE("both crossings get a green man every cycle");
    {
        TrafficSignalController signals;
        const float step = 0.05f;
        bool sawMain = false, sawSide = false;
        for (int i = 0; i < static_cast<int>(signals.cycleLength() / step) + 4; ++i)
        {
            signals.update(step);
            sawMain = sawMain || signals.pedestrianGreen(SignalAxis::Main);
            sawSide = sawSide || signals.pedestrianGreen(SignalAxis::Side);
        }
        CHECK(sawMain);
        CHECK(sawSide);
    }

    CASE("red-amber only ever precedes green");
    {
        TrafficSignalController signals;
        const float step = 0.05f;
        SignalAspect previousMain = signals.vehicleAspect(SignalAxis::Main);
        for (int i = 0; i < 4000; ++i)
        {
            signals.update(step);
            const SignalAspect now = signals.vehicleAspect(SignalAxis::Main);
            if (previousMain == SignalAspect::RedAmber && now != SignalAspect::RedAmber)
                CHECK(now == SignalAspect::Green);
            if (previousMain == SignalAspect::Green && now != SignalAspect::Green)
                CHECK(now == SignalAspect::Amber);
            previousMain = now;
        }
    }

    CASE("a frame longer than a stage cannot stall the junction");
    {
        // The bug this guards against: an update that adds the delta once and
        // advances one stage leaves the controller stuck mid-cycle after a
        // stall, and every vehicle on the street waits at a light that never
        // changes. One 60-second frame must land somewhere sensible.
        TrafficSignalController fast;
        TrafficSignalController slow;
        for (int i = 0; i < 6000; ++i) fast.update(0.01f);
        slow.update(60.0f);
        CHECK(std::string(slow.stageName()) == std::string(fast.stageName()));
        CHECK_NEAR(slow.phaseTime(), fast.phaseTime(), 0.02);
    }

    CASE("timing is honoured");
    {
        TrafficSignalController::Timing timing;
        timing.mainGreen = 4.0f;
        timing.sideGreen = 4.0f;
        timing.amber     = 1.0f;
        timing.redAmber  = 1.0f;
        timing.allRed    = 1.0f;
        TrafficSignalController signals(timing);
        CHECK_NEAR(signals.cycleLength(), 4.0 + 1.0 + 1.0 + 1.0 + 4.0 + 1.0 + 1.0 + 1.0, 1e-4);

        // Four seconds in, the main street should still be green; a moment later
        // it should not be.
        signals.update(3.9f);
        CHECK(signals.vehicleAspect(SignalAxis::Main) == SignalAspect::Green);
        signals.update(0.2f);
        CHECK(signals.vehicleAspect(SignalAxis::Main) == SignalAspect::Amber);
    }

    CASE("reset returns the controller to the start of the cycle");
    {
        TrafficSignalController signals;
        signals.update(7.3f);
        signals.reset();
        CHECK_NEAR(signals.phaseTime(), 0.0, 1e-6);
        CHECK(signals.vehicleAspect(SignalAxis::Main) == SignalAspect::Green);
    }

    TEST_MAIN("traffic-signals");
}
