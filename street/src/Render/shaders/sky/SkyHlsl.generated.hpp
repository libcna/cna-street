// SPDX-License-Identifier: MIT
// Rebuild with scripts/generate-sky-hlsl.py.
#pragma once
#include <string_view>
namespace CnaStreet::SkyShaders {
// sky.vulkan.vert.glsl SHA-256: 914f0b075f437d2632b1e1c7f8c29238cef7213491587752d38fea6b1f8a4aba
inline constexpr std::string_view kDirectXVertexHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b0)
{
    float2 viewportSize : packoffset(c0);
};


static float4 gl_Position;
static float2 aPos;
static float2 TexCoord;
static float2 aTexCoord;
static float4 SpriteColor;
static float4 aColor;

struct SPIRV_Cross_Input
{
    float2 aPos : POSITION;
    float2 aTexCoord : TEXCOORD;
    float4 aColor : COLOR;
};

struct SPIRV_Cross_Output
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 ndc = ((aPos / viewportSize) * 2.0f) - 1.0f.xx;
    gl_Position = float4(ndc, 0.0f, 1.0f);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    aTexCoord = stage_input.aTexCoord;
    aColor = stage_input.aColor;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TexCoord = TexCoord;
    stage_output.SpriteColor = SpriteColor;
    return stage_output;
}
)CNA_HLSL";
// sky.vulkan.frag.glsl SHA-256: 1c2c5950190fc37e3a9c8404d2ee3e9487c1298d87364c9ed408e98411b2fb00
inline constexpr std::string_view kDirectXFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uSkyScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b5)
{
    row_major float4x4 uSkyMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b6)
{
    float3 uSkyVectors[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float cnaAirMass(float upwards)
{
    float up = clamp(upwards, 0.0f, 1.0f);
    float zenithDegrees = degrees(acos(up));
    return 1.0f / max(up + (0.50572001934051513671875f * pow(max(96.07994842529296875f - zenithDegrees, 0.001000000047497451305389404296875f), -1.6363999843597412109375f)), 9.9999997473787516355514526367188e-05f);
}

float cnaRayleighPhase(float cosAngle)
{
    return 0.0596831031143665313720703125f * (1.0f + (cosAngle * cosAngle));
}

float cnaMiePhase(float cosAngle)
{
    float gg = 0.577600002288818359375f;
    float d = (1.0f + gg) - (1.519999980926513671875f * cosAngle);
    return (0.079577468335628509521484375f * (1.0f - gg)) / max(pow(max(d, 9.9999997473787516355514526367188e-05f), 1.5f) * (2.0f + gg), 9.9999997473787516355514526367188e-05f);
}

float3 cnaScatteringAlongPath(float3 viewDirection, float3 sunDirection, float turbidity, float viewMass)
{
    float3 view = normalize(viewDirection);
    float3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);
    float param = toSun.y;
    float sunMass = cnaAirMass(param);
    float mie = 0.02099999971687793731689453125f * max(turbidity - 1.0f, 0.0f);
    float3 total = float3(0.0463999994099140167236328125f, 0.108499996364116668701171875f, 0.26499998569488525390625f) + mie.xxx;
    float param_1 = cosAngle;
    float param_2 = cosAngle;
    float3 scattered = (float3(0.0463999994099140167236328125f, 0.108499996364116668701171875f, 0.26499998569488525390625f) * cnaRayleighPhase(param_1)) + (mie * cnaMiePhase(param_2)).xxx;
    float3 alongView = 1.0f.xxx - exp((-total) * viewMass);
    float3 sunlight = exp((-total) * sunMass);
    return (((scattered / total) * alongView) * sunlight) * 24.0f;
}

float3 cnaSkyRadiance(float3 viewDirection, float3 sunDirection, float turbidity)
{
    float param = normalize(viewDirection).y;
    float3 param_1 = viewDirection;
    float3 param_2 = sunDirection;
    float param_3 = turbidity;
    float param_4 = cnaAirMass(param);
    return cnaScatteringAlongPath(param_1, param_2, param_3, param_4);
}

float hash12(float2 p)
{
    float3 p3 = frac(p.xyx * 0.103100001811981201171875f);
    p3 += dot(p3, p3.yzx + 33.3300018310546875f.xxx).xxx;
    return frac((p3.x + p3.y) * p3.z);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = ((f * f) * f) * ((f * ((f * 6.0f) - 15.0f.xx)) + 10.0f.xx);
    float2 param = i;
    float2 param_1 = i + float2(1.0f, 0.0f);
    float2 param_2 = i + float2(0.0f, 1.0f);
    float2 param_3 = i + 1.0f.xx;
    return lerp(lerp(hash12(param), hash12(param_1), u.x), lerp(hash12(param_2), hash12(param_3), u.x), u.y);
}

float fbm(inout float2 p, int octaves)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        if (i >= octaves)
        {
            break;
        }
        float2 param = p;
        sum += (valueNoise(param) * amplitude);
        total += amplitude;
        p = (p * 2.0299999713897705078125f) + float2(17.299999237060546875f, 9.1000003814697265625f);
        amplitude *= 0.5f;
    }
    return sum / total;
}

