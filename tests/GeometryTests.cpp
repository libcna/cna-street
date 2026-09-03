// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief Mesh building, seeded randomness and the noise the textures are made of.
 *
 * The winding convention is the thing to guard here. Getting it wrong does not
 * crash and does not look like a bug: it produces geometry that is visible but
 * lit from behind, or invisible from the only side anyone looks at it from.
 * Both happened -- a roof that stayed grey in full sun, a car whose near-side
 * panels were culled so you could see the inside of its far side.
 */
#include "CnaStreet/Assets/Noise.hpp"
#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Geometry/MeshBuilder.hpp"
#include "CnaStreet/Geometry/Transform.hpp"

#include "TestSupport.hpp"

#include <cmath>
#include <set>

using namespace CnaStreet;
using namespace CnaStreet::Geometry;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

namespace {

float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

}  // namespace

int main()
{
    CASE("a quad's vertex normals point where the corner order says");
    {
        MeshBuilder builder;
        // Counter-clockwise seen from +Y, i.e. facing up.
        builder.addQuad(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(1, 0, 1), Vector3(1, 0, 0));
        const MeshData& mesh = builder.mesh();
        CHECK(mesh.vertices.size() == 4);
        CHECK(mesh.indices.size() == 6);
        for (const Vertex& vertex : mesh.vertices)
            CHECK_NEAR(vertex.Normal.Y, 1.0, 1e-4);
    }

    CASE("addQuadFacing makes the normal follow the hint, whatever the corner order");
    {
        const Vector3 corners[4] = {Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(1, 0, 1),
                                    Vector3(1, 0, 0)};
        for (const float sign : {-1.0f, 1.0f})
            for (const bool reversed : {false, true})
            {
                MeshBuilder builder;
                const Vector3 hint(0.0f, sign, 0.0f);
                if (reversed)
                    builder.addQuadFacing(corners[3], corners[2], corners[1], corners[0], hint);
                else
                    builder.addQuadFacing(corners[0], corners[1], corners[2], corners[3], hint);
                for (const Vertex& vertex : builder.mesh().vertices)
                    CHECK_NEAR(vertex.Normal.Y, sign, 1e-4);
            }
    }

    CASE("addQuadFacingUv does the same and lays unit UVs on it");
    {
        MeshBuilder builder;
        builder.setUvMode(UvMode::Explicit);
        builder.addQuadFacingUv(Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 1, 0),
                                Vector3(0, 1, 0), Vector3(0, 0, -1));
        float minU = 9.0f, maxU = -9.0f, minV = 9.0f, maxV = -9.0f;
        for (const Vertex& vertex : builder.mesh().vertices)
        {
            CHECK_NEAR(vertex.Normal.Z, -1.0, 1e-4);
            minU = std::min(minU, vertex.TextureCoordinate.X);
            maxU = std::max(maxU, vertex.TextureCoordinate.X);
            minV = std::min(minV, vertex.TextureCoordinate.Y);
            maxV = std::max(maxV, vertex.TextureCoordinate.Y);
        }
        CHECK_NEAR(minU, 0.0, 1e-5);
        CHECK_NEAR(maxU, 1.0, 1e-5);
        CHECK_NEAR(minV, 0.0, 1e-5);
        CHECK_NEAR(maxV, 1.0, 1e-5);
    }

    CASE("a degenerate quad is refused rather than silently mis-lit");
    {
        MeshBuilder builder;
        CHECK(!builder.addQuad(Vector3::Zero, Vector3::Zero, Vector3::Zero, Vector3::Zero));
        CHECK(builder.mesh().vertices.empty());
    }

    CASE("a box has six outward faces and the bounds it was asked for");
    {
        MeshBuilder builder;
        builder.addBox(Vector3(-1, 0, -2), Vector3(3, 4, 5));
        const MeshData& mesh = builder.mesh();
        CHECK(mesh.indices.size() == 36);
        // Every vertex normal points away from the centre.
        const Vector3 centre(1.0f, 2.0f, 1.5f);
        for (const Vertex& vertex : mesh.vertices)
        {
            const Vector3 out(vertex.Position.X - centre.X, vertex.Position.Y - centre.Y,
                              vertex.Position.Z - centre.Z);
            CHECK_MSG(Dot(out, vertex.Normal) > 0.0f, "a box face points inward");
        }
        const Microsoft::Xna::Framework::BoundingBox bounds = mesh.bounds();
        CHECK_NEAR(bounds.Min.X, -1.0, 1e-5);
        CHECK_NEAR(bounds.Max.Z, 5.0, 1e-5);
    }

    CASE("BoxFaces omits exactly what it says");
    {
        MeshBuilder all, sides, allButBottom;
        all.addBox(Vector3::Zero, Vector3::One);
        sides.addBox(Vector3::Zero, Vector3::One, BoxFaces::sides());
        allButBottom.addBox(Vector3::Zero, Vector3::One, BoxFaces::allButBottom());
        CHECK(sides.mesh().indices.size() == all.mesh().indices.size() * 4 / 6);
        CHECK(allButBottom.mesh().indices.size() == all.mesh().indices.size() * 5 / 6);
    }

    CASE("a cylinder between two points spans them and stays on its axis");
    {
        MeshBuilder builder;
        const Vector3 from(1.0f, 2.0f, 3.0f);
        const Vector3 to(1.0f, 6.0f, 3.0f);
        builder.addCylinderBetween(from, to, 0.25f, 12, true);
        const Microsoft::Xna::Framework::BoundingBox bounds = builder.mesh().bounds();
        CHECK_NEAR(bounds.Min.Y, 2.0, 1e-4);
        CHECK_NEAR(bounds.Max.Y, 6.0, 1e-4);
        CHECK_NEAR(bounds.Max.X - bounds.Min.X, 0.5, 0.02);
        CHECK_NEAR(bounds.Max.Z - bounds.Min.Z, 0.5, 0.02);
    }

    CASE("append with a mirroring transform keeps the faces pointing outward");
    {
        // A mirror flips the handedness of the transform, and a naive append
        // turns every face of the mirrored copy inside out.
        MeshBuilder source;
        source.addBox(Vector3(0, 0, 0), Vector3(1, 1, 1));
        MeshBuilder target;
        Matrix mirror = Matrix::getIdentityProperty();
        mirror.M11 = -1.0f;   // mirror in X
        target.append(source.mesh(), mirror);

        const Vector3 centre(-0.5f, 0.5f, 0.5f);
        for (const Vertex& vertex : target.mesh().vertices)
        {
            const Vector3 out(vertex.Position.X - centre.X, vertex.Position.Y - centre.Y,
                              vertex.Position.Z - centre.Z);
            CHECK_MSG(Dot(out, vertex.Normal) > 0.0f, "a mirrored face points inward");
        }
    }

    CASE("Place puts a prop where it is told, facing where it is told");
    {
        const Matrix world = Place(4.0f, 1.5f, -7.0f, 1.5707963f);
        CHECK_NEAR(world.M41, 4.0, 1e-5);
        CHECK_NEAR(world.M42, 1.5, 1e-5);
        CHECK_NEAR(world.M43, -7.0, 1e-5);
        // Local +Z, which is the way every prop faces, must map to world +X.
        CHECK_NEAR(world.M31, 1.0, 1e-5);
        CHECK_NEAR(world.M33, 0.0, 1e-5);
    }

    CASE("the seeded generator is reproducible, and derived streams are independent");
    {
        Rng a(4242u), b(4242u);
        for (int i = 0; i < 500; ++i) CHECK_NEAR(a.unit(), b.unit(), 0.0);

        Rng one = Rng::derive(7u, "traffic");
        Rng two = Rng::derive(7u, "traffic");
        Rng three = Rng::derive(7u, "vegetation");
        bool same = true, differs = false;
        for (int i = 0; i < 100; ++i)
        {
            const float x = one.unit(), y = two.unit(), z = three.unit();
            same = same && std::fabs(x - y) < 1e-9f;
            differs = differs || std::fabs(x - z) > 1e-6f;
        }
        CHECK_MSG(same, "the same derived stream gave two different sequences");
        CHECK_MSG(differs, "two different derived streams gave the same sequence");
    }

    CASE("the generator's ranges are the ranges it advertises");
    {
        Rng rng(99u);
        for (int i = 0; i < 5000; ++i)
        {
            const float u = rng.unit();
            CHECK(u >= 0.0f && u < 1.0f);
            const float r = rng.range(-3.0f, 7.0f);
            CHECK(r >= -3.0f && r < 7.0f);
            const int n = rng.intRange(2, 5);
            CHECK(n >= 2 && n <= 5);
            CHECK(rng.index(4) < 4u);
        }
    }

    CASE("smoothstep works with the edges either way round");
    {
        using namespace CnaStreet::Noise;
        // A descending pair is how every "bright where the value is small" ramp
        // in the texture generators is written. Rejecting it turned all of them
        // into hard binary masks, which is what an early build's signs and
        // lettering actually were.
        CHECK_NEAR(smoothstep(0.0f, 1.0f, 0.5f), 0.5, 1e-6);
        CHECK_NEAR(smoothstep(1.0f, 0.0f, 0.5f), 0.5, 1e-6);
        CHECK_NEAR(smoothstep(1.0f, 0.0f, 0.0f), 1.0, 1e-6);
        CHECK_NEAR(smoothstep(1.0f, 0.0f, 1.0f), 0.0, 1e-6);
        CHECK_NEAR(smoothstep(0.3f, 0.3f, 0.2f), 0.0, 1e-6);
        CHECK_NEAR(smoothstep(0.3f, 0.3f, 0.4f), 1.0, 1e-6);
        // Monotone in between, in both directions.
        float previous = -1.0f;
        for (int i = 0; i <= 20; ++i)
        {
            const float t = static_cast<float>(i) / 20.0f;
            const float v = smoothstep(0.2f, 0.8f, t);
            CHECK(v >= previous - 1e-6f);
            previous = v;
        }
    }

    CASE("the noise the textures tile with actually tiles");
    {
        using namespace CnaStreet::Noise;
        // Every generated surface has to repeat seamlessly, and the whole reason
        // the noise takes a period is that it wraps at it.
        for (int i = 0; i < 32; ++i)
        {
            const float t = static_cast<float>(i) * 0.37f;
            CHECK_NEAR(value2(t, 3.0f, 16, 5u), value2(t + 16.0f, 3.0f, 16, 5u), 1e-4);
            CHECK_NEAR(value2(3.0f, t, 16, 5u), value2(3.0f, t + 16.0f, 16, 5u), 1e-4);
            CHECK_NEAR(fbm(t, 1.5f, 8, 3, 2.0f, 0.5f, 9u),
                       fbm(t + 8.0f, 1.5f, 8, 3, 2.0f, 0.5f, 9u), 1e-4);
        }
    }

    CASE("noise stays in range and depends on its seed");
    {
        using namespace CnaStreet::Noise;
        bool differs = false;
        for (int i = 0; i < 400; ++i)
        {
            const float u = static_cast<float>(i) * 0.11f;
            const float a = value2(u, u * 0.7f, 32, 1u);
            const float b = value2(u, u * 0.7f, 32, 2u);
            CHECK(a >= 0.0f && a <= 1.0f);
            const float f = fbm(u, u * 0.3f, 32, 4, 2.0f, 0.5f, 1u);
            CHECK(f >= -0.01f && f <= 1.01f);
            differs = differs || std::fabs(a - b) > 1e-4f;
        }
        CHECK_MSG(differs, "two different noise seeds gave the same field");
    }

    TEST_MAIN("geometry");
}
