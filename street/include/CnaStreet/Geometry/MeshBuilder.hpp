// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <vector>

namespace CnaStreet::Geometry {

/**
 * @brief How texture coordinates are produced for a surface.
 *
 * `Planar` is the default for architecture and paving, and it is the single
 * reason nothing in this city has stretched texture on it: the UV is the world
 * position projected onto the face's dominant axis and divided by the material's
 * physical tile size in metres. A brick is 25 cm wide on a 4 m wall and on a 40 m
 * wall, because in both cases the UV came from metres rather than from the
 * surface's own parameterisation.
 *
 * `Explicit` is for surfaces whose image is authored to fit exactly — a sign
 * face, a shop fascia, a number plate.
 */
enum class UvMode
{
    Planar,
    Explicit
};

/// Which world axis a planar projection runs along, chosen from the face normal.
enum class PlanarAxis
{
    Auto,   ///< dominant component of the normal
    X, Y, Z
};

/// Which faces of a box to build. The workhorse for architecture: the faces
/// nobody can ever see are simply not emitted, which on a street of terraced
/// buildings removes roughly a third of the façade triangles for free.
struct BoxFaces
{
    bool negX = true, posX = true;
    bool negY = true, posY = true;
    bool negZ = true, posZ = true;

    /// Four walls, no lid and no floor.
    [[nodiscard]] static BoxFaces sides()
    {
        BoxFaces faces;
        faces.negY = faces.posY = false;
        return faces;
    }
    /// Everything a box sitting on the ground needs.
    [[nodiscard]] static BoxFaces allButBottom()
    {
        BoxFaces faces;
        faces.negY = false;
        return faces;
    }
    [[nodiscard]] static BoxFaces only(bool nx, bool px, bool ny, bool py, bool nz, bool pz)
    {
        BoxFaces faces;
        faces.negX = nx; faces.posX = px;
        faces.negY = ny; faces.posY = py;
        faces.negZ = nz; faces.posZ = pz;
        return faces;
    }
};

/**
 * @brief Accumulates triangles into a @ref MeshData.
 *
 * Deliberately not a scene graph and not a mesh class: it is a buffer with
 * geometry helpers on it. Everything in the city — road surfaces, kerbs, façades,
 * signal housings, tree crowns — is one of these, and the interesting code is
 * the *shape*, not the plumbing.
 *
 * Winding is counter-clockwise seen from the front face, matching CNA's default
 * `RasterizerState::CullCounterClockwise`... which culls counter-clockwise
 * *back* faces, i.e. front faces are clockwise in XNA's left-handed convention.
 * The helpers here all emit consistent winding; @ref setFlipWinding exists for
 * the handful of places that build a shape inside out on purpose.
 */
class MeshBuilder
{
public:
    MeshBuilder() = default;

    // --- state ------------------------------------------------------------
    /// Physical size in metres that one texture tile covers. Planar UVs divide
    /// by this, so 2.0 means the image repeats every two metres.
    void setTileSize(float metres) { tileU_ = tileV_ = metres; }
    void setTileSize(float metresU, float metresV) { tileU_ = metresU; tileV_ = metresV; }
    void setUvOffset(const Microsoft::Xna::Framework::Vector2& offset) { uvOffset_ = offset; }
    void setUvMode(UvMode mode) { uvMode_ = mode; }
    void setPlanarAxis(PlanarAxis axis) { planarAxis_ = axis; }
    /// Rotates planar UVs about the projection axis, in radians. Used to break
    /// up obviously aligned tiling on adjacent surfaces.
    void setUvRotation(float radians) { uvRotation_ = radians; }
    void setFlipWinding(bool flip) { flipWinding_ = flip; }

    // --- raw ---------------------------------------------------------------
    std::uint32_t addVertex(const Vertex& vertex);
    void addTriangleIndices(std::uint32_t a, std::uint32_t b, std::uint32_t c);