float deck(float3 direction, float height, float scale, float coverage, float sharpness, float2 drift)
{
    if (direction.y < 0.008000000379979610443115234375f)
    {
        return 0.0f;
    }
    float2 at = ((direction.xz * (height / direction.y)) * scale) + drift;
    float2 param = at;
    int param_1 = 5;
    float _355 = fbm(param, param_1);
    float base = _355;
    float2 param_2 = (at * 3.7000000476837158203125f) + float2(4.19999980926513671875f, 1.7000000476837158203125f);
    int param_3 = 4;
    float _367 = fbm(param_2, param_3);
    float detail = _367;
    float density = (base * 0.7799999713897705078125f) + (detail * 0.2199999988079071044921875f);
    density = smoothstep(1.0f - coverage, (1.0f - coverage) + sharpness, density);
    return density * smoothstep(0.0f, 0.1599999964237213134765625f, direction.y);
}

void frag_main()
{
    float2 screen = float2(TexCoord.x, lerp(TexCoord.y, 1.0f - TexCoord.y, uSkyScalars[5]));
    float4 ray = mul(float4((screen * 2.0f) - 1.0f.xx, 1.0f, 1.0f), uSkyMatrices[0]);
    float3 direction = normalize(ray.xyz / ray.w.xxx);
    float3 param = direction;
    float3 param_1 = uSkyVectors[0];
    float param_2 = uSkyScalars[0];
    float3 sky = cnaSkyRadiance(param, param_1, param_2) * uSkyScalars[1];
    float3 toSun = -normalize(uSkyVectors[0]);
    float dusk = clamp((-toSun.y) * 9.0f, 0.0f, 1.0f);
    if (dusk > 0.0f)
    {
        float up = clamp(direction.y, 0.0f, 1.0f);
        float3 zenithBlue = float3(0.00999999977648258209228515625f, 0.0199999995529651641845703125f, 0.0579999983310699462890625f);
        float3 horizonBlue = float3(0.02999999932944774627685546875f, 0.0379999987781047821044921875f, 0.070000000298023223876953125f);
        float3 twilight = lerp(horizonBlue, zenithBlue, pow(up, 0.60000002384185791015625f).xxx) * uSkyScalars[1];
        float towards = clamp(dot(normalize(float3(direction.x, 0.0f, direction.z)), normalize(float3(toSun.x, 0.0f, toSun.z))), 0.0f, 1.0f);
        float glow = pow(towards, 3.0f) * (1.0f - smoothstep(0.0f, 0.2800000011920928955078125f, direction.y));
        float3 afterglow = (float3(0.3400000035762786865234375f, 0.14000000059604644775390625f, 0.0500000007450580596923828125f) * uSkyScalars[1]) * glow;
        sky = lerp(sky, (twilight + afterglow) + (sky * 0.3499999940395355224609375f), dusk.xxx);
    }
    float cosAngle = dot(direction, toSun);
    float disc = smoothstep(0.999870002269744873046875f, 0.999939978122711181640625f, cosAngle);
    float limb = sqrt(max(1.0f - pow(max(1.0f - cosAngle, 0.0f) / 0.0001300000003539025783538818359375f, 2.0f), 0.0f));
    float3 sunColour = float3(1.0f, 0.939999997615814208984375f, 0.86000001430511474609375f);
    sky += ((((sunColour * disc) * (0.550000011920928955078125f + (0.449999988079071044921875f * limb))) * 42.0f) * uSkyScalars[1]);
    sky += (((sunColour * pow(max(cosAngle, 0.0f), 480.0f)) * 1.60000002384185791015625f) * uSkyScalars[1]);
    if (uSkyScalars[4] > 0.5f)
    {
        float2 drift = float2(uSkyScalars[2] * 0.89999997615814208984375f, uSkyScalars[2] * 0.3499999940395355224609375f);
        float3 param_3 = direction;
        float param_4 = 1500.0f;
        float param_5 = 0.0004199999966658651828765869140625f;
        float param_6 = uSkyScalars[3];
        float param_7 = 0.300000011920928955078125f;
        float2 param_8 = drift;
        float lower = deck(param_3, param_4, param_5, param_6, param_7, param_8);
        float3 toward = normalize(direction + (toSun * 0.1599999964237213134765625f));
        float3 param_9 = toward;
        float param_10 = 1500.0f;
        float param_11 = 0.0004199999966658651828765869140625f;
        float param_12 = uSkyScalars[3];
        float param_13 = 0.300000011920928955078125f;
        float2 param_14 = drift;
        float shadowed = deck(param_9, param_10, param_11, param_12, param_13, param_14);
        float thickness = clamp(shadowed * 1.14999997615814208984375f, 0.0f, 1.0f);
        float3 lit = float3(1.059999942779541015625f, 1.03999996185302734375f, 1.019999980926513671875f);
        float3 shade = float3(0.439999997615814208984375f, 0.4699999988079071044921875f, 0.550000011920928955078125f);
        float3 cloudColour = lerp(lit, shade, (thickness * 0.85000002384185791015625f).xxx);
        float rim = clamp(lower - thickness, 0.0f, 1.0f);
        cloudColour += (((sunColour * rim) * pow(max(cosAngle, 0.0f), 6.0f)) * 0.89999997615814208984375f);
        float daylight = clamp((toSun.y * 3.0f) + 0.07999999821186065673828125f, 0.0f, 1.0f);
        cloudColour *= (uSkyScalars[1] * (0.3499999940395355224609375f + (0.75f * clamp(toSun.y, 0.0f, 1.0f))));
        cloudColour = lerp((sky * 1.25f) + 0.00200000009499490261077880859375f.xxx, cloudColour, daylight.xxx);
        sky = lerp(sky, cloudColour, (clamp(lower, 0.0f, 1.0f) * 0.959999978542327880859375f).xxx);
        float3 param_15 = direction;
        float param_16 = 6200.0f;
        float param_17 = 0.00019000000611413270235061645507812f;
        float param_18 = (uSkyScalars[3] * 0.550000011920928955078125f) + 0.100000001490116119384765625f;
        float param_19 = 0.550000011920928955078125f;
        float2 param_20 = drift * 2.400000095367431640625f;
        float high = deck(param_15, param_16, param_17, param_18, param_19, param_20);
        float3 cirrus = lerp(sky * 1.14999997615814208984375f, float3(1.019999980926513671875f, 1.0099999904632568359375f, 1.0299999713897705078125f) * uSkyScalars[1], daylight.xxx);
        sky = lerp(sky, cirrus, (high * 0.319999992847442626953125f).xxx);
    }
    float below = smoothstep(0.0199999995529651641845703125f, -0.0599999986588954925537109375f, direction.y);
    float3 param_21 = float3(direction.x, 0.02999999932944774627685546875f, direction.z);
    float3 param_22 = uSkyVectors[0];
    float param_23 = uSkyScalars[0];
    float3 haze = cnaSkyRadiance(param_21, param_22, param_23) * uSkyScalars[1];
    float3 ground = lerp(haze, float3(0.100000001490116119384765625f, 0.097999997437000274658203125f, 0.0949999988079071044921875f) * uSkyScalars[1], 0.550000011920928955078125f.xxx);
    sky = lerp(sky, ground, below.xxx);
    if (uSkyScalars[6] > 0.5f)
    {
        float3 c = clamp(sky, 0.0f.xxx, 1.0f.xxx);
        sky = lerp(c * 12.9200000762939453125f, (pow(c, 0.4166666567325592041015625f.xxx) * 1.05499994754791259765625f) - 0.054999999701976776123046875f.xxx, step(0.003130800090730190277099609375f.xxx, c));
    }
    FragColor = float4(sky, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
}
