// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/CameraController.hpp"

#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::KeyboardState;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::MouseState;

namespace CnaStreet {

namespace {

constexpr float kMinSpeed = 0.35f;
constexpr float kMaxSpeed = 180.0f;
/// One wheel notch is 120 units in XNA's scroll value.
constexpr float kScrollNotch = 120.0f;
/// A frame longer than this is a stall, not motion.
constexpr float kMaxFrameSeconds = 0.10f;
constexpr float kGravity = 21.0f;
/// How far the camera can rise in one step without jumping — a kerb, a step.
constexpr float kStepHeight = 0.42f;
/// Radius of the walking camera's collision cylinder.
constexpr float kBodyRadius = 0.32f;

bool Pressed(const KeyboardState& now, const KeyboardState& before, Keys key)
{
    return now.IsKeyDown(key) && before.IsKeyUp(key);
}

}  // namespace

CameraController::CameraController()
{
    home_.name     = "Home";
    home_.position = Vector3(0.0f, Metrics::kEyeHeight, 42.0f);
}

void CameraController::setMode(CameraMode mode)
{
    mode_ = mode;
    verticalVelocity_ = 0.0f;
    if (mode_ == CameraMode::Walk && camera_ != nullptr && ground_)
    {
        const Vector3 p = camera_->position();
        const float surface = ground_(p.X, p.Z);
        if (surface > -1000.0f)
            camera_->setPosition(Vector3(p.X, surface + Metrics::kEyeHeight, p.Z));
    }
    if (mode_ == CameraMode::Cinematic) cinematicTime_ = 0.0f;
}

void CameraController::setHome(const Viewpoint& home)
{
    home_ = home;
    applyViewpoint(home_);
}

void CameraController::applyViewpoint(const Viewpoint& viewpoint)
{
    if (camera_ == nullptr) return;
    camera_->setPosition(viewpoint.position);
    camera_->setOrientation(viewpoint.yaw, viewpoint.pitch);
    camera_->setVerticalFov(viewpoint.fov);
    verticalVelocity_ = 0.0f;
}

void CameraController::setViewpoints(std::vector<Viewpoint> viewpoints)
{
    viewpoints_ = std::move(viewpoints);
}

void CameraController::setMoveSpeed(float metresPerSecond)
{
    moveSpeed_ = std::clamp(metresPerSecond, kMinSpeed, kMaxSpeed);
}

void CameraController::setMouseCaptured(bool captured)
{
    if (mouseCaptured_ == captured) return;
    mouseCaptured_ = captured;
    // Relative mode is what makes mouse-look continuous: the pointer stops
    // existing, deltas keep arriving, and there is no window edge to run into
    // and no warp-to-centre jump to filter out afterwards.
    Mouse::setIsRelativeMouseModeEXTProperty(captured);
}

void CameraController::setCinematicPath(std::vector<Viewpoint> path, float secondsPerLeg)
{
    cinematicPath_ = std::move(path);
    cinematicSecondsPerLeg_ = std::max(0.5f, secondsPerLeg);
    cinematicTime_ = 0.0f;
}

const char* CameraController::modeName() const
{
    switch (mode_)
    {
        case CameraMode::Fly:       return "fly";
        case CameraMode::Walk:      return "walk";
        case CameraMode::Cinematic: return "cinematic";
    }
    return "?";
}

void CameraController::applyLook(float deltaYaw, float deltaPitch)
{
    if (camera_ == nullptr) return;
    camera_->setOrientation(camera_->yaw() + deltaYaw, camera_->pitch() + deltaPitch);
}

void CameraController::moveWithCollision(const Vector3& delta)
{
    if (camera_ == nullptr) return;
    Vector3 position = camera_->position();

    if (mode_ != CameraMode::Walk || !collision_)
    {
        camera_->setPosition(position + delta);
        return;
    }

    // Axis-separated sliding: try each axis on its own so a glancing contact
    // with a wall slides along it instead of stopping dead, which is the
    // difference between exploring a street and fighting the controls.
    const Vector3 steps[3] = {Vector3(delta.X, 0.0f, 0.0f), Vector3(0.0f, 0.0f, delta.Z),
                              Vector3(0.0f, delta.Y, 0.0f)};
    for (const Vector3& step : steps)
    {
        if (step.X == 0.0f && step.Y == 0.0f && step.Z == 0.0f) continue;
        const Vector3 candidate = position + step;
        // Probe at knee and shoulder: a kerb must be steppable, a railing not.
        const Vector3 knee(candidate.X, candidate.Y - Metrics::kEyeHeight + 0.55f, candidate.Z);
        const Vector3 head(candidate.X, candidate.Y - 0.12f, candidate.Z);
        if (!collision_(knee) && !collision_(head)) position = candidate;
    }
    camera_->setPosition(position);
}

void CameraController::update(float deltaSeconds, const KeyboardState& keyboard,
                              const KeyboardState& previousKeyboard, const MouseState& mouse,
                              const MouseState& previousMouse)
{
    if (camera_ == nullptr) return;
    const float dt = std::clamp(deltaSeconds, 0.0f, kMaxFrameSeconds);

    // --- modes and viewpoints ---------------------------------------------
    if (Pressed(keyboard, previousKeyboard, Keys::Tab))
        setMode(mode_ == CameraMode::Fly ? CameraMode::Walk : CameraMode::Fly);
    if (Pressed(keyboard, previousKeyboard, Keys::C))
        setMode(mode_ == CameraMode::Cinematic ? CameraMode::Fly : CameraMode::Cinematic);
    if (Pressed(keyboard, previousKeyboard, Keys::R)) applyViewpoint(home_);
    if (Pressed(keyboard, previousKeyboard, Keys::Escape)) setMouseCaptured(false);

    static const Keys kViewpointKeys[] = {Keys::D1, Keys::D2, Keys::D3, Keys::D4,
                                          Keys::D5, Keys::D6, Keys::D7, Keys::D8};
    for (std::size_t i = 0; i < std::size(kViewpointKeys); ++i)
        if (i < viewpoints_.size() && Pressed(keyboard, previousKeyboard, kViewpointKeys[i]))
        {
            applyViewpoint(viewpoints_[i]);
            if (mode_ == CameraMode::Cinematic) setMode(CameraMode::Fly);
        }

    // --- mouse look ---------------------------------------------------------
    using Microsoft::Xna::Framework::Input::ButtonState;
    if (mouse.getLeftButtonProperty() == ButtonState::Pressed && !mouseCaptured_)
        setMouseCaptured(true);

    if (mouseCaptured_)
    {
        // In relative mode the reported position *is* the accumulated delta on
        // some back ends and an absolute position on others; differencing works
        // for both as long as the first frame after capture is discarded, which
        // the capture transition does by leaving previousMouse equal to mouse.
        const float dx = static_cast<float>(mouse.getXProperty() - previousMouse.getXProperty());
        const float dy = static_cast<float>(mouse.getYProperty() - previousMouse.getYProperty());
        const float pitchSign = invertY_ ? 1.0f : -1.0f;
        applyLook(dx * mouseSensitivity_, dy * mouseSensitivity_ * pitchSign);
    }

    // Arrow keys look too, so the demo is usable with no mouse at all — which is
    // exactly the situation in the headless screenshot runs.
    const float keyLook = 1.5f * dt;
    float yawDelta = 0.0f, pitchDelta = 0.0f;
    if (keyboard.IsKeyDown(Keys::Left))  yawDelta   -= keyLook;
    if (keyboard.IsKeyDown(Keys::Right)) yawDelta   += keyLook;
    if (keyboard.IsKeyDown(Keys::Up))    pitchDelta += keyLook;
    if (keyboard.IsKeyDown(Keys::Down))  pitchDelta -= keyLook;
    if (yawDelta != 0.0f || pitchDelta != 0.0f) applyLook(yawDelta, pitchDelta);

    // --- speed ---------------------------------------------------------------
    const int scroll = mouse.getScrollWheelValueProperty();
    if (hasLastScroll_ && scroll != lastScroll_)
    {
        const float notches = static_cast<float>(scroll - lastScroll_) / kScrollNotch;
        // Exponential, so a notch is the same proportional change whether the
        // camera is strolling or crossing the district.
        setMoveSpeed(moveSpeed_ * std::pow(1.25f, notches));
    }
    lastScroll_    = scroll;
    hasLastScroll_ = true;

    if (mode_ == CameraMode::Cinematic)
    {
        updateCinematic(dt);
        return;
    }

    // --- movement ------------------------------------------------------------
    float speed = moveSpeed_;
    if (keyboard.IsKeyDown(Keys::LeftShift) || keyboard.IsKeyDown(Keys::RightShift))
        speed *= 4.0f;
    if (keyboard.IsKeyDown(Keys::LeftControl) || keyboard.IsKeyDown(Keys::RightControl))
        speed *= 0.22f;
    if (mode_ == CameraMode::Walk)
        speed = std::min(speed, keyboard.IsKeyDown(Keys::LeftShift) ? 4.6f : 1.9f);

    const Vector3 forward = mode_ == CameraMode::Walk ? camera_->groundForward()
                                                      : camera_->forward();
    const Vector3 right   = camera_->right();

    Vector3 wish = Vector3::Zero;
    if (keyboard.IsKeyDown(Keys::W)) wish = wish + forward;
    if (keyboard.IsKeyDown(Keys::S)) wish = wish - forward;
    if (keyboard.IsKeyDown(Keys::D)) wish = wish + right;
    if (keyboard.IsKeyDown(Keys::A)) wish = wish - right;
    if (mode_ == CameraMode::Fly)
    {
        if (keyboard.IsKeyDown(Keys::E)) wish = wish + Vector3::Up;
        if (keyboard.IsKeyDown(Keys::Q)) wish = wish - Vector3::Up;
    }

    const float wishLengthSquared = wish.X * wish.X + wish.Y * wish.Y + wish.Z * wish.Z;
    if (wishLengthSquared > 1e-6f)
    {
        // Normalising is what stops diagonal movement being 41 % faster.
        wish = Vector3::Normalize(wish) * (speed * dt);
        moveWithCollision(wish);
    }

    // --- walking on the ground ----------------------------------------------
    if (mode_ == CameraMode::Walk && ground_)
    {
        const Vector3 p = camera_->position();
        const float surface = ground_(p.X, p.Z);
        if (surface > -1000.0f)
        {
            const float targetEye = surface + Metrics::kEyeHeight;
            if (p.Y <= targetEye + kStepHeight)
            {
                // Rise instantly onto a kerb but settle down smoothly, so a
                // dropped kerb does not read as a lift shaft.
                const float eye = p.Y < targetEye
                                      ? targetEye
                                      : targetEye + (p.Y - targetEye) * std::pow(0.02f, dt);
                camera_->setPosition(Vector3(p.X, eye, p.Z));
                verticalVelocity_ = 0.0f;
            }
            else
            {
                verticalVelocity_ -= kGravity * dt;
                const float eye = std::max(targetEye, p.Y + verticalVelocity_ * dt);
                camera_->setPosition(Vector3(p.X, eye, p.Z));
                if (eye <= targetEye) verticalVelocity_ = 0.0f;
            }
        }
    }
}

void CameraController::updateCinematic(float deltaSeconds)
{
    if (camera_ == nullptr || cinematicPath_.size() < 2) return;

    cinematicTime_ += deltaSeconds;
    const float total = cinematicSecondsPerLeg_ * static_cast<float>(cinematicPath_.size());
    if (cinematicTime_ >= total) cinematicTime_ -= total;

    const float legFloat = cinematicTime_ / cinematicSecondsPerLeg_;
    const std::size_t leg = static_cast<std::size_t>(legFloat) % cinematicPath_.size();
    const std::size_t next = (leg + 1) % cinematicPath_.size();
    const float t = legFloat - std::floor(legFloat);
    // Smoothstep, so the dolly eases in and out of every waypoint rather than
    // changing direction on a corner.
    const float e = t * t * (3.0f - 2.0f * t);

    const Viewpoint& a = cinematicPath_[leg];
    const Viewpoint& b = cinematicPath_[next];
    camera_->setPosition(Vector3::Lerp(a.position, b.position, e));
    // Interpolate the shorter way round the circle, or a pan past north spins
    // the camera the long way for no reason.
    float deltaYaw = MathHelper::WrapAngle(b.yaw - a.yaw);
    camera_->setOrientation(a.yaw + deltaYaw * e, a.pitch + (b.pitch - a.pitch) * e);
    camera_->setVerticalFov(a.fov + (b.fov - a.fov) * e);
}

}  // namespace CnaStreet
