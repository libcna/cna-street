// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The walk and idle clips, checked for the things a still cannot.
 *
 * A gait has a handful of properties a viewer reads without naming them, and
 * every one of them has a sign that can be wrong: a knee bends one way, an
 * arm swings against the leg on its own side, the two legs are half a cycle
 * apart, and a stride is a stride and not a shuffle. The fourth pass's walk
 * bent every knee forward and nobody saw it in a crowd; a row of eight frozen
 * mid-stride made it obvious, and this suite keeps it obvious.
 */
#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Props/CharacterFactory.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace CnaStreet;
using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Graphics::AnimationClip;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;

namespace {

/// The pitch of a rotation about the figure's X, which is what every limb
/// track here is: positive swings a bone's far end backward.
float PitchOf(const Quaternion& q)
{
    return 2.0f * std::atan2(q.X, q.W);
}

const BoneTrackEXT* TrackFor(const AnimationClip& clip, const Geometry::Skeleton& skeleton,
                             const std::string& bone)
{
    const int index = skeleton.find(bone);
    for (const BoneTrackEXT& track : clip.Tracks)
        if (track.BoneIndex == index && index >= 0) return &track;
    return nullptr;
}

float MaxPitch(const BoneTrackEXT& track)
{
    float best = -1e9f;
    for (const auto& key : track.Keys) best = std::max(best, PitchOf(key.Rotation));
    return best;
}

float MinPitch(const BoneTrackEXT& track)
{
    float best = 1e9f;
    for (const auto& key : track.Keys) best = std::min(best, PitchOf(key.Rotation));
    return best;
}

/// Where every bone is at time @p t of @p clip, exactly the way
/// `AnimationPlayer` computes it: a keyframe's local transform is
/// rotation then translation, and a bone's world transform is its local
/// times its parent's.
std::vector<Vector3> BonesAt(const AnimationClip& clip, const Geometry::Skeleton& skeleton, float t)
{
    const int count = skeleton.count();
    std::vector<Matrix> local = skeleton.bindPose();
    for (const BoneTrackEXT& track : clip.Tracks)
    {
        if (track.BoneIndex < 0 || track.BoneIndex >= count || track.Keys.empty()) continue;
        const double duration = static_cast<double>(clip.Duration.getTicksProperty());
        const double want = duration * static_cast<double>(t);
        std::size_t i = 0;
        while (i + 2 < track.Keys.size()
               && static_cast<double>(track.Keys[i + 1].Time.getTicksProperty()) < want)
            ++i;
        const auto& a = track.Keys[i];
        const auto& b = track.Keys[std::min(i + 1, track.Keys.size() - 1)];
        const double span = static_cast<double>(b.Time.getTicksProperty() - a.Time.getTicksProperty());
        const float u = span > 0.0 ? static_cast<float>(
                            std::clamp((want - static_cast<double>(a.Time.getTicksProperty())) / span,
                                       0.0, 1.0))
                                   : 0.0f;
        local[static_cast<std::size_t>(track.BoneIndex)] =
            Matrix::CreateFromQuaternion(Quaternion::Slerp(a.Rotation, b.Rotation, u))
            * Matrix::CreateTranslation(Vector3::Lerp(a.Translation, b.Translation, u));
    }
    std::vector<Matrix> world(static_cast<std::size_t>(count));
    std::vector<Vector3> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const int parent = skeleton[i].parent;
        world[static_cast<std::size_t>(i)] =
            parent < 0 ? local[static_cast<std::size_t>(i)]
                       : local[static_cast<std::size_t>(i)] * world[static_cast<std::size_t>(parent)];
        out[static_cast<std::size_t>(i)] =
            world[static_cast<std::size_t>(i)].getTranslationProperty();
    }
    return out;
}

