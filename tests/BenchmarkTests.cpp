// SPDX-License-Identifier: MIT
/**
 * @file BenchmarkTests.cpp
 * @brief The benchmark presets and the result writers.
 *
 * A benchmark is only worth running if it is the same benchmark tomorrow, so
 * the presets are checked for the things that would silently make it a
 * different one: a name that appears twice, a camera that is not a place, a
 * measurement window too short to settle. The writers are checked by reading
 * their output back: the JSON through the same System.Text.Json the settings
 * file goes through, the CSV by counting columns against its own header.
 */
#include "CnaStreet/Bench/Benchmark.hpp"

#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include "TestSupport.hpp"

#include <cmath>
#include <set>
#include <sstream>
#include <string>

using namespace CnaStreet;
using namespace CnaStreet::Test;
using System::Text::Json::JsonDocument;
using System::Text::Json::JsonElement;
using System::Text::Json::JsonValueKind;

namespace {

int Columns(const std::string& line)
{
    int columns = 1;
    bool quoted = false;
    for (const char c : line)
    {
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) ++columns;
    }
    return columns;
}

BenchmarkResult Sample()
{
    BenchmarkResult r;
    r.preset = "baseline";
    r.what = "a \"quoted\" description, with a comma";
    r.version = "0.1.0";
    r.renderer = "OPENGL33";
    r.adapter = "Dell Inc. 27\"";
    r.gpu = "AMD Radeon 780M";
    r.content = "compiled";
    r.width = 1600; r.height = 900; r.seed = 20260903u;
    r.warmupFrames = 12; r.measuredFrames = 60;
    r.loadAverage = 9.5;
    r.cpuMeanMs = 48.75; r.cpuMedianMs = 48.0; r.cpuP95Ms = 55.0; r.cpuMinMs = 44.0; r.cpuMaxMs = 61.0;
    r.gpuFrameMs = 40.1; r.gpuShadowMs = 9.8; r.gpuOpaqueMs = 20.0; r.gpuPostMs = 7.4;
    r.gpuPostPasses = {{"SSAO", 2.4}, {"LightShafts", 2.1}, {"Bloom", 0.8}};
    r.draws = 1436; r.shadowDraws = 1892; r.triangles = 7550000; r.shadowTriangles = 6500000;
    r.cascades = {{152, 580000, 7.3f}, {276, 960000, 17.4f}, {550, 1860000, 45.9f}, {1030, 3090000, 190.0f}};
    r.staticBatches = 1817; r.meshBytes = 95u << 20;
    return r;
}

}  // namespace

int main()
{
    beginCase("the preset table has the six workloads, each once, each a place");
    {
        const auto& presets = benchmarkPresets();
        CHECK(presets.size() >= 6);
        std::set<std::string> names;
        for (const BenchmarkPreset& preset : presets)
        {
            CHECK(names.insert(preset.name).second);
            CHECK(std::string(preset.what).size() > 10);
            CHECK(std::isfinite(preset.camera.position.X) && std::isfinite(preset.camera.position.Y)
                  && std::isfinite(preset.camera.position.Z));
            // Above the ground and below the aircraft: a camera at eye height
            // or looking down from a window, never under the road.
            CHECK(preset.camera.position.Y > 0.5f && preset.camera.position.Y < 120.0f);
            CHECK(preset.camera.fov > 0.3f && preset.camera.fov < 2.0f);
            CHECK(preset.warmupFrames >= 6);
            CHECK(preset.measuredFrames >= 30);
            if (preset.sunElevationDegrees > -90.0f)
                CHECK(preset.sunElevationDegrees > 5.0f && preset.sunElevationDegrees < 85.0f);
        }
        for (const char* wanted : {"baseline", "shadow", "traffic", "crowd", "city", "post"})
            CHECK(findBenchmarkPreset(wanted) != nullptr);
        CHECK(findBenchmarkPreset("nonesuch") == nullptr);
        CHECK(benchmarkPresetNames().find("baseline") != std::string::npos);
    }

    beginCase("the JSON line parses back with the numbers it was given");
    {
        const BenchmarkResult sample = Sample();
        const std::string json = benchmarkToJson(sample);
        CHECK(json.find('\n') == std::string::npos);
        std::shared_ptr<JsonDocument> document;
        bool parsed = true;
        try { document = JsonDocument::Parse(json); }
        catch (...) { parsed = false; }
        CHECK(parsed);
        if (parsed)
        {
            const JsonElement root = document->getRootElementProperty();
            CHECK(root.getValueKindProperty() == JsonValueKind::Object);
            JsonElement value;
            CHECK(root.TryGetProperty("preset", value) && value.GetString() == "baseline");
            CHECK(root.TryGetProperty("what", value)
                  && value.GetString() == "a \"quoted\" description, with a comma");
            CHECK(root.TryGetProperty("draws", value) && std::fabs(value.GetDouble() - 1436.0) < 0.01);
            CHECK(root.TryGetProperty("gpuShadowMs", value) && std::fabs(value.GetDouble() - 9.8) < 0.001);
            CHECK(root.TryGetProperty("cascades", value)
                  && value.getValueKindProperty() == JsonValueKind::Array
                  && value.GetArrayLength() == 4);
            CHECK(root.TryGetProperty("gpuPostPasses", value)
                  && value.getValueKindProperty() == JsonValueKind::Object);
            JsonElement ssao;
            CHECK(value.TryGetProperty("SSAO", ssao) && std::fabs(ssao.GetDouble() - 2.4) < 0.001);
            // A renderer without timers writes -1, not nothing, so a column
            // is never missing.
            CHECK(root.TryGetProperty("gpuSkyMs", value) && value.GetDouble() < 0.0);
        }
    }

    beginCase("the CSV row has as many columns as its header, quotes and commas included");
    {
        const BenchmarkResult sample = Sample();
        const std::string header = benchmarkCsvHeader(sample);
        const std::string row = benchmarkToCsv(sample);
        CHECK(Columns(header) == Columns(row));
        CHECK(header.find("gpuPost_ssaoMs") != std::string::npos);
        CHECK(header.find("cascade3Triangles") != std::string::npos);
        CHECK(row.find("\"AMD Radeon 780M\"") != std::string::npos);
        note("columns: " + std::to_string(Columns(header)));
    }

    return summary("benchmark_tests");
}
