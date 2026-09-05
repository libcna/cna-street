// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/BuildingBuilder.hpp"

#include "CnaStreet/Assets/Noise.hpp"
#include "CnaStreet/Geometry/Transform.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using CnaStreet::Geometry::BoxFaces;
using CnaStreet::Geometry::MeshBuilder;
using CnaStreet::Geometry::UvMode;

namespace CnaStreet {

namespace M = Metrics;

namespace {

/// The render palette, in the order `Plot::colourIndex` selects from.
constexpr MaterialId kRenderColours[] = {
    MaterialId::RenderCream, MaterialId::RenderOchre, MaterialId::RenderSage,
    MaterialId::RenderGrey,  MaterialId::RenderTerracotta, MaterialId::RenderBlue,
    MaterialId::RenderWhite};

constexpr MaterialId kDoorColours[] = {MaterialId::DoorGreen, MaterialId::DoorRed,
                                       MaterialId::DoorBlue, MaterialId::DoorOak};

/// A rectangular panel on a façade, in the frame's own coordinates.
void Panel(MeshBuilder& builder, const FacadeFrame& frame, float u0, float v0, float u1, float v1,
           float depth)
{
    builder.addQuad(frame.at(u0, v0, depth), frame.at(u1, v0, depth), frame.at(u1, v1, depth),
                    frame.at(u0, v1, depth));
}

/// A box in façade coordinates: `u0..u1` along the wall, `v0..v1` up it, and
/// `d0..d1` out of it. The one primitive every projecting element is made of.
void FacadeBox(MeshBuilder& builder, const FacadeFrame& frame, float u0, float v0, float d0,
               float u1, float v1, float d1, const BoxFaces& faces = BoxFaces{})
{
    const Vector3 c000 = frame.at(u0, v0, d0);
    const Vector3 c100 = frame.at(u1, v0, d0);
    const Vector3 c110 = frame.at(u1, v1, d0);
    const Vector3 c010 = frame.at(u0, v1, d0);
    const Vector3 c001 = frame.at(u0, v0, d1);
    const Vector3 c101 = frame.at(u1, v0, d1);
    const Vector3 c111 = frame.at(u1, v1, d1);
    const Vector3 c011 = frame.at(u0, v1, d1);

    // Named for the local axes: "back" is against the wall, "front" toward the
    // street, "left"/"right" along it, "top"/"bottom" up it.
    if (faces.negZ) builder.addQuad(c100, c000, c010, c110);   // back, into the wall
    if (faces.posZ) builder.addQuad(c001, c101, c111, c011);   // front
    if (faces.negX) builder.addQuad(c000, c001, c011, c010);   // left
    if (faces.posX) builder.addQuad(c101, c100, c110, c111);   // right
    if (faces.posY) builder.addQuad(c010, c011, c111, c110);   // top
    if (faces.negY) builder.addQuad(c000, c100, c101, c001);   // bottom
}

/// A box attached to the wall: a cornice, a sill, a string course, a pilaster.
///
/// Two things every one of them needs and none of them should have to remember.
/// It starts 1 cm *inside* the wall rather than exactly on it, because two
/// coplanar surfaces fighting for the same depth is what turned the first
/// version of these façades into a mess of flickering horizontal stripes. And it
/// omits the face against the wall, which nothing can ever see.
/// A roof slope. Always wound to face upward, because the corner order here is
/// derived from the plot's own extents and comes out inverted on half of them.
void AddRoofQuad(MeshBuilder& builder, const Vector3& a, const Vector3& b, const Vector3& c,
                 const Vector3& d)
{
    builder.addQuadFacing(a, b, c, d, Vector3::Up);
}

void WallBox(MeshBuilder& builder, const FacadeFrame& frame, float u0, float v0, float u1,
             float v1, float projection)
{
    BoxFaces faces;
    faces.negZ = false;
    FacadeBox(builder, frame, u0, v0, -0.010f, u1, v1, projection, faces);
}

/// A box on the wall with its four front edges chamfered.
///
/// The one change that most separates built stone and joinery from an
/// extrusion. A real sill, cornice, pilaster or frame member has an arris of a
/// centimetre or two that catches a highlight along its whole length and
/// throws a soft line of shade under it; a box with a mathematically sharp
/// edge catches nothing, and a facade made of them reads as a drawing however
/// good the material on it is. `chamfer` is the size of the cut, clamped so a
/// thin member keeps a front face. The back face, against the wall, is left
/// out as `WallBox` leaves it out.
void BevelBox(MeshBuilder& builder, const FacadeFrame& frame, float u0, float v0, float d0,
              float u1, float v1, float d1, float chamfer)
{
    const float c = std::min({chamfer, (u1 - u0) * 0.45f, (v1 - v0) * 0.45f, (d1 - d0) * 0.9f});
    if (c <= 1e-4f)
    {
        BoxFaces faces;
        faces.negZ = false;
        FacadeBox(builder, frame, u0, v0, d0, u1, v1, d1, faces);
        return;
    }
    const float dc = d1 - c;
    // The four sides, up to where the chamfer begins.
    builder.addQuad(frame.at(u0, v0, d0), frame.at(u0, v0, dc), frame.at(u0, v1, dc),
                    frame.at(u0, v1, d0));
    builder.addQuad(frame.at(u1, v0, dc), frame.at(u1, v0, d0), frame.at(u1, v1, d0),
                    frame.at(u1, v1, dc));
    builder.addQuad(frame.at(u0, v1, d0), frame.at(u0, v1, dc), frame.at(u1, v1, dc),
                    frame.at(u1, v1, d0));
    builder.addQuad(frame.at(u0, v0, d0), frame.at(u1, v0, d0), frame.at(u1, v0, dc),
                    frame.at(u0, v0, dc));
    // The front, inset by the chamfer.
    builder.addQuad(frame.at(u0 + c, v0 + c, d1), frame.at(u1 - c, v0 + c, d1),
                    frame.at(u1 - c, v1 - c, d1), frame.at(u0 + c, v1 - c, d1));
    // The four chamfers, sharing their corner edges so the surface closes.
    builder.addQuad(frame.at(u0, v0, dc), frame.at(u0 + c, v0 + c, d1),
                    frame.at(u0 + c, v1 - c, d1), frame.at(u0, v1, dc));
    builder.addQuad(frame.at(u1 - c, v0 + c, d1), frame.at(u1, v0, dc), frame.at(u1, v1, dc),
                    frame.at(u1 - c, v1 - c, d1));
    builder.addQuad(frame.at(u0, v1, dc), frame.at(u0 + c, v1 - c, d1),
                    frame.at(u1 - c, v1 - c, d1), frame.at(u1, v1, dc));
    builder.addQuad(frame.at(u0, v0, dc), frame.at(u1, v0, dc), frame.at(u1 - c, v0 + c, d1),
                    frame.at(u0 + c, v0 + c, d1));
}

/// `WallBox` with the arris `BevelBox` gives it: what every cornice, string
/// course, sill and pilaster on the street is now made of.
void WallBevel(MeshBuilder& builder, const FacadeFrame& frame, float u0, float v0, float u1,
               float v1, float projection, float chamfer)
{
    BevelBox(builder, frame, u0, v0, -0.010f, u1, v1, projection, chamfer);
}

}  // namespace

int BuildingBuilder::renderColourCount()
{
    return static_cast<int>(std::size(kRenderColours));
}

BuildingBuilder::BuildingBuilder(MaterialLibrary& materials, const CityLayout& layout)
    : materials_(materials), layout_(layout)
{
}

BuildingBuilder::Palette BuildingBuilder::paletteFor(const Plot& plot, Rng& rng) const
{
    Palette palette;
    const int colour = std::clamp(plot.colourIndex, 0, renderColourCount() - 1);

    palette.glass     = &materials_.get(MaterialId::Glazing);
    palette.shopGlass = &materials_.get(MaterialId::ShopGlazing);
    palette.interior  = &materials_.get(MaterialId::Interior);
    palette.metal     = &materials_.get(MaterialId::RoofZinc);
    palette.fascia    = &materials_.get(MaterialId::ShopFascia);
    palette.door      = &materials_.get(kDoorColours[rng.index(std::size(kDoorColours))]);

    switch (plot.style)
    {
        case BuildingStyle::Gruenderzeit:
            palette.wall   = &materials_.get(kRenderColours[colour]);
            palette.plinth = &materials_.get(MaterialId::Ashlar);
            palette.trim   = &materials_.get(MaterialId::RenderWhite);
            palette.frame  = &materials_.get(MaterialId::FrameWhite);
            palette.roof   = &materials_.get(MaterialId::RoofTile);
            break;
        case BuildingStyle::BrickWarehouse:
            palette.wall   = &materials_.get(rng.chance(0.6f) ? MaterialId::BrickRed
                                                              : MaterialId::BrickBuff);
            palette.plinth = &materials_.get(MaterialId::BrickEngineering);
            palette.trim   = &materials_.get(MaterialId::Ashlar);
            palette.frame  = &materials_.get(MaterialId::FrameDark);
            palette.roof   = &materials_.get(MaterialId::RoofTile);
            break;
        case BuildingStyle::PostWar:
            palette.wall   = &materials_.get(kRenderColours[colour]);
            palette.plinth = &materials_.get(MaterialId::ConcretePanel);
            palette.trim   = &materials_.get(MaterialId::ConcretePanel);
            palette.frame  = &materials_.get(MaterialId::FrameWhite);
            palette.roof   = &materials_.get(MaterialId::RoofFelt);
            break;
        case BuildingStyle::ModernOffice:
            palette.wall   = &materials_.get(MaterialId::ConcretePanel);
            palette.plinth = &materials_.get(MaterialId::ConcretePanel);
            palette.trim   = &materials_.get(MaterialId::ConcretePanel);
            palette.frame  = &materials_.get(MaterialId::FrameBronze);
            palette.roof   = &materials_.get(MaterialId::RoofFelt);
            break;
        case BuildingStyle::ShopUnit:
            palette.wall   = &materials_.get(kRenderColours[colour]);
            palette.plinth = &materials_.get(MaterialId::ConcretePanel);
            palette.trim   = &materials_.get(MaterialId::RenderWhite);
            palette.frame  = &materials_.get(MaterialId::FrameWhite);
            palette.roof   = &materials_.get(MaterialId::RoofFelt);
            break;
        case BuildingStyle::CornerBlock:
            palette.wall   = &materials_.get(rng.chance(0.45f) ? MaterialId::Ashlar
                                                               : kRenderColours[colour]);
            palette.plinth = &materials_.get(MaterialId::Ashlar);
            palette.trim   = &materials_.get(MaterialId::Ashlar);
            palette.frame  = &materials_.get(MaterialId::FrameWhite);
            palette.roof   = &materials_.get(MaterialId::RoofZinc);
            break;
    }
    return palette;
}

FacadeFrame BuildingBuilder::frameFor(const Plot& plot, Facing facing) const
{
    FacadeFrame frame;
    frame.up = Vector3::Up;
    frame.height = plot.height();

    switch (facing)
    {
        case Facing::PosX:
            frame.out    = Vector3(1.0f, 0.0f, 0.0f);
            frame.origin = Vector3(plot.maxX, 0.0f, plot.maxZ);
            frame.width  = plot.depth();
            break;
        case Facing::NegX:
            frame.out    = Vector3(-1.0f, 0.0f, 0.0f);
            frame.origin = Vector3(plot.minX, 0.0f, plot.minZ);
            frame.width  = plot.depth();
            break;
        case Facing::PosZ:
            frame.out    = Vector3(0.0f, 0.0f, 1.0f);
            frame.origin = Vector3(plot.minX, 0.0f, plot.maxZ);
            frame.width  = plot.width();
            break;
        case Facing::NegZ:
            frame.out    = Vector3(0.0f, 0.0f, -1.0f);
            frame.origin = Vector3(plot.maxX, 0.0f, plot.minZ);
            frame.width  = plot.width();
            break;
    }
    // Right is up x out, which puts u increasing to the right as seen from the
    // street for all four elevations.
    frame.right = Vector3::Cross(frame.up, frame.out);
    return frame;
}

void BuildingBuilder::build(const Plot& plot, int plotIndex, GeometryCollector& collector,
                            GeometryCollector& interiors, Rng& rng,
                            std::vector<FacadeAnchor>& anchors,
                            std::vector<ShopDisplay>& displays)
{
    // One region per building: a plot is a natural unit for culling, and it
    // keeps a whole elevation in one draw call per material.
    const Vector2 centre = plot.centre();
    collector.setRegion(centre.X, centre.Y);
    interiors.setRegion(centre.X, centre.Y);

    const Palette palette = paletteFor(plot, rng);
    weathering_ = plot.weathering;
    plotSeed_   = plot.seed;
    style_      = plot.style;
    flatFacade_ = plot.style == BuildingStyle::PostWar || plot.style == BuildingStyle::ModernOffice
                  || plot.style == BuildingStyle::ShopUnit;
    {
        Rng ornament = Rng::derive(plot.seed, "ornament");
        shutters_ = plot.style == BuildingStyle::Gruenderzeit && ornament.chance(0.45f);
        quoins_   = (plot.style == BuildingStyle::Gruenderzeit
                     || plot.style == BuildingStyle::CornerBlock)
                    && ornament.chance(0.5f);
    }
    if (netCurtain_ == nullptr)
    {
        // What windows show besides the room: a net curtain is pale and
        // matt, a blind a shade darker and greyer, and a room with the
        // light off is the atlas cell at a third of its glow.
        netCurtain_   = materials_.deriveTinted("net-curtain", MaterialId::ShopBlind,
                                                Vector3(0.80f, 0.78f, 0.72f));
        windowBlind_  = materials_.deriveTinted("window-blind", MaterialId::ShopBlind,
                                                Vector3(0.58f, 0.56f, 0.52f));
        const Material& interior = materials_.get(MaterialId::Interior);
        interiorDark_ = materials_.deriveTinted("interior-dark", MaterialId::Interior,
                                                interior.baseColour * 0.55f,
                                                interior.emissiveFactor * 0.30f);
    }

    buildMass(plot, palette, collector, rng);

    for (int face = 0; face < 4; ++face)
    {
        const Facing facing = static_cast<Facing>(face);
        const FacadeFrame frame = frameFor(plot, facing);
        if (plot.isDetailed(facing))
            buildFacade(plot, plotIndex, frame, palette, collector, rng, interiors, anchors,
                        displays, facing == plot.primary);
        else
            // Windows even on the elevations that do not front a street. Most of
            // these are party walls nobody can see, but the ones at the end of a
            // terrace and the rear elevations across a courtyard are visible from
            // the junction and from any camera above eye level, and a seventeen
            // metre blank wall is the loudest thing in the picture.
            buildPlainWall(frame, palette, collector, rng, /*addWindows=*/true);
    }

    buildRoof(plot, palette, collector, rng);
}

void BuildingBuilder::buildMass(const Plot& plot, const Palette& palette,
                                GeometryCollector& collector, Rng& rng)
{
    (void)rng;
    // The plinth: a heavier base course that runs round the whole building and
    // sits proud of the wall above it. It is what stops a façade looking like it
    // was extruded from the pavement.
    MeshBuilder& plinth = collector.builder(palette.plinth);
    plinth.setTileSize(2.0f);
    const float p = M::kPlinthProjection;
    plinth.addBox(Vector3(plot.minX - p, 0.0f, plot.minZ - p),
                  Vector3(plot.maxX + p, M::kCurbHeight + M::kPlinthHeight, plot.maxZ + p),
                  BoxFaces::sides());
    // The weathering course on top of the plinth.
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    trim.addBox(Vector3(plot.minX - p, M::kCurbHeight + M::kPlinthHeight, plot.minZ - p),
                Vector3(plot.maxX + p, M::kCurbHeight + M::kPlinthHeight + 0.05f, plot.maxZ + p),
                BoxFaces::allButBottom());
}

void BuildingBuilder::buildPlainWall(const FacadeFrame& frame, const Palette& palette,
                                     GeometryCollector& collector, Rng& rng, bool addWindows)
{
    dressing_ = Rng::derive(plotSeed_, "dressing-plain");
    MeshBuilder& wall = collector.builder(palette.wall);
    wall.setTileSize(2.0f);
    Panel(wall, frame, 0.0f, M::kCurbHeight + M::kPlinthHeight, frame.width, frame.height, 0.0f);

    if (!addWindows) return;
    // A rear or flank elevation: plainer than a street front -- smaller openings,
    // no architrave, no balcony -- but a real opening all the same, recessed with
    // a frame and a sill. A pane painted flat on the wall reads as a poster.
    const int bays = std::max(1, static_cast<int>(frame.width / 3.4f));
    const float pitch = frame.width / static_cast<float>(bays);
    const float reveal = 0.10f;
    MeshBuilder& sash = collector.builder(palette.frame);
    sash.setTileSize(0.5f);
    MeshBuilder& glass = collector.builder(palette.glass);
    glass.setTileSize(1.4f);
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);

    for (int bay = 0; bay < bays; ++bay)
        for (float v = M::kGroundFloorHeight; v < frame.height - 1.9f; v += M::kUpperFloorHeight)
        {
            if (!rng.chance(0.72f)) continue;
            const float u0 = (static_cast<float>(bay) + 0.5f) * pitch - 0.50f;
            const float u1 = u0 + 1.00f;
            const float v0 = v + 0.85f;
            const float v1 = v0 + 1.35f;

            wall.addQuad(frame.at(u0, v0, 0.0f), frame.at(u0, v0, -reveal),
                         frame.at(u0, v1, -reveal), frame.at(u0, v1, 0.0f));
            wall.addQuad(frame.at(u1, v0, -reveal), frame.at(u1, v0, 0.0f),
                         frame.at(u1, v1, 0.0f), frame.at(u1, v1, -reveal));
            wall.addQuad(frame.at(u0, v1, 0.0f), frame.at(u0, v1, -reveal),
                         frame.at(u1, v1, -reveal), frame.at(u1, v1, 0.0f));
            wall.addQuad(frame.at(u0, v0, -reveal), frame.at(u0, v0, 0.0f),
                         frame.at(u1, v0, 0.0f), frame.at(u1, v0, -reveal));

            const float glassDepth = -reveal + 0.04f;
            FacadeBox(sash, frame, u0, v0, glassDepth - 0.02f, u0 + 0.05f, v1, glassDepth + 0.02f);
            FacadeBox(sash, frame, u1 - 0.05f, v0, glassDepth - 0.02f, u1, v1, glassDepth + 0.02f);
            FacadeBox(sash, frame, u0, v0, glassDepth - 0.02f, u1, v0 + 0.05f, glassDepth + 0.02f);
            FacadeBox(sash, frame, u0, v1 - 0.05f, glassDepth - 0.02f, u1, v1, glassDepth + 0.02f);
            Panel(glass, frame, u0 + 0.02f, v0 + 0.02f, u1 - 0.02f, v1 - 0.02f, glassDepth);
            FacadeBox(trim, frame, u0 - 0.04f, v0 - 0.07f, -reveal, u1 + 0.04f, v0, 0.04f);
        }
}

