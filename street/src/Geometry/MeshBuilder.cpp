// SPDX-License-Identifier: MIT
#include "CnaStreet/Geometry/MeshBuilder.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Microsoft::Xna::Framework;

namespace CnaStreet::Geometry {

namespace {

constexpr float kEpsilon = 1e-6f;

Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback)
{
    const float lengthSquared = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (lengthSquared < kEpsilon * kEpsilon) return fallback;
    const float inverse = 1.0f / std::sqrt(lengthSquared);
    return Vector3(v.X * inverse, v.Y * inverse, v.Z * inverse);
}

/// The axis a planar projection runs along, and the two axes it maps to UV.
void ProjectionAxes(const Vector3& normal, PlanarAxis requested, int& uAxis, int& vAxis,
                    bool& flipU)
{
    int dominant;
    if (requested != PlanarAxis::Auto)
    {
        dominant = requested == PlanarAxis::X ? 0 : (requested == PlanarAxis::Y ? 1 : 2);
    }
    else
    {
        const float ax = std::fabs(normal.X);
        const float ay = std::fabs(normal.Y);
        const float az = std::fabs(normal.Z);
        dominant = (ay >= ax && ay >= az) ? 1 : ((ax >= az) ? 0 : 2);
    }

    // The pairs are chosen so that U runs "right" and V runs "down" when the
    // surface is seen from outside, which is what makes brick courses land the
    // right way up on all four walls of a building instead of on two of them.
    switch (dominant)
    {
        case 0:  uAxis = 2; vAxis = 1; flipU = normal.X < 0.0f; break;   // wall facing X: Z/Y
        case 1:  uAxis = 0; vAxis = 2; flipU = false;           break;   // ground: X/Z
        default: uAxis = 0; vAxis = 1; flipU = normal.Z > 0.0f; break;   // wall facing Z: X/Y
    }
}

float Component(const Vector3& v, int axis)
{
    return axis == 0 ? v.X : (axis == 1 ? v.Y : v.Z);
}

}  // namespace

