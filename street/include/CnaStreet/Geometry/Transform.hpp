// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <cmath>
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaStreet::Geometry {

/**
 * @brief Builds a world matrix from an origin and three axes.
 *
 * XNA's `Matrix::CreateWorld` takes a forward vector and derives the rest,
 * which is the wrong shape for extruding a kerb profile along a street: there
 * the three axes are all known and none of them is "forward". Row-vector
 * convention, matching the rest of the framework — row 1 is the image of local X.
 */
[[nodiscard]] inline Microsoft::Xna::Framework::Matrix Frame(
    const Microsoft::Xna::Framework::Vector3& origin,
    const Microsoft::Xna::Framework::Vector3& axisX,
    const Microsoft::Xna::Framework::Vector3& axisY,
    const Microsoft::Xna::Framework::Vector3& axisZ)
{
    Microsoft::Xna::Framework::Matrix m = Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    m.M11 = axisX.X; m.M12 = axisX.Y; m.M13 = axisX.Z;
    m.M21 = axisY.X; m.M22 = axisY.Y; m.M23 = axisY.Z;
    m.M31 = axisZ.X; m.M32 = axisZ.Y; m.M33 = axisZ.Z;
    m.M41 = origin.X; m.M42 = origin.Y; m.M43 = origin.Z;
    return m;
}

/// A frame whose local Z runs along the given horizontal direction, local Y is
/// up and local X is to its left. The shape every extrusion along a street wants.
[[nodiscard]] inline Microsoft::Xna::Framework::Matrix AlongFrame(
    const Microsoft::Xna::Framework::Vector3& origin, float dirX, float dirZ)
{
    using Microsoft::Xna::Framework::Vector3;
    const float length = std::sqrt(dirX * dirX + dirZ * dirZ);
    const float ux = length > 1e-6f ? dirX / length : 0.0f;
    const float uz = length > 1e-6f ? dirZ / length : 1.0f;
    // Left of a heading (ux,uz) on the ground is (uz,-ux) with +Y up.
    return Frame(origin, Vector3(uz, 0.0f, -ux), Vector3(0.0f, 1.0f, 0.0f),
                 Vector3(ux, 0.0f, uz));
}

/// Rotation about +Y, then translation. The workhorse for placing props.
[[nodiscard]] inline Microsoft::Xna::Framework::Matrix Place(float x, float y, float z,
                                                             float yawRadians)
{
    return Microsoft::Xna::Framework::Matrix::CreateRotationY(yawRadians)
           * Microsoft::Xna::Framework::Matrix::CreateTranslation(x, y, z);
}

}  // namespace CnaStreet::Geometry