void BuildingBuilder::buildWindow(const FacadeFrame& frame, float u, float v, float width,
                                  float height, const Palette& palette,
                                  GeometryCollector& collector, Rng& rng, bool arched,
                                  std::vector<Opening>& openings)
{
    openings.push_back(Opening{u, v, u + width, v + height});
    const float reveal = M::kWindowReveal;
    const float frameWidth = M::kWindowFrame;

    // --- the reveal ------------------------------------------------------
    // Four narrow strips cut back into the wall. This is the whole trick: an
    // opening with depth catches a shadow along one edge and a highlight along
    // the other, and that shading is what the eye reads as a window rather than
    // a painted rectangle.
    MeshBuilder& wall = collector.builder(palette.wall);
    wall.setTileSize(2.0f);
    const float u1 = u + width, v1 = v + height;
    // left, right, head, sill soffit
    wall.addQuad(frame.at(u, v, 0.0f), frame.at(u, v, -reveal), frame.at(u, v1, -reveal),
                 frame.at(u, v1, 0.0f));
    wall.addQuad(frame.at(u1, v, -reveal), frame.at(u1, v, 0.0f), frame.at(u1, v1, 0.0f),
                 frame.at(u1, v1, -reveal));
    wall.addQuad(frame.at(u, v1, 0.0f), frame.at(u, v1, -reveal), frame.at(u1, v1, -reveal),
                 frame.at(u1, v1, 0.0f));
    wall.addQuad(frame.at(u, v, -reveal), frame.at(u, v, 0.0f), frame.at(u1, v, 0.0f),
                 frame.at(u1, v, -reveal));

    // --- what the window shows ---------------------------------------------
    // A street's windows are not sixty holes into sixty lit rooms. Behind
    // most of them is something: a net curtain across the whole pane or
    // drawn to the sides, a roller blind part way down, a room with the
    // light off; and on the rendered blocks that have them, a pair of
    // shutters folded back beside the opening or, on one window in eight,
    // closed over it. Each is decided per window from the plot's stream,
    // and each puts a different tone and a different depth in the opening,
    // which is what stops the upper floors reading as a pattern.
    const float dress = dressing_.range(0.0f, 1.0f);
    const bool net      = dress < 0.28f;
    const bool halfNet  = dress >= 0.28f && dress < 0.42f;
    const bool blind    = dress >= 0.42f && dress < 0.58f;
    const bool unlit    = dress >= 0.58f && dress < 0.82f;
    const bool shuttered = shutters_ && !flatFacade_;
    const bool closed   = shuttered && dressing_.chance(0.12f);
    const float centre  = u + width * 0.5f;

    // One cell of the interior atlas, chosen per window and sometimes mirrored,
    // so a façade of sixty windows does not show the same room sixty times.
    const int cell = rng.intRange(0, 15);
    const float cellU = static_cast<float>(cell % 4) * 0.25f;
    const float cellV = static_cast<float>(cell / 4) * 0.25f;
    const bool mirrored = rng.chance(0.5f);
    if (!closed)
    {
        MeshBuilder& interior = collector.builder(unlit && interiorDark_ != nullptr
                                                      ? interiorDark_ : palette.interior);
        interior.setUvMode(UvMode::Explicit);
        const std::size_t firstVertex = interior.vertexCount();
        Panel(interior, frame, u + frameWidth, v + frameWidth, u1 - frameWidth, v1 - frameWidth,
              -reveal + 0.012f);
        interior.offsetUv(firstVertex, Vector2(mirrored ? -0.25f : 0.25f, 0.25f),
                          Vector2(mirrored ? cellU + 0.25f : cellU, cellV));
    }
    if (!closed && (net || halfNet) && netCurtain_ != nullptr)
    {
        MeshBuilder& curtain = collector.builder(netCurtain_);
        curtain.setTileSize(0.45f);
        const float cd = -reveal + 0.026f;
        if (net)
            Panel(curtain, frame, u + frameWidth + 0.01f, v + frameWidth + 0.02f,
                  u1 - frameWidth - 0.01f, v1 - frameWidth - 0.01f, cd);
        else
        {
            const float leaf = (width - frameWidth * 2.0f) * dressing_.range(0.24f, 0.36f);
            Panel(curtain, frame, u + frameWidth + 0.01f, v + frameWidth + 0.02f,
                  u + frameWidth + 0.01f + leaf, v1 - frameWidth - 0.01f, cd);
            Panel(curtain, frame, u1 - frameWidth - 0.01f - leaf, v + frameWidth + 0.02f,
                  u1 - frameWidth - 0.01f, v1 - frameWidth - 0.01f, cd);
        }
    }
    if (!closed && blind && windowBlind_ != nullptr)
    {
        MeshBuilder& roller = collector.builder(windowBlind_);
        roller.setTileSize(0.6f);
        const float drop = (height - frameWidth * 2.0f) * dressing_.range(0.30f, 0.70f);
        const float bd = -reveal + 0.030f;
        Panel(roller, frame, u + frameWidth + 0.005f, v1 - frameWidth - drop, u1 - frameWidth - 0.005f,
              v1 - frameWidth, bd);
        MeshBuilder& bar = collector.builder(palette.frame);
        bar.setTileSize(0.5f);
        FacadeBox(bar, frame, u + frameWidth, v1 - frameWidth - drop - 0.025f, bd - 0.012f,
                  u1 - frameWidth, v1 - frameWidth - drop + 0.005f, bd + 0.012f);
    }

    // --- the frame ---------------------------------------------------------
    MeshBuilder& sash = collector.builder(palette.frame);
    sash.setTileSize(0.5f);
    const float glassDepth = -reveal + 0.055f;
    const float outer = glassDepth - 0.028f;
    // Outer frame: four members around the opening.
    FacadeBox(sash, frame, u, v, outer, u + frameWidth, v1, glassDepth + 0.02f);
    FacadeBox(sash, frame, u1 - frameWidth, v, outer, u1, v1, glassDepth + 0.02f);
    FacadeBox(sash, frame, u, v, outer, u1, v + frameWidth, glassDepth + 0.02f);
    FacadeBox(sash, frame, u, v1 - frameWidth, outer, u1, v1, glassDepth + 0.02f);
    // A central mullion and a transom: two casements with a fanlight, which is
    // what almost every window on a street like this is.
    const float mullion = u + width * 0.5f - frameWidth * 0.4f;
    FacadeBox(sash, frame, mullion, v, outer, mullion + frameWidth * 0.8f, v1,
              glassDepth + 0.015f);
    if (height > 1.3f)
    {
        const float transom = v + height * (arched ? 0.70f : 0.74f);
        FacadeBox(sash, frame, u, transom, outer, u1, transom + frameWidth * 0.8f,
                  glassDepth + 0.015f);
    }

    // --- the glass ---------------------------------------------------------
    if (!closed)
    {
        MeshBuilder& glass = collector.builder(palette.glass);
        glass.setTileSize(1.4f);
        Panel(glass, frame, u + frameWidth * 0.5f, v + frameWidth * 0.5f, u1 - frameWidth * 0.5f,
              v1 - frameWidth * 0.5f, glassDepth);
    }

    // --- the shutters ---------------------------------------------------------
    // A leaf each side, folded back flat against the wall, in the plot's
    // door colour: a panel with a rail top, middle and bottom, an arris on
    // its edges. Closed, the two leaves meet in the reveal in front of the
    // glass. They stop short of a neighbour and of the corner, so a leaf
    // never crosses a quoin.
    if (shuttered)
    {
        MeshBuilder& leafBuilder = collector.builder(palette.door);
        leafBuilder.setTileSize(0.8f);
        const float room = std::min(u - (quoins_ ? 0.52f : 0.10f), frame.width - u1 - (quoins_ ? 0.52f : 0.10f));
        const float leaf = std::min({width * 0.46f, 0.52f, room - 0.05f});
        const auto shutterLeaf = [&](float l0, float l1, float d0, float d1) {
            BevelBox(leafBuilder, frame, l0, v + 0.02f, d0, l1, v1 - 0.02f, d1, 0.008f);
            for (const float rv : {v + 0.06f, (v + v1) * 0.5f - 0.045f, v1 - 0.15f})
                FacadeBox(leafBuilder, frame, l0 + 0.03f, rv, d1 - 0.004f, l1 - 0.03f, rv + 0.09f,
                          d1 + 0.014f);
        };
        if (closed)
        {
            const float d0 = -reveal + 0.065f, d1 = -reveal + 0.105f;
            shutterLeaf(u + 0.015f, centre - 0.008f, d0, d1);
            shutterLeaf(centre + 0.008f, u1 - 0.015f, d0, d1);
        }
        else if (leaf >= 0.22f)
        {
            shutterLeaf(u - 0.05f - leaf, u - 0.05f, -0.01f, 0.038f);
            shutterLeaf(u1 + 0.05f, u1 + 0.05f + leaf, -0.01f, 0.038f);
        }
    }

    // --- the sill ----------------------------------------------------------
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    if (flatFacade_)
    {
        // A projecting surround instead of a sill: four members around the
        // opening, a hand wide and a hand proud, chamfered. This is what a
        // flat post-war elevation has instead of mouldings, and without it
        // the wall is a plane with holes in it -- no shadow on the reveal's
        // sunny side, no highlight on its edge, nothing for the eye to
        // measure depth by.
        const float band = 0.11f, out = 0.09f, arris = 0.016f;
        BevelBox(trim, frame, u - band, v - band, -0.01f, u, v1 + band, out, arris);
        BevelBox(trim, frame, u1, v - band, -0.01f, u1 + band, v1 + band, out, arris);
        BevelBox(trim, frame, u, v1, -0.01f, u1, v1 + band, out, arris);
        BevelBox(trim, frame, u - band, v - band - 0.03f, -reveal, u1 + band, v, out + 0.02f,
                 arris);
    }
    else
    {
        BevelBox(trim, frame, u - 0.055f, v - 0.085f, -reveal, u1 + 0.055f, v, 0.065f, 0.018f);
        // A moulded architrave over the head on the piano nobile, a flat
        // hood above it, and on the rendered and ashlar blocks a keystone
        // standing proud of either: the one ornament that reads from the
        // street, because it breaks the straight line of the head.
        const bool masonry = style_ == BuildingStyle::Gruenderzeit
                             || style_ == BuildingStyle::CornerBlock;
        if (arched)
            BevelBox(trim, frame, u - 0.075f, v1, -reveal, u1 + 0.075f, v1 + 0.14f, 0.075f,
                     0.022f);
        else
            BevelBox(trim, frame, u - 0.06f, v1 + 0.01f, -0.01f, u1 + 0.06f, v1 + 0.10f, 0.05f,
                     0.015f);
        if (masonry)
            BevelBox(trim, frame, centre - 0.075f, v1 - 0.01f, -0.01f, centre + 0.075f,
                     v1 + (arched ? 0.20f : 0.16f), arched ? 0.10f : 0.075f, 0.012f);
    }

    // The rain that runs off the sill and down the wall under it. Where the
    // water goes is the one thing a tiling wall material cannot know, and it is
    // what makes a facade read as weathered rather than as noisy: the streaks
    // hang *from* something. More of them on a grubby building, none on a
    // freshly painted one.
    if (rng.chance(0.15f + weathering_ * 0.55f))
    {
        const float drop = rng.range(0.35f, 0.75f) * (0.6f + weathering_);
        grimeDecal(frame, u - 0.12f, v - 0.085f - drop, u1 + 0.12f, v - 0.085f, 0, collector,
                   rng.chance(0.5f));
    }
}

ShopKind BuildingBuilder::shopKindFor(const Plot& plot, int plotIndex)
{
    // From the plot and its index alone, so the fascia lettering, the fittings
    // and the window display are all decided by the same number and cannot
    // disagree. A shoe shop with a bread counter behind the glass is worse than
    // an empty one.
    static const ShopKind kMix[] = {
        ShopKind::Bakery,     ShopKind::Clothing, ShopKind::Convenience, ShopKind::Bakery,
        ShopKind::Electrical, ShopKind::Clothing, ShopKind::Optician,    ShopKind::Convenience,
        ShopKind::Florist,    ShopKind::Office,   ShopKind::Furniture,   ShopKind::Clothing,
        ShopKind::Bakery,     ShopKind::Vacant,   ShopKind::Convenience, ShopKind::Electrical,
    };
    const int slot = (plotIndex * 7 + static_cast<int>(plot.style) * 3 + plot.colourIndex)
                     % static_cast<int>(std::size(kMix));
    return kMix[static_cast<std::size_t>(slot < 0 ? slot + static_cast<int>(std::size(kMix))
                                                  : slot)];
}

const char* BuildingBuilder::shopKindName(ShopKind kind)
{
    switch (kind)
    {
        case ShopKind::Bakery:      return "bakery";
        case ShopKind::Clothing:    return "clothing";
        case ShopKind::Convenience: return "convenience";
        case ShopKind::Electrical:  return "electrical";
        case ShopKind::Florist:     return "florist";
        case ShopKind::Optician:    return "optician";
        case ShopKind::Furniture:   return "furniture";
        case ShopKind::Office:      return "office";
        case ShopKind::Vacant:      return "vacant";
        case ShopKind::Count:       break;
    }
    return "shop";
}