Microsoft::Xna::Framework::BoundingBox MeshData::bounds() const
{
    if (vertices.empty()) return BoundingBox(Vector3::Zero, Vector3::Zero);
    float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float hi[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest()};
    for (const Vertex& v : vertices)
    {
        const float p[3] = {v.Position.X, v.Position.Y, v.Position.Z};
        for (int i = 0; i < 3; ++i)
        {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    return BoundingBox(Vector3(lo[0], lo[1], lo[2]), Vector3(hi[0], hi[1], hi[2]));
}

void MeshBuilder::reserve(std::size_t vertices, std::size_t indices)
{
    mesh_.vertices.reserve(mesh_.vertices.size() + vertices);
    mesh_.indices.reserve(mesh_.indices.size() + indices);
}

std::uint32_t MeshBuilder::addVertex(const Vertex& vertex)
{
    mesh_.vertices.push_back(vertex);
    return static_cast<std::uint32_t>(mesh_.vertices.size() - 1);
}

void MeshBuilder::addTriangleIndices(std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    // CNA follows XNA: with the default RasterizerState::CullCounterClockwise a
    // face is visible when its vertices are CLOCKWISE seen from the front, i.e.
    // when cross(b-a, c-a) points *away* from the viewer. Every helper in this
    // file takes corners counter-clockwise from the front -- the readable,
    // glTF-shaped convention -- and this one function is where that becomes
    // CNA's winding. Determined empirically against the OPENGL33 renderer, not
    // assumed; see docs/cna-findings.md CNA-F5.
    if (flipWinding_)
    {
        mesh_.indices.push_back(a);
        mesh_.indices.push_back(b);
        mesh_.indices.push_back(c);
    }
    else
    {
        mesh_.indices.push_back(a);
        mesh_.indices.push_back(c);
        mesh_.indices.push_back(b);
    }
}

Vector2 MeshBuilder::planarUv(const Vector3& position, const Vector3& normal) const
{
    int uAxis = 0, vAxis = 1;
    bool flipU = false;
    ProjectionAxes(normal, planarAxis_, uAxis, vAxis, flipU);

    float u = Component(position, uAxis);
    float v = Component(position, vAxis);
    if (flipU) u = -u;
    // V runs downward on walls so brick courses stack from the ground up.
    if (vAxis == 1) v = -v;

    if (uvRotation_ != 0.0f)
    {
        const float c = std::cos(uvRotation_);
        const float s = std::sin(uvRotation_);
        const float ru = u * c - v * s;
        const float rv = u * s + v * c;
        u = ru;
        v = rv;
    }
    return Vector2(u / tileU_ + uvOffset_.X, v / tileV_ + uvOffset_.Y);
}

void MeshBuilder::emitQuad(const Vector3 corners[4], const Vector2 uvs[4], const Vector3& normal,
                           const Vector3& tangent, float handedness)
{
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    const Vector4 tangent4(tangent.X, tangent.Y, tangent.Z, handedness);
    for (int i = 0; i < 4; ++i)
        mesh_.vertices.push_back(Vertex(corners[i], normal, tangent4, uvs[i]));
    addTriangleIndices(base + 0, base + 1, base + 2);
    addTriangleIndices(base + 0, base + 2, base + 3);
}

namespace {

/// Derives the tangent frame of a quad from its corners and texture coordinates.
/// Falls back to an arbitrary but orthonormal frame where the UVs are degenerate
/// (a zero-area triangle in UV space, which happens on untextured detail geometry).
void TangentFrame(const Vector3 corners[4], const Vector2 uvs[4], const Vector3& normal,
                  Vector3& tangent, float& handedness)
{
    const Vector3 e1 = corners[1] - corners[0];
    const Vector3 e2 = corners[2] - corners[0];
    const Vector2 d1 = uvs[1] - uvs[0];
    const Vector2 d2 = uvs[2] - uvs[0];

    const float determinant = d1.X * d2.Y - d2.X * d1.Y;
    if (std::fabs(determinant) > kEpsilon)
    {
        const float inverse = 1.0f / determinant;
        Vector3 t((d2.Y * e1.X - d1.Y * e2.X) * inverse, (d2.Y * e1.Y - d1.Y * e2.Y) * inverse,
                  (d2.Y * e1.Z - d1.Y * e2.Z) * inverse);
        // Gram-Schmidt against the normal, as the shader re-orthogonalises anyway.
        const float projection = Vector3::Dot(t, normal);
        t = t - normal * projection;
        tangent = SafeNormalize(t, Vector3::Right);

        Vector3 bComputed((d1.X * e2.X - d2.X * e1.X) * inverse,
                          (d1.X * e2.Y - d2.X * e1.Y) * inverse,
                          (d1.X * e2.Z - d2.X * e1.Z) * inverse);
        // glTF: Bitangent = cross(Normal, Tangent.xyz) * Tangent.w.
        handedness = Vector3::Dot(Vector3::Cross(normal, tangent), bComputed) < 0.0f ? -1.0f : 1.0f;
        return;
    }

    const Vector3 reference = std::fabs(normal.Y) > 0.99f ? Vector3::Forward : Vector3::Up;
    tangent = SafeNormalize(Vector3::Cross(reference, normal), Vector3::Right);
    handedness = 1.0f;
}

}  // namespace

bool MeshBuilder::addQuadUv(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                            const Vector2& uvA, const Vector2& uvB, const Vector2& uvC,
                            const Vector2& uvD)
{
    const Vector3 corners[4] = {a, b, c, d};
    const Vector2 uvs[4]     = {uvA, uvB, uvC, uvD};
    const Vector3 raw        = Vector3::Cross(b - a, c - a);
    if (raw.X * raw.X + raw.Y * raw.Y + raw.Z * raw.Z < kEpsilon * kEpsilon) return false;
    const Vector3 normal = SafeNormalize(raw, Vector3::Up);

    Vector3 tangent;
    float   handedness = 1.0f;
    TangentFrame(corners, uvs, normal, tangent, handedness);
    emitQuad(corners, uvs, normal, tangent, handedness);
    return true;
}

bool MeshBuilder::addQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
{
    const Vector3 raw = Vector3::Cross(b - a, c - a);
    if (raw.X * raw.X + raw.Y * raw.Y + raw.Z * raw.Z < kEpsilon * kEpsilon) return false;
    const Vector3 normal = SafeNormalize(raw, Vector3::Up);

    if (uvMode_ == UvMode::Explicit)
        return addQuadUnitUv(a, b, c, d);

    return addQuadUv(a, b, c, d, planarUv(a, normal), planarUv(b, normal), planarUv(c, normal),
                     planarUv(d, normal));
}

bool MeshBuilder::addQuadFacing(const Vector3& a, const Vector3& b, const Vector3& c,
                                const Vector3& d, const Vector3& hint)
{
    const Vector3 raw = Vector3::Cross(b - a, c - a);
    if (Vector3::Dot(raw, hint) < 0.0f) return addQuad(a, d, c, b);
    return addQuad(a, b, c, d);
}

bool MeshBuilder::addQuadUnitUv(const Vector3& a, const Vector3& b, const Vector3& c,
                                const Vector3& d)
{
    return addQuadUv(a, b, c, d, Vector2(0.0f, 1.0f), Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f),
                     Vector2(0.0f, 0.0f));
}

bool MeshBuilder::addTriangle(const Vector3& a, const Vector3& b, const Vector3& c)
{
    const Vector3 raw = Vector3::Cross(b - a, c - a);
    if (raw.X * raw.X + raw.Y * raw.Y + raw.Z * raw.Z < kEpsilon * kEpsilon) return false;
    const Vector3 normal = SafeNormalize(raw, Vector3::Up);

    const Vector3 corners[4] = {a, b, c, c};
    const Vector2 uvs[4]     = {planarUv(a, normal), planarUv(b, normal), planarUv(c, normal),
                                planarUv(c, normal)};
    Vector3 tangent;
    float   handedness = 1.0f;
    TangentFrame(corners, uvs, normal, tangent, handedness);

    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    const Vector4 tangent4(tangent.X, tangent.Y, tangent.Z, handedness);
    for (int i = 0; i < 3; ++i)
        mesh_.vertices.push_back(Vertex(corners[i], normal, tangent4, uvs[i]));
    addTriangleIndices(base + 0, base + 1, base + 2);
    return true;
}

void MeshBuilder::addBox(const Vector3& min, const Vector3& max)
{
    addBox(min, max, BoxFaces{});
}

void MeshBuilder::addBox(const Vector3& min, const Vector3& max, const BoxFaces& faces)
{
    const float x0 = min.X, y0 = min.Y, z0 = min.Z;
    const float x1 = max.X, y1 = max.Y, z1 = max.Z;

    // Corners named by sign, e.g. c011 is (-X, +Y, +Z). Every face below lists
    // them counter-clockwise seen from outside the box.
    const Vector3 c000(x0, y0, z0), c100(x1, y0, z0), c110(x1, y1, z0), c010(x0, y1, z0);
    const Vector3 c001(x0, y0, z1), c101(x1, y0, z1), c111(x1, y1, z1), c011(x0, y1, z1);

    if (faces.negZ) addQuad(c100, c000, c010, c110);
    if (faces.posZ) addQuad(c001, c101, c111, c011);
    if (faces.negX) addQuad(c000, c001, c011, c010);
    if (faces.posX) addQuad(c101, c100, c110, c111);
    if (faces.posY) addQuad(c010, c011, c111, c110);
    if (faces.negY) addQuad(c000, c100, c101, c001);
}

void MeshBuilder::addOrientedBox(const Vector3& halfExtents, const Matrix& transform,
                                 const BoxFaces& faces)
{
    MeshBuilder local;
    local.tileU_ = tileU_;
    local.tileV_ = tileV_;
    local.uvOffset_ = uvOffset_;
    local.uvMode_ = uvMode_;
    local.planarAxis_ = planarAxis_;
    local.uvRotation_ = uvRotation_;
    local.addBox(Vector3(-halfExtents.X, -halfExtents.Y, -halfExtents.Z), halfExtents, faces);
    append(local.mesh(), transform);
}

void MeshBuilder::addCylinder(const Vector3& baseCentre, float baseRadius, float topRadius,
                              float height, int segments, bool capBottom, bool capTop)
{
    segments = std::max(3, segments);
    const float step = MathHelper::TwoPi / static_cast<float>(segments);

    // The side wall. Built as quads so the tangent frame comes from the same
    // code path as everything else; a cone tip degenerates to a triangle and
    // addQuad's zero-area guard drops the extra one.
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        const Vector3 d0(std::cos(a0), 0.0f, std::sin(a0));
        const Vector3 d1(std::cos(a1), 0.0f, std::sin(a1));

        const Vector3 b0 = baseCentre + d0 * baseRadius;
        const Vector3 b1 = baseCentre + d1 * baseRadius;
        const Vector3 t0 = baseCentre + d0 * topRadius + Vector3(0.0f, height, 0.0f);
        const Vector3 t1 = baseCentre + d1 * topRadius + Vector3(0.0f, height, 0.0f);

        if (uvMode_ == UvMode::Explicit)
        {
            const float u0 = static_cast<float>(i) / static_cast<float>(segments);
            const float u1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            addQuadUv(b0, b1, t1, t0, Vector2(u0, 1.0f), Vector2(u1, 1.0f), Vector2(u1, 0.0f),
                      Vector2(u0, 0.0f));
        }
        else
        {
            // Circumferential U so the texture wraps around rather than being
            // projected flat, which is what makes a painted pole look painted.
            const float circumference = MathHelper::TwoPi * std::max(baseRadius, topRadius);
            const float u0 = static_cast<float>(i) / static_cast<float>(segments)
                             * circumference / tileU_;
            const float u1 = static_cast<float>(i + 1) / static_cast<float>(segments)
                             * circumference / tileU_;
            const float v0 = -baseCentre.Y / tileV_;
            const float v1 = -(baseCentre.Y + height) / tileV_;
            addQuadUv(b0, b1, t1, t0, Vector2(u0, v0), Vector2(u1, v0), Vector2(u1, v1),
                      Vector2(u0, v1));
        }
    }

    if (capBottom && baseRadius > kEpsilon) addDisc(baseCentre, baseRadius, segments, false);
    if (capTop && topRadius > kEpsilon)
        addDisc(baseCentre + Vector3(0.0f, height, 0.0f), topRadius, segments, true);
}

