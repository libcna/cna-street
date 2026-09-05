// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/CharacterLibrary.hpp"

#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "System/Text/Json/JsonDocument.hpp"
#include "System/Text/Json/JsonElement.hpp"
#include "System/Text/Json/JsonValueKind.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using System::Text::Json::JsonDocument;
using System::Text::Json::JsonElement;
using System::Text::Json::JsonValueKind;

namespace CnaStreet {

namespace {

/// One vertex as the script writes it: seventeen little-endian floats, the
/// last of which carries the four bone indices as bytes. The same 68 bytes
/// as `VertexPositionNormalTangentTextureSkinned`, in the same order.
constexpr std::size_t kVertexBytes = 17 * sizeof(float);

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

float Number(const JsonElement& element, const char* name, float fallback)
{
    JsonElement value;
    if (!element.TryGetProperty(name, value)) return fallback;
    if (value.getValueKindProperty() != JsonValueKind::Number) return fallback;
    return static_cast<float>(value.GetDouble());
}

std::string Text(const JsonElement& element, const char* name)
{
    JsonElement value;
    if (!element.TryGetProperty(name, value)) return {};
    if (value.getValueKindProperty() != JsonValueKind::String) return {};
    return value.GetString();
}

bool Flag(const JsonElement& element, const char* name)
{
    JsonElement value;
    if (!element.TryGetProperty(name, value)) return false;
    return value.getValueKindProperty() == JsonValueKind::True;
}

Vector3 Triple(const JsonElement& element, const char* name, const Vector3& fallback)
{
    JsonElement value;
    if (!element.TryGetProperty(name, value)) return fallback;
    if (value.getValueKindProperty() != JsonValueKind::Array) return fallback;
    const std::vector<JsonElement> items = value.EnumerateArray();
    if (items.size() < 3) return fallback;
    return Vector3(static_cast<float>(items[0].GetDouble()),
                   static_cast<float>(items[1].GetDouble()),
                   static_cast<float>(items[2].GetDouble()));
}

/// Reads one level's parts out of @p binary into @p out. Returns what went
/// wrong, or an empty string.
std::string ReadLevel(const JsonElement& level, const std::string& binary,
                      const std::string& person, MaterialLibrary& materials,
                      std::vector<CharacterLibrary::Part>& out)
{
    JsonElement parts;
    if (!level.TryGetProperty("parts", parts)
        || parts.getValueKindProperty() != JsonValueKind::Array)
        return "no parts";
    int index = 0;
    for (const JsonElement& part : parts.EnumerateArray())
    {
        const std::size_t vertexOffset = static_cast<std::size_t>(Number(part, "vertexOffset", -1));
        const std::size_t vertexCount  = static_cast<std::size_t>(Number(part, "vertexCount", 0));
        const std::size_t indexOffset  = static_cast<std::size_t>(Number(part, "indexOffset", -1));
        const std::size_t indexCount   = static_cast<std::size_t>(Number(part, "indexCount", 0));
        if (vertexCount == 0 || indexCount < 3) continue;
        if (vertexOffset + vertexCount * kVertexBytes > binary.size()
            || indexOffset + indexCount * sizeof(std::uint32_t) > binary.size())
            return "a part points past the end of " + Text(level, "file");

        Material material;
        material.baseColour  = Triple(part, "baseColour", Vector3::One);
        material.roughness   = Number(part, "roughness", 0.8f);
        material.metallic    = Number(part, "metallic", 0.0f);
        material.doubleSided = Flag(part, "doubleSided");
        if (Text(part, "alphaMode") == "MASK")
        {
            material.alphaMode   = AlphaModeEXT::Mask;
            // Low, because MakeHuman's hair strips fade out along their
            // length and a cut at a half leaves a scalp with tufts.
            material.alphaCutoff = 0.28f;
        }
        const std::string albedo = Text(part, "albedo");
        const std::string normal = Text(part, "normal");
        const std::string kind   = Text(part, "kind");
        // The skin's own roughness map does not exist, so the factor is the
        // value; a face at four metres is a colour and a silhouette, and a
        // specular sheen on it is what says "plastic".
        const Material* installed = materials.addFromContent(
            person + "." + kind + "." + std::to_string(index), albedo, normal, material);
        ++index;
        if (installed == nullptr)
            return "texture '" + albedo + "' is not in the content root";

        CharacterLibrary::Part loaded;
        loaded.material = installed;
        loaded.kind     = kind;
        loaded.mesh.vertices.resize(vertexCount);
        const char* cursor = binary.data() + vertexOffset;
        for (std::size_t v = 0; v < vertexCount; ++v, cursor += kVertexBytes)
        {
            float f[17];
            std::memcpy(f, cursor, sizeof(f));
            Geometry::SkinnedVertex& vertex = loaded.mesh.vertices[v];
            vertex.Position          = Vector3(f[0], f[1], f[2]);
            vertex.Normal            = Vector3(f[3], f[4], f[5]);
            vertex.Tangent           = Vector4(f[6], f[7], f[8], f[9]);
            vertex.TextureCoordinate = Vector2(f[10], f[11]);
            vertex.BlendWeight       = Vector4(f[12], f[13], f[14], f[15]);
            std::memcpy(vertex.BlendIndices.data(), &f[16], 4);
        }
        loaded.mesh.indices.resize(indexCount);
        std::memcpy(loaded.mesh.indices.data(), binary.data() + indexOffset,
                    indexCount * sizeof(std::uint32_t));
        out.push_back(std::move(loaded));
    }
    return out.empty() ? "no drawable parts" : "";
}

}  // namespace

CharacterLibrary::CharacterLibrary(MaterialLibrary& materials) : materials_(materials) {}

void CharacterLibrary::setContentRoot(const std::string& root) { root_ = root; }

const CharacterLibrary::Person* CharacterLibrary::load(const std::string& name)
{
    const auto cached = cache_.find(name);
    if (cached != cache_.end()) return cached->second->empty() ? nullptr : cached->second.get();
    auto& slot = cache_[name];
    slot = std::make_unique<Person>();
    slot->name = name;
    if (root_.empty()) return nullptr;

    const std::filesystem::path base = std::filesystem::path(root_) / (name + ".json");
    const std::string json = ReadFile(base);
    if (json.empty()) return nullptr;   // not generated: the normal case

    std::string problem;
    try
    {
        std::shared_ptr<JsonDocument> document = JsonDocument::Parse(json);
        const JsonElement root = document->getRootElementProperty();
        slot->height = Number(root, "height", 1.75f);

        JsonElement bones;
        if (!root.TryGetProperty("bones", bones)
            || bones.getValueKindProperty() != JsonValueKind::Array)
            throw std::runtime_error("no bones");
        for (const JsonElement& bone : bones.EnumerateArray())
        {
            const std::string boneName = Text(bone, "name");
            const std::string parent   = Text(bone, "parent");
            const int parentIndex = parent.empty() ? -1 : slot->skeleton.find(parent);
            if (!parent.empty() && parentIndex < 0)
                throw std::runtime_error("bone '" + boneName + "' comes before its parent");
            slot->skeleton.add(boneName, parentIndex, Triple(bone, "head", Vector3::Zero), 0.1f);
        }
        if (slot->skeleton.count() < 19) throw std::runtime_error("fewer than nineteen bones");

        for (const char* level : {"near", "far"})
        {
            JsonElement element;
            if (!root.TryGetProperty(level, element)) continue;
            const std::string binary =
                ReadFile(std::filesystem::path(root_) / Text(element, "file"));
            if (binary.empty()) throw std::runtime_error(std::string(level) + ": no binary");
            problem = ReadLevel(element, binary, name, materials_,
                                std::string(level) == "near" ? slot->near : slot->far);
            if (!problem.empty()) throw std::runtime_error(std::string(level) + ": " + problem);
        }
    }
    catch (const std::exception& failure)
    {
        failures_.push_back(name + ": " + failure.what());
        CNA::Logger::Warn("cna-street: person '" + name + "' not loaded: " + failure.what());
        slot->near.clear();
        return nullptr;
    }
    if (slot->far.empty()) slot->far = slot->near;

    std::size_t triangles = 0;
    for (const Part& part : slot->near) triangles += part.mesh.triangleCount();
    ++loaded_;
    CNA::Logger::Info("cna-street: imported person '" + name + "' -- "
                      + std::to_string(slot->near.size()) + " part(s), "
                      + std::to_string(triangles) + " triangles, "
                      + std::to_string(slot->height) + " m tall as authored");
    return slot.get();
}

}  // namespace CnaStreet
