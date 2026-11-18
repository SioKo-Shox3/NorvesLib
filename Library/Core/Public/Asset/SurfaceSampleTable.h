#pragma once
// LOD0 の表面から選んだ標本を、頂点番号と重心座標で保持する。
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    inline constexpr uint32_t SurfaceSampleRecordBytes = 16;
    inline constexpr uint32_t SurfaceSampleVertexIndexBits = 21;
    inline constexpr uint32_t SurfaceSampleMaxVertexIndex = (1u << SurfaceSampleVertexIndexBits) - 1u;
    inline constexpr uint32_t SurfaceSampleBarycentricOne = 65535;
    inline constexpr uint32_t MaximumSurfaceSampleCount = 1u << 20;
    inline constexpr uint32_t DefaultSurfaceSampleCount = 16384;

    // Words[0..1] は 3 頂点の番号、Words[2] は b1・b2、Words[3] は R が最下位の RGBA8。
    // 位置はバインド姿勢・メッシュ空間の b0 * P[v0] + b1 * P[v1] + b2 * P[v2]。
    // b0 = 1 - b1 - b2。アルベドは基本色テクスチャと同じ sRGB の符号化のまま保持する。
    struct SurfaceSampleRecord
    {
        uint32_t Words[4];
    };
    static_assert(sizeof(SurfaceSampleRecord) == SurfaceSampleRecordBytes);

    // 各頂点番号は SurfaceSampleMaxVertexIndex 以下で渡す。範囲外の上位ビットは保持されない。
    [[nodiscard]] inline constexpr SurfaceSampleRecord PackSurfaceSampleRecord(
        uint32_t v0, uint32_t v1, uint32_t v2, uint16_t b1, uint16_t b2, uint32_t rgba8) noexcept
    {
        return {{(v0 & SurfaceSampleMaxVertexIndex) | ((v1 & 0x7ffu) << 21),
                 ((v1 >> 11) & 0x3ffu) | ((v2 & SurfaceSampleMaxVertexIndex) << 10),
                 uint32_t(b1) | (uint32_t(b2) << 16), rgba8}};
    }

    inline constexpr void UnpackSurfaceSampleVertexIndices(const SurfaceSampleRecord& record, uint32_t& v0,
                                                            uint32_t& v1, uint32_t& v2) noexcept
    {
        v0 = record.Words[0] & SurfaceSampleMaxVertexIndex;
        v1 = (record.Words[0] >> 21) | ((record.Words[1] & 0x3ffu) << 11);
        v2 = (record.Words[1] >> 10) & SurfaceSampleMaxVertexIndex;
    }

    inline constexpr void UnpackSurfaceSampleBarycentrics(const SurfaceSampleRecord& record, float& b0, float& b1,
                                                           float& b2) noexcept
    {
        const uint32_t q1 = record.Words[2] & 0xffffu;
        const uint32_t q2 = record.Words[2] >> 16;
        b0 = float(int32_t(SurfaceSampleBarycentricOne) - int32_t(q1) - int32_t(q2)) /
             float(SurfaceSampleBarycentricOne);
        b1 = float(q1) / float(SurfaceSampleBarycentricOne);
        b2 = float(q2) / float(SurfaceSampleBarycentricOne);
    }

    [[nodiscard]] inline constexpr bool IsValidSurfaceSampleRecord(const SurfaceSampleRecord& record,
                                                                    uint32_t vertexCount) noexcept
    {
        uint32_t v0 = 0, v1 = 0, v2 = 0;
        UnpackSurfaceSampleVertexIndices(record, v0, v1, v2);
        const uint32_t b1 = record.Words[2] & 0xffffu;
        const uint32_t b2 = record.Words[2] >> 16;
        return v0 < vertexCount && v1 < vertexCount && v2 < vertexCount &&
               b1 + b2 <= SurfaceSampleBarycentricOne && (record.Words[1] & 0x80000000u) == 0;
    }
} // namespace NorvesLib::Core::Skeletal
