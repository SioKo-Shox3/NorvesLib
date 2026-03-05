#version 450

// ========================================
// SSAO (Screen-Space Ambient Occlusion)
// ========================================
// アルゴリズム: John Chapman SSAO
// GBufferの深度と法線から、各ピクセル周辺の遮蔽度を計算。
// ランダムカーネルサンプリング + ノイズによるジッタリング。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

// GBufferテクスチャ
layout(set = 0, binding = 0) uniform sampler2D gbufferDepth;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;

// SSAOパラメータ
layout(set = 0, binding = 2) uniform SSAOParams
{
    mat4 projection;
    mat4 invProjection;
    vec4 screenSize;      // xy=size, zw=1/size
    float radius;         // サンプリング半径（ワールド単位）
    float bias;           // 深度バイアス（自己遮蔽回避）
    float intensity;      // AO強度
    float _pad0;
} params;

// サンプルカーネル（半球内のランダム方向、64サンプル）
layout(set = 0, binding = 3) uniform SampleKernel
{
    vec4 samples[64];
} kernel;

// ノイズテクスチャ（4x4 タイリング）
layout(set = 0, binding = 4) uniform sampler2D noiseTexture;

// ========================================
// 深度値からビュースペース座標を復元
// ========================================
vec3 ReconstructViewPos(vec2 uv, float depth)
{
    // UV→NDC
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = params.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main()
{
    float depth = texture(gbufferDepth, fragUV).r;

    // 空（深度=1.0）はAO=1.0（遮蔽なし）
    if (depth >= 0.9999)
    {
        outAO = 1.0;
        return;
    }

    // ビュースペース座標と法線を復元
    vec3 fragPos = ReconstructViewPos(fragUV, depth);

    // GBuffer法線はワールドスペース → ビュースペースに変換が必要だが、
    // 簡易版としてワールド法線をそのまま使用（カメラが大きく回転しない前提）
    vec3 normal = normalize(texture(gbufferNormal, fragUV).rgb);

    // ノイズテクスチャからランダム回転ベクトル取得
    vec2 noiseScale = params.screenSize.xy / 4.0; // 4x4テクスチャのタイリング
    vec3 randomVec = normalize(texture(noiseTexture, fragUV * noiseScale).rgb * 2.0 - 1.0);

    // Gramm-Schmidt法で接線空間基底を構築
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // サンプリングとオクルージョン計算
    float occlusion = 0.0;
    const int KERNEL_SIZE = 32;

    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        // カーネルサンプルを接線空間から変換
        vec3 sampleDir = TBN * kernel.samples[i].xyz;
        vec3 samplePos = fragPos + sampleDir * params.radius;

        // ビュースペース→クリップスペース→UV
        vec4 offset = params.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        // サンプル位置の実際の深度を取得
        float sampleDepth = texture(gbufferDepth, offset.xy).r;
        vec3 sampleActualPos = ReconstructViewPos(offset.xy, sampleDepth);

        // 深度差による遮蔽判定
        // サンプル位置が実際のジオメトリより奥にある場合 → 遮蔽
        float rangeCheck = smoothstep(0.0, 1.0, params.radius / abs(fragPos.z - sampleActualPos.z));
        occlusion += (sampleActualPos.z >= samplePos.z + params.bias ? 1.0 : 0.0) * rangeCheck;
    }

    // 正規化してAO値に変換
    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));

    // 強度を適用して出力
    outAO = pow(occlusion, params.intensity);
}
