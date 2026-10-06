// ========================================
// ビジビリティバッファの記録から、三角形の頂点番号と位置を引く（幾何の解決とソフトウェアラスタが共有する）
//
// 取り込む側が先に #version 450 以上・拡張 GL_EXT_buffer_reference と GL_EXT_buffer_reference_uvec2 の有効化と、
// Common/VisibilityBuffer.glsl の取り込み（記録の表を宣言するなら VIS_RECORD_TABLE_SET・VIS_RECORD_TABLE_BINDING を先に定義）を済ませる
// （#extension は宣言より前に書く必要があるので、この文書の中では有効にしない）。
// 記録の頂点・インデックスのアドレス（デバイスアドレス）から読むので、BufferDeviceAddress が無い装置では
// 記録のアドレスが 0 になり、どの関数も false を返す。
//
// 位置はローカル空間のまま返す。ワールド空間にする変換（インスタンスの表）は取り込む側が持つ。
// ========================================

#ifndef NORVES_VISIBILITY_TRIANGLE_FETCH_GLSL
#define NORVES_VISIBILITY_TRIANGLE_FETCH_GLSL

// 手続きメッシュ・MegaGeometry・スキニングの出力はどれも 1 頂点 8 個の float（位置 3・法線 3・UV 2）
struct VisVertex
{
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VisVertexArray
{
    VisVertex vertices[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VisIndexWords
{
    uint words[];
};

uint VisLoadIndex(VisIndexWords indices, bool bIndex16, uint position)
{
    if (bIndex16)
    {
        const uint word = indices.words[position >> 1];
        return (position & 1u) != 0u ? (word >> 16) : (word & 0xFFFFu);
    }
    return indices.words[position];
}

// 記録の頂点・インデックスのアドレスが使えるか（どちらかが 0 なら引けない）
bool VisRecordHasAddresses(VisibilityDrawRecord record)
{
    const uvec2 vertexAddress = VisRecordVertexAddress(record);
    const uvec2 indexAddress = VisRecordIndexAddress(record);
    return (vertexAddress.x | vertexAddress.y) != 0u && (indexAddress.x | indexAddress.y) != 0u;
}

// 三角形 triangleIndex の k 番目（0..2）の頂点の、頂点配列の中の番号（インデックスに VertexBase を足した値。VertexBase は符号つき）。
// 呼ぶ前に VisRecordHasAddresses が真であること
uint VisLoadTriangleVertexIndex(VisibilityDrawRecord record, VisIndexWords indices, uint triangleIndex, uint k)
{
    const uint index = VisLoadIndex(indices, VisRecordHasIndex16(record), VisTriangleIndexPosition(record, triangleIndex, k));
    return uint(int(index) + int(VisRecordVertexBase(record)));
}

// 三角形 triangleIndex の 3 頂点のローカル空間の位置（位置だけを読む。法線・UV には触れない）。
// アドレスが無いときは false（localPosition は 0）
bool VisLoadTrianglePositions(VisibilityDrawRecord record, uint triangleIndex, out vec3 localPosition[3])
{
    localPosition[0] = vec3(0.0);
    localPosition[1] = vec3(0.0);
    localPosition[2] = vec3(0.0);
    if (!VisRecordHasAddresses(record))
    {
        return false;
    }

    // reference 型の変数には const を付けられない
    VisIndexWords indices = VisIndexWords(VisRecordIndexAddress(record));
    VisVertexArray vertices = VisVertexArray(VisRecordVertexAddress(record));
    for (uint k = 0u; k < 3u; ++k)
    {
        const uint vertexIndex = VisLoadTriangleVertexIndex(record, indices, triangleIndex, k);
        localPosition[k] = vec3(vertices.vertices[vertexIndex].px,
                                vertices.vertices[vertexIndex].py,
                                vertices.vertices[vertexIndex].pz);
    }
    return true;
}

#endif // NORVES_VISIBILITY_TRIANGLE_FETCH_GLSL
