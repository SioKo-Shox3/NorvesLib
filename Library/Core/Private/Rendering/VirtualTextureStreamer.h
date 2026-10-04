#pragma once

// VT（sparse の材質のテクスチャ）のストリーマ。
// フィードバックから集計したタイルの要求を受け取り、未常駐のタイルを優先度順に読み、物理ページを結び、
// ステージングのリング経由でコピーして常駐させる。読み込み・結び付け・コピーは窓口（インターフェース）越しに行うので、
// GPU もファイルも使わずにテストできる。
//
// 1 フレームの流れ（Update。RenderThread から、フレームのコマンドを開く前に呼ぶ）:
//   1) 要求を取り込む（範囲外・ミップテイルの要求は捨て、同じタイルは 1 つにまとめる）
//   2) 完了した読み込みを集める（失敗・大きさの食い違いは再試行の待ちへ）
//   3) ミップテイル、続いて優先度の高い順に読み込み済みのタイルについて、先にコピーをステージングへ積み、
//      積めたものだけページを結ぶ（1 回の BindSparse にまとめる。ミップテイルもタイルも同じ予算）
//   4) 優先度の高い順に、未常駐のタイルの読み込みを始める
// 結び付けの前にコピーを積むのは、結んだ時点でハードウェアの常駐判定が「常駐」になり、描画がそのページを読めて
// しまうため。積んだコピーは同じフレームのコマンドの先頭で記録される（アップローダが記録できる量の範囲でだけ積む）。
// どの段にも 1 フレームの上限があり、超える分は次のフレームへ持ち越す。

