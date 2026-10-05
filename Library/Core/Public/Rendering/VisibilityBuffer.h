#pragma once

// ビジビリティバッファの ID と、フレームごとの描画の記録の表。
//
// 不透明の描画（MegaGeometry のクラスタ・手続きメッシュの塊・スキニングの塊）は、画素ごとに 32bit の ID を
// 資源 VisBuffer.Id（R32_UINT、画面の大きさ。深度は GBuffer.Depth を共有）へ書く。
//   ID = (記録の番号 << 7) | 記録の中の三角形の番号
// 記録は、その描画が何者か（種類・インスタンス・頂点とインデックスの在処・材質・前のフレームの情報）を持つ表の1行で、
// 材質の解決が ID から記録を引いて 3 頂点を読む。三角形の番号が 7bit なので、1つの記録が覆う三角形は 128 以下
// （MegaGeometry のクラスタ・MeshIndexChunks.h の塊と同じ大きさ）。
//
// ID が 0 のときは画素が何も描かれていない（空）。そのため表の 0 番は空の記録として予約し、実際の記録の番号は 1 から。
// 記録の番号は 25bit に収まる必要があり、表の枠は最大 2^25 個（0 番を含む）、実際に使える記録は 2^25 - 1 個。
//
// このファイルの定数・構造体の並びは Assets/Shaders/Common/VisibilityBuffer.glsl と一致していなければならない
// （VisibilityBufferEncodingTest が定数を照合する）。

