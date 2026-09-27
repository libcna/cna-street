// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/EnvironmentBaker.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaStreet {

namespace {

/// Face-local coordinates in -1..1 with v running *down* the face, which is the
/// cube-map convention and the same layout `SkySystem::cubeDirection` writes.
Vector3 FaceDirection(int face, float u, float v)
{
    const float a = u * 2.0f - 1.0f;
    const float b = 1.0f - v * 2.0f;
    Vector3 direction;
    switch (face)
    {
        case 0:  direction = Vector3( 1.0f,    b,   -a); break;   // +X
        case 1:  direction = Vector3(-1.0f,    b,    a); break;   // -X
        case 2:  direction = Vector3(    a, 1.0f,   -b); break;   // +Y
        case 3:  direction = Vector3(    a, -1.0f,   b); break;   // -Y
        case 4:  direction = Vector3(    a,    b, 1.0f); break;   // +Z
        default: direction = Vector3(   -a,    b, -1.0f); break;  // -Z
    }
    const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y
                                   + direction.Z * direction.Z);
    return Vector3(direction.X / length, direction.Y / length, direction.Z / length);
}

Vector3 Normalise(const Vector3& v)
{
    const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    if (length < 1e-9f) return Vector3(0.0f, 0.0f, 1.0f);
    return Vector3(v.X / length, v.Y / length, v.Z / length);
}

float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

Vector3 Cross(const Vector3& a, const Vector3& b)
{
    return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
}

/// An orthonormal basis around @p normal, with the helper axis chosen away from
/// it: a cross product with a parallel vector is zero, and every sample after
/// it would be NaN.
void BuildBasis(const Vector3& normal, Vector3& tangent, Vector3& bitangent)
{
    const Vector3 up = std::abs(normal.Z) < 0.999f ? Vector3(0.0f, 0.0f, 1.0f)
                                                   : Vector3(1.0f, 0.0f, 0.0f);
    tangent   = Normalise(Cross(up, normal));
    bitangent = Cross(normal, tangent);
}

void Hammersley(int index, int count, float& x, float& y)
{
    x = count > 0 ? (static_cast<float>(index) + 0.5f) / static_cast<float>(count) : 0.0f;
    // Van der Corput radical inverse, base 2.
    unsigned int bits = static_cast<unsigned int>(index);
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    y = static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vector3 ImportanceSampleGgx(float x, float y, const Vector3& normal, float roughness)
{
    const float a = std::max(roughness * roughness, 1e-4f);
    const float phi = 2.0f * MathHelper::Pi * x;
    const float cosTheta = std::sqrt((1.0f - y) / (1.0f + (a * a - 1.0f) * y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const Vector3 local(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);

    Vector3 tangent, bitangent;
    BuildBasis(normal, tangent, bitangent);
    return Normalise(Vector3(tangent.X * local.X + bitangent.X * local.Y + normal.X * local.Z,
                             tangent.Y * local.X + bitangent.Y * local.Y + normal.Y * local.Z,
                             tangent.Z * local.X + bitangent.Z * local.Y + normal.Z * local.Z));
}

Color ToColor(const Vector3& linear)
{
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return Color(channel(linear.X), channel(linear.Y), channel(linear.Z), 255);
}

/// The environment read back once, sampled nearest-neighbour by direction.
/// Every integral here averages hundreds of samples, so filtering would only
/// remove noise that is already gone.
struct CubeSampler
{
    std::vector<std::vector<Color>> faces;
    int size = 0;

    explicit CubeSampler(TextureCube& cube)
    {
        size = cube.getSizeProperty();
        faces.resize(6);
        for (int face = 0; face < 6; ++face)
        {
            auto& texels = faces[static_cast<std::size_t>(face)];
            texels.assign(static_cast<std::size_t>(size) * size, Color::Black);
            cube.GetData(static_cast<CubeMapFace>(face), texels.data(),
                         static_cast<int>(texels.size()));
        }
    }

    [[nodiscard]] Vector3 sample(const Vector3& direction) const
    {
        const float ax = std::abs(direction.X);
        const float ay = std::abs(direction.Y);
        const float az = std::abs(direction.Z);

        int face = 0;
        float sc = 0.0f, tc = 0.0f, ma = 1.0f;
        if (ax >= ay && ax >= az)
        {
            ma = ax;
            if (direction.X > 0.0f) { face = 0; sc = -direction.Z; tc = direction.Y; }
            else                    { face = 1; sc =  direction.Z; tc = direction.Y; }
        }
        else if (ay >= az)
        {
            ma = ay;
            if (direction.Y > 0.0f) { face = 2; sc = direction.X; tc = -direction.Z; }
            else                    { face = 3; sc = direction.X; tc =  direction.Z; }
        }
        else
        {
            ma = az;
            if (direction.Z > 0.0f) { face = 4; sc =  direction.X; tc = direction.Y; }
            else                    { face = 5; sc = -direction.X; tc = direction.Y; }
        }

        if (ma < 1e-9f) ma = 1e-9f;
        const float u = (sc / ma) * 0.5f + 0.5f;
        const float v = 0.5f - (tc / ma) * 0.5f;
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(size)), 0, size - 1);
        const int y = std::clamp(static_cast<int>(v * static_cast<float>(size)), 0, size - 1);

        const Color& texel = faces[static_cast<std::size_t>(face)]
                                  [static_cast<std::size_t>(y) * size + x];
        return Vector3(texel.getRProperty() / 255.0f, texel.getGProperty() / 255.0f,
                       texel.getBProperty() / 255.0f);
    }
};

}  // namespace

