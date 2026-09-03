// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief Camera basis, projection and the frustum the renderer culls against.
 *
 * Culling is tested here rather than in the renderer because this is where the
 * decision is actually made: the renderer asks the frustum whether a bounding
 * volume is outside it and skips the draw. If the frustum is wrong, the symptom
 * is geometry vanishing at the edge of the screen, which is both maddening to
 * find by eye and trivial to assert.
 */
#include "CnaStreet/Render/Camera.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"

#include "TestSupport.hpp"

#include <cmath>

using namespace CnaStreet;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::ContainmentType;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

namespace {

bool visible(const Camera& camera, const Vector3& at, float radius)
{
    return camera.frustum().Contains(BoundingSphere(at, radius)) != ContainmentType::Disjoint;
}

float Length(const Vector3& v) { return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z); }

}  // namespace

int main()
{
    CASE("yaw zero looks along -Z, and the basis is orthonormal");
    {
        Camera camera;
        camera.setPosition(Vector3(0.0f, 1.66f, 0.0f));
        camera.setOrientation(0.0f, 0.0f);
        CHECK_NEAR(camera.forward().Z, -1.0, 1e-4);
        CHECK_NEAR(camera.forward().X, 0.0, 1e-4);

        // A quarter turn to the right looks east.
        camera.setOrientation(1.5707963f, 0.0f);
        CHECK_NEAR(camera.forward().X, 1.0, 1e-4);
        CHECK_NEAR(camera.forward().Z, 0.0, 1e-4);

        for (const float yaw : {0.0f, 0.7f, 2.9f, -1.3f})
            for (const float pitch : {0.0f, 0.6f, -0.9f})
            {
                camera.setOrientation(yaw, pitch);
                const Vector3 f = camera.forward(), r = camera.right(), u = camera.up();
                CHECK_NEAR(Length(f), 1.0, 1e-4);
                CHECK_NEAR(Length(r), 1.0, 1e-4);
                CHECK_NEAR(Length(u), 1.0, 1e-4);
                CHECK_NEAR(f.X * r.X + f.Y * r.Y + f.Z * r.Z, 0.0, 1e-4);
                CHECK_NEAR(f.X * u.X + f.Y * u.Y + f.Z * u.Z, 0.0, 1e-4);
                // The right vector stays level however far the camera looks up
                // or down, which is what stops a free camera rolling.
                CHECK_NEAR(r.Y, 0.0, 1e-4);
            }
    }

    CASE("ground-forward is forward flattened, and survives looking straight down");
    {
        Camera camera;
        camera.setOrientation(0.9f, -1.5707f);
        const Vector3 ground = camera.groundForward();
        CHECK_NEAR(Length(ground), 1.0, 1e-3);
        CHECK_NEAR(ground.Y, 0.0, 1e-4);
        CHECK_NEAR(ground.X, std::sin(0.9f), 1e-3);
        CHECK_NEAR(ground.Z, -std::cos(0.9f), 1e-3);
    }

    CASE("what is in front is drawn and what is behind is not");
    {
        Camera camera;
        camera.setPosition(Vector3(0.0f, 1.7f, 0.0f));
        camera.setOrientation(0.0f, 0.0f);
        camera.setPerspective(1.0996f, 16.0f / 9.0f, 0.1f, 400.0f);

        CHECK(visible(camera, Vector3(0.0f, 1.7f, -20.0f), 1.0f));
        CHECK(!visible(camera, Vector3(0.0f, 1.7f, 20.0f), 1.0f));
        // Beyond the far plane, and this side of the near plane.
        CHECK(!visible(camera, Vector3(0.0f, 1.7f, -600.0f), 1.0f));
        // Well off to the side.
        CHECK(!visible(camera, Vector3(300.0f, 1.7f, -20.0f), 1.0f));
        // A building that straddles the edge of the view is still drawn.
        CHECK(visible(camera, Vector3(22.0f, 8.0f, -20.0f), 14.0f));
    }

    CASE("turning the camera changes what it can see");
    {
        Camera camera;
        camera.setPosition(Vector3::Zero);
        camera.setPerspective(1.0996f, 16.0f / 9.0f, 0.1f, 400.0f);
        const Vector3 east(40.0f, 0.0f, 0.0f);

        camera.setOrientation(0.0f, 0.0f);
        CHECK(!visible(camera, east, 1.0f));
        camera.setOrientation(1.5707963f, 0.0f);
        CHECK(visible(camera, east, 1.0f));
    }

    CASE("a wider field of view sees more");
    {
        Camera narrow, wide;
        for (Camera* camera : {&narrow, &wide})
        {
            camera->setPosition(Vector3::Zero);
            camera->setOrientation(0.0f, 0.0f);
        }
        narrow.setPerspective(0.6f, 16.0f / 9.0f, 0.1f, 400.0f);
        wide.setPerspective(1.5f, 16.0f / 9.0f, 0.1f, 400.0f);

        int narrowSeen = 0, wideSeen = 0;
        for (int i = -40; i <= 40; ++i)
        {
            const Vector3 at(static_cast<float>(i) * 1.5f, 0.0f, -30.0f);
            narrowSeen += visible(narrow, at, 0.3f) ? 1 : 0;
            wideSeen += visible(wide, at, 0.3f) ? 1 : 0;
        }
        CHECK(wideSeen > narrowSeen);
    }

    CASE("the frustum follows the camera when it moves");
    {
        Camera camera;
        camera.setPerspective(1.0996f, 16.0f / 9.0f, 0.1f, 400.0f);
        camera.setOrientation(0.0f, 0.0f);
        const Vector3 target(0.0f, 0.0f, -50.0f);

        camera.setPosition(Vector3::Zero);
        CHECK(visible(camera, target, 1.0f));
        camera.setPosition(Vector3(0.0f, 0.0f, -120.0f));
        CHECK(!visible(camera, target, 1.0f));
    }

    CASE("view times projection is the matrix the shaders are handed");
    {
        Camera camera;
        camera.setPosition(Vector3(3.0f, 2.0f, -4.0f));
        camera.setOrientation(0.4f, -0.2f);
        camera.setPerspective(1.0f, 1.6f, 0.2f, 300.0f);
        const Matrix combined = camera.view() * camera.projection();
        const Matrix& cached = camera.viewProjection();
        CHECK_NEAR(cached.M11, combined.M11, 1e-4);
        CHECK_NEAR(cached.M34, combined.M34, 1e-4);
        CHECK_NEAR(cached.M44, combined.M44, 1e-4);
    }

    CASE("a cascade sub-range projects a narrower slice than the whole view");
    {
        Camera camera;
        camera.setPerspective(1.0996f, 16.0f / 9.0f, 0.1f, 400.0f);
        const Matrix slice = camera.projectionForRange(10.0f, 40.0f);
        // Same field of view, so the same lateral scale; a different depth range,
        // so a different depth mapping. Getting this wrong makes every cascade
        // fit the whole view and the near shadows go soft.
        CHECK_NEAR(slice.M11, camera.projection().M11, 1e-4);
        CHECK(std::fabs(slice.M33 - camera.projection().M33) > 1e-3f);
    }

    CASE("the bounding box of a batch is culled the same way as its sphere");
    {
        Camera camera;
        camera.setPosition(Vector3::Zero);
        camera.setOrientation(0.0f, 0.0f);
        camera.setPerspective(1.0996f, 16.0f / 9.0f, 0.1f, 400.0f);

        const BoundingBox ahead(Vector3(-5.0f, 0.0f, -40.0f), Vector3(5.0f, 12.0f, -20.0f));
        const BoundingBox behind(Vector3(-5.0f, 0.0f, 20.0f), Vector3(5.0f, 12.0f, 40.0f));
        CHECK(camera.frustum().Contains(ahead) != ContainmentType::Disjoint);
        CHECK(camera.frustum().Contains(behind) == ContainmentType::Disjoint);
    }

    TEST_MAIN("camera");
}