void MeshBuilder::addCylinderBetween(const Vector3& from, const Vector3& to, float radius,
                                     int segments, bool capEnds)
{
    const Vector3 axis = to - from;
    const float length = std::sqrt(axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z);
    if (length < 1e-5f || radius <= 0.0f) return;

    // Build it along +Y in a local frame and transform, so the tangent frames and
    // the UV wrap come from the same code path as every other cylinder.
    const Vector3 up = Vector3(axis.X / length, axis.Y / length, axis.Z / length);
    const Vector3 reference = std::fabs(up.Y) > 0.99f ? Vector3::Forward : Vector3::Up;
    const Vector3 right = SafeNormalize(Vector3::Cross(reference, up), Vector3::Right);
    const Vector3 forward = Vector3::Cross(up, right);

    MeshBuilder local;
    local.tileU_ = tileU_;
    local.tileV_ = tileV_;
    local.uvMode_ = uvMode_;
    local.addCylinder(Vector3::Zero, radius, radius, length, segments, capEnds, capEnds);

    Matrix frame = Matrix::getIdentityProperty();
    frame.M11 = right.X;   frame.M12 = right.Y;   frame.M13 = right.Z;
    frame.M21 = up.X;      frame.M22 = up.Y;      frame.M23 = up.Z;
    frame.M31 = forward.X; frame.M32 = forward.Y; frame.M33 = forward.Z;
    frame.M41 = from.X;    frame.M42 = from.Y;    frame.M43 = from.Z;
    append(local.mesh(), frame);
}