EnvironmentBaker::EnvironmentBaker(GraphicsDevice& device) : device_(device) {}

std::unique_ptr<TextureCube> EnvironmentBaker::irradiance(TextureCube& environment, int size,
                                                          int sampleCount) const
{
    if (size <= 0 || sampleCount <= 0)
        throw std::invalid_argument("EnvironmentBaker::irradiance: size and sampleCount must be "
                                    "positive");

    const CubeSampler source(environment);
    auto result = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    std::vector<Color> face(static_cast<std::size_t>(size) * size, Color::Black);

    // A regular sweep rather than an importance-sampled one: the cosine lobe is
    // smooth and wide, so a grid converges as fast and is reproducible texel for
    // texel. The angles are the same for every texel, so they are taken once.
    const int phiSteps   = sampleCount * 4;
    const int thetaSteps = sampleCount;
    std::vector<float> cosPhi(static_cast<std::size_t>(phiSteps));
    std::vector<float> sinPhi(static_cast<std::size_t>(phiSteps));
    for (int p = 0; p < phiSteps; ++p)
    {
        const float phi = 2.0f * MathHelper::Pi * (static_cast<float>(p) + 0.5f)
                          / static_cast<float>(phiSteps);
        cosPhi[static_cast<std::size_t>(p)] = std::cos(phi);
        sinPhi[static_cast<std::size_t>(p)] = std::sin(phi);
    }
    std::vector<float> cosTheta(static_cast<std::size_t>(thetaSteps));
    std::vector<float> sinTheta(static_cast<std::size_t>(thetaSteps));
    for (int t = 0; t < thetaSteps; ++t)
    {
        const float theta = 0.5f * MathHelper::Pi * (static_cast<float>(t) + 0.5f)
                            / static_cast<float>(thetaSteps);
        cosTheta[static_cast<std::size_t>(t)] = std::cos(theta);
        sinTheta[static_cast<std::size_t>(t)] = std::sin(theta);
    }

    for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
    {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const Vector3 normal = FaceDirection(faceIndex, u, v);
                Vector3 tangent, bitangent;
                BuildBasis(normal, tangent, bitangent);

                Vector3 sum(0.0f, 0.0f, 0.0f);
                float weight = 0.0f;
                for (int p = 0; p < phiSteps; ++p)
                {
                    const float cp = cosPhi[static_cast<std::size_t>(p)];
                    const float sp = sinPhi[static_cast<std::size_t>(p)];
                    for (int t = 0; t < thetaSteps; ++t)
                    {
                        const float st = sinTheta[static_cast<std::size_t>(t)];
                        const float ct = cosTheta[static_cast<std::size_t>(t)];
                        const Vector3 direction(
                            tangent.X * cp * st + bitangent.X * sp * st + normal.X * ct,
                            tangent.Y * cp * st + bitangent.Y * sp * st + normal.Y * ct,
                            tangent.Z * cp * st + bitangent.Z * sp * st + normal.Z * ct);
                        const Vector3 radiance = source.sample(direction);
                        // cos for the projected area, sin for the ring's solid angle.
                        const float w = ct * st;
                        sum.X += radiance.X * w;
                        sum.Y += radiance.Y * w;
                        sum.Z += radiance.Z * w;
                        weight += w;
                    }
                }
                const float inverse = weight > 1e-9f ? 1.0f / weight : 0.0f;
                face[static_cast<std::size_t>(y) * size + x] =
                    ToColor(Vector3(sum.X * inverse, sum.Y * inverse, sum.Z * inverse));
            }
        result->SetData(static_cast<CubeMapFace>(faceIndex), face.data(),
                        static_cast<int>(face.size()));
    }
    return result;
}

