// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

namespace CnaStreet {

/// Preset quality levels. Each one is a *set* of decisions that make sense
/// together, not a single slider: dropping shadow resolution while keeping
/// four cascades, for instance, is worse than dropping to two sharper ones.
enum class QualityPreset
{
    Low,
    Medium,
    High,
    Ultra
};

/**
 * @brief Everything about the frame a user can change.
 *
 * Loaded from `assets/config/render.json` at start-up if present, overridden by
 * the command line, and adjustable at runtime from the debug overlay. Kept as a
 * plain struct so that reading a value costs nothing and the settings file, the
 * command line and the key bindings all write to exactly the same place.
 */
struct RenderSettings
{
    // --- presentation ---
    int  windowWidth  = 1600;
    int  windowHeight = 900;
    bool fullscreen   = false;
    bool vsync        = true;
    int  multiSample  = 4;
    /// Renders at this fraction of the window and upscales. Below 1 it is the
    /// cheapest large saving there is; above 1 it is supersampling.
    float renderScale = 1.0f;

    // --- camera ---
    float verticalFovDegrees = 63.0f;
    float nearPlane = 0.12f;
    float farPlane  = 620.0f;
    float mouseSensitivity = 0.0022f;
    float moveSpeed = 7.0f;
    bool  invertY = false;

    // --- lighting and shadows ---
    bool  shadows = true;
    int   shadowCascades = 4;
    /// 0 low / 1 medium / 2 high / 3 ultra, mapped onto CNA's ShadowQuality.
    int   shadowQuality = 2;
    float shadowDistance = 190.0f;
    float shadowSplitLambda = 0.86f;
    /// Bigger than CNA's 0.0015 default, and it has to be. The cascade atlas is
    /// an 8-bit colour target holding light-space distance, so one quantisation
    /// step is 1/255 = 0.0039 — larger than that default, which guarantees
    /// self-shadowing acne on every surface facing the sun.
    float shadowDepthBias = 0.0060f;
    float shadowBlendBand = 0.12f;
    bool  shadowDebugTint = false;

    // --- atmosphere ---
    /// Sun elevation and azimuth in degrees, azimuth measured clockwise from
    /// north.
    ///
    /// The elevation is the number that matters, and it is a geometry problem
    /// rather than a taste one. A 17 m building throws a shadow 17/tan(elevation)
    /// long; the street is 18.6 m between building lines. Below about 43 degrees
    /// that shadow crosses the whole carriageway and the entire canyon is in
    /// shade — physically right, and a flat, sunless picture. At 56 degrees it
    /// reaches 15.3 m, which puts a crisp shadow edge down the road with the
    /// eastern few metres and the far footway still in full sun. That contrast
    /// is most of what makes a street look photographed.
    float sunElevationDegrees = 48.0f;
    float sunAzimuthDegrees   = 250.0f;

    /// Whether the street's own lights are burning.
    ///
    /// Derived from the sun rather than set independently, because that is the
    /// relationship it has: a photocell on a lamp column switches at civil
    /// dusk, and a demo where the sun is down and the lamps are off is a demo
    /// with a bug in it. Three degrees is about when a street stops being lit
    /// by the sky.
    [[nodiscard]] bool nightLighting() const { return sunElevationDegrees < 3.0f; }
    float sunIntensity        = 3.0f;
    /// Image based lighting from the baked sky cubemap. Off falls back to a
    /// hemisphere ambient term, which is also what a renderer without IBL gets.
    bool  imageBasedLighting  = true;
    /// Multiplies the whole image-based ambient term. 1.0 is the physically
    /// consistent value -- the sky really is that bright next to the sun. It
    /// used to sit at 0.86 to stand in for the sky a street canyon cannot
    /// see; the reflection probes now capture that canyon, walls and all, so
    /// the stand-in would count the same occlusion twice.
    float iblIntensity        = 1.0f;
    float skyTurbidity        = 2.9f;
    float skyIntensity        = 1.0f;
    bool  clouds = true;
    float cloudCoverage = 0.45f;
    float cloudSpeed = 0.006f;