void BuildingBuilder::buildShopInterior(const FacadeFrame& frame, float u0, float u1, float floor,
                                        float ceiling, ShopKind kind, int plotIndex,
                                        GeometryCollector& collector, Rng& rng,
                                        std::vector<ShopDisplay>* displays)
{
    const float depth = -rng.range(4.2f, 6.4f);   // into the building

    // The room is painted to its trade. A bakery is warm, an optician white, an
    // electrical shop dark, and a street where every unit has the same
    // off-white walls is a street of one shop repeated. Tints are derived
    // materials over one texture, so eight trades cost nothing.
    struct Decor { Vector3 wall; float light; };
    Decor decor{Vector3(0.62f, 0.60f, 0.57f), 1.0f};
    switch (kind)
    {
        case ShopKind::Bakery:      decor = {Vector3(0.66f, 0.56f, 0.42f), 1.05f}; break;
        case ShopKind::Clothing:    decor = {Vector3(0.58f, 0.57f, 0.56f), 0.95f}; break;
        case ShopKind::Convenience: decor = {Vector3(0.64f, 0.64f, 0.60f), 1.10f}; break;
        case ShopKind::Electrical:  decor = {Vector3(0.22f, 0.23f, 0.26f), 0.75f}; break;
        case ShopKind::Florist:     decor = {Vector3(0.52f, 0.60f, 0.50f), 0.95f}; break;
        case ShopKind::Optician:    decor = {Vector3(0.70f, 0.70f, 0.70f), 1.00f}; break;
        case ShopKind::Furniture:   decor = {Vector3(0.60f, 0.52f, 0.44f), 0.85f}; break;
        case ShopKind::Office:      decor = {Vector3(0.58f, 0.60f, 0.64f), 0.90f}; break;
        case ShopKind::Vacant:      decor = {Vector3(0.50f, 0.48f, 0.45f), 0.0f}; break;
        case ShopKind::Count:       break;
    }
    const Material& wallBase = materials_.get(MaterialId::ShopWall);
    const std::string tag = shopKindName(kind);
    // Two shades of every wall: the front of the room, under the tubes and the
    // window's own daylight, and the back, which is a metre or two deeper and
    // darker than the front. A room with a gradient in it has depth; a room
    // painted one tone is a lightbox however well it is furnished.
    const Material* wallFront = materials_.deriveTinted(
        "shop-wall-" + tag, MaterialId::ShopWall, decor.wall, wallBase.emissiveFactor * decor.light);
    const Material* wallBack = materials_.deriveTinted(
        "shop-wall-back-" + tag, MaterialId::ShopWall, decor.wall * 0.62f,
        wallBase.emissiveFactor * decor.light * 0.45f);
    const Material* soffit = materials_.deriveTinted(
        "shop-ceiling-" + tag, MaterialId::ShopWall, decor.wall * 0.72f + Vector3(0.08f, 0.08f, 0.08f),
        wallBase.emissiveFactor * decor.light * 0.55f);

    // Plaster, not the interior atlas. The atlas is a four-by-four grid of *whole
    // rooms*, drawn one cell per window; stretched across the back wall of a real
    // modelled room it showed all sixteen at once, and every shopfront on the
    // street had a wall of thumbnail rooms behind the glass.
    MeshBuilder& room = collector.builder(wallFront);
    room.setTileSize(1.8f);
    MeshBuilder& back = collector.builder(wallBack);
    back.setTileSize(1.8f);
    MeshBuilder& lid = collector.builder(soffit);
    lid.setTileSize(1.8f);
    // Inward-facing, and stated as such rather than wound by hand. Deriving the
    // corner order for five faces of a box in façade coordinates is exactly the
    // sort of thing that comes out right for three of them: the first version of
    // this room had its back wall and ceiling culled, so a shopper looking in
    // saw the sky through the back of the shop.
    const Vector3 inU = frame.right;   // toward u1
    const Vector3 inV = frame.up;
    const Vector3 inW = frame.out;     // toward the street
    const float mid = depth * 0.42f;   // where the front of the room becomes the back
    // Back wall, facing the street.
    back.addQuadFacing(frame.at(u0, floor, depth), frame.at(u1, floor, depth),
                       frame.at(u1, ceiling, depth), frame.at(u0, ceiling, depth), inW);
    // Side walls, each facing across the room, in a front and a back half.
    room.addQuadFacing(frame.at(u0, floor, mid), frame.at(u0, floor, 0.0f),
                       frame.at(u0, ceiling, 0.0f), frame.at(u0, ceiling, mid), inU);
    back.addQuadFacing(frame.at(u0, floor, depth), frame.at(u0, floor, mid),
                       frame.at(u0, ceiling, mid), frame.at(u0, ceiling, depth), inU);
    room.addQuadFacing(frame.at(u1, floor, mid), frame.at(u1, floor, 0.0f),
                       frame.at(u1, ceiling, 0.0f), frame.at(u1, ceiling, mid), inU * -1.0f);
    back.addQuadFacing(frame.at(u1, floor, depth), frame.at(u1, floor, mid),
                       frame.at(u1, ceiling, mid), frame.at(u1, ceiling, depth), inU * -1.0f);
    // Ceiling looking down, floor looking up.
    lid.addQuadFacing(frame.at(u0, ceiling, depth), frame.at(u1, ceiling, depth),
                      frame.at(u1, ceiling, 0.0f), frame.at(u0, ceiling, 0.0f), inV * -1.0f);
    MeshBuilder& floorSurface = collector.builder(&materials_.get(MaterialId::ShopFloor));
    // Eight tiles to the repeat, so 2.4 m gives 30 cm floor tiles -- a shop
    // floor, not a footway. This tracks the paving generator's cell count: it
    // went from three cells to eight to stop a footway reading as a repeating
    // three-by-three block, and every surface that shares the generator has to
    // move its tile size with it or the joints change size underneath it.
    floorSurface.setTileSize(2.4f);
    floorSurface.addQuadFacing(frame.at(u0, floor, depth), frame.at(u1, floor, depth),
                               frame.at(u1, floor, 0.0f), frame.at(u0, floor, 0.0f), inV);

    if (kind == ShopKind::Vacant)
    {
        // A unit to let: a bare floor, a paper notice taped inside the glass,
        // and nothing else. One of these on a street of sixteen is worth more
        // than a sixteenth shop full of stock -- it is the thing that says
        // somebody owns these buildings and one of them is between tenants.
        MeshBuilder& notice = collector.builder(&materials_.get(MaterialId::ShopFitting));
        notice.setTileSize(0.5f);
        const float nu = (u0 + u1) * 0.5f;
        FacadeBox(notice, frame, nu - 0.21f, floor + 1.35f, -0.14f, nu + 0.21f, floor + 1.65f,
                  -0.13f);
        return;
    }

    // --- the lit ceiling ----------------------------------------------------
    // Two strips of light on the soffit, and they are the single most valuable
    // thing in here. A shop interior is *brighter* than the street outside it,
    // and an unlit one behind glass reads as a cupboard however well it is
    // furnished -- which is exactly what the first version of these looked like.
    {
        MeshBuilder& lamp = collector.builder(&materials_.get(MaterialId::ShopCeilingLight));
        lamp.setUvMode(UvMode::Explicit);
        // Tubes in batten fittings: a hand wide, not a quarter of a metre, and
        // broken into lengths with a gap between them the way a run of fittings
        // is. Two continuous strips the width of the shop read as a light box
        // rather than a lit ceiling.
        for (int i = 0; i < 2; ++i)
        {
            const float lv = ceiling - 0.06f;
            const float ld = depth * (0.30f + 0.36f * static_cast<float>(i));
            float a = u0 + 0.55f;
            while (a < u1 - 0.75f)
            {
                const float b = std::min(a + 1.50f, u1 - 0.55f);
                lamp.addQuadFacingUv(frame.at(a, lv, ld - 0.075f), frame.at(b, lv, ld - 0.075f),
                                     frame.at(b, lv, ld + 0.075f), frame.at(a, lv, ld + 0.075f),
                                     inV * -1.0f);
                a = b + 0.22f;
            }
        }
    }

    // Posters and notices on the walls: a cheap, legible sign that a room is
    // occupied, and the one thing that varies from one unit to the next that a
    // passer-by actually reads.
    MeshBuilder& posters = collector.builder(&materials_.get(MaterialId::ShopPoster));
    posters.setUvMode(UvMode::Explicit);
    const auto poster = [&](float u, float v, float w, float h, float d, const Vector3& facing,
                            bool onSideWall) {
        const int cell = rng.intRange(0, 3);
        const float cu = static_cast<float>(cell % 2) * 0.5f;
        const float cv = static_cast<float>(cell / 2) * 0.5f;
        if (!onSideWall)
            posters.addQuadUv(frame.at(u, v, d), frame.at(u + w, v, d), frame.at(u + w, v + h, d),
                              frame.at(u, v + h, d), Vector2(cu, cv + 0.5f), Vector2(cu + 0.5f, cv + 0.5f),
                              Vector2(cu + 0.5f, cv), Vector2(cu, cv));
        else
            // On a side wall `u` is fixed and the poster runs along the depth.
            posters.addQuadFacingUv(frame.at(u, v, d), frame.at(u, v, d + w),
                                    frame.at(u, v + h, d + w), frame.at(u, v + h, d), facing);
        (void)facing;
    };

    MeshBuilder& fittings = collector.builder(&materials_.get(MaterialId::ShopFitting));
    fittings.setTileSize(1.0f);
    MeshBuilder& timber = collector.builder(&materials_.get(MaterialId::ShopTimber));
    // 0.62 m rather than 1.1: a shop counter is faced in boards, and a grain
    // that repeats once every metre reads as tiger stripe rather than as wood.
    timber.setTileSize(0.62f);
    // The stock a stop darker and a third less saturated than its texture
    // draws it: through glass at three metres the full-colour grid of
    // packets was the loudest surface on the street, a wallpaper rather
    // than a shelf, and product is darker than the wall it stands against.
    const Material& stockBase = materials_.get(MaterialId::ShopStock);
    MeshBuilder& stock = collector.builder(materials_.deriveTinted(
        "shop-stock-muted", MaterialId::ShopStock, Vector3(0.62f, 0.60f, 0.58f),
        stockBase.emissiveFactor * 0.55f));
    stock.setTileSize(0.34f);

    // A run of shelving against a wall, loaded with product. The product is what
    // does the work: four shelves of small blocks at slightly different depths
    // and heights reads, through glass at three metres, as a shop. A bare shelf
    // reads as a bookcase in an empty flat.
    const auto shelving = [&](float a, float b, float d, int levels, float height) {
        if (b - a < 0.5f) return;
        const float thickness = 0.045f;
        FacadeBox(fittings, frame, a, floor, d, b, floor + 0.10f, d + 0.42f);
        for (int level = 0; level < levels; ++level)
        {
            const float v = floor + 0.28f
                            + (height - 0.28f) * static_cast<float>(level)
                                  / static_cast<float>(levels);
            FacadeBox(fittings, frame, a, v, d, b, v + thickness, d + 0.42f);
            // A *run* of packets rather than a packet, because the stock
            // texture already draws the individual packets and their seams:
            // one box per packet was ten thousand boxes across the street, a
            // hundred and twenty thousand triangles, and through glass at three
            // metres it looked exactly like this does. Geometry supplies what
            // the texture cannot -- the breaks in the run, and the different
            // depth and height each block sits at -- and nothing else.
            float u = a + 0.04f;
            while (u < b - 0.20f)
            {
                const float w = rng.range(0.26f, 0.56f);
                if (u + w > b - 0.05f) break;
                if (rng.chance(0.86f))
                    FacadeBox(stock, frame, u, v + thickness, d + rng.range(0.04f, 0.12f), u + w,
                              v + thickness + rng.range(0.13f, 0.24f), d + rng.range(0.26f, 0.38f));
                u += w + rng.range(0.02f, 0.09f);
            }
        }
        FacadeBox(fittings, frame, a, floor, d, a + thickness, floor + height, d + 0.42f);
        FacadeBox(fittings, frame, b - thickness, floor, d, b, floor + height, d + 0.42f);
    };

    // A counter with a till on it. The body is a faced panel and only the
    // worktop is timber, which is what a shop counter is actually made of --
    // and it keeps four metres of hardwood figure out of the middle of the
    // window, where a grain that reads as oak on a bench reads as tiger stripe.
    const auto counter = [&](float a, float b, float d) {
        if (b - a < 0.5f) return;
        FacadeBox(fittings, frame, a, floor, d, b, floor + 0.92f, d + 0.62f);
        FacadeBox(timber, frame, a - 0.02f, floor + 0.92f, d - 0.03f, b + 0.02f, floor + 0.97f,
                  d + 0.66f);
        const float tu = a + (b - a) * 0.62f;
        FacadeBox(fittings, frame, tu, floor + 0.97f, d + 0.16f, tu + 0.30f, floor + 1.13f,
                  d + 0.44f);
    };

    // A display plinth in the window, and the anchor for whatever stands on it.
    const auto plinth = [&](float u, float width, float height, float span) {
        const float d0 = -0.95f, d1 = -0.35f;
        FacadeBox(fittings, frame, u, floor, d0, u + width, floor + height, d1);
        if (displays == nullptr) return;
        ShopDisplay display;
        display.stand =
            Geometry::Frame(frame.at(u + width * 0.5f, floor + height, (d0 + d1) * 0.5f),
                            frame.right, frame.up, frame.out);
        display.span      = span;
        display.kind      = kind;
        display.plotIndex = plotIndex;
        displays->push_back(display);
    };

    const float span = u1 - u0;

    // The scans a trade puts about its room, where the content has them:
    // crates and cartons by a grocer's counter, plants in a florist's,
    // baskets and a chalkboard in a bakery, a picture and a plant in an
    // office. The scene stands them; without the scans the room is as
    // generated. They are what stops the room next to the hero cafe reading
    // as a box of packets beside a composed one. The potted plant is kept
    // to the florist: the scan is 176 000 triangles, and one in every shop
    // window was three million of them.
    const float faceStreet = std::atan2(frame.out.X, frame.out.Z);
    const float faceIn     = faceStreet + MathHelper::Pi;
    const float faceRight  = std::atan2(frame.right.X, frame.right.Z);
    const auto prop = [&](const char* asset, float u, float v, float d, float yaw,
                          float cull = 36.0f) {
        if (heroProps_ == nullptr) return;
        const Vector3 p = frame.at(u, v, d);
        heroProps_->push_back(HeroProp{asset, Geometry::Place(p.X, p.Y, p.Z, yaw), 0.0f, false, cull});
    };
    switch (kind)
    {
        case ShopKind::Bakery:
            prop("ph-chalkboard", u0 + span * 0.30f, floor, -1.15f, faceStreet + 0.25f);
            prop("ph-wicker-basket-01", u0 + span * 0.06f + 0.31f, floor + 0.78f, -0.65f, faceStreet + 0.4f);
            prop("ph-hamburger-buns", u0 + span * 0.06f + 0.31f, floor + 0.83f, -0.65f, faceStreet + 0.4f);
            prop("ph-wicker-basket-01", u0 + span * 0.45f, floor + 0.97f, depth * 0.42f + 0.30f, faceStreet - 0.6f);
            break;
        case ShopKind::Convenience:
            prop("ph-crate", u1 - 0.75f, floor, depth * 0.30f - 0.9f, faceIn + 0.2f);
            prop("ph-crate", u1 - 0.75f, floor + 0.31f, depth * 0.30f - 0.9f, faceIn - 0.15f);
            prop("ph-cardboard-box", u1 - 1.45f, floor, depth * 0.30f - 0.85f, faceIn + 0.5f);
            prop("ph-bananas", u0 + 1.2f, floor + 0.97f, -1.55f + 0.30f, faceStreet + 0.9f);
            prop("ph-hand-truck", u1 - 0.55f, floor, depth + 0.75f, faceRight + 0.3f, 30.0f);
            break;
        case ShopKind::Clothing:
            prop("ph-hanging-picture-frame-01", u0 + 0.03f, floor + 1.55f, depth * 0.62f, faceRight);
            break;
        case ShopKind::Florist:
            prop("ph-potted-plant-01", u0 + span * 0.30f, floor + 0.30f, -0.55f, faceStreet + 0.3f, 28.0f);
            if (span > 7.0f)
                prop("ph-potted-plant-01", u0 + span * 0.72f, floor + 0.56f, -0.97f, faceStreet - 0.7f, 24.0f);
            prop("ph-planter-1", u0 + 0.5f, floor, depth * 0.45f + 0.9f, faceStreet + 0.1f);
            prop("ph-wicker-basket-01", u1 - std::min(2.2f, span * 0.34f) + 0.4f, floor + 0.97f, depth * 0.45f + 0.3f, faceStreet);
            break;
        case ShopKind::Electrical:
            prop("ph-cardboard-box", u1 - 0.7f, floor, depth + 0.6f, faceIn + 0.4f);
            prop("ph-cardboard-box", u1 - 0.7f, floor + 0.42f, depth + 0.6f, faceIn - 0.3f);
            break;
        case ShopKind::Furniture:
            prop("ph-hanging-picture-frame-01", u1 - 0.03f, floor + 1.50f, depth * 0.55f, faceRight + MathHelper::Pi);
            prop("ph-modern-ceiling-lamp-01", u0 + span * 0.5f, ceiling - 0.95f, depth * 0.5f, faceStreet);
            prop("ph-cafe-set", u0 + span * 0.55f, floor, depth * 0.55f, faceStreet + 0.4f);
            break;
        case ShopKind::Office:
            prop("ph-wall-clock", u0 + span * 0.5f, floor + 2.25f, depth + 0.03f, faceStreet);
            prop("ph-hanging-picture-frame-01", u0 + 0.03f, floor + 1.55f, depth * 0.75f, faceRight);
            break;
        case ShopKind::Optician:
            prop("ph-hanging-picture-frame-01", u0 + 0.03f, floor + 1.50f, depth * 0.62f, faceRight);
            break;
        case ShopKind::Vacant:
        case ShopKind::Count:
            break;
    }
    switch (kind)
    {
        case ShopKind::Bakery:
        {
            // A serving counter across the back, shelves of loaves behind it,
            // and two cafe tables in the window.
            counter(u0 + 0.5f, u1 - 0.5f, depth * 0.42f);
            shelving(u0 + 0.6f, u1 - 0.6f, depth + 0.05f, 4, 2.05f);
            for (int i = 0; i < 2; ++i)
            {
                const float tu = u0 + span * (0.24f + 0.44f * static_cast<float>(i));
                timber.addCylinder(frame.at(tu, floor, -1.30f), 0.035f, 0.035f, 0.72f, 8);
                timber.addCylinder(frame.at(tu, floor + 0.72f, -1.30f), 0.34f, 0.34f, 0.04f, 12);
                for (const float side : {-0.52f, 0.52f})
                {
                    FacadeBox(timber, frame, tu - 0.17f, floor, -1.30f + side - 0.17f, tu + 0.17f,
                              floor + 0.44f, -1.30f + side + 0.17f);
                    FacadeBox(timber, frame, tu - 0.17f, floor + 0.44f, -1.30f + side + 0.10f,
                              tu + 0.17f, floor + 0.86f, -1.30f + side + 0.17f);
                }
            }
            plinth(u0 + span * 0.06f, 0.62f, 0.78f, 0.34f);
            poster(u0 + span * 0.62f, floor + 1.55f, 0.60f, 0.84f, depth + 0.02f, inW, false);
            break;
        }
        case ShopKind::Clothing:
        {
            // Rails of garments, a bank of shelves and plinths in the window.
            for (int i = 0; i < 2; ++i)
            {
                const float d = depth * (0.35f + 0.34f * static_cast<float>(i));
                const float a = u0 + 0.55f, b = u1 - 0.55f;
                if (b - a < 0.6f) continue;
                for (const float u : {a, b})
                    fittings.addCylinder(frame.at(u, floor, d), 0.028f, 0.028f, 1.62f, 8);
                fittings.addCylinderBetween(frame.at(a, floor + 1.62f, d),
                                            frame.at(b, floor + 1.62f, d), 0.020f, 8);
                float u = a + 0.10f;
                while (u < b - 0.10f)
                {
                    // A hanging garment: a thin slab, not a box. Two dozen of
                    // them in a row is what a clothes rail looks like through a
                    // window, and nothing else does.
                    const float w = rng.range(0.05f, 0.085f);
                    FacadeBox(stock, frame, u, floor + 0.62f, d - 0.16f, u + w, floor + 1.58f,
                              d + 0.16f);
                    u += w + rng.range(0.010f, 0.035f);
                }
            }
            shelving(u0 + 0.4f, u0 + 0.4f + std::min(2.2f, span * 0.35f), depth + 0.05f, 4, 1.85f);
            plinth(u0 + span * 0.10f, 0.55f, 0.62f, 0.5f);
            if (span > 4.5f) plinth(u0 + span * 0.62f, 0.55f, 0.44f, 0.5f);
            poster(u0 + span * 0.55f, floor + 1.30f, 0.84f, 1.18f, depth + 0.02f, inW, false);
            poster(u1 - 0.01f, floor + 1.35f, 0.60f, 0.84f, depth * 0.30f, inU * -1.0f, true);
            break;
        }
        case ShopKind::Convenience:
        {
            // Aisles: two or three double-sided runs down the shop, a wall of
            // shelving at the back and a counter by the door.
            const int aisles = span > 6.0f ? 3 : 2;
            for (int i = 0; i < aisles; ++i)
                shelving(u0 + 0.45f, u1 - 0.45f, depth * (0.30f + 0.24f * static_cast<float>(i)), 4,
                         1.72f);
            shelving(u0 + 0.35f, u1 - 0.35f, depth + 0.05f, 5, 2.10f);
            counter(u0 + 0.4f, u0 + std::min(2.4f, span * 0.35f), -1.55f);
            plinth(u1 - span * 0.20f, 0.7f, 0.55f, 0.4f);
            poster(u0 + 0.01f, floor + 1.45f, 0.42f, 0.60f, -1.0f, inU, true);
            poster(u0 + 0.01f, floor + 1.45f, 0.42f, 0.60f, -1.55f, inU, true);
            break;
        }
        case ShopKind::Electrical:
        {
            counter(u0 + 0.5f, u0 + std::min(2.6f, span * 0.4f), depth * 0.5f);
            shelving(u0 + 0.4f, u1 - 0.4f, depth + 0.05f, 3, 1.95f);
            // A wall of screens: dark rectangles at eye level, which is the one
            // thing that says "electrical shop" from the pavement.
            MeshBuilder& screens = collector.builder(&materials_.get(MaterialId::ShopScreen));
            screens.setTileSize(0.8f);
            float u = u0 + 0.5f;
            while (u < u1 - 0.9f)
            {
                const float w = rng.range(0.55f, 0.95f);
                FacadeBox(screens, frame, u, floor + 1.10f, depth * 0.72f, u + w,
                          floor + 1.10f + w * 0.58f, depth * 0.72f + 0.06f);
                u += w + rng.range(0.12f, 0.30f);
            }
            plinth(u1 - span * 0.24f, 0.66f, 0.72f, 0.34f);
            poster(u0 + span * 0.08f, floor + 1.95f, 0.84f, 0.60f, depth + 0.02f, inW, false);
            break;
        }
        case ShopKind::Florist:
        {
            // Tiered staging, the way a florist's window actually works.
            for (int i = 0; i < 3; ++i)
            {
                const float d = -0.55f - 0.42f * static_cast<float>(i);
                FacadeBox(timber, frame, u0 + 0.4f, floor, d - 0.20f, u1 - 0.4f,
                          floor + 0.30f + 0.26f * static_cast<float>(i), d + 0.20f);
            }
            shelving(u0 + 0.4f, u1 - 0.4f, depth + 0.05f, 3, 1.70f);
            counter(u1 - std::min(2.2f, span * 0.34f), u1 - 0.4f, depth * 0.45f);
            plinth(u0 + span * 0.14f, 0.5f, 0.86f, 0.45f);
            if (span > 4.0f) plinth(u0 + span * 0.55f, 0.5f, 0.68f, 0.45f);
            break;
        }
        case ShopKind::Optician:
        {
            // Frames on the wall in rows, which is what an optician's is.
            for (int row = 0; row < 4; ++row)
            {
                const float v = floor + 0.95f + 0.30f * static_cast<float>(row);
                FacadeBox(fittings, frame, u0 + 0.45f, v, depth + 0.05f, u1 - 0.45f, v + 0.030f,
                          depth + 0.24f);
                float u = u0 + 0.55f;
                while (u < u1 - 0.65f)
                {
                    FacadeBox(stock, frame, u, v + 0.030f, depth + 0.09f, u + 0.135f, v + 0.095f,
                              depth + 0.20f);
                    u += 0.135f + rng.range(0.05f, 0.12f);
                }
            }
            counter(u0 + 0.5f, u0 + std::min(2.4f, span * 0.4f), depth * 0.5f);
            plinth(u0 + span * 0.12f, 0.44f, 0.90f, 0.22f);
            if (span > 4.0f) plinth(u0 + span * 0.58f, 0.44f, 0.90f, 0.22f);
            poster(u1 - 0.01f, floor + 1.40f, 0.84f, 1.18f, depth * 0.40f, inU * -1.0f, true);
            break;
        }
        case ShopKind::Furniture:
        {
            shelving(u0 + 0.4f, u1 - 0.4f, depth + 0.05f, 3, 2.00f);
            plinth(u0 + span * 0.10f, std::min(1.9f, span * 0.34f), 0.12f, 1.10f);
            if (span > 5.0f) plinth(u0 + span * 0.56f, std::min(1.5f, span * 0.28f), 0.12f, 0.90f);
            break;
        }
        case ShopKind::Office:
        {
            // Desks in a row facing the window, with a screen on each: an office
            // at street level is glazed and you see straight into it.
            const int desks = std::max(1, static_cast<int>(span / 2.1f));
            for (int i = 0; i < desks; ++i)
            {
                const float du = u0 + 0.4f
                                 + (span - 0.8f) * static_cast<float>(i)
                                       / static_cast<float>(desks);
                const float dw = std::min(1.55f,
                                          (span - 0.8f) / static_cast<float>(desks) - 0.3f);
                if (dw < 0.6f) continue;
                FacadeBox(timber, frame, du, floor + 0.70f, depth * 0.55f, du + dw, floor + 0.75f,
                          depth * 0.55f + 0.70f);
                for (const float leg : {0.06f, dw - 0.10f})
                    FacadeBox(fittings, frame, du + leg, floor, depth * 0.55f + 0.06f,
                              du + leg + 0.04f, floor + 0.70f, depth * 0.55f + 0.10f);
                MeshBuilder& screen = collector.builder(&materials_.get(MaterialId::ShopScreen));
                screen.setTileSize(0.5f);
                FacadeBox(screen, frame, du + dw * 0.28f, floor + 0.79f, depth * 0.55f + 0.50f,
                          du + dw * 0.72f, floor + 1.09f, depth * 0.55f + 0.54f);
                FacadeBox(fittings, frame, du + dw * 0.34f, floor + 0.42f, depth * 0.55f - 0.36f,
                          du + dw * 0.66f, floor + 0.48f, depth * 0.55f - 0.04f);
                FacadeBox(fittings, frame, du + dw * 0.34f, floor + 0.48f, depth * 0.55f - 0.36f,
                          du + dw * 0.66f, floor + 0.95f, depth * 0.55f - 0.30f);
            }
            shelving(u0 + 0.4f, u1 - 0.4f, depth + 0.05f, 4, 1.85f);
            // A notice board and a framed print, which is what an office
            // hangs on its walls.
            poster(u0 + 0.01f, floor + 1.30f, 0.90f, 0.64f, depth * 0.30f, inU, true);
            poster(u1 - 0.01f, floor + 1.45f, 0.60f, 0.84f, depth * 0.55f, inU * -1.0f, true);
            break;
        }
        case ShopKind::Vacant:
        case ShopKind::Count:
            break;
    }
}