void MeshBuilder::addDisc(const Vector3& centre, float radius, int segments, bool faceUp)
{
    segments = std::max(3, segments);
    const float   step   = MathHelper::TwoPi / static_cast<float>(segments);
    const Vector3 normal = faceUp ? Vector3::Up : Vector3::Down;
    const Vector3 tangent = Vector3::Right;

    const std::uint32_t centreIndex = static_cast<std::uint32_t>(mesh_.vertices.size());
    const Vector4 tangent4(tangent.X, tangent.Y, tangent.Z, 1.0f);
    mesh_.vertices.push_back(Vertex(centre, normal, tangent4, planarUv(centre, normal)));

    for (int i = 0; i <= segments; ++i)
    {
        const float   a = step * static_cast<float>(i);
        const Vector3 p = centre + Vector3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
        mesh_.vertices.push_back(Vertex(p, normal, tangent4, planarUv(p, normal)));
    }
    for (int i = 0; i < segments; ++i)
    {
        const std::uint32_t a = centreIndex + 1 + static_cast<std::uint32_t>(i);
        const std::uint32_t b = centreIndex + 2 + static_cast<std::uint32_t>(i);
        if (faceUp) addTriangleIndices(centreIndex, a, b);
        else        addTriangleIndices(centreIndex, b, a);
    }
}

