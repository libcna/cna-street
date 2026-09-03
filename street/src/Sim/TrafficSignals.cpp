// SPDX-License-Identifier: MIT
#include "CnaStreet/Sim/TrafficSignals.hpp"

#include <algorithm>

namespace CnaStreet {

TrafficSignalController::TrafficSignalController() = default;

TrafficSignalController::TrafficSignalController(const Timing& timing) : timing_(timing) {}

void TrafficSignalController::setTiming(const Timing& timing)
{
    timing_ = timing;
    // Clamp rather than reject: a configuration file with a zero amber should
    // produce a junction that still works, not a launch failure.
    timing_.mainGreen = std::max(2.0f, timing_.mainGreen);
    timing_.sideGreen = std::max(2.0f, timing_.sideGreen);
    timing_.amber     = std::max(0.5f, timing_.amber);
    timing_.redAmber  = std::max(0.0f, timing_.redAmber);
    timing_.allRed    = std::max(0.5f, timing_.allRed);
    timing_.pedestrianClearance = std::max(0.0f, timing_.pedestrianClearance);
}

void TrafficSignalController::reset()
{
    stage_ = Stage::MainGreen;
    phaseTime_ = 0.0f;
}

float TrafficSignalController::stageLength(Stage stage) const
{
    switch (stage)
    {
        case Stage::MainGreen:       return timing_.mainGreen;
        case Stage::MainAmber:       return timing_.amber;
        case Stage::AllRedAfterMain: return timing_.allRed;
        case Stage::SideRedAmber:    return timing_.redAmber;
        case Stage::SideGreen:       return timing_.sideGreen;
        case Stage::SideAmber:       return timing_.amber;
        case Stage::AllRedAfterSide: return timing_.allRed;
        case Stage::MainRedAmber:    return timing_.redAmber;
    }
    return 1.0f;
}

float TrafficSignalController::cycleLength() const
{
    return timing_.mainGreen + timing_.sideGreen + 2.0f * timing_.amber + 2.0f * timing_.redAmber
           + 2.0f * timing_.allRed;
}

void TrafficSignalController::advance()
{
    switch (stage_)
    {
        case Stage::MainGreen:       stage_ = Stage::MainAmber;       break;
        case Stage::MainAmber:       stage_ = Stage::AllRedAfterMain; break;
        case Stage::AllRedAfterMain: stage_ = Stage::SideRedAmber;    break;
        case Stage::SideRedAmber:    stage_ = Stage::SideGreen;       break;
        case Stage::SideGreen:       stage_ = Stage::SideAmber;       break;
        case Stage::SideAmber:       stage_ = Stage::AllRedAfterSide; break;
        case Stage::AllRedAfterSide: stage_ = Stage::MainRedAmber;    break;
        case Stage::MainRedAmber:    stage_ = Stage::MainGreen;       break;
    }
}

void TrafficSignalController::update(float deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return;
    phaseTime_ += deltaSeconds;
    // A loop rather than an if: a long frame -- a stall while assets load -- must
    // not leave the junction stuck showing amber for two seconds of wall clock.
    int guard = 0;
    while (phaseTime_ >= stageLength(stage_) && guard++ < 64)
    {
        phaseTime_ -= stageLength(stage_);
        advance();
    }
}

SignalAspect TrafficSignalController::vehicleAspect(SignalAxis axis) const
{
    const bool main = axis == SignalAxis::Main;
    switch (stage_)
    {
        case Stage::MainGreen:       return main ? SignalAspect::Green    : SignalAspect::Red;
        case Stage::MainAmber:       return main ? SignalAspect::Amber    : SignalAspect::Red;
        case Stage::AllRedAfterMain: return SignalAspect::Red;
        case Stage::SideRedAmber:    return main ? SignalAspect::Red      : SignalAspect::RedAmber;
        case Stage::SideGreen:       return main ? SignalAspect::Red      : SignalAspect::Green;
        case Stage::SideAmber:       return main ? SignalAspect::Red      : SignalAspect::Amber;
        case Stage::AllRedAfterSide: return SignalAspect::Red;
        case Stage::MainRedAmber:    return main ? SignalAspect::RedAmber : SignalAspect::Red;
    }
    return SignalAspect::Red;
}

bool TrafficSignalController::vehicleMayProceed(SignalAxis axis) const
{
    const SignalAspect aspect = vehicleAspect(axis);
    return aspect == SignalAspect::Green || aspect == SignalAspect::Amber;
}

bool TrafficSignalController::pedestrianGreen(SignalAxis crossedAxis) const
{
    // Crossing the main street runs with the side street's vehicle green, and
    // stops a clearance interval before the main street's green returns.
    const bool crossingMain = crossedAxis == SignalAxis::Main;
    const Stage running = crossingMain ? Stage::SideGreen : Stage::MainGreen;
    if (stage_ != running) return false;
    const float length = stageLength(running);
    return phaseTime_ < std::max(0.0f, length - timing_.pedestrianClearance);
}

const char* TrafficSignalController::stageName() const
{
    switch (stage_)
    {
        case Stage::MainGreen:       return "main green";
        case Stage::MainAmber:       return "main amber";
        case Stage::AllRedAfterMain: return "all red";
        case Stage::SideRedAmber:    return "side red/amber";
        case Stage::SideGreen:       return "side green";
        case Stage::SideAmber:       return "side amber";
        case Stage::AllRedAfterSide: return "all red";
        case Stage::MainRedAmber:    return "main red/amber";
    }
    return "?";
}

}  // namespace CnaStreet
