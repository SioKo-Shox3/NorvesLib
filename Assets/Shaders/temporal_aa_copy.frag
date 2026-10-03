#version 450

// TAA の解決の結果（次のフレームの履歴）を、軽くシャープ化して SceneColor へ書き戻す。
// 履歴を混ぜて柔らかくなった細かな模様を戻すため、上下左右4画素の平均との差を blend.w の割合で足す。
// 明るい画素の縁に輪が出ないよう、差は輝度で圧縮した色（x / (1 + 輝度)）で取り、結果は近傍の最小・最大の
// 範囲へ収める。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D resolvedTexture;

layout(std140, set = 0, binding = 4) uniform TemporalAAParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;
    vec4 imageSize;
    vec4 blend;
    vec4 jitter;
} params;

float Luma(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 Compress(vec3 color)
{
    return color / (1.0 + Luma(color));
}

vec3 Expand(vec3 color)
{
    return color / max(1.0 - Luma(color), 1.0e-4);
}

vec3 FetchCompressed(ivec2 texel, ivec2 size)
{
    return Compress(max(texelFetch(resolvedTexture, clamp(texel, ivec2(0), size - 1), 0).rgb, vec3(0.0)));
}

void main()
{
    ivec2 size = ivec2(params.imageSize.xy);
    ivec2 texel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    vec4 center = texelFetch(resolvedTexture, texel, 0);
    float strength = params.blend.w;
    if (strength <= 0.0)
    {
        outColor = center;
        return;
    }
    vec3 c = Compress(max(center.rgb, vec3(0.0)));
    vec3 n = FetchCompressed(texel + ivec2(0, -1), size);
    vec3 s = FetchCompressed(texel + ivec2(0, 1), size);
    vec3 e = FetchCompressed(texel + ivec2(1, 0), size);
    vec3 w = FetchCompressed(texel + ivec2(-1, 0), size);
    vec3 minimum = min(c, min(min(n, s), min(e, w)));
    vec3 maximum = max(c, max(max(n, s), max(e, w)));
    vec3 sharpened = c + strength * (c - 0.25 * (n + s + e + w));
    outColor = vec4(Expand(clamp(sharpened, minimum, maximum)), center.a);
}