void MeshBuilder::addSphere(const Vector3& centre, float radius, int slices, int stacks)
{
    addEllipsoid(centre, Vector3(radius, radius, radius), slices, stacks);
}

void MeshBuilder::addEllipsoid(const Vector3& centre, const Vector3& radii, int slices, int stacks)
{
    slices = std::max(3, slices);
    stacks = std::max(2, stacks);

    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (int y = 0; y <= stacks; ++y)
    {
        const float v     = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi   = v * MathHelper::Pi;
        const float sinP  = std::sin(phi);
        const float cosP  = std::cos(phi);
        for (int x = 0; x <= slices; ++x)
        {
            const float u    = static_cast<float>(x) / static_cast<float>(slices);
            const float th   = u * MathHelper::TwoPi;
            const Vector3 unit(sinP * std::cos(th), cosP, sinP * std::sin(th));
            const Vector3 position(centre.X + unit.X * radii.X, centre.Y + unit.Y * radii.Y,
                                   centre.Z + unit.Z * radii.Z);
            // For an ellipsoid the surface normal is the unit sphere normal
            // divided by the radii, not the position direction.
            const Vector3 normal = SafeNormalize(
                Vector3(unit.X / (radii.X * radii.X), unit.Y / (radii.Y * radii.Y),
                        unit.Z / (radii.Z * radii.Z)),
                Vector3::Up);
            const Vector3 tangent = SafeNormalize(Vector3(-std::sin(th), 0.0f, std::cos(th)),
                                                  Vector3::Right);
            mesh_.vertices.push_back(Vertex(position, normal,
                                            Vector4(tangent.X, tangent.Y, tangent.Z, 1.0f),
                                            Vector2(u, v)));
        }
    }
    const std::uint32_t rowStride = static_cast<std::uint32_t>(slices) + 1;
    for (int y = 0; y < stacks; ++y)
        for (int x = 0; x < slices; ++x)
        {
            const std::uint32_t i0 = base + static_cast<std::uint32_t>(y) * rowStride
                                     + static_cast<std::uint32_t>(x);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + rowStride;
            const std::uint32_t i3 = i2 + 1;
            addTriangleIndices(i0, i2, i1);
            addTriangleIndices(i1, i2, i3);
        }
}

