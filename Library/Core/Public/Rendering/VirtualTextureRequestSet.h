#pragma once

// VT のフィードバック（材質のサンプルの箇所が書いた、欲しいタイルの要求）の詰め方と、
// 読み戻した要求をテクスチャごとにまとめた集合。
// GPU が書くバッファの並びは、先頭 HeaderWords 語のヘッダと、その後ろの capacity 語の要求から成る:
//   [0]   書き込もうとした件数（atomicAdd の結果。capacity を超えた分も数える）
//   [1-3] 予約（0）
//   [4..] 要求 1 件 = 32bit 1 語（Pack の並び）
//   [4 + capacity ..] 重複を減らす小さなハッシュの表（HashWords 語。要求の語そのもの）
//   [4 + capacity + HashWords ..] ハッシュの表の各枠の「2 件目以降の要求の件数」（HashWords 語。同じ枠の語に重なった要求の数）
//                    読み戻しは、要求の列の語を 1 件ずつ数え、この件数を同じ枠のタイルへ足す（AddFeedbackRepeatCounts）。
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
        /** @brief 要求の後ろに続く、重複を減らすハッシュの表の語数（2 の冪。シェーダーの VT_FEEDBACK_HASH_WORDS と同じ） */
        constexpr uint32_t HashWords = 4096;
        /** @brief ハッシュの表で、衝突したときに次の語へ進んで探す回数の上限（超えたら重複を許して書く） */
        constexpr uint32_t HashProbeLimit = 8;

        // 語の並び（上位から）: テクスチャの番号 + 1（12bit）・ミップ（4bit）・y（8bit）・x（8bit）
        constexpr uint32_t TextureBits = 12;
        constexpr uint32_t MipBits = 4;
        constexpr uint32_t TileBits = 8;
        /** @brief 詰められるテクスチャの番号の最大（番号 + 1 が 12bit に収まり、0 を空きに使う） */
        constexpr uint32_t MaxTextureIndex = (1u << TextureBits) - 2u;
        constexpr uint32_t MaxMip = (1u << MipBits) - 1u;
        constexpr uint32_t MaxTileCoord = (1u << TileBits) - 1u;

        /** @brief 要求の後ろに続く、ハッシュの表と件数の表を合わせた語数（シェーダーの 2 * VT_FEEDBACK_HASH_WORDS） */
        constexpr uint32_t TableWords = HashWords * 2u;

        /** @brief 要求のバッファ全体の大きさ（バイト。ヘッダ + 要求 + ハッシュの表 + 件数の表） */
        constexpr uint64_t GetBufferBytes(uint32_t capacity)
        {
            return (static_cast<uint64_t>(HeaderWords) + capacity + TableWords) * sizeof(uint32_t);
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

        // ---- 材質の UBO がシェーダーへ渡すパラメータ（float の1要素に収めるので 24bit。0 は「要求を書かない」） ----
        // 下位から: タイル幅の log2（4bit）・タイル高さの log2（4bit）・フレームの巡回位相（4bit）・テクスチャの番号 + 1（12bit）。
        // シェーダーの Common/VirtualTextureFeedback.glsl が同じ並びで読む。
        constexpr uint32_t ParamLog2Bits = 4;
        constexpr uint32_t ParamPhaseBits = 4;
        constexpr uint32_t ParamPhaseShift = 2u * ParamLog2Bits;
        constexpr uint32_t ParamTextureShift = ParamPhaseShift + ParamPhaseBits;
        /** @brief 4×4 の画素のうち、フレームごとに巡回する位相の数 */
        constexpr uint32_t PhaseCount = 16;

        /** @brief 2 の冪の値の log2。2 の冪でない・0・範囲外は 0xFFFFFFFF */
        constexpr uint32_t Log2OfPowerOfTwo(uint32_t value)
        {
            if (value == 0 || (value & (value - 1u)) != 0)
            {
                return 0xFFFFFFFFu;
            }
            uint32_t log2 = 0;
            while ((value >> log2) > 1u)
            {
                ++log2;
            }
            return log2;
        }

        /**
         * @brief 材質の UBO のパラメータを詰める
         * @param textureIndex VT の表の添字
         * @param tileWidth・tileHeight 形式の標準ブロック形状（texel。2 の冪）
         * @param frame フレームの番号（下位 4bit が巡回の位相になる）
         * @return 詰めた値（24bit。float に正確に載る）。収まらないとき（テクスチャの番号・タイルの大きさ）は 0
         */
        constexpr uint32_t PackMaterialParam(uint32_t textureIndex, uint32_t tileWidth, uint32_t tileHeight, uint64_t frame)
        {
            const uint32_t log2Width = Log2OfPowerOfTwo(tileWidth);
            const uint32_t log2Height = Log2OfPowerOfTwo(tileHeight);
            if (textureIndex > MaxTextureIndex || log2Width >= (1u << ParamLog2Bits) || log2Height >= (1u << ParamLog2Bits))
            {
                return 0;
            }
            return ((textureIndex + 1u) << ParamTextureShift) | (static_cast<uint32_t>(frame % PhaseCount) << ParamPhaseShift) |
                   (log2Height << ParamLog2Bits) | log2Width;
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
        /**
         * @brief 集合に取り込んだ要求の件数（同じタイルの要求を足し合わせた数）
         *
         * 復号した要求 1 語につき 1 件を足し、GPU のハッシュの表で重なった要求の件数（AddFeedbackRepeatCounts）も足す。
         * 実際のバッファでは、そのタイルを要求した画素の数（画面に占める大きさ。ただし書く画素は巡回の位相の画素と
         * 非常駐で逃げた画素だけ）になる。読み込みの優先度で、同じミップの中の順に使う。
         */
        uint32_t HitCount = 0;
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
                if (!Add(key, frame, 1))
                {
                    ++result.Duplicates;
                }
            }
            return result;
        }

        /**
         * @brief ハッシュの表の件数（重なった要求の数）を、同じバッファの要求として既に集合にあるタイルの件数へ足す
         *
         * AddFeedbackBuffer の後に、同じバッファで呼ぶ。件数の表の枠が 0 でなければ、同じ枠のハッシュの表の語を印へ戻し、
         * 集合に既にあるタイルの件数へ足す（要求の列が溢れて捨てられたタイルは、捨てたままにして新しく足さない）。
         * @param words バッファの先頭。null のときは何もしない
         * @param capacity バッファの要求の件数（ヘッダを除く）
         * @return 足した件数の合計
         */
        uint64_t AddFeedbackRepeatCounts(const uint32_t *words, uint32_t capacity)
        {
            if (words == nullptr)
            {
                return 0;
            }
            const uint32_t *hashTable = words + VirtualTextureFeedback::HeaderWords + capacity;
            const uint32_t *countTable = hashTable + VirtualTextureFeedback::HashWords;
            uint64_t added = 0;
            for (uint32_t i = 0; i < VirtualTextureFeedback::HashWords; ++i)
            {
                const uint32_t repeats = countTable[i];
                VirtualTextureTileKey key;
                if (repeats == 0 || !VirtualTextureFeedback::Unpack(hashTable[i], key))
                {
                    continue;
                }
                const auto texture = m_Textures.find(key.TextureIndex);
                if (texture == m_Textures.end())
                {
                    continue;
                }
                const auto tile = texture->second.find(MakeTileKey(key));
                if (tile == texture->second.end())
                {
                    continue;
                }
                tile->second.Hits = AddSaturated(tile->second.Hits, repeats);
                added += repeats;
            }
            return added;
        }

        /**
         * @brief 1 タイルの要求を足す。新しいタイルなら true、既にあれば最後のフレームを進めて件数を足して false
         *
         * 1 語に収まらない印（CanPack が false）は、別のタイルと区別できなくなるので足さない（false）。
         * @param hits このタイルの要求の件数（件数の合計は uint32_t で頭打ちにする）
         */
        bool Add(const VirtualTextureTileKey &key, uint64_t frame, uint32_t hits = 1)
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
                tiles.emplace(tileKey, TileEntry{frame, hits});
                ++m_RequestCount;
                return true;
            }
            if (frame > it->second.Frame)
            {
                it->second.Frame = frame;
            }
            it->second.Hits = AddSaturated(it->second.Hits, hits);
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
                    Add(key, tile.second.Frame, tile.second.Hits);
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
            outFrame = tile->second.Frame;
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
                request.LastRequestedFrame = tile.second.Frame;
                request.HitCount = tile.second.Hits;
                requests.push_back(request);
            }
            return requests;
        }

    private:
        struct TileEntry
        {
            uint64_t Frame = 0;
            uint32_t Hits = 0;
        };

        // テクスチャの欄を除いた語（ミップ・y・x）から、最後に要求したフレームと要求の件数へ
        using TileMap = Container::UnorderedMap<uint32_t, TileEntry>;

        static constexpr uint32_t AddSaturated(uint32_t a, uint32_t b)
        {
            return a > 0xFFFFFFFFu - b ? 0xFFFFFFFFu : a + b;
        }

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
