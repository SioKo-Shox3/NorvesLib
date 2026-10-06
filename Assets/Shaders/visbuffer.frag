#version 450

// ========================================
// ビジビリティバッファ: ID を書くフラグメントシェーダー
//
// ID = (記録の番号 << 7) | 三角形の番号。記録の番号は頂点シェーダーが描画ごとの値で渡し、三角形の番号は
// その描画（1 クラスタ・1 塊 = 128 三角形以下）の中の gl_PrimitiveID。
// ========================================

#include "Common/VisibilityBuffer.glsl"

layout(location = 0) flat in uint inRecord;
layout(location = 0) out uint outId;

void main()
{
    outId = VisEncodeId(inRecord, uint(gl_PrimitiveID));
}