#include "Asset/CookedTextureFormat.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/VirtualTextureRequestSet.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /** @brief 1 タイルの読み込みの結果 */
    struct VirtualTextureTileReadResult
    {
        VirtualTextureTileKey Key;
        bool bSucceeded = false;
        /** @brief タイルのバイト列（CookedTextureTile の説明の並び。失敗のときは空） */
        Container::VariableArray<uint8_t> Data;
    };

    /**
     * @brief タイルの読み込みの窓口（テクスチャ 1 枚に 1 つ）
     *
     * 読み込みは呼び出し側のスレッドを待たせない。本番はジョブシステムの範囲読み、テストでは偽物。
     */
    class IVirtualTextureTileSource
    {
    public:
        virtual ~IVirtualTextureTileSource() = default;

        /** @brief タイルの読み込みを始める。始められなければ false（ジョブを積めない等） */
        virtual bool BeginRead(const VirtualTextureTileKey &key) = 0;

        /** @brief 完了した読み込み（失敗も含む）を out へ足す。完了したものは二度返さない */
        virtual void CollectCompleted(Container::VariableArray<VirtualTextureTileReadResult> &out) = 0;
    };

    /**
     * @brief 結び付けとコピーの窓口
     *
     * 本番は IDevice::BindSparse と TileUploader。どちらも RenderThread の直列化の下で呼ばれる。
     */
    class IVirtualTextureGpu
    {
    public:
        virtual ~IVirtualTextureGpu() = default;

        /** @brief 結び付け・外しを 1 回の提出で出す。不正があれば何も変えず false */
        virtual bool BindSparse(const RHI::SparseBindRequest &request) = 0;

        /** @brief テクスチャを最初の ShaderResource の状態へ遷移する依頼を積む（コピーより先） */
        virtual bool EnqueueInitialize(const RHI::TexturePtr &texture) = 0;

        /** @brief タイルの領域へのコピーを積む。リングに空きが無いなどで積めなければ false（後で出し直す） */
        virtual bool EnqueueTile(const RHI::TexturePtr &texture,
                                 const RHI::TextureRegionCopy &region,
                                 const void *data,
                                 uint64_t bytes) = 0;

        /** @brief 次のコマンドの記録で確実にコピーできる量の残り（バイト）。これを超えて積まない */
        virtual uint64_t GetCopyBytesAvailable() const = 0;

        /** @brief 最後に積んだ count 件（EnqueueInitialize・EnqueueTile の合計）を、記録する前に取り消す */
        virtual void DiscardEnqueued(uint32_t count) = 0;

        /** @brief 登録を解除するテクスチャ宛ての、まだ GPU へ出していない依頼を無効にする */
        virtual void AbandonTexture(const RHI::TexturePtr &texture) = 0;
    };

    struct VirtualTextureStreamerConfig
    {
        /** @brief 1 フレームに読み込みを始めるタイルの数 */
        uint32_t MaxReadsStartedPerFrame = 16;
        /** @brief 読み込み中と読み込み済み（まだ GPU へ渡していない）のタイルの数の上限 */
        uint32_t MaxReadsInFlight = 32;
        /** @brief 1 フレームにページを結ぶタイルとミップテイルの数（ミップテイルは 1 枚のテクスチャで 1 件） */
        uint32_t MaxBindsPerFrame = 32;
        /** @brief 1 フレームに積むコピーの数（ミップテイルは段ごとに 1 件） */
        uint32_t MaxCopiesPerFrame = 128;
        /**
         * @brief 1 フレームにコピーするデータの量（バイト）。TileUploader のフレームの上限以下にする
         *
         * タイルもミップテイルも数える。そのフレームの最初の 1 件だけは、この量を超えていても通す
         * （1 件がこれより大きいと永久に進まなくなるため）。
         */
        uint64_t MaxCopyBytesPerFrame = 4ull * 1024ull * 1024ull;
        /** @brief 読み込みに失敗したタイルを諦めるまでの失敗の回数 */
        uint32_t MaxRetries = 3;
        /** @brief 失敗から再試行を許すまでの待ち（フレーム）。失敗の回数を掛ける */
        uint32_t RetryDelayFrames = 30;
        /** @brief 要求が途絶えた未読み込みのタイルを忘れるまでのフレーム */
        uint64_t WantedMaxAgeFrames = 240;
    };

    /** @brief 登録する VT のテクスチャ */
    struct VirtualTextureRegistration
    {
        /** @brief sparse のテクスチャ（GetSparseInfo が返せること） */
        RHI::TexturePtr Texture;
        Asset::CookedTexturePixelFormat Format = Asset::CookedTexturePixelFormat::R8UNorm;
        /** @brief ミップ 0 の大きさ（texel） */
        uint32_t Width = 0;
        uint32_t Height = 0;
        Container::TSharedPtr<IVirtualTextureTileSource> Source;
        /** @brief ミップテイル（FirstTailMip 以降の段を行優先で詰めたもの）。ミップテイルの無いテクスチャは空 */
        Container::VariableArray<uint8_t> TailData;
    };

    enum class VirtualTextureTileState : uint8_t
    {
        /** @brief 記録が無い */
        None,
        /** @brief 要求されている。読み込みはまだ始めていない */
        Wanted,
        /** @brief 読み込み中 */
        Reading,
        /** @brief 読み込み済みで、ページの結び付けを待っている */
        Ready,
        /** @brief コピーを積んで、ページを結んだ */
        Resident,
        /** @brief 読み込みに失敗した（再試行の待ち、または諦めた） */
        Failed,
    };

    struct VirtualTextureStreamerStats
    {
        uint32_t TextureCount = 0;
        uint32_t WantedTiles = 0;
        uint32_t ReadingTiles = 0;
        uint32_t ReadyTiles = 0;
        uint32_t ResidentTiles = 0;
        uint32_t FailedTiles = 0;

        uint64_t ReadsStarted = 0;
        uint64_t ReadsCompleted = 0;
        uint64_t ReadsFailed = 0;
        uint64_t TilesBound = 0;
        uint64_t TilesCopied = 0;
        uint64_t BindFailures = 0;
        /** @brief コピーを積めず（リングが満杯等）、そのフレームの残りの結び付けを見送ったフレームの数 */
        uint64_t CopyBlockedFrames = 0;
        uint64_t PoolExhaustedFrames = 0;
        uint64_t InvalidRequests = 0;
        uint64_t StaleDropped = 0;
        uint64_t PermanentFailures = 0;
    };

    /** @brief Update 1 回の結果 */
    struct VirtualTextureFrameResult
    {
        uint32_t ReadsStarted = 0;
        uint32_t TilesBound = 0;
        uint32_t CopiesEnqueued = 0;
        uint64_t CopiedBytes = 0;
    };

    /**
     * @brief VT のストリーマ（RenderResources が 1 つ持つ）
     *
     * すべての公開関数は内部のミューテックスで守る。ただし BindSparse の外部同期のため、Update は
     * RenderThread（コマンドの送信と同じ直列化の下）から呼ぶこと。登録・解除は GameThread からでもよい
     * （ミップテイルの結び付けは次の Update で行う）。
     *
     * ミップテイルは次の Update 以降で、コピーを積んでから結ぶ（予算に入らなければ持ち越す）。それまでテクスチャは
     * 使えない（IsMipTailResident が true になってから材質へ出す）。ミップテイルは外さない。
     *
     * pool・gpu・retireQueue はストリーマより長く生きること。
     */
    class VirtualTextureStreamer final
    {
    public:
        using Config = VirtualTextureStreamerConfig;

        static constexpr uint32_t InvalidIndex = 0xFFFFFFFFu;

        VirtualTextureStreamer(SparsePagePool &pool,
                               IVirtualTextureGpu &gpu,
                               GpuRetireQueue *retireQueue = nullptr,
                               const Config &config = Config())
            : m_Pool(pool), m_Gpu(gpu), m_pRetireQueue(retireQueue), m_Config(config)
        {
        }

        ~VirtualTextureStreamer() { Clear(); }

        VirtualTextureStreamer(const VirtualTextureStreamer &) = delete;
        VirtualTextureStreamer &operator=(const VirtualTextureStreamer &) = delete;

        /**
         * @brief VT のテクスチャを登録する
         * @return VT の表の添字（フィードバックの要求が指す番号）。登録できなければ InvalidIndex
         *         （sparse でない・形状が形式の標準ブロック形状と合わない・ミップテイルの大きさが合わない・表が満杯）。
         */
        uint32_t RegisterTexture(VirtualTextureRegistration registration)
        {
            Thread::ScopedLock lock(m_Mutex);

            Container::TUniquePtr<Entry> entry = Container::MakeUnique<Entry>();
            if (!BuildEntryLocked(std::move(registration), *entry))
            {
                return InvalidIndex;
            }

            // 解除済みの枠を再利用する。前回の割り当ての続きから探し、番号をすぐ使い回さない。
            const uint32_t slotCount = static_cast<uint32_t>(m_Entries.size());
            for (uint32_t step = 0; step < slotCount; ++step)
            {
                const uint32_t index = (m_NextSlot + step) % slotCount;
                if (m_Entries[index] == nullptr)
                {
                    m_Entries[index] = std::move(entry);
                    m_NextSlot = index + 1;
                    return index;
                }
            }
            if (m_Entries.size() > VirtualTextureFeedback::MaxTextureIndex)
            {
                LOG_ERROR("VirtualTextureStreamer: テクスチャの表が満杯 max=%u", VirtualTextureFeedback::MaxTextureIndex + 1u);
                return InvalidIndex;
            }
            m_Entries.push_back(std::move(entry));
            m_NextSlot = static_cast<uint32_t>(m_Entries.size());
            return static_cast<uint32_t>(m_Entries.size() - 1);
        }

        /**
         * @brief テクスチャの登録を解除する。結んだページは GpuRetireQueue 経由で、GPU が使い終わってからプールへ戻る
         */
        void UnregisterTexture(uint32_t index)
        {
            Thread::ScopedLock lock(m_Mutex);
            Entry *entry = FindEntryLocked(index);
            if (entry != nullptr)
            {
                ReleaseEntryLocked(*entry);
                m_Entries[index].reset();
            }
        }

        /** @brief 全テクスチャの登録を解除する */
        void Clear()
        {
            Thread::ScopedLock lock(m_Mutex);
            for (Container::TUniquePtr<Entry> &slot : m_Entries)
            {
                if (slot != nullptr)
                {
                    ReleaseEntryLocked(*slot);
                }
            }
            m_Entries.clear();
            m_NextSlot = 0;
        }

        /** @brief ミップテイルを結んでコピーも積んだか（この後のフレームから、そのテクスチャをサンプルしてよい） */
        bool IsMipTailResident(uint32_t index) const
        {
            Thread::ScopedLock lock(m_Mutex);
            const Entry *entry = FindEntryLocked(index);
            return entry != nullptr && IsTailDone(*entry);
        }

        VirtualTextureTileState GetTileState(const VirtualTextureTileKey &key) const
        {
            Thread::ScopedLock lock(m_Mutex);
            const Entry *entry = FindEntryLocked(key.TextureIndex);
            if (entry == nullptr || !VirtualTextureFeedback::CanPack(key))
            {
                return VirtualTextureTileState::None;
            }
            const auto it = entry->Tiles.find(MakeKey(key.Mip, key.X, key.Y));
            return it == entry->Tiles.end() ? VirtualTextureTileState::None : it->second.State;
        }

        VirtualTextureStreamerStats GetStats() const
        {
            Thread::ScopedLock lock(m_Mutex);
            VirtualTextureStreamerStats stats = m_Stats;
            for (const Container::TUniquePtr<Entry> &slot : m_Entries)
            {
                if (slot == nullptr)
                {
                    continue;
                }
                ++stats.TextureCount;
                for (const auto &tile : slot->Tiles)
                {
                    switch (tile.second.State)
                    {
                    case VirtualTextureTileState::Wanted:
                        ++stats.WantedTiles;
                        break;
                    case VirtualTextureTileState::Reading:
                        ++stats.ReadingTiles;
                        break;
                    case VirtualTextureTileState::Ready:
                        ++stats.ReadyTiles;
                        break;
                    case VirtualTextureTileState::Resident:
                        ++stats.ResidentTiles;
                        break;
                    case VirtualTextureTileState::Failed:
                        ++stats.FailedTiles;
                        break;
                    case VirtualTextureTileState::None:
                        break;
                    }
                }
            }
            return stats;
        }

        const Config &GetConfig() const { return m_Config; }

        /**
         * @brief 1 フレーム分のストリーミングを進める（RenderThread）
         * @param frame 増えていくフレームの番号（要求が途絶えたタイルを忘れる時刻の基準）
         * @param requests フィードバックから集計した要求。無ければ null
         */
        VirtualTextureFrameResult Update(uint64_t frame, const VirtualTextureRequestSet *requests)
        {
            Thread::ScopedLock lock(m_Mutex);
            VirtualTextureFrameResult result;
            m_Frame = frame;

            if (requests != nullptr)
            {
                IngestRequestsLocked(*requests);
            }
            CollectReadsLocked();
            StageAndBindLocked(result);
            StartReadsLocked(result);
            DropStaleLocked();
            return result;
        }

    private:
        struct TileRecord
        {
            VirtualTextureTileState State = VirtualTextureTileState::Wanted;
            // フィードバックが最後に要求したフレーム（優先度の比較用）
            uint64_t LastRequestedFrame = 0;
            // 直近の取り込みでの要求の件数（画面で目立つほど多い。優先度の比較用）
            uint32_t HitCount = 0;
            // ストリーマが最後にこのタイルの要求を取り込んだ Update のフレーム（忘れる時刻の基準）
            uint64_t LastIngestFrame = 0;
            uint32_t FailCount = 0;
            uint64_t RetryAtFrame = 0;
            Container::VariableArray<uint8_t> Data;
            SparsePagePool::PageLease Page;
        };

        struct TailCopy
        {
            RHI::TextureRegionCopy Region;
            uint64_t Offset = 0;
            uint64_t Bytes = 0;
        };

        struct Entry
        {
            bool bActive = false;
            RHI::TexturePtr Texture;
            Asset::CookedTexturePixelFormat Format = Asset::CookedTexturePixelFormat::R8UNorm;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::SparseTextureInfo Info;
            Container::TSharedPtr<IVirtualTextureTileSource> Source;
            Container::VariableArray<uint8_t> TailData;
            Container::VariableArray<TailCopy> TailCopies;
            Container::VariableArray<SparsePagePool::PageLease> TailPages;
            // ミップテイルの初期化とコピーを積み、ページを結び終えた（ミップテイルが無いテクスチャは最初から true）
            bool bTailBound = false;
            Container::UnorderedMap<uint32_t, TileRecord> Tiles;
        };

        struct Candidate
        {
            uint32_t TextureIndex = 0;
            uint32_t TileKey = 0;
            uint32_t Mip = 0;
            uint32_t HitCount = 0;
            uint64_t LastRequestedFrame = 0;
        };

        // 1 フレームの予算の使用量
        struct FrameBudget
        {
            uint32_t Binds = 0;
            uint32_t Copies = 0;
            uint64_t Bytes = 0;
            // アップローダが今フレームで確実に記録できる量の残り
            uint64_t UploaderBytes = 0;
        };

        struct PendingTileBind
        {
            uint32_t TextureIndex = 0;
            uint32_t TileKey = 0;
            SparsePagePool::PageLease Page;
        };

        struct PendingTailBind
        {
            uint32_t TextureIndex = 0;
            Container::VariableArray<SparsePagePool::PageLease> Pages;
        };

        // タイルの印（ミップ・y・x）を 1 語にする。VirtualTextureRequestSet の内部の並びと同じ。
        static constexpr uint32_t MakeKey(uint32_t mip, uint32_t x, uint32_t y)
        {
            return (mip << (2u * VirtualTextureFeedback::TileBits)) | (y << VirtualTextureFeedback::TileBits) | x;
        }

        static constexpr uint32_t KeyMip(uint32_t key)
        {
            return (key >> (2u * VirtualTextureFeedback::TileBits)) & VirtualTextureFeedback::MaxMip;
        }

        static constexpr uint32_t KeyY(uint32_t key)
        {
            return (key >> VirtualTextureFeedback::TileBits) & VirtualTextureFeedback::MaxTileCoord;
        }

        static constexpr uint32_t KeyX(uint32_t key) { return key & VirtualTextureFeedback::MaxTileCoord; }

        static bool IsTailDone(const Entry &entry) { return entry.bTailBound; }

        // 優先度: 粗いミップが先、同じミップでは画面で目立つ（要求の件数が多い）ものが先、次に最近要求されたものが先、
        // 最後に決定的な順にそろえる。要求の語には画面上の位置が無いので、画面の近さは要求の件数（画面に占める
        // 面積の目安）で表す。
        static bool HigherPriority(const Candidate &a, const Candidate &b)
        {
            if (a.Mip != b.Mip)
            {
                return a.Mip > b.Mip;
            }
            if (a.HitCount != b.HitCount)
            {
                return a.HitCount > b.HitCount;
            }
            if (a.LastRequestedFrame != b.LastRequestedFrame)
            {
                return a.LastRequestedFrame > b.LastRequestedFrame;
            }
            if (a.TextureIndex != b.TextureIndex)
            {
                return a.TextureIndex < b.TextureIndex;
            }
            return a.TileKey < b.TileKey;
        }

        Entry *FindEntryLocked(uint32_t index)
        {
            return index < m_Entries.size() ? m_Entries[index].get() : nullptr;
        }

        const Entry *FindEntryLocked(uint32_t index) const
        {
            return index < m_Entries.size() ? m_Entries[index].get() : nullptr;
        }

        static uint32_t MipDimension(uint32_t base, uint32_t mip)
        {
            const uint32_t value = mip >= 32 ? 0u : (base >> mip);
            return value == 0 ? 1u : value;
        }

        // タイルのコピー先の矩形とバイト数を求める。範囲外・形式が不正なら false。
        static bool ComputeTileRegion(const Entry &entry,
                                      uint32_t mip,
                                      uint32_t x,
                                      uint32_t y,
                                      RHI::TextureRegionCopy &outRegion,
                                      uint64_t &outBytes)
        {
            const uint32_t mipWidth = MipDimension(entry.Width, mip);
            const uint32_t mipHeight = MipDimension(entry.Height, mip);
            Asset::CookedTextureTileRect rect;
            if (!Asset::ComputeCookedTextureTileRect(entry.Format, mipWidth, mipHeight, x, y, rect))
            {
                return false;
            }
            outRegion = RHI::TextureRegionCopy();
            outRegion.MipLevel = mip;
            outRegion.ArrayIndex = 0;
            outRegion.OffsetX = x * entry.Info.TileWidth;
            outRegion.OffsetY = y * entry.Info.TileHeight;
            outRegion.Width = std::min(entry.Info.TileWidth, mipWidth - outRegion.OffsetX);
            outRegion.Height = std::min(entry.Info.TileHeight, mipHeight - outRegion.OffsetY);
            outBytes = rect.DataBytes;
            return outBytes > 0;
        }

        bool BuildEntryLocked(VirtualTextureRegistration registration, Entry &outEntry)
        {
            if (registration.Texture == nullptr || registration.Source == nullptr || registration.Width == 0 ||
                registration.Height == 0)
            {
                LOG_ERROR("VirtualTextureStreamer: 登録の引数が不正（テクスチャ・読み込みの窓口・大きさ）");
                return false;
            }
            RHI::SparseTextureInfo info;
            if (!registration.Texture->IsSparse() || !registration.Texture->GetSparseInfo(info))
            {
                LOG_ERROR("VirtualTextureStreamer: sparse でないテクスチャは登録できない");
                return false;
            }
            const Asset::CookedTextureTileShape shape = Asset::GetCookedTextureStandardTileShape(registration.Format);
            if (shape.Width == 0 || info.TileWidth != shape.Width || info.TileHeight != shape.Height ||
                info.MipLevels == 0 || info.MipLevels > RHI::SparseTextureInfo::MaxMipLevels ||
                info.MipTailFirstLevel > info.MipLevels)
            {
                LOG_ERROR("VirtualTextureStreamer: テクスチャの形状が形式の標準ブロック形状と合わない tile=%ux%u expected=%ux%u",
                          info.TileWidth, info.TileHeight, shape.Width, shape.Height);
                return false;
            }
            const Asset::CookedTextureBlockInfo block = Asset::GetCookedTextureBlockInfo(registration.Format);
            if (block.BlockBytes == 0)
            {
                return false;
            }

            outEntry.Texture = std::move(registration.Texture);
            outEntry.Format = registration.Format;
            outEntry.Width = registration.Width;
            outEntry.Height = registration.Height;
            outEntry.Info = info;
            outEntry.Source = std::move(registration.Source);

            // ミップテイルのコピー: FirstTailMip 以降の段を行優先で詰めた順に、段ごとに 1 件
            uint64_t offset = 0;
            for (uint32_t mip = info.MipTailFirstLevel; mip < info.MipLevels; ++mip)
            {
                const uint32_t mipWidth = MipDimension(registration.Width, mip);
                const uint32_t mipHeight = MipDimension(registration.Height, mip);
                uint64_t rowBytes = 0;
                uint64_t rowCount = 0;
                if (!Asset::ComputeCookedTextureMipLayout(registration.Format, mipWidth, mipHeight, rowBytes, rowCount))
                {
                    return false;
                }
                TailCopy copy;
                copy.Region.MipLevel = mip;
                copy.Region.Width = mipWidth;
                copy.Region.Height = mipHeight;
                copy.Offset = offset;
                copy.Bytes = rowBytes * rowCount;
                offset += copy.Bytes;
                outEntry.TailCopies.push_back(copy);
            }
            if (offset != registration.TailData.size())
            {
                LOG_ERROR("VirtualTextureStreamer: ミップテイルのデータの大きさが合わない expected=%llu actual=%llu",
                          static_cast<unsigned long long>(offset),
                          static_cast<unsigned long long>(registration.TailData.size()));
                outEntry.Texture.reset();
                return false;
            }
            outEntry.TailData = std::move(registration.TailData);
            // ミップテイルの無いテクスチャは結ぶものが無い
            outEntry.bTailBound = outEntry.TailCopies.empty();
            outEntry.bActive = true;
            return true;
        }

        void RetirePage(SparsePagePool::PageLease page)
        {
            if (!page.IsValid())
            {
                return;
            }
            if (m_pRetireQueue != nullptr)
            {
                m_pRetireQueue->Retire(std::move(page));
            }
            else
            {
                page.Reset();
            }
        }

        void ReleaseEntryLocked(Entry &entry)
        {
            // 結んだページは別のテクスチャへ使い回されるので、出していないコピーが後から書き込まないようにする
            m_Gpu.AbandonTexture(entry.Texture);
            for (auto &tile : entry.Tiles)
            {
                RetirePage(std::move(tile.second.Page));
            }
            for (SparsePagePool::PageLease &page : entry.TailPages)
            {
                RetirePage(std::move(page));
            }
            entry.Tiles.clear();
            entry.TailPages.clear();
            entry.TailCopies.clear();
            entry.TailData.clear();
            entry.Source.reset();
            entry.Texture.reset();
            entry.bActive = false;
        }

        void MarkFailedLocked(TileRecord &record)
        {
            ++record.FailCount;
            record.RetryAtFrame = m_Frame + static_cast<uint64_t>(m_Config.RetryDelayFrames) * record.FailCount;
            record.State = VirtualTextureTileState::Failed;
            record.Data.clear();
            if (record.FailCount >= m_Config.MaxRetries)
            {
                ++m_Stats.PermanentFailures;
            }
        }

        void IngestRequestsLocked(const VirtualTextureRequestSet &requests)
        {
            const Container::VariableArray<uint32_t> textureIndices = requests.GetTextureIndices();
            for (const uint32_t textureIndex : textureIndices)
            {
                const Container::VariableArray<VirtualTextureTileRequest> tileRequests = requests.GetRequests(textureIndex);
                Entry *entry = FindEntryLocked(textureIndex);
                if (entry == nullptr)
                {
                    m_Stats.InvalidRequests += tileRequests.size();
                    continue;
                }
                // ミップテイルが常駐するまでは、タイルを結ばない（コピーの前に初期化の遷移が要る）
                if (!IsTailDone(*entry))
                {
                    continue;
                }
                for (const VirtualTextureTileRequest &request : tileRequests)
                {
                    if (request.Mip >= entry->Info.MipLevels)
                    {
                        ++m_Stats.InvalidRequests;
                        continue;
                    }
                    if (request.Mip >= entry->Info.MipTailFirstLevel)
                    {
                        // ミップテイルは常に常駐
                        continue;
                    }
                    if (request.X >= entry->Info.TilesX[request.Mip] || request.Y >= entry->Info.TilesY[request.Mip])
                    {
                        ++m_Stats.InvalidRequests;
                        continue;
                    }

                    const uint32_t key = MakeKey(request.Mip, request.X, request.Y);
                    auto it = entry->Tiles.find(key);
                    if (it == entry->Tiles.end())
                    {
                        TileRecord record;
                        record.State = VirtualTextureTileState::Wanted;
                        record.LastRequestedFrame = request.LastRequestedFrame;
                        record.HitCount = request.HitCount;
                        record.LastIngestFrame = m_Frame;
                        entry->Tiles.emplace(key, std::move(record));
                        continue;
                    }
                    TileRecord &record = it->second;
                    record.LastRequestedFrame = std::max(record.LastRequestedFrame, request.LastRequestedFrame);
                    record.HitCount = request.HitCount;
                    record.LastIngestFrame = m_Frame;
                    if (record.State == VirtualTextureTileState::Failed && record.FailCount < m_Config.MaxRetries &&
                        m_Frame >= record.RetryAtFrame)
                    {
                        record.State = VirtualTextureTileState::Wanted;
                    }
                }
            }
        }

        void CollectReadsLocked()
        {
            Container::VariableArray<VirtualTextureTileReadResult> results;
            for (Container::TUniquePtr<Entry> &slot : m_Entries)
            {
                if (slot == nullptr)
                {
                    continue;
                }
                Entry &entry = *slot;
                results.clear();
                entry.Source->CollectCompleted(results);
                for (VirtualTextureTileReadResult &read : results)
                {
                    if (!VirtualTextureFeedback::CanPack(read.Key))
                    {
                        continue;
                    }
                    auto it = entry.Tiles.find(MakeKey(read.Key.Mip, read.Key.X, read.Key.Y));
                    if (it == entry.Tiles.end() || it->second.State != VirtualTextureTileState::Reading)
                    {
                        // 解除された・忘れられたタイルの読み込みの完了は捨てる
                        continue;
                    }
                    TileRecord &record = it->second;
                    ++m_Stats.ReadsCompleted;

                    RHI::TextureRegionCopy region;
                    uint64_t expectedBytes = 0;
                    const bool bShapeOk = ComputeTileRegion(entry, read.Key.Mip, read.Key.X, read.Key.Y, region, expectedBytes);
                    if (!read.bSucceeded || !bShapeOk || read.Data.size() != expectedBytes)
                    {
                        ++m_Stats.ReadsFailed;
                        MarkFailedLocked(record);
                        continue;
                    }
                    record.Data = std::move(read.Data);
                    record.State = VirtualTextureTileState::Ready;
                }
            }
        }

        // 1 件（結び付け 1 つ・コピー copies 件・bytes バイト）が今フレームの予算に収まるか。
        // そのフレームの最初の 1 件は、1 件が上限より大きくても通す（永久に進まなくなるのを防ぐ）。
        // ただしアップローダが確実に記録できる量に収まらなければ通さない。
        bool FitsBudget(const FrameBudget &budget, uint32_t copies, uint64_t bytes) const
        {
            if (m_Config.MaxBindsPerFrame == 0 || bytes > budget.UploaderBytes)
            {
                return false;
            }
            if (budget.Binds == 0)
            {
                return true;
            }
            return budget.Binds < m_Config.MaxBindsPerFrame && budget.Copies + copies <= m_Config.MaxCopiesPerFrame &&
                   budget.Bytes + bytes <= m_Config.MaxCopyBytesPerFrame;
        }

        static void TakeBudget(FrameBudget &budget, uint32_t copies, uint64_t bytes)
        {
            ++budget.Binds;
            budget.Copies += copies;
            budget.Bytes += bytes;
            budget.UploaderBytes -= bytes;
        }

        // ミップテイルとタイルを、先にコピーを積んでから 1 回の BindSparse で結ぶ。
        // 結び付けに失敗したら積んだコピーを取り消すので、結ばれていないページへのコピーも、コピーされない
        // 結ばれたページも残らない。
        void StageAndBindLocked(VirtualTextureFrameResult &result)
        {
            FrameBudget budget;
            budget.UploaderBytes = m_Gpu.GetCopyBytesAvailable();

            RHI::SparseBindRequest request;
            Container::VariableArray<PendingTailBind> tailBinds;
            Container::VariableArray<PendingTileBind> tileBinds;
            // この Update でアップローダへ積んだ件数（結び付けに失敗したときに取り消す）
            uint32_t stagedOps = 0;
            uint32_t stagedCopies = 0;
            uint64_t stagedBytes = 0;
            bool bPoolExhausted = false;
            bool bStageBlocked = false;

            // 1) ミップテイル: 登録したテクスチャのうち、まだ結んでいないもの
            for (uint32_t index = 0; index < m_Entries.size() && !bPoolExhausted && !bStageBlocked; ++index)
            {
                if (m_Entries[index] == nullptr || m_Entries[index]->bTailBound)
                {
                    continue;
                }
                Entry &entry = *m_Entries[index];
                const uint32_t tailCopyCount = static_cast<uint32_t>(entry.TailCopies.size());
                const uint64_t tailBytes = entry.TailData.size();
                if (!FitsBudget(budget, tailCopyCount, tailBytes))
                {
                    continue;
                }

                const uint64_t pageCount =
                    (entry.Info.MipTailSize + SparsePagePool::PageSizeBytes - 1) / SparsePagePool::PageSizeBytes;
                PendingTailBind bind;
                bind.TextureIndex = index;
                for (uint64_t page = 0; page < pageCount; ++page)
                {
                    SparsePagePool::PageLease lease = m_Pool.Acquire();
                    if (!lease.IsValid())
                    {
                        bPoolExhausted = true;
                        break;
                    }
                    bind.Pages.push_back(std::move(lease));
                }
                if (bPoolExhausted)
                {
                    // 一部だけ結んだミップテイルは使えないので、借りた分は返す（bind が破棄されると戻る）
                    break;
                }

                // 初期化の遷移 → 各段のコピーの順に積む。途中で積めなければ、このテクスチャの分だけ取り消す。
                uint32_t tailOps = 0;
                bool bStaged = m_Gpu.EnqueueInitialize(entry.Texture);
                if (bStaged)
                {
                    ++tailOps;
                }
                for (size_t copyIndex = 0; bStaged && copyIndex < entry.TailCopies.size(); ++copyIndex)
                {
                    const TailCopy &copy = entry.TailCopies[copyIndex];
                    bStaged = m_Gpu.EnqueueTile(entry.Texture, copy.Region, entry.TailData.data() + copy.Offset, copy.Bytes);
                    if (bStaged)
                    {
                        ++tailOps;
                    }
                }
                if (!bStaged)
                {
                    m_Gpu.DiscardEnqueued(tailOps);
                    bStageBlocked = true;
                    break;
                }
                stagedOps += tailOps;
                stagedCopies += tailCopyCount;
                stagedBytes += tailBytes;
                TakeBudget(budget, tailCopyCount, tailBytes);

                for (uint64_t page = 0; page < pageCount; ++page)
                {
                    RHI::SparseMipTailBind tail;
                    tail.Texture = entry.Texture.get();
                    tail.PageIndex = static_cast<uint32_t>(page);
                    tail.Page = bind.Pages[static_cast<size_t>(page)].GetPage();
                    request.MipTails.push_back(tail);
                }
                tailBinds.push_back(std::move(bind));
            }

            // 2) タイル: 読み込み済みのものを優先度順に、1 フレームの予算の範囲で
            Container::VariableArray<Candidate> candidates;
            for (uint32_t index = 0; index < m_Entries.size(); ++index)
            {
                if (m_Entries[index] == nullptr || !IsTailDone(*m_Entries[index]))
                {
                    continue;
                }
                const Entry &entry = *m_Entries[index];
                for (const auto &tile : entry.Tiles)
                {
                    if (tile.second.State == VirtualTextureTileState::Ready)
                    {
                        candidates.push_back(MakeCandidate(index, tile.first, tile.second));
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end(), HigherPriority);

            for (const Candidate &candidate : candidates)
            {
                if (bPoolExhausted || bStageBlocked)
                {
                    break;
                }
                Entry &entry = *m_Entries[candidate.TextureIndex];
                TileRecord &record = entry.Tiles.find(candidate.TileKey)->second;
                const uint64_t tileBytes = record.Data.size();
                if (!FitsBudget(budget, 1, tileBytes))
                {
                    break;
                }
                RHI::TextureRegionCopy region;
                uint64_t expectedBytes = 0;
                if (!ComputeTileRegion(entry, candidate.Mip, KeyX(candidate.TileKey), KeyY(candidate.TileKey), region,
                                       expectedBytes) ||
                    expectedBytes != tileBytes)
                {
                    // 読み込みの取り込みで確かめ済みなので通常は起きない。使えないデータは失敗として扱う。
                    MarkFailedLocked(record);
                    continue;
                }
                SparsePagePool::PageLease lease = m_Pool.Acquire();
                if (!lease.IsValid())
                {
                    bPoolExhausted = true;
                    break;
                }
                if (!m_Gpu.EnqueueTile(entry.Texture, region, record.Data.data(), tileBytes))
                {
                    // リングが満杯など。借りたページは返し、残りの結び付けも見送って次のフレームに任せる
                    bStageBlocked = true;
                    break;
                }
                ++stagedOps;
                ++stagedCopies;
                stagedBytes += tileBytes;
                TakeBudget(budget, 1, tileBytes);

                RHI::SparseTileBind tile;
                tile.Texture = entry.Texture.get();
                tile.MipLevel = candidate.Mip;
                tile.TileX = KeyX(candidate.TileKey);
                tile.TileY = KeyY(candidate.TileKey);
                tile.Page = lease.GetPage();
                request.Tiles.push_back(tile);

                PendingTileBind bind;
                bind.TextureIndex = candidate.TextureIndex;
                bind.TileKey = candidate.TileKey;
                bind.Page = std::move(lease);
                tileBinds.push_back(std::move(bind));
            }

            if (bPoolExhausted)
            {
                ++m_Stats.PoolExhaustedFrames;
            }
            if (bStageBlocked)
            {
                ++m_Stats.CopyBlockedFrames;
            }
            if (request.IsEmpty())
            {
                return;
            }

            // 3) 1 回の BindSparse にまとめる。失敗したら何も結ばれていないので、借りたページは返り、積んだコピーは取り消す。
            if (!m_Gpu.BindSparse(request))
            {
                m_Gpu.DiscardEnqueued(stagedOps);
                ++m_Stats.BindFailures;
                LOG_ERROR("VirtualTextureStreamer: BindSparse に失敗した tails=%zu tiles=%zu", request.MipTails.size(),
                          request.Tiles.size());
                return;
            }

            // 4) 結べた。コピーは積み済みなので、結んだものをそのまま常駐として記録する。
            for (PendingTailBind &bind : tailBinds)
            {
                Entry &entry = *m_Entries[bind.TextureIndex];
                entry.TailPages = std::move(bind.Pages);
                entry.bTailBound = true;
                entry.TailData.clear();
                entry.TailData.shrink_to_fit();
            }
            for (PendingTileBind &bind : tileBinds)
            {
                Entry &entry = *m_Entries[bind.TextureIndex];
                TileRecord &record = entry.Tiles.find(bind.TileKey)->second;
                record.Page = std::move(bind.Page);
                record.State = VirtualTextureTileState::Resident;
                record.Data.clear();
                record.Data.shrink_to_fit();
                ++m_Stats.TilesBound;
                ++m_Stats.TilesCopied;
                ++result.TilesBound;
            }
            result.CopiesEnqueued += stagedCopies;
            result.CopiedBytes += stagedBytes;
        }

        static Candidate MakeCandidate(uint32_t textureIndex, uint32_t key, const TileRecord &record)
        {
            Candidate candidate;
            candidate.TextureIndex = textureIndex;
            candidate.TileKey = key;
            candidate.Mip = KeyMip(key);
            candidate.HitCount = record.HitCount;
            candidate.LastRequestedFrame = record.LastRequestedFrame;
            return candidate;
        }

        void StartReadsLocked(VirtualTextureFrameResult &result)
        {
            uint32_t inFlight = 0;
            Container::VariableArray<Candidate> candidates;
            for (uint32_t index = 0; index < m_Entries.size(); ++index)
            {
                if (m_Entries[index] == nullptr)
                {
                    continue;
                }
                const Entry &entry = *m_Entries[index];
                for (const auto &tile : entry.Tiles)
                {
                    switch (tile.second.State)
                    {
                    case VirtualTextureTileState::Reading:
                    case VirtualTextureTileState::Ready:
                        ++inFlight;
                        break;
                    case VirtualTextureTileState::Wanted:
                        candidates.push_back(MakeCandidate(index, tile.first, tile.second));
                        break;
                    default:
                        break;
                    }
                }
            }
            if (candidates.empty() || inFlight >= m_Config.MaxReadsInFlight)
            {
                return;
            }
            std::sort(candidates.begin(), candidates.end(), HigherPriority);

            uint32_t slots = std::min(m_Config.MaxReadsStartedPerFrame, m_Config.MaxReadsInFlight - inFlight);
            for (const Candidate &candidate : candidates)
            {
                if (slots == 0)
                {
                    break;
                }
                Entry &entry = *m_Entries[candidate.TextureIndex];
                TileRecord &record = entry.Tiles.find(candidate.TileKey)->second;
                VirtualTextureTileKey key;
                key.TextureIndex = candidate.TextureIndex;
                key.Mip = candidate.Mip;
                key.X = KeyX(candidate.TileKey);
                key.Y = KeyY(candidate.TileKey);
                --slots;
                if (entry.Source->BeginRead(key))
                {
                    record.State = VirtualTextureTileState::Reading;
                    ++m_Stats.ReadsStarted;
                    ++result.ReadsStarted;
                }
                else
                {
                    ++m_Stats.ReadsFailed;
                    MarkFailedLocked(record);
                }
            }
        }

        void DropStaleLocked()
        {
            for (Container::TUniquePtr<Entry> &slot : m_Entries)
            {
                if (slot == nullptr)
                {
                    continue;
                }
                Entry &entry = *slot;
                for (auto it = entry.Tiles.begin(); it != entry.Tiles.end();)
                {
                    if (it->second.State == VirtualTextureTileState::Wanted &&
                        m_Frame > it->second.LastIngestFrame &&
                        m_Frame - it->second.LastIngestFrame > m_Config.WantedMaxAgeFrames)
                    {
                        it = entry.Tiles.erase(it);
                        ++m_Stats.StaleDropped;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }

        SparsePagePool &m_Pool;
        IVirtualTextureGpu &m_Gpu;
        GpuRetireQueue *m_pRetireQueue = nullptr;
        Config m_Config;

        mutable Thread::Mutex m_Mutex;
        // 添字が VT の表の番号。解除した枠は null
        Container::VariableArray<Container::TUniquePtr<Entry>> m_Entries;
        VirtualTextureStreamerStats m_Stats;
        uint32_t m_NextSlot = 0;
        uint64_t m_Frame = 0;
    };
} // namespace NorvesLib::Core::Rendering
