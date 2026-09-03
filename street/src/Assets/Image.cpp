// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/Image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::Xna::Framework::Color;

namespace CnaStreet::Assets {

namespace {

/// IEC 61966-2-1 linear -> sRGB. The piecewise form, not the 1/2.2
/// approximation: the difference is visible in the dark end of an asphalt
/// texture, which is most of an asphalt texture.
float LinearToSrgb(float linear)
{
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f
                                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

std::uint8_t Quantise(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

int Wrap(int v, int n) { const int m = v % n; return m < 0 ? m + n : m; }

}  // namespace

Image::Image(int width, int height, float r, float g, float b, float a)
    : width_(std::max(0, width)), height_(std::max(0, height))
{
    data_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
    for (std::size_t i = 0; i < data_.size(); i += 4)
    {
        data_[i]     = r;
        data_[i + 1] = g;
        data_[i + 2] = b;
        data_[i + 3] = a;
    }
}

void Image::set(int x, int y, float r, float g, float b, float a)
{
    float* p = at(x, y);
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

void Image::setRgb(int x, int y, float r, float g, float b)
{
    float* p = at(x, y);
    p[0] = r; p[1] = g; p[2] = b;
}

const float* Image::wrapped(int x, int y) const
{
    return &data_[index(Wrap(x, width_), Wrap(y, height_))];
}

void Image::sampleBilinear(float u, float v, float out[4]) const
{
    const float fx = u * static_cast<float>(width_) - 0.5f;
    const float fy = v * static_cast<float>(height_) - 0.5f;
    const int   x0 = static_cast<int>(std::floor(fx));
    const int   y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const float* p00 = wrapped(x0, y0);
    const float* p10 = wrapped(x0 + 1, y0);
    const float* p01 = wrapped(x0, y0 + 1);
    const float* p11 = wrapped(x0 + 1, y0 + 1);
    for (int c = 0; c < 4; ++c)
    {
        const float a = p00[c] + (p10[c] - p00[c]) * tx;
        const float b = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = a + (b - a) * ty;
    }
}

void Image::blur(int radius, int passes)
{
    if (radius <= 0 || empty()) return;
    std::vector<float> scratch(data_.size());
    const float inverse = 1.0f / static_cast<float>(radius * 2 + 1);

    for (int pass = 0; pass < passes; ++pass)
    {
        // Horizontal.
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
            {
                float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int k = -radius; k <= radius; ++k)
                {
                    const float* p = wrapped(x + k, y);
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
                const std::size_t o = index(x, y);
                for (int c = 0; c < 4; ++c) scratch[o + c] = sum[c] * inverse;
            }
        data_.swap(scratch);
        // Vertical.
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
            {
                float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int k = -radius; k <= radius; ++k)
                {
                    const float* p = wrapped(x, y + k);
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
                const std::size_t o = index(x, y);
                for (int c = 0; c < 4; ++c) scratch[o + c] = sum[c] * inverse;
            }
        data_.swap(scratch);
    }
}

Image Image::downsampled() const
{
    const int w = std::max(1, width_ / 2);
    const int h = std::max(1, height_ / 2);
    Image out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float* a = wrapped(x * 2, y * 2);
            const float* b = wrapped(x * 2 + 1, y * 2);
            const float* c = wrapped(x * 2, y * 2 + 1);
            const float* d = wrapped(x * 2 + 1, y * 2 + 1);
            float* o = out.at(x, y);
            for (int ch = 0; ch < 4; ++ch) o[ch] = (a[ch] + b[ch] + c[ch] + d[ch]) * 0.25f;
        }
    return out;
}

std::vector<Color> Image::toColors(bool srgb) const
{
    std::vector<Color> out;
    out.reserve(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
        {
            const float* p = at(x, y);
            if (srgb)
                out.emplace_back(static_cast<int>(Quantise(LinearToSrgb(p[0]))),
                                 static_cast<int>(Quantise(LinearToSrgb(p[1]))),
                                 static_cast<int>(Quantise(LinearToSrgb(p[2]))),
                                 static_cast<int>(Quantise(p[3])));
            else
                out.emplace_back(static_cast<int>(Quantise(p[0])), static_cast<int>(Quantise(p[1])),
                                 static_cast<int>(Quantise(p[2])), static_cast<int>(Quantise(p[3])));
        }
    return out;
}

Image Image::toNormalMap(float strength) const
{
    Image out(width_, height_, 0.5f, 0.5f, 1.0f, 1.0f);
    // Sobel, which gives a smoother gradient than a central difference and
    // therefore a normal map without the stair-stepping a 3x3 cross produces on
    // a diagonal crack.
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
        {
            const float h00 = wrapped(x - 1, y - 1)[0], h10 = wrapped(x, y - 1)[0],
                        h20 = wrapped(x + 1, y - 1)[0];
            const float h01 = wrapped(x - 1, y)[0],     h21 = wrapped(x + 1, y)[0];
            const float h02 = wrapped(x - 1, y + 1)[0], h12 = wrapped(x, y + 1)[0],
                        h22 = wrapped(x + 1, y + 1)[0];

            const float dx = (h20 + 2.0f * h21 + h22) - (h00 + 2.0f * h01 + h02);
            const float dy = (h02 + 2.0f * h12 + h22) - (h00 + 2.0f * h10 + h20);

            float nx = -dx * strength;
            float ny = -dy * strength;
            float nz = 1.0f;
            const float inverse = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inverse; ny *= inverse; nz *= inverse;
            out.set(x, y, nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, 1.0f);
        }
    return out;
}

void Image::setChannelFrom(int channel, const Image& source, int sourceChannel)
{
    if (source.empty() || empty()) return;
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
        {
            const int sx = source.width() == width_ ? x : x * source.width() / width_;
            const int sy = source.height() == height_ ? y : y * source.height() / height_;
            at(x, y)[channel] = source.at(sx, sy)[sourceChannel];
        }
}

// ---------------------------------------------------------------------------
// PNG writing. A complete, correct, minimal encoder: stored (uncompressed)
// deflate blocks inside a valid zlib stream. Deliberately not a compressor --
// these files are build-time intermediates handed straight to CNA's content
// pipeline, and pulling in a compression dependency to make an intermediate
// smaller would be the wrong trade. Filter type 0 on every row.
// ---------------------------------------------------------------------------
namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t length, std::uint32_t crc = 0xFFFFFFFFu)
{
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready)
    {
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    for (std::size_t i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void PushBigEndian32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void PushChunk(std::vector<std::uint8_t>& out, const char type[5],
               const std::vector<std::uint8_t>& payload)
{
    PushBigEndian32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc =
        Crc32(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
    PushBigEndian32(out, crc);
}

std::uint32_t Adler32(const std::vector<std::uint8_t>& data)
{
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : data)
    {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

}  // namespace

bool Image::writePng(const std::string& path, bool srgb) const
{
    if (empty()) return false;

    std::vector<std::uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(width_) * 4u + 1u) * static_cast<std::size_t>(height_));
    for (int y = 0; y < height_; ++y)
    {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < width_; ++x)
        {
            const float* p = at(x, y);
            if (srgb)
            {
                raw.push_back(Quantise(LinearToSrgb(p[0])));
                raw.push_back(Quantise(LinearToSrgb(p[1])));
                raw.push_back(Quantise(LinearToSrgb(p[2])));
            }
            else
            {
                raw.push_back(Quantise(p[0]));
                raw.push_back(Quantise(p[1]));
                raw.push_back(Quantise(p[2]));
            }
            raw.push_back(Quantise(p[3]));
        }
    }

    std::vector<std::uint8_t> zlib;
    zlib.push_back(0x78);  // CM = 8 (deflate), CINFO = 7 (32K window)
    zlib.push_back(0x01);  // FLG: no dictionary, fastest, header checksum valid
    const std::size_t blockMax = 65535;
    for (std::size_t offset = 0; offset < raw.size(); offset += blockMax)
    {
        const std::size_t length = std::min(blockMax, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((~length) & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>(((~length) >> 8) & 0xFFu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }
    PushBigEndian32(zlib, Adler32(raw));

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    PushBigEndian32(ihdr, static_cast<std::uint32_t>(width_));
    PushBigEndian32(ihdr, static_cast<std::uint32_t>(height_));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    PushChunk(png, "IHDR", ihdr);
    PushChunk(png, "IDAT", zlib);
    PushChunk(png, "IEND", {});

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return written == png.size();
}

}  // namespace CnaStreet::Assets
