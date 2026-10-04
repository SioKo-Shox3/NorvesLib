#pragma once

// VT のフィードバック（材質のサンプルの箇所が書いた、欲しいタイルの要求）の詰め方と、
// 読み戻した要求をテクスチャごとにまとめた集合。
// GPU が書くバッファの並びは、先頭 HeaderWords 語のヘッダと、その後ろの capacity 語の要求から成る:
//   [0]   書き込もうとした件数（atomicAdd の結果。capacity を超えた分も数える）
//   [1-3] 予約（0）
//   [4..] 要求 1 件 = 32bit 1 語（Pack の並び）
// 要求の語は、テクスチャの番号に +1 して詰めるので、0 は「書かれていない」を表す。
// 時刻もデバイスも持たない計算だけの型なので、GPU を使わずにテストできる。

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    /** @brief 1 枚のテクスチャの 1 タイルを指す印 */
    struct VirtualTextureTileKey
    {
        /** @brief VT の表の添字（テクスチャの番号） */
        uint32_t TextureIndex = 0;
        uint32_t Mip = 0;
        uint32_t X = 0;
        uint32_t Y = 0;

        bool operator==(const VirtualTextureTileKey &other) const
        {
            return TextureIndex == other.TextureIndex && Mip == other.Mip && X == other.X && Y == other.Y;
        }
    };

    namespace VirtualTextureFeedback
    {
        /** @brief 要求のバッファの既定の件数（64K 件） */
        constexpr uint32_t DefaultCapacity = 65536;
        /** @brief 要求の前にあるヘッダの語数 */
        constexpr uint32_t HeaderWords = 4;

        // 語の並び（上位から）: テクスチャの番号 + 1（12bit）・ミップ（4bit）・y（8bit）・x（8bit）
        constexpr uint32_t TextureBits = 12;
        constexpr uint32_t MipBits = 4;
        constexpr uint32_t TileBits = 8;
        /** @brief 詰められるテクスチャの番号の最大（番号 + 1 が 12bit に収まり、0 を空きに使う） */
        constexpr uint32_t MaxTextureIndex = (1u << TextureBits) - 2u;
        constexpr uint32_t MaxMip = (1u << MipBits) - 1u;
        constexpr uint32_t MaxTileCoord = (1u << TileBits) - 1u;

        /** @brief 要求のバッファ全体の大きさ（バイト） */
        constexpr uint64_t GetBufferBytes(uint32_t capacity)
        {
            return (static_cast<uint64_t>(HeaderWords) + capacity) * sizeof(uint32_t);
        }

        /** @brief 印が 1 語に収まるか（テクスチャの番号・ミップ・x・y の範囲） */
        constexpr bool CanPack(const VirtualTextureTileKey &key)
        {
            return key.TextureIndex <= MaxTextureIndex && key.Mip <= MaxMip && key.X <= MaxTileCoord &&
                   key.Y <= MaxTileCoord;
        }

        /** @brief 印を 1 語へ詰める。収まらないときは 0（書かれていない語と同じ）を返す */
        constexpr uint32_t Pack(const VirtualTextureTileKey &key)
        {
            if (!CanPack(key))
            {
                return 0;
            }
            return ((key.TextureIndex + 1u) << (MipBits + 2u * TileBits)) | (key.Mip << (2u * TileBits)) |
                   (key.Y << TileBits) | key.X;
        }

        /** @brief 語を印へ戻す。書かれていない語（テクスチャの欄が 0）は false */
        constexpr bool Unpack(uint32_t word, VirtualTextureTileKey &outKey)
        {
            const uint32_t textureField = word >> (MipBits + 2u * TileBits);
            if (textureField == 0)
            {
                return false;
            }
            outKey.TextureIndex = textureField - 1u;
            outKey.Mip = (word >> (2u * TileBits)) & MaxMip;
            outKey.Y = (word >> TileBits) & MaxTileCoord;
            outKey.X = word & MaxTileCoord;
            return true;
        }
    } // namespace VirtualTextureFeedback

    /** @brief 集合の中の 1 タイルの要求（テクスチャは外側のキーが持つ） */
    struct VirtualTextureTileRequest
    {
        uint32_t Mip = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
        /** @brief このタイルを最後に要求したフレーム */
        uint64_t LastRequestedFrame = 0;
    };

    /** @brief バッファ 1 枚を取り込んだときの内訳 */
    struct VirtualTextureFeedbackDecodeResult
    {
        /** @brief ヘッダの件数（GPU が書こうとした数。capacity を超えた分を含む） */
        uint32_t Attempted = 0;
        /** @brief バッファに実際に入っていた件数（Attempted を capacity で止めた値） */
        uint32_t Stored = 0;
        /** @brief 復号できた件数 */
        uint32_t Decoded = 0;
        /** @brief 復号できた件数のうち、集合に既にあったタイルの数 */
        uint32_t Duplicates = 0;
        /** @brief 入っていたはずなのに復号できなかった語の数（書かれていない 0） */
        uint32_t Invalid = 0;
        /** @brief 溢れて捨てられた件数（Attempted が capacity を超えた分） */
        uint64_t Overflow = 0;
    };

    /**
     * @brief テクスチャごとのタイルの要求の集合
     *
     * 同じタイルの要求は 1 つにまとめ、最後に要求したフレームを持つ。フレームは増える向きにだけ更新する
     * （古いバッファが後から取り込まれても、新しい値を巻き戻さない）。
     */
    class VirtualTextureRequestSet
    {
    public:
        /**
         * @brief GPU が書いたバッファ（ヘッダ + capacity 語）を復号して集合へ足す
         * @param words バッファの先頭。null のときは何もしない
         * @param capacity バッファの要求の件数（ヘッダを除く）
         * @param frame このバッファを書いたフレーム
         */
        VirtualTextureFeedbackDecodeResult AddFeedbackBuffer(const uint32_t *words, uint32_t capacity, uint64_t frame)
        {
            VirtualTextureFeedbackDecodeResult result;
            if (words == nullptr)
            {
                return result;
            }

            result.Attempted = words[0];
            result.Stored = result.Attempted < capacity ? result.Attempted : capacity;
            result.Overflow = result.Attempted > capacity ? static_cast<uint64_t>(result.Attempted - capacity) : 0;
            m_OverflowCount += result.Overflow;

            const uint32_t *entries = words + VirtualTextureFeedback::HeaderWords;
            for (uint32_t i = 0; i < result.Stored; ++i)
            {
                VirtualTextureTileKey key;
                if (!VirtualTextureFeedback::Unpack(entries[i], key))
                {
                    ++result.Invalid;
                    continue;
                }
                ++result.Decoded;
                if (!Add(key, frame))
                {
                    ++result.Duplicates;
                }
            }
            return result;
        }

        /**
         * @brief 1 タイルの要求を足す。新しいタイルなら true、既にあれば最後のフレームだけ進めて false
         *
         * 1 語に収まらない印（CanPack が false）は、別のタイルと区別できなくなるので足さない（false）。
         */
        bool Add(const VirtualTextureTileKey &key, uint64_t frame)
        {
            if (!VirtualTextureFeedback::CanPack(key))
            {
                return false;
            }
            TileMap &tiles = m_Textures[key.TextureIndex];
            const uint32_t tileKey = MakeTileKey(key);
            auto it = tiles.find(tileKey);
            if (it == tiles.end())
            {
                tiles.emplace(tileKey, frame);
                ++m_RequestCount;
                return true;
            }
            if (frame > it->second)
            {
                it->second = frame;
            }
            return false;
        }

        /** @brief 別の集合を取り込む（溢れた件数も足す） */
        void Merge(const VirtualTextureRequestSet &other)
        {
            for (const auto &texture : other.m_Textures)
            {
                for (const auto &tile : texture.second)
                {
                    VirtualTextureTileKey key;
                    key.TextureIndex = texture.first;
                    key.Mip = (tile.first >> (2u * VirtualTextureFeedback::TileBits)) & VirtualTextureFeedback::MaxMip;
                    key.Y = (tile.first >> VirtualTextureFeedback::TileBits) & VirtualTextureFeedback::MaxTileCoord;
                    key.X = tile.first & VirtualTextureFeedback::MaxTileCoord;
                    Add(key, tile.second);
                }
            }
            m_OverflowCount += other.m_OverflowCount;
        }

        void Clear()
        {
            m_Textures.clear();
            m_RequestCount = 0;
            m_OverflowCount = 0;
        }

        /** @brief 要求も溢れた件数も無いか */
        bool IsEmpty() const { return m_RequestCount == 0 && m_OverflowCount == 0; }

        /** @brief 要求のあるテクスチャの数 */
        uint32_t GetTextureCount() const { return static_cast<uint32_t>(m_Textures.size()); }

        /** @brief 集合のタイルの数（同じタイルは 1 つ） */
        uint32_t GetRequestCount() const { return m_RequestCount; }

        /** @brief 溢れて捨てられた件数の合計 */
        uint64_t GetOverflowCount() const { return m_OverflowCount; }

        /** @brief タイルの要求があれば、最後に要求したフレームを返す */
        bool Find(const VirtualTextureTileKey &key, uint64_t &outFrame) const
        {
            const auto texture = m_Textures.find(key.TextureIndex);
            if (texture == m_Textures.end())
            {
                return false;
            }
            const auto tile = texture->second.find(MakeTileKey(key));
            if (tile == texture->second.end())
            {
                return false;
            }
            outFrame = tile->second;
            return true;
        }

        /** @brief 要求のあるテクスチャの番号（順序は不定） */
        Container::VariableArray<uint32_t> GetTextureIndices() const
        {
            Container::VariableArray<uint32_t> indices;
            indices.reserve(m_Textures.size());
            for (const auto &texture : m_Textures)
            {
                indices.push_back(texture.first);
            }
            return indices;
        }

        /** @brief 1 枚のテクスチャのタイルの要求（順序は不定。無ければ空） */
        Container::VariableArray<VirtualTextureTileRequest> GetRequests(uint32_t textureIndex) const
        {
            Container::VariableArray<VirtualTextureTileRequest> requests;
            const auto texture = m_Textures.find(textureIndex);
            if (texture == m_Textures.end())
            {
                return requests;
            }
            requests.reserve(texture->second.size());
            for (const auto &tile : texture->second)
            {
                VirtualTextureTileRequest request;
                request.Mip = (tile.first >> (2u * VirtualTextureFeedback::TileBits)) & VirtualTextureFeedback::MaxMip;
                request.Y = (tile.first >> VirtualTextureFeedback::TileBits) & VirtualTextureFeedback::MaxTileCoord;
                request.X = tile.first & VirtualTextureFeedback::MaxTileCoord;
                request.LastRequestedFrame = tile.second;
                requests.push_back(request);
            }
            return requests;
        }

    private:
        // テクスチャの欄を除いた語（ミップ・y・x）から最後に要求したフレームへ
        using TileMap = Container::UnorderedMap<uint32_t, uint64_t>;

        static constexpr uint32_t MakeTileKey(const VirtualTextureTileKey &key)
        {
            return (key.Mip << (2u * VirtualTextureFeedback::TileBits)) | (key.Y << VirtualTextureFeedback::TileBits) |
                   key.X;
        }

        Container::UnorderedMap<uint32_t, TileMap> m_Textures;
        uint32_t m_RequestCount = 0;
        uint64_t m_OverflowCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
