// SPDX-License-Identifier: MIT
/**
 * @file
 * @brief The contract between scripts/blender-people.py and CharacterLibrary.
 *
 * The people in the crowd are written by a Blender script in this project's
 * own format and read back by `CharacterLibrary`, and the two agree on things
 * a screenshot cannot check: that the skeleton is the nineteen bones
 * `CharacterFactory` names, in an order a parent always precedes its child;
 * that the joints sit where a standing figure's do, with the arms *down* --
 * MakeHuman's rest pose is an A-pose, and a clip that swings a hanging arm
 * applied to a raised one puts the hands out at the shoulders; that every part
 * points inside its binary and every vertex is a unit-normal, unit-tangent,
 * weights-summing-to-one vertex with bone indices under nineteen. The first
 * export got the last two wrong in ways that drew perfectly plausible garbage.
 *
 * Reads the derived files where they exist and passes with a note where they
 * do not: a tree that has not run Blender has no people, and that is a
 * supported state, not a failure.
 */
#include "CnaStreet/Props/CharacterFactory.hpp"

#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include "TestSupport.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace CnaStreet::Test;
using System::Text::Json::JsonDocument;
using System::Text::Json::JsonElement;
using System::Text::Json::JsonValueKind;

#define CASE(name) beginCase(name)
#define CHECK(cond) check((cond), __FILE__, __LINE__, #cond)

namespace {

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

double Number(const JsonElement& e, const char* name)
{
    JsonElement v;
    if (!e.TryGetProperty(name, v) || v.getValueKindProperty() != JsonValueKind::Number) return -1.0;
    return v.GetDouble();
}

std::string Text(const JsonElement& e, const char* name)
{
    JsonElement v;
    if (!e.TryGetProperty(name, v) || v.getValueKindProperty() != JsonValueKind::String) return {};
    return v.GetString();
}

}  // namespace

