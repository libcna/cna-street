// SPDX-License-Identifier: MIT
#include "CnaStreet/Audio/SoundScape.hpp"

#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/CameraController.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Scene/CityScene.hpp"
#include "CnaStreet/Sim/PedestrianSystem.hpp"
#include "CnaStreet/Sim/TrafficSystem.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Audio;

namespace CnaStreet {

namespace {

/// The names `scripts/prepare-audio.py` writes. Anything absent is simply
/// not played; nothing here is required.
const char* const kSampleNames[] = {
    "engine-idle", "engine-low", "engine-high", "engine-drive", "horn",
    "ambience-wind", "ambience-birds",
    "step-1", "step-2", "step-3", "step-4", "step-5", "step-6", "step-7", "step-8",
    "voice-m-1", "voice-m-2", "voice-m-3", "voice-m-4",
    "voice-f-1", "voice-f-2", "voice-f-3",
};

/// How many cars can be heard at once, and how many footsteps, horns and
/// voices. The mixer's own ceiling is not published; this stays well under
/// anything reasonable and a refused voice is logged once, not thrown.
constexpr int kEngineVoices  = 6;
constexpr int kOneShotVoices = 6;
/// Past this a car is not heard at all, whatever the attenuation says: a
/// street full of cars at -30 dB is a hum, and the nearest six are the ones
/// a person can pick out.
constexpr float kEngineReach = 55.0f;
/// Within this a source is at full volume; beyond it the mixer's own inverse
/// law takes over. Three and a half metres is about a car's length: a car
/// beside the camera is loud, a car across the street is half as loud.
constexpr float kDistanceScale = 3.5f;

/// A walking camera's stride, in metres. The same 0.72 m the pedestrians
/// are queued at, which is about a step.
constexpr float kCameraStride = 0.72f;

float Length(const Vector3& v) { return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z); }

}  // namespace

SoundScape::SoundScape(const std::string& directory)
{
    if (!std::filesystem::is_directory(directory))
    {
        status_ = "silent: no derived sounds at " + directory
                  + " (run scripts/prepare-audio.py)";
        CNA::Logger::Info("cna-street: audio " + status_);
        return;
    }

    int loaded = 0;
    std::string firstFailure;
    for (const char* name : kSampleNames)
    {
        const std::filesystem::path file = std::filesystem::path(directory) / (std::string(name) + ".wav");
        if (!std::filesystem::is_regular_file(file)) continue;
        try
        {
            auto effect = std::make_unique<SoundEffect>(file.string());
            samples_[name] = std::move(effect);
            ++loaded;
        }
        catch (const std::exception& failure)
        {
            // The first failure is almost always "no audio device", and one
            // line saying so is enough; a machine with no sound card should
            // not read twenty-two of them.
            if (firstFailure.empty()) firstFailure = failure.what();
        }
    }

    if (loaded == 0)
    {
        status_ = firstFailure.empty() ? "silent: nothing derived in " + directory
                                       : "silent: " + firstFailure;
        CNA::Logger::Info("cna-street: audio " + status_);
        return;
    }

    for (int i = 1; i <= 8; ++i)
        if (SoundEffect* step = sample("step-" + std::to_string(i))) steps_.push_back(step);
    for (int i = 1; i <= 4; ++i)
        if (SoundEffect* voice = sample("voice-m-" + std::to_string(i))) voicesMale_.push_back(voice);
    for (int i = 1; i <= 3; ++i)
        if (SoundEffect* voice = sample("voice-f-" + std::to_string(i))) voicesFemale_.push_back(voice);

    // The mixer's three global 3D constants. Distance is the one that
    // matters; the other two are physics and stay at physics.
    SoundEffect::setDistanceScaleProperty(kDistanceScale);
    SoundEffect::setDopplerScaleProperty(1.0f);
    SoundEffect::setSpeedOfSoundProperty(343.0f);

    engines_.resize(kEngineVoices);
    available_ = true;
    status_ = std::to_string(loaded) + " of " + std::to_string(std::size(kSampleNames))
              + " sounds loaded";
    CNA::Logger::Info("cna-street: audio " + status_);
}

