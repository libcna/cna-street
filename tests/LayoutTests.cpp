// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The street plan, and the queries the camera and the pedestrians make of it.
 */
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Render/Material.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "TestSupport.hpp"

#include <cmath>

using namespace CnaStreet;

namespace M = CnaStreet::Metrics;

namespace {

bool overlaps(const Plot& a, const Plot& b)
{
    constexpr float kSlack = 0.05f;   // plots abut along a frontage
    return a.minX < b.maxX - kSlack && b.minX < a.maxX - kSlack
           && a.minZ < b.maxZ - kSlack && b.minZ < a.maxZ - kSlack;
}

}  // namespace

int main()
{
    CityLayout layout;
    layout.generate(20260903u);

    CASE("the district has buildings, and they are buildings");
    {
        CHECK(layout.plots().size() > 30);
        for (const Plot& plot : layout.plots())
        {
            CHECK(plot.width() >= M::kPlotWidthMin - 0.01f);
            CHECK(plot.depth() > 8.0f);
            CHECK(plot.storeys >= 1 && plot.storeys <= 8);
            CHECK(plot.height() > 3.0f && plot.height() < 32.0f);
            CHECK(plot.groundFloorHeight > 2.6f);
            CHECK(plot.upperFloorHeight > 2.4f);
            CHECK(plot.isDetailed(plot.primary));
            if (plot.hasShop) CHECK(!plot.shopName.empty());
        }
    }

    CASE("no two buildings stand in the same place");
    {
        // This is the check that was missing when the side-street frontages
        // started at the main street's building line: every corner had a
        // building generated inside the corner block.
        const std::vector<Plot>& plots = layout.plots();
        int collisions = 0;
        for (std::size_t a = 0; a < plots.size(); ++a)
            for (std::size_t b = a + 1; b < plots.size(); ++b)
                if (overlaps(plots[a], plots[b])) ++collisions;
        CHECK_MSG(collisions == 0,
                  std::to_string(collisions) + " pairs of plots overlap");
    }

    CASE("nothing is built in the road");
    {
        const float mainHalf = M::kMainStreetHalfWidth;
        const float sideHalf = M::kSideStreetHalfWidth;
        for (const Plot& plot : layout.plots())
        {
            const bool insideMain = plot.minX < mainHalf - 0.02f && plot.maxX > -mainHalf + 0.02f;
            const bool insideSide = plot.minZ < sideHalf - 0.02f && plot.maxZ > -sideHalf + 0.02f;
            CHECK_MSG(!(insideMain && plot.maxZ > -M::kMainStreetHalfLength
                        && plot.minZ < M::kMainStreetHalfLength && insideSide),
                      "a building stands in the junction");
            CHECK_MSG(!insideMain || plot.minZ >= sideHalf - 0.02f || plot.maxZ <= -sideHalf + 0.02f,
                      "a building stands in the main carriageway");
        }
    }

    CASE("there is a footway down every arm, on both sides");
    {
        CHECK(layout.footways().size() == 8);
        int mainRuns = 0;
        for (const FootwayRun& run : layout.footways())
        {
            const float dx = run.end.X - run.start.X;
            const float dz = run.end.Y - run.start.Y;
            CHECK(std::sqrt(dx * dx + dz * dz) > 20.0f);
            CHECK_NEAR(run.toKerb.X * run.toKerb.X + run.toKerb.Y * run.toKerb.Y, 1.0, 1e-4);
            CHECK(run.width > 2.0f && run.width < 5.0f);
            mainRuns += run.main ? 1 : 0;

            // The kerb direction must point at the road, i.e. toward the centre
            // line of the street the run belongs to.
            const float halfway = run.main ? run.start.X : run.start.Y;
            const float toward = run.main ? run.toKerb.X : run.toKerb.Y;
            CHECK_MSG(halfway * toward < 0.0f, "a footway's kerb faces away from the road");
        }
        CHECK(mainRuns == 4);
    }

    CASE("the ground is where the walking camera expects it");
    {
        // In the carriageway: road level. On a footway: kerb height.
        CHECK_NEAR(layout.groundHeight(0.0f, 40.0f), 0.0, 1e-4);
        CHECK_NEAR(layout.groundHeight(0.0f, 0.0f), 0.0, 1e-4);
        const float footwayX = (M::kMainCarriagewayWidth * 0.5f + M::kMainStreetHalfWidth) * 0.5f;
        CHECK_NEAR(layout.groundHeight(footwayX, 40.0f), M::kCurbHeight, 1e-4);
        CHECK_NEAR(layout.groundHeight(-footwayX, -40.0f), M::kCurbHeight, 1e-4);
        CHECK(layout.isFootway(footwayX, 40.0f));
        CHECK(!layout.isFootway(0.0f, 40.0f));
        CHECK(layout.isJunction(0.0f, 0.0f));
        CHECK(!layout.isJunction(0.0f, 40.0f));
    }

    CASE("the walking camera cannot walk into a building");
    {
        int solidPlots = 0;
        for (const Plot& plot : layout.plots())
        {
            const Microsoft::Xna::Framework::Vector2 centre = plot.centre();
            if (layout.isSolid(centre.X, 1.5f, centre.Y)) ++solidPlots;
        }
        CHECK_MSG(solidPlots == static_cast<int>(layout.plots().size()),
                  "some buildings are not solid");

        // The street itself never is.
        for (const float z : {-90.0f, -20.0f, 0.0f, 20.0f, 90.0f})
        {
            CHECK(!layout.isSolid(0.0f, 1.5f, z));
            CHECK(!layout.isSolid(4.0f, 1.5f, z));
        }
        // Nor is the sky above a building.
        for (const Plot& plot : layout.plots())
        {
            const Microsoft::Xna::Framework::Vector2 centre = plot.centre();
            CHECK(!layout.isSolid(centre.X, plot.height() + 12.0f, centre.Y));
        }
    }

    CASE("the same seed lays out the same street, a different one does not");
    {
        CityLayout a, b, c;
        a.generate(4242u);
        b.generate(4242u);
        c.generate(4243u);
        CHECK(a.plots().size() == b.plots().size());
        for (std::size_t i = 0; i < a.plots().size(); ++i)
        {
            CHECK_NEAR(a.plots()[i].minX, b.plots()[i].minX, 0.0);
            CHECK_NEAR(a.plots()[i].maxZ, b.plots()[i].maxZ, 0.0);
            CHECK(a.plots()[i].style == b.plots()[i].style);
            CHECK(a.plots()[i].shopName == b.plots()[i].shopName);
        }
        bool differs = a.plots().size() != c.plots().size();
        for (std::size_t i = 0; i < a.plots().size() && i < c.plots().size() && !differs; ++i)
            differs = std::fabs(a.plots()[i].minZ - c.plots()[i].minZ) > 1e-3f;
        CHECK_MSG(differs, "a different seed produced the same street");
    }

    CASE("the collector batches by material and by cell");
    {
        GeometryCollector collector;
        Material stone, glass;
        stone.name = "stone";
        glass.name = "glass";

        collector.setRegion(0.0f, 0.0f);
        collector.builder(&stone).addBox(Microsoft::Xna::Framework::Vector3::Zero,
                                         Microsoft::Xna::Framework::Vector3::One);
        collector.builder(&glass).addBox(Microsoft::Xna::Framework::Vector3::Zero,
                                         Microsoft::Xna::Framework::Vector3::One);
        // Same material, same cell: one batch.
        collector.builder(&stone).addBox(Microsoft::Xna::Framework::Vector3::Zero,
                                         Microsoft::Xna::Framework::Vector3::One);
        CHECK(collector.batchCount() == 2);

        // Same material, a cell away: a second batch, so it can cull separately.
        collector.setRegion(GeometryCollector::kCellSize * 3.0f, 0.0f);
        collector.builder(&stone).addBox(Microsoft::Xna::Framework::Vector3::Zero,
                                         Microsoft::Xna::Framework::Vector3::One);
        CHECK(collector.batchCount() == 3);
        CHECK(collector.triangleCount() == 12 * 4);

        std::vector<GeometryCollector::Batch> batches = collector.take();
        CHECK(batches.size() == 3);
        CHECK(collector.batchCount() == 0);
    }

    CASE("a builder reference survives asking for another material");
    {
        // A window is built from a wall, a frame and a pane at the same time.
        // An implementation that stored the builders by value in a vector
        // invalidated the first reference the moment the third material was
        // asked for, and the result was a use-after-free that looked like a
        // crash somewhere else entirely.
        GeometryCollector collector;
        Material a, b, c;
        a.name = "a"; b.name = "b"; c.name = "c";
        Geometry::MeshBuilder& first = collector.builder(&a);
        collector.builder(&b);
        collector.builder(&c);
        first.addBox(Microsoft::Xna::Framework::Vector3::Zero,
                     Microsoft::Xna::Framework::Vector3::One);
        CHECK(collector.triangleCount() == 12);
    }

    TEST_MAIN("layout");
}