int main()
{
    const std::filesystem::path people =
        std::filesystem::path(CNA_STREET_DEFAULT_ASSET_DIR) / "external" / "downloads" / "derived"
        / "people";
    std::vector<std::filesystem::path> headers;
    if (std::filesystem::is_directory(people))
        for (const auto& entry : std::filesystem::directory_iterator(people))
            if (entry.path().extension() == ".json") headers.push_back(entry.path());
    if (headers.empty())
    {
        std::printf("  no derived people under %s; nothing to check\n", people.string().c_str());
        return summary("character_format_tests");
    }

    // The bones the clip builder looks up, by name. Every one must be there.
    const std::vector<std::string> expected = {
        "pelvis", "spine", "chest", "neck", "head",
        "clavicle.R", "upperarm.R", "forearm.R", "hand.R",
        "clavicle.L", "upperarm.L", "forearm.L", "hand.L",
        "thigh.R", "shin.R", "foot.R", "thigh.L", "shin.L", "foot.L"};

    for (const std::filesystem::path& header : headers)
    {
        CASE(header.filename().string().c_str());
        std::shared_ptr<JsonDocument> document = JsonDocument::Parse(ReadFile(header));
        const JsonElement root = document->getRootElementProperty();

        const double height = Number(root, "height");
        CHECK(height > 1.30 && height < 2.10);

        // --- the skeleton ------------------------------------------------------
        std::map<std::string, int> index;
        std::map<std::string, std::array<double, 3>> head;
        std::vector<std::string> order;
        JsonElement bones;
        CHECK(root.TryGetProperty("bones", bones));
        for (const JsonElement& bone : bones.EnumerateArray())
        {
            const std::string name = Text(bone, "name");
            const std::string parent = Text(bone, "parent");
            // A parent always comes first: the skinning data is consumed in
            // this order and a child before its parent is a matrix read from
            // uninitialised memory.
            CHECK(parent.empty() || index.count(parent) == 1);
            index[name] = static_cast<int>(order.size());
            order.push_back(name);
            JsonElement at;
            std::array<double, 3> p{0.0, 0.0, 0.0};
            if (bone.TryGetProperty("head", at))
            {
                const std::vector<JsonElement> items = at.EnumerateArray();
                for (std::size_t i = 0; i < 3 && i < items.size(); ++i) p[i] = items[i].GetDouble();
            }
            head[name] = p;
        }
        CHECK(order.size() == 19);
        for (const std::string& name : expected) CHECK(index.count(name) == 1);
        if (order.size() != 19) continue;

        // Standing: the spine rises, the feet are on the ground, the hands
        // hang below the elbows and the elbows below the shoulders -- the
        // arms are down, which is the whole point of the pose step.
        CHECK(head["pelvis"][1] < head["spine"][1]);
        CHECK(head["spine"][1] < head["chest"][1]);
        CHECK(head["chest"][1] < head["neck"][1]);
        CHECK(head["neck"][1] < head["head"][1]);
        for (const char* side : {".R", ".L"})
        {
            const std::string s(side);
            CHECK(head["foot" + s][1] < 0.20);
            CHECK(head["shin" + s][1] > head["foot" + s][1]);
            CHECK(head["thigh" + s][1] > head["shin" + s][1]);
            CHECK(head["upperarm" + s][1] > head["forearm" + s][1]);
            CHECK(head["forearm" + s][1] > head["hand" + s][1]);
            // A hanging hand is near the hip, not out at shoulder width.
            CHECK(std::fabs(head["hand" + s][0]) < std::fabs(head["upperarm" + s][0]) + 0.16);
        }
        // Sides: ".R" is the +x side here, as it is in CharacterFactory.
        CHECK(head["upperarm.R"][0] > 0.0 && head["upperarm.L"][0] < 0.0);
        CHECK(head["thigh.R"][0] > 0.0 && head["thigh.L"][0] < 0.0);

        // --- the parts --------------------------------------------------------
        for (const char* level : {"near", "far"})
        {
            JsonElement element;
            CHECK(root.TryGetProperty(level, element));
            if (!root.TryGetProperty(level, element)) continue;
            const std::string binary = ReadFile(header.parent_path() / Text(element, "file"));
            CHECK(!binary.empty());
            JsonElement parts;
            CHECK(element.TryGetProperty("parts", parts));
            int bodies = 0;
            for (const JsonElement& part : parts.EnumerateArray())
            {
                const auto vo = static_cast<std::size_t>(Number(part, "vertexOffset"));
                const auto vc = static_cast<std::size_t>(Number(part, "vertexCount"));
                const auto io = static_cast<std::size_t>(Number(part, "indexOffset"));
                const auto ic = static_cast<std::size_t>(Number(part, "indexCount"));
                CHECK(vc > 0 && ic >= 3 && ic % 3 == 0);
                CHECK(vo + vc * 68 <= binary.size());
                CHECK(io + ic * 4 <= binary.size());
                if (vo + vc * 68 > binary.size() || io + ic * 4 > binary.size()) continue;
                if (Text(part, "kind") == "body") ++bodies;
                CHECK(!Text(part, "albedo").empty());

                // Every index inside the part, every vertex well formed.
                std::size_t badIndex = 0, badNormal = 0, badTangent = 0, badWeight = 0,
                            badBone = 0, badUv = 0;
                for (std::size_t i = 0; i < ic; ++i)
                {
                    std::uint32_t value;
                    std::memcpy(&value, binary.data() + io + i * 4, 4);
                    if (value >= vc) ++badIndex;
                }
                for (std::size_t v = 0; v < vc; ++v)
                {
                    float f[17];
                    std::memcpy(f, binary.data() + vo + v * 68, sizeof(f));
                    const double n = std::sqrt(f[3] * f[3] + f[4] * f[4] + f[5] * f[5]);
                    const double t = std::sqrt(f[6] * f[6] + f[7] * f[7] + f[8] * f[8]);
                    if (std::fabs(n - 1.0) > 0.02) ++badNormal;
                    if (std::fabs(t - 1.0) > 0.02 || std::fabs(f[9]) != 1.0f) ++badTangent;
                    if (f[10] < -0.01f || f[10] > 1.01f || f[11] < -0.01f || f[11] > 1.01f) ++badUv;
                    const double w = f[12] + f[13] + f[14] + f[15];
                    if (std::fabs(w - 1.0) > 0.01) ++badWeight;
                    std::uint8_t bones4[4];
                    std::memcpy(bones4, &f[16], 4);
                    for (int k = 0; k < 4; ++k)
                        if (bones4[k] >= 19) ++badBone;
                }
                CHECK(badIndex == 0);
                CHECK(badNormal == 0);
                CHECK(badTangent == 0);
                CHECK(badWeight == 0);
                CHECK(badBone == 0);
                // The UVs are what went wrong first: a stale layer reference
                // in Blender read another array as the UVs and half of every
                // garment sampled outside the texture.
                CHECK(badUv == 0);
            }
            CHECK(bodies == 1);
        }
    }
    return summary("character_format_tests");
}
