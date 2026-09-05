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