    /**
     * @brief A planar quad, corners in counter-clockwise order seen from the front.
     *
     * The normal and the tangent frame are derived from the corners, so a
     * degenerate quad produces a degenerate frame rather than silently wrong
     * lighting: @ref addQuad returns false and adds nothing in that case.
     */
    bool addQuad(const Microsoft::Xna::Framework::Vector3& a,
                 const Microsoft::Xna::Framework::Vector3& b,
                 const Microsoft::Xna::Framework::Vector3& c,
                 const Microsoft::Xna::Framework::Vector3& d);

    /**
     * @brief A quad wound so that its normal points the same way as @p hint.
     *
     * The corner order of a quad decides which way it faces, and getting it
     * wrong produces geometry that is *visible but lit from behind* -- a roof
     * slope that stays grey in full sun, a lane marking that renders black on
     * black asphalt. Both happened here. Where the intended facing is known and
     * the corner order is derived from something else (a direction of travel, a
     * side of the street), state the facing and let this sort the order out.
     */
    bool addQuadFacing(const Microsoft::Xna::Framework::Vector3& a,
                       const Microsoft::Xna::Framework::Vector3& b,
                       const Microsoft::Xna::Framework::Vector3& c,
                       const Microsoft::Xna::Framework::Vector3& d,
                       const Microsoft::Xna::Framework::Vector3& hint);

    /// @ref addQuadFacing with unit UVs, for a surface that carries an image
    /// rather than a tiled material -- an interior cell, a sign face.
    bool addQuadFacingUv(const Microsoft::Xna::Framework::Vector3& a,
                         const Microsoft::Xna::Framework::Vector3& b,
                         const Microsoft::Xna::Framework::Vector3& c,
                         const Microsoft::Xna::Framework::Vector3& d,
                         const Microsoft::Xna::Framework::Vector3& hint);

    /// Explicit-UV quad. UVs are given per corner in the same order.
    bool addQuadUv(const Microsoft::Xna::Framework::Vector3& a,
                   const Microsoft::Xna::Framework::Vector3& b,
                   const Microsoft::Xna::Framework::Vector3& c,
                   const Microsoft::Xna::Framework::Vector3& d,
                   const Microsoft::Xna::Framework::Vector2& uvA,
                   const Microsoft::Xna::Framework::Vector2& uvB,
                   const Microsoft::Xna::Framework::Vector2& uvC,
                   const Microsoft::Xna::Framework::Vector2& uvD);

    /// Convenience: a quad whose UVs run 0..1 across it in the given order.
    bool addQuadUnitUv(const Microsoft::Xna::Framework::Vector3& a,
                       const Microsoft::Xna::Framework::Vector3& b,
                       const Microsoft::Xna::Framework::Vector3& c,
                       const Microsoft::Xna::Framework::Vector3& d);

    bool addTriangle(const Microsoft::Xna::Framework::Vector3& a,
                     const Microsoft::Xna::Framework::Vector3& b,
                     const Microsoft::Xna::Framework::Vector3& c);

    // --- primitives ---------------------------------------------------------
    /// Axis-aligned box from its minimum and maximum corner.
    void addBox(const Microsoft::Xna::Framework::Vector3& min,
                const Microsoft::Xna::Framework::Vector3& max);

    void addBox(const Microsoft::Xna::Framework::Vector3& min,
                const Microsoft::Xna::Framework::Vector3& max, const BoxFaces& faces);

    /// A box under an arbitrary transform, for anything not axis-aligned.
    void addOrientedBox(const Microsoft::Xna::Framework::Vector3& halfExtents,
                        const Microsoft::Xna::Framework::Matrix& transform,
                        const BoxFaces& faces = BoxFaces{});

    /// Cylinder along +Y from @p baseCentre, optionally tapered and capped.
    void addCylinder(const Microsoft::Xna::Framework::Vector3& baseCentre, float baseRadius,
                     float topRadius, float height, int segments,
                     bool capBottom = true, bool capTop = true);

