#version 450

// ========================================
// ビジビリティバッファ: MegaGeometry のクラスタを描く頂点シェーダー（位置だけを読む）
//
// 描くのは MegaGeometryPass のカリングが積んだ IndirectDraw コマンドで、コマンドの firstInstance がコマンドの通しの位置。
// 記録の番号は「1 + コマンドの通しの位置」で、記録の表の 1 番から、コマンドと同じ並びで置く
// （visbuffer_records.comp が、そのフレームのコマンドから記録を書く）。
// 位置の計算は megageometry.vert と同じ式・同じ順序（GBuffer の深度と同じ値になる）。
// ========================================

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform VisFrame
{
    mat4 view;
    mat4 projection;
} frame;

// インスタンスの表（cluster_cull.comp の MegaInstance と一致）。描画が引くのは変換だけ。
struct MegaInstance
{
    mat4 world;
    mat4 previousWorld;
    vec4 lodSphere;
    uvec4 clusterInfo;
    uvec4 drawInfo;
    uvec4 bvhInfo;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer
{
    MegaInstance instances[];
};

// コマンドごとの描画情報（x = インスタンスの番号, y = payload）。添え字は firstInstance
layout(std430, set = 0, binding = 2) readonly buffer DrawInfoBuffer
{
    uvec2 drawInfos[];
};

layout(location = 0) flat out uint outRecord;

void main()
{
    uvec2 drawInfo = drawInfos[gl_InstanceIndex];
    mat4 world = instances[drawInfo.x].world;

    vec4 worldPos = world * vec4(inPosition, 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    outRecord = uint(gl_InstanceIndex) + 1u;
}
