#version 450

// ========================================
// Bloom Post-Process Fragment Shader
// ========================================
// シングルパスブルーム：輝度抽出 + 等方性カーネルぼかし + 合成
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

    float r = max(radius, 0.5);

    const int SAMPLE_COUNT = 16;
    vec2 sampleOffsets[SAMPLE_COUNT] = vec2[](
        vec2( 0.0000,  0.0000),
        vec2( 0.5278,  0.0935),
        vec2(-0.4063,  0.2066),
        vec2( 0.1398, -0.5453),
        vec2(-0.2521, -0.4046),
        vec2( 0.6350, -0.3192),
        vec2(-0.6025, -0.0981),
        vec2( 0.3177,  0.5670),
        vec2(-0.1176,  0.7124),
        vec2( 0.8219,  0.2143),
        vec2(-0.7448,  0.3981),
        vec2( 0.4922, -0.7486),
        vec2(-0.5084, -0.7056),
        vec2( 0.0532,  0.9148),
        vec2(-0.8865, -0.3121),
        vec2( 0.9116, -0.5110)
    );

    float sampleWeights[SAMPLE_COUNT] = float[](
        0.1900,
        0.1050,
        0.0980,
        0.0940,
        0.0890,
        0.0790,
        0.0730,
        0.0670,
        0.0610,
        0.0490,
        0.0410,
        0.0330,
        0.0280,
        0.0240,
        0.0180,
        0.0140
    );

    vec3 bloomNear = vec3(0.0);
    vec3 bloomFar = vec3(0.0);
    float weightSum = 0.0;

    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        vec2 ringNear = sampleOffsets[i] * texelSize * r * 2.0;
        vec2 ringFar = sampleOffsets[i] * texelSize * r * 4.5;
        float weight = sampleWeights[i];

        bloomNear += ExtractBright(texture(sceneColor, fragUV + ringNear).rgb) * weight;
        bloomFar += ExtractBright(texture(sceneColor, fragUV + ringFar).rgb) * weight;
        weightSum += weight;
    }

    vec3 bloom = (bloomNear * 0.68 + bloomFar * 0.32) / max(weightSum, 0.0001);

    // 元の色にブルームを加算
    vec3 result = originalColor + bloom * intensity;

    outColor = vec4(result, 1.0);
}
