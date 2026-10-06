#version 450

// ========================================
// ビジビリティバッファ: 手続きメッシュの塊を描く頂点シェーダー（位置だけを読む）
//
// 塊ごとに 1 回の描画で、開始インスタンスが記録の番号。記録（記録の表の cpu 側の範囲）が持つインスタンスの番号で、
// 描画のインスタンスの表（gbuffer.vert と同じ InstanceBuffer）から変換を引く。
// 位置の計算は gbuffer.vert と同じ式・同じ順序（GBuffer の深度と同じ値になる）。
// ========================================

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform VisFrame
{
    mat4 view;
    mat4 projection;
} frame;

struct InstanceData
{
    mat4 world;
    mat4 previousWorld;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

#define VIS_RECORD_TABLE_SET 0
#define VIS_RECORD_TABLE_BINDING 2
#include "Common/VisibilityBuffer.glsl"

layout(location = 0) flat out uint outRecord;

void main()
{
    const uint recordNumber = uint(gl_InstanceIndex);
    const uint instanceIndex = VisRecordInstanceIndex(visRecords[recordNumber]);

    vec4 worldPos = instances[instanceIndex].world * vec4(inPosition, 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    outRecord = recordNumber;
}
