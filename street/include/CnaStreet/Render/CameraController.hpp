// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Camera.hpp"

#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>
#include <string>
#include <vector>

namespace CnaStreet {

/// How the camera behaves.
enum class CameraMode
{
    Fly,        ///< six degrees of freedom, no gravity, no collision
    Walk,       ///< eye height above the ground, blocked by buildings and vehicles
    Cinematic   ///< follows a scripted path, ignores movement input
};

/// A named camera position, used for the screenshot set and the F-key jumps.
struct Viewpoint
{
    std::string name;
    Microsoft::Xna::Framework::Vector3 position;
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float fov   = 1.09955743f;
};

/**
 * @brief Turns input into camera motion.
 *
 * The controls are the ones a person who has used any 3-D tool already knows:
 * WASD to move, mouse to look, Q/E down and up, Shift faster, Ctrl slower, wheel
 * to change the base speed, Escape to release the pointer, Tab to switch between
 * flying and walking, R to go back to the start.
 *
 * Everything that could jitter is handled here rather than in the camera:
 * movement is frame-rate independent, mouse deltas come from relative mouse mode
 * (so there is no edge to hit and no warp-to-centre stutter), and the speed
 * multiplier is exponential in the wheel so that one notch feels the same at
 * walking pace and at 100 m/s.
 */
class CameraController
{
public:
    /// Reports whether a point is inside solid geometry, for Walk mode.
    using CollisionProbe = std::function<bool(const Microsoft::Xna::Framework::Vector3&)>;
    /// Returns the walkable surface height under a point, or a large negative
    /// number where there is none.
    using GroundProbe = std::function<float(float x, float z)>;

    CameraController();

    void setCamera(Camera* camera) { camera_ = camera; }
    void setCollisionProbe(CollisionProbe probe) { collision_ = std::move(probe); }
    void setGroundProbe(GroundProbe probe) { ground_ = std::move(probe); }

    void setMode(CameraMode mode);
    [[nodiscard]] CameraMode mode() const { return mode_; }

    /// Where R and startup put the camera.
    void setHome(const Viewpoint& home);
    void applyViewpoint(const Viewpoint& viewpoint);
    void setViewpoints(std::vector<Viewpoint> viewpoints);
    [[nodiscard]] const std::vector<Viewpoint>& viewpoints() const { return viewpoints_; }

    void setMouseSensitivity(float radiansPerPixel) { mouseSensitivity_ = radiansPerPixel; }
    [[nodiscard]] float mouseSensitivity() const { return mouseSensitivity_; }
    void setMoveSpeed(float metresPerSecond);
    [[nodiscard]] float moveSpeed() const { return moveSpeed_; }
    void setInvertY(bool invert) { invertY_ = invert; }

    [[nodiscard]] bool isMouseCaptured() const { return mouseCaptured_; }
    void setMouseCaptured(bool captured);

    /// One frame of input. `deltaSeconds` is clamped internally: a stall while
    /// an asset loads must not teleport the camera through a building.
    void update(float deltaSeconds,
                const Microsoft::Xna::Framework::Input::KeyboardState& keyboard,
                const Microsoft::Xna::Framework::Input::KeyboardState& previousKeyboard,
                const Microsoft::Xna::Framework::Input::MouseState& mouse,
                const Microsoft::Xna::Framework::Input::MouseState& previousMouse);

    /// Drives the cinematic dolly. Separated so a headless run can step it
    /// deterministically without any input at all.
    void updateCinematic(float deltaSeconds);
    void setCinematicPath(std::vector<Viewpoint> path, float secondsPerLeg);
    [[nodiscard]] float cinematicProgress() const { return cinematicTime_; }

    [[nodiscard]] const char* modeName() const;

private:
    void applyLook(float deltaYaw, float deltaPitch);
    void moveWithCollision(const Microsoft::Xna::Framework::Vector3& delta);

    Camera*    camera_ = nullptr;
    CameraMode mode_   = CameraMode::Fly;

    float moveSpeed_        = 7.0f;
    float mouseSensitivity_ = 0.0022f;
    bool  invertY_          = false;
    bool  mouseCaptured_    = false;
    int   lastScroll_       = 0;
    bool  hasLastScroll_    = false;

    Viewpoint              home_;
    std::vector<Viewpoint> viewpoints_;

    std::vector<Viewpoint> cinematicPath_;
    float cinematicSecondsPerLeg_ = 7.0f;
    float cinematicTime_          = 0.0f;

    CollisionProbe collision_;
    GroundProbe    ground_;

    /// Vertical velocity in Walk mode, so stepping off a kerb is a fall and not
    /// a teleport.
    float verticalVelocity_ = 0.0f;
};

}  // namespace CnaStreet