void MeshBuilder::addTubeArc(const Matrix& frame, float arcRadius, float tubeRadius,
                             float startAngle, float sweepAngle, int arcSegments, int tubeSegments)
{
    arcSegments  = std::max(2, arcSegments);
    tubeSegments = std::max(3, tubeSegments);

    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (int i = 0; i <= arcSegments; ++i)
    {
        const float t     = static_cast<float>(i) / static_cast<float>(arcSegments);
        const float angle = startAngle + sweepAngle * t;
        const Vector3 centre(std::cos(angle) * arcRadius, std::sin(angle) * arcRadius, 0.0f);
        const Vector3 radial(std::cos(angle), std::sin(angle), 0.0f);
        const Vector3 forward(0.0f, 0.0f, 1.0f);

        for (int j = 0; j <= tubeSegments; ++j)
        {
            const float s     = static_cast<float>(j) / static_cast<float>(tubeSegments);
            const float phi   = s * MathHelper::TwoPi;
            const Vector3 offset = radial * (std::cos(phi) * tubeRadius)
                                   + forward * (std::sin(phi) * tubeRadius);
            const Vector3 localPosition = centre + offset;
            const Vector3 localNormal   = SafeNormalize(offset, radial);
            const Vector3 tangentLocal(-std::sin(angle), std::cos(angle), 0.0f);

            mesh_.vertices.push_back(Vertex(
                Vector3::Transform(localPosition, frame),
                SafeNormalize(Vector3::TransformNormal(localNormal, frame), Vector3::Up),
                Vector4(0.0f, 0.0f, 0.0f, 1.0f),
                Vector2(t * arcRadius * std::fabs(sweepAngle) / tileU_,
                        s * MathHelper::TwoPi * tubeRadius / tileV_)));
            const Vector3 worldTangent =
                SafeNormalize(Vector3::TransformNormal(tangentLocal, frame), Vector3::Right);
            Vertex& written = mesh_.vertices.back();
            written.Tangent = Vector4(worldTangent.X, worldTangent.Y, worldTangent.Z, 1.0f);
        }
    }
    const std::uint32_t rowStride = static_cast<std::uint32_t>(tubeSegments) + 1;
    for (int i = 0; i < arcSegments; ++i)
        for (int j = 0; j < tubeSegments; ++j)
        {
            const std::uint32_t i0 = base + static_cast<std::uint32_t>(i) * rowStride
                                     + static_cast<std::uint32_t>(j);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + rowStride;
            const std::uint32_t i3 = i2 + 1;
            addTriangleIndices(i0, i1, i2);
            addTriangleIndices(i1, i3, i2);
        }
}

void MeshBuilder::addExtrusion(const std::vector<Vector2>& profile, const Matrix& frame,
                               float length, bool capStart, bool capEnd)
{
    if (profile.size() < 3) return;

    const std::size_t count = profile.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vector2& p0 = profile[i];
        const Vector2& p1 = profile[(i + 1) % count];

        const Vector3 a = Vector3::Transform(Vector3(p0.X, p0.Y, 0.0f), frame);
        const Vector3 b = Vector3::Transform(Vector3(p1.X, p1.Y, 0.0f), frame);
        const Vector3 c = Vector3::Transform(Vector3(p1.X, p1.Y, length), frame);
        const Vector3 d = Vector3::Transform(Vector3(p0.X, p0.Y, length), frame);
        addQuad(a, b, c, d);
    }

    if (capStart || capEnd)
    {
        std::vector<Vector3> start;
        std::vector<Vector3> end;
        start.reserve(count);
        end.reserve(count);
        for (const Vector2& p : profile)
        {
            start.push_back(Vector3::Transform(Vector3(p.X, p.Y, 0.0f), frame));
            end.push_back(Vector3::Transform(Vector3(p.X, p.Y, length), frame));
        }
        const Vector3 axis = SafeNormalize(Vector3::TransformNormal(Vector3(0, 0, 1), frame),
                                           Vector3::Forward);
        if (capStart)
        {
            std::reverse(start.begin(), start.end());
            addPolygon(start, -axis);
        }
        if (capEnd) addPolygon(end, axis);
    }
}

