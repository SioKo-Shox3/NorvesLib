// パストレーサーのレイ生成・命中・不交差の各段が共有する定数ブロックとペイロード。

layout(set = 0, binding = 1, std140) uniform PathTracingParameters
{
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    uvec4 imageState; // xy=寸法、z=試料番号、w=インスタンス数
    vec4 skySunDirectionAndCosRadius;
    vec4 skyState; // x=プリエクスポージャ、y=太陽の事前露出照度、z=空有効、w=空要求
    vec4 fogDensityHeightFalloffAndEnabled; // xyz=R3密度・基準高さ・減衰率、w=霧有効
    vec4 fogColorAndPreExposure; // rgb=空欠落時の霧色、w=事前露出
    vec4 fogLightDirectionAndAnisotropy; // xyz=方向光の進行方向、w=HG異方性
    vec4 fogLightRadianceAndEnabled; // rgb=方向光放射輝度、w=散乱有効
    vec4 exposureAndDebug; // x=カメラのプリエクスポージャ、y=検証出力（0=放射輝度）
} parameters;

// 検証出力。1次命中面の材質値を放射輝度の代わりに書く。
const uint PATH_DEBUG_NONE = 0u;
const uint PATH_DEBUG_ALBEDO = 1u;
const uint PATH_DEBUG_SHADING_NORMAL = 2u;
const uint PATH_DEBUG_METALLIC_ROUGHNESS = 3u;

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
