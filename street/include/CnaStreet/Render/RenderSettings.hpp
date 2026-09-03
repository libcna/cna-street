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
    float shadowDepthBias = 0.0016f;
    float shadowBlendBand = 0.12f;
    bool  shadowDebugTint = false;

    // --- atmosphere ---
    /// Sun elevation and azimuth in degrees. 34/128 is a mid-afternoon sun from
    /// the south-west, which rakes the façades on one side of the street and
    /// leaves the other in shadow — the light that makes a street look like a
    /// photograph rather than like a render.
    float sunElevationDegrees = 34.0f;
    float sunAzimuthDegrees   = 128.0f;
    float sunIntensity        = 2.6f;
    /// Image based lighting from the baked sky cubemap. Off falls back to a
    /// hemisphere ambient term, which is also what a renderer without IBL gets.
    bool  imageBasedLighting  = true;
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

    // --- content ---
    bool traffic = true;
    bool pedestrians = true;
    bool vegetation = true;
    bool streetFurniture = true;
    bool debugOverlay = true;
    /// Distance past which small props stop being drawn at all.
    float propCullDistance = 210.0f;
    /// Distance past which a prop stops being written into the shadow map.
    float propShadowDistance = 74.0f;

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