void MeshBuilder::addPolygon(const std::vector<Vector3>& corners, const Vector3& normal)
{
    if (corners.size() < 3) return;

    Vector3 centroid = Vector3::Zero;
    for (const Vector3& c : corners) centroid = centroid + c;
    centroid = centroid * (1.0f / static_cast<float>(corners.size()));

    const Vector3 reference = std::fabs(normal.Y) > 0.99f ? Vector3::Forward : Vector3::Up;
    const Vector3 tangent = SafeNormalize(Vector3::Cross(reference, normal), Vector3::Right);
    const Vector4 tangent4(tangent.X, tangent.Y, tangent.Z, 1.0f);

    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.push_back(Vertex(centroid, normal, tangent4, planarUv(centroid, normal)));
    for (const Vector3& c : corners)
        mesh_.vertices.push_back(Vertex(c, normal, tangent4, planarUv(c, normal)));

    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const std::uint32_t a = base + 1 + static_cast<std::uint32_t>(i);
        const std::uint32_t b = base + 1 + static_cast<std::uint32_t>((i + 1) % corners.size());
        addTriangleIndices(base, a, b);
    }
}

void MeshBuilder::append(const MeshData& other)
{
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.insert(mesh_.vertices.end(), other.vertices.begin(), other.vertices.end());
    mesh_.indices.reserve(mesh_.indices.size() + other.indices.size());
    for (std::uint32_t index : other.indices) mesh_.indices.push_back(base + index);
}

void MeshBuilder::append(const MeshData& other, const Matrix& transform)
{
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.reserve(mesh_.vertices.size() + other.vertices.size());

    // A mirroring transform inverts the bitangent handedness *and* the triangle
    // winding. Getting only one of the two right is how a mirrored building wing
    // ends up lit from inside.
    const float determinant =
        transform.M11 * (transform.M22 * transform.M33 - transform.M23 * transform.M32)
        - transform.M12 * (transform.M21 * transform.M33 - transform.M23 * transform.M31)
        + transform.M13 * (transform.M21 * transform.M32 - transform.M22 * transform.M31);
    const bool mirrored = determinant < 0.0f;

    for (const Vertex& v : other.vertices)
    {
        Vertex out;
        out.Position = Vector3::Transform(v.Position, transform);
        out.Normal   = SafeNormalize(Vector3::TransformNormal(v.Normal, transform), Vector3::Up);
        const Vector3 t = SafeNormalize(
            Vector3::TransformNormal(Vector3(v.Tangent.X, v.Tangent.Y, v.Tangent.Z), transform),
            Vector3::Right);
        out.Tangent = Vector4(t.X, t.Y, t.Z, mirrored ? -v.Tangent.W : v.Tangent.W);
        out.TextureCoordinate = v.TextureCoordinate;
        mesh_.vertices.push_back(out);
    }

    mesh_.indices.reserve(mesh_.indices.size() + other.indices.size());
    for (std::size_t i = 0; i + 2 < other.indices.size(); i += 3)
    {
        const std::uint32_t a = base + other.indices[i];
        const std::uint32_t b = base + other.indices[i + 1];
        const std::uint32_t c = base + other.indices[i + 2];
        if (mirrored)
        {
            mesh_.indices.push_back(a);
            mesh_.indices.push_back(c);
            mesh_.indices.push_back(b);
        }
        else
        {
            mesh_.indices.push_back(a);
            mesh_.indices.push_back(b);
            mesh_.indices.push_back(c);
        }
    }
}

void MeshBuilder::offsetUv(std::size_t firstVertex, const Vector2& scale, const Vector2& offset)
{
    for (std::size_t i = firstVertex; i < mesh_.vertices.size(); ++i)
    {
        Vertex& v = mesh_.vertices[i];
        v.TextureCoordinate = Vector2(v.TextureCoordinate.X * scale.X + offset.X,
                                      v.TextureCoordinate.Y * scale.Y + offset.Y);
    }
}

}  // namespace CnaStreet::Geometry
