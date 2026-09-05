// SPDX-License-Identifier: MIT
/**
 * @file RealismTests.cpp
 * @brief The invariants behind the second visual pass: reflections, glass,
 *        weathering, the things that stand in the street.
 *
 * None of these is a pixel test. Each pins a property a screenshot showed to
 * be wrong -- a cube face captured mirrored, a brake lamp the size of a
 * bumper, a shelf of confetti -- as a number that a later change cannot move
 * without saying so.
 */
#include "TestSupport.hpp"

#include "CnaStreet/Assets/TextureFactory.hpp"
#include "CnaStreet/Props/PropFactory.hpp"
#include "CnaStreet/Props/VehicleFactory.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Render/SceneRenderer.hpp"
#include "CnaStreet/Render/SkySystem.hpp"
#include "CnaStreet/Scene/CityScene.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>

using namespace CnaStreet;
using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;

namespace {

Vector3 Unit(const Vector3& v)
{
    const float l = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    return Vector3(v.X / l, v.Y / l, v.Z / l);
}

float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

/// The mean alpha of one atlas cell over a band of rows.
double meanAlpha(const Assets::Image& image, int cell, float rowFrom, float rowTo)
{
    const int half = image.width() / 2;
    const int x0 = (cell % 2) * half, y0 = (cell / 2) * half;
    double sum = 0.0;
    int n = 0;
    for (int y = y0 + static_cast<int>(rowFrom * half); y < y0 + static_cast<int>(rowTo * half); ++y)
        for (int x = x0; x < x0 + half; ++x)
        {
            sum += static_cast<double>(image.at(x, y)[3]);
            ++n;
        }
    return n == 0 ? 0.0 : sum / n;
}

struct Extent
{
    Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void add(const Vector3& p)
    {
        lo = Vector3(std::min(lo.X, p.X), std::min(lo.Y, p.Y), std::min(lo.Z, p.Z));
        hi = Vector3(std::max(hi.X, p.X), std::max(hi.Y, p.Y), std::max(hi.Z, p.Z));
    }
};

Extent ExtentOf(GeometryCollector& collector)
{
    Extent e;
    for (const GeometryCollector::Batch& batch : collector.take())
        for (const Geometry::Vertex& v : batch.mesh.vertices) e.add(v.Position);
    return e;
}

}  // namespace