SoundScape::~SoundScape()
{
    // Stop everything before the effects go: an instance outliving its
    // effect is what a use-after-free sounds like.
    for (Engine& engine : engines_)
        if (engine.voice != nullptr) engine.voice->Stop();
    for (OneShot& shot : oneShots_)
        if (shot.voice != nullptr) shot.voice->Stop();
    if (wind_ != nullptr) wind_->Stop();
    if (birds_ != nullptr) birds_->Stop();
    engines_.clear();
    oneShots_.clear();
    wind_.reset();
    birds_.reset();
}

SoundEffect* SoundScape::sample(const std::string& name) const
{
    const auto found = samples_.find(name);
    return found == samples_.end() ? nullptr : found->second.get();
}

AudioEmitter SoundScape::emitterAt(const Vector3& at, const Vector3& velocity,
                                   const Vector3& forward)
{
    AudioEmitter emitter;
    emitter.setPositionProperty(at);
    emitter.setVelocityProperty(velocity);
    emitter.setForwardProperty(Length(forward) > 1e-4f ? forward : Vector3(0.0f, 0.0f, 1.0f));
    emitter.setUpProperty(Vector3::Up);
    emitter.setDopplerScaleProperty(1.0f);
    return emitter;
}

void SoundScape::setEnabled(bool enabled)
{
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (!enabled_)
    {
        for (Engine& engine : engines_)
        {
            if (engine.voice != nullptr) engine.voice->Stop();
            engine.voice.reset();
            engine.vehicle = -1;
            engine.band = -1;
        }
        for (OneShot& shot : oneShots_)
            if (shot.voice != nullptr) shot.voice->Stop();
        oneShots_.clear();
        if (wind_ != nullptr) wind_->Pause();
        if (birds_ != nullptr) birds_->Pause();
    }
    else
    {
        if (wind_ != nullptr) wind_->Resume();
        if (birds_ != nullptr) birds_->Resume();
    }
}

bool SoundScape::playAt(SoundEffect* effect, const Vector3& at, const Vector3& velocity,
                        float volume, float pitch)
{
    if (effect == nullptr) return false;
    if (static_cast<int>(oneShots_.size()) >= kOneShotVoices) return false;
    try
    {
        OneShot shot;
        shot.voice = std::make_unique<SoundEffectInstance>(effect->CreateInstance());
        shot.voice->setVolumeProperty(std::clamp(volume, 0.0f, 1.0f));
        shot.voice->setPitchProperty(std::clamp(pitch, -1.0f, 1.0f));
        shot.voice->Apply3D(listener_, emitterAt(at, velocity, Vector3(0.0f, 0.0f, 1.0f)));
        shot.voice->Play();
        shot.placed = true;
        oneShots_.push_back(std::move(shot));
        return true;
    }
    catch (const std::exception& failure)
    {
        if (!warnedVoiceLimit_)
        {
            warnedVoiceLimit_ = true;
            CNA::Logger::Warn(std::string("cna-street: audio refused a voice: ") + failure.what());
        }
        return false;
    }
}

bool SoundScape::playFlat(SoundEffect* effect, float volume, float pitch)
{
    if (effect == nullptr) return false;
    if (static_cast<int>(oneShots_.size()) >= kOneShotVoices) return false;
    try
    {
        OneShot shot;
        shot.voice = std::make_unique<SoundEffectInstance>(effect->CreateInstance());
        shot.voice->setVolumeProperty(std::clamp(volume, 0.0f, 1.0f));
        shot.voice->setPitchProperty(std::clamp(pitch, -1.0f, 1.0f));
        shot.voice->Play();
        oneShots_.push_back(std::move(shot));
        return true;
    }
    catch (const std::exception& failure)
    {
        if (!warnedVoiceLimit_)
        {
            warnedVoiceLimit_ = true;
            CNA::Logger::Warn(std::string("cna-street: audio refused a voice: ") + failure.what());
        }
        return false;
    }
}