void BuildingBuilder::buildHeroBakery(const FacadeFrame& frame, float u0, float u1, float floor,
                                      float ceiling, int plotIndex, GeometryCollector& collector,
                                      Rng& rng, std::vector<ShopDisplay>* displays)
{
    // One shop on the street built the way an environment artist would build
    // a hero asset: composed, not populated. The generic interiors are a box
    // with a trade's fittings scattered in it, and through the glass at three
    // metres that is what they look like. This one is a bakery-cafe with a
    // plan -- a serving counter under a glass case, the bread behind it, a
    // coffee station against the end wall, a back door into a darker store,
    // pendant lamps over the counter and a dressed window -- and every prop
    // in it is a scan standing where a person would have put it.
    (void)rng;
    const float depth = -7.2f;        // the back wall
    const float storeDepth = -9.8f;   // the store room beyond it
    const float mid = -3.4f;          // where the front of the room becomes the back
    const float span = u1 - u0;
    const Vector3 inU = frame.right;
    const Vector3 inV = frame.up;
    const Vector3 inW = frame.out;
    const float faceStreet = std::atan2(frame.out.X, frame.out.Z);
    const float faceIn     = faceStreet + MathHelper::Pi;
    const float faceRight  = std::atan2(frame.right.X, frame.right.Z);
    const auto prop = [&](const char* asset, float u, float v, float d, float yaw,
                          float fit = 0.0f, bool byWidth = false, float cull = 40.0f) {
        if (heroProps_ == nullptr) return;
        const Vector3 p = frame.at(u, v, d);
        heroProps_->push_back(
            HeroProp{asset, Geometry::Place(p.X, p.Y, p.Z, yaw), fit, byWidth, cull});
    };

    // --- the room ---------------------------------------------------------------
    const Material& wallBase = materials_.get(MaterialId::ShopWall);
    const Vector3 warm(0.70f, 0.60f, 0.47f);
    // Lit a stop and a half brighter than the generic rooms. In daylight the
    // pane in front of this room adds the sunlit street's reflection over
    // whatever is behind it, and a room lit like the others disappeared
    // under it: through the shop-window viewpoint the oak floor read as a
    // pale plane, which was the pavement reflected. A cafe is lit anyway.
    const Material* wallFront = materials_.deriveTinted("hero-bakery-wall", MaterialId::ShopWall,
                                                        warm, wallBase.emissiveFactor * 1.7f);
    const Material* wallBack = materials_.deriveTinted("hero-bakery-wall-back", MaterialId::ShopWall,
                                                       warm * 0.58f, wallBase.emissiveFactor * 0.75f);
    const Material* wallStore = materials_.deriveTinted(
        "hero-bakery-store", MaterialId::ShopWall, warm * 0.30f, wallBase.emissiveFactor * 0.12f);
    const Material* soffit = materials_.deriveTinted(
        "hero-bakery-ceiling", MaterialId::ShopWall, warm * 0.70f + Vector3(0.10f, 0.10f, 0.10f),
        wallBase.emissiveFactor * 0.85f);
    MeshBuilder& room  = collector.builder(wallFront);
    MeshBuilder& back  = collector.builder(wallBack);
    MeshBuilder& store = collector.builder(wallStore);
    MeshBuilder& lid   = collector.builder(soffit);
    for (MeshBuilder* b : {&room, &back, &store, &lid}) b->setTileSize(1.8f);

    // The doorway in the back wall, off centre, and the wall either side of it.
    const float doorU0 = u0 + span * 0.22f, doorU1 = doorU0 + 0.95f, doorTop = floor + 2.10f;
    back.addQuadFacing(frame.at(u0, floor, depth), frame.at(doorU0, floor, depth),
                       frame.at(doorU0, ceiling, depth), frame.at(u0, ceiling, depth), inW);
    back.addQuadFacing(frame.at(doorU1, floor, depth), frame.at(u1, floor, depth),
                       frame.at(u1, ceiling, depth), frame.at(doorU1, ceiling, depth), inW);
    back.addQuadFacing(frame.at(doorU0, doorTop, depth), frame.at(doorU1, doorTop, depth),
                       frame.at(doorU1, ceiling, depth), frame.at(doorU0, ceiling, depth), inW);
    // Side walls in a front and a back half, the ceiling and the floor.
    room.addQuadFacing(frame.at(u0, floor, mid), frame.at(u0, floor, 0.0f),
                       frame.at(u0, ceiling, 0.0f), frame.at(u0, ceiling, mid), inU);
    back.addQuadFacing(frame.at(u0, floor, depth), frame.at(u0, floor, mid),
                       frame.at(u0, ceiling, mid), frame.at(u0, ceiling, depth), inU);
    room.addQuadFacing(frame.at(u1, floor, mid), frame.at(u1, floor, 0.0f),
                       frame.at(u1, ceiling, 0.0f), frame.at(u1, ceiling, mid), inU * -1.0f);
    back.addQuadFacing(frame.at(u1, floor, depth), frame.at(u1, floor, mid),
                       frame.at(u1, ceiling, mid), frame.at(u1, ceiling, depth), inU * -1.0f);
    lid.addQuadFacing(frame.at(u0, ceiling, depth), frame.at(u1, ceiling, depth),
                      frame.at(u1, ceiling, 0.0f), frame.at(u0, ceiling, 0.0f), inV * -1.0f);
    // An oak floor rather than the tiles the other shops have, dark enough
    // that the pale things on it -- the tables, the counter top, the bread
    // -- are what the eye finds first; a pale floor under a lit ceiling was
    // the biggest flat surface in the shop-window frame.
    const Material& timberBase = materials_.get(MaterialId::ShopTimber);
    const Material* oak = materials_.deriveTinted("hero-bakery-floor", MaterialId::ShopTimber,
                                                  Vector3(0.50f, 0.38f, 0.26f),
                                                  timberBase.emissiveFactor * 0.9f);
    MeshBuilder& floorSurface = collector.builder(oak);
    floorSurface.setTileSize(0.62f, 2.8f);
    floorSurface.addQuadFacing(frame.at(u0, floor, storeDepth), frame.at(u1, floor, storeDepth),
                               frame.at(u1, floor, 0.0f), frame.at(u0, floor, 0.0f), inV);
    // The store room: dark, seen only through the doorway, with a door leaf
    // standing ajar in it and a stack of crates against its wall.
    store.addQuadFacing(frame.at(u0, floor, storeDepth), frame.at(u1, floor, storeDepth),
                        frame.at(u1, ceiling, storeDepth), frame.at(u0, ceiling, storeDepth), inW);
    store.addQuadFacing(frame.at(u0, floor, storeDepth), frame.at(u0, floor, depth),
                        frame.at(u0, ceiling, depth), frame.at(u0, ceiling, storeDepth), inU);
    store.addQuadFacing(frame.at(u1, floor, storeDepth), frame.at(u1, floor, depth),
                        frame.at(u1, ceiling, depth), frame.at(u1, ceiling, storeDepth), inU * -1.0f);
    store.addQuadFacing(frame.at(u0, ceiling, storeDepth), frame.at(u1, ceiling, storeDepth),
                        frame.at(u1, ceiling, depth), frame.at(u0, ceiling, depth), inV * -1.0f);
    // The doorway's reveal and a frame round it.
    store.addQuadFacing(frame.at(doorU0, floor, depth), frame.at(doorU0, floor, depth - 0.24f),
                        frame.at(doorU0, doorTop, depth - 0.24f), frame.at(doorU0, doorTop, depth), inU);
    store.addQuadFacing(frame.at(doorU1, floor, depth), frame.at(doorU1, floor, depth - 0.24f),
                        frame.at(doorU1, doorTop, depth - 0.24f), frame.at(doorU1, doorTop, depth), inU * -1.0f);
    store.addQuadFacing(frame.at(doorU0, doorTop, depth), frame.at(doorU1, doorTop, depth),
                        frame.at(doorU1, doorTop, depth - 0.24f), frame.at(doorU0, doorTop, depth - 0.24f), inV * -1.0f);
    MeshBuilder& timber = collector.builder(&materials_.get(MaterialId::ShopTimber));
    timber.setTileSize(0.62f);
    // What gives the room a section rather than six planes: three beams
    // across the ceiling, a dado rail and boarded panelling to hip height
    // along both side walls, and a skirting round the floor.
    {
        const Material* darkTimber = materials_.deriveTinted(
            "hero-bakery-beam", MaterialId::ShopTimber, Vector3(0.42f, 0.32f, 0.22f),
            timberBase.emissiveFactor * 0.6f);
        MeshBuilder& beams = collector.builder(darkTimber);
        beams.setTileSize(0.62f);
        for (const float bd : {-1.9f, -4.2f, -6.2f})
            FacadeBox(beams, frame, u0, ceiling - 0.22f, bd - 0.08f, u1, ceiling, bd + 0.08f);
        for (const float su : {u0, u1})
        {
            const float sgn = su == u0 ? 1.0f : -1.0f;
            // Boards, each a little proud of the wall, a hand wide, with the
            // rail over them and the skirting under.
            for (float bd = -0.02f; bd > depth + 0.05f; bd -= 0.115f)
                FacadeBox(beams, frame, su, floor + 0.08f, bd - 0.105f, su + sgn * 0.022f,
                          floor + 1.02f, bd);
            FacadeBox(beams, frame, su, floor + 1.02f, depth, su + sgn * 0.045f, floor + 1.07f, 0.0f);
            FacadeBox(beams, frame, su, floor, depth, su + sgn * 0.03f, floor + 0.09f, 0.0f);
        }
    }
    FacadeBox(timber, frame, doorU0 - 0.07f, floor, depth - 0.02f, doorU0, doorTop + 0.07f, depth + 0.05f);
    FacadeBox(timber, frame, doorU1, floor, depth - 0.02f, doorU1 + 0.07f, doorTop + 0.07f, depth + 0.05f);
    FacadeBox(timber, frame, doorU0 - 0.07f, doorTop, depth - 0.02f, doorU1 + 0.07f, doorTop + 0.07f, depth + 0.05f);
    // The leaf, swung a third open into the store.
    {
        const Vector3 hinge = frame.at(doorU0 + 0.02f, floor, depth - 0.12f);
        const Vector3 swing = Vector3::Normalize(inU * 0.55f + inW * -0.83f);
        const Vector3 a = hinge, b = hinge + swing * 0.90f;
        timber.addQuadFacing(a, b, b + inV * (doorTop - floor - 0.02f),
                             a + inV * (doorTop - floor - 0.02f),
                             Vector3::Cross(swing, inV) * -1.0f);
        timber.addQuadFacing(b, a, a + inV * (doorTop - floor - 0.02f),
                             b + inV * (doorTop - floor - 0.02f),
                             Vector3::Cross(swing, inV));
    }
    prop("ph-crate", u1 - 0.9f, floor, storeDepth + 0.5f, faceRight + 0.2f, 0.0f, false, 30.0f);
    prop("ph-crate", u1 - 0.9f, floor + 0.31f, storeDepth + 0.5f, faceRight - 0.15f, 0.0f, false, 30.0f);
    prop("ph-cardboard-box", u0 + 0.8f, floor, storeDepth + 0.6f, faceIn + 0.3f, 0.0f, false, 30.0f);

    // --- light --------------------------------------------------------------------
    // Battens over the back of the room only; the front is lit, to the eye,
    // by three pendants over the counter.
    {
        MeshBuilder& lamp = collector.builder(&materials_.get(MaterialId::ShopCeilingLight));
        lamp.setUvMode(UvMode::Explicit);
        const float lv = ceiling - 0.06f;
        for (const float ld : {depth * 0.62f, depth * 0.84f})
        {
            float a = u0 + 0.6f;
            while (a < u1 - 0.8f)
            {
                const float b = std::min(a + 1.50f, u1 - 0.6f);
                lamp.addQuadFacingUv(frame.at(a, lv, ld - 0.075f), frame.at(b, lv, ld - 0.075f),
                                     frame.at(b, lv, ld + 0.075f), frame.at(a, lv, ld + 0.075f),
                                     inV * -1.0f);
                a = b + 0.22f;
            }
        }
    }

    // --- the counter --------------------------------------------------------------
    MeshBuilder& fittings = collector.builder(&materials_.get(MaterialId::ShopFitting));
    fittings.setTileSize(1.0f);
    const float cU0 = u0 + 0.7f, cU1 = u1 - 1.9f;
    const float cD0 = -3.6f;          // the front face, toward the window
    const float cD1 = cD0 - 0.70f;    // the back
    const float top = floor + 0.92f;
    FacadeBox(timber, frame, cU0, floor + 0.08f, cD1, cU1, top, cD0);
    // A boarded front: vertical boards a hand wide, every other one a
    // shade prouder, over a recessed kick, and the top a slab that
    // overhangs them. A counter that is one box is a crate.
    {
        MeshBuilder& boards = collector.builder(materials_.deriveTinted(
            "hero-bakery-counter", MaterialId::ShopTimber, Vector3(0.36f, 0.27f, 0.19f),
            timberBase.emissiveFactor * 0.6f));
        boards.setTileSize(0.62f);
        int n = 0;
        for (float bu = cU0; bu < cU1 - 0.02f; bu += 0.11f, ++n)
            FacadeBox(boards, frame, bu, floor + 0.09f, cD0 - 0.01f, std::min(bu + 0.10f, cU1), top - 0.01f,
                      cD0 + (n % 2 == 0 ? 0.03f : 0.018f));
        FacadeBox(boards, frame, cU0, floor, cD1, cU1, floor + 0.08f, cD0 - 0.06f);
    }
    FacadeBox(timber, frame, cU0 - 0.03f, top, cD1 - 0.04f, cU1 + 0.03f, top + 0.05f, cD0 + 0.07f);
    // The glass case over the left two thirds: a front pane, a top and two
    // ends, and a shelf inside with the cakes on it.
    const float caseU1 = cU0 + (cU1 - cU0) * 0.64f;
    const float caseTop = top + 0.05f + 0.52f;
    MeshBuilder& glass = collector.builder(&materials_.get(MaterialId::ShopGlazing));
    glass.setTileSize(2.4f);
    glass.addQuadFacing(frame.at(cU0, top + 0.05f, cD0 + 0.06f), frame.at(caseU1, top + 0.05f, cD0 + 0.06f),
                        frame.at(caseU1, caseTop, cD0 + 0.06f), frame.at(cU0, caseTop, cD0 + 0.06f), inW);
    glass.addQuadFacing(frame.at(cU0, caseTop, cD1 + 0.30f), frame.at(caseU1, caseTop, cD1 + 0.30f),
                        frame.at(caseU1, caseTop, cD0 + 0.06f), frame.at(cU0, caseTop, cD0 + 0.06f), inV);
    for (const float eu : {cU0, caseU1})
        glass.addQuadFacing(frame.at(eu, top + 0.05f, cD1 + 0.30f), frame.at(eu, top + 0.05f, cD0 + 0.06f),
                            frame.at(eu, caseTop, cD0 + 0.06f), frame.at(eu, caseTop, cD1 + 0.30f),
                            eu == cU0 ? inU : inU * -1.0f);
    MeshBuilder& metal = collector.builder(&materials_.get(MaterialId::Aluminium));
    metal.setTileSize(0.3f);
    for (const float eu : {cU0, caseU1})
        FacadeBox(metal, frame, eu - 0.012f, top + 0.05f, cD1 + 0.30f, eu + 0.012f, caseTop + 0.012f, cD0 + 0.08f);
    FacadeBox(metal, frame, cU0 - 0.012f, caseTop, cD0 + 0.04f, caseU1 + 0.012f, caseTop + 0.024f, cD0 + 0.08f);
    // What is in the case and on the counter.
    const float shelfV = top + 0.07f;
    const float caseD  = cD0 - 0.30f;
    prop("ph-strawberry-chocolate-cake", cU0 + 0.35f, shelfV, caseD, faceStreet + 0.3f);
    prop("ph-carrot-cake", cU0 + 0.80f, shelfV, caseD, faceStreet - 0.4f);
    prop("ph-wooden-cutting-board", cU0 + 1.35f, shelfV, caseD, faceStreet + 0.1f);
    prop("ph-croissant", cU0 + 1.25f, shelfV + 0.03f, caseD + 0.05f, faceStreet + 0.9f);
    prop("ph-croissant", cU0 + 1.42f, shelfV + 0.03f, caseD - 0.06f, faceStreet - 0.6f);
    prop("ph-croissant", cU0 + 1.60f, shelfV + 0.03f, caseD + 0.02f, faceStreet + 2.3f);
    prop("ph-hamburger-buns", cU0 + 2.05f, shelfV, caseD, faceStreet + 0.2f);
    prop("ph-cashregister-01", caseU1 + 0.45f, top + 0.05f, cD0 - 0.36f, faceIn - 0.15f);
    prop("ph-jug-01", cU1 - 0.35f, top + 0.05f, cD0 - 0.30f, faceIn + 0.8f);
    prop("ph-bananas", cU1 - 0.85f, top + 0.05f, cD0 - 0.42f, faceStreet + 1.1f);

    // --- behind the counter ----------------------------------------------------------
    // Bread shelving on the back wall, a lower back-counter under it with
    // baskets, and the coffee station against the end wall.
    // Timber shelving across the back wall, not the white steel rack: the
    // steel one, lit by the sky the room cannot see, glowed like a fridge.
    prop("ph-wooden-display-shelves-01", u0 + 0.85f, floor, depth + 0.22f, faceStreet, 0.0f, false, 40.0f);
    prop("ph-painted-wooden-shelves", u0 + 2.05f, floor, depth + 0.22f, faceStreet, 0.0f, false, 40.0f);
    prop("ph-wooden-display-shelves-01", u0 + 3.25f, floor, depth + 0.22f, faceStreet, 0.0f, false, 40.0f);
    prop("ph-painted-wooden-shelves", u0 + 4.25f, floor, depth + 0.22f, faceStreet, 0.0f, false, 40.0f);
    prop("ph-wicker-basket-01", u0 + 0.85f, floor + 1.02f, depth + 0.24f, faceStreet + 0.2f);
    prop("ph-hamburger-buns", u0 + 0.85f, floor + 1.07f, depth + 0.24f, faceStreet + 0.2f);
    prop("ph-wicker-basket-01", u0 + 3.25f, floor + 1.02f, depth + 0.24f, faceStreet - 0.3f);
    prop("ph-croissant", u0 + 3.15f, floor + 1.06f, depth + 0.20f, faceStreet + 1.2f);
    prop("ph-croissant", u0 + 3.35f, floor + 1.06f, depth + 0.28f, faceStreet - 0.4f);
    prop("ph-jug-01", u0 + 2.05f, floor + 1.48f, depth + 0.24f, faceStreet + 0.5f);
    prop("ph-tea-set-01", u0 + 3.25f, floor + 1.62f, depth + 0.24f, faceStreet + 0.1f, 0.0f, false, 30.0f);
    prop("ph-tea-set-01", u0 + 4.25f, floor + 1.13f, depth + 0.22f, faceStreet + 0.2f, 0.0f, false, 30.0f);
    FacadeBox(fittings, frame, u0 + 4.95f, floor, depth + 0.02f, std::min(u1 - 0.4f, u0 + 4.95f + 2.6f),
              floor + 0.90f, depth + 0.62f);
    FacadeBox(timber, frame, u0 + 4.93f, floor + 0.90f, depth, std::min(u1 - 0.38f, u0 + 4.97f + 2.6f),
              floor + 0.94f, depth + 0.66f);
    prop("ph-wicker-basket-01", u0 + 5.35f, floor + 0.94f, depth + 0.32f, faceStreet + 0.3f);
    prop("ph-hamburger-buns", u0 + 5.35f, floor + 0.99f, depth + 0.32f, faceStreet + 0.3f);
    prop("ph-wicker-basket-01", u0 + 5.95f, floor + 0.94f, depth + 0.32f, faceStreet - 0.5f);
    prop("ph-coffeecart-01", u1 - 0.62f, floor, depth + 1.85f, faceRight + MathHelper::Pi, 0.0f, false, 40.0f);

    // --- on the walls ------------------------------------------------------------------
    // A shelf along the left wall at head height with the tea things on it,
    // and a bench under the prints.
    FacadeBox(timber, frame, u0, floor + 1.72f, -3.3f, u0 + 0.24f, floor + 1.75f, -1.4f);
    for (const float bd : {-3.2f, -1.5f})
        FacadeBox(timber, frame, u0, floor + 1.60f, bd - 0.02f, u0 + 0.22f, floor + 1.72f, bd + 0.02f);
    prop("ph-tea-set-01", u0 + 0.12f, floor + 1.75f, -2.0f, faceRight + 0.3f, 0.0f, false, 30.0f);
    prop("ph-jug-01", u0 + 0.12f, floor + 1.75f, -2.7f, faceRight - 0.6f, 0.0f, false, 30.0f);
    FacadeBox(timber, frame, u0 + 0.02f, floor + 0.44f, -3.35f, u0 + 0.40f, floor + 0.48f, -1.45f);
    for (const float bd : {-3.3f, -1.5f})
        FacadeBox(timber, frame, u0 + 0.05f, floor, bd - 0.02f, u0 + 0.37f, floor + 0.44f, bd + 0.02f);
    prop("ph-wall-clock", u0 + (u1 - u0) * 0.5f, floor + 2.25f, depth + 0.03f, faceStreet, 0.0f, false, 40.0f);
    prop("ph-hanging-picture-frame-01", u0 + 0.03f, floor + 1.35f, -1.9f, faceRight, 0.0f, false, 40.0f);
    prop("ph-hanging-picture-frame-01", u0 + 0.03f, floor + 1.45f, -2.9f, faceRight, 0.0f, false, 40.0f);
    prop("ph-chalkboard", cU1 + 0.55f, floor, cD0 + 0.9f, faceStreet - 0.35f, 0.0f, false, 40.0f);
    for (const float lu : {cU0 + 0.9f, cU0 + 2.3f, cU0 + 3.7f})
        if (lu < cU1) prop("ph-modern-ceiling-lamp-01", lu, ceiling - 0.95f, cD0 + 0.35f, faceStreet, 0.0f, false, 40.0f);
    {
        // Posters: a menu by the counter and a print by the window, from the
        // same atlas the other shops use.
        MeshBuilder& posters = collector.builder(&materials_.get(MaterialId::ShopPoster));
        posters.setUvMode(UvMode::Explicit);
        posters.addQuadFacingUv(frame.at(u1 - 0.02f, floor + 1.55f, -2.4f), frame.at(u1 - 0.02f, floor + 1.55f, -1.5f),
                                frame.at(u1 - 0.02f, floor + 2.20f, -1.5f), frame.at(u1 - 0.02f, floor + 2.20f, -2.4f),
                                inU * -1.0f);
        posters.addQuadUv(frame.at(u1 - 1.4f, floor + 1.55f, depth + 0.02f), frame.at(u1 - 0.7f, floor + 1.55f, depth + 0.02f),
                          frame.at(u1 - 0.7f, floor + 2.45f, depth + 0.02f), frame.at(u1 - 1.4f, floor + 2.45f, depth + 0.02f),
                          Vector2(0.0f, 0.5f), Vector2(0.5f, 0.5f), Vector2(0.5f, 0.0f), Vector2(0.0f, 0.0f));
    }

    // --- the window ----------------------------------------------------------------------
    // A low table in the window with the day's bread on it, and two cafe
    // tables between the window and the counter.
    const float wU0 = u0 + 0.5f, wU1 = std::min(u0 + 2.7f, cU1);
    FacadeBox(timber, frame, wU0, floor + 0.72f, -1.25f, wU1, floor + 0.76f, -0.45f);
    for (const float lu : {wU0 + 0.06f, wU1 - 0.10f})
        for (const float ld : {-1.19f, -0.55f})
            FacadeBox(timber, frame, lu, floor, ld, lu + 0.04f, floor + 0.72f, ld + 0.04f);
    prop("ph-wooden-cutting-board", wU0 + 0.45f, floor + 0.76f, -0.85f, faceStreet + 0.25f);
    prop("ph-croissant", wU0 + 0.40f, floor + 0.79f, -0.80f, faceStreet + 1.7f);
    prop("ph-croissant", wU0 + 0.55f, floor + 0.79f, -0.92f, faceStreet - 0.9f);
    prop("ph-wicker-basket-01", wU0 + 1.15f, floor + 0.76f, -0.85f, faceStreet - 0.2f);
    prop("ph-hamburger-buns", wU0 + 1.15f, floor + 0.81f, -0.85f, faceStreet - 0.2f);
    prop("ph-carrot-cake", wU0 + 1.75f, floor + 0.76f, -0.80f, faceStreet + 0.5f);
    prop("ph-cafe-set", u1 - 1.5f, floor, -2.0f, faceStreet + 0.35f, 0.0f, false, 40.0f);
    if (span > 7.0f) prop("ph-cafe-set", u1 - 3.4f, floor, -2.2f, faceStreet - 0.6f, 0.0f, false, 40.0f);

    // The display anchor the Khronos props use, kept so the window is not
    // bare when the scans are absent.
    if (displays != nullptr)
    {
        ShopDisplay display;
        display.stand = Geometry::Frame(frame.at(wU0 + (wU1 - wU0) * 0.5f, floor + 0.76f, -0.85f),
                                        frame.right, frame.up, frame.out);
        display.span      = 0.34f;
        display.kind      = ShopKind::Bakery;
        display.plotIndex = plotIndex;
        displays->push_back(display);
    }
}

