// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief Settings parsing and the quality presets.
 *
 * The parser's contract is the interesting part: a settings file written for a
 * newer build must still start the demo, so an unknown key is reported and
 * ignored rather than fatal. A malformed document is a different matter and is
 * refused.
 */
#include "CnaStreet/Render/RenderSettings.hpp"

#include "TestSupport.hpp"

using namespace CnaStreet;

int main()
{
    CASE("the defaults are a street somebody can actually walk down");
    {
        RenderSettings settings;
        CHECK(settings.windowWidth > 0 && settings.windowHeight > 0);
        CHECK(settings.nearPlane > 0.0f);
        CHECK(settings.farPlane > settings.nearPlane * 100.0f);
        CHECK(settings.verticalFovDegrees > 30.0f && settings.verticalFovDegrees < 110.0f);
        CHECK(settings.shadowDistance > 20.0f);
        CHECK(settings.shadowCascades >= 1 && settings.shadowCascades <= 4);
        CHECK(settings.sunElevationDegrees > 0.0f && settings.sunElevationDegrees < 90.0f);
        CHECK(settings.exposure > 0.0f);
    }

    CASE("known values are applied");
    {
        RenderSettings settings;
        std::string error;
        const int applied = settings.applyJson(
            R"({"exposure": 0.75, "shadows": false, "shadowCascades": 2,
                "sunElevationDegrees": 22.5, "seed": 99})",
            error);
        CHECK_MSG(applied >= 5, "expected five values to be applied, got "
                                    + std::to_string(applied) + " (" + error + ")");
        CHECK_NEAR(settings.exposure, 0.75, 1e-6);
        CHECK(!settings.shadows);
        CHECK(settings.shadowCascades == 2);
        CHECK_NEAR(settings.sunElevationDegrees, 22.5, 1e-4);
        CHECK(settings.seed == 99u);
    }

    CASE("an unknown key is survivable");
    {
        RenderSettings settings;
        std::string error;
        const int applied = settings.applyJson(
            R"({"exposure": 0.5, "raytracedCaustics": true})", error);
        CHECK_MSG(applied >= 1, "a document with one unknown key applied nothing");
        CHECK_NEAR(settings.exposure, 0.5, 1e-6);
    }

    CASE("a malformed document is refused, and changes nothing");
    {
        RenderSettings settings;
        const float before = settings.exposure;
        std::string error;
        CHECK(settings.applyJson("{ this is not json", error) == -1);
        CHECK_MSG(!error.empty(), "a refused document gave no reason");
        CHECK_NEAR(settings.exposure, before, 0.0);
    }

    CASE("the presets are ordered, and each is internally consistent");
    {
        RenderSettings low, medium, high, ultra;
        low.applyPreset(QualityPreset::Low);
        medium.applyPreset(QualityPreset::Medium);
        high.applyPreset(QualityPreset::High);
        ultra.applyPreset(QualityPreset::Ultra);

        CHECK(low.shadowQuality <= medium.shadowQuality);
        CHECK(medium.shadowQuality <= high.shadowQuality);
        CHECK(high.shadowQuality <= ultra.shadowQuality);
        CHECK(low.shadowDistance <= ultra.shadowDistance);
        CHECK(low.propCullDistance <= ultra.propCullDistance);
        CHECK(!low.ssao);
        CHECK(ultra.ssao && ultra.bloom);

        for (const RenderSettings* preset : {&low, &medium, &high, &ultra})
        {
            CHECK(preset->shadowCascades >= 1 && preset->shadowCascades <= 4);
            CHECK(preset->renderScale > 0.3f && preset->renderScale <= 2.0f);
            CHECK(preset->multiSample >= 0 && preset->multiSample <= 8);   // 0 is off
            CHECK(preset->exposure > 0.0f);
        }
    }

    CASE("a preset leaves the seed and the camera alone");
    {
        // Changing quality must not change which street you are standing in, or
        // the screenshot comparison is meaningless.
        RenderSettings settings;
        settings.seed = 4242u;
        settings.verticalFovDegrees = 71.0f;
        settings.mouseSensitivity = 0.0031f;
        settings.applyPreset(QualityPreset::Low);
        CHECK(settings.seed == 4242u);
        CHECK_NEAR(settings.verticalFovDegrees, 71.0, 1e-5);
        CHECK_NEAR(settings.mouseSensitivity, 0.0031, 1e-9);
    }

    CASE("every preset has a name");
    {
        for (const QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium,
                                           QualityPreset::High, QualityPreset::Ultra})
        {
            const char* name = RenderSettings::presetName(preset);
            CHECK(name != nullptr && name[0] != '\0');
        }
    }

    TEST_MAIN("settings");
}