void SoundScape::reapOneShots()
{
    oneShots_.erase(std::remove_if(oneShots_.begin(), oneShots_.end(),
                                   [](const OneShot& shot) {
                                       return shot.voice == nullptr
                                              || shot.voice->getStateProperty()
                                                     == SoundState::Stopped;
                                   }),
                    oneShots_.end());
}

void SoundScape::update(float deltaSeconds, const Camera& camera, bool walking,
                        const CityScene& scene, const RenderSettings& settings)
{
    if (!available_) return;
    setEnabled(settings.audio);
    if (!enabled_) return;

    SoundEffect::setMasterVolumeProperty(std::clamp(settings.audioVolume, 0.0f, 1.0f));

    // The listener is the camera. Its velocity is the camera's own, clamped
    // to a run: in Fly mode the camera does a hundred metres a second, and a
    // listener doing that hears every engine in the street a fifth up.
    const Vector3 at = camera.position();
    Vector3 velocity = Vector3::Zero;
    if (listenerPlaced_ && deltaSeconds > 1e-4f)
    {
        velocity = (at - lastListenerAt_) * (1.0f / deltaSeconds);
        const float speed = Length(velocity);
        if (speed > 8.0f) velocity = velocity * (8.0f / speed);
    }
    listener_.setPositionProperty(at);
    listener_.setForwardProperty(camera.forward());
    listener_.setUpProperty(camera.up());
    listener_.setVelocityProperty(velocity);

    reapOneShots();
    updateAmbience(scene);
    updateEngines(deltaSeconds, scene);
    updateFootsteps(deltaSeconds, camera, walking, scene);
    updateVoices(deltaSeconds, scene);

    lastListenerAt_ = at;
    listenerPlaced_ = true;

    activeVoices_ = static_cast<int>(oneShots_.size()) + (wind_ ? 1 : 0) + (birds_ ? 1 : 0);
    for (const Engine& engine : engines_)
        if (engine.voice != nullptr) ++activeVoices_;
}

void SoundScape::updateAmbience(const CityScene& scene)
{
    // The wind is nowhere in particular, so it is never placed: it is the one
    // sound that does not pan or fall off, which is what "everywhere" sounds
    // like.
    if (wind_ == nullptr)
        if (SoundEffect* wind = sample("ambience-wind"))
        {
            try
            {
                wind_ = std::make_unique<SoundEffectInstance>(wind->CreateInstance());
                wind_->setIsLoopedProperty(true);
                wind_->setVolumeProperty(0.28f);
                wind_->Play();
            }
            catch (const std::exception&) { wind_.reset(); }
        }

    // The birds are in the nearest tree, because that is where birds are: the
    // one emitter follows whichever crown is closest to the listener, so the
    // flock is loudest under the trees and a memory on the crossing.
    const std::vector<Vector3>& trees = scene.treePositions();
    if (trees.empty()) return;
    const Vector3 ear = listener_.getPositionProperty();
    const Vector3* nearest = nullptr;
    float best = 1e30f;
    for (const Vector3& tree : trees)
    {
        const float dx = tree.X - ear.X, dz = tree.Z - ear.Z;
        const float d = dx * dx + dz * dz;
        if (d < best) { best = d; nearest = &tree; }
    }
    if (nearest == nullptr) return;
    const Vector3 crown(nearest->X, nearest->Y + 5.5f, nearest->Z);
    if (birds_ == nullptr)
        if (SoundEffect* birds = sample("ambience-birds"))
        {
            try
            {
                birds_ = std::make_unique<SoundEffectInstance>(birds->CreateInstance());
                birds_->setIsLoopedProperty(true);
                birds_->setVolumeProperty(0.55f);
                birds_->Apply3D(listener_, emitterAt(crown, Vector3::Zero, Vector3::Up));
                birds_->Play();
            }
            catch (const std::exception&) { birds_.reset(); }
        }
    if (birds_ != nullptr)
        birds_->Apply3D(listener_, emitterAt(crown, Vector3::Zero, Vector3::Up));
}