void BuildingBuilder::buildEntrance(const FacadeFrame& frame, float u, float sillHeight,
                                    const Palette& palette, GeometryCollector& collector, Rng& rng,
                                    std::vector<Opening>& openings)
{
    const float width  = M::kEntranceDoorWidth;
    const float height = M::kEntranceDoorHeight;
    const float reveal = 0.26f;
    openings.push_back(Opening{u, sillHeight, u + width, sillHeight + height});

    // The opening's reveal.
    MeshBuilder& wall = collector.builder(palette.wall);
    wall.setTileSize(2.0f);
    const float u1 = u + width, v1 = sillHeight + height;
    wall.addQuad(frame.at(u, sillHeight, 0.0f), frame.at(u, sillHeight, -reveal),
                 frame.at(u, v1, -reveal), frame.at(u, v1, 0.0f));
    wall.addQuad(frame.at(u1, sillHeight, -reveal), frame.at(u1, sillHeight, 0.0f),
                 frame.at(u1, v1, 0.0f), frame.at(u1, v1, -reveal));
    wall.addQuad(frame.at(u, v1, 0.0f), frame.at(u, v1, -reveal), frame.at(u1, v1, -reveal),
                 frame.at(u1, v1, 0.0f));

    // The door leaf, its panels, and the fanlight above it.
    MeshBuilder& door = collector.builder(palette.door);
    door.setTileSize(1.2f);
    const float leafDepth = -reveal + 0.06f;
    Panel(door, frame, u + 0.03f, sillHeight, u1 - 0.03f, v1 - 0.42f, leafDepth);
    // Raised panels: four boxes proud of the leaf.
    for (int panel = 0; panel < 4; ++panel)
    {
        const float px = u + 0.14f + static_cast<float>(panel % 2) * (width * 0.5f - 0.06f);
        const float py = sillHeight + 0.22f
                         + static_cast<float>(panel / 2) * ((height - 0.9f) * 0.5f);
        FacadeBox(door, frame, px, py, leafDepth, px + width * 0.5f - 0.22f,
                  py + (height - 1.05f) * 0.5f, leafDepth + 0.022f);
    }
    // Ironmongery: a handle and a letter plate.
    MeshBuilder& metal = collector.builder(palette.metal);
    metal.setTileSize(0.3f);
    FacadeBox(metal, frame, u1 - 0.24f, sillHeight + 1.02f, leafDepth,
              u1 - 0.10f, sillHeight + 1.10f, leafDepth + 0.05f);

    MeshBuilder& glass = collector.builder(palette.glass);
    glass.setTileSize(0.8f);
    Panel(glass, frame, u + 0.10f, v1 - 0.38f, u1 - 0.10f, v1 - 0.06f, leafDepth - 0.01f);

    // The surround: pilasters and a moulded head, which is where an entrance
    // gets its weight from on a nineteenth-century block.
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    WallBox(trim, frame, u - 0.20f, sillHeight, u - 0.02f, v1 + 0.10f, 0.09f);
    WallBox(trim, frame, u1 + 0.02f, sillHeight, u1 + 0.20f, v1 + 0.10f, 0.09f);
    WallBox(trim, frame, u - 0.26f, v1 + 0.10f, u1 + 0.26f, v1 + 0.26f, 0.14f);
    // A step up from the pavement.
    WallBox(trim, frame, u - 0.06f, M::kCurbHeight, u1 + 0.06f, sillHeight, 0.30f);
    (void)rng;
}

void BuildingBuilder::buildBalcony(const FacadeFrame& frame, float u, float v, float width,
                                   const Palette& palette, GeometryCollector& collector, Rng& rng)
{
    const float depth = M::kBalconyDepth;

    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    // The slab, with a chamfered nosing along its front edge, and two
    // brackets under it. A slab that meets the wall with nothing holding it
    // up is the kind of thing a viewer cannot name and does not believe.
    WallBox(trim, frame, u, v - M::kBalconySlab, u + width, v, depth - 0.06f);
    BevelBox(trim, frame, u, v - M::kBalconySlab - 0.05f, depth - 0.10f, u + width, v,
             depth + 0.03f, 0.03f);
    for (const float bu : {u + 0.12f, u + width - 0.26f})
    {
        WallBevel(trim, frame, bu, v - M::kBalconySlab - 0.46f, bu + 0.14f, v - M::kBalconySlab,
                  depth * 0.52f, 0.02f);
        // The diagonal strut, as a thinner member from the foot of the
        // bracket out to the slab.
        FacadeBox(trim, frame, bu + 0.02f, v - M::kBalconySlab - 0.30f, depth * 0.50f,
                  bu + 0.12f, v - M::kBalconySlab - 0.06f, depth * 0.72f);
    }

    // The railing: a cast-iron balustrade of uprights between a top and bottom
    // rail. Drawn as boxes rather than a texture because it is seen against the
    // sky and an alpha card would show its edges.
    MeshBuilder& metal = collector.builder(palette.metal);
    metal.setTileSize(0.3f);
    const float rail = M::kRailingHeight;
    FacadeBox(metal, frame, u, v + rail - 0.05f, depth - 0.06f, u + width, v + rail, depth);
    FacadeBox(metal, frame, u, v + 0.06f, depth - 0.06f, u + width, v + 0.11f, depth);
    // Returns along the two sides.
    WallBox(metal, frame, u, v + rail - 0.05f, u + 0.05f, v + rail, depth);
    WallBox(metal, frame, u + width - 0.05f, v + rail - 0.05f, u + width, v + rail, depth);

    const float spacing = 0.115f;
    const int uprights = std::max(2, static_cast<int>(width / spacing));
    for (int i = 0; i <= uprights; ++i)
    {
        const float bu = u + static_cast<float>(i) * (width / static_cast<float>(uprights));
        FacadeBox(metal, frame, bu - 0.011f, v + 0.06f, depth - 0.045f, bu + 0.011f, v + rail,
                  depth - 0.023f);
    }
    // The two side rails, so the balcony is enclosed rather than open at the ends.
    for (const float side : {0.0f, width})
    {
        const float bu = u + side;
        for (float d = 0.10f; d < depth - 0.05f; d += spacing)
            FacadeBox(metal, frame, bu - 0.011f, v + 0.06f, d, bu + 0.011f, v + rail, d + 0.022f);
    }

    // What people put on a balcony: a planter hung on the rail on about half
    // of them, a folding chair on a few. A balcony with something on it is a
    // flat that is lived in; a row of empty ones is a drawing.
    if (rng.chance(0.5f))
    {
        MeshBuilder& trough = collector.builder(&materials_.get(MaterialId::Timber));
        trough.setTileSize(0.5f);
        const float a = u + rng.range(0.10f, 0.30f) * width;
        const float b = a + std::min(0.9f, width * 0.55f);
        FacadeBox(trough, frame, a, v + rail - 0.24f, depth - 0.22f, b, v + rail + 0.02f, depth - 0.06f);
        MeshBuilder& plants = collector.builder(&materials_.get(MaterialId::Hedge));
        plants.setTileSize(0.5f);
        const int clumps = std::max(2, static_cast<int>((b - a) / 0.22f));
        for (int i = 0; i < clumps; ++i)
        {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(clumps);
            plants.addEllipsoid(frame.at(a + (b - a) * t, v + rail + 0.05f + rng.range(0.0f, 0.08f),
                                         depth - 0.14f),
                                Vector3(0.13f, rng.range(0.10f, 0.17f), 0.12f), 8, 5);
        }
    }
    if (rng.chance(0.28f))
    {
        MeshBuilder& chair = collector.builder(palette.metal);
        chair.setTileSize(0.3f);
        const float cu = u + width - 0.55f;
        const float cd = depth * 0.45f;
        FacadeBox(chair, frame, cu, v + 0.42f, cd - 0.20f, cu + 0.42f, v + 0.46f, cd + 0.20f);
        FacadeBox(chair, frame, cu, v + 0.46f, cd - 0.20f, cu + 0.42f, v + 0.88f, cd - 0.16f);
        for (const float lu : {cu + 0.02f, cu + 0.38f})
            for (const float ld : {cd - 0.18f, cd + 0.16f})
                FacadeBox(chair, frame, lu, v, ld, lu + 0.02f, v + 0.42f, ld + 0.02f);
    }
}

