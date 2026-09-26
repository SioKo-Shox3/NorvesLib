#version 450

// ========================================
// ブルームの合成パス
// ========================================
// 縮小・拡大の段を積み上げた結果（半分の解像度）を3×3のテントで拡大し、元の色へ混ぜる。
// 積み上げた結果は各段の和なので、段数で割って平均にする。
// しきい値なし（既定）: 元の色と平均を一定の割合で線形補間し、全体のエネルギーを保つ。
// しきい値あり: 取り出した明るい部分を強度倍して元の色へ加える。
// 入力: SceneColor (HDR R16G16B16A16_FLOAT)
// 出力: Bloomed SceneColor (HDR R16G16B16A16_FLOAT)

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: SceneColor テクスチャ
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// binding 1: 積み上げたブルーム（最上段の拡大結果）
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

// binding 2: 合成パラメータ UBO (std140)
layout(set = 0, binding = 2) uniform BloomCompositeParams
{
    // x = 強度（しきい値なしでは補間の割合、ありでは加算の乗数）
    // y = テントの半径（ブルームのテクセル単位）
    // z = しきい値ありなら1
    // w = 1 / 段数
    vec4 params;
};

vec3 SampleTent(sampler2D source, vec2 uv, float radius)
{
    vec2 offset = radius / vec2(textureSize(source, 0));

    vec3 sum = texture(source, uv).rgb * 4.0;
    sum += texture(source, uv + vec2(-offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2( offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0, -offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0,  offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(-offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2(-offset.x,  offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x,  offset.y)).rgb;
    return sum * (1.0 / 16.0);
}

void main()
{
    vec3 originalColor = texture(sceneColor, fragUV).rgb;
    float intensity = params.x;

    if (intensity <= 0.0)
    {
        outColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 bloom = SampleTent(bloomTexture, fragUV, params.y) * params.w;

    vec3 result = params.z > 0.5
                      ? originalColor + bloom * intensity
                      : mix(originalColor, bloom, intensity);

    outColor = vec4(result, 1.0);
}
