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
//      積めたものだけページを結ぶ（1 回の BindSparse にまとめる。ミップテイルもタイルも同じ予算）。
//      ミップテイルが 1 フレームの上限に収まらないときは、数フレームに分けて書き、全部書き終えてから使えるようにする
//   4) 優先度の高い順に、未常駐のタイルの読み込みを始める
// 常駐させる量には目標（SetResidentBudget。VideoMemoryBudgetManager の VT の目標）がある。目標を超えたときと、結びたい
// タイルのページが目標に収まらないときは、常駐しているタイルを外してページを空ける（ミップテイルは外さない）。
// 外す順は「しばらく要求が無いタイルが先（最後に要求したフレームが古い順 = LRU）、次に使われているタイルを細かいミップから」。
// 要求の優先度が結びたいタイルより高い（同じか粗いミップの）使われているタイルは外さず、そのタイルは結ばずに待つ。
// 外すタイルは、結び付けと同じ 1 回の BindSparse で外す。外したページは、そのフレームでは誰にも渡さず、結び付けの成功の後に
// GpuRetireQueue へ渡す（最後に提出したフレームの完了まで再利用しない）。外すタイル宛ての未記録のコピーは無効にする。
// 長辺が MipGranularMaxDimension 以下のテクスチャは、ミップ全体を 1 単位として読み・結び・外す。
// 結び付けの前にコピーを積むのは、結んだ時点でハードウェアの常駐判定が「常駐」になり、描画がそのページを読めて
// しまうため。積んだコピーは同じフレームのコマンドの先頭で記録される（アップローダが記録できる量の範囲でだけ積む）。
// どの段にも 1 フレームの上限があり、超える分は次のフレームへ持ち越す。上限は最初の 1 件にも掛かる
// （1 件が上限より大きくなる設定は、登録の時点で拒否する）。

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

        /**
         * @brief 積んだがまだ記録していないコピーの数と量（バイト）
         *
         * 前のフレームを提出できず（Abort）に未記録へ戻ったコピーも含む。今フレームの上限に算入する。
         */
        virtual void GetPendingCopies(uint32_t &outCount, uint64_t &outBytes) const = 0;

        /** @brief 最後に積んだ count 件（EnqueueInitialize・EnqueueTile の合計）を、記録する前に取り消す */
        virtual void DiscardEnqueued(uint32_t count) = 0;

        /** @brief 登録を解除するテクスチャ宛ての、まだ GPU へ出していない依頼を無効にする */
        virtual void AbandonTexture(const RHI::TexturePtr &texture) = 0;

        /** @brief 追い出すタイルの領域宛ての、まだ GPU へ出していないコピーを無効にする */
        virtual void AbandonRegion(const RHI::TexturePtr &texture, const RHI::TextureRegionCopy &region) = 0;
    };

    struct VirtualTextureStreamerConfig
    {
        /** @brief 1 フレームに読み込みを始めるタイルの数 */
        uint32_t MaxReadsStartedPerFrame = 16;
        /**
         * @brief 読み込み中と読み込み済み（まだ GPU へ渡していない）のタイルの数の上限
         *
         * ミップ単位で扱うテクスチャは、ミップの全タイルがこの数に収まらないと登録を拒否する。
         */
        uint32_t MaxReadsInFlight = 128;
        /**
         * @brief 1 フレームにページを結ぶタイルとミップテイルの数（ミップテイルは 1 枚のテクスチャで 1 件）
         *
         * 1 以上にする。0 では何も結べないので、登録を拒否する。
         */
        uint32_t MaxBindsPerFrame = 32;
        /**
         * @brief 1 フレームに積むコピーの数（ミップテイルは段ごとに 1 件）
         *
         * 1 以上にする。0 では何もコピーできないので、登録を拒否する。
         */
        uint32_t MaxCopiesPerFrame = 128;
        /**
         * @brief 1 フレームにコピーするデータの量（バイト）。TileUploader のフレームの上限以下にする
         *
         * タイルもミップテイルも数える。最初の 1 件にも掛かるので、1 件のタイル、またはミップテイルの 1 段より
         * 小さい値は永久に進まなくなる。そのようなテクスチャは登録を拒否する。
         */
        uint64_t MaxCopyBytesPerFrame = 4ull * 1024ull * 1024ull;
        /** @brief 読み込みに失敗したタイルを諦めるまでの失敗の回数 */
        uint32_t MaxRetries = 3;
        /** @brief 失敗から再試行を許すまでの待ち（フレーム）。失敗の回数を掛ける */
        uint32_t RetryDelayFrames = 30;
        /** @brief 要求が途絶えた未常駐のタイル（要求済み・読み込み済み）を忘れるまでのフレーム */
        uint64_t WantedMaxAgeFrames = 240;
        /**
         * @brief 常駐しているタイルへの要求がこのフレーム数より長く途絶えたら「使われていない」とみなす
         *
         * 使われていないタイルは、追い出しで先に外す（LRU）。フィードバックは数フレーム遅れ、画素を間引いて書くので、
         * 1 フレーム要求が無いだけでは使われていないとはしない。
         */
        uint64_t EvictIdleFrames = 30;
        /**
         * @brief 長辺（ミップ 0）がこの値以下のテクスチャは、ミップ全体を 1 単位として読み・結び・外す（0 で無効）
         *
         * 1 単位の読み込み数・コピー数・コピー量が 1 フレームの上限（MaxReadsInFlight・MaxCopiesPerFrame・
         * MaxCopyBytesPerFrame）に収まらないテクスチャは、永久に結べないので登録を拒否する（タイル単位へは戻さない）。
         */
        uint32_t MipGranularMaxDimension = 1024;
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
        /** @brief 追い出して外したタイルの数（累計。ミップ単位のテクスチャはミップのタイル数ぶん） */
        uint64_t EvictedTiles = 0;
        /** @brief ストリーマが持つページ（ミップテイルと常駐タイル）の数。目標との比較に使う */
        uint64_t ResidentPages = 0;
        /** @brief 結びたいタイルのページが目標に収まらず、外せるタイルも無くて結び付けを見送ったフレームの数 */
        uint64_t BudgetBlockedFrames = 0;
    };

    /** @brief Update 1 回の結果 */
    struct VirtualTextureFrameResult
    {
        uint32_t ReadsStarted = 0;
        uint32_t TilesBound = 0;
        uint32_t CopiesEnqueued = 0;
        uint64_t CopiedBytes = 0;
        /** @brief このフレームで外したタイルの数 */
        uint32_t TilesEvicted = 0;
    };

    /**
     * @brief VT のストリーマ（RenderResources が 1 つ持つ）
     *
     * すべての公開関数は内部のミューテックスで守る。ただし BindSparse の外部同期のため、Update は
     * RenderThread（コマンドの送信と同じ直列化の下）から呼ぶこと。登録・解除は GameThread からでもよい
     * （ミップテイルの結び付けは次の Update で行う）。
     *
     * ミップテイルは次の Update 以降で、コピーを積んでから結ぶ（予算に入らなければ持ち越す）。1 フレームの上限に
     * 収まらない大きさのときは、最初の Update でページを結んで一部のコピーを積み、残りを続くフレームで積む。
     * どちらも、全部のコピーを積み終えるまでテクスチャは使えない（IsMipTailResident が true になってから材質へ出す）。
     * ミップテイルは外さない。
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
            stats.ResidentPages = CountLivePagesLocked();
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
         * @brief 常駐させる量の目標を決める（RenderThread、または Update と同じ直列化の下）
         * @param bLimited false なら目標なし（外さない）
         * @param bytes 目標（バイト）。ページの大きさ未満は切り捨てる。ミップテイルの分も数える
         *
         * 目標を下げたとき、超えた分は次の Update で外す。ミップテイルは外さないので、ミップテイルだけで
         * 目標を超えるときは、タイルをすべて外して止める。
         */
        void SetResidentBudget(bool bLimited, uint64_t bytes)
        {
            Thread::ScopedLock lock(m_Mutex);
            m_bBudgetLimited = bLimited;
            m_BudgetBytes = bytes;
        }

        /** @brief ストリーマが持つページ（ミップテイルと常駐タイル）の量（バイト） */
        uint64_t GetResidentBytes() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return CountLivePagesLocked() * SparsePagePool::PageSizeBytes;
        }

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
            // ストリーマが最後にこのタイルの要求を取り込んだ Update のフレーム（忘れる時刻・使われていない判定の基準）
            uint64_t LastIngestFrame = 0;
            // HitCount を最後に更新した Update のフレーム（ミップ単位のテクスチャで、同じフレームの要求を足し合わせる）
            uint64_t HitIngestFrame = 0;
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
            // ミップテイルのページを結んだ（コピーは TailCursor 件目まで積んである。全部積むまでは使えない）
            bool bTailPagesBound = false;
            // 次に積むミップテイルのコピーの添字
            uint32_t TailCursor = 0;
            // ミップテイルの初期化とコピーを全部積み、ページを結び終えた（ミップテイルが無いテクスチャは最初から true）
            bool bTailBound = false;
            // ミップ全体を 1 単位として読み・結び・外す（RegisterTexture で決める）
            bool bMipGranular = false;
            Container::UnorderedMap<uint32_t, TileRecord> Tiles;
        };

        struct Candidate
        {
            uint32_t TextureIndex = 0;
            uint32_t TileKey = 0;
            uint32_t Mip = 0;
            uint32_t HitCount = 0;
            uint64_t LastRequestedFrame = 0;
            // 結ぶ・読む単位に含まれるタイルの数（ミップ単位のテクスチャのミップは全タイル。TileKey はそのミップの (0, 0)）
            uint32_t TileCount = 1;
        };

        // 追い出しの候補（常駐しているタイル 1 枚、またはミップ単位のテクスチャのミップ 1 つ）
        struct Victim
        {
            uint32_t TextureIndex = 0;
            uint32_t TileKey = 0;
            uint32_t Mip = 0;
            uint32_t Pages = 0;
            uint32_t HitCount = 0;
            uint64_t LastRequestedFrame = 0;
            uint64_t LastIngestFrame = 0;
            // 要求が EvictIdleFrames より長く途絶えている
            bool bIdle = false;
        };

        // 外すと決めたタイル（BindSparse の成功の後に、ページを返してレコードを消す）
        struct PendingEvict
        {
            uint32_t TextureIndex = 0;
            uint32_t TileKey = 0;
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

        // 今フレームで進めるミップテイルの 1 段階（最初の段階はページを結び、続きはコピーだけを積む）
        struct PendingTailStep
        {
            uint32_t TextureIndex = 0;
            bool bStart = false;
            uint32_t NextCursor = 0;
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

        // ミップ mip のタイルの数（ミップ単位のテクスチャの 1 単位の大きさ）
        static uint32_t MipTileCount(const Entry &entry, uint32_t mip)
        {
            return mip < RHI::SparseTextureInfo::MaxMipLevels ? entry.Info.TilesX[mip] * entry.Info.TilesY[mip] : 0u;
        }

        // 結ぶ・外す 1 単位に含まれるタイルの印を集める。ミップ単位のテクスチャはミップの全タイル、そうでなければ 1 枚。
        static void CollectUnitKeys(const Entry &entry, uint32_t tileKey, Container::VariableArray<uint32_t> &outKeys)
        {
            outKeys.clear();
            if (!entry.bMipGranular)
            {
                outKeys.push_back(tileKey);
                return;
            }
            const uint32_t mip = KeyMip(tileKey);
            if (mip >= RHI::SparseTextureInfo::MaxMipLevels)
            {
                return;
            }
            for (uint32_t y = 0; y < entry.Info.TilesY[mip]; ++y)
            {
                for (uint32_t x = 0; x < entry.Info.TilesX[mip]; ++x)
                {
                    outKeys.push_back(MakeKey(mip, x, y));
                }
            }
        }

        static uint32_t SaturatingAdd(uint32_t a, uint32_t b)
        {
            const uint64_t sum = static_cast<uint64_t>(a) + b;
            return sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(sum);
        }

        static bool IsTailDone(const Entry &entry) { return entry.bTailBound; }

        // 優先度: 粗いミップが先、同じミップでは要求の件数（HitCount。画面に占める大きさの目安）が多いものが先、
        // 次に最後に要求したフレームが新しいものが先、最後に決定的な順にそろえる。
        // 要求の語にはカメラからの距離や画面上の位置が無いので、「画面の近く」は要求の件数で表す。
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

        // 追い出しの順（先に外すものが true）。
        // 1) 使われていない（要求が途絶えた）タイルが先。その中は最後に要求したフレームが古い順（LRU）、同じなら細かいミップが先。
        // 2) 使われているタイルは、細かいミップが先、同じミップでは要求の件数が少ない方、次に最後に要求したフレームが古い方。
        // 最後は決定的な順にそろえる。
        static bool EvictsBefore(const Victim &a, const Victim &b)
        {
            if (a.bIdle != b.bIdle)
            {
                return a.bIdle;
            }
            if (a.bIdle)
            {
                if (a.LastRequestedFrame != b.LastRequestedFrame)
                {
                    return a.LastRequestedFrame < b.LastRequestedFrame;
                }
                if (a.Mip != b.Mip)
                {
                    return a.Mip < b.Mip;
                }
                if (a.HitCount != b.HitCount)
                {
                    return a.HitCount < b.HitCount;
                }
            }
            else
            {
                if (a.Mip != b.Mip)
                {
                    return a.Mip < b.Mip;
                }
                if (a.HitCount != b.HitCount)
                {
                    return a.HitCount < b.HitCount;
                }
                if (a.LastRequestedFrame != b.LastRequestedFrame)
                {
                    return a.LastRequestedFrame < b.LastRequestedFrame;
                }
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
            if (!ValidateFrameLimitsLocked(outEntry))
            {
                outEntry.Texture.reset();
                return false;
            }
            outEntry.bMipGranular = m_Config.MipGranularMaxDimension != 0 &&
                                    std::max(outEntry.Width, outEntry.Height) <= m_Config.MipGranularMaxDimension;
            if (outEntry.bMipGranular && !ValidateMipUnitLimitsLocked(outEntry))
            {
                outEntry.Texture.reset();
                return false;
            }
            outEntry.TailData = std::move(registration.TailData);
            // ミップテイルの無いテクスチャは結ぶものが無い
            outEntry.bTailBound = outEntry.TailCopies.empty();
            outEntry.bActive = true;
            return true;
        }

        // ミップ単位で扱うテクスチャの、どのミップも 1 単位が 1 フレームの上限（読み込み中の数・コピーの数と量）に
        // 収まるか。単位の全タイルは同じ BindSparse で結び、同じフレームにコピーするので、収まらないと永久に結べない。
        // タイル単位へ戻すと「ミップ全体を 1 単位として扱う」約束が崩れるので、収まらない設定は登録を拒否する。
        bool ValidateMipUnitLimitsLocked(const Entry &entry) const
        {
            for (uint32_t mip = 0; mip < entry.Info.MipTailFirstLevel; ++mip)
            {
                const uint32_t tiles = MipTileCount(entry, mip);
                if (tiles == 0 || tiles > m_Config.MaxReadsInFlight || tiles > m_Config.MaxCopiesPerFrame)
                {
                    LOG_ERROR("VirtualTextureStreamer: ミップ %u の 1 単位が読み込み中の数かコピーの数の上限を超えるので登録できない tiles=%u reads=%u copies=%u",
                              mip, tiles, m_Config.MaxReadsInFlight, m_Config.MaxCopiesPerFrame);
                    return false;
                }
                uint64_t unitBytes = 0;
                for (uint32_t y = 0; y < entry.Info.TilesY[mip]; ++y)
                {
                    for (uint32_t x = 0; x < entry.Info.TilesX[mip]; ++x)
                    {
                        RHI::TextureRegionCopy region;
                        uint64_t tileBytes = 0;
                        if (!ComputeTileRegion(entry, mip, x, y, region, tileBytes))
                        {
                            LOG_ERROR("VirtualTextureStreamer: タイルのコピーの大きさを求められない mip=%u", mip);
                            return false;
                        }
                        unitBytes += tileBytes;
                    }
                }
                if (unitBytes > m_Config.MaxCopyBytesPerFrame)
                {
                    LOG_ERROR("VirtualTextureStreamer: ミップ %u の 1 単位が 1 フレームのコピー量の上限を超えるので登録できない unit=%llu limit=%llu",
                              mip, static_cast<unsigned long long>(unitBytes),
                              static_cast<unsigned long long>(m_Config.MaxCopyBytesPerFrame));
                    return false;
                }
            }
            return true;
        }

        // 1 フレームの上限で、このテクスチャのコピーが 1 件ずつでも進められるか。進められない設定は登録を拒否する
        // （上限は最初の 1 件にも掛かるので、1 件が入らない設定では永久に常駐しない）。
        bool ValidateFrameLimitsLocked(const Entry &entry) const
        {
            if (m_Config.MaxBindsPerFrame == 0 || m_Config.MaxCopiesPerFrame == 0)
            {
                LOG_ERROR("VirtualTextureStreamer: 1 フレームの結び付けまたはコピーの上限が 0 なので登録できない binds=%u copies=%u",
                          m_Config.MaxBindsPerFrame, m_Config.MaxCopiesPerFrame);
                return false;
            }
            uint64_t largestCopyBytes = 0;
            for (const TailCopy &copy : entry.TailCopies)
            {
                largestCopyBytes = std::max(largestCopyBytes, copy.Bytes);
            }
            if (entry.Info.MipTailFirstLevel > 0)
            {
                RHI::TextureRegionCopy region;
                uint64_t tileBytes = 0;
                if (!ComputeTileRegion(entry, 0, 0, 0, region, tileBytes))
                {
                    LOG_ERROR("VirtualTextureStreamer: タイルのコピーの大きさを求められない");
                    return false;
                }
                largestCopyBytes = std::max(largestCopyBytes, tileBytes);
            }
            if (largestCopyBytes > m_Config.MaxCopyBytesPerFrame)
            {
                LOG_ERROR("VirtualTextureStreamer: 1 件のコピーが 1 フレームのコピー量の上限を超えるので登録できない copy=%llu limit=%llu",
                          static_cast<unsigned long long>(largestCopyBytes),
                          static_cast<unsigned long long>(m_Config.MaxCopyBytesPerFrame));
                return false;
            }
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

                    if (entry->bMipGranular)
                    {
                        // ミップ単位: そのミップの全タイルを同じ要求として扱う
                        for (uint32_t y = 0; y < entry->Info.TilesY[request.Mip]; ++y)
                        {
                            for (uint32_t x = 0; x < entry->Info.TilesX[request.Mip]; ++x)
                            {
                                TouchOrCreateLocked(*entry, MakeKey(request.Mip, x, y), request, true);
                            }
                        }
                        continue;
                    }
                    TouchOrCreateLocked(*entry, MakeKey(request.Mip, request.X, request.Y), request, false);
                }
            }
        }

        // タイルの記録へ要求を取り込む（無ければ作る）。bAccumulateHits は、同じフレームの要求の件数を足し合わせる
        // （ミップ単位のテクスチャで、1 つのミップへ複数のタイルの要求が来る）。
        void TouchOrCreateLocked(Entry &entry,
                                 uint32_t key,
                                 const VirtualTextureTileRequest &request,
                                 bool bAccumulateHits)
        {
            auto it = entry.Tiles.find(key);
            if (it == entry.Tiles.end())
            {
                TileRecord record;
                record.State = VirtualTextureTileState::Wanted;
                record.LastRequestedFrame = request.LastRequestedFrame;
                record.HitCount = request.HitCount;
                record.LastIngestFrame = m_Frame;
                record.HitIngestFrame = m_Frame;
                entry.Tiles.emplace(key, std::move(record));
                return;
            }
            TileRecord &record = it->second;
            record.LastRequestedFrame = std::max(record.LastRequestedFrame, request.LastRequestedFrame);
            if (bAccumulateHits && record.HitIngestFrame == m_Frame)
            {
                record.HitCount = SaturatingAdd(record.HitCount, request.HitCount);
            }
            else
            {
                record.HitCount = request.HitCount;
            }
            record.HitIngestFrame = m_Frame;
            record.LastIngestFrame = m_Frame;
            if (record.State == VirtualTextureTileState::Failed && record.FailCount < m_Config.MaxRetries &&
                m_Frame >= record.RetryAtFrame)
            {
                record.State = VirtualTextureTileState::Wanted;
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

        // 結び付け（bNewBind なら 1 件）・コピー copies 件・bytes バイトが今フレームの予算に収まるか。
        // 最初の 1 件にも上限を掛ける（1 件が上限より大きいテクスチャは登録の時点で拒否してある）。
        // アップローダが確実に記録できる量に収まらなければ通さない。
        bool FitsBudget(const FrameBudget &budget, bool bNewBind, uint32_t copies, uint64_t bytes) const
        {
            if (bytes > budget.UploaderBytes)
            {
                return false;
            }
            if (bNewBind && budget.Binds >= m_Config.MaxBindsPerFrame)
            {
                return false;
            }
            return budget.Copies + copies <= m_Config.MaxCopiesPerFrame &&
                   budget.Bytes + bytes <= m_Config.MaxCopyBytesPerFrame;
        }

        static void TakeBudget(FrameBudget &budget, bool bNewBind, uint32_t copies, uint64_t bytes)
        {
            if (bNewBind)
            {
                ++budget.Binds;
            }
            budget.Copies += copies;
            budget.Bytes += bytes;
            budget.UploaderBytes -= bytes;
        }

        // ミップテイルとタイルを、先にコピーを積んでから 1 回の BindSparse で結ぶ。
        // 結び付けに失敗したら積んだコピーを取り消すので、結ばれていないページへのコピーも、コピーされない
        // 結ばれたページも残らない。ミップテイルは 1 フレームの予算に収まる分ずつ積み、全部積み終えるまで使えるように
        // しない（最初の段階でページを結び、続きのフレームはコピーだけを積む）。
        void StageAndBindLocked(VirtualTextureFrameResult &result)
        {
            FrameBudget budget;
            budget.UploaderBytes = m_Gpu.GetCopyBytesAvailable();
            // 記録されないまま持ち越したコピー（Abort で未記録へ戻ったもの）も、次の記録で出るので今フレームの上限に算入する
            m_Gpu.GetPendingCopies(budget.Copies, budget.Bytes);

            RHI::SparseBindRequest request;
            Container::VariableArray<PendingTailStep> tailSteps;
            Container::VariableArray<PendingTileBind> tileBinds;
            // この Update でアップローダへ積んだ件数（結び付けに失敗したときに取り消す）
            uint32_t stagedOps = 0;
            uint32_t stagedCopies = 0;
            uint64_t stagedBytes = 0;
            bool bPoolExhausted = false;
            bool bStageBlocked = false;
            bool bBudgetBlocked = false;
            // 外すと決めたタイル（結び付けの成功の後に、ページを返してレコードを消す）
            Container::VariableArray<PendingEvict> pendingEvicts;
            // 目標と比べるページの数（今フレームで結ぶ分を足し、外すと決めた分を引く）
            uint64_t livePages = CountLivePagesLocked();
            // プールから借りているが、ストリーマの常駐に数えないページ（外して GPU の完了を待っているものなど）。
            // 物理メモリは返るまで空かないので、新しく借りる前に目標へ足して比べる。
            const uint64_t poolUsedPages = m_Pool.GetStats().UsedBytes / SparsePagePool::PageSizeBytes;
            const uint64_t unreturnedPages = poolUsedPages > livePages ? poolUsedPages - livePages : 0;
            // 今フレームで外すと決めたページ（リトアキューがあれば、GPU の完了まで返らない）
            uint64_t evictedPages = 0;

            // 1) ミップテイル: 登録したテクスチャのうち、まだ全部を積んでいないもの
            for (uint32_t index = 0; index < m_Entries.size() && !bPoolExhausted && !bStageBlocked; ++index)
            {
                if (m_Entries[index] == nullptr || m_Entries[index]->bTailBound)
                {
                    continue;
                }
                Entry &entry = *m_Entries[index];
                const bool bStart = !entry.bTailPagesBound;
                const uint32_t tailTotal = static_cast<uint32_t>(entry.TailCopies.size());

                // 予算に収まる限りの段を、順番に数える（入らない段で止める）
                FrameBudget trial = budget;
                uint32_t count = 0;
                uint64_t bytes = 0;
                while (entry.TailCursor + count < tailTotal)
                {
                    const uint64_t copyBytes = entry.TailCopies[entry.TailCursor + count].Bytes;
                    const bool bNewBind = bStart && count == 0;
                    if (!FitsBudget(trial, bNewBind, 1, copyBytes))
                    {
                        break;
                    }
                    TakeBudget(trial, bNewBind, 1, copyBytes);
                    ++count;
                    bytes += copyBytes;
                }
                if (count == 0)
                {
                    continue;
                }

                PendingTailStep step;
                step.TextureIndex = index;
                step.bStart = bStart;
                const uint64_t pageCount =
                    (entry.Info.MipTailSize + SparsePagePool::PageSizeBytes - 1) / SparsePagePool::PageSizeBytes;
                if (bStart)
                {
                    for (uint64_t page = 0; page < pageCount; ++page)
                    {
                        SparsePagePool::PageLease lease = m_Pool.Acquire();
                        if (!lease.IsValid())
                        {
                            bPoolExhausted = true;
                            break;
                        }
                        step.Pages.push_back(std::move(lease));
                    }
                    if (bPoolExhausted)
                    {
                        // 一部だけ結んだミップテイルは使えないので、借りた分は返す（step が破棄されると戻る）
                        break;
                    }
                }

                // 最初の段階は初期化の遷移 → 各段のコピーの順に積む。途中で積めなければ、このテクスチャの分だけ取り消す。
                uint32_t tailOps = 0;
                bool bStaged = true;
                if (bStart)
                {
                    bStaged = m_Gpu.EnqueueInitialize(entry.Texture);
                    if (bStaged)
                    {
                        ++tailOps;
                    }
                }
                for (uint32_t offset = 0; bStaged && offset < count; ++offset)
                {
                    const TailCopy &copy = entry.TailCopies[entry.TailCursor + offset];
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
                stagedCopies += count;
                stagedBytes += bytes;
                budget = trial;
                step.NextCursor = entry.TailCursor + count;
                if (bStart)
                {
                    livePages += pageCount;
                }

                if (bStart)
                {
                    for (uint64_t page = 0; page < pageCount; ++page)
                    {
                        RHI::SparseMipTailBind tail;
                        tail.Texture = entry.Texture.get();
                        tail.PageIndex = static_cast<uint32_t>(page);
                        tail.Page = step.Pages[static_cast<size_t>(page)].GetPage();
                        request.MipTails.push_back(tail);
                    }
                }
                tailSteps.push_back(std::move(step));
            }

            // 2) 追い出し: 目標を超えているぶんを、外す順に外す（ミップテイルは外さない）。
            // 外すタイルのページは今フレームでは誰にも渡さず（返すのは結び付けの成功の後）、結び付けと同じ BindSparse で外す。
            const bool bLimited = m_bBudgetLimited;
            const uint64_t targetPages = m_BudgetBytes / SparsePagePool::PageSizeBytes;
            auto stageEviction = [&](const Victim &victim)
            {
                const uint64_t before = livePages;
                StageEvictionLocked(victim, request, pendingEvicts, livePages);
                evictedPages += before - livePages;
            };
            Container::VariableArray<Victim> victims;
            size_t victimCursor = 0;
            bool bVictimsBuilt = false;
            auto ensureVictims = [&]()
            {
                if (!bVictimsBuilt)
                {
                    BuildVictimsLocked(victims);
                    bVictimsBuilt = true;
                }
            };
            if (bLimited && livePages > targetPages)
            {
                ensureVictims();
                while (livePages > targetPages && victimCursor < victims.size())
                {
                    stageEviction(victims[victimCursor++]);
                }
            }
            // 結びたい単位のページが目標に収まるよう、外せるものを外す。使われていないタイルはいつでも外してよい。
            // 使われているタイルは、結びたい単位より細かいミップのときだけ（要求の優先度が低い）。それ以外は外さず、
            // その単位は結ばずに待つ（外す順の先頭が外せなければ、後ろも外せない）。
            // 外したページが返る前（リトアキューの待ち）は、プールの使用量が目標を超えるので、新しいページは借りずに待つ。
            auto makeRoom = [&](uint64_t pagesNeeded, uint32_t candidateMip) -> bool
            {
                if (!bLimited)
                {
                    return true;
                }
                while (livePages + pagesNeeded > targetPages)
                {
                    ensureVictims();
                    if (victimCursor >= victims.size())
                    {
                        return false;
                    }
                    const Victim &victim = victims[victimCursor];
                    if (!victim.bIdle && victim.Mip >= candidateMip)
                    {
                        return false;
                    }
                    ++victimCursor;
                    stageEviction(victim);
                }
                const uint64_t heldPages = livePages + unreturnedPages + (m_pRetireQueue != nullptr ? evictedPages : 0);
                return heldPages + pagesNeeded <= targetPages;
            };

            // 3) 結ぶ: 読み込み済みの単位を優先度順に、1 フレームの予算の範囲で。
            // ミップ単位のテクスチャは、ミップの全タイルが読み込み済みのときだけ 1 単位（全部を同じ BindSparse で結ぶ）。
            Container::VariableArray<Candidate> candidates;
            CollectBindCandidatesLocked(candidates);
            std::sort(candidates.begin(), candidates.end(), HigherPriority);

            Container::VariableArray<uint32_t> unitKeys;
            Container::VariableArray<RHI::TextureRegionCopy> unitRegions;
            Container::VariableArray<uint64_t> unitBytes;
            Container::VariableArray<SparsePagePool::PageLease> unitLeases;
            for (const Candidate &candidate : candidates)
            {
                if (bPoolExhausted || bStageBlocked)
                {
                    break;
                }
                Entry &entry = *m_Entries[candidate.TextureIndex];
                CollectUnitKeys(entry, candidate.TileKey, unitKeys);
                unitRegions.clear();
                unitBytes.clear();
                uint64_t unitTotalBytes = 0;
                bool bUnitValid = !unitKeys.empty();
                for (const uint32_t key : unitKeys)
                {
                    TileRecord &record = entry.Tiles.find(key)->second;
                    RHI::TextureRegionCopy region;
                    uint64_t expectedBytes = 0;
                    if (!ComputeTileRegion(entry, KeyMip(key), KeyX(key), KeyY(key), region, expectedBytes) ||
                        expectedBytes != record.Data.size())
                    {
                        // 読み込みの取り込みで確かめ済みなので通常は起きない。使えないデータは失敗として扱う。
                        MarkFailedLocked(record);
                        bUnitValid = false;
                        break;
                    }
                    unitRegions.push_back(region);
                    unitBytes.push_back(expectedBytes);
                    unitTotalBytes += expectedBytes;
                }
                if (!bUnitValid)
                {
                    continue;
                }
                const uint32_t unitCount = static_cast<uint32_t>(unitKeys.size());
                if (!FitsBudget(budget, true, unitCount, unitTotalBytes))
                {
                    break;
                }
                if (!makeRoom(unitCount, candidate.Mip))
                {
                    bBudgetBlocked = true;
                    break;
                }

                unitLeases.clear();
                for (uint32_t i = 0; i < unitCount; ++i)
                {
                    SparsePagePool::PageLease lease = m_Pool.Acquire();
                    if (!lease.IsValid())
                    {
                        bPoolExhausted = true;
                        break;
                    }
                    unitLeases.push_back(std::move(lease));
                }
                if (bPoolExhausted)
                {
                    // 一部だけ借りた分は返す
                    unitLeases.clear();
                    break;
                }

                // 単位の全タイルのコピーを積む。途中で積めなければ、この単位の分だけ取り消す
                uint32_t unitOps = 0;
                bool bUnitStaged = true;
                for (uint32_t i = 0; i < unitCount; ++i)
                {
                    const TileRecord &record = entry.Tiles.find(unitKeys[i])->second;
                    if (!m_Gpu.EnqueueTile(entry.Texture, unitRegions[i], record.Data.data(), unitBytes[i]))
                    {
                        bUnitStaged = false;
                        break;
                    }
                    ++unitOps;
                }
                if (!bUnitStaged)
                {
                    // リングが満杯など。借りたページは返し、残りの結び付けも見送って次のフレームに任せる
                    m_Gpu.DiscardEnqueued(unitOps);
                    unitLeases.clear();
                    bStageBlocked = true;
                    break;
                }
                stagedOps += unitOps;
                stagedCopies += unitCount;
                stagedBytes += unitTotalBytes;
                TakeBudget(budget, true, unitCount, unitTotalBytes);
                livePages += unitCount;

                for (uint32_t i = 0; i < unitCount; ++i)
                {
                    RHI::SparseTileBind tile;
                    tile.Texture = entry.Texture.get();
                    tile.MipLevel = KeyMip(unitKeys[i]);
                    tile.TileX = KeyX(unitKeys[i]);
                    tile.TileY = KeyY(unitKeys[i]);
                    tile.Page = unitLeases[i].GetPage();
                    request.Tiles.push_back(tile);

                    PendingTileBind bind;
                    bind.TextureIndex = candidate.TextureIndex;
                    bind.TileKey = unitKeys[i];
                    bind.Page = std::move(unitLeases[i]);
                    tileBinds.push_back(std::move(bind));
                }
                unitLeases.clear();
            }

            if (bPoolExhausted)
            {
                ++m_Stats.PoolExhaustedFrames;
            }
            if (bStageBlocked)
            {
                ++m_Stats.CopyBlockedFrames;
            }
            if (bBudgetBlocked)
            {
                ++m_Stats.BudgetBlockedFrames;
            }

            // 3) 1 回の BindSparse にまとめる。失敗したら何も結ばれていないので、借りたページは返り、積んだコピーは取り消す。
            // 続きのコピーだけのフレーム（結ぶものが無い）は、結び付けを出さない。
            if (!request.IsEmpty() && !m_Gpu.BindSparse(request))
            {
                m_Gpu.DiscardEnqueued(stagedOps);
                ++m_Stats.BindFailures;
                LOG_ERROR("VirtualTextureStreamer: BindSparse に失敗した tails=%zu tiles=%zu", request.MipTails.size(),
                          request.Tiles.size());
                return;
            }

            // 4) 結べた。コピーは積み済みなので、結んだものをそのまま記録する。
            for (PendingTailStep &step : tailSteps)
            {
                Entry &entry = *m_Entries[step.TextureIndex];
                if (step.bStart)
                {
                    entry.TailPages = std::move(step.Pages);
                    entry.bTailPagesBound = true;
                }
                entry.TailCursor = step.NextCursor;
                if (entry.TailCursor >= entry.TailCopies.size())
                {
                    // 全部のコピーを積んだ。ここで初めてミップテイルを使えるようにする
                    entry.bTailBound = true;
                    entry.TailData.clear();
                    entry.TailData.shrink_to_fit();
                }
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

            // 外したタイルは、結び付けの成功の後にページを返す（失敗したときは何も変えず、次のフレームでやり直す）。
            // 未記録のコピーが残っていれば無効にし、返したページへ古いコピーが書かれないようにする。
            // ページは RetireQueue が、最後に提出したフレームの完了まで再利用を止める。
            for (const PendingEvict &evict : pendingEvicts)
            {
                Entry &entry = *m_Entries[evict.TextureIndex];
                const auto it = entry.Tiles.find(evict.TileKey);
                if (it == entry.Tiles.end() || it->second.State != VirtualTextureTileState::Resident)
                {
                    continue;
                }
                RHI::TextureRegionCopy region;
                uint64_t regionBytes = 0;
                if (ComputeTileRegion(entry, KeyMip(evict.TileKey), KeyX(evict.TileKey), KeyY(evict.TileKey), region,
                                      regionBytes))
                {
                    m_Gpu.AbandonRegion(entry.Texture, region);
                }
                RetirePage(std::move(it->second.Page));
                entry.Tiles.erase(it);
                ++m_Stats.EvictedTiles;
                ++result.TilesEvicted;
            }
        }

        // ストリーマが持つページの数（ミップテイルと常駐タイル）
        uint64_t CountLivePagesLocked() const
        {
            uint64_t pages = 0;
            for (const Container::TUniquePtr<Entry> &slot : m_Entries)
            {
                if (slot == nullptr)
                {
                    continue;
                }
                pages += slot->TailPages.size();
                for (const auto &tile : slot->Tiles)
                {
                    if (tile.second.State == VirtualTextureTileState::Resident)
                    {
                        ++pages;
                    }
                }
            }
            return pages;
        }

        // 要求が EvictIdleFrames より長く途絶えているか
        bool IsIdleLocked(uint64_t lastIngestFrame) const
        {
            return m_Frame > lastIngestFrame && m_Frame - lastIngestFrame > m_Config.EvictIdleFrames;
        }

        // 追い出しの候補を、外す順に並べる。常駐しているタイル（ミップ単位のテクスチャはミップごとにまとめて 1 件）が対象。
        void BuildVictimsLocked(Container::VariableArray<Victim> &out) const
        {
            out.clear();
            for (uint32_t index = 0; index < m_Entries.size(); ++index)
            {
                if (m_Entries[index] == nullptr)
                {
                    continue;
                }
                const Entry &entry = *m_Entries[index];
                if (!entry.bMipGranular)
                {
                    for (const auto &tile : entry.Tiles)
                    {
                        if (tile.second.State != VirtualTextureTileState::Resident)
                        {
                            continue;
                        }
                        Victim victim;
                        victim.TextureIndex = index;
                        victim.TileKey = tile.first;
                        victim.Mip = KeyMip(tile.first);
                        victim.Pages = 1;
                        victim.HitCount = tile.second.HitCount;
                        victim.LastRequestedFrame = tile.second.LastRequestedFrame;
                        victim.LastIngestFrame = tile.second.LastIngestFrame;
                        out.push_back(victim);
                    }
                    continue;
                }
                Victim units[RHI::SparseTextureInfo::MaxMipLevels];
                bool hasUnit[RHI::SparseTextureInfo::MaxMipLevels] = {};
                for (const auto &tile : entry.Tiles)
                {
                    const uint32_t mip = KeyMip(tile.first);
                    if (tile.second.State != VirtualTextureTileState::Resident ||
                        mip >= RHI::SparseTextureInfo::MaxMipLevels)
                    {
                        continue;
                    }
                    Victim &unit = units[mip];
                    if (!hasUnit[mip])
                    {
                        hasUnit[mip] = true;
                        unit.TextureIndex = index;
                        unit.TileKey = MakeKey(mip, 0, 0);
                        unit.Mip = mip;
                    }
                    ++unit.Pages;
                    unit.HitCount = std::max(unit.HitCount, tile.second.HitCount);
                    unit.LastRequestedFrame = std::max(unit.LastRequestedFrame, tile.second.LastRequestedFrame);
                    unit.LastIngestFrame = std::max(unit.LastIngestFrame, tile.second.LastIngestFrame);
                }
                for (uint32_t mip = 0; mip < RHI::SparseTextureInfo::MaxMipLevels; ++mip)
                {
                    if (hasUnit[mip])
                    {
                        out.push_back(units[mip]);
                    }
                }
            }
            for (Victim &victim : out)
            {
                victim.bIdle = IsIdleLocked(victim.LastIngestFrame);
            }
            std::sort(out.begin(), out.end(), EvictsBefore);
        }

        // 1 件の追い出しを、今フレームの BindSparse への「外す」指定と、成功の後の後始末の予定として積む。
        void StageEvictionLocked(const Victim &victim,
                                 RHI::SparseBindRequest &request,
                                 Container::VariableArray<PendingEvict> &pendingEvicts,
                                 uint64_t &livePages)
        {
            Entry &entry = *m_Entries[victim.TextureIndex];
            Container::VariableArray<uint32_t> keys;
            CollectUnitKeys(entry, victim.TileKey, keys);
            for (const uint32_t key : keys)
            {
                const auto it = entry.Tiles.find(key);
                if (it == entry.Tiles.end() || it->second.State != VirtualTextureTileState::Resident)
                {
                    continue;
                }
                RHI::SparseTileBind unbind;
                unbind.Texture = entry.Texture.get();
                unbind.MipLevel = KeyMip(key);
                unbind.TileX = KeyX(key);
                unbind.TileY = KeyY(key);
                request.Tiles.push_back(unbind);

                PendingEvict evict;
                evict.TextureIndex = victim.TextureIndex;
                evict.TileKey = key;
                pendingEvicts.push_back(evict);
                if (livePages > 0)
                {
                    --livePages;
                }
            }
        }

        // 結べる単位を集める。ミップ単位のテクスチャは、ミップの全タイルが読み込み済みのときだけ 1 単位にする。
        void CollectBindCandidatesLocked(Container::VariableArray<Candidate> &out) const
        {
            for (uint32_t index = 0; index < m_Entries.size(); ++index)
            {
                if (m_Entries[index] == nullptr || !IsTailDone(*m_Entries[index]))
                {
                    continue;
                }
                const Entry &entry = *m_Entries[index];
                if (!entry.bMipGranular)
                {
                    for (const auto &tile : entry.Tiles)
                    {
                        if (tile.second.State == VirtualTextureTileState::Ready)
                        {
                            out.push_back(MakeCandidate(index, tile.first, tile.second));
                        }
                    }
                    continue;
                }
                struct MipTotals
                {
                    uint32_t Ready = 0;
                    uint32_t HitCount = 0;
                    uint64_t LastRequestedFrame = 0;
                };
                MipTotals totals[RHI::SparseTextureInfo::MaxMipLevels];
                for (const auto &tile : entry.Tiles)
                {
                    const uint32_t mip = KeyMip(tile.first);
                    if (tile.second.State != VirtualTextureTileState::Ready ||
                        mip >= RHI::SparseTextureInfo::MaxMipLevels)
                    {
                        continue;
                    }
                    ++totals[mip].Ready;
                    totals[mip].HitCount = std::max(totals[mip].HitCount, tile.second.HitCount);
                    totals[mip].LastRequestedFrame = std::max(totals[mip].LastRequestedFrame, tile.second.LastRequestedFrame);
                }
                for (uint32_t mip = 0; mip < entry.Info.MipTailFirstLevel && mip < RHI::SparseTextureInfo::MaxMipLevels;
                     ++mip)
                {
                    const uint32_t tileCount = MipTileCount(entry, mip);
                    if (tileCount == 0 || totals[mip].Ready != tileCount)
                    {
                        continue;
                    }
                    Candidate candidate;
                    candidate.TextureIndex = index;
                    candidate.TileKey = MakeKey(mip, 0, 0);
                    candidate.Mip = mip;
                    candidate.HitCount = totals[mip].HitCount;
                    candidate.LastRequestedFrame = totals[mip].LastRequestedFrame;
                    candidate.TileCount = tileCount;
                    out.push_back(candidate);
                }
            }
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

        // ミップ単位のテクスチャの単位（テクスチャ・ミップ）の識別
        static constexpr uint64_t MakeUnitId(uint32_t textureIndex, uint32_t mip)
        {
            return (static_cast<uint64_t>(textureIndex) << 8) | static_cast<uint64_t>(mip & 0xFFu);
        }

        // 優先度の高い順に、未常駐のタイルの読み込みを始める。
        // 読み込み中と読み込み済みのタイルの数は MaxReadsInFlight に収める。ミップ単位のテクスチャは、始める前に
        // 単位の残り全部のタイルが収まる余裕があるときだけ始め、始めた単位の残りの枠は先に確保しておく
        // （単位が中途半端に読み込まれて、結べないまま枠を占めることを防ぐ）。
        // 目標があるときは、読み込んだタイルのページを（空きと外せるタイルで）確保できる数までしか始めない。
        // その数え方では、始めたいタイルと同じか粗いミップ（優先度が同じか高い）の読み込み中・読み込み済みのタイルだけを
        // 数える。より細かいミップのタイルは、結ぶ順で後になるので、粗いミップのタイルの読み込みを塞がない。
        void StartReadsLocked(VirtualTextureFrameResult &result)
        {
            struct UnitCount
            {
                uint32_t Started = 0;
                uint32_t Wanted = 0;
            };
            uint32_t inFlight = 0;
            // ページを必要とする読み込み中・読み込み済みのタイル（と、始めた単位の残り）の数。ミップ別
            uint32_t pendingByMip[RHI::SparseTextureInfo::MaxMipLevels] = {};
            Container::VariableArray<Candidate> candidates;
            Container::UnorderedMap<uint64_t, UnitCount> units;
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
                        if (KeyMip(tile.first) < RHI::SparseTextureInfo::MaxMipLevels)
                        {
                            ++pendingByMip[KeyMip(tile.first)];
                        }
                        if (entry.bMipGranular)
                        {
                            ++units[MakeUnitId(index, KeyMip(tile.first))].Started;
                        }
                        break;
                    case VirtualTextureTileState::Wanted:
                        candidates.push_back(MakeCandidate(index, tile.first, tile.second));
                        if (entry.bMipGranular)
                        {
                            ++units[MakeUnitId(index, KeyMip(tile.first))].Wanted;
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
            if (candidates.empty())
            {
                return;
            }
            std::sort(candidates.begin(), candidates.end(), HigherPriority);

            // 始めた単位の、まだ読み込みを始めていない残りのタイルの枠
            uint32_t reserved = 0;
            Container::UnorderedMap<uint64_t, bool> admitted;
            for (const auto &unit : units)
            {
                if (unit.second.Started > 0 && unit.second.Wanted > 0)
                {
                    reserved += unit.second.Wanted;
                    pendingByMip[unit.first & 0xFFu] += unit.second.Wanted;
                    admitted.emplace(unit.first, true);
                }
            }

            // 目標に対するページの余裕: 目標までの空きと、外せるタイル（使われていないもの、使われているなら細かいミップ）
            const bool bLimited = m_bBudgetLimited;
            uint64_t freePages = 0;
            uint64_t idlePages = 0;
            uint64_t activePagesByMip[RHI::SparseTextureInfo::MaxMipLevels] = {};
            if (bLimited)
            {
                const uint64_t targetPages = m_BudgetBytes / SparsePagePool::PageSizeBytes;
                const uint64_t livePages = CountLivePagesLocked();
                freePages = targetPages > livePages ? targetPages - livePages : 0;
                for (const Container::TUniquePtr<Entry> &slot : m_Entries)
                {
                    if (slot == nullptr)
                    {
                        continue;
                    }
                    for (const auto &tile : slot->Tiles)
                    {
                        if (tile.second.State != VirtualTextureTileState::Resident)
                        {
                            continue;
                        }
                        const uint32_t mip = KeyMip(tile.first);
                        if (IsIdleLocked(tile.second.LastIngestFrame))
                        {
                            ++idlePages;
                        }
                        else if (mip < RHI::SparseTextureInfo::MaxMipLevels)
                        {
                            ++activePagesByMip[mip];
                        }
                    }
                }
            }
            // mip のタイル extra 枚を、ページの目標に収められるか（同じか粗いミップの読み込み中のタイルも数える）
            auto hasRoom = [&](uint32_t mip, uint64_t extra) -> bool
            {
                if (!bLimited)
                {
                    return true;
                }
                uint64_t available = freePages + idlePages;
                for (uint32_t finer = 0; finer < mip && finer < RHI::SparseTextureInfo::MaxMipLevels; ++finer)
                {
                    available += activePagesByMip[finer];
                }
                uint64_t pending = extra;
                for (uint32_t coarser = mip; coarser < RHI::SparseTextureInfo::MaxMipLevels; ++coarser)
                {
                    pending += pendingByMip[coarser];
                }
                return pending <= available;
            };

            uint32_t slots = m_Config.MaxReadsStartedPerFrame;
            for (const Candidate &candidate : candidates)
            {
                if (slots == 0)
                {
                    break;
                }
                Entry &entry = *m_Entries[candidate.TextureIndex];
                const uint64_t occupied = static_cast<uint64_t>(inFlight) + reserved;
                bool bReservedTile = false;
                if (entry.bMipGranular)
                {
                    const uint64_t unitId = MakeUnitId(candidate.TextureIndex, candidate.Mip);
                    if (admitted.find(unitId) != admitted.end())
                    {
                        bReservedTile = true;
                    }
                    else
                    {
                        const uint32_t wanted = units[unitId].Wanted;
                        if (occupied + wanted > m_Config.MaxReadsInFlight || !hasRoom(candidate.Mip, wanted))
                        {
                            continue;
                        }
                        admitted.emplace(unitId, true);
                        reserved += wanted;
                        pendingByMip[candidate.Mip] += wanted;
                        bReservedTile = true;
                    }
                }
                else if (occupied + 1 > m_Config.MaxReadsInFlight || !hasRoom(candidate.Mip, 1))
                {
                    continue;
                }

                TileRecord &record = entry.Tiles.find(candidate.TileKey)->second;
                VirtualTextureTileKey key;
                key.TextureIndex = candidate.TextureIndex;
                key.Mip = candidate.Mip;
                key.X = KeyX(candidate.TileKey);
                key.Y = KeyY(candidate.TileKey);
                --slots;
                if (bReservedTile && reserved > 0)
                {
                    --reserved;
                }
                if (entry.Source->BeginRead(key))
                {
                    record.State = VirtualTextureTileState::Reading;
                    ++inFlight;
                    if (!bReservedTile)
                    {
                        // 単位の残りの枠は、すでに数えてある
                        ++pendingByMip[candidate.Mip];
                    }
                    ++m_Stats.ReadsStarted;
                    ++result.ReadsStarted;
                }
                else
                {
                    ++m_Stats.ReadsFailed;
                    MarkFailedLocked(record);
                    if (bReservedTile && pendingByMip[candidate.Mip] > 0)
                    {
                        --pendingByMip[candidate.Mip];
                    }
                }
            }
        }

        // 要求が途絶えた未常駐のタイル（要求済み・読み込み済み）を忘れる。読み込み済みで結べないまま残ったタイル
        // （目標に収まらない・ミップ単位の単位が揃わない）が、読み込みの枠を占め続けないようにする。
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
                    const VirtualTextureTileState state = it->second.State;
                    if ((state == VirtualTextureTileState::Wanted || state == VirtualTextureTileState::Ready) &&
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
        // 常駐させる量の目標（SetResidentBudget）
        bool m_bBudgetLimited = false;
        uint64_t m_BudgetBytes = 0;
    };
} // namespace NorvesLib::Core::Rendering