void BuildingBuilder::buildShopfront(const Plot& plot, int plotIndex, const FacadeFrame& frame,
                                     const Palette& palette, GeometryCollector& collector,
                                     Rng& rng, GeometryCollector& interiors,
                                     std::vector<FacadeAnchor>& anchors,
                                     std::vector<ShopDisplay>& displays,
                                     std::vector<Opening>& openings)
{
    const float base   = M::kCurbHeight;
    const float sill   = base + M::kShopSillHeight;
    const float head   = sill + M::kShopGlassHeight;
    const float fascia = head + M::kShopFasciaHeight;
    const float margin = 0.55f;

    const float u0 = margin;
    const float u1 = frame.width - margin;
    if (u1 - u0 < 2.0f) return;
    const bool hero = plotIndex == heroPlot_;
    // The whole shopfront is one opening in the wall, from the pavement to the
    // underside of the fascia.
    openings.push_back(Opening{u0, base, u1, head + 0.05f});

    // The stallriser: the solid panel under the glass that takes the kicks.
    MeshBuilder& plinth = collector.builder(palette.plinth);
    plinth.setTileSize(1.2f);
    WallBox(plinth, frame, u0, base, u1, sill, 0.055f);

    // The glazing, set back behind its frame so the shopfront has depth.
    const float glassDepth = -0.11f;
    MeshBuilder& glass = collector.builder(palette.shopGlass);
    glass.setTileSize(2.4f);

    // The door is at one end, and the display window fills the rest.
    const bool doorLeft = rng.chance(0.5f);
    const float doorWidth = 1.05f;
    const float doorU = doorLeft ? u0 + 0.12f : u1 - doorWidth - 0.12f;

    // The cafe's joinery is dark bronze rather than the street's white: a
    // cafe paints its shopfront, and dark frames make the glass read as
    // glass and the room behind it read as lit.
    MeshBuilder& sash = collector.builder(hero ? &materials_.get(MaterialId::FrameBronze)
                                               : palette.frame);
    sash.setTileSize(0.5f);

    // Mullions every 1.1-1.5 m across the display window.
    const float displayFrom = doorLeft ? doorU + doorWidth + 0.10f : u0;
    const float displayTo   = doorLeft ? u1 : doorU - 0.10f;
    Panel(glass, frame, displayFrom, sill, displayTo, head, glassDepth);
    const int mullions = std::max(1, static_cast<int>((displayTo - displayFrom) / 1.30f));
    for (int i = 1; i < mullions; ++i)
    {
        const float mu = displayFrom
                         + static_cast<float>(i) * (displayTo - displayFrom)
                               / static_cast<float>(mullions);
        FacadeBox(sash, frame, mu - 0.032f, sill, glassDepth - 0.03f, mu + 0.032f, head, 0.02f);
    }
    // The shopfront's own frame: cill, jambs and head.
    FacadeBox(sash, frame, displayFrom - 0.05f, sill - 0.05f, glassDepth - 0.03f,
              displayTo + 0.05f, sill, 0.045f);
    FacadeBox(sash, frame, displayFrom - 0.05f, head, glassDepth - 0.03f, displayTo + 0.05f,
              head + 0.05f, 0.045f);
    FacadeBox(sash, frame, displayFrom - 0.05f, sill, glassDepth - 0.03f, displayFrom,
              head, 0.045f);
    FacadeBox(sash, frame, displayTo, sill, glassDepth - 0.03f, displayTo + 0.05f, head, 0.045f);

    // The shop door, set back into a proper opening. A door flush with the
    // glass beside it is a painted door; one a third of a metre back, with
    // the wall returning into the reveal on both sides and a step up to it,
    // is somewhere a person walks in. The reveal is in the plinth material
    // and the wall's, the way a shopfront's return actually is.
    const float doorDepth = -0.34f;
    const float leaf = doorDepth + 0.05f;
    {
        MeshBuilder& reveal = collector.builder(palette.plinth);
        reveal.setTileSize(1.2f);
        const float rl = doorU - 0.03f, rr = doorU + doorWidth + 0.03f;
        reveal.addQuad(frame.at(rl, base, glassDepth), frame.at(rl, base, doorDepth),
                       frame.at(rl, head, doorDepth), frame.at(rl, head, glassDepth));
        reveal.addQuad(frame.at(rr, base, doorDepth), frame.at(rr, base, glassDepth),
                       frame.at(rr, head, glassDepth), frame.at(rr, head, doorDepth));
        reveal.addQuad(frame.at(rl, head, glassDepth), frame.at(rl, head, doorDepth),
                       frame.at(rr, head, doorDepth), frame.at(rr, head, glassDepth));
        // The threshold: a stone step a shoe's depth, chamfered, and the
        // recess floor behind it.
        BevelBox(reveal, frame, rl - 0.04f, base - 0.01f, doorDepth - 0.02f, rr + 0.04f,
                 base + 0.035f, glassDepth + 0.10f, 0.012f);
    }
    Panel(glass, frame, doorU + 0.06f, base + 0.30f, doorU + doorWidth - 0.06f, head - 0.05f,
          leaf + 0.02f);
    FacadeBox(sash, frame, doorU, base + 0.035f, leaf - 0.02f, doorU + 0.06f, head, leaf + 0.03f);
    FacadeBox(sash, frame, doorU + doorWidth - 0.06f, base + 0.035f, leaf - 0.02f,
              doorU + doorWidth, head, leaf + 0.03f);
    FacadeBox(sash, frame, doorU, base + 0.035f, leaf - 0.02f, doorU + doorWidth, base + 0.30f,
              leaf + 0.03f);
    FacadeBox(sash, frame, doorU, head - 0.05f, leaf - 0.02f, doorU + doorWidth, head,
              leaf + 0.03f);
    if (hero)
    {
        // What a cafe sticks on its glass: the opening hours beside the
        // handle, a poster in the corner of the window, and a lamp on the
        // pilaster for the evening. Small, legible, and the kind of thing a
        // shop has and a generator forgets.
        MeshBuilder& notices = collector.builder(&materials_.get(MaterialId::ShopPoster));
        notices.setUvMode(UvMode::Explicit);
        const float nu = doorLeft ? doorU + 0.18f : doorU + doorWidth - 0.40f;
        notices.addQuadUv(frame.at(nu, base + 1.45f, leaf + 0.028f),
                          frame.at(nu + 0.22f, base + 1.45f, leaf + 0.028f),
                          frame.at(nu + 0.22f, base + 1.75f, leaf + 0.028f),
                          frame.at(nu, base + 1.75f, leaf + 0.028f),
                          Vector2(0.0f, 1.0f), Vector2(0.5f, 1.0f), Vector2(0.5f, 0.5f),
                          Vector2(0.0f, 0.5f));
        const float pu = doorLeft ? displayTo - 0.55f : displayFrom + 0.12f;
        notices.addQuadUv(frame.at(pu, sill + 1.55f, glassDepth + 0.012f),
                          frame.at(pu + 0.42f, sill + 1.55f, glassDepth + 0.012f),
                          frame.at(pu + 0.42f, sill + 2.14f, glassDepth + 0.012f),
                          frame.at(pu, sill + 2.14f, glassDepth + 0.012f),
                          Vector2(0.0f, 0.5f), Vector2(0.5f, 0.5f), Vector2(0.5f, 0.0f),
                          Vector2(0.0f, 0.0f));
        MeshBuilder& lamp = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
        lamp.setTileSize(0.3f);
        const float lu = doorLeft ? u0 - 0.17f : u1 + 0.17f;
        lamp.addCylinderBetween(frame.at(lu, head - 0.55f, 0.13f), frame.at(lu, head - 0.55f, 0.42f),
                                0.014f, 6);
        lamp.addCylinder(frame.at(lu, head - 0.55f - 0.30f, 0.42f), 0.075f, 0.11f, 0.30f, 10,
                         true, false);
        MeshBuilder& lampGlass = collector.builder(&materials_.get(MaterialId::LampGlass));
        lampGlass.addSphere(frame.at(lu, head - 0.55f - 0.26f, 0.42f), 0.045f, 8, 6);
        // A hanging sign on a wrought bracket at the corner of the fascia,
        // square to the wall, so the cafe reads from along the footway and
        // not only from across the road.
        {
            MeshBuilder& bracket = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
            bracket.setTileSize(0.3f);
            const float bu = doorLeft ? u1 + 0.20f : u0 - 0.20f;
            const float bv = fascia - 0.02f;
            bracket.addCylinderBetween(frame.at(bu, bv, 0.12f), frame.at(bu, bv, 0.95f), 0.016f, 6);
            bracket.addCylinderBetween(frame.at(bu, bv - 0.55f, 0.12f), frame.at(bu, bv - 0.04f, 0.88f),
                                       0.012f, 6);
            for (const float hd : {0.30f, 0.86f})
                bracket.addCylinderBetween(frame.at(bu, bv, hd), frame.at(bu, bv - 0.16f, hd), 0.008f, 5);
            MeshBuilder& board = collector.builder(palette.fascia);
            board.setTileSize(0.6f);
            const Vector3 a = frame.at(bu - 0.018f, bv - 0.16f, 0.26f);
            const Vector3 b = frame.at(bu + 0.018f, bv - 0.62f, 0.90f);
            board.addBox(Vector3(std::min(a.X, b.X), std::min(a.Y, b.Y), std::min(a.Z, b.Z)),
                         Vector3(std::max(a.X, b.X), std::max(a.Y, b.Y), std::max(a.Z, b.Z)));
        }
        // A potted plant by the door, on the footway.
        if (heroProps_ != nullptr)
        {
            const float plantU = doorLeft ? doorU + doorWidth + 0.55f : doorU - 0.55f;
            const Vector3 p = frame.at(plantU, base, 0.36f);
            const float yaw = std::atan2(frame.out.X, frame.out.Z);
            heroProps_->push_back(HeroProp{"ph-potted-plant-01",
                                           Geometry::Place(p.X, p.Y, p.Z, yaw + 0.4f), 0.0f,
                                           false, 60.0f});
        }
    }

    // The head of the door, for the camera or the lamp the scene hangs there.
    {
        FacadeAnchor doorHead;
        doorHead.position  = frame.at(doorU + doorWidth * 0.5f, head + 0.08f, 0.0f);
        doorHead.normal    = frame.out;
        doorHead.width     = doorWidth;
        doorHead.height    = 0.2f;
        doorHead.kind      = FacadeAnchor::Kind::DoorHead;
        doorHead.plotIndex = plotIndex;
        anchors.push_back(doorHead);
    }

    const ShopKind kind = hero ? ShopKind::Bakery : shopKindFor(plot, plotIndex);
    if (hero)
        buildHeroBakery(frame, u0 + 0.03f, u1 - 0.03f, base, head, plotIndex, interiors, rng,
                        &displays);
    else
        buildShopInterior(frame, u0 + 0.03f, u1 - 0.03f, base, head, kind, plotIndex, interiors,
                          rng, &displays);

    // A shop that is shut: a roller blind three quarters down behind the
    // glass. One in six, decided from the plot alone so the fascia and the
    // display know about it too. Imperfect occupancy is the single cheapest
    // thing that says a street is real: nobody believes forty units all
    // trading at once, and a blind is the shape of a shut shop.
    const bool shut = !hero && kind != ShopKind::Vacant
                      && static_cast<float>(Noise::hash2(plotIndex, 41, plot.seed) & 0xFFFFu)
                                 / 65536.0f
                             < 0.17f;
    if (shut)
    {
        MeshBuilder& blind = collector.builder(&materials_.get(MaterialId::ShopBlind));
        blind.setTileSize(0.6f);
        const float drop = head - rng.range(0.55f, 0.85f) * (head - sill);
        // Hung just inside the glass, one quad per pane so the mullions stay
        // in front of it, with a roller box at the head.
        FacadeBox(blind, frame, displayFrom - 0.02f, head - 0.14f, glassDepth - 0.16f,
                  displayTo + 0.02f, head, glassDepth - 0.04f);
        blind.addQuadFacing(frame.at(displayFrom, drop, glassDepth - 0.10f),
                            frame.at(displayTo, drop, glassDepth - 0.10f),
                            frame.at(displayTo, head - 0.14f, glassDepth - 0.10f),
                            frame.at(displayFrom, head - 0.14f, glassDepth - 0.10f), frame.out);
        // The bottom bar.
        FacadeBox(sash, frame, displayFrom, drop - 0.03f, glassDepth - 0.115f, displayTo,
                  drop + 0.01f, glassDepth - 0.085f);
    }
    // A pull handle: a vertical bar on two stubs rather than a block, on the
    // lock side, at hand height.
    MeshBuilder& metal = collector.builder(palette.metal);
    metal.setTileSize(0.3f);
    {
        const float hu = doorLeft ? doorU + doorWidth - 0.16f : doorU + 0.16f;
        metal.addCylinderBetween(frame.at(hu, base + 0.88f, leaf + 0.075f),
                                 frame.at(hu, base + 1.38f, leaf + 0.075f), 0.013f, 8);
        for (const float hv : {base + 0.94f, base + 1.32f})
            metal.addCylinderBetween(frame.at(hu, hv, leaf + 0.02f),
                                     frame.at(hu, hv, leaf + 0.075f), 0.009f, 6);
    }

    // The fascia board above, and the corbels at each end.
    MeshBuilder& board = collector.builder(palette.fascia);
    board.setTileSize(1.5f);
    WallBox(board, frame, u0 - 0.10f, head + 0.05f, u1 + 0.10f, fascia, 0.10f);
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    // Pilasters the full height of the shopfront at both ends, a hand proud
    // and chamfered, with the corbel block over each: the shopfront is a
    // frame standing in the wall, not a hole cut in it.
    WallBevel(trim, frame, u0 - 0.30f, base, u0 - 0.04f, head, 0.13f, 0.02f);
    WallBevel(trim, frame, u1 + 0.04f, base, u1 + 0.30f, head, 0.13f, 0.02f);
    WallBevel(trim, frame, u0 - 0.34f, head, u0 - 0.02f, fascia + 0.06f, 0.18f, 0.02f);
    WallBevel(trim, frame, u1 + 0.02f, head, u1 + 0.34f, fascia + 0.06f, 0.18f, 0.02f);
    WallBevel(trim, frame, u0 - 0.36f, fascia, u1 + 0.36f, fascia + 0.09f, 0.21f, 0.025f);

    FacadeAnchor anchor;
    anchor.position = frame.at((u0 + u1) * 0.5f, (head + 0.05f + fascia) * 0.5f, 0.105f);
    anchor.normal   = frame.out;
    anchor.width    = (u1 - u0) + 0.20f;
    anchor.height   = fascia - head - 0.05f;
    anchor.kind     = FacadeAnchor::Kind::ShopFascia;
    anchor.plotIndex = plotIndex;
    anchors.push_back(anchor);

    // A retractable awning over about half the shops, half of them out; the
    // hero cafe's is always out, because a cafe's awning is its sign.
    if (rng.chance(0.42f) || hero)
    {
        MeshBuilder& awning = collector.builder(&materials_.get(MaterialId::Awning));
        awning.setTileSize(0.9f);
        const float projection = rng.range(0.9f, 1.5f);
        const float top = head - 0.04f;
        const float front = top - 0.38f;
        // A retractable awning comes in standard widths and is fitted per bay,
        // so a 20 m shopfront gets one over part of it rather than one 20 m
        // canopy -- which is what the first version built, and it roofed the
        // whole footway.
        const float span = std::min(u1 - u0, 4.6f);
        const float a0 = (u0 + u1) * 0.5f - span * 0.5f;
        const float a1 = a0 + span;

        // Canopy, underside and valance, each told which way it faces: a canopy
        // wound the wrong way is a dark slab over the footway in full sun.
        const Vector3 upAndOut = frame.up * 0.86f + frame.out * 0.51f;
        awning.addQuadFacing(frame.at(a0, top, 0.05f), frame.at(a1, top, 0.05f),
                             frame.at(a1, front, projection), frame.at(a0, front, projection),
                             upAndOut);
        awning.addQuadFacing(frame.at(a0, top - 0.03f, 0.05f), frame.at(a1, top - 0.03f, 0.05f),
                             frame.at(a1, front - 0.03f, projection),
                             frame.at(a0, front - 0.03f, projection), upAndOut * -1.0f);
        awning.addQuadFacing(frame.at(a0, front, projection), frame.at(a1, front, projection),
                             frame.at(a1, front - 0.24f, projection),
                             frame.at(a0, front - 0.24f, projection), frame.out);
        // The front bar the canopy rolls onto, and the box the roller sits in
        // against the wall. Real retractable awnings have both, and they are what
        // keeps the canopy from reading as a floating triangle.
        MeshBuilder& bar = collector.builder(palette.metal);
        bar.setTileSize(0.3f);
        bar.addCylinderBetween(frame.at(a0 - 0.06f, front - 0.28f, projection),
                               frame.at(a1 + 0.06f, front - 0.28f, projection), 0.035f, 8);
        bar.addBox(frame.at(a0 - 0.06f, top - 0.02f, 0.02f) - Vector3(0.08f, 0.08f, 0.08f),
                   frame.at(a1 + 0.06f, top + 0.14f, 0.20f) + Vector3(0.08f, 0.08f, 0.08f));
        // The folding arms, which is what a viewer reads as "this is an awning"
        // rather than "this is a wedge".
        for (const float u : {a0 + 0.14f, a1 - 0.14f})
            bar.addCylinderBetween(frame.at(u, top - 0.10f, 0.06f),
                                   frame.at(u, front - 0.24f, projection - 0.03f), 0.022f, 6);
    }
    (void)plot;
}