std::unique_ptr<TextureCube> EnvironmentBaker::prefilteredSpecular(TextureCube& environment,
                                                                   int baseSize, int mipCount,
                                                                   int sampleCount) const
{
    if (baseSize <= 0 || mipCount <= 0 || sampleCount <= 0)
        throw std::invalid_argument("EnvironmentBaker::prefilteredSpecular: baseSize, mipCount "
                                    "and sampleCount must be positive");

    const CubeSampler source(environment);
    auto result = std::make_unique<TextureCube>(device_, baseSize, true, SurfaceFormat::Color);

    for (int mip = 0; mip < mipCount; ++mip)
    {
        const int mipSize = std::max(1, baseSize >> mip);
        const float roughness = mipCount <= 1
                                    ? 0.0f
                                    : std::clamp(static_cast<float>(mip)
                                                     / static_cast<float>(mipCount - 1),
                                                 0.0f, 1.0f);
        std::vector<Color> face(static_cast<std::size_t>(mipSize) * mipSize, Color::Black);

        for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
        {
            for (int y = 0; y < mipSize; ++y)
                for (int x = 0; x < mipSize; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(mipSize);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(mipSize);
                    // The view direction is taken to be the normal, which is what
                    // lets one cube serve every viewing angle; the BRDF LUT
                    // corrects the grazing angles this gets wrong.
                    const Vector3 normal = FaceDirection(faceIndex, u, v);

                    Vector3 sum(0.0f, 0.0f, 0.0f);
                    float weight = 0.0f;
                    for (int i = 0; i < sampleCount; ++i)
                    {
                        float hx = 0.0f, hy = 0.0f;
                        Hammersley(i, sampleCount, hx, hy);
                        const Vector3 half = ImportanceSampleGgx(hx, hy, normal, roughness);
                        const float vDotH = Dot(normal, half);
                        const Vector3 light = Normalise(Vector3(2.0f * vDotH * half.X - normal.X,
                                                                2.0f * vDotH * half.Y - normal.Y,
                                                                2.0f * vDotH * half.Z - normal.Z));
                        const float nDotL = Dot(normal, light);
                        if (nDotL <= 0.0f) continue;
                        const Vector3 radiance = source.sample(light);
                        sum.X += radiance.X * nDotL;
                        sum.Y += radiance.Y * nDotL;
                        sum.Z += radiance.Z * nDotL;
                        weight += nDotL;
                    }
                    const float inverse = weight > 1e-9f ? 1.0f / weight : 0.0f;
                    face[static_cast<std::size_t>(y) * mipSize + x] =
                        ToColor(Vector3(sum.X * inverse, sum.Y * inverse, sum.Z * inverse));
                }
            result->SetData(static_cast<CubeMapFace>(faceIndex), mip, nullptr, face.data(), 0,
                            static_cast<int>(face.size()));
        }
    }
    return result;
}

std::unique_ptr<Texture2D> EnvironmentBaker::brdfLut(int size, int sampleCount) const
{
    if (size <= 0 || sampleCount <= 0)
        throw std::invalid_argument("EnvironmentBaker::brdfLut: size and sampleCount must be "
                                    "positive");

    auto result = std::make_unique<Texture2D>(device_, size, size);
    std::vector<Color> texels(static_cast<std::size_t>(size) * size, Color::Black);

    for (int y = 0; y < size; ++y)
    {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        for (int x = 0; x < size; ++x)
        {
            const float nDotV = std::max((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                         1e-3f);
            const Vector3 view(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
            const Vector3 normal(0.0f, 0.0f, 1.0f);

            float scale = 0.0f;
            float bias  = 0.0f;
            for (int i = 0; i < sampleCount; ++i)
            {
                float hx = 0.0f, hy = 0.0f;
                Hammersley(i, sampleCount, hx, hy);
                const Vector3 half = ImportanceSampleGgx(hx, hy, normal, roughness);
                const float vDotH = Dot(view, half);
                const Vector3 light = Normalise(Vector3(2.0f * vDotH * half.X - view.X,
                                                        2.0f * vDotH * half.Y - view.Y,
                                                        2.0f * vDotH * half.Z - view.Z));
                const float nDotL = light.Z;
                if (nDotL <= 0.0f) continue;
                const float nDotH = std::max(half.Z, 0.0f);
                const float vDotHClamped = std::max(vDotH, 0.0f);

                // Smith geometry with the IBL k (roughness^2 / 2), not the
                // direct-lighting one, which darkens every rough surface here.
                const float k  = roughness * roughness * 0.5f;
                const float gv = nDotV / (nDotV * (1.0f - k) + k);
                const float gl = nDotL / (nDotL * (1.0f - k) + k);
                const float gVis = (gv * gl * vDotHClamped) / std::max(nDotH * nDotV, 1e-6f);
                const float fc = std::pow(1.0f - vDotHClamped, 5.0f);
                scale += (1.0f - fc) * gVis;
                bias  += fc * gVis;
            }
            scale /= static_cast<float>(sampleCount);
            bias  /= static_cast<float>(sampleCount);
            texels[static_cast<std::size_t>(y) * size + x] = ToColor(Vector3(scale, bias, 0.0f));
        }
    }
    result->SetData(texels.data(), static_cast<int>(texels.size()));
    return result;
}

}  // namespace CnaStreet