/// A rig shaped like the imported MakeHuman people: nineteen bones in the
/// order the project builds them, with the legs *diverging* the way
/// MakeHuman's A-posed base mesh leaves them -- hips 22 cm apart, ankles 42,
/// which is person-05 as scripts/blender-people.py wrote it.
Geometry::Skeleton SplayedRig()
{
    Geometry::Skeleton s;
    const int pelvis = s.add("pelvis", -1, Vector3(0.0f, 0.984f, 0.0f), 0.10f);
    const int spine  = s.add("spine", pelvis, Vector3(0.0f, 1.10f, 0.0f), 0.13f);
    const int chest  = s.add("chest", spine, Vector3(0.0f, 1.32f, 0.0f), 0.145f);
    const int neck   = s.add("neck", chest, Vector3(0.0f, 1.57f, 0.0f), 0.05f);
    s.add("head", neck, Vector3(0.0f, 1.70f, 0.0f), 0.11f);
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side == 0 ? 1.0f : -1.0f;
        const std::string suffix = side == 0 ? ".R" : ".L";
        const int clavicle = s.add("clavicle" + suffix, chest, Vector3(sign * 0.05f, 1.50f, 0.0f), 0.075f);
        const int upper = s.add("upperarm" + suffix, clavicle, Vector3(sign * 0.19f, 1.48f, 0.0f), 0.052f);
        const int fore = s.add("forearm" + suffix, upper, Vector3(sign * 0.21f, 1.20f, 0.0f), 0.042f);
        s.add("hand" + suffix, fore, Vector3(sign * 0.22f, 0.95f, 0.0f), 0.06f);
    }
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side == 0 ? 1.0f : -1.0f;
        const std::string suffix = side == 0 ? ".R" : ".L";
        const int thigh = s.add("thigh" + suffix, pelvis, Vector3(sign * 0.111f, 0.992f, 0.0f), 0.072f);
        const int shin = s.add("shin" + suffix, thigh, Vector3(sign * 0.152f, 0.537f, 0.0f), 0.062f);
        s.add("foot" + suffix, shin, Vector3(sign * 0.208f, 0.077f, 0.0f), 0.09f);
    }
    return s;
}

}  // namespace

