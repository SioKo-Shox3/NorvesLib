#pragma once

// カリングが書いたジオメトリのページの要求（GPU の要求のバッファ）を読み取り、ページごとに重複なく集める。
// バッファの並びは Common/MegaGeometryCull.glsl の PageRequestBuffer と同じ:
//   [0] 積もうとした数（容量を超えた分も数えるので、読むときは容量で頭打ちにする）  [1] 容量を超えて捨てた数  [2] 容量  [3] 予約
//   [GeometryPageRequestBuffer::HeaderWords ..] ページの表の位置（GeometryPageTable のグローバルな位置）

#include "Container/Containers.h"

#include <algorithm>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    namespace GeometryPageRequestBuffer
    {
        constexpr uint32_t HeaderWords = 4;
        constexpr uint32_t CountWord = 0;
        constexpr uint32_t OverflowWord = 1;
        constexpr uint32_t CapacityWord = 2;
        /** @brief 要求1枚のバッファの既定の容量（要求の件数。ヘッダを除く） */
        constexpr uint32_t DefaultCapacity = 4096;

        constexpr uint64_t GetBufferBytes(uint32_t capacity)
        {
            return (static_cast<uint64_t>(HeaderWords) + capacity) * sizeof(uint32_t);
        }
    } // namespace GeometryPageRequestBuffer

    /** @brief 要求のバッファ1枚を読み取った結果 */
    struct GeometryPageRequestDecodeResult
    {
        /** @brief 集合へ足した要求の数（バッファの中の件数） */
        uint32_t Accepted = 0;
        /** @brief 容量を超えて GPU が捨てた件数 */
        uint32_t Overflow = 0;
        /** @brief 容量やヘッダが不正で読まなかったとき true */
        bool bInvalid = false;
    };

    /**
     * @brief ページの要求の集合（ページの表の位置ごとに、最後に要求されたフレーム）
     *
     * 要求は表の位置だけを持つが、位置の範囲は解放の後に別のメッシュへ再利用される。
     * そこで要求ごとに、書いたフレームのシェーダーが見た表の版（TableVersion）を持ち、
     * 表の位置を引き直すときに、その版より後に割り当てられた範囲の要求を棄却できるようにする
     * （MegaGeometryResourceStore::ResolvePageTableIndex）。
     */
    class GeometryPageRequestSet
    {
    public:
        struct Request
        {
            /** @brief GeometryPageTable のグローバルな位置 */
            uint32_t TableIndex = 0;
            /** @brief このページを最後に要求したフレームの番号 */
            uint64_t LastRequestedFrame = 0;
            /** @brief 最後に要求したフレームのシェーダーが見たページの表の版（GeometryPageTable::GetVersion） */
            uint64_t TableVersion = 0;
        };

        /**
         * @brief 要求のバッファの中身を読んで足す
         * @param words バッファの全体（ヘッダ + capacity 語）
         * @param capacity バッファの容量（要求の件数）
         * @param frame このバッファを書いたフレームの番号
         * @param tableVersion そのフレームのシェーダーが見たページの表の版
         */
        GeometryPageRequestDecodeResult AddBuffer(const uint32_t *words, uint32_t capacity, uint64_t frame,
                                                  uint64_t tableVersion)
        {
            GeometryPageRequestDecodeResult result;
            if (words == nullptr || capacity == 0)
            {
                result.bInvalid = true;
                return result;
            }
            // GPU が書いた数が容量を超えていても、読むのは容量までにとどめる（別の値が書かれていても範囲外を読まない）
            const uint32_t count = std::min(words[GeometryPageRequestBuffer::CountWord], capacity);
            result.Overflow = words[GeometryPageRequestBuffer::OverflowWord];
            for (uint32_t index = 0; index < count; ++index)
            {
                Add(words[GeometryPageRequestBuffer::HeaderWords + index], frame, tableVersion);
                ++result.Accepted;
            }
            m_OverflowTotal += result.Overflow;
            return result;
        }

        /**
         * @brief 1件足す（同じ位置は、新しいフレームの要求とその版を残す）
         *
         * 表の版はフレームと共に進むので、新しいフレームの版が残れば、古い要求が指していた範囲が解放されていても
         * 新しい要求の持ち主で引き直せる。
         */
        void Add(uint32_t tableIndex, uint64_t frame, uint64_t tableVersion)
        {
            auto position = std::lower_bound(m_Requests.begin(), m_Requests.end(), tableIndex,
                                             [](const Request &request, uint32_t value) { return request.TableIndex < value; });
            if (position != m_Requests.end() && position->TableIndex == tableIndex)
            {
                if (frame >= position->LastRequestedFrame)
                {
                    position->LastRequestedFrame = frame;
                    position->TableVersion = std::max(position->TableVersion, tableVersion);
                }
                return;
            }
            Request request;
            request.TableIndex = tableIndex;
            request.LastRequestedFrame = frame;
            request.TableVersion = tableVersion;
            m_Requests.insert(position, request);
        }

        void Merge(const GeometryPageRequestSet &other)
        {
            for (const Request &request : other.m_Requests)
            {
                Add(request.TableIndex, request.LastRequestedFrame, request.TableVersion);
            }
            m_OverflowTotal += other.m_OverflowTotal;
        }

        void Clear()
        {
            m_Requests.clear();
            m_OverflowTotal = 0;
        }

        bool IsEmpty() const { return m_Requests.empty() && m_OverflowTotal == 0; }

        /** @brief 要求（ページの表の位置の昇順） */
        const VariableArray<Request> &GetRequests() const { return m_Requests; }

        /** @brief 容量を超えて捨てられた件数の合計 */
        uint64_t GetOverflowTotal() const { return m_OverflowTotal; }

    private:
        VariableArray<Request> m_Requests;
        uint64_t m_OverflowTotal = 0;
    };
} // namespace NorvesLib::Core::Rendering::MegaGeometry
