// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/Camera.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;

namespace CnaStreet {

namespace {
/// Just short of straight up/down. Exactly vertical makes the view matrix
/// singular against a world up vector, and the picture snaps sideways.
constexpr float kPitchLimit = 1.55334303f;   // 89 degrees
}  // namespace

void Camera::setPosition(const Vector3& position)
{
    position_ = position;
    invalidate();
}

void Camera::setOrientation(float yawRadians, float pitchRadians)
{
    yaw_   = MathHelper::WrapAngle(yawRadians);
    pitch_ = std::clamp(pitchRadians, -kPitchLimit, kPitchLimit);
    invalidate();
}

void Camera::setPerspective(float verticalFovRadians, float aspect, float nearPlane,
                            float farPlane)
{
    fov_    = std::clamp(verticalFovRadians, 0.15f, 2.60f);
    aspect_ = aspect > 0.0f ? aspect : 1.0f;
    near_   = std::max(0.01f, nearPlane);
    far_    = std::max(near_ + 1.0f, farPlane);
    invalidate();
}

void Camera::setAspect(float aspect)
{
    aspect_ = aspect > 0.0f ? aspect : 1.0f;
    invalidate();
}

void Camera::setVerticalFov(float radians)
{
    fov_ = std::clamp(radians, 0.15f, 2.60f);
    invalidate();
}

Vector3 Camera::forward() const
{
    const float cosPitch = std::cos(pitch_);
    // Yaw 0 looks along -Z (north is +Z, so the default view is down the street
    // toward the junction), and increases clockwise seen from above.
    return Vector3(cosPitch * std::sin(yaw_), std::sin(pitch_), -cosPitch * std::cos(yaw_));
}

Vector3 Camera::groundForward() const
{
    return Vector3(std::sin(yaw_), 0.0f, -std::cos(yaw_));
}

Vector3 Camera::right() const
{
    return Vector3(std::cos(yaw_), 0.0f, std::sin(yaw_));
}

Vector3 Camera::up() const
{
    return Vector3::Cross(right(), forward());
}

void Camera::rebuild() const
{
    if (!dirty_) return;
    view_           = Matrix::CreateLookAt(position_, position_ + forward(), Vector3::Up);
    projection_     = Matrix::CreatePerspectiveFieldOfView(fov_, aspect_, near_, far_);
    viewProjection_ = view_ * projection_;
    frustum_        = BoundingFrustum(viewProjection_);
    dirty_          = false;
}

const Matrix& Camera::view() const
{
    rebuild();
    return view_;
}

const Matrix& Camera::projection() const
{
    rebuild();
    return projection_;
}

const Matrix& Camera::viewProjection() const
{
    rebuild();
    return viewProjection_;
}

const BoundingFrustum& Camera::frustum() const
{
    rebuild();
    return frustum_;
}

Matrix Camera::projectionForRange(float nearPlane, float farPlane) const
{
    return Matrix::CreatePerspectiveFieldOfView(fov_, aspect_, std::max(0.01f, nearPlane),
                                                std::max(nearPlane + 0.01f, farPlane));
}

}  // namespace CnaStreet
