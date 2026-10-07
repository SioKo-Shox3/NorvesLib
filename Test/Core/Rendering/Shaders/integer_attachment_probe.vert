#version 450

// 整数の添付の確認用の頂点シェーダー（IntegerAttachmentVulkanTest）
//
// 頂点バッファなしで、1 回の描画に 6 個の三角形を出す。三角形 t は、描画先の (列 t, 行 r) の 4x4 画素のセルの
// 上辺（NDC の y が小さい側）を底辺に、下辺の中央を頂点にした二等辺三角形（セルの中の画素 (2, 0) を必ず覆い、画素 (0, 3) は覆わない）。
// 行 r と描画の番号 d は、描画の開始インスタンス（gl_InstanceIndex）に (r << 16) | d で渡す。
// 描画の番号 d は flat で fragment へ渡す。fragment は (d << 7) | gl_PrimitiveID を ID として書く。

layout(location = 0) flat out uint vDrawNumber;

const uint TRIANGLES_PER_DRAW = 6u;
const uint ROW_COUNT = 2u;

void main()
{
    uint triangle = uint(gl_VertexIndex) / 3u;
    uint corner = uint(gl_VertexIndex) % 3u;
    uint row = uint(gl_InstanceIndex) >> 16;
    uint drawNumber = uint(gl_InstanceIndex) & 0xFFFFu;

    float x0 = -1.0 + 2.0 * float(triangle) / float(TRIANGLES_PER_DRAW);
    float x1 = -1.0 + 2.0 * float(triangle + 1u) / float(TRIANGLES_PER_DRAW);
    float y0 = -1.0 + 2.0 * float(row) / float(ROW_COUNT);
    float y1 = -1.0 + 2.0 * float(row + 1u) / float(ROW_COUNT);

    vec2 position = vec2(x0, y0);
    if (corner == 1u)
    {
        position = vec2(x1, y0);
    }
    else if (corner == 2u)
    {
        position = vec2(0.5 * (x0 + x1), y1);
    }

    gl_Position = vec4(position, 0.0, 1.0);
    vDrawNumber = drawNumber;
}