int main()
{
    MaterialLibrary materials(nullptr);   // no device: the skeleton needs none
    const CharacterFactory characters(materials);
    Rng rng(7u);
    const CharacterLook look = characters.look(rng, 0);
    const CharacterFactory::Character figure = characters.build(look, false);
    const Geometry::Skeleton& skeleton = figure.skeleton;
    const CharacterFactory::Clips clips = CharacterFactory::clips(skeleton, look.height, 1.06f);
    const AnimationClip* walks[3] = {&clips.walk, &clips.walkBrisk, &clips.walkEasy};
    const AnimationClip* idles[3] = {&clips.idle, &clips.idlePhone, &clips.idleHands};

    CASE("every walk drives the legs, the arms, the pelvis, the trunk and the head");
    for (const AnimationClip* walk : walks)
    {
        CHECK_NEAR(static_cast<double>(walk->Duration.getTicksProperty()) / 1.0e7, 1.06, 1e-3);
        for (const char* bone : {"thigh.R", "thigh.L", "shin.R", "shin.L", "foot.R", "foot.L",
                                 "upperarm.R", "upperarm.L", "forearm.R", "forearm.L", "pelvis",
                                 "chest", "neck", "head"})
            CHECK_MSG(TrackFor(*walk, skeleton, bone) != nullptr, std::string("no track for ") + bone);
        for (const BoneTrackEXT& track : walk->Tracks)
        {
            CHECK(track.Keys.size() >= 13);
            // A keyframe replaces the whole local transform, so a track that
            // leaves the translation at zero collapses its bone onto its parent.
            if (track.BoneIndex > 0)
                for (const auto& key : track.Keys)
                    CHECK(key.Translation.Length() > 0.02f);
        }
    }

    CASE("a knee only bends backward");
    for (const AnimationClip* walk : walks)
        for (const char* shin : {"shin.R", "shin.L"})
        {
            const BoneTrackEXT* track = TrackFor(*walk, skeleton, shin);
            CHECK(track != nullptr);
            if (track == nullptr) continue;
            CHECK_MSG(MinPitch(*track) > -0.02f, "a knee bends forward");
            CHECK_MSG(MaxPitch(*track) > 0.6f, "a knee that never clears the ground");
        }

    CASE("a stride swings the thigh both ways, and the legs are half a cycle apart");
    for (const AnimationClip* walk : walks)
    {
        const BoneTrackEXT* right = TrackFor(*walk, skeleton, "thigh.R");
        const BoneTrackEXT* left  = TrackFor(*walk, skeleton, "thigh.L");
        CHECK(right != nullptr && left != nullptr);
        if (right == nullptr || left == nullptr) continue;
        CHECK(MaxPitch(*right) > 0.15f);
        CHECK(MinPitch(*right) < -0.25f);
        const std::size_t keys = right->Keys.size() - 1;   // the last key repeats the first
        for (std::size_t i = 0; i < keys; ++i)
        {
            const std::size_t j = (i + keys / 2) % keys;
            CHECK_NEAR(PitchOf(right->Keys[i].Rotation), PitchOf(left->Keys[j].Rotation), 0.03);
        }
    }

    CASE("the arm on a side swings against the leg on that side");
    for (const AnimationClip* walk : walks)
    {
        const BoneTrackEXT* thigh = TrackFor(*walk, skeleton, "thigh.R");
        const BoneTrackEXT* arm   = TrackFor(*walk, skeleton, "upperarm.R");
        CHECK(thigh != nullptr && arm != nullptr);
        if (thigh == nullptr || arm == nullptr) continue;
        // At the key where the right leg is furthest forward the right arm is
        // back, and where the leg is furthest back the arm is forward.
        std::size_t forward = 0, back = 0;
        for (std::size_t i = 0; i < thigh->Keys.size(); ++i)
        {
            if (PitchOf(thigh->Keys[i].Rotation) < PitchOf(thigh->Keys[forward].Rotation)) forward = i;
            if (PitchOf(thigh->Keys[i].Rotation) > PitchOf(thigh->Keys[back].Rotation)) back = i;
        }
        CHECK(PitchOf(arm->Keys[forward].Rotation) > 0.05f);
        CHECK(PitchOf(arm->Keys[back].Rotation) < -0.05f);
    }

    CASE("the three walks are three walks");
    {
        const auto swing = [&](const AnimationClip& clip, const char* bone) {
            const BoneTrackEXT* track = TrackFor(clip, skeleton, bone);
            return track == nullptr ? 0.0f : MaxPitch(*track) - MinPitch(*track);
        };
        CHECK(swing(clips.walkBrisk, "thigh.R") > swing(clips.walk, "thigh.R"));
        CHECK(swing(clips.walk, "thigh.R") > swing(clips.walkEasy, "thigh.R"));
        CHECK(swing(clips.walkBrisk, "upperarm.R") > swing(clips.walk, "upperarm.R"));
        CHECK(swing(clips.walk, "upperarm.R") > swing(clips.walkEasy, "upperarm.R"));
        CHECK(CharacterFactory::Clips::kStrideScale[1] > CharacterFactory::Clips::kStrideScale[0]);
        CHECK(CharacterFactory::Clips::kStrideScale[2] < CharacterFactory::Clips::kStrideScale[0]);
    }

    CASE("standing still is not standing still, and the three stances differ");
    {
        for (const AnimationClip* idle : idles)
        {
            CHECK(static_cast<double>(idle->Duration.getTicksProperty()) / 1.0e7 > 3.0);
            const BoneTrackEXT* head = TrackFor(*idle, skeleton, "head");
            CHECK(head != nullptr);
            // Nobody's legs walk while they wait.
            for (const char* thigh : {"thigh.R", "thigh.L"})
                if (const BoneTrackEXT* track = TrackFor(*idle, skeleton, thigh))
                    CHECK(MaxPitch(*track) - MinPitch(*track) < 0.2f);
        }
        const BoneTrackEXT* phoneArm  = TrackFor(clips.idlePhone, skeleton, "forearm.R");
        const BoneTrackEXT* phoneHead = TrackFor(clips.idlePhone, skeleton, "head");
        CHECK(phoneArm != nullptr && MinPitch(*phoneArm) < -1.2f);
        CHECK(phoneHead != nullptr && MinPitch(*phoneHead) > 0.2f);
        for (const char* forearm : {"forearm.R", "forearm.L"})
        {
            const BoneTrackEXT* hands = TrackFor(clips.idleHands, skeleton, forearm);
            CHECK(hands != nullptr && MinPitch(*hands) < -1.0f);
        }
        const BoneTrackEXT* lookHead = TrackFor(clips.idle, skeleton, "head");
        CHECK(lookHead != nullptr);
        if (lookHead != nullptr)
        {
            float yawMin = 1e9f, yawMax = -1e9f;
            for (const auto& key : lookHead->Keys)
            {
                const float yaw = 2.0f * std::atan2(key.Rotation.Y, key.Rotation.W);
                yawMin = std::min(yawMin, yaw);
                yawMax = std::max(yawMax, yaw);
            }
            CHECK_MSG(yawMax - yawMin > 0.3f, "the looking-about idle does not look about");
        }
    }

    CASE("nobody walks astride: the feet stay under the body");
    {
        // The defect this catches: MakeHuman's base mesh stands in an A-pose
        // and its rig diverges all the way down, so an imported person's
        // ankle joints are 29 to 42 cm apart. A gait built only in the
        // sagittal plane leaves them there, and the figure walks with its
        // feet a third of a metre either side of its centre line -- which is
        // what somebody watching the street from the kerb called an
        // anatomically implausible wide-legged walk. A real walking base of
        // support is 5 to 13 cm.
        //
        // Checked on both rigs the project animates: the generated one, whose
        // legs are already parallel, and a copy of the widest imported one.
        struct Rig { const char* what; Geometry::Skeleton bones; };
        std::vector<Rig> rigs;
        rigs.push_back(Rig{"the generated rig", figure.skeleton});
        rigs.push_back(Rig{"a MakeHuman rig (person-05)", SplayedRig()});
        for (const Rig& rig : rigs)
        {
            const int footR = rig.bones.find("foot.R");
            const int footL = rig.bones.find("foot.L");
            CHECK(footR >= 0 && footL >= 0);
            if (footR < 0 || footL < 0) continue;
            const float bind = std::fabs(rig.bones[footR].head.X - rig.bones[footL].head.X);
            for (const float stance : {0.88f, 1.0f, 1.14f})
            {
                const CharacterFactory::Clips posed =
                    CharacterFactory::clips(rig.bones, 1.75f, 1.06f, stance);
                const AnimationClip* moving[4] = {&posed.walk, &posed.walkBrisk, &posed.walkEasy,
                                                  &posed.idle};
                for (const AnimationClip* clip : moving)
                {
                    float widest = 0.0f;
                    for (int step = 0; step <= 24; ++step)
                    {
                        const std::vector<Vector3> at =
                            BonesAt(*clip, rig.bones, static_cast<float>(step) / 24.0f);
                        widest = std::max(widest,
                                          std::fabs(at[static_cast<std::size_t>(footR)].X
                                                    - at[static_cast<std::size_t>(footL)].X));
                    }
                    // Never wider than a shoulder-width stance, and never
                    // wider than the rig arrived: the correction only ever
                    // brings a leg in.
                    CHECK_MSG(widest < 0.30f,
                              std::string(rig.what) + ": the feet are "
                                  + std::to_string(widest) + " m apart at some point in the cycle");
                    CHECK_MSG(widest <= bind + 1e-3f,
                              std::string(rig.what) + ": the correction pushed a leg outward");
                    // And not so narrow that the figure walks a tightrope.
                    CHECK_MSG(widest > 0.03f, std::string(rig.what) + ": the feet are on one line");
                }
            }
        }
    }

    CASE("the clips install under the names the scene asks for");
    {
        std::unordered_map<std::string, AnimationClip> installed;
        clips.install(installed);
        CHECK(installed.size() == 6);
        for (const char* name : CharacterFactory::Clips::kWalkNames) CHECK(installed.count(name) == 1);
        for (const char* name : CharacterFactory::Clips::kIdleNames) CHECK(installed.count(name) == 1);
    }

    TEST_MAIN("gait");
}
