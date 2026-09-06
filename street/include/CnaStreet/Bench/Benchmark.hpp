// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/CameraController.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CnaStreet {

/**
 * @brief One reproducible workload: a camera, a sun, and how long to measure.
 *
 * The street is a real scene and not a test pattern, which is what makes it
 * useful for finding where the framework's renderer stops scaling; a
 * benchmark of it has to be *the same scene* every time or the numbers say
 * nothing. A preset fixes everything a frame's cost depends on that the
 * settings do not: where the camera stands and looks, where the sun is, how
 * many frames to throw away while the driver warms up and how many to
 * measure. The clock runs at a fixed step under a preset, so frame N of a
 * run is the same traffic and the same crowd as frame N of the last run.
 *
 * The cameras are copied from the named viewpoints rather than read from
 * them on purpose: a viewpoint is a picture somebody composes and moves when
 * the composition wants it, and a benchmark camera that moved with it would
 * quietly change what every earlier result measured.
 */
struct BenchmarkPreset
{
    const char* name = "";
    /// One line on what the workload stresses.
    const char* what = "";
    Viewpoint camera;
    /// The sun to run under, or elevation <= -90 to keep the settings' own.
    float sunElevationDegrees = -180.0f;
    float sunAzimuthDegrees   = 0.0f;
    int warmupFrames   = 12;
    int measuredFrames = 60;
    /// A workload that does not resemble a view a person would take of the
    /// street, kept for what it isolates. None of the shipped presets is.
    bool diagnostic = false;
};

/// The shipped presets, in the order `--benchmark-list` prints them.
[[nodiscard]] const std::vector<BenchmarkPreset>& benchmarkPresets();
/// The preset called @p name, or null.
[[nodiscard]] const BenchmarkPreset* findBenchmarkPreset(std::string_view name);
/// "baseline, shadow, ..." for a usage message.
[[nodiscard]] std::string benchmarkPresetNames();

/**
 * @brief What one benchmark run measured, in the form a comparison wants.
 *
 * Every number is a per-frame mean over the measured frames unless it says
 * otherwise. GPU times are the renderer's own timer queries and -1 where the
 * renderer has none. `loadAverage` is the machine's one-minute load when the
 * run started, because on a shared machine it is the first thing to look at
 * when two runs of the same build disagree.
 */
struct BenchmarkResult
{
    std::string preset;
    std::string what;
    std::string version;
    std::string renderer;
    std::string adapter;
    std::string content;   ///< "compiled" or "generated"
    int width = 0, height = 0;
    std::uint32_t seed = 0;
    int warmupFrames = 0, measuredFrames = 0;
    double loadAverage = -1.0;

    double cpuMeanMs = 0.0, cpuMedianMs = 0.0, cpuP95Ms = 0.0, cpuMinMs = 0.0, cpuMaxMs = 0.0;
    double cullMs = 0.0, shadowMs = 0.0, prepassMs = 0.0, skyMs = 0.0, opaqueMs = 0.0, postMs = 0.0;
    double opaqueApplyMs = -1.0, opaqueDrawMs = -1.0, skinnedMs = -1.0;

    double gpuFrameMs = -1.0;
    double gpuShadowMs = -1.0, gpuPrepassMs = -1.0, gpuSkyMs = -1.0, gpuOpaqueMs = -1.0,
           gpuPostMs = -1.0;
    std::vector<std::pair<std::string, double>> gpuPostPasses;

    double draws = 0.0, shadowDraws = 0.0, instancedDraws = 0.0, skinnedDraws = 0.0;
    double triangles = 0.0, shadowTriangles = 0.0;
    double materialApplies = 0.0, repeatedMaterialApplies = 0.0;
    struct Cascade
    {
        double draws = 0.0, triangles = 0.0;
        float  split = 0.0f;
    };
    std::vector<Cascade> cascades;
    double visibleCharacters = 0.0, vehicleDraws = 0.0, driverDraws = 0.0;
    double characterShadowDraws = 0.0;

    int staticBatches = 0, instanceGroups = 0, instances = 0;
    std::size_t meshBytes = 0, textureBytes = 0;
};

/// One JSON object on one line. Flat, so `jq`, a spreadsheet import or a
/// twenty-line script can read it; the per-pass and per-cascade lists are
/// nested one level.
[[nodiscard]] std::string benchmarkToJson(const BenchmarkResult& result);
/// The CSV column line, and one row. The columns are the flat fields in a
/// fixed order plus the post passes and cascades by name, so a file of rows
/// from different builds lines up as long as the pipeline has the same passes.
[[nodiscard]] std::string benchmarkCsvHeader(const BenchmarkResult& shape);
[[nodiscard]] std::string benchmarkToCsv(const BenchmarkResult& result);
/// Appends @p result to @p path: a `.csv` gets a header when the file is new
/// and a row otherwise, anything else gets one JSON object per line. Returns
/// false and fills @p error when the file cannot be written.
bool appendBenchmarkResult(const std::string& path, const BenchmarkResult& result,
                           std::string& error);

}  // namespace CnaStreet
