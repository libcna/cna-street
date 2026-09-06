// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect;
    class SoundEffectInstance;
}

namespace CnaStreet {

class Camera;
class CityScene;
struct RenderSettings;

/**
 * @brief What the street sounds like, from where the camera stands.
 *
 * The demo drew for six passes and said nothing. This is the sound of it,
 * through CNA's own XNA audio: `SoundEffect` for the samples, a
 * `SoundEffectInstance` per voice, and `Apply3D` against an `AudioListener`
 * at the camera for everything that has a place in the world -- which gives
 * each engine its distance attenuation, its pan across the listener's own
 * right axis and its Doppler shift from the mixer, not from code here.
 *
 * What plays, and why each thing is where it is:
 *
 *  * **Engines.** One looping voice for each of the nearest few moving cars,
 *    its sample chosen by the car's speed -- idle, low revs, high revs, on the
 *    move -- and its pitch bent within the band by the speed and by a detune
 *    of the car's own, so a queue at the lights is not one engine six times.
 *    The emitter carries the car's velocity, so a car passing the camera
 *    drops in pitch as it goes by.
 *  * **Horns.** Now and then, from a car braking hard near the camera.
 *  * **Footsteps.** The walking camera's own, a stride apart, on the paving
 *    the footway is made of; and the nearest few pedestrians', at their own
 *    stride from their own feet.
 *  * **Voices.** A laugh from somebody waiting at a crossing, occasionally.
 *  * **Ambience.** Wind, everywhere and not placed; birds, placed in whatever
 *    tree is nearest, because that is where birds are.
 *
 * Every sample comes from the NOX Sound Essentials packs (CC0) through
 * `scripts/prepare-audio.py`; a tree that has derived none of them runs
 * silent, and says so once in the log, the same way one with no models runs
 * with generated props. A machine with no audio device does the same.
 */
class SoundScape
{
public:
    /// @p directory is where the derived sounds are; nothing is loaded from
    /// anywhere else. Loading is the whole start-up cost: a few megabytes of
    /// PCM decoded once.
    explicit SoundScape(const std::string& directory);
    ~SoundScape();

    SoundScape(const SoundScape&) = delete;
    SoundScape& operator=(const SoundScape&) = delete;

    /// Whether anything can be heard: a device opened and at least one
    /// sample loaded.
    [[nodiscard]] bool available() const { return available_; }
    /// One line for the log and the overlay: what loaded, or why nothing did.
    [[nodiscard]] const std::string& status() const { return status_; }
    /// Voices playing right now, for the overlay.
    [[nodiscard]] int activeVoices() const { return activeVoices_; }

    /// Moves the listener to the camera and every emitter to where its
    /// source is this frame. @p walking says whether the camera is on foot,
    /// which is what decides whether it has footsteps.
    void update(float deltaSeconds, const Camera& camera, bool walking, const CityScene& scene,
                const RenderSettings& settings);

    /// Silences everything without unloading it, and the reverse.
    void setEnabled(bool enabled);

private:
    using SoundEffect = Microsoft::Xna::Framework::Audio::SoundEffect;
    using SoundEffectInstance = Microsoft::Xna::Framework::Audio::SoundEffectInstance;
    using AudioEmitter = Microsoft::Xna::Framework::Audio::AudioEmitter;
    using AudioListener = Microsoft::Xna::Framework::Audio::AudioListener;
    using Vector3 = Microsoft::Xna::Framework::Vector3;

    /// A sample by the name `prepare-audio.py` gave it, or null if that file
    /// was not derived.
    [[nodiscard]] SoundEffect* sample(const std::string& name) const;
    /// Plays @p effect once at @p at, in the world, and forgets it when it
    /// ends. Returns false when the mixer refused another voice.
    bool playAt(SoundEffect* effect, const Vector3& at, const Vector3& velocity, float volume,
                float pitch);
    /// Plays @p effect once with no place in the world: the camera's own
    /// footsteps, which are where the listener is by definition.
    bool playFlat(SoundEffect* effect, float volume, float pitch);
    void updateAmbience(const CityScene& scene);
    void updateEngines(float deltaSeconds, const CityScene& scene);
    void updateFootsteps(float deltaSeconds, const Camera& camera, bool walking,
                         const CityScene& scene);
    void updateVoices(float deltaSeconds, const CityScene& scene);
    void reapOneShots();

    /// The emitter for a thing at @p at moving at @p velocity, facing
    /// @p forward. The mixer reads position, velocity and orientation from
    /// it; the orientation is only ever used for a cone this project does
    /// not set, so a forward that is roughly right is right enough.
    static AudioEmitter emitterAt(const Vector3& at, const Vector3& velocity,
                                  const Vector3& forward);

    std::unordered_map<std::string, std::unique_ptr<SoundEffect>> samples_;
    std::vector<SoundEffect*> steps_;
    std::vector<SoundEffect*> voicesMale_;
    std::vector<SoundEffect*> voicesFemale_;

    AudioListener listener_;
    Vector3 lastListenerAt_{0.0f, 0.0f, 0.0f};
    bool    listenerPlaced_ = false;

    std::unique_ptr<SoundEffectInstance> wind_;
    std::unique_ptr<SoundEffectInstance> birds_;

    /// One looping engine voice, following one vehicle while it is among the
    /// nearest. The instance is replaced when the vehicle's speed crosses
    /// into another band's sample.
    struct Engine
    {
        int   vehicle = -1;
        int   band = -1;
        float detune = 0.0f;
        std::unique_ptr<SoundEffectInstance> voice;
    };
    std::vector<Engine> engines_;

    struct OneShot
    {
        std::unique_ptr<SoundEffectInstance> voice;
        bool placed = false;
    };
    std::vector<OneShot> oneShots_;

    /// How far the walking camera has gone since its last footstep.
    float  stridePending_ = 0.0f;
    /// Each pedestrian's walk phase at their last footstep, by index.
    std::unordered_map<int, float> lastStep_;
    float  hornCooldown_ = 0.0f;
    float  voiceCooldown_ = 0.0f;
    unsigned int noise_ = 0x9E3779B9u;

    bool   available_ = false;
    bool   enabled_ = true;
    bool   warnedVoiceLimit_ = false;
    int    activeVoices_ = 0;
    std::string status_;
};

}  // namespace CnaStreet