void SoundScape::updateEngines(float deltaSeconds, const CityScene& scene)
{
    SoundEffect* bands[4] = {sample("engine-idle"), sample("engine-low"), sample("engine-high"),
                             sample("engine-drive")};
    if (bands[0] == nullptr && bands[1] == nullptr) return;

    const TrafficSystem& traffic = scene.traffic();
    const std::vector<Vehicle>& fleet = traffic.vehicles();
    const std::vector<Lane>& lanes = traffic.lanes();
    const Vector3 ear = listener_.getPositionProperty();

    // The nearest few moving cars, by distance to the ear.
    struct Near { int index; float distance; };
    std::vector<Near> nearest;
    for (std::size_t i = 0; i < fleet.size(); ++i)
    {
        const Vehicle& vehicle = fleet[i];
        if (vehicle.parked) continue;
        const Vector2 ground = vehicle.groundPosition(lanes);
        const float dx = ground.X - ear.X, dz = ground.Y - ear.Z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > kEngineReach) continue;
        nearest.push_back(Near{static_cast<int>(i), distance});
    }
    std::sort(nearest.begin(), nearest.end(),
              [](const Near& a, const Near& b) { return a.distance < b.distance; });
    if (nearest.size() > engines_.size()) nearest.resize(engines_.size());

    // Keep the voices that are still among the nearest, free the rest.
    for (Engine& engine : engines_)
    {
        if (engine.vehicle < 0) continue;
        const bool still = std::any_of(nearest.begin(), nearest.end(),
                                       [&](const Near& n) { return n.index == engine.vehicle; });
        if (still) continue;
        if (engine.voice != nullptr) engine.voice->Stop();
        engine.voice.reset();
        engine.vehicle = -1;
        engine.band = -1;
    }

    for (const Near& near : nearest)
    {
        Engine* engine = nullptr;
        for (Engine& candidate : engines_)
            if (candidate.vehicle == near.index) { engine = &candidate; break; }
        if (engine == nullptr)
        {
            for (Engine& candidate : engines_)
                if (candidate.vehicle < 0) { engine = &candidate; break; }
            if (engine == nullptr) break;
            engine->vehicle = near.index;
            engine->band = -1;
            // A detune of the car's own, from its index, so a queue at the
            // lights is not one engine six times over.
            noise_ = noise_ * 1664525u + 1013904223u;
            engine->detune = -0.12f + 0.24f * static_cast<float>((near.index * 7919) % 1000) / 1000.0f;
        }

        const Vehicle& vehicle = fleet[static_cast<std::size_t>(near.index)];
        const float speed = vehicle.speed;
        // Which sample, and how far to bend it inside its band: idle under
        // half a metre a second, low revs to about 20 km/h, high revs to
        // about 40, and the road-noise loop over that.
        int band;
        float bend;
        if (speed < 0.5f)       { band = 0; bend = 0.0f; }
        else if (speed < 5.5f)  { band = 1; bend = -0.12f + 0.30f * (speed / 5.5f); }
        else if (speed < 11.0f) { band = 2; bend = -0.15f + 0.32f * ((speed - 5.5f) / 5.5f); }
        else                    { band = 3; bend = -0.05f + 0.20f * std::min(1.0f, (speed - 11.0f) / 8.0f); }
        while (bands[band] == nullptr && band > 0) --band;
        if (bands[band] == nullptr) continue;
        // A van is a bigger engine turning slower.
        const float sizePitch = vehicle.type == VehicleType::Van ? -0.22f : 0.0f;

        if (band != engine->band)
        {
            if (engine->voice != nullptr) engine->voice->Stop();
            engine->voice.reset();
            try
            {
                engine->voice = std::make_unique<SoundEffectInstance>(bands[band]->CreateInstance());
                engine->voice->setIsLoopedProperty(true);
                engine->voice->setVolumeProperty(band == 0 ? 0.55f : 0.75f);
                engine->band = band;
            }
            catch (const std::exception& failure)
            {
                if (!warnedVoiceLimit_)
                {
                    warnedVoiceLimit_ = true;
                    CNA::Logger::Warn(std::string("cna-street: audio refused a voice: ")
                                      + failure.what());
                }
                engine->voice.reset();
                engine->vehicle = -1;
                engine->band = -1;
                continue;
            }
        }
        if (engine->voice == nullptr) continue;

        // Where the car is and which way it is going, for the pan and the
        // Doppler: the transform's third row is its forward.
        const Matrix world = vehicle.transform(lanes);
        const Vector3 forward(world.M31, world.M32, world.M33);
        const Vector3 at(world.M41, world.M42 + 0.5f, world.M43);
        engine->voice->setPitchProperty(std::clamp(bend + engine->detune + sizePitch, -1.0f, 1.0f));
        engine->voice->Apply3D(listener_, emitterAt(at, forward * speed, forward));
        if (engine->voice->getStateProperty() != SoundState::Playing) engine->voice->Play();
    }

    // A horn, now and then, from a car braking hard near the camera. One
    // every eight seconds at most from the whole street, because a street
    // where somebody leans on the horn every few seconds is a film, not a
    // street.
    hornCooldown_ = std::max(0.0f, hornCooldown_ - deltaSeconds);
    if (hornCooldown_ <= 0.0f)
        if (SoundEffect* horn = sample("horn"))
            for (const Near& near : nearest)
            {
                const Vehicle& vehicle = fleet[static_cast<std::size_t>(near.index)];
                if (!vehicle.braking || vehicle.speed < 3.0f || near.distance > 40.0f) continue;
                noise_ = noise_ * 1664525u + 1013904223u;
                // About one horn per twenty-five seconds of hard braking
                // within earshot.
                if (static_cast<float>(noise_ >> 8) / 16777216.0f > deltaSeconds / 25.0f) continue;
                const Matrix world = vehicle.transform(lanes);
                const Vector3 forward(world.M31, world.M32, world.M33);
                if (playAt(horn, Vector3(world.M41, world.M42 + 0.7f, world.M43),
                           forward * vehicle.speed, 0.8f,
                           -0.1f + 0.2f * static_cast<float>((near.index * 31) % 100) / 100.0f))
                    hornCooldown_ = 8.0f;
                break;
            }
}