    // --- post processing ---
    bool  hdr = true;
    float exposure = 0.42f;
    /// 0 none / 1 Reinhard / 2 ACES / 3 filmic, mapped onto TonemappingMode.
    int   tonemap = 2;
    bool  bloom = true;
    float bloomIntensity = 0.42f;
    float bloomThreshold = 1.15f;
    bool  ssao = true;
    float ssaoRadius = 0.28f;
    float ssaoIntensity = 0.72f;
    bool  fxaa = true;
    bool  heightFog = true;
    float fogDensity = 0.016f;
    float fogFalloff = 0.055f;
    bool  lightShafts = true;
    bool  ssr = false;
    bool  depthOfField = false;

    // --- reflections ---
    /// Local environment capture. The street is rendered into a small cube
    /// map from a row of points along each carriageway at scene build, and
    /// every draw near one of them reads its image-based lighting from that
    /// cube instead of from the sky's. A car then reflects the building it is
    /// parked outside and a shop window reflects the street in front of it,
    /// which is the single cue the sky-only environment could never supply:
    /// under a sky cube alone a dark car is a dark shape.
    bool  reflectionProbes = true;
    /// Whether a probe's own irradiance replaces the sky's for the diffuse
    /// ambient term as well. A street canyon sees a third of the sky the
    /// open hemisphere does and is lit the rest of the way by the sunlit
    /// facade opposite; the probe has both, the sky cube has neither.
    bool  probeIrradiance = true;
    /// Distance between probes along a carriageway, in metres.
    float probeSpacing = 24.0f;
    /// Edge length of one captured cube face, in pixels. 64 resolves a car
    /// parked across the street as a car; 32 as a coloured smear, which is
    /// still what a glossy panel reflects at a glance.
    int   probeFaceSize = 64;

    // --- content ---
    bool traffic = true;
    bool pedestrians = true;
    bool vegetation = true;
    bool streetFurniture = true;
    bool debugOverlay = true;
    /// Development mode: one of every vehicle variant parked in a row, nothing
    /// moving, and a viewpoint aimed at each. Not a preset and not in the
    /// settings file -- `--lineup` on the command line and nothing else.
    bool vehicleLineup = false;
    /// Distance past which small props stop being drawn at all.
    float propCullDistance = 210.0f;
    /// Distance past which a prop stops being written into the shadow map.
    float propShadowDistance = 74.0f;
    /// Distance past which a person is not drawn.
    ///
    /// Its own number rather than @ref propCullDistance, because a person is
    /// the most expensive small object in the scene and the cheapest one to
    /// lose. A skinned figure cannot be instanced -- each one carries its own
    /// bone palette -- so it costs three draw calls wherever it stands, and at
    /// 210 m it is under four pixels tall. 130 m is the whole modelled length
    /// of the street and then some: everybody a viewer can make out as a person
    /// is still there, and the fifty who were four pixels at the vanishing
    /// point are not.
    float pedestrianCullDistance = 130.0f;
    /// Distance past which a person is drawn with the collapsed material set:
    /// head, coat and legs in three draws rather than six. About the width of
    /// this street, so everybody on the near footway is fully detailed.
    float pedestrianDetailDistance = 20.0f;

    // --- determinism ---
    std::uint32_t seed = 20260903u;

    /// Applies a preset over the current values, leaving the camera and the
    /// determinism seed alone.
    void applyPreset(QualityPreset preset);
    [[nodiscard]] static const char* presetName(QualityPreset preset);

    /// Parses a JSON settings document (sharp-runtime's System.Text.Json).
    /// Unknown keys are reported and ignored rather than failing the launch:
    /// a settings file written for a newer build should still start the demo.
    /// Returns the number of values applied, or -1 if the document is invalid.
    int applyJson(const std::string& json, std::string& error);

    /// Serialises the current values back to JSON, so the overlay can dump a
    /// settings file that reproduces what is on screen.
    [[nodiscard]] std::string toJson() const;
};

}  // namespace CnaStreet
