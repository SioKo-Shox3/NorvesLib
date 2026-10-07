#version 450

// 整数の添付（R32_UINT）の確認用のフラグメントシェーダー（IntegerAttachmentVulkanTest）
//
// 描画の番号（頂点シェーダーから flat で受ける）と gl_PrimitiveID から、ビジビリティバッファの ID と同じ並びの値
// 「描画の番号 << 7 | 描画の中の三角形の番号」を作って書く。

layout(location = 0) flat in uint vDrawNumber;
layout(location = 0) out uint outId;

void main()
{
    outId = (vDrawNumber << 7) | uint(gl_PrimitiveID);
}
