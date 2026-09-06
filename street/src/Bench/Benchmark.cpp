// SPDX-License-Identifier: MIT
#include "CnaStreet/Bench/Benchmark.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using Microsoft::Xna::Framework::Vector3;

namespace CnaStreet {

namespace {

// Eye height on the footway: kerb plus a standing adult, the same figure the
// viewpoints stand at. Repeated here rather than read from StreetMetrics so a
// change to the street's kerb cannot move a benchmark camera.
constexpr float kEye = 0.14f + 1.66f;
constexpr float kFov = 1.09955743f;   // 63 degrees vertical
constexpr float kSouth = 0.0f, kEast = 1.5707963f, kNorth = 3.1415927f;

const std::vector<BenchmarkPreset>& Presets()
{
    static const std::vector<BenchmarkPreset> presets = {
        {"baseline",
         "the representative view: the footway looking south to the junction, everything on",
         Viewpoint{"baseline", Vector3(-7.4f, kEye, 46.0f), kSouth, -0.035f, kFov}},
        {"shadow",
         "the long view south under a low sun: every cascade full, every caster's shadow long",
         Viewpoint{"shadow", Vector3(-7.1f, kEye + 0.30f, 104.0f), kSouth - 0.035f, -0.012f, 0.95f},
         28.0f, 250.0f},
        {"traffic",
         "on the centre line north of the junction, the traffic coming and going with its drivers",
         Viewpoint{"traffic", Vector3(0.0f, kEye + 1.8f, -22.0f), kNorth, -0.06f, kFov}},
        {"crowd",
         "on the crossing among the people: the skinned crowd at its densest",
         Viewpoint{"crowd", Vector3(0.0f, kEye, 15.5f), kSouth + 0.02f, 0.02f, kFov}},
        {"city",
         "above the junction: the district to the skyline, the long-distance architecture",
         Viewpoint{"city", Vector3(3.0f, 44.0f, 62.0f), 0.03f, -0.60f, kFov}},
        {"post",
         "looking up at the facades and the sky: few draws, the post chain at its most visible",
         Viewpoint{"post", Vector3(-6.6f, kEye, -16.0f), kEast, 0.60f, 1.22f}},
    };
    return presets;
}

std::string Escape(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string Number(double value, int places = 3)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(places) << value;
    return out.str();
}

/// A short, file-safe key for a pass or a cascade column.
std::string Key(const std::string& name)
{
    std::string out;
    for (const char c : name)
        out.push_back(std::isalnum(static_cast<unsigned char>(c))
                          ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                          : '_');
    return out;
}

}  // namespace

const std::vector<BenchmarkPreset>& benchmarkPresets() { return Presets(); }

const BenchmarkPreset* findBenchmarkPreset(std::string_view name)
{
    for (const BenchmarkPreset& preset : Presets())
        if (name == preset.name) return &preset;
    return nullptr;
}

std::string benchmarkPresetNames()
{
    std::string names;
    for (const BenchmarkPreset& preset : Presets())
    {
        if (!names.empty()) names += ", ";
        names += preset.name;
    }
    return names;
}

std::string benchmarkToJson(const BenchmarkResult& r)
{
    std::ostringstream out;
    out << '{';
    const auto str = [&](const char* key, const std::string& value, bool first = false) {
        out << (first ? "" : ", ") << '"' << key << "\": \"" << Escape(value) << '"';
    };
    const auto num = [&](const char* key, double value, int places = 3) {
        out << ", \"" << key << "\": " << Number(value, places);
    };
    const auto integer = [&](const char* key, long long value) {
        out << ", \"" << key << "\": " << value;
    };
    str("preset", r.preset, true);
    str("what", r.what);
    str("version", r.version);
    str("renderer", r.renderer);
    str("adapter", r.adapter);
    str("gpu", r.gpu);
    str("content", r.content);
    integer("width", r.width);
    integer("height", r.height);
    integer("seed", static_cast<long long>(r.seed));
    integer("warmupFrames", r.warmupFrames);
    integer("measuredFrames", r.measuredFrames);
    num("loadAverage", r.loadAverage, 2);
    num("cpuMeanMs", r.cpuMeanMs);
    num("cpuMedianMs", r.cpuMedianMs);
    num("cpuP95Ms", r.cpuP95Ms);
    num("cpuMinMs", r.cpuMinMs);
    num("cpuMaxMs", r.cpuMaxMs);
    num("fps", r.cpuMeanMs > 0.0 ? 1000.0 / r.cpuMeanMs : 0.0, 2);
    num("cullMs", r.cullMs);
    num("shadowMs", r.shadowMs);
    num("prepassMs", r.prepassMs);
    num("skyMs", r.skyMs);
    num("opaqueMs", r.opaqueMs);
    num("postMs", r.postMs);
    num("opaqueApplyMs", r.opaqueApplyMs);
    num("opaqueDrawMs", r.opaqueDrawMs);
    num("skinnedMs", r.skinnedMs);
    num("gpuFrameMs", r.gpuFrameMs);
    num("gpuShadowMs", r.gpuShadowMs);
    num("gpuPrepassMs", r.gpuPrepassMs);
    num("gpuSkyMs", r.gpuSkyMs);
    num("gpuOpaqueMs", r.gpuOpaqueMs);
    num("gpuPostMs", r.gpuPostMs);
    out << ", \"gpuPostPasses\": {";
    for (std::size_t i = 0; i < r.gpuPostPasses.size(); ++i)
        out << (i ? ", " : "") << '"' << Escape(r.gpuPostPasses[i].first) << "\": "
            << Number(r.gpuPostPasses[i].second);
    out << '}';
    num("draws", r.draws, 1);
    num("shadowDraws", r.shadowDraws, 1);
    num("instancedDraws", r.instancedDraws, 1);
    num("skinnedDraws", r.skinnedDraws, 1);
    num("triangles", r.triangles, 0);
    num("shadowTriangles", r.shadowTriangles, 0);
    num("materialApplies", r.materialApplies, 1);
    num("repeatedMaterialApplies", r.repeatedMaterialApplies, 1);
    out << ", \"cascades\": [";
    for (std::size_t i = 0; i < r.cascades.size(); ++i)
        out << (i ? ", " : "") << "{\"split\": " << Number(r.cascades[i].split, 1)
            << ", \"draws\": " << Number(r.cascades[i].draws, 1)
            << ", \"triangles\": " << Number(r.cascades[i].triangles, 0) << '}';
    out << ']';
    num("visibleCharacters", r.visibleCharacters, 1);
    num("vehicleDraws", r.vehicleDraws, 1);
    num("driverDraws", r.driverDraws, 1);
    num("characterShadowDraws", r.characterShadowDraws, 1);
    integer("staticBatches", r.staticBatches);
    integer("instanceGroups", r.instanceGroups);
    integer("instances", r.instances);
    integer("meshBytes", static_cast<long long>(r.meshBytes));
    integer("textureBytes", static_cast<long long>(r.textureBytes));
    out << '}';
    return out.str();
}

std::string benchmarkCsvHeader(const BenchmarkResult& shape)
{
    std::string header =
        "preset,version,renderer,adapter,gpu,content,width,height,seed,warmupFrames,measuredFrames,"
        "loadAverage,cpuMeanMs,cpuMedianMs,cpuP95Ms,cpuMinMs,cpuMaxMs,fps,"
        "cullMs,shadowMs,prepassMs,skyMs,opaqueMs,postMs,opaqueApplyMs,opaqueDrawMs,skinnedMs,"
        "gpuFrameMs,gpuShadowMs,gpuPrepassMs,gpuSkyMs,gpuOpaqueMs,gpuPostMs,"
        "draws,shadowDraws,instancedDraws,skinnedDraws,triangles,shadowTriangles,"
        "materialApplies,repeatedMaterialApplies,visibleCharacters,vehicleDraws,driverDraws,"
        "characterShadowDraws,staticBatches,instanceGroups,instances,meshBytes,textureBytes";
    for (const auto& pass : shape.gpuPostPasses) header += ",gpuPost_" + Key(pass.first) + "Ms";
    for (std::size_t i = 0; i < shape.cascades.size(); ++i)
    {
        header += ",cascade" + std::to_string(i) + "Draws";
        header += ",cascade" + std::to_string(i) + "Triangles";
    }
    return header;
}

std::string benchmarkToCsv(const BenchmarkResult& r)
{
    std::ostringstream out;
    const auto quoted = [](const std::string& s) {
        std::string q = "\"";
        for (const char c : s) { if (c == '"') q += '"'; q.push_back(c); }
        return q + "\"";
    };
    out << quoted(r.preset) << ',' << quoted(r.version) << ',' << quoted(r.renderer) << ','
        << quoted(r.adapter) << ',' << quoted(r.gpu) << ',' << quoted(r.content) << ',' << r.width
        << ',' << r.height << ','
        << r.seed << ',' << r.warmupFrames << ',' << r.measuredFrames << ','
        << Number(r.loadAverage, 2) << ',' << Number(r.cpuMeanMs) << ',' << Number(r.cpuMedianMs)
        << ',' << Number(r.cpuP95Ms) << ',' << Number(r.cpuMinMs) << ',' << Number(r.cpuMaxMs)
        << ',' << Number(r.cpuMeanMs > 0.0 ? 1000.0 / r.cpuMeanMs : 0.0, 2) << ','
        << Number(r.cullMs) << ',' << Number(r.shadowMs) << ',' << Number(r.prepassMs) << ','
        << Number(r.skyMs) << ',' << Number(r.opaqueMs) << ',' << Number(r.postMs) << ','
        << Number(r.opaqueApplyMs) << ',' << Number(r.opaqueDrawMs) << ',' << Number(r.skinnedMs)
        << ',' << Number(r.gpuFrameMs) << ',' << Number(r.gpuShadowMs) << ','
        << Number(r.gpuPrepassMs) << ',' << Number(r.gpuSkyMs) << ',' << Number(r.gpuOpaqueMs)
        << ',' << Number(r.gpuPostMs) << ',' << Number(r.draws, 1) << ','
        << Number(r.shadowDraws, 1) << ',' << Number(r.instancedDraws, 1) << ','
        << Number(r.skinnedDraws, 1) << ',' << Number(r.triangles, 0) << ','
        << Number(r.shadowTriangles, 0) << ',' << Number(r.materialApplies, 1) << ','
        << Number(r.repeatedMaterialApplies, 1) << ',' << Number(r.visibleCharacters, 1) << ','
        << Number(r.vehicleDraws, 1) << ',' << Number(r.driverDraws, 1) << ','
        << Number(r.characterShadowDraws, 1) << ',' << r.staticBatches << ','
        << r.instanceGroups << ',' << r.instances << ',' << r.meshBytes << ',' << r.textureBytes;
    for (const auto& pass : r.gpuPostPasses) out << ',' << Number(pass.second);
    for (const BenchmarkResult::Cascade& cascade : r.cascades)
        out << ',' << Number(cascade.draws, 1) << ',' << Number(cascade.triangles, 0);
    return out.str();
}

bool appendBenchmarkResult(const std::string& path, const BenchmarkResult& result,
                           std::string& error)
{
    const std::filesystem::path file(path);
    if (file.has_parent_path())
    {
        std::error_code code;
        std::filesystem::create_directories(file.parent_path(), code);
    }
    const bool csv = file.extension() == ".csv";
    std::error_code sizeError;
    const bool fresh = !std::filesystem::exists(file, sizeError)
                       || std::filesystem::file_size(file, sizeError) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out)
    {
        error = "could not open '" + path + "' for writing";
        return false;
    }
    if (csv)
    {
        if (fresh) out << benchmarkCsvHeader(result) << '\n';
        out << benchmarkToCsv(result) << '\n';
    }
    else
        out << benchmarkToJson(result) << '\n';
    return static_cast<bool>(out);
}

}  // namespace CnaStreet
