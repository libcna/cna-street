// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/RenderSettings.hpp"

#include "CNA/Logger.hpp"
#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonProperty.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <unordered_map>
#include <variant>

using System::Text::Json::JsonDocument;
using System::Text::Json::JsonElement;
using System::Text::Json::JsonValueKind;

namespace CnaStreet {

const char* RenderSettings::presetName(QualityPreset preset)
{
    switch (preset)
    {
        case QualityPreset::Low:    return "low";
        case QualityPreset::Medium: return "medium";
        case QualityPreset::High:   return "high";
        case QualityPreset::Ultra:  return "ultra";
    }
    return "?";
}

void RenderSettings::applyPreset(QualityPreset preset)
{
    switch (preset)
    {
        case QualityPreset::Low:
            // Everything a software rasteriser or an old integrated GPU can
            // still run at an interactive rate. Shadows survive -- a street
            // without them stops being a street -- but at two coarse cascades,
            // which is the fewest CascadedShadowMap accepts.
            multiSample = 0;   renderScale = 0.72f;
            shadows = true;    shadowCascades = 2;  shadowQuality = 0;
            shadowDistance = 70.0f;  propShadowDistance = 34.0f;
            hdr = false;  bloom = false;  ssao = false;  fxaa = false;
            heightFog = true;  lightShafts = false;  ssr = false;  depthOfField = false;
            clouds = true;
            reflectionProbes = false;
            propCullDistance = 105.0f;
            pedestrianCullDistance = 70.0f;  pedestrianDetailDistance = 12.0f;
            break;
        case QualityPreset::Medium:
            multiSample = 0;   renderScale = 1.0f;
            shadows = true;    shadowCascades = 2;  shadowQuality = 1;
            shadowDistance = 120.0f; propShadowDistance = 52.0f;
            hdr = true;   bloom = true;   ssao = false;  fxaa = true;
            heightFog = true;  lightShafts = false;  ssr = false;  depthOfField = false;
            reflectionProbes = true;  probeFaceSize = 32;  probeSpacing = 32.0f;
            propCullDistance = 160.0f;
            pedestrianCullDistance = 100.0f;  pedestrianDetailDistance = 16.0f;
            break;
        case QualityPreset::High:
            multiSample = 4;   renderScale = 1.0f;
            shadows = true;    shadowCascades = 3;  shadowQuality = 2;
            shadowDistance = 175.0f; propShadowDistance = 74.0f;
            hdr = true;   bloom = true;   ssao = true;   fxaa = true;
            heightFog = true;  lightShafts = true;  ssr = false;  depthOfField = false;
            reflectionProbes = true;  probeFaceSize = 64;  probeSpacing = 24.0f;
            propCullDistance = 215.0f;
            pedestrianCullDistance = 130.0f;  pedestrianDetailDistance = 20.0f;
            break;
        case QualityPreset::Ultra:
            multiSample = 4;   renderScale = 1.0f;
            shadows = true;    shadowCascades = 4;  shadowQuality = 3;
            shadowDistance = 240.0f; propShadowDistance = 110.0f;
            hdr = true;   bloom = true;   ssao = true;   fxaa = true;
            heightFog = true;  lightShafts = true;  ssr = true;   depthOfField = false;
            ssaoIntensity = 0.85f;
            reflectionProbes = true;  probeFaceSize = 96;  probeSpacing = 18.0f;
            propCullDistance = 320.0f;
            pedestrianCullDistance = 185.0f;  pedestrianDetailDistance = 28.0f;
            break;
    }
}

namespace {

/// One settings key: where it lives and how to read it. A table rather than a
/// chain of `if (name == "...")` so that the JSON reader, the JSON writer and
/// the list of legal keys can never drift apart -- they are all this one table.
struct Binding
{
    using Target = std::variant<bool*, int*, float*, std::uint32_t*>;
    const char* name;
    Target      target;
};

std::vector<Binding> Bindings(RenderSettings& s)
{
    return {
        {"windowWidth", &s.windowWidth}, {"windowHeight", &s.windowHeight},
        {"fullscreen", &s.fullscreen},   {"vsync", &s.vsync},
        {"multiSample", &s.multiSample}, {"renderScale", &s.renderScale},

        {"verticalFovDegrees", &s.verticalFovDegrees},
        {"nearPlane", &s.nearPlane}, {"farPlane", &s.farPlane},
        {"mouseSensitivity", &s.mouseSensitivity}, {"moveSpeed", &s.moveSpeed},
        {"invertY", &s.invertY},

        {"shadows", &s.shadows}, {"shadowCascades", &s.shadowCascades},
        {"shadowQuality", &s.shadowQuality}, {"shadowDistance", &s.shadowDistance},
        {"shadowSplitLambda", &s.shadowSplitLambda},
        {"shadowDepthBias", &s.shadowDepthBias}, {"shadowBlendBand", &s.shadowBlendBand},
        {"shadowDebugTint", &s.shadowDebugTint},

        {"sunElevationDegrees", &s.sunElevationDegrees},
        {"sunAzimuthDegrees", &s.sunAzimuthDegrees},
        {"sunIntensity", &s.sunIntensity},
        {"imageBasedLighting", &s.imageBasedLighting},
        {"iblIntensity", &s.iblIntensity},
        {"skyTurbidity", &s.skyTurbidity}, {"skyIntensity", &s.skyIntensity},
        {"clouds", &s.clouds}, {"cloudCoverage", &s.cloudCoverage},
        {"cloudSpeed", &s.cloudSpeed},

        {"hdr", &s.hdr}, {"exposure", &s.exposure}, {"tonemap", &s.tonemap},
        {"bloom", &s.bloom}, {"bloomIntensity", &s.bloomIntensity},
        {"bloomThreshold", &s.bloomThreshold},
        {"ssao", &s.ssao}, {"ssaoRadius", &s.ssaoRadius}, {"ssaoIntensity", &s.ssaoIntensity},
        {"fxaa", &s.fxaa},
        {"heightFog", &s.heightFog}, {"fogDensity", &s.fogDensity},
        {"fogFalloff", &s.fogFalloff},
        {"lightShafts", &s.lightShafts}, {"ssr", &s.ssr}, {"depthOfField", &s.depthOfField},
        {"reflectionProbes", &s.reflectionProbes}, {"probeIrradiance", &s.probeIrradiance},
        {"probeSpacing", &s.probeSpacing}, {"probeFaceSize", &s.probeFaceSize},

        {"traffic", &s.traffic}, {"pedestrians", &s.pedestrians},
        {"vegetation", &s.vegetation}, {"streetFurniture", &s.streetFurniture},
        {"debugOverlay", &s.debugOverlay},
        {"propCullDistance", &s.propCullDistance},
        {"propShadowDistance", &s.propShadowDistance},
        {"pedestrianCullDistance", &s.pedestrianCullDistance},
        {"pedestrianDetailDistance", &s.pedestrianDetailDistance},

        {"seed", &s.seed},
    };
}

bool ApplyValue(const Binding::Target& target, const JsonElement& value, std::string& error)
{
    const JsonValueKind kind = value.getValueKindProperty();
    if (const auto* flag = std::get_if<bool*>(&target))
    {
        if (kind != JsonValueKind::True && kind != JsonValueKind::False)
        {
            error = "expected a boolean";
            return false;
        }
        **flag = value.GetBoolean();
        return true;
    }
    if (kind != JsonValueKind::Number)
    {
        error = "expected a number";
        return false;
    }
    if (const auto* integer = std::get_if<int*>(&target))
        **integer = static_cast<int>(std::lround(value.GetDouble()));
    else if (const auto* real = std::get_if<float*>(&target))
        **real = static_cast<float>(value.GetDouble());
    else if (const auto* unsignedInteger = std::get_if<std::uint32_t*>(&target))
        **unsignedInteger = static_cast<std::uint32_t>(std::max(0.0, value.GetDouble()));
    return true;
}

void WriteValue(std::ostringstream& out, const Binding::Target& target)
{
    if (const auto* flag = std::get_if<bool*>(&target))            out << (**flag ? "true" : "false");
    else if (const auto* integer = std::get_if<int*>(&target))     out << **integer;
    else if (const auto* real = std::get_if<float*>(&target))      out << **real;
    else if (const auto* u = std::get_if<std::uint32_t*>(&target)) out << **u;
}

}  // namespace

int RenderSettings::applyJson(const std::string& json, std::string& error)
{
    error.clear();
    std::shared_ptr<JsonDocument> document;
    try
    {
        document = JsonDocument::Parse(json);
    }
    catch (const std::exception& failure)
    {
        error = std::string("could not be parsed: ") + failure.what();
        return -1;
    }
    if (document == nullptr)
    {
        error = "could not be parsed";
        return -1;
    }

    const JsonElement root = document->getRootElementProperty();
    if (root.getValueKindProperty() != JsonValueKind::Object)
    {
        error = "the document root must be an object";
        return -1;
    }

    // A preset is applied first, so that individual keys in the same file
    // override it rather than the other way round -- which is the order anyone
    // writing `{"preset":"low","shadows":true}` expects.
    JsonElement preset;
    if (root.TryGetProperty("preset", preset)
        && preset.getValueKindProperty() == JsonValueKind::String)
    {
        const std::string name = preset.GetString();
        if (name == "low")         applyPreset(QualityPreset::Low);
        else if (name == "medium") applyPreset(QualityPreset::Medium);
        else if (name == "high")   applyPreset(QualityPreset::High);
        else if (name == "ultra")  applyPreset(QualityPreset::Ultra);
        else
            CNA::Logger::Warn("cna-street: settings name an unknown preset '" + name
                                 + "'; ignoring it");
    }

    const std::vector<Binding> bindings = Bindings(*this);
    std::unordered_map<std::string, Binding::Target> byName;
    byName.reserve(bindings.size());
    for (const Binding& binding : bindings) byName.emplace(binding.name, binding.target);

    int applied = 0;
    for (const auto& property : root.EnumerateObject())
    {
        const std::string name = property.getNameProperty();
        if (name == "preset") continue;
        const auto found = byName.find(name);
        if (found == byName.end())
        {
            // Ignored rather than fatal: a settings file written for a newer
            // build must still start this one.
            CNA::Logger::Warn("cna-street: settings key '" + name + "' is not recognised");
            continue;
        }
        std::string reason;
        if (!ApplyValue(found->second, property.getValueProperty(), reason))
            CNA::Logger::Warn("cna-street: settings key '" + name + "' " + reason);
        else
            ++applied;
    }
    return applied;
}

std::string RenderSettings::toJson() const
{
    // The bindings hold non-const pointers, so serialise through a copy rather
    // than casting away const on this.
    RenderSettings copy = *this;
    const std::vector<Binding> bindings = Bindings(copy);

    std::ostringstream out;
    out << "{\n";
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        out << "  \"" << bindings[i].name << "\": ";
        WriteValue(out, bindings[i].target);
        if (i + 1 < bindings.size()) out << ',';
        out << '\n';
    }
    out << "}\n";
    return out.str();
}

}  // namespace CnaStreet
