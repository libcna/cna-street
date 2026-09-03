// SPDX-License-Identifier: MIT
#include "CnaStreet/Props/PedestrianFactory.hpp"

#include "CnaStreet/Geometry/Transform.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework;
using CnaStreet::Geometry::MeshBuilder;

namespace CnaStreet {

namespace {

/// A tapered limb between two joints, with a rounded end so knees and elbows do
/// not show a hard cylinder edge.
void Limb(MeshBuilder& builder, const Vector3& from, const Vector3& to, float radiusFrom,
          float radiusTo)
{
    builder.addCylinderBetween(from, to, (radiusFrom + radiusTo) * 0.5f, 8, false);
    builder.addEllipsoid(from, Vector3(radiusFrom, radiusFrom, radiusFrom), 8, 5);
    builder.addEllipsoid(to, Vector3(radiusTo, radiusTo, radiusTo), 8, 5);
}

/// Rotates a point about the X axis around a pivot — the only rotation a
/// sagittal-plane walk cycle needs.
Vector3 Swing(const Vector3& pivot, float length, float angle)
{
    return Vector3(pivot.X, pivot.Y - length * std::cos(angle), pivot.Z + length * std::sin(angle));
}

}  // namespace

PedestrianFactory::PedestrianFactory(const MaterialLibrary& materials) : materials_(materials) {}

void PedestrianFactory::build(GeometryCollector& collector, float height, int phase, bool standing,
                              const Material* skin, const Material* clothing,
                              const Material* trousers, Rng& rng) const
{
    const Material* skinMaterial = skin != nullptr ? skin : &materials_.get(MaterialId::Skin);
    const Material* topMaterial = clothing != nullptr ? clothing
                                                     : &materials_.get(MaterialId::Clothing);
    const Material* legMaterial = trousers != nullptr ? trousers : topMaterial;

    MeshBuilder& flesh = collector.builder(skinMaterial);
    flesh.setTileSize(0.4f);
    MeshBuilder& top = collector.builder(topMaterial);
    top.setTileSize(0.5f);
    MeshBuilder& legs = collector.builder(legMaterial);
    legs.setTileSize(0.5f);
    MeshBuilder& shoes = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
    shoes.setTileSize(0.2f);

    // Canonical proportions, scaled from the figure's height.
    const float headRadius = height * 0.0645f;
    const float eyeLine    = height * 0.935f;
    const float shoulderY  = height * 0.815f;
    const float shoulderHalf = height * 0.115f;
    const float hipY       = height * 0.530f;
    const float hipHalf    = height * 0.062f;
    const float kneeLength = height * 0.245f;
    const float shinLength = height * 0.250f;
    const float upperArm   = height * 0.175f;
    const float foreArm    = height * 0.160f;

    // --- the walk cycle ----------------------------------------------------
    // One sine, two limbs, opposite phase. The arms swing at a third of the leg
    // amplitude and opposite the leg on the same side, which is what makes a
    // walk read as a walk rather than as a march.
    const float t = standing ? 0.0f
                             : static_cast<float>(phase) / static_cast<float>(kPhaseCount)
                                   * MathHelper::TwoPi;
    const float legSwing = standing ? 0.0f : std::sin(t) * 0.42f;
    const float armSwing = standing ? 0.06f : -std::sin(t) * 0.30f;
    // The body rises and falls twice per stride, at the midpoint of each step.
    const float bob = standing ? 0.0f : std::fabs(std::cos(t)) * height * 0.012f;

    // --- torso -------------------------------------------------------------
    const Vector3 hip(0.0f, hipY + bob, 0.0f);
    const Vector3 neck(0.0f, shoulderY + bob + height * 0.028f, 0.0f);
    // A tapered trunk: shoulders wider than the waist, and slightly deeper than
    // it is wide, because a person seen from the side is not a cylinder.
    top.addCylinderBetween(hip, Vector3(0.0f, shoulderY + bob, 0.0f), height * 0.098f, 10, true);
    top.addEllipsoid(Vector3(0.0f, shoulderY + bob, 0.0f),
                     Vector3(shoulderHalf, height * 0.045f, height * 0.062f), 12, 7);
    top.addEllipsoid(Vector3(0.0f, hipY + bob + height * 0.02f, 0.0f),
                     Vector3(hipHalf * 1.25f, height * 0.045f, height * 0.055f), 12, 7);

    // --- head and neck ------------------------------------------------------
    flesh.addCylinderBetween(Vector3(0.0f, shoulderY + bob, 0.0f), neck, height * 0.026f, 8, false);
    flesh.addEllipsoid(Vector3(0.0f, eyeLine + bob - height * 0.02f, 0.0f),
                       Vector3(headRadius * 0.85f, headRadius * 1.10f, headRadius * 0.95f), 12, 8);
    // Hair as a slightly larger cap over the back of the skull.
    MeshBuilder& hair = collector.builder(&materials_.get(MaterialId::PaintedSteelBlack));
    hair.setTileSize(0.2f);
    hair.addEllipsoid(Vector3(0.0f, eyeLine + bob - height * 0.006f, -headRadius * 0.13f),
                      Vector3(headRadius * 0.90f, headRadius * 1.02f, headRadius * 0.96f), 12, 8);

    // --- legs ---------------------------------------------------------------
    for (const float side : {-1.0f, 1.0f})
    {
        const float swing = side > 0.0f ? legSwing : -legSwing;
        const Vector3 hipJoint(side * hipHalf, hipY + bob, 0.0f);
        const Vector3 knee = Swing(hipJoint, kneeLength, swing);
        // The trailing leg bends at the knee; the leading one is nearly straight.
        const float bend = standing ? 0.0f : std::max(0.0f, -swing) * 0.9f;
        const Vector3 ankle = Swing(knee, shinLength, swing - bend);
        Limb(legs, hipJoint, knee, height * 0.049f, height * 0.036f);
        Limb(legs, knee, ankle, height * 0.036f, height * 0.026f);
        shoes.addBox(Vector3(ankle.X - height * 0.030f, 0.0f, ankle.Z - height * 0.035f),
                     Vector3(ankle.X + height * 0.030f, height * 0.036f,
                             ankle.Z + height * 0.100f));
    }

    // --- arms ----------------------------------------------------------------
    for (const float side : {-1.0f, 1.0f})
    {
        const float swing = side > 0.0f ? -armSwing : armSwing;
        const Vector3 shoulder(side * shoulderHalf, shoulderY + bob - height * 0.012f, 0.0f);
        const Vector3 elbow = Swing(shoulder, upperArm, swing);
        const Vector3 wrist = Swing(elbow, foreArm, swing * 0.45f + 0.10f);
        Limb(top, shoulder, elbow, height * 0.035f, height * 0.027f);
        Limb(flesh, elbow, wrist, height * 0.026f, height * 0.021f);
        flesh.addEllipsoid(wrist, Vector3(height * 0.022f, height * 0.028f, height * 0.016f), 8, 5);
    }
    (void)rng;
}

}  // namespace CnaStreet
