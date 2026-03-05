#version 450

// ========================================
// FXAA 3.11 (Fast Approximate Anti-Aliasing)
// Based on NVIDIA FXAA by Timothy Lottes
// Simplified for quality preset 12 (default)
// ========================================

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: トーンマッピング後のLDRシーンカラー
layout(set = 0, binding = 0) uniform sampler2D inputTexture;

// binding 1: FXAAパラメータ
layout(set = 0, binding = 1) uniform FXAAParams
{
    vec4 texelSize;       // xy = 1.0/resolution, zw = resolution
    float edgeThreshold;  // エッジ検出閾値 (default: 0.0312)
    float edgeThresholdMin; // 最小エッジ閾値 (default: 0.0625)
    float subpixelQuality;  // サブピクセル品質 (default: 0.75)
    uint  bEnabled;       // FXAA有効フラグ
} params;

// ========================================
// 輝度計算（Rec.709）
// ========================================
float FxaaLuma(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

// ========================================
// テクスチャサンプリングヘルパー
// ========================================
vec3 SampleOffset(vec2 uv, vec2 offset)
{
    return texture(inputTexture, uv + offset * params.texelSize.xy).rgb;
}

void main()
{
    // FXAA無効の場合はパススルー
    if (params.bEnabled == 0u)
    {
        outColor = texture(inputTexture, fragUV);
        return;
    }

    vec2 uv = fragUV;
    vec2 texelSize = params.texelSize.xy;

    // ========================================
    // Step 1: 周辺ルミナンスサンプリング
    // ========================================
    vec3 rgbM  = texture(inputTexture, uv).rgb;
    vec3 rgbN  = SampleOffset(uv, vec2( 0.0, -1.0));
    vec3 rgbS  = SampleOffset(uv, vec2( 0.0,  1.0));
    vec3 rgbE  = SampleOffset(uv, vec2( 1.0,  0.0));
    vec3 rgbW  = SampleOffset(uv, vec2(-1.0,  0.0));

    float lumaM = FxaaLuma(rgbM);
    float lumaN = FxaaLuma(rgbN);
    float lumaS = FxaaLuma(rgbS);
    float lumaE = FxaaLuma(rgbE);
    float lumaW = FxaaLuma(rgbW);

    // ========================================
    // Step 2: エッジ検出
    // ========================================
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // エッジ閾値以下ならスキップ
    if (lumaRange < max(params.edgeThresholdMin, lumaMax * params.edgeThreshold))
    {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // ========================================
    // Step 3: コーナーサンプリング
    // ========================================
    vec3 rgbNW = SampleOffset(uv, vec2(-1.0, -1.0));
    vec3 rgbNE = SampleOffset(uv, vec2( 1.0, -1.0));
    vec3 rgbSW = SampleOffset(uv, vec2(-1.0,  1.0));
    vec3 rgbSE = SampleOffset(uv, vec2( 1.0,  1.0));

    float lumaNW = FxaaLuma(rgbNW);
    float lumaNE = FxaaLuma(rgbNE);
    float lumaSW = FxaaLuma(rgbSW);
    float lumaSE = FxaaLuma(rgbSE);

    // ========================================
    // Step 4: サブピクセルエイリアシング検出
    // ========================================
    float lumaL = (lumaN + lumaS + lumaE + lumaW) * 0.25;
    float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0, (rangeL / lumaRange) - params.subpixelQuality);
    blendL = min(blendL / (1.0 - params.subpixelQuality), 1.0);
    // Simplified: cap at 0.75
    blendL = min(blendL, 0.75);

    // ========================================
    // Step 5: エッジ方向検出（水平 vs 垂直）
    // ========================================
    float edgeH = abs(lumaNW + lumaNE - 2.0 * lumaN) +
                  2.0 * abs(lumaW + lumaE - 2.0 * lumaM) +
                  abs(lumaSW + lumaSE - 2.0 * lumaS);

    float edgeV = abs(lumaNW + lumaSW - 2.0 * lumaW) +
                  2.0 * abs(lumaN + lumaS - 2.0 * lumaM) +
                  abs(lumaNE + lumaSE - 2.0 * lumaE);

    bool bHorizontal = (edgeH >= edgeV);

    // ========================================
    // Step 6: エッジに沿ったサンプリング方向決定
    // ========================================
    float stepLength = bHorizontal ? texelSize.y : texelSize.x;

    float luma1 = bHorizontal ? lumaN : lumaW;
    float luma2 = bHorizontal ? lumaS : lumaE;

    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;

    bool bSteepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    if (!bSteepest)
    {
        stepLength = -stepLength;
    }

    // エッジの中間点
    vec2 currentUV = uv;
    if (bHorizontal)
    {
        currentUV.y += stepLength * 0.5;
    }
    else
    {
        currentUV.x += stepLength * 0.5;
    }

    // ========================================
    // Step 7: エッジに沿って両方向に探索
    // ========================================
    vec2 searchDir = bHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    vec2 uv1 = currentUV - searchDir;
    vec2 uv2 = currentUV + searchDir;

    float lumaEnd1 = FxaaLuma(texture(inputTexture, uv1).rgb) - lumaM;
    float lumaEnd2 = FxaaLuma(texture(inputTexture, uv2).rgb) - lumaM;

    bool bReached1 = abs(lumaEnd1) >= gradientScaled;
    bool bReached2 = abs(lumaEnd2) >= gradientScaled;
    bool bReachedBoth = bReached1 && bReached2;

    // 最大12ステップ探索
    const float QUALITY[12] = float[12](
        1.0, 1.0, 1.0, 1.0, 1.0,
        1.5, 2.0, 2.0, 2.0, 2.0,
        4.0, 8.0
    );

    for (int i = 0; i < 12 && !bReachedBoth; i++)
    {
        if (!bReached1)
        {
            uv1 -= searchDir * QUALITY[i];
            lumaEnd1 = FxaaLuma(texture(inputTexture, uv1).rgb) - lumaM;
            bReached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!bReached2)
        {
            uv2 += searchDir * QUALITY[i];
            lumaEnd2 = FxaaLuma(texture(inputTexture, uv2).rgb) - lumaM;
            bReached2 = abs(lumaEnd2) >= gradientScaled;
        }
        bReachedBoth = bReached1 && bReached2;
    }

    // ========================================
    // Step 8: 最終ブレンド
    // ========================================
    float dist1 = bHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = bHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool bDirection1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);

    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLength + 0.5;

    bool bLumaEndCorrect = ((bDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != ((lumaM - lumaL) < 0.0);

    float finalOffset = bLumaEndCorrect ? pixelOffset : 0.0;

    // サブピクセルブレンドと組み合わせ
    finalOffset = max(finalOffset, blendL);

    // 最終UV計算
    vec2 finalUV = uv;
    if (bHorizontal)
    {
        finalUV.y += finalOffset * stepLength;
    }
    else
    {
        finalUV.x += finalOffset * stepLength;
    }

    outColor = vec4(texture(inputTexture, finalUV).rgb, 1.0);
}
