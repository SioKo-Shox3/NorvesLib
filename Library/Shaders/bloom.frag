#version 450

// ========================================
// Bloom Post-Process Fragment Shader
// ========================================
// シングルパスブルーム：輝度抽出 + マルチサンプルガウスぼかし + 合成
// 入力: SceneColor (HDR R16G16B16A16_FLOAT)
// 出力: Bloomed SceneColor (HDR R16G16B16A16_FLOAT)

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: SceneColor テクスチャ
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// binding 1: Bloom パラメータ UBO (std140)
layout(set = 0, binding = 1) uniform BloomParams
{
    float threshold;    // 輝度閾値（この値以上が光る）
    float intensity;    // ブルーム強度
    float radius;       // ブラー半径（ピクセル数）
    float softKnee;     // ソフト閾値の膝（0-1）
};

// 輝度計算（BT.709）
float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// ソフト閾値によるブルーム輝度抽出
vec3 ExtractBright(vec3 color)
{
    float lum = Luminance(color);
    float knee = threshold * softKnee;
    float soft = lum - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, lum - threshold) / max(lum, 0.00001);
    return color * max(contribution, 0.0);
}

void main()
{
    vec2 texelSize = 1.0 / textureSize(sceneColor, 0);
    vec3 originalColor = texture(sceneColor, fragUV).rgb;

    // ========================================
    // 13-tap ガウスブラーカーネル（2パス相当の近似）
    // ========================================
    // 中心 + 十字方向の複数サンプルで放射状ブラーを実現
    float r = radius;

    // ガウス重み（σ ≈ radius/2）
    // オフセットと重み: 0, ±1, ±2, ±3, ±4, ±5, ±6 ピクセル分
    const int SAMPLE_COUNT = 7;
    float offsets[SAMPLE_COUNT] = float[](0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
    float weights[SAMPLE_COUNT] = float[](
        0.1964825501511404,
        0.2969069646728344,
        0.2195956136101940,
        0.0449740700466070,
        0.0045813026965520,
        0.0003335213221380,
        0.0000024688202280
    );

    // 明るい部分を抽出してブラー
    vec3 bloomH = ExtractBright(originalColor) * weights[0];
    vec3 bloomV = bloomH;

    for (int i = 1; i < SAMPLE_COUNT; ++i)
    {
        float offset = offsets[i] * r;

        // 水平方向サンプル
        vec2 offsetH = vec2(offset * texelSize.x, 0.0);
        bloomH += ExtractBright(texture(sceneColor, fragUV + offsetH).rgb) * weights[i];
        bloomH += ExtractBright(texture(sceneColor, fragUV - offsetH).rgb) * weights[i];

        // 垂直方向サンプル
        vec2 offsetV = vec2(0.0, offset * texelSize.y);
        bloomV += ExtractBright(texture(sceneColor, fragUV + offsetV).rgb) * weights[i];
        bloomV += ExtractBright(texture(sceneColor, fragUV - offsetV).rgb) * weights[i];
    }

    // 対角方向も追加（ブルームの広がりをより自然に）
    vec3 bloomD1 = ExtractBright(originalColor) * weights[0];
    vec3 bloomD2 = bloomD1;

    for (int i = 1; i < SAMPLE_COUNT; ++i)
    {
        float offset = offsets[i] * r * 0.7071; // 1/sqrt(2) 対角距離補正

        // 対角方向1（左上→右下）
        vec2 offsetD1 = vec2(offset * texelSize.x, offset * texelSize.y);
        bloomD1 += ExtractBright(texture(sceneColor, fragUV + offsetD1).rgb) * weights[i];
        bloomD1 += ExtractBright(texture(sceneColor, fragUV - offsetD1).rgb) * weights[i];

        // 対角方向2（右上→左下）
        vec2 offsetD2 = vec2(offset * texelSize.x, -offset * texelSize.y);
        bloomD2 += ExtractBright(texture(sceneColor, fragUV + offsetD2).rgb) * weights[i];
        bloomD2 += ExtractBright(texture(sceneColor, fragUV - offsetD2).rgb) * weights[i];
    }

    // 4方向のブルームを平均して合成
    vec3 bloom = (bloomH + bloomV + bloomD1 + bloomD2) * 0.25;

    // 元の色にブルームを加算
    vec3 result = originalColor + bloom * intensity;

    outColor = vec4(result, 1.0);
}