int main()
{
    // ---------------------------------------------------------------------
    // Reflection probes
    // ---------------------------------------------------------------------
    CASE("a probe face's camera agrees with the cube layout the environment is read in");
    {
        // A cube map is addressed from inside the cube and a camera image from
        // outside, so the capture copies each face mirrored in x. That is the
        // kind of convention that is wrong silently: the reflection is of the
        // *other* side of the street and nothing reports it. So: the texel a
        // quarter of the way from the face's centre toward its right edge must
        // lie along the camera's *left* (minus right = minus forward x up),
        // and the texel below the centre along minus up.
        for (int f = 0; f < 6; ++f)
        {
            const CubeMapFace face = static_cast<CubeMapFace>(f);
            Vector3 forward, up;
            SceneRenderer::probeFaceBasis(face, forward, up);
            const Vector3 right = Vector3::Cross(forward, up);
            CHECK_NEAR(Dot(SkySystem::cubeDirection(face, 0.5f, 0.5f), forward), 1.0, 1e-4);
            const Vector3 toTheRight = SkySystem::cubeDirection(face, 0.75f, 0.5f);
            CHECK_NEAR(Dot(toTheRight, Unit(forward - right * 0.5f)), 1.0, 1e-4);
            const Vector3 below = SkySystem::cubeDirection(face, 0.5f, 0.75f);
            CHECK_NEAR(Dot(below, Unit(forward - up * 0.5f)), 1.0, 1e-4);
        }
    }

    CASE("probes stand in the carriageway, out of the junction box, at the pitch asked for");
    {
        RenderSettings settings;
        settings.probeSpacing = 24.0f;
        const std::vector<Vector3> probes = CityScene::probePositions(settings);
        CHECK_MSG(probes.size() >= 20 && probes.size() <= 60,
                  "a 260 m street at 24 m pitch wants a few dozen probes, not a handful or a "
                  "thousand");
        int inJunction = 0;
        for (const Vector3& p : probes)
        {
            const bool onMain = std::fabs(p.X) < Metrics::kMainCarriagewayWidth * 0.5f
                                && std::fabs(p.Z) <= Metrics::kMainStreetHalfLength;
            const bool onSide = std::fabs(p.Z) < Metrics::kSideCarriagewayWidth * 0.5f
                                && std::fabs(p.X) <= Metrics::kSideStreetHalfLength;
            CHECK_MSG(onMain || onSide, "every probe stands on a carriageway");
            CHECK_MSG(p.Y > 1.2f && p.Y < 2.0f, "a probe stands at the eye height of a car door");
            if (std::fabs(p.X) < Metrics::kMainStreetHalfWidth
                && std::fabs(p.Z) < Metrics::kSideStreetHalfWidth
                && !(p.X == 0.0f && p.Z == 0.0f))
                ++inJunction;
        }
        CHECK_MSG(inJunction == 0, "only the one probe at the junction centre sits in the box");
        // Halving the spacing roughly doubles the count.
        settings.probeSpacing = 12.0f;
        CHECK_MSG(CityScene::probePositions(settings).size() > probes.size() * 3 / 2,
                  "the spacing setting decides the count");
    }

    // ---------------------------------------------------------------------
    // Vehicles
    // ---------------------------------------------------------------------
    CASE("a brake lens is a hand tall and sits on the tail, not a slab proud of the bumper");
    {
        MaterialLibrary materials(nullptr);   // no device: materials without textures
        const VehicleFactory vehicles(materials);
        for (int t = 0; t < static_cast<int>(VehicleType::Count); ++t)
        {
            const VehicleType type = static_cast<VehicleType>(t);
            const VehicleDimensions d = VehicleFactory::dimensionsFor(type);
            GeometryCollector collector;
            collector.setRegionKey(0);
            vehicles.buildBrakeLamps(collector, type);
            const Extent e = ExtentOf(collector);
            CHECK_MSG(e.hi.Y - e.lo.Y < 0.20f, "a rear lamp cluster is under 20 cm tall");
            // Within the last 30 cm of the body, and never more than 2 cm
            // behind the rear face: the lit lens sits *on* the tail lens.
            CHECK_MSG(e.lo.Z > d.rearZ() - 0.02f && e.hi.Z < d.rearZ() + 0.32f,
                      "brake lenses lie on the tail, wrapping the corner");
            CHECK_MSG(e.hi.X < d.width * 0.5f + 0.01f, "brake lenses stay inside the body width");
        }
    }

    // ---------------------------------------------------------------------
    // Street furniture
    // ---------------------------------------------------------------------
    CASE("a bicycle is bicycle-sized and stands on its wheels");
    {
        MaterialLibrary materials(nullptr);
        const PropFactory props(materials);
        GeometryCollector collector;
        collector.setRegionKey(0);
        props.bicycle(collector, nullptr);
        const Extent e = ExtentOf(collector);
        CHECK_MSG(e.lo.Y > -0.05f && e.lo.Y < 0.03f, "the tyres touch the ground");
        CHECK_MSG(e.hi.Y > 0.90f && e.hi.Y < 1.15f, "saddle and bars at about a metre");
        CHECK_MSG(e.hi.X - e.lo.X > 1.55f && e.hi.X - e.lo.X < 1.95f,
                  "a city bicycle is about 1.75 m long");
        CHECK_MSG(e.hi.Z - e.lo.Z < 0.75f, "and less than 75 cm wide, lean included");
    }

    // ---------------------------------------------------------------------
    // Weathering and interiors
    // ---------------------------------------------------------------------
    CASE("run-off hangs from the sill and splash rises from the pavement");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::grimeDecals(64, 5u);
        // Cell 0: heavier in the top quarter than the bottom quarter.
        CHECK_MSG(meanAlpha(maps.albedo, 0, 0.0f, 0.25f) > meanAlpha(maps.albedo, 0, 0.75f, 1.0f) * 1.5,
                  "rain run-off is densest just under the sill and fades downwards");
        // Cell 1: the other way round.
        CHECK_MSG(meanAlpha(maps.albedo, 1, 0.75f, 1.0f) > meanAlpha(maps.albedo, 1, 0.0f, 0.25f) * 1.5,
                  "splash dirt is densest at the pavement and gone by half height");
        // Every cell has something in it and nothing is opaque everywhere.
        for (int cell = 0; cell < 4; ++cell)
        {
            const double a = meanAlpha(maps.albedo, cell, 0.0f, 1.0f);
            CHECK_MSG(a > 0.02 && a < 0.7, "a weathering decal is a partial film, not a paint job");
        }
    }

    CASE("a shelf of stock is mostly pale card, not a paint chart");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::shopStock(128, 3u);
        int pale = 0, total = 0;
        double saturation = 0.0;
        for (int y = 0; y < maps.albedo.height(); y += 2)
            for (int x = 0; x < maps.albedo.width(); x += 2)
            {
                const float* px = maps.albedo.at(x, y);
                const float hi = std::max({px[0], px[1], px[2]});
                const float lo = std::min({px[0], px[1], px[2]});
                if (hi > 0.35f && hi - lo < 0.12f) ++pale;
                saturation += hi > 1e-3f ? static_cast<double>((hi - lo) / hi) : 0.0;
                ++total;
            }
        CHECK_MSG(total > 0 && static_cast<double>(pale) / total > 0.30,
                  "at least a third of a shelf is white or cream card");
        CHECK_MSG(saturation / total < 0.55, "the mean saturation of packaging is well under a "
                                             "full-chroma paint chart");
    }

    CASE("a poster atlas is opaque paper with print on it");
    {
        const Assets::SurfaceMaps maps = Assets::TextureFactory::posters(64, 9u);
        double lo = 1.0, hi = 0.0, alpha = 0.0;
        int n = 0;
        for (int y = 0; y < maps.albedo.height(); ++y)
            for (int x = 0; x < maps.albedo.width(); ++x)
            {
                const float* px = maps.albedo.at(x, y);
                const double l = (px[0] + px[1] + px[2]) / 3.0;
                lo = std::min(lo, l);
                hi = std::max(hi, l);
                alpha += static_cast<double>(px[3]);
                ++n;
            }
        CHECK_NEAR(alpha / n, 1.0, 1e-3);
        CHECK_MSG(hi - lo > 0.4, "a poster has ink on paper: dark on light or light on dark");
    }

    CASE("render is cracked a little, not crazed");
    {
        // The plaster's cracks were thresholded low enough to put a dark dash
        // every few centimetres over every wall in the city. A crack in render
        // is rare: well under two per cent of the surface at full grime.
        const float white[3] = {1.0f, 1.0f, 1.0f};
        const Assets::SurfaceMaps maps = Assets::TextureFactory::plaster(128, 13u, white, 0.9f);
        int dark = 0, n = 0;
        for (int y = 0; y < maps.albedo.height(); ++y)
            for (int x = 0; x < maps.albedo.width(); ++x)
            {
                const float* px = maps.albedo.at(x, y);
                if (px[0] < 0.62f) ++dark;   // a white render's cracks and blown patches
                ++n;
            }
        CHECK_MSG(static_cast<double>(dark) / n < 0.02, "cracks cover under two per cent of a wall");
    }

    TEST_MAIN("realism");
}
