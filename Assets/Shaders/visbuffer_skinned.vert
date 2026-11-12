#version 450
#extension GL_GOOGLE_include_directive : require

// ========================================
// ビジビリティバッファ: スキニングの塊を描く頂点シェーダー（位置だけを読む）
//
// 頂点は skinning_compute.comp が変形してワールド空間で書いた列（1 頂点 8 個の float: 位置 3・法線 3・UV 2）で、
// 変換を掛け直さない。塊ごとに 1 回の描画で、開始インスタンスが記録の番号、ベース頂点が出力の中の先頭の頂点番号
// （gl_VertexIndex = インデックス + ベース頂点）。
// ========================================

layout(set = 0, binding = 0) uniform VisFrame
{
    mat4 view;
    mat4 projection;
    vec4 vertexAnimTime;
} frame;

layout(std430, set = 0, binding = 1) readonly buffer SkinnedVertices
{
    float values[];
} vertices;

#define VIS_RECORD_TABLE_SET 0
#define VIS_RECORD_TABLE_BINDING 2
#define VIS_VERTEX_ANIM_PARAMS_TABLE_SET 0
#define VIS_VERTEX_ANIM_PARAMS_TABLE_BINDING 3
#include "Common/VisibilityBuffer.glsl"
#include "Common/VertexAnimation.glsl"

layout(location = 0) flat out uint outRecord;

void main()
{
    const uint base = uint(gl_VertexIndex) * 8u;
    vec4 worldPos = vec4(vertices.values[base + 0u], vertices.values[base + 1u], vertices.values[base + 2u], 1.0);
    const uint recordNumber = uint(gl_InstanceIndex);
    const vec4 params = VertexAnimationParamsForFrame(VisRecordVertexAnimParams(visRecords[recordNumber]),
                                                       frame.vertexAnimTime);
    worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, worldPos.xyz, params, frame.vertexAnimTime.x);
    gl_Position = frame.projection * frame.view * worldPos;
    outRecord = recordNumber;
}