void SoundScape::updateFootsteps(float deltaSeconds, const Camera& camera, bool walking,
                                 const CityScene& scene)
{
    (void)deltaSeconds;
    if (steps_.empty()) return;

    // The camera's own steps: one every stride of ground actually covered,
    // measured along the ground, so standing still and looking about is
    // silent and a run is a quicker footfall.
    if (walking && listenerPlaced_)
    {
        const Vector3 at = camera.position();
        const float dx = at.X - lastListenerAt_.X, dz = at.Z - lastListenerAt_.Z;
        stridePending_ += std::sqrt(dx * dx + dz * dz);
        if (stridePending_ >= kCameraStride)
        {
            stridePending_ = 0.0f;
            noise_ = noise_ * 1664525u + 1013904223u;
            SoundEffect* step = steps_[(noise_ >> 8) % steps_.size()];
            noise_ = noise_ * 1664525u + 1013904223u;
            playFlat(step, 0.42f, -0.06f + 0.12f * static_cast<float>((noise_ >> 8) % 1000) / 1000.0f);
        }
    }
    else
    {
        stridePending_ = 0.0f;
    }

    // The nearest few pedestrians' steps, at their own stride from their own
    // feet. A person's walk phase is the distance they have walked, so a step
    // is that crossing a multiple of half a stride -- the same clock their
    // legs run on, which is what keeps the sound on the foot.
    const PedestrianSystem& pedestrians = scene.pedestrians();
    const std::vector<Pedestrian>& crowd = pedestrians.people();
    const Vector3 ear = listener_.getPositionProperty();
    constexpr float kStepReach = 11.0f;
    int played = 0;
    for (std::size_t i = 0; i < crowd.size() && played < 3; ++i)
    {
        const Pedestrian& person = crowd[i];
        if (person.waiting) continue;
        const Vector2 ground = person.position(pedestrians.nodes(), pedestrians.edges());
        const float dx = ground.X - ear.X, dz = ground.Y - ear.Z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > kStepReach)
        {
            lastStep_.erase(static_cast<int>(i));
            continue;
        }
        const float half = std::max(person.stride, 0.4f) * 0.5f;
        const float now = std::floor(person.phase / half);
        auto last = lastStep_.find(static_cast<int>(i));
        if (last == lastStep_.end())
        {
            lastStep_[static_cast<int>(i)] = now;
            continue;
        }
        if (now <= last->second) continue;
        last->second = now;
        noise_ = noise_ * 1664525u + 1013904223u;
        SoundEffect* step = steps_[(noise_ >> 8) % steps_.size()];
        const Vector3 foot(ground.X, scene.groundHeight(ground.X, ground.Y) + 0.05f, ground.Y);
        // Quieter than the camera's own, and lighter for a lighter person.
        const float volume = 0.30f * std::clamp(person.height / 1.75f, 0.7f, 1.1f);
        if (playAt(step, foot, Vector3::Zero, volume,
                   0.10f - 0.25f * std::clamp((person.height - 1.4f) / 0.5f, 0.0f, 1.0f)))
            ++played;
    }
}

