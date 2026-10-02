#version 450

// ========================================
// Screen-Space Reflections (SSR)
// Hi-Z Raymarching with binary refinement
// ========================================

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// GBufferテクスチャ
layout(set = 0, binding = 0) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 1) uniform sampler2D gbufferMaterial; // r=metallic, g=roughness, b=ao
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;

// シーンカラー（Lightingパス出力）
layout(set = 0, binding = 3) uniform sampler2D sceneColor;

// SSRパラメータ
layout(set = 0, binding = 4) uniform SSRParams
{
    mat4 projection;
    mat4 invProjection;
    mat4 view;
    mat4 invView;
    vec4 screenSize;      // xy = resolution, zw = 1/resolution
    float maxDistance;     // 最大レイ距離
    float thickness;      // レイの厚み判定
    float maxSteps;       // 最大ステップ数
    float fadeStart;      // フェード開始距離
    float fadeEnd;        // フェード終了距離
    float roughnessFadeStart; // 反射を粗さで弱め始める粗さ
    float roughnessFadeEnd;   // 反射が0になる粗さ（これ以上はSSRなし）
    float intensity;      // SSR強度
    uint  bEnabled;       // SSR有効フラグ
} params;

// サンプラー
layout(set = 0, binding = 5) uniform sampler2D noiseSampler;

// ========================================
// ビュー空間座標復元
// ========================================
vec3 ReconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = params.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// ========================================
// ビュー空間→UV座標変換
// ========================================
vec2 ViewToUV(vec3 viewPos)
{
    vec4 clipPos = params.projection * vec4(viewPos, 1.0);
    clipPos.xy /= clipPos.w;
    return clipPos.xy * 0.5 + 0.5;
}

// ========================================
// レイマーチング（ビュー空間）
// ========================================
bool RayMarch(vec3 origin, vec3 direction, out vec2 hitUV, out float hitDist)
{
    float stepSize = params.maxDistance / params.maxSteps;
    vec3 currentPos = origin;

    for (int i = 0; i < int(params.maxSteps); i++)
    {
        currentPos += direction * stepSize;

        // ビュー空間からUVに変換
        vec2 uv = ViewToUV(currentPos);

        // 画面外チェック
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        {
            return false;
        }

        // 深度サンプリング
        float depth = texture(gbufferDepth, uv).r;
        vec3 sampledViewPos = ReconstructViewPos(uv, depth);

        // ヒット判定（レイがサーフェスを通過した）
        float depthDiff = currentPos.z - sampledViewPos.z;
        if (depthDiff > 0.0 && depthDiff < params.thickness)
        {
            // バイナリリファインメント（精度向上）
            float refinementStep = stepSize * 0.5;
            vec3 refinedPos = currentPos;

            for (int j = 0; j < 5; j++)
            {
                refinedPos -= direction * refinementStep;
                vec2 refinedUV = ViewToUV(refinedPos);
                float refinedDepth = texture(gbufferDepth, refinedUV).r;
                vec3 refinedSampledPos = ReconstructViewPos(refinedUV, refinedDepth);

                float refinedDiff = refinedPos.z - refinedSampledPos.z;
                if (refinedDiff > 0.0)
                {
                    refinedPos -= direction * refinementStep;
                }
                else
                {
                    refinedPos += direction * refinementStep;
                }
                refinementStep *= 0.5;
            }

            hitUV = ViewToUV(refinedPos);
            hitDist = length(refinedPos - origin);
            return true;
        }
    }

    return false;
}

void main()
{
    // SSR無効時はパススルー
    vec4 sceneColorSample = texture(sceneColor, fragUV);

    if (params.bEnabled == 0u)
    {
        outColor = sceneColorSample;
        return;
    }

    // GBufferデータ取得
    float depth = texture(gbufferDepth, fragUV).r;

    // 背景（深度=1.0）はスキップ
    if (depth >= 0.999)
    {
        outColor = sceneColorSample;
        return;
    }

    // GBufferの法線は符号付きのワールド法線（RGBA16F）をそのまま持つ（Lightingと同じ読み方）
    vec3 normalWS = normalize(texture(gbufferNormal, fragUV).xyz);
    vec4 materialSample = texture(gbufferMaterial, fragUV);
    float metallic = materialSample.r;
    float roughness = materialSample.g;

    // 反射が0になる粗さ以上の面はレイを飛ばさない
    if (roughness >= params.roughnessFadeEnd)
    {
        outColor = sceneColorSample;
        return;
    }

    // ビュー空間の位置と法線
    vec3 viewPos = ReconstructViewPos(fragUV, depth);
    // ワールド法線→ビュー法線
    vec3 normalVS = normalize((params.view * vec4(normalWS, 0.0)).xyz);

    // 反射ベクトル計算（ビュー空間）
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = reflect(viewDir, normalVS);

    // レイマーチング
    vec2 hitUV;
    float hitDist;
    bool bHit = RayMarch(viewPos, reflectDir, hitUV, hitDist);

    // 当たった面が反射光線の方を向いていない（裏から当たった）とき、または画面上で1画素も進まずに当たった
    // ときは、凸な面の輪郭で自分の隣の画素に当たった誤りとして棄却する（凸な面から出た光線は自分に当たらない）。
    if (bHit)
    {
        vec3 hitNormalWS = normalize(texture(gbufferNormal, hitUV).xyz);
        vec3 hitNormalVS = normalize((params.view * vec4(hitNormalWS, 0.0)).xyz);
        vec2 hitPixelOffset = (hitUV - fragUV) * params.screenSize.xy;
        if (dot(hitNormalVS, reflectDir) >= 0.0 || dot(hitPixelOffset, hitPixelOffset) < 1.0)
        {
            bHit = false;
        }
    }

    if (bHit)
    {
        // ヒットしたUVからシーンカラーをサンプリング
        vec3 reflectedColor = texture(sceneColor, hitUV).rgb;

        // フレネル（斜めから見るほど反射が強い）
        float NdotV = max(dot(normalVS, -viewDir), 0.0);
        float fresnel = pow(1.0 - NdotV, 5.0);
        // 粗い面ほど斜めのフレネルの上限を下げる（粗さを考えたSchlick。濡れていない石畳が斜めから鏡にならない）
        float f0 = mix(0.04, 1.0, metallic);
        float reflectStrength = mix(f0, max(1.0 - roughness, f0), fresnel);

        // 粗さによるフェード（Start〜End の間でなめらかに0へ）
        float roughnessFade = 1.0 - smoothstep(params.roughnessFadeStart, params.roughnessFadeEnd, roughness);

        // 距離によるフェード
        float distanceFade = 1.0 - smoothstep(params.fadeStart, params.fadeEnd, hitDist);

        // 画面端フェード
        vec2 edgeFade = smoothstep(0.0, 0.1, hitUV) * (1.0 - smoothstep(0.9, 1.0, hitUV));
        float screenEdgeFade = edgeFade.x * edgeFade.y;

        // 最終ブレンド
        float alpha = reflectStrength * roughnessFade * distanceFade * screenEdgeFade * params.intensity;
        vec3 finalColor = mix(sceneColorSample.rgb, reflectedColor, alpha);
        outColor = vec4(finalColor, 1.0);
    }
    else
    {
        outColor = sceneColorSample;
    }
}