    /// Cylinder between two arbitrary points. `addCylinder` only ever runs along
    /// world +Y, which is right for a lamp column and useless for a mast arm, a
    /// handrail or a bicycle stand.
    void addCylinderBetween(const Microsoft::Xna::Framework::Vector3& from,
                            const Microsoft::Xna::Framework::Vector3& to, float radius,
                            int segments, bool capEnds = true);

    /// A flat disc facing +Y (or -Y when @p faceUp is false).
    void addDisc(const Microsoft::Xna::Framework::Vector3& centre, float radius, int segments,
                 bool faceUp = true);

    /// UV sphere. Used for tree crowns and lamp globes, not for detail.
    void addSphere(const Microsoft::Xna::Framework::Vector3& centre, float radius,
                   int slices, int stacks);

    /// Ellipsoid — a sphere that can be squashed, which is what a tree crown is.
    void addEllipsoid(const Microsoft::Xna::Framework::Vector3& centre,
                      const Microsoft::Xna::Framework::Vector3& radii, int slices, int stacks);

    /// A torus arc in the XY plane, swept about +Z. Bicycle stands, door handles.
    void addTubeArc(const Microsoft::Xna::Framework::Matrix& frame, float arcRadius,
                    float tubeRadius, float startAngle, float sweepAngle,
                    int arcSegments, int tubeSegments);

    /**
     * @brief Extrudes a closed 2-D profile along +Y and caps both ends.
     *
     * The profile is given in the XZ plane, counter-clockwise. This is how the
     * kerb stones, the cornices and the window frames are made: draw the section
     * once, run it along the wall.
     */
    void addExtrusion(const std::vector<Microsoft::Xna::Framework::Vector2>& profile,
                      const Microsoft::Xna::Framework::Matrix& frame, float length,
                      bool capStart = true, bool capEnd = true);

    /// A closed polygon triangulated as a fan about its centroid, facing @p normal.
    void addPolygon(const std::vector<Microsoft::Xna::Framework::Vector3>& corners,
                    const Microsoft::Xna::Framework::Vector3& normal);

    // --- composition --------------------------------------------------------
    /// Appends another mesh, transforming positions, normals and tangents.
    /// Handedness is corrected for a mirroring transform.
    void append(const MeshData& other, const Microsoft::Xna::Framework::Matrix& transform);
    void append(const MeshData& other);

    /// Rewrites every texture coordinate through a scale and offset. Used to give
    /// each instance of a repeated façade a different patch of the same atlas.
    void offsetUv(std::size_t firstVertex, const Microsoft::Xna::Framework::Vector2& scale,
                  const Microsoft::Xna::Framework::Vector2& offset);

    [[nodiscard]] const MeshData& mesh() const { return mesh_; }
    [[nodiscard]] MeshData& mesh() { return mesh_; }
    [[nodiscard]] MeshData take() { return std::move(mesh_); }
    [[nodiscard]] std::size_t vertexCount() const { return mesh_.vertices.size(); }
    [[nodiscard]] std::size_t triangleCount() const { return mesh_.triangleCount(); }
    void reserve(std::size_t vertices, std::size_t indices);
    void clear() { mesh_.clear(); }

private:
    [[nodiscard]] Microsoft::Xna::Framework::Vector2 planarUv(
        const Microsoft::Xna::Framework::Vector3& position,
        const Microsoft::Xna::Framework::Vector3& normal) const;

    void emitQuad(const Microsoft::Xna::Framework::Vector3 corners[4],
                  const Microsoft::Xna::Framework::Vector2 uvs[4],
                  const Microsoft::Xna::Framework::Vector3& normal,
                  const Microsoft::Xna::Framework::Vector3& tangent, float handedness);

    MeshData mesh_;
    float    tileU_ = 1.0f;
    float    tileV_ = 1.0f;
    float    uvRotation_ = 0.0f;
    Microsoft::Xna::Framework::Vector2 uvOffset_{0.0f, 0.0f};
    UvMode     uvMode_ = UvMode::Planar;
    PlanarAxis planarAxis_ = PlanarAxis::Auto;
    bool       flipWinding_ = false;
};

}  // namespace CnaStreet::Geometry
