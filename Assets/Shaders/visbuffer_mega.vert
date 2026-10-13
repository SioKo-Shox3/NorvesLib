#version 450

// ========================================
// ビジビリティバッファ: MegaGeometry のクラスタを描く頂点シェーダー（位置だけを読む）
//
// 描くのは MegaGeometryPass のカリングが積んだ IndirectDraw コマンドで、記録の番号は「1 + firstInstance」。
// firstInstance は、詰めた経路では可視クラスタの詰めた並びの位置 P（ソフトのラスタも同じ記録を引く）、
// 旧の経路（--mega-binning=off）ではコマンドの通しの位置（visbuffer_records.comp が、そのフレームの記録を書く）。
// 位置の計算は megageometry.vert と同じ式・同じ順序（GBuffer の深度と同じ値になる）。
// ========================================

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform VisFrame
{
    mat4 view;
    mat4 projection;
} frame;

// インスタンスの表（binding 1）とメッシュの表（binding 22）。描画が引くのは変換だけ。
#define MEGA_INSTANCE_BINDING 1
#define MEGA_MESH_BINDING 22
#include "Common/MegaInstanceTable.glsl"

// コマンドごとの描画情報（x = インスタンスの番号, y = payload）。添え字は firstInstance
layout(std430, set = 0, binding = 2) readonly buffer DrawInfoBuffer
{
    uvec2 drawInfos[];
};

layout(location = 0) flat out uint outRecord;

void main()
{
    uvec2 drawInfo = drawInfos[gl_InstanceIndex];
    mat4 world = LoadMegaInstance(drawInfo.x).world;

    vec4 worldPos = world * vec4(inPosition, 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    outRecord = uint(gl_InstanceIndex) + 1u;
}