void SoundScape::updateVoices(float deltaSeconds, const CityScene& scene)
{
    if (voicesMale_.empty() && voicesFemale_.empty()) return;
    voiceCooldown_ = std::max(0.0f, voiceCooldown_ - deltaSeconds);
    if (voiceCooldown_ > 0.0f) return;

    // Somebody waiting at a crossing laughs, now and then. Waiting people,
    // because they are the ones standing together; and rarely, because a
    // crowd that laughs every few seconds is a laugh track.
    const PedestrianSystem& pedestrians = scene.pedestrians();
    const std::vector<Pedestrian>& crowd = pedestrians.people();
    const Vector3 ear = listener_.getPositionProperty();
    for (std::size_t i = 0; i < crowd.size(); ++i)
    {
        const Pedestrian& person = crowd[i];
        if (!person.waiting) continue;
        const Vector2 ground = person.position(pedestrians.nodes(), pedestrians.edges());
        const float dx = ground.X - ear.X, dz = ground.Y - ear.Z;
        if (dx * dx + dz * dz > 14.0f * 14.0f) continue;
        noise_ = noise_ * 1664525u + 1013904223u;
        // About one voice per forty seconds per waiting person in earshot.
        if (static_cast<float>(noise_ >> 8) / 16777216.0f > deltaSeconds / 40.0f) continue;
        const std::vector<SoundEffect*>& set =
            person.height < 1.68f && !voicesFemale_.empty() ? voicesFemale_ : voicesMale_;
        if (set.empty()) continue;
        noise_ = noise_ * 1664525u + 1013904223u;
        SoundEffect* voice = set[(noise_ >> 8) % set.size()];
        const Vector3 mouth(ground.X, scene.groundHeight(ground.X, ground.Y) + person.height * 0.92f,
                            ground.Y);
        if (playAt(voice, mouth, Vector3::Zero, 0.5f, 0.0f)) voiceCooldown_ = 6.0f;
        break;
    }
}

}  // namespace CnaStreet
