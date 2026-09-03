// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaStreet {

/**
 * @brief A perspective camera: where it is, where it looks, what it sees.
 *
 * Holds the state; @ref FreeCameraController decides how it moves. Splitting the
 * two is what lets the cinematic camera, the scripted screenshot viewpoints and
 * the player share one projection and one frustum.
 */
class Camera
{
public:
    void setPosition(const Microsoft::Xna::Framework::Vector3& position);
    void setOrientation(float yawRadians, float pitchRadians);
    void setPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
    void setAspect(float aspect);
    void setVerticalFov(float radians);

    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& position() const { return position_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] float verticalFov() const { return fov_; }
    [[nodiscard]] float nearPlane() const { return near_; }
    [[nodiscard]] float farPlane() const { return far_; }
    [[nodiscard]] float aspect() const { return aspect_; }

    /// Unit vectors of the camera basis in world space.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 forward() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 right() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 up() const;
    /// Forward flattened onto the ground plane — what "walk forward" means.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 groundForward() const;

    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& view() const;
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& projection() const;
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& viewProjection() const;
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingFrustum& frustum() const;

    /// A projection with a different far plane, for the shadow cascade fit.
    [[nodiscard]] Microsoft::Xna::Framework::Matrix projectionForRange(float nearPlane,
                                                                      float farPlane) const;

private:
    void invalidate() { dirty_ = true; }
    void rebuild() const;

    Microsoft::Xna::Framework::Vector3 position_{0.0f, 1.66f, 0.0f};
    float yaw_    = 0.0f;
    float pitch_  = 0.0f;
    float fov_    = 1.09955743f;   // 63 degrees vertical, ~90 horizontal at 16:9
    float aspect_ = 16.0f / 9.0f;
    float near_   = 0.12f;
    float far_    = 620.0f;

    mutable bool dirty_ = true;
    mutable Microsoft::Xna::Framework::Matrix view_{};
    mutable Microsoft::Xna::Framework::Matrix projection_{};
    mutable Microsoft::Xna::Framework::Matrix viewProjection_{};
    mutable Microsoft::Xna::Framework::BoundingFrustum frustum_{
        Microsoft::Xna::Framework::Matrix::getIdentityProperty()};
};

}  // namespace CnaStreet
