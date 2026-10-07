#version 450

// 整数の添付（R32G32_UINT）の確認用のフラグメントシェーダー（IntegerAttachmentVulkanTest）
//
// r に integer_attachment_probe.frag と同じ ID（描画の番号 << 7 | 三角形の番号）、g に描画の番号だけを書く。

layout(location = 0) flat in uint vDrawNumber;
layout(location = 0) out uvec2 outId;

void main()
{
    outId = uvec2((vDrawNumber << 7) | uint(gl_PrimitiveID), vDrawNumber);
}