#include "Container/Containers.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MeshIndexChunks.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/RHITypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace VisibilityBuffer
    {
        /** @brief ID の下位の、記録の中の三角形の番号のビット数 */
        constexpr uint32_t TRIANGLE_BITS = 7;
        /** @brief 1つの記録が覆える三角形の最大数（2^TRIANGLE_BITS） */
        constexpr uint32_t MAX_TRIANGLES_PER_RECORD = 1u << TRIANGLE_BITS;
        /** @brief ID の上位の、記録の番号のビット数 */
        constexpr uint32_t RECORD_BITS = 32 - TRIANGLE_BITS;
        /** @brief 表の枠の数の上限（0 番の空の記録を含む。記録の番号はこれ未満） */
        constexpr uint32_t RECORD_SLOT_LIMIT = 1u << RECORD_BITS;
        /** @brief 実際に使える記録の数の上限（0 番は空の記録として予約する） */
        constexpr uint32_t MAX_RECORD_COUNT = RECORD_SLOT_LIMIT - 1;
        /** @brief 画素が何も描かれていないことを表す ID */
        constexpr uint32_t EMPTY_ID = 0;

        static_assert(MAX_TRIANGLES_PER_RECORD == MESH_CHUNK_MAX_TRIANGLES,
                      "手続き・スキニングの塊の最大三角形数と ID の三角形のビット幅が一致しなければならない");
        static_assert(MAX_TRIANGLES_PER_RECORD == MegaGeometry::MAX_TRIANGLES_PER_CLUSTER,
                      "MegaGeometry のクラスタの最大三角形数と ID の三角形のビット幅が一致しなければならない");

        /** @brief 画素の ID が空（何も描かれていない）か */
        constexpr bool IsEmpty(uint32_t id)
        {
            return id == EMPTY_ID;
        }

        /** @brief 記録の番号が使える範囲（1 以上 RECORD_SLOT_LIMIT 未満）か */
        constexpr bool IsValidRecordNumber(uint32_t recordNumber)
        {
            return recordNumber != 0 && recordNumber < RECORD_SLOT_LIMIT;
        }

        /**
         * @brief 記録の番号と記録の中の三角形の番号から ID を作る
         * @return 範囲外（記録の番号が 0・2^25 以上、三角形の番号が 128 以上）なら false で outId は触らない
         */
        constexpr bool TryEncode(uint32_t recordNumber, uint32_t triangleIndex, uint32_t& outId)
        {
            if (!IsValidRecordNumber(recordNumber) || triangleIndex >= MAX_TRIANGLES_PER_RECORD)
            {
                return false;
            }
            outId = (recordNumber << TRIANGLE_BITS) | triangleIndex;
            return true;
        }

        /**
         * @brief ID から記録の番号と記録の中の三角形の番号を取り出す
         * @return 空の ID（0）なら false で出力は触らない
         */
        constexpr bool TryDecode(uint32_t id, uint32_t& outRecordNumber, uint32_t& outTriangleIndex)
        {
            if (IsEmpty(id))
            {
                return false;
            }
            outRecordNumber = id >> TRIANGLE_BITS;
            outTriangleIndex = id & (MAX_TRIANGLES_PER_RECORD - 1);
            return true;
        }

        /** @brief 描画の記録が表す描画の種類（GLSL の VIS_KIND_* と一致） */
        enum class RecordKind : uint32_t
        {
            None = 0,
            /** @brief MegaGeometry のクラスタ */
            MegaGeometryCluster = 1,
            /** @brief 手続きメッシュの塊（MeshIndexChunk） */
            ProceduralChunk = 2,
            /** @brief スキニングの塊（MeshIndexChunk。頂点は計算シェーダーが変形したもの） */
            SkinnedChunk = 3,
        };

        /** @brief 描画の記録のフラグ（GLSL の VIS_RECORD_FLAG_* と一致） */
        constexpr uint32_t RECORD_FLAG_INDEX16 = 1u;

        /** @brief 前のフレームの変換が無いことを表す PreviousTransformIndex */
        constexpr uint32_t NO_PREVIOUS_TRANSFORM = 0xFFFFFFFFu;

        /**
         * @brief 描画の記録 1 行（storage buffer の 1 要素。GLSL の VisibilityDrawRecord と同じ 64 バイト）
         *
         * 三角形 t（0 以上 TriangleCount 未満）の k 番目の頂点は、
         *   頂点番号 = インデックス[FirstIndex + 3 * t + k] + VertexBase
         * で、頂点は VertexAddress（頂点の先頭のデバイスアドレス）から並ぶ。インデックスは IndexAddress から並び、
         * 既定は 32bit（RECORD_FLAG_INDEX16 があれば 16bit）。
         *
         * 前のフレームの情報は、静的・剛体の描画では PreviousTransformIndex（前のフレームの変換の表の添字）で、
         * スキニングでは PreviousVertexAddress（前のフレームに変形した頂点の先頭のアドレス。頂点番号は今と同じ）で表す。
         * 使わないほうは NO_PREVIOUS_TRANSFORM・0。アドレスは BufferDeviceAddress が使えないデバイスでは 0。
         */
        struct alignas(16) DrawRecord
        {
            /** @brief RecordKind の値 */
            uint32_t Kind = 0;
            /** @brief 種類ごとのインスタンスの表の中の番号 */
            uint32_t InstanceIndex = 0;
            /** @brief 材質の番号（材質の解決が材質ごとのタイルに分ける） */
            uint32_t MaterialIndex = 0;
            /** @brief この記録が覆う三角形の数（1 以上 MAX_TRIANGLES_PER_RECORD 以下） */
            uint32_t TriangleCount = 0;

            /** @brief インデックスの並びの中の最初のインデックスの位置（3 の倍数とは限らない。MegaGeometry の共有プールの区画の基点は 3 の倍数に整列しない） */
            uint32_t FirstIndex = 0;
            /** @brief インデックスから引いた値に足す頂点番号の基点 */
            uint32_t VertexBase = 0;
            uint32_t PreviousTransformIndex = NO_PREVIOUS_TRANSFORM;
            uint32_t Flags = 0;

            uint64_t VertexAddress = 0;
            uint64_t IndexAddress = 0;

            uint64_t PreviousVertexAddress = 0;
            uint64_t Reserved = 0;
        };

        static_assert(sizeof(DrawRecord) == 64, "VisibilityBuffer.glsl の VisibilityDrawRecord と一致しなければならない");
        static_assert(alignof(DrawRecord) == 16, "storage buffer の要素の配置は 16 バイト");
        static_assert(offsetof(DrawRecord, FirstIndex) == 16 && offsetof(DrawRecord, VertexAddress) == 32 &&
                          offsetof(DrawRecord, PreviousVertexAddress) == 48,
                      "VisibilityBuffer.glsl の uvec4 の並びと一致しなければならない");

        /** @brief 記録が表の行として使える形か（種類が決まっていて、三角形が 1 以上 128 以下） */
        constexpr bool IsValidRecord(const DrawRecord& record)
        {
            return record.Kind >= static_cast<uint32_t>(RecordKind::MegaGeometryCluster) &&
                   record.Kind <= static_cast<uint32_t>(RecordKind::SkinnedChunk) && record.TriangleCount != 0 &&
                   record.TriangleCount <= MAX_TRIANGLES_PER_RECORD;
        }

        /**
         * @brief フレームごとの描画の記録の表（CPU 側の積み場所。GPU へは Data()・SizeInBytes() をそのまま書く）
         *
         * 0 番は空の記録（ID の 0 に対応）で、Clear() が置く。Add は 1 番から順に割り当てる。
         * 容量を超えたとき・使えない記録のときは 0（空）を返し、数えておく。
         */
        class RecordTable final
        {
        public:
            /** @param maxRecords 使える記録の数の上限（MAX_RECORD_COUNT を超えない。負荷の測定やテストで小さくできる） */
            explicit RecordTable(uint32_t maxRecords = MAX_RECORD_COUNT)
                : m_MaxRecords(maxRecords < MAX_RECORD_COUNT ? maxRecords : MAX_RECORD_COUNT)
            {
                Clear();
            }

            /** @brief 空の記録（0 番）だけにし、数え直す */
            void Clear()
            {
                m_Records.clear();
                m_Records.push_back(DrawRecord{});
                m_OverflowCount = 0;
                m_RejectedCount = 0;
            }

            /**
             * @brief 記録を足して、その記録の番号（1 以上）を返す
             * @return 容量を超えた・使えない記録なら 0
             */
            uint32_t Add(const DrawRecord& record)
            {
                if (!IsValidRecord(record))
                {
                    ++m_RejectedCount;
                    return 0;
                }
                if (RecordCount() >= m_MaxRecords)
                {
                    ++m_OverflowCount;
                    return 0;
                }
                const uint32_t recordNumber = static_cast<uint32_t>(m_Records.size());
                m_Records.push_back(record);
                return recordNumber;
            }

            /** @brief 記録を足して、その記録の三角形 triangleIndex の ID を返す。足せなかった・三角形が範囲外なら 0（空） */
            uint32_t AddAndEncode(const DrawRecord& record, uint32_t triangleIndex)
            {
                if (triangleIndex >= record.TriangleCount)
                {
                    ++m_RejectedCount;
                    return EMPTY_ID;
                }
                const uint32_t recordNumber = Add(record);
                uint32_t id = EMPTY_ID;
                return TryEncode(recordNumber, triangleIndex, id) ? id : EMPTY_ID;
            }

            /**
             * @brief ID から記録と記録の中の三角形の番号を引く
             * @return 空の ID・表に無い記録の番号・三角形の番号が記録の三角形数以上なら false で出力は触らない
             */
            bool TryResolve(uint32_t id, const DrawRecord*& outRecord, uint32_t& outTriangleIndex) const
            {
                uint32_t recordNumber = 0;
                uint32_t triangleIndex = 0;
                if (!TryDecode(id, recordNumber, triangleIndex) || recordNumber >= m_Records.size())
                {
                    return false;
                }
                const DrawRecord& record = m_Records[recordNumber];
                if (triangleIndex >= record.TriangleCount)
                {
                    return false;
                }
                outRecord = &record;
                outTriangleIndex = triangleIndex;
                return true;
            }

            /** @brief 使った記録の数（0 番の空の記録を含まない） */
            uint32_t RecordCount() const { return static_cast<uint32_t>(m_Records.size()) - 1; }
            /** @brief 表の枠の数（0 番を含む。GPU のバッファの要素数） */
            uint32_t SlotCount() const { return static_cast<uint32_t>(m_Records.size()); }
            uint32_t MaxRecords() const { return m_MaxRecords; }
            /** @brief 容量を超えて足せなかった回数 */
            uint32_t OverflowCount() const { return m_OverflowCount; }
            /** @brief 使えない記録で断った回数 */
            uint32_t RejectedCount() const { return m_RejectedCount; }

            const DrawRecord* Data() const { return m_Records.data(); }
            uint64_t SizeInBytes() const { return static_cast<uint64_t>(m_Records.size()) * sizeof(DrawRecord); }

        private:
            uint32_t m_MaxRecords = MAX_RECORD_COUNT;
            uint32_t m_OverflowCount = 0;
            uint32_t m_RejectedCount = 0;
            Container::VariableArray<DrawRecord> m_Records;
        };

        /** @brief VisBuffer.Id の RenderGraph の資源（R32_UINT。カラー添付で書き、材質の解決が読む） */
        inline RGTextureDesc MakeIdTextureDesc(uint32_t width, uint32_t height)
        {
            return RGTextureDesc::RenderTarget(width, height, RHI::Format::R32_UINT, "VisBuffer_Id");
        }

        /** @brief 描画の記録の表の RenderGraph の資源（storage buffer。slotCount は 0 番を含む枠の数） */
        inline RGBufferDesc MakeRecordTableBufferDesc(uint32_t slotCount)
        {
            RGBufferDesc desc;
            desc.Size = static_cast<uint64_t>(slotCount < 1 ? 1 : slotCount) * sizeof(DrawRecord);
            desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
            desc.bCPUAccessible = false;
            desc.DebugName = "VisBuffer_DrawRecords";
            return desc;
        }
    } // namespace VisibilityBuffer
} // namespace NorvesLib::Core::Rendering
