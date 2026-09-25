// パストレーサーのレイ生成・命中・不交差の各段が共有する定数ブロックとペイロード。

layout(set = 0, binding = 1, std140) uniform PathTracingParameters
{
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    uvec4 imageState; // xy=寸法、z=試料番号、w=インスタンス数
    vec4 skySunDirectionAndCosRadius;
    vec4 skyState; // x=プリエクスポージャ、y=地表での太陽の事前露出照度の輝度、z=空有効、w=空要求
    vec4 fogDensityHeightFalloffAndEnabled; // xyz=R3密度・基準高さ・減衰率、w=霧有効
    vec4 fogColorAndPreExposure; // rgb=空欠落時の霧色、w=事前露出
    vec4 fogLightDirectionAndAnisotropy; // xyz=方向光の進行方向、w=HG異方性
    vec4 fogLightRadianceAndEnabled; // rgb=方向光放射輝度、w=散乱有効
    vec4 exposureAndDebug; // x=カメラのプリエクスポージャ、y=検証出力（0=放射輝度）
    uvec4 lightState; // x=点・spot・方向光の数、y=発光instance数、z=発光三角形数、w=BSDF(bit0-1)と標本化戦略(bit2-3)
    vec4 environmentRadiance; // rgb=一様環境の放射輝度または環境textureの倍率、w=環境光の種類
    uvec4 sampleState; // x=このdispatchの試料数、y=bit0正射影・bit1画素中心、z=検証mode 252、w=輸送範囲 // x=このdispatchの試料数、y=正射影(1)、z=検証mode 252の被覆alpha(1)
    vec4 skySunIlluminance; // rgb=地表での太陽の事前露出照度（大気の透過率込み。ラスタの空の太陽の方向光と同じ値）
} parameters;

// 表面BSDF（PathTracingBsdfModeと同じ値）。
const uint PATH_BSDF_PRODUCTION = 0u;
const uint PATH_BSDF_VALIDATION_LAMBERT = 1u;

// 発光三角形と太陽円盤の標本化戦略（PathTracingLightSamplingと同じ値）。
const uint PATH_SAMPLING_MIS = 0u;
const uint PATH_SAMPLING_LIGHT_ONLY = 1u;
const uint PATH_SAMPLING_BSDF_ONLY = 2u;

// 光輸送の範囲（PathTracingTransportScopeと同じ値）。
const uint PATH_TRANSPORT_FULL = 0u;
const uint PATH_TRANSPORT_DIRECT_ONLY = 1u;
const uint PATH_TRANSPORT_SINGLE_DIFFUSE_BOUNCE = 2u;
const uint PATH_TRANSPORT_TWO_DIFFUSE_BOUNCES = 3u;

// 空が無効なときの環境光（PathTracingEnvironmentModeと同じ値）。
const uint PATH_ENVIRONMENT_BLACK = 0u;
const uint PATH_ENVIRONMENT_UNIFORM = 1u;
const uint PATH_ENVIRONMENT_EQUIRECT = 2u;

// 検証出力。1次命中面の値（材質値、または1次光線の始点からの距離）を放射輝度の代わりに書く。
const uint PATH_DEBUG_NONE = 0u;
const uint PATH_DEBUG_ALBEDO = 1u;
const uint PATH_DEBUG_SHADING_NORMAL = 2u;
const uint PATH_DEBUG_METALLIC_ROUGHNESS = 3u;
const uint PATH_DEBUG_HIT_DISTANCE = 4u;

struct PathPayload
{
    vec3 Position;
    uint Hit;
    vec3 GeometricNormal; // レイに向けた三角形の面法線
    uint InstanceIndex;
    vec3 ShadingNormal; // 頂点法線の補間と法線マップを適用し、面法線と同じ側へ向けた法線
    uint PrimitiveIndex;
    vec3 Albedo; // instance色×アルベドtexture（GBufferと同じ規則）
    float Metallic;
    vec3 Emission; // 色×nits（プリエクスポージャ前の物理値）
    float Roughness;
    float TriangleArea; // ワールド空間の三角形面積
};
