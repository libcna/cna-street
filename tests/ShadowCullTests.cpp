// SPDX-License-Identifier: MIT
/**
 * @file ShadowCullTests.cpp
 * @brief The per-cascade caster test the shadow pass culls with.
 *
 * A cascaded shadow map is read by view depth: a receiver at 30 m samples the
 * cascade whose slice covers 30 m and no other. So a caster only has to be
 * written into a cascade if something it can shade lies in that cascade's
 * slice, and `SceneRenderer::casterShadowReachesSlice` decides that from the
 * volume the caster's shadow can occupy. These cases pin the decisions that
 * matter: a bollard beside the camera is in the near cascade and out of the
 * far one; a building whose shadow leans forward is in the cascades its
 * shadow crosses; a caster behind the camera whose shadow falls in front of
 * it is kept; and a sun on the horizon keeps everything, because a shadow
 * that long lands anywhere.
 */
#include "CnaStreet/Render/SceneRenderer.hpp"

#include "TestSupport.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace CnaStreet;
using namespace CnaStreet::Test;

namespace {

/// The light's direction of travel for a sun at @p elevation degrees, coming
/// from the +X, +Z quadrant: the same shape SkySystem::lightDirection has.
Vector3 LightAt(float elevationDegrees)
{
    const float e = elevationDegrees * 3.14159265f / 180.0f;
    const float horizontal = std::cos(e);
    return Vector3::Normalize(Vector3(-horizontal * 0.94f, -std::sin(e), -horizontal * 0.34f));
}

// The splits the shipped settings produce: 0.12 | 7.3 | 17.4 | 45.9 | 190.
constexpr float kNear = 0.12f, kSplit0 = 7.3f, kSplit1 = 17.4f, kSplit2 = 45.9f, kSplit3 = 190.0f;
constexpr float kGround = -0.10f;
constexpr float kMargin = 1.12f;

bool InCascade(int cascade, const Vector3& light, const Vector3& centre, float radius,
               const Vector3& eye = Vector3(0.0f, 1.7f, 0.0f),
               const Vector3& forward = Vector3(0.0f, 0.0f, -1.0f))
{
    const float splits[] = {kNear, kSplit0, kSplit1, kSplit2, kSplit3};
    return SceneRenderer::casterShadowReachesSlice(eye, forward, splits[cascade],
                                                    splits[cascade + 1], light, centre, radius,
                                                    kGround, kMargin);
}

}  // namespace

int main()
{
    const Vector3 noon = LightAt(48.0f);

    beginCase("a bollard beside the camera is in the near cascade and out of the far ones");
    {
        const Vector3 bollard(2.0f, 0.55f, -3.0f);
        CHECK(InCascade(0, noon, bollard, 0.6f));
        CHECK(!InCascade(2, noon, bollard, 0.6f));
        CHECK(!InCascade(3, noon, bollard, 0.6f));
    }

    beginCase("a person on the far footway is in the cascade that covers them only");
    {
        const Vector3 person(-7.0f, 1.0f, -30.0f);
        CHECK(!InCascade(0, noon, person, 1.0f));
        CHECK(!InCascade(1, noon, person, 1.0f));
        CHECK(InCascade(2, noon, person, 1.0f));
        CHECK(!InCascade(3, noon, person, 1.0f));
    }

    beginCase("a building's shadow leans forward and is in every cascade it crosses");
    {
        // Twelve metres of radius, twenty-four metres out, under a 48 degree
        // sun coming from behind the camera's right shoulder: the sphere's
        // top reaches the ground 28 m along the light, 6.5 m further down the
        // view, so the shadow occupies about 11 m to 44 m of depth -- the
        // first three cascades and not the fourth.
        const Vector3 block(10.0f, 9.0f, -24.0f);
        CHECK(InCascade(1, noon, block, 12.0f));
        CHECK(InCascade(2, noon, block, 12.0f));
        CHECK(!InCascade(3, noon, block, 12.0f));
        // Twelve metres further and it is in the far cascade too.
        CHECK(InCascade(3, noon, Vector3(10.0f, 9.0f, -36.0f), 12.0f));
    }

    beginCase("a caster behind the camera whose shadow falls in front of it is kept");
    {
        // Light travelling the way the camera looks, so the shadow of a thing
        // five metres behind the camera lands just in front of it.
        const Vector3 light = Vector3::Normalize(Vector3(0.0f, -0.6f, -0.8f));
        const Vector3 behind(0.0f, 3.0f, 5.0f);
        CHECK(InCascade(0, light, behind, 1.0f));
        CHECK(!InCascade(1, light, behind, 1.0f));
        // The same thing with the light travelling the other way shades the
        // ground further behind the camera and is in no cascade at all.
        const Vector3 away = Vector3::Normalize(Vector3(0.0f, -0.6f, 0.8f));
        CHECK(!InCascade(0, away, Vector3(0.0f, 3.0f, 8.0f), 1.0f));
    }

    beginCase("a sun on the horizon keeps every caster");
    {
        const Vector3 dawn = LightAt(1.5f);
        CHECK(InCascade(3, dawn, Vector3(2.0f, 0.55f, -3.0f), 0.6f));
        CHECK(InCascade(0, dawn, Vector3(0.0f, 9.0f, -150.0f), 12.0f));
    }

    beginCase("the test is about depth along the view, not distance from the eye");
    {
        // A caster off to the side at 40 m of distance but only 20 m of depth
        // belongs to the cascade that covers 20 m.
        const Vector3 aside(34.6f, 1.0f, -20.0f);
        CHECK(InCascade(2, noon, aside, 1.0f));
        CHECK(!InCascade(3, noon, aside, 1.0f));
    }

    return summary("shadow_cull_tests");
}