void BuildingBuilder::buildFacade(const Plot& plot, int plotIndex, const FacadeFrame& frame,
                                  const Palette& palette, GeometryCollector& collector, Rng& rng,
                                  GeometryCollector& interiors,
                                  std::vector<FacadeAnchor>& anchors,
                                  std::vector<ShopDisplay>& displays, bool primary)
{
    const bool classical = plot.style == BuildingStyle::Gruenderzeit
                           || plot.style == BuildingStyle::CornerBlock
                           || plot.style == BuildingStyle::BrickWarehouse;
    const bool office = plot.style == BuildingStyle::ModernOffice;

    const float wallFrom = M::kCurbHeight + M::kPlinthHeight;
    dressing_ = Rng::derive(plotSeed_, primary ? "dressing-front" : "dressing-side");
    // Every opening this elevation cuts. The wall is built last, around them.
    std::vector<Opening> openings;

    // --- storeys -----------------------------------------------------------
    std::vector<float> floorLevel;
    floorLevel.push_back(M::kCurbHeight);
    floorLevel.push_back(M::kCurbHeight + plot.groundFloorHeight);
    for (int storey = 1; storey < plot.storeys; ++storey)
        floorLevel.push_back(floorLevel.back() + plot.upperFloorHeight);

    // --- ground floor -------------------------------------------------------
    if (plot.hasShop && primary)
    {
        buildShopfront(plot, plotIndex, frame, palette, collector, rng, interiors, anchors,
                       displays, openings);
    }
    else if (office)
    {
        // A modern entrance: full-height glazing between two piers.
        const float base = M::kCurbHeight;
        const float head = floorLevel[1] - 0.35f;
        openings.push_back(Opening{0.9f, base, frame.width - 0.9f, head});
        MeshBuilder& glass = collector.builder(palette.shopGlass);
        glass.setTileSize(3.0f);
        Panel(glass, frame, 0.9f, base, frame.width - 0.9f, head, -0.20f);
        MeshBuilder& sash = collector.builder(palette.frame);
        sash.setTileSize(0.6f);
        const int bays = std::max(2, static_cast<int>((frame.width - 1.8f) / 1.6f));
        for (int i = 0; i <= bays; ++i)
        {
            const float u = 0.9f + static_cast<float>(i) * (frame.width - 1.8f)
                                       / static_cast<float>(bays);
            FacadeBox(sash, frame, u - 0.045f, base, -0.24f, u + 0.045f, head, -0.02f);
        }
        FacadeBox(sash, frame, 0.9f, head - 0.09f, -0.24f, frame.width - 0.9f, head, -0.02f);
        buildShopInterior(frame, 0.9f, frame.width - 0.9f, base, head, ShopKind::Office, plotIndex,
                          interiors, rng, nullptr);
    }
    else
    {
        const float entranceU = rng.range(0.9f, std::max(1.0f, frame.width - 2.4f));
        buildEntrance(frame, entranceU, M::kCurbHeight + 0.16f, palette, collector, rng,
                      openings);
        // Ground-floor windows either side of the door.
        const float windowV = M::kCurbHeight + 1.05f;
        const float windowH = plot.groundFloorHeight - 2.0f;
        for (float u = 0.75f; u < frame.width - 1.7f; u += 2.5f)
        {
            if (u + 1.2f > entranceU - 0.35f && u < entranceU + M::kEntranceDoorWidth + 0.35f)
                continue;
            buildWindow(frame, u, windowV, 1.20f, std::max(1.2f, windowH), palette, collector, rng,
                        classical, openings);
        }
    }

    // --- upper storeys ------------------------------------------------------
    // Bay pitch: aim for 2.4-3.2 m and divide the elevation evenly, because a
    // façade whose end bays are a different width from the middle ones is the
    // first thing that reads as procedural.
    const float targetPitch = office ? 2.10f : 2.85f;
    const int bays = std::max(1, static_cast<int>(std::round((frame.width - 1.0f) / targetPitch)));
    const float pitch = frame.width / static_cast<float>(bays);
    const float windowWidth = office ? std::min(pitch - 0.45f, 1.75f)
                                     : std::min(pitch - 1.15f, M::kWindowWidth);

    // Which bays carry a balcony, decided once so they line up vertically the
    // way a real building's do.
    std::vector<bool> balconyBay(static_cast<std::size_t>(bays), false);
    if (classical && bays >= 3)
        for (int bay = 1; bay + 1 < bays; ++bay)
            balconyBay[static_cast<std::size_t>(bay)] = rng.chance(0.34f);

    for (int storey = 1; storey < plot.storeys; ++storey)
    {
        const float floor = floorLevel[static_cast<std::size_t>(storey)];
        const float storeyHeight = (storey + 1 < static_cast<int>(floorLevel.size()))
                                       ? floorLevel[static_cast<std::size_t>(storey) + 1] - floor
                                       : plot.upperFloorHeight;
        const float sill = floor + M::kWindowSill;
        float windowHeight = office ? storeyHeight - 1.05f
                                    : std::min(M::kWindowHeight, storeyHeight - 1.12f);
        // The piano nobile: the first floor of a nineteenth-century block has
        // taller windows than the ones above it, and getting that wrong is what
        // makes a façade look like a spreadsheet.
        if (classical && storey == 1) windowHeight *= 1.12f;

        for (int bay = 0; bay < bays; ++bay)
        {
            const float centre = (static_cast<float>(bay) + 0.5f) * pitch;
            const float u = centre - windowWidth * 0.5f;
            const bool french = classical && balconyBay[static_cast<std::size_t>(bay)]
                                && storey <= 2;
            if (french)
            {
                buildWindow(frame, u - 0.06f, floor + 0.10f, windowWidth + 0.12f,
                            M::kBalconyDoorHeight, palette, collector, rng, classical, openings);
                buildBalcony(frame, centre - M::kBalconyWidth * 0.5f, floor + 0.10f,
                             std::min(M::kBalconyWidth, pitch - 0.35f), palette, collector, rng);
            }
            else
            {
                buildWindow(frame, u, sill, windowWidth, windowHeight, palette, collector, rng,
                            classical && storey <= 2, openings);
            }
        }

        // A string course between storeys, on the older styles only.
        if (classical && storey >= 1 && storey + 1 < plot.storeys && rng.chance(0.65f))
        {
            MeshBuilder& trim = collector.builder(palette.trim);
            trim.setTileSize(1.0f);
            const float v = floor + storeyHeight - 0.30f;
            WallBevel(trim, frame, -0.02f, v, frame.width + 0.02f,
                      v + M::kStringCourseHeight, M::kStringCourseProjection, 0.02f);
        }
    }

    // A satellite dish or two on the older blocks, clamped to the wall beside
    // an upper window and tilted at the sky, and a cable dropping from it.
    // Ordinary, ugly and everywhere, which is the point.
    if (!office && plot.storeys >= 3 && rng.chance(0.55f))
    {
        MeshBuilder& dish = collector.builder(&materials_.get(MaterialId::PaintedSteelGrey));
        dish.setTileSize(0.3f);
        const int dishes = rng.intRange(1, 2);
        for (int i = 0; i < dishes; ++i)
        {
            const int bay = rng.intRange(0, bays - 1);
            const int storey = rng.intRange(std::max(1, plot.storeys - 3), plot.storeys - 1);
            const float du = (static_cast<float>(bay) + 0.5f) * pitch + windowWidth * 0.5f + 0.42f;
            if (du > frame.width - 0.5f) continue;
            const float dv = floorLevel[static_cast<std::size_t>(storey)] + M::kWindowSill + 0.9f;
            const Vector3 mount = frame.at(du, dv, 0.0f);
            const Vector3 arm = frame.at(du, dv - 0.05f, 0.30f);
            // Tilted up and toward the south: a dish points at the sky, not
            // at the street.
            const Vector3 facing =
                Vector3::Normalize(frame.out * 0.55f + frame.up * 0.75f + frame.right * 0.25f);
            dish.addCylinderBetween(mount, arm, 0.016f, 6);
            dish.addDiscFacing(arm + facing * 0.12f, facing, 0.30f, 14);
            dish.addCylinderBetween(arm + facing * 0.12f, arm + facing * 0.34f, 0.010f, 5);
            // The cable, down the wall to the window below.
            MeshBuilder& cable = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
            cable.addCylinderBetween(mount, frame.at(du, dv - 1.1f, 0.012f), 0.006f, 4, false);
        }
    }

    // --- the services on the wall ---------------------------------------------
    // What a real elevation carries and a generated one never does: a meter
    // cabinet by the door, a vent through the wall, a conduit run along the
    // storey line with its saddles, a lamp over the entrance. None of it is
    // structure and all of it is what the eye reads as "somebody uses this
    // building". Kept to the lower two storeys, where a camera on the footway
    // can see it, and drawn from the plot's own seed so a building keeps
    // its fittings when its neighbour changes.
    if (!office && frame.width > 6.0f)
    {
        Rng fittings = Rng::derive(plotSeed_, "fittings");
        MeshBuilder& grey = collector.builder(&materials_.get(MaterialId::CabinetGrey));
        grey.setTileSize(0.5f);
        MeshBuilder& dark = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
        dark.setTileSize(0.3f);
        // A meter cabinet or a bell panel beside the entrance, on the
        // primary elevation, at a height a hand reaches.
        if (primary && fittings.chance(0.7f))
        {
            const float mu = std::clamp(fittings.range(0.35f, 0.9f) * frame.width, 0.4f,
                                        frame.width - 0.8f);
            const float mv = M::kCurbHeight + fittings.range(1.15f, 1.45f);
            BevelBox(grey, frame, mu, mv, -0.01f, mu + 0.36f, mv + 0.48f, 0.09f, 0.008f);
        }
        // A vent grille through the wall, low down, on the flank of a shop
        // unit or beside a residential door.
        if (fittings.chance(0.55f))
        {
            const float gu = std::clamp(fittings.range(0.1f, 0.9f) * frame.width, 0.3f,
                                        frame.width - 0.6f);
            const float gv = M::kCurbHeight + fittings.range(0.7f, 2.4f);
            BevelBox(dark, frame, gu, gv, -0.03f, gu + 0.24f, gv + 0.24f, 0.018f, 0.005f);
            for (int slat = 1; slat < 5; ++slat)
                FacadeBox(dark, frame, gu + 0.02f, gv + 0.045f * static_cast<float>(slat),
                          0.018f, gu + 0.22f, gv + 0.045f * static_cast<float>(slat) + 0.012f,
                          0.03f);
        }
        // A conduit run at the first-floor line, with a saddle every metre
        // and a drop down to a box: the cable television or the phone line
        // that was added a century after the building.
        if (fittings.chance(0.45f) && floorLevel.size() > 1)
        {
            const float cv = floorLevel[1] - fittings.range(0.15f, 0.40f);
            const float from = fittings.range(0.0f, 0.3f) * frame.width;
            const float to   = frame.width - fittings.range(0.0f, 0.2f) * frame.width;
            dark.addCylinderBetween(frame.at(from, cv, 0.028f), frame.at(to, cv, 0.028f), 0.014f,
                                    6, false);
            for (float su = from + 0.5f; su < to - 0.3f; su += 1.1f)
                FacadeBox(dark, frame, su - 0.02f, cv - 0.03f, 0.0f, su + 0.02f, cv + 0.03f,
                          0.045f);
            const float dropU = std::clamp(fittings.range(0.2f, 0.8f) * frame.width, from,
                                           to);
            dark.addCylinderBetween(frame.at(dropU, cv, 0.028f),
                                    frame.at(dropU, M::kCurbHeight + 2.2f, 0.028f), 0.014f, 6,
                                    false);
            BevelBox(grey, frame, dropU - 0.12f, M::kCurbHeight + 1.95f, -0.01f, dropU + 0.12f,
                     M::kCurbHeight + 2.25f, 0.08f, 0.006f);
        }
    }
    // Where a condenser unit hangs: beside an upper window on the flat
    // elevations, on about one bay in twelve -- the scan is five thousand
    // triangles and a wall of them is a wall of them. The scene stands the scanned unit
    // there; here is only the anchor.
    if (flatFacade_ && plot.storeys >= 3)
    {
        Rng units = Rng::derive(plotSeed_, "aircon");
        for (int storey = 1; storey < plot.storeys; ++storey)
            for (int bay = 0; bay < bays; ++bay)
            {
                if (!units.chance(0.08f)) continue;
                const float centre = (static_cast<float>(bay) + 0.5f) * pitch;
                const float au = centre + windowWidth * 0.5f + 0.42f;
                if (au > frame.width - 0.6f) continue;
                FacadeAnchor anchor;
                anchor.position  = frame.at(au, floorLevel[static_cast<std::size_t>(storey)]
                                                    + M::kWindowSill + 0.05f, 0.0f);
                anchor.normal    = frame.out;
                anchor.width     = 0.8f;
                anchor.height    = 0.55f;
                anchor.kind      = FacadeAnchor::Kind::AirCon;
                anchor.plotIndex = plotIndex;
                anchors.push_back(anchor);
            }
    }

    // --- the wall, at last --------------------------------------------------
    buildWallPanels(frame, wallFrom, frame.height, std::move(openings), palette, collector);

    // The splash zone: every wall is dirtiest in the half metre above the
    // pavement, where the rain bounces and the feet pass. In pieces of a few
    // metres so the atlas cell does not stretch, and only where the wall is
    // exposed rather than behind a shopfront.
    if (!(plot.hasShop && primary))
    {
        const float top = wallFrom + rng.range(0.35f, 0.65f) * (0.5f + weathering_);
        for (float u = 0.0f; u < frame.width - 0.5f; u += 3.0f)
        {
            if (!rng.chance(0.4f + weathering_ * 0.5f)) continue;
            grimeDecal(frame, u, wallFrom - 0.02f, std::min(u + 3.0f, frame.width), top, 1,
                       collector, rng.chance(0.5f));
        }
    }

    // --- quoins ---------------------------------------------------------------
    // Alternating long and short blocks up both corners of the masonry
    // blocks that have them, a finger proud of the wall with an arris on
    // each: the corner is where two elevations meet, and a corner that is a
    // line is the thing that says the building is a box.
    const float eaves = frame.height;
    if (quoins_ && classical)
    {
        MeshBuilder& quoin = collector.builder(palette.trim);
        quoin.setTileSize(0.6f);
        const float course = 0.30f, joint = 0.02f;
        int row = 0;
        for (float qv = wallFrom + 0.06f; qv + course < eaves - 0.56f; qv += course + joint, ++row)
        {
            const float w = row % 2 == 0 ? 0.44f : 0.30f;
            BevelBox(quoin, frame, -0.015f, qv, -0.01f, w, qv + course, 0.032f, 0.012f);
            BevelBox(quoin, frame, frame.width - w, qv, -0.01f, frame.width + 0.015f, qv + course,
                     0.032f, 0.012f);
        }
    }

    // --- cornice ------------------------------------------------------------
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);
    if (classical)
    {
        // A three-part cornice: frieze, dentil band, and the projecting corona.
        WallBox(trim, frame, -0.04f, eaves - 0.52f, frame.width + 0.04f, eaves - 0.34f, 0.10f);
        const int dentils = std::max(4, static_cast<int>(frame.width / 0.26f));
        for (int i = 0; i < dentils; ++i)
        {
            const float u = (static_cast<float>(i) + 0.25f) * (frame.width / static_cast<float>(dentils));
            WallBox(trim, frame, u, eaves - 0.34f, u + 0.13f, eaves - 0.20f, 0.24f);
        }
        WallBevel(trim, frame, -0.10f, eaves - 0.20f, frame.width + 0.10f, eaves,
                  M::kCorniceProjection, 0.035f);
    }
    else
    {
        // A flat roof's edge: a coping with a drip, and the parapet's line
        // read as two members rather than one slab.
        WallBevel(trim, frame, -0.06f, eaves - 0.22f, frame.width + 0.06f, eaves - 0.06f, 0.12f,
                  0.02f);
        WallBevel(trim, frame, -0.08f, eaves - 0.06f, frame.width + 0.08f, eaves, 0.17f, 0.02f);
    }

    buildRainwaterGoods(plot, frame, palette, collector);

    // A street-name plate and a house number go on the primary elevation.
    if (primary)
    {
        FacadeAnchor number;
        number.position  = frame.at(0.35f, M::kCurbHeight + 2.35f, 0.03f);
        number.normal    = frame.out;
        number.width     = 0.26f;
        number.height    = 0.34f;
        number.kind      = FacadeAnchor::Kind::HouseNumber;
        number.plotIndex = plotIndex;
        anchors.push_back(number);
    }
}

void BuildingBuilder::buildWallPanels(const FacadeFrame& frame, float from, float to,
                                      std::vector<Opening> openings, const Palette& palette,
                                      GeometryCollector& collector)
{
    MeshBuilder& wall = collector.builder(palette.wall);
    wall.setTileSize(2.0f);

    if (openings.empty())
    {
        Panel(wall, frame, 0.0f, from, frame.width, to, 0.0f);
        return;
    }

    // Clamp everything into the wall and drop anything degenerate, so a feature
    // placed slightly outside the elevation cannot produce an inverted strip.
    for (Opening& opening : openings)
    {
        opening.u0 = std::clamp(opening.u0, 0.0f, frame.width);
        opening.u1 = std::clamp(opening.u1, 0.0f, frame.width);
        opening.v0 = std::clamp(opening.v0, from, to);
        opening.v1 = std::clamp(opening.v1, from, to);
    }
    openings.erase(std::remove_if(openings.begin(), openings.end(),
                                  [](const Opening& o) {
                                      return o.u1 - o.u0 < 1e-3f || o.v1 - o.v0 < 1e-3f;
                                  }),
                   openings.end());
    if (openings.empty())
    {
        Panel(wall, frame, 0.0f, from, frame.width, to, 0.0f);
        return;
    }

    // A scanline decomposition, not a row grouping. Bands are delimited by every
    // opening edge, so inside one band an opening either spans it completely or
    // is absent -- which means the wall strips between them can never overlap.
    // The row-grouping version this replaced emitted two coplanar strips wherever
    // a balcony door on one storey overlapped a window on the next, and the
    // façade came out striped with depth fighting.
    std::vector<float> levels{from, to};
    levels.reserve(openings.size() * 2 + 2);
    for (const Opening& opening : openings)
    {
        levels.push_back(opening.v0);
        levels.push_back(opening.v1);
    }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end(),
                             [](float a, float b) { return std::fabs(a - b) < 1e-3f; }),
                 levels.end());

    std::vector<std::pair<float, float>> spans;
    std::vector<std::pair<float, float>> merged;
    for (std::size_t band = 0; band + 1 < levels.size(); ++band)
    {
        const float v0 = levels[band];
        const float v1 = levels[band + 1];
        if (v1 - v0 < 1e-3f) continue;

        spans.clear();
        for (const Opening& opening : openings)
            if (opening.v0 <= v0 + 1e-3f && opening.v1 >= v1 - 1e-3f)
                spans.emplace_back(opening.u0, opening.u1);

        if (spans.empty())
        {
            Panel(wall, frame, 0.0f, v0, frame.width, v1, 0.0f);
            continue;
        }

        // Merge overlapping openings first, then emit the gaps between them.
        std::sort(spans.begin(), spans.end());
        merged.clear();
        for (const std::pair<float, float>& span : spans)
        {
            if (!merged.empty() && span.first <= merged.back().second + 1e-3f)
                merged.back().second = std::max(merged.back().second, span.second);
            else
                merged.push_back(span);
        }

        float cursor = 0.0f;
        for (const std::pair<float, float>& span : merged)
        {
            if (span.first > cursor + 1e-3f) Panel(wall, frame, cursor, v0, span.first, v1, 0.0f);
            cursor = std::max(cursor, span.second);
        }
        if (cursor < frame.width - 1e-3f)
            Panel(wall, frame, cursor, v0, frame.width, v1, 0.0f);
    }
}

