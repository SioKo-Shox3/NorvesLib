// ========================================
// ビジビリティバッファの ID と描画の記録（Rendering/VisibilityBuffer.h と一致）
//
// 不透明の描画は画素ごとに 32bit の ID を VisBuffer.Id（R32_UINT）へ書く。
//   ID = (記録の番号 << 7) | 記録の中の三角形の番号
// 三角形の番号は、書く側（frag）では gl_PrimitiveID。記録の番号は描画ごとの値（頂点シェーダーの開始インスタンスなど）で渡す。
// ID が 0 のときは何も描かれていない。表の 0 番は空の記録で、実際の記録の番号は 1 から。
//
// 記録の表を読むシェーダーは、取り込む前に VIS_RECORD_TABLE_SET と VIS_RECORD_TABLE_BINDING を定義すると、
// 表（visRecords）の宣言と VisLoadRecord が使える。表を別の方法（buffer_reference など）で引くシェーダーは定義しない。
//
// 定数は `const uint 名前 = 値u;` の1行で書く（VisibilityBufferEncodingTest が C++ の定数と照合する）。
// ========================================

#ifndef NORVES_VISIBILITY_BUFFER_GLSL
#define NORVES_VISIBILITY_BUFFER_GLSL

const uint VIS_TRIANGLE_BITS = 7u;
const uint VIS_MAX_TRIANGLES_PER_RECORD = 128u;
const uint VIS_RECORD_BITS = 25u;
const uint VIS_RECORD_SLOT_LIMIT = 33554432u;
const uint VIS_EMPTY_ID = 0u;

// 描画の種類（VisibilityBuffer.h の RecordKind と一致）
const uint VIS_KIND_NONE = 0u;
const uint VIS_KIND_MEGA_CLUSTER = 1u;
const uint VIS_KIND_PROCEDURAL_CHUNK = 2u;
const uint VIS_KIND_SKINNED_CHUNK = 3u;

// 記録のフラグ（VisibilityBuffer.h の RECORD_FLAG_* と一致）
const uint VIS_RECORD_FLAG_INDEX16 = 1u;

// 前のフレームの変換が無い（VisibilityBuffer.h の NO_PREVIOUS_TRANSFORM と一致）
const uint VIS_NO_PREVIOUS_TRANSFORM = 4294967295u;

// ========================================
// 描画の記録（VisibilityBuffer.h の DrawRecord と同じ 64 バイト。uint64 は下位・上位の 2 語）
// ========================================

struct VisibilityDrawRecord
{
    uvec4 header;   // kind, instanceIndex, materialIndex, triangleCount
    uvec4 source;   // firstIndex, vertexBase, previousTransformIndex, flags
    uvec4 address;  // vertexAddress.xy, indexAddress.xy（x = 下位 32bit）
    uvec4 previous; // previousVertexAddress.xy, lodPayload, reserved
};

uint VisRecordKind(VisibilityDrawRecord r) { return r.header.x; }
uint VisRecordInstanceIndex(VisibilityDrawRecord r) { return r.header.y; }
uint VisRecordMaterialIndex(VisibilityDrawRecord r) { return r.header.z; }
uint VisRecordTriangleCount(VisibilityDrawRecord r) { return r.header.w; }
uint VisRecordFirstIndex(VisibilityDrawRecord r) { return r.source.x; }
uint VisRecordVertexBase(VisibilityDrawRecord r) { return r.source.y; }
uint VisRecordPreviousTransformIndex(VisibilityDrawRecord r) { return r.source.z; }
bool VisRecordHasIndex16(VisibilityDrawRecord r) { return (r.source.w & VIS_RECORD_FLAG_INDEX16) != 0u; }
uvec2 VisRecordVertexAddress(VisibilityDrawRecord r) { return r.address.xy; }
uvec2 VisRecordIndexAddress(VisibilityDrawRecord r) { return r.address.zw; }
uvec2 VisRecordPreviousVertexAddress(VisibilityDrawRecord r) { return r.previous.xy; }
// MegaGeometry のクラスタの描画番号の payload（LOD の段。カリングが書く drawInfo.y）。他の種類は 0
uint VisRecordLodPayload(VisibilityDrawRecord r) { return r.previous.z; }

// ========================================
// ID の符号化・復号
// ========================================

// 記録の番号が使える範囲（1 以上 VIS_RECORD_SLOT_LIMIT 未満）か
bool VisIsValidRecordNumber(uint recordNumber)
{
    return recordNumber != 0u && recordNumber < VIS_RECORD_SLOT_LIMIT;
}

// 記録の番号と記録の中の三角形の番号から ID を作る。範囲外（記録の番号が不正・三角形が 128 以上）なら空の ID
uint VisEncodeId(uint recordNumber, uint triangleIndex)
{
    if (!VisIsValidRecordNumber(recordNumber) || triangleIndex >= VIS_MAX_TRIANGLES_PER_RECORD)
    {
        return VIS_EMPTY_ID;
    }
    return (recordNumber << VIS_TRIANGLE_BITS) | triangleIndex;
}

bool VisIsEmpty(uint id) { return id == VIS_EMPTY_ID; }
uint VisRecordNumber(uint id) { return id >> VIS_TRIANGLE_BITS; }
uint VisTriangleIndex(uint id) { return id & (VIS_MAX_TRIANGLES_PER_RECORD - 1u); }

// 三角形 triangleIndex の k 番目（0..2）の頂点番号（インデックスに VertexBase を足した値）を求めるための、
// インデックスの並びの中の位置
uint VisTriangleIndexPosition(VisibilityDrawRecord r, uint triangleIndex, uint k)
{
    return VisRecordFirstIndex(r) + 3u * triangleIndex + k;
}

#if defined(VIS_RECORD_TABLE_SET) && defined(VIS_RECORD_TABLE_BINDING)
layout(std430, set = VIS_RECORD_TABLE_SET, binding = VIS_RECORD_TABLE_BINDING) readonly buffer VisibilityRecordTable
{
    VisibilityDrawRecord visRecords[];
};

// ID の記録を表から読む。空の ID・表の外の記録の番号のときは false
bool VisLoadRecord(uint id, out VisibilityDrawRecord record, out uint triangleIndex)
{
    record = VisibilityDrawRecord(uvec4(0u), uvec4(0u), uvec4(0u), uvec4(0u));
    triangleIndex = 0u;
    if (VisIsEmpty(id))
    {
        return false;
    }
    const uint recordNumber = VisRecordNumber(id);
    if (recordNumber >= visRecords.length())
    {
        return false;
    }
    record = visRecords[recordNumber];
    triangleIndex = VisTriangleIndex(id);
    return triangleIndex < VisRecordTriangleCount(record);
}
#endif

#endif // NORVES_VISIBILITY_BUFFER_GLSL