void BuildingBuilder::grimeDecal(const FacadeFrame& frame, float u0, float v0, float u1, float v1,
                                 int cell, GeometryCollector& collector, bool flipU)
{
    // Eight millimetres off the wall: clear of the depth fight, under the sill
    // that casts it. Explicit UVs into one quarter of the atlas.
    MeshBuilder& grime = collector.builder(&materials_.get(MaterialId::FacadeGrime));
    grime.setUvMode(UvMode::Explicit);
    const float cu = static_cast<float>(cell % 2) * 0.5f;
    const float cv = static_cast<float>(cell / 2) * 0.5f;
    const Vector2 uvA(flipU ? cu + 0.5f : cu, cv + 0.5f);
    const Vector2 uvB(flipU ? cu : cu + 0.5f, cv + 0.5f);
    const Vector2 uvC(flipU ? cu : cu + 0.5f, cv);
    const Vector2 uvD(flipU ? cu + 0.5f : cu, cv);
    grime.addQuadUv(frame.at(u0, v0, 0.008f), frame.at(u1, v0, 0.008f), frame.at(u1, v1, 0.008f),
                    frame.at(u0, v1, 0.008f), uvA, uvB, uvC, uvD);
}

void BuildingBuilder::buildRainwaterGoods(const Plot& plot, const FacadeFrame& frame,
                                          const Palette& palette, GeometryCollector& collector)
{
    (void)plot;
    // A downpipe at each end of the elevation, with its shoe at the bottom and
    // its collar brackets up the wall. Small, but their absence is noticed.
    MeshBuilder& metal = collector.builder(palette.metal);
    metal.setTileSize(0.4f);
    for (const float u : {0.16f, frame.width - 0.16f})
    {
        // A leaking joint stains the wall behind the pipe, on about one
        // building in three, more often the more weathered it is.
        const std::uint32_t pick = Noise::hash2(static_cast<int>(u * 100.0f), 7, plotSeed_);
        if (static_cast<float>(pick & 0xFFFFu) / 65536.0f < 0.15f + weathering_ * 0.4f)
        {
            const float top = M::kCurbHeight + 1.2f
                              + static_cast<float>((pick >> 16) & 0xFFu) / 255.0f
                                    * (frame.height - 3.0f);
            grimeDecal(frame, u - 0.28f, M::kCurbHeight + 0.6f, u + 0.28f, top, 2, collector);
        }
        const Vector3 base = frame.at(u, M::kCurbHeight, M::kDownpipeRadius + 0.02f);
        metal.addCylinder(base, M::kDownpipeRadius, M::kDownpipeRadius,
                          frame.height - M::kCurbHeight - 0.25f, 8, false, false);
        // The shoe: a short flared length turning out at the bottom.
        metal.addCylinder(frame.at(u, M::kCurbHeight, M::kDownpipeRadius + 0.02f),
                          M::kDownpipeRadius * 1.45f, M::kDownpipeRadius, 0.22f, 8, true, false);
        for (float v = M::kCurbHeight + 1.2f; v < frame.height - 0.6f; v += 2.0f)
        {
            // A collar bracket: a short stub from the wall to the pipe. Built
            // with the oriented cylinder rather than an axis-aligned box, whose
            // min and max corners swap over on the two elevations whose outward
            // normal points along a negative axis.
            metal.addCylinderBetween(frame.at(u, v, 0.0f),
                                     frame.at(u, v, M::kDownpipeRadius * 2.2f), 0.016f, 6);
        }
    }
}

void BuildingBuilder::buildRoof(const Plot& plot, const Palette& palette,
                                GeometryCollector& collector, Rng& rng)
{
    const float eaves = plot.height();
    const float w = plot.width(), d = plot.depth();
    // The ridge runs *along the street*, always: a perimeter block presents its
    // eaves to the road and its gable to the party wall, and getting this from
    // the plot's longer side instead put a gable end on every second frontage.
    const bool ridgeAlongX = plot.primary == Facing::PosZ || plot.primary == Facing::NegZ;
    const float span = ridgeAlongX ? d : w;

    MeshBuilder& roof = collector.builder(palette.roof);
    roof.setTileSize(2.0f);
    MeshBuilder& trim = collector.builder(palette.trim);
    trim.setTileSize(1.0f);

    switch (plot.roof)
    {
        case RoofStyle::Pitched:
        {
            // Capped, and this matters. A 42-degree pitch over a 24 m deep block
            // computes to an 11 m ridge -- a roof taller than the building under
            // it. Real blocks of this depth carry a truncated or mansard roof;
            // 4.6 m above the eaves is what they actually look like.
            const float rise = std::min(span * 0.5f
                                            * std::tan(MathHelper::ToRadians(M::kRoofPitchDegrees)),
                                        4.6f);
            const float ridge = eaves + rise;
            const float overhang = 0.22f;
            if (ridgeAlongX)
            {
                const float zMid = (plot.minZ + plot.maxZ) * 0.5f;
                AddRoofQuad(roof, Vector3(plot.minX - overhang, eaves, plot.minZ - overhang),
                             Vector3(plot.maxX + overhang, eaves, plot.minZ - overhang),
                             Vector3(plot.maxX + overhang, ridge, zMid),
                             Vector3(plot.minX - overhang, ridge, zMid));
                AddRoofQuad(roof, Vector3(plot.maxX + overhang, eaves, plot.maxZ + overhang),
                             Vector3(plot.minX - overhang, eaves, plot.maxZ + overhang),
                             Vector3(plot.minX - overhang, ridge, zMid),
                             Vector3(plot.maxX + overhang, ridge, zMid));
                // Gable walls at the two ends.
                MeshBuilder& gable = collector.builder(palette.wall);
                gable.setTileSize(2.0f);
                for (const float x : {plot.minX, plot.maxX})
                {
                    const float sign = x == plot.minX ? -1.0f : 1.0f;
                    gable.addTriangle(Vector3(x, eaves, plot.minZ),
                                      sign > 0.0f ? Vector3(x, eaves, plot.maxZ)
                                                  : Vector3(x, ridge, zMid),
                                      sign > 0.0f ? Vector3(x, ridge, zMid)
                                                  : Vector3(x, eaves, plot.maxZ));
                }
            }
            else
            {
                const float xMid = (plot.minX + plot.maxX) * 0.5f;
                AddRoofQuad(roof, Vector3(plot.minX - overhang, eaves, plot.maxZ + overhang),
                             Vector3(plot.minX - overhang, eaves, plot.minZ - overhang),
                             Vector3(xMid, ridge, plot.minZ - overhang),
                             Vector3(xMid, ridge, plot.maxZ + overhang));
                AddRoofQuad(roof, Vector3(plot.maxX + overhang, eaves, plot.minZ - overhang),
                             Vector3(plot.maxX + overhang, eaves, plot.maxZ + overhang),
                             Vector3(xMid, ridge, plot.maxZ + overhang),
                             Vector3(xMid, ridge, plot.minZ - overhang));
                MeshBuilder& gable = collector.builder(palette.wall);
                gable.setTileSize(2.0f);
                for (const float z : {plot.minZ, plot.maxZ})
                {
                    const float sign = z == plot.minZ ? -1.0f : 1.0f;
                    gable.addTriangle(Vector3(plot.minX, eaves, z),
                                      sign > 0.0f ? Vector3(xMid, ridge, z)
                                                  : Vector3(plot.maxX, eaves, z),
                                      sign > 0.0f ? Vector3(plot.maxX, eaves, z)
                                                  : Vector3(xMid, ridge, z));
                }
            }
            // The ridge tile.
            MeshBuilder& metal = collector.builder(palette.metal);
            metal.setTileSize(0.5f);
            if (ridgeAlongX)
                metal.addBox(Vector3(plot.minX, ridge - 0.06f, (plot.minZ + plot.maxZ) * 0.5f - 0.09f),
                             Vector3(plot.maxX, ridge + 0.04f,
                                     (plot.minZ + plot.maxZ) * 0.5f + 0.09f));
            else
                metal.addBox(Vector3((plot.minX + plot.maxX) * 0.5f - 0.09f, ridge - 0.06f, plot.minZ),
                             Vector3((plot.minX + plot.maxX) * 0.5f + 0.09f, ridge + 0.04f,
                                     plot.maxZ));

            // Dormers on the street slope, on about half the pitched roofs.
            if (rng.chance(0.55f))
            {
                const int dormers = rng.intRange(1, 3);
                for (int i = 0; i < dormers; ++i)
                {
                    const float t = (static_cast<float>(i) + 1.0f) / (static_cast<float>(dormers) + 1.0f);
                    const float dw = 1.15f, dh = 1.30f;
                    const float slopeIn = span * 0.24f;
                    if (ridgeAlongX)
                    {
                        const float x = plot.minX + t * w - dw * 0.5f;
                        const float z = plot.minZ + slopeIn;
                        const float y = eaves + slopeIn * (rise / (span * 0.5f));
                        MeshBuilder& cheek = collector.builder(palette.metal);
                        cheek.setTileSize(1.0f);
                        cheek.addBox(Vector3(x, y - 0.25f, z - 0.55f),
                                     Vector3(x + dw, y + dh, z + 0.35f), BoxFaces::allButBottom());
                        MeshBuilder& glass = collector.builder(palette.glass);
                        glass.setTileSize(1.0f);
                        glass.addQuad(Vector3(x + 0.10f, y - 0.10f, z - 0.56f),
                                      Vector3(x + dw - 0.10f, y - 0.10f, z - 0.56f),
                                      Vector3(x + dw - 0.10f, y + dh - 0.16f, z - 0.56f),
                                      Vector3(x + 0.10f, y + dh - 0.16f, z - 0.56f));
                    }
                }
            }
            break;
        }
        case RoofStyle::Mansard:
        {
            // Steep lower slope, shallow upper, flat deck: the roof that gives a
            // corner block its extra habitable storey.
            // Proportions taken from the building, not from the plot: a mansard
            // adds one habitable storey, so the steep face is about a storey
            // high and the shallow one takes it to the deck. Scaling all four
            // numbers with the block's depth gave 24 m deep plots a seven-metre
            // tent for a roof.
            const float breakUp = std::min(span * 0.28f, 3.1f);
            const float breakIn = std::min(span * 0.20f, breakUp * 0.62f);
            const float deckUp  = breakUp + std::min(span * 0.10f, 1.15f);
            const float deckIn  = std::min(span * 0.40f, breakIn + span * 0.16f);
            const float x0 = plot.minX, x1 = plot.maxX, z0 = plot.minZ, z1 = plot.maxZ;

            // Four steep faces.
            AddRoofQuad(roof, Vector3(x0 - 0.15f, eaves, z0 - 0.15f), Vector3(x1 + 0.15f, eaves, z0 - 0.15f),
                         Vector3(x1 - breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x0 + breakIn, eaves + breakUp, z0 + breakIn));
            AddRoofQuad(roof, Vector3(x1 + 0.15f, eaves, z1 + 0.15f), Vector3(x0 - 0.15f, eaves, z1 + 0.15f),
                         Vector3(x0 + breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x1 - breakIn, eaves + breakUp, z1 - breakIn));
            AddRoofQuad(roof, Vector3(x0 - 0.15f, eaves, z1 + 0.15f), Vector3(x0 - 0.15f, eaves, z0 - 0.15f),
                         Vector3(x0 + breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x0 + breakIn, eaves + breakUp, z1 - breakIn));
            AddRoofQuad(roof, Vector3(x1 + 0.15f, eaves, z0 - 0.15f), Vector3(x1 + 0.15f, eaves, z1 + 0.15f),
                         Vector3(x1 - breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x1 - breakIn, eaves + breakUp, z0 + breakIn));
            // Four shallow faces up to the deck.
            AddRoofQuad(roof, Vector3(x0 + breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x1 - breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z0 + deckIn),
                         Vector3(x0 + deckIn, eaves + deckUp, z0 + deckIn));
            AddRoofQuad(roof, Vector3(x1 - breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x0 + breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x0 + deckIn, eaves + deckUp, z1 - deckIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z1 - deckIn));
            AddRoofQuad(roof, Vector3(x0 + breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x0 + breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x0 + deckIn, eaves + deckUp, z0 + deckIn),
                         Vector3(x0 + deckIn, eaves + deckUp, z1 - deckIn));
            AddRoofQuad(roof, Vector3(x1 - breakIn, eaves + breakUp, z0 + breakIn),
                         Vector3(x1 - breakIn, eaves + breakUp, z1 - breakIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z1 - deckIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z0 + deckIn));
            // The deck.
            MeshBuilder& felt = collector.builder(&materials_.get(MaterialId::RoofFelt));
            felt.setTileSize(2.0f);
            felt.addQuad(Vector3(x0 + deckIn, eaves + deckUp, z0 + deckIn),
                         Vector3(x0 + deckIn, eaves + deckUp, z1 - deckIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z1 - deckIn),
                         Vector3(x1 - deckIn, eaves + deckUp, z0 + deckIn));

            // Mansard windows: a row of them in the steep face on the street side.
            const int windows = std::max(2, static_cast<int>(w / 3.0f));
            for (int i = 0; i < windows; ++i)
            {
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(windows);
                const float x = x0 + t * w;
                MeshBuilder& metal = collector.builder(palette.metal);
                metal.setTileSize(0.8f);
                metal.addBox(Vector3(x - 0.55f, eaves + 0.30f, z0 - 0.05f),
                             Vector3(x + 0.55f, eaves + 1.55f, z0 + 0.55f),
                             BoxFaces::allButBottom());
                MeshBuilder& glass = collector.builder(palette.glass);
                glass.setTileSize(1.0f);
                glass.addQuad(Vector3(x - 0.42f, eaves + 0.42f, z0 - 0.07f),
                              Vector3(x + 0.42f, eaves + 0.42f, z0 - 0.07f),
                              Vector3(x + 0.42f, eaves + 1.42f, z0 - 0.07f),
                              Vector3(x - 0.42f, eaves + 1.42f, z0 - 0.07f));
            }
            break;
        }
        case RoofStyle::Flat:
        case RoofStyle::FlatWithPlant:
        {
            MeshBuilder& deck = collector.builder(&materials_.get(MaterialId::RoofFelt));
            deck.setTileSize(2.0f);
            deck.addQuad(Vector3(plot.minX, eaves, plot.minZ), Vector3(plot.minX, eaves, plot.maxZ),
                         Vector3(plot.maxX, eaves, plot.maxZ), Vector3(plot.maxX, eaves, plot.minZ));
            // The parapet, with a metal coping on top.
            trim.addBox(Vector3(plot.minX - 0.08f, eaves, plot.minZ - 0.08f),
                        Vector3(plot.maxX + 0.08f, eaves + M::kParapetHeight, plot.maxZ + 0.08f),
                        BoxFaces::sides());
            trim.addBox(Vector3(plot.minX + 0.20f, eaves, plot.minZ + 0.20f),
                        Vector3(plot.maxX - 0.20f, eaves + M::kParapetHeight, plot.maxZ - 0.20f),
                        BoxFaces::sides());
            MeshBuilder& coping = collector.builder(palette.metal);
            coping.setTileSize(0.8f);
            coping.addBox(Vector3(plot.minX - 0.11f, eaves + M::kParapetHeight, plot.minZ - 0.11f),
                          Vector3(plot.maxX + 0.11f, eaves + M::kParapetHeight + 0.055f,
                                  plot.maxZ + 0.11f),
                          BoxFaces::allButBottom());

            if (plot.roof == RoofStyle::FlatWithPlant)
            {
                // Lift overrun, plant enclosure and a few condenser units: the
                // things that make a modern roofline look occupied instead of
                // like the top of a box.
                MeshBuilder& plant = collector.builder(palette.trim);
                plant.setTileSize(1.5f);
                const float px = plot.minX + w * rng.range(0.25f, 0.55f);
                const float pz = plot.minZ + d * rng.range(0.30f, 0.60f);
                plant.addBox(Vector3(px, eaves, pz), Vector3(px + 3.4f, eaves + 2.9f, pz + 2.6f),
                             BoxFaces::allButBottom());
                MeshBuilder& units = collector.builder(palette.metal);
                units.setTileSize(0.6f);
                for (int i = 0; i < 3; ++i)
                {
                    const float ux = plot.minX + w * rng.range(0.12f, 0.85f);
                    const float uz = plot.minZ + d * rng.range(0.12f, 0.85f);
                    units.addBox(Vector3(ux, eaves + 0.08f, uz),
                                 Vector3(ux + 1.05f, eaves + 0.85f, uz + 0.80f),
                                 BoxFaces::allButBottom());
                }
            }
            break;
        }
    }

    // Chimneys: a stack for every couple of flats, on the older styles.
    if (plot.roof == RoofStyle::Pitched || plot.roof == RoofStyle::Mansard)
    {
        const int stacks = rng.intRange(1, 3);
        MeshBuilder& brick = collector.builder(&materials_.get(MaterialId::BrickEngineering));
        brick.setTileSize(0.8f);
        MeshBuilder& pots = collector.builder(&materials_.get(MaterialId::RoofTile));
        pots.setTileSize(0.4f);
        for (int i = 0; i < stacks; ++i)
        {
            const float t = (static_cast<float>(i) + 1.0f) / (static_cast<float>(stacks) + 1.0f);
            const float x = ridgeAlongX ? plot.minX + t * w : (plot.minX + plot.maxX) * 0.5f;
            const float z = ridgeAlongX ? (plot.minZ + plot.maxZ) * 0.5f : plot.minZ + t * d;
            // The same numbers the roof itself is built from. They used to be
            // re-derived here, and the mansard's `span * 0.38` bore no relation
            // to the capped deck the roof generator actually builds: on a 26 m
            // block that put the stacks five metres above a roof they were
            // supposed to be sitting on.
            const float rise = plot.roof == RoofStyle::Pitched
                                   ? std::min(span * 0.5f * std::tan(
                                                  MathHelper::ToRadians(M::kRoofPitchDegrees)),
                                              4.6f)
                                   : std::min(span * 0.28f, 3.1f)
                                         + std::min(span * 0.10f, 1.15f);
            // The stack passes through the roof; what shows is the metre or so
            // above the ridge, not a five-metre tower.
            const float top = eaves + rise + rng.range(0.75f, 1.25f);
            brick.addBox(Vector3(x - 0.45f, eaves, z - 0.30f), Vector3(x + 0.45f, top, z + 0.30f),
                         BoxFaces::sides());
            brick.addBox(Vector3(x - 0.52f, top, z - 0.37f), Vector3(x + 0.52f, top + 0.10f,
                                                                     z + 0.37f));
            for (int pot = 0; pot < 2; ++pot)
                pots.addCylinder(Vector3(x - 0.20f + static_cast<float>(pot) * 0.40f, top + 0.10f,
                                         z),
                                 0.115f, 0.10f, 0.34f, 10, false, false);
        }
    }
}

}  // namespace CnaStreet
