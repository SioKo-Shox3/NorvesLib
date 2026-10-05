#pragma once

// ジオメトリのページのストリーマ。
// カリングが書いたページの要求（GeometryPageRequestSet）を受け取り、未常駐のページを優先度順に読み、
// ステージングのリング経由で区画へ書いてから、ページの表を更新して描画から見えるようにする。
// 読み込み・区画への書き込み・ページの表の更新・追い出しは窓口（IGeometryPageBackend）越しに行うので、
// GPU もファイルも使わずにテストできる（本番の窓口は MegaGeometryResourceStore）。
//
// 1 フレームの流れ（Update。RenderThread から、フレームのコマンドを開く前に呼ぶ）:
//   1) 解放されたメッシュのページの記録を捨てる
//   2) 要求を取り込む（解決できない要求は捨て、同じページは 1 つにまとめる。要求の数と最後に要求したフレームを数える）
//   3) 完了した読み込みを集める（失敗・大きさの食い違いは再試行の待ちへ）
//   4) 書き込み中のページが GPU で書き終わったかを見て、終わったものだけをページの表へ公開する（書き終える前に公開しない）
//   5) 常駐の量が目標を超えていれば、最後に要求したフレームが古いページから外す（LRU）
//   6) 優先度の高い順に、読み込み済みのページの書き込みを始める（上限: 1 フレームのコピーの量）。
//      目標に収まらないときは、LRU で外して空ける
//   7) 優先度の高い順に、要求されたページの読み込みを始める（上限: 1 フレームの読みの量・件数・読み込み中の数）
//
// 優先度は「粗い段が先 → 要求されたフレームの数が多い → 最後に要求されたフレームが新しい」。
//
// ページの親子の関係を守る: 細かい側のページは、親（粗い側）のページが全て常駐していないと公開しない（親が無いと細かい側だけが
// 描かれて穴になる）。親のページは、それを親に持つページが全て常駐していないときだけ外す（子から外す）。
// 親が常駐していないページは、書き込みを見送って親を先に要求する。
// 根のページは、メッシュを作るときに常駐させて、ここでは扱わない（外さない）。

#include "Container/Containers.h"
#include "Container/Map.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /** @brief ストリーマがページ 1 つについて知っておく静的な情報（窓口が答える） */
    struct GeometryPageDescriptor
    {
        /** @brief 区画の大きさ（バイト）。常駐の量と目標の比較に数える */
        uint64_t RegionBytes = 0;
        /** @brief 読み込みの結果の Data の大きさ（バイト。頂点とインデックス） */
        uint64_t DataBytes = 0;
        /** @brief 読み込みが実際に読むファイルの範囲（バイト。ヘッダ・クラスタの記録を含み、DataBytes 以上）。1 フレームの読みの量はこれで数える */
        uint64_t ReadBytes = 0;
        /** @brief 書き込みで積むコピーの量（バイト。ページの中身とクラスタの記録の書き換えの合計） */
        uint64_t CopyBytes = 0;
        /** @brief 段（大きいほど粗い）。粗い段から先に読み込む */
        uint32_t Level = 0;
        /** @brief 根のページか（根のページは常駐のままで、要求の対象にならない） */
        bool bRoot = false;
        /** @brief 親のページ（粗い側）のうち、根でないもの。根のページは常に常駐なので含めない */
        Container::VariableArray<uint32_t> Parents;
        /** @brief 子のページ（細かい側。根にはならない） */
        Container::VariableArray<uint32_t> Children;
    };

    /** @brief ページ 1 つの読み込みの完了 */
    struct GeometryPageReadCompletion
    {
        uint64_t MeshId = 0;
        uint32_t PageId = 0;
        bool bSucceeded = false;
        Container::VariableArray<uint8_t> Data;
    };

    /** @brief BeginUpload の結果 */
    enum class GeometryPageUploadStart : uint8_t
    {
        /** @brief 区画を借りて、コピーを積んだ */
        Queued,
        /** @brief 区画・リングの空きが無い（フレームが進めば空くので、後で出し直す） */
        Blocked,
        /** @brief ページが不正（未登録・範囲外・すでに常駐・大きさの食い違い）。出し直しても同じ */
        Rejected
    };

    /** @brief 書き込み中のページの状態 */
    enum class GeometryPageUploadState : uint8_t
    {
        /** @brief 積んだコピーのどれかが、GPU でまだ完了していない */
        Pending,
        /** @brief 全てのコピーが GPU で完了した（ページの表へ公開してよい） */
        Complete,
        /** @brief メッシュが解放された・コピーが取り消された */
        Lost
    };

    /**
     * @brief ストリーマが読み込み・書き込み・公開・追い出しに使う窓口
     *
     * 本番は MegaGeometryResourceStore。どの呼び出しも RenderThread の直列化の下で行う。
     */
    class IGeometryPageBackend
    {
    public:
        virtual ~IGeometryPageBackend() = default;

        /**
         * @brief 要求（ページの表のグローバルな位置と、書いたフレームのシェーダーが見た表の版）を、メッシュとページへ引き直す
         * @return 範囲が解放されている・別のメッシュへ再利用された・ストリーミングしないメッシュなら false
         */
        virtual bool ResolveRequest(uint32_t tableIndex, uint64_t tableVersion, uint64_t &outMeshId,
                                    uint32_t &outPageId) const = 0;

        /** @brief ページの静的な情報。メッシュが無い・ページが範囲外・ストリーミングしないメッシュなら false */
        virtual bool GetPageDescriptor(uint64_t meshId, uint32_t pageId, GeometryPageDescriptor &out) const = 0;

        /** @brief メッシュが生きていて、ページをストリーミングするメッシュか（解放されたら false） */
        virtual bool IsMeshStreamed(uint64_t meshId) const = 0;

        /** @brief ページの読み込みを始める。始められなければ false（ジョブを積めない等） */
        virtual bool BeginRead(uint64_t meshId, uint32_t pageId) = 0;

        /** @brief 完了した読み込み（失敗も含む）を out へ足す。完了したものは二度返さない */
        virtual void CollectReads(Container::VariableArray<GeometryPageReadCompletion> &out) = 0;

        /**
         * @brief ページの区画を借り、中身とクラスタの記録の書き換えをステージングのリングへ積む
         *
         * data は呼び出しの間に複写するので、戻った後で手放してよい。公開（Publish）はしない。
         */
        virtual GeometryPageUploadStart BeginUpload(uint64_t meshId, uint32_t pageId,
                                                    const Container::VariableArray<uint8_t> &data) = 0;

        /** @brief BeginUpload で積んだコピーが GPU で完了したか */
        virtual GeometryPageUploadState PollUpload(uint64_t meshId, uint32_t pageId) const = 0;

        /** @brief 書き終えたページをページの表へ公開する（描画から見える）。失敗なら false */
        virtual bool Publish(uint64_t meshId, uint32_t pageId) = 0;

        /**
         * @brief 公開したページを外す（ページの表を非常駐にし、区画は使っていた提出の完了まで再利用しない）
         * @return メッシュが無い・常駐していないページなら false
         */
        virtual bool Evict(uint64_t meshId, uint32_t pageId) = 0;

        /** @brief 次のコマンドの記録で確実にコピーできる量の残り（バイト）。これを超えて積まない */
        virtual uint64_t GetCopyBytesAvailable() const = 0;
    };

    struct GeometryPageStreamerConfig
    {
        /** @brief 1 フレームに読み込みを始めるページの数 */
        uint32_t MaxReadsStartedPerFrame = 16;
        /** @brief 読み込み中と、読み込み済みでまだ書き込みを始めていないページの数の上限 */
        uint32_t MaxReadsInFlight = 64;
        /** @brief 1 フレームに読み込みを始めるページの量（バイト）。最初の 1 件にも掛かる。1 ページがこれを超えるなら読めないので諦める */
        uint64_t MaxReadBytesPerFrame = 4ull * 1024ull * 1024ull;
        /** @brief 1 フレームにコピーを積むページの数 */
        uint32_t MaxUploadsPerFrame = 32;
        /** @brief 1 フレームに積むコピーの量（バイト）。TileUploader のフレームの上限以下にする。最初の 1 件にも掛かる。1 ページがこれを超えるなら積めないので諦める */
        uint64_t MaxCopyBytesPerFrame = 4ull * 1024ull * 1024ull;
        /** @brief 読み込みに失敗したページを諦めるまでの失敗の回数 */
        uint32_t MaxRetries = 3;
        /** @brief 失敗から再試行を許すまでの待ち（フレーム）。失敗の回数を掛ける */
        uint32_t RetryDelayFrames = 30;
        /** @brief 要求が途絶えた未常駐のページ（要求済み・読み込み済み・失敗）を忘れるまでのフレーム */
        uint64_t WantedMaxAgeFrames = 240;
    };

    enum class GeometryPageState : uint8_t
    {
        /** @brief 記録が無い */
        None,
        /** @brief 要求されている。読み込みはまだ始めていない */
        Wanted,
        /** @brief 読み込み中 */
        Reading,
        /** @brief 読み込み済みで、書き込みを待っている */
        Ready,
        /** @brief 書き込みを積んで、GPU での完了を待っている */
        Uploading,
        /** @brief ページの表へ公開した（描画から見える） */
        Resident,
        /** @brief 読み込みに失敗した（再試行の待ち、または諦めた） */
        Failed
    };

    struct GeometryPageStreamerStats
    {
        uint32_t WantedPages = 0;
        uint32_t ReadingPages = 0;
        uint32_t ReadyPages = 0;
        uint32_t UploadingPages = 0;
        uint32_t ResidentPages = 0;
        uint32_t FailedPages = 0;
        /** @brief 常駐（公開済み）と書き込み中のページの区画の合計（バイト）。目標との比較に使う */
        uint64_t ResidentBytes = 0;

        uint64_t ReadsStarted = 0;
        uint64_t ReadsCompleted = 0;
        uint64_t ReadsFailed = 0;
        uint64_t PagesUploaded = 0;
        uint64_t PagesPublished = 0;
        /** @brief 追い出して外したページの数（累計） */
        uint64_t EvictedPages = 0;
        uint64_t InvalidRequests = 0;
        uint64_t StaleDropped = 0;
        uint64_t PermanentFailures = 0;
        /** @brief 1 フレームの読み・コピーの上限より大きく、1 度も通せないので諦めたページの数（PermanentFailures にも含む） */
        uint64_t OversizedPages = 0;
        /** @brief 区画・リングに空きが無く、書き込みを見送ったフレームの数 */
        uint64_t UploadBlockedFrames = 0;
        /** @brief 目標に収まらず、外せるページも無くて書き込みを見送ったフレームの数 */
        uint64_t BudgetBlockedFrames = 0;
        /** @brief 親のページが常駐していないために書き込みを見送ったページの数（のべ） */
        uint64_t ParentWaitSkips = 0;
    };

    /** @brief Update 1 回の結果 */
    struct GeometryPageFrameResult
    {
        uint32_t ReadsStarted = 0;
        uint32_t UploadsStarted = 0;
        uint32_t PagesPublished = 0;
        uint32_t PagesEvicted = 0;
        uint64_t ReadBytes = 0;
        uint64_t CopyBytes = 0;
    };

    /**
     * @brief ジオメトリのページのストリーマ（RenderResources が 1 つ持つ）
     *
     * すべての公開関数は内部のミューテックスで守る。Update は RenderThread から呼ぶこと。
     * backend はストリーマより長く生きること。
     */
    class GeometryPageStreamer final
    {
    public:
        using Config = GeometryPageStreamerConfig;

        /** @brief 書き込みが空きを待って進まないまま、この数の Update が続いたら、落ち着かない待ちとして数えない */
        static constexpr uint32_t StalledUploadFrames = 120;

        explicit GeometryPageStreamer(IGeometryPageBackend &backend, const Config &config = Config())
            : m_Backend(backend), m_Config(config)
        {
        }

        GeometryPageStreamer(const GeometryPageStreamer &) = delete;
        GeometryPageStreamer &operator=(const GeometryPageStreamer &) = delete;

        /**
         * @brief 常駐させる量の目標を決める（Update と同じ直列化の下、または GameThread から）
         * @param bLimited false なら目標なし（外さない）
         * @param bytes ストリーミングするページに使える量（バイト）。根のページなど外せない分を引いた残り
         *
         * 目標を下げたとき、超えた分は次の Update で外す。
         */
        void SetResidentBudget(bool bLimited, uint64_t bytes)
        {
            Thread::ScopedLock lock(m_Mutex);
            m_bBudgetLimited = bLimited;
            m_BudgetBytes = bytes;
        }

        /** @brief 常駐（公開済み）と書き込み中のページの区画の合計（バイト） */
        uint64_t GetResidentBytes() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_ResidentBytes;
        }

        GeometryPageState GetPageState(uint64_t meshId, uint32_t pageId) const
        {
            Thread::ScopedLock lock(m_Mutex);
            const auto it = m_Records.find(PageKey{meshId, pageId});
            return it == m_Records.end() ? GeometryPageState::None : it->second.State;
        }

        GeometryPageStreamerStats GetStats() const
        {
            Thread::ScopedLock lock(m_Mutex);
            GeometryPageStreamerStats stats = m_Stats;
            stats.ResidentBytes = m_ResidentBytes;
            for (const auto &pair : m_Records)
            {
                switch (pair.second.State)
                {
                case GeometryPageState::Wanted:
                    ++stats.WantedPages;
                    break;
                case GeometryPageState::Reading:
                    ++stats.ReadingPages;
                    break;
                case GeometryPageState::Ready:
                    ++stats.ReadyPages;
                    break;
                case GeometryPageState::Uploading:
                    ++stats.UploadingPages;
                    break;
                case GeometryPageState::Resident:
                    ++stats.ResidentPages;
                    break;
                case GeometryPageState::Failed:
                    ++stats.FailedPages;
                    break;
                case GeometryPageState::None:
                    break;
                }
            }
            return stats;
        }

        /**
         * @brief 読み込み・書き込みが進行中、または進められる要求が残っているか
         *
         * 読み込み中・書き込み中のページがあれば true。要求済み・読み込み済みのページは、直前の Update が目標に収まらず
         * 進められなかった（外せるページも無い）とき、または書き込みが長く（StalledUploadFrames 以上）区画・リングの空きを
         * 待ち続けているときは数えない（待ち続けても落ち着かないので）。失敗は数えない。
         * 撮影が、ページが落ち着くのを待つのに使う。
         */
        bool HasPendingWork() const
        {
            Thread::ScopedLock lock(m_Mutex);
            for (const auto &pair : m_Records)
            {
                switch (pair.second.State)
                {
                case GeometryPageState::Reading:
                case GeometryPageState::Uploading:
                    return true;
                case GeometryPageState::Wanted:
                case GeometryPageState::Ready:
                    if (!m_bBudgetBlockedLastUpdate && m_UploadBlockedStreak < StalledUploadFrames)
                    {
                        return true;
                    }
                    break;
                default:
                    break;
                }
            }
            return false;
        }

        const Config &GetConfig() const { return m_Config; }

        /** @brief 設定を替える（試験と、実行中の調整のため。次の Update から効く） */
        void SetConfig(const Config &config)
        {
            Thread::ScopedLock lock(m_Mutex);
            m_Config = config;
        }

        /** @brief 全ての記録を捨てる（常駐のページは外さない。メッシュの解放はバックエンドが片付ける） */
        void Clear()
        {
            Thread::ScopedLock lock(m_Mutex);
            m_Records.clear();
            m_ResidentBytes = 0;
        }

        /**
         * @brief 1 フレーム分のストリーミングを進める（RenderThread）
         * @param frame 増えていくフレームの番号（最後に要求されたフレームの基準）
         * @param requests カリングが書いた要求。無ければ null
         */
        GeometryPageFrameResult Update(uint64_t frame, const MegaGeometry::GeometryPageRequestSet *requests)
        {
            Thread::ScopedLock lock(m_Mutex);
            GeometryPageFrameResult result;
            m_Frame = frame;
            m_bBudgetBlockedLastUpdate = false;
            const uint64_t uploadBlockedBefore = m_Stats.UploadBlockedFrames;
            const uint64_t pagesUploadedBefore = m_Stats.PagesUploaded;

            DropDeadMeshesLocked();
            if (requests != nullptr)
            {
                IngestRequestsLocked(*requests);
            }
            CollectReadsLocked();
            PollUploadsLocked(result);
            EnforceBudgetLocked(result);
            StartUploadsLocked(result);
            StartReadsLocked(result);
            DropStaleLocked();

            // 書き込みが空きを待って進まないフレームの続き（進んだら 0 へ戻す）
            if (m_Stats.UploadBlockedFrames != uploadBlockedBefore && m_Stats.PagesUploaded == pagesUploadedBefore)
            {
                ++m_UploadBlockedStreak;
            }
            else if (m_Stats.PagesUploaded != pagesUploadedBefore)
            {
                m_UploadBlockedStreak = 0;
            }
            return result;
        }

    private:
        struct PageKey
        {
            uint64_t MeshId = 0;
            uint32_t PageId = 0;

            bool operator<(const PageKey &other) const
            {
                return MeshId != other.MeshId ? MeshId < other.MeshId : PageId < other.PageId;
            }
        };

        struct Record
        {
            GeometryPageState State = GeometryPageState::Wanted;
            GeometryPageDescriptor Desc;
            /** @brief 最後に要求された Update のフレーム */
            uint64_t LastRequestedFrame = 0;
            /** @brief 要求を取り込んだフレームの数（同じフレームの重複は 1 つにまとまっている） */
            uint32_t RequestCount = 0;
            uint32_t Failures = 0;
            uint64_t RetryFrame = 0;
            bool bPermanentlyFailed = false;
            /** @brief 読み込み済み（Ready）の間だけ持つページの中身 */
            Container::VariableArray<uint8_t> Data;
        };

        using RecordMap = Container::Map<PageKey, Record>;

        static bool IsLive(GeometryPageState state)
        {
            return state == GeometryPageState::Resident || state == GeometryPageState::Uploading;
        }

        // 常駐の量の数え方: 書き込み中と公開済みのページの区画
        void AddLiveBytes(const Record &record) { m_ResidentBytes += record.Desc.RegionBytes; }
        void SubLiveBytes(const Record &record)
        {
            m_ResidentBytes = m_ResidentBytes > record.Desc.RegionBytes ? m_ResidentBytes - record.Desc.RegionBytes : 0;
        }

        void EraseLocked(typename RecordMap::iterator it)
        {
            if (IsLive(it->second.State))
            {
                SubLiveBytes(it->second);
            }
            m_Records.erase(it);
        }

        // 1) 解放されたメッシュのページの記録を捨てる
        void DropDeadMeshesLocked()
        {
            uint64_t lastMesh = 0;
            bool bLastValid = false;
            bool bLastStreamed = false;
            for (auto it = m_Records.begin(); it != m_Records.end();)
            {
                if (!bLastValid || it->first.MeshId != lastMesh)
                {
                    lastMesh = it->first.MeshId;
                    bLastStreamed = m_Backend.IsMeshStreamed(lastMesh);
                    bLastValid = true;
                }
                if (!bLastStreamed)
                {
                    auto victim = it++;
                    EraseLocked(victim);
                }
                else
                {
                    ++it;
                }
            }
        }

        // 2) 要求を取り込む
        void IngestRequestsLocked(const MegaGeometry::GeometryPageRequestSet &requests)
        {
            // 要求のフレームの番号は、要求のリングの数え方（ストリーマの Update の番号とは別）。
            // 集合の中で最も新しい要求を今のフレームとして、古い要求は同じだけ前のフレームへ写す（新しさの順を保つ）
            uint64_t newestRequestFrame = 0;
            for (const MegaGeometry::GeometryPageRequestSet::Request &request : requests.GetRequests())
            {
                newestRequestFrame = std::max(newestRequestFrame, request.LastRequestedFrame);
            }
            for (const MegaGeometry::GeometryPageRequestSet::Request &request : requests.GetRequests())
            {
                PageKey key;
                if (!m_Backend.ResolveRequest(request.TableIndex, request.TableVersion, key.MeshId, key.PageId))
                {
                    ++m_Stats.InvalidRequests;
                    continue;
                }
                Record *record = FindOrCreateLocked(key);
                if (record == nullptr)
                {
                    ++m_Stats.InvalidRequests;
                    continue;
                }
                const uint64_t age = newestRequestFrame - request.LastRequestedFrame;
                const uint64_t requestedFrame = m_Frame > age ? m_Frame - age : 0;
                // 作ったばかりの記録（要求の数 0）は作った時点のフレームを持つので、要求のフレームで置き換える
                record->LastRequestedFrame =
                    record->RequestCount == 0 ? requestedFrame : std::max(record->LastRequestedFrame, requestedFrame);
                ++record->RequestCount;
            }
        }

        // 記録を探す。無ければ静的な情報を引いて、要求された状態で作る（根のページ・不正なページは作らず null）
        Record *FindOrCreateLocked(const PageKey &key)
        {
            auto it = m_Records.find(key);
            if (it != m_Records.end())
            {
                return &it->second;
            }
            Record record;
            if (!m_Backend.GetPageDescriptor(key.MeshId, key.PageId, record.Desc) || record.Desc.bRoot ||
                record.Desc.RegionBytes == 0)
            {
                return nullptr;
            }
            record.State = GeometryPageState::Wanted;
            record.LastRequestedFrame = m_Frame;
            return &m_Records.emplace(key, std::move(record)).first->second;
        }

        // 3) 完了した読み込みを集める
        void CollectReadsLocked()
        {
            Container::VariableArray<GeometryPageReadCompletion> completed;
            m_Backend.CollectReads(completed);
            for (GeometryPageReadCompletion &completion : completed)
            {
                const auto it = m_Records.find(PageKey{completion.MeshId, completion.PageId});
                if (it == m_Records.end() || it->second.State != GeometryPageState::Reading)
                {
                    continue; // 忘れたページ・古い結果
                }
                Record &record = it->second;
                ++m_Stats.ReadsCompleted;
                if (!completion.bSucceeded || completion.Data.size() != record.Desc.DataBytes)
                {
                    FailLocked(record);
                    continue;
                }
                record.Data = std::move(completion.Data);
                record.State = GeometryPageState::Ready;
            }
        }

        void FailLocked(Record &record)
        {
            ++m_Stats.ReadsFailed;
            ++record.Failures;
            record.Data.clear();
            record.State = GeometryPageState::Failed;
            if (record.Failures >= m_Config.MaxRetries)
            {
                record.bPermanentlyFailed = true;
                ++m_Stats.PermanentFailures;
            }
            else
            {
                record.RetryFrame = m_Frame + static_cast<uint64_t>(m_Config.RetryDelayFrames) * record.Failures;
            }
        }

        // 1 フレームの上限より大きいページは、1 度も通せないので、読み込みの再試行もせず諦める
        void RejectOversizedLocked(const PageKey &key, Record &record, const char *what, uint64_t bytes, uint64_t limit)
        {
            LOG_ERROR("GEOMETRY_PAGE_STREAMER ページが1フレームの%sの上限を超えるので読み込めません mesh=%llu page=%u bytes=%llu limit=%llu",
                      what, static_cast<unsigned long long>(key.MeshId), static_cast<unsigned>(key.PageId),
                      static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(limit));
            record.Data = Container::VariableArray<uint8_t>();
            record.State = GeometryPageState::Failed;
            if (!record.bPermanentlyFailed)
            {
                record.bPermanentlyFailed = true;
                ++m_Stats.PermanentFailures;
            }
            ++m_Stats.OversizedPages;
        }

        // 4) 書き込み中のページが GPU で書き終わったものを公開する
        void PollUploadsLocked(GeometryPageFrameResult &result)
        {
            for (auto it = m_Records.begin(); it != m_Records.end();)
            {
                Record &record = it->second;
                if (record.State != GeometryPageState::Uploading)
                {
                    ++it;
                    continue;
                }
                const GeometryPageUploadState state = m_Backend.PollUpload(it->first.MeshId, it->first.PageId);
                if (state == GeometryPageUploadState::Pending)
                {
                    ++it;
                    continue;
                }
                if (state == GeometryPageUploadState::Complete && m_Backend.Publish(it->first.MeshId, it->first.PageId))
                {
                    record.State = GeometryPageState::Resident;
                    ++m_Stats.PagesPublished;
                    ++result.PagesPublished;
                    ++it;
                    continue;
                }
                // メッシュが解放された・公開できなかった。記録を捨てる（要求があれば作り直す）
                auto victim = it++;
                EraseLocked(victim);
            }
        }

        // 外してよいページか: 公開済みで、子のページが 1 つも常駐していない（書き込み中も含めて）。protect は外さないページ
        bool IsEvictableLocked(const PageKey &key, const Record &record,
                               const Container::VariableArray<uint32_t> *protectedPages) const
        {
            if (record.State != GeometryPageState::Resident)
            {
                return false;
            }
            if (protectedPages != nullptr &&
                std::find(protectedPages->begin(), protectedPages->end(), key.PageId) != protectedPages->end())
            {
                return false;
            }
            for (const uint32_t childPage : record.Desc.Children)
            {
                const auto child = m_Records.find(PageKey{key.MeshId, childPage});
                if (child != m_Records.end() && IsLive(child->second.State))
                {
                    return false;
                }
            }
            return true;
        }

        // 最後に要求されたフレームが古いページから 1 つ外す。外せたら true
        // （同じ古さなら細かい段が先。maxLastRequestedFrame より新しく要求されたページは外さない）
        bool EvictOneLocked(const Container::VariableArray<uint32_t> *protectedPages, uint64_t meshOfProtected,
                            uint64_t maxLastRequestedFrame, GeometryPageFrameResult &result)
        {
            typename RecordMap::iterator best = m_Records.end();
            for (auto it = m_Records.begin(); it != m_Records.end(); ++it)
            {
                const Record &record = it->second;
                if (record.State != GeometryPageState::Resident || record.LastRequestedFrame > maxLastRequestedFrame)
                {
                    continue;
                }
                const Container::VariableArray<uint32_t> *protect =
                    (protectedPages != nullptr && it->first.MeshId == meshOfProtected) ? protectedPages : nullptr;
                if (!IsEvictableLocked(it->first, record, protect))
                {
                    continue;
                }
                if (best == m_Records.end() || record.LastRequestedFrame < best->second.LastRequestedFrame ||
                    (record.LastRequestedFrame == best->second.LastRequestedFrame &&
                     record.Desc.Level < best->second.Desc.Level))
                {
                    best = it;
                }
            }
            if (best == m_Records.end())
            {
                return false;
            }
            m_Backend.Evict(best->first.MeshId, best->first.PageId);
            ++m_Stats.EvictedPages;
            ++result.PagesEvicted;
            EraseLocked(best);
            return true;
        }

        // 5) 目標を超えた分を LRU で外す
        void EnforceBudgetLocked(GeometryPageFrameResult &result)
        {
            if (!m_bBudgetLimited)
            {
                return;
            }
            while (m_ResidentBytes > m_BudgetBytes)
            {
                if (!EvictOneLocked(nullptr, 0, ~0ull, result))
                {
                    break; // 外せるページが無い（子が常駐している等）。子が外れた後の次の Update で続ける
                }
            }
        }

        struct Candidate
        {
            PageKey Key;
            uint32_t Level = 0;
            uint32_t RequestCount = 0;
            uint64_t LastRequestedFrame = 0;
        };

        // 優先度: 粗い段が先 → 要求の数が多い → 最後に要求されたフレームが新しい → 決まった順
        static bool HasHigherPriority(const Candidate &a, const Candidate &b)
        {
            if (a.Level != b.Level)
            {
                return a.Level > b.Level;
            }
            if (a.RequestCount != b.RequestCount)
            {
                return a.RequestCount > b.RequestCount;
            }
            if (a.LastRequestedFrame != b.LastRequestedFrame)
            {
                return a.LastRequestedFrame > b.LastRequestedFrame;
            }
            return a.Key < b.Key;
        }

        Container::VariableArray<Candidate> CollectCandidatesLocked(GeometryPageState state, bool bIncludeRetry) const
        {
            Container::VariableArray<Candidate> candidates;
            for (const auto &pair : m_Records)
            {
                const Record &record = pair.second;
                const bool bMatch = record.State == state ||
                                    (bIncludeRetry && record.State == GeometryPageState::Failed &&
                                     !record.bPermanentlyFailed && record.RetryFrame <= m_Frame);
                if (!bMatch)
                {
                    continue;
                }
                Candidate candidate;
                candidate.Key = pair.first;
                candidate.Level = record.Desc.Level;
                candidate.RequestCount = record.RequestCount;
                candidate.LastRequestedFrame = record.LastRequestedFrame;
                candidates.push_back(candidate);
            }
            std::sort(candidates.begin(), candidates.end(), HasHigherPriority);
            return candidates;
        }

        // 親のページが全て常駐しているか。足りない親は、要求の数を引き継いで要求する（先に読み込ませる）
        bool ParentsResidentLocked(const PageKey &key, const Record &record)
        {
            bool bAllResident = true;
            for (const uint32_t parentPage : record.Desc.Parents)
            {
                const PageKey parentKey{key.MeshId, parentPage};
                Record *parent = FindOrCreateLocked(parentKey);
                if (parent == nullptr)
                {
                    // 解決できない親（範囲外・根と誤って申告された）では、いつまでも公開できないので待ち続ける
                    bAllResident = false;
                    continue;
                }
                if (parent->State == GeometryPageState::Resident)
                {
                    continue;
                }
                bAllResident = false;
                parent->LastRequestedFrame = m_Frame;
                parent->RequestCount = std::max(parent->RequestCount, record.RequestCount);
            }
            return bAllResident;
        }

        // 6) 優先度の高い順に、読み込み済みのページの書き込みを始める
        void StartUploadsLocked(GeometryPageFrameResult &result)
        {
            const Container::VariableArray<Candidate> candidates = CollectCandidatesLocked(GeometryPageState::Ready, false);
            uint64_t copyBytes = 0;
            for (const Candidate &candidate : candidates)
            {
                if (result.UploadsStarted >= m_Config.MaxUploadsPerFrame)
                {
                    break;
                }
                auto it = m_Records.find(candidate.Key);
                if (it == m_Records.end() || it->second.State != GeometryPageState::Ready)
                {
                    continue; // 親の要求で記録が変わった
                }
                Record &record = it->second;
                const uint64_t pageCopyBytes = record.Desc.CopyBytes;

                // 1 フレームのコピーの量とリングの空き。1 ページだけで上限を超えるなら、いつまでも積めないので諦める
                if (pageCopyBytes > m_Config.MaxCopyBytesPerFrame)
                {
                    RejectOversizedLocked(candidate.Key, record, "コピー", pageCopyBytes, m_Config.MaxCopyBytesPerFrame);
                    continue;
                }
                if (copyBytes + pageCopyBytes > m_Config.MaxCopyBytesPerFrame)
                {
                    break;
                }
                if (pageCopyBytes + copyBytes > m_Backend.GetCopyBytesAvailable())
                {
                    ++m_Stats.UploadBlockedFrames;
                    break;
                }

                if (!ParentsResidentLocked(candidate.Key, record))
                {
                    ++m_Stats.ParentWaitSkips;
                    continue;
                }

                // 目標に収まらないときは、このページを守りつつ（親は外さない）LRU で空ける
                if (m_bBudgetLimited && m_ResidentBytes + record.Desc.RegionBytes > m_BudgetBytes)
                {
                    bool bFits = true;
                    while (m_ResidentBytes + record.Desc.RegionBytes > m_BudgetBytes)
                    {
                        if (!EvictOneLocked(&record.Desc.Parents, candidate.Key.MeshId, record.LastRequestedFrame, result))
                        {
                            bFits = false;
                            break;
                        }
                    }
                    if (!bFits)
                    {
                        ++m_Stats.BudgetBlockedFrames;
                        m_bBudgetBlockedLastUpdate = true;
                        break;
                    }
                    // 外した結果、記録の表が変わったので引き直す
                    it = m_Records.find(candidate.Key);
                    if (it == m_Records.end() || it->second.State != GeometryPageState::Ready)
                    {
                        continue;
                    }
                }
                Record &target = it->second;

                switch (m_Backend.BeginUpload(candidate.Key.MeshId, candidate.Key.PageId, target.Data))
                {
                case GeometryPageUploadStart::Queued:
                    target.Data = Container::VariableArray<uint8_t>();
                    target.State = GeometryPageState::Uploading;
                    AddLiveBytes(target);
                    copyBytes += pageCopyBytes;
                    ++result.UploadsStarted;
                    result.CopyBytes += pageCopyBytes;
                    ++m_Stats.PagesUploaded;
                    break;
                case GeometryPageUploadStart::Blocked:
                    ++m_Stats.UploadBlockedFrames;
                    return;
                case GeometryPageUploadStart::Rejected:
                    target.Data.clear();
                    target.State = GeometryPageState::Failed;
                    target.bPermanentlyFailed = true;
                    ++m_Stats.PermanentFailures;
                    break;
                }
            }
        }

        // 7) 優先度の高い順に、要求されたページの読み込みを始める
        void StartReadsLocked(GeometryPageFrameResult &result)
        {
            uint32_t inFlight = 0;
            for (const auto &pair : m_Records)
            {
                if (pair.second.State == GeometryPageState::Reading || pair.second.State == GeometryPageState::Ready)
                {
                    ++inFlight;
                }
            }

            const Container::VariableArray<Candidate> candidates = CollectCandidatesLocked(GeometryPageState::Wanted, true);
            for (const Candidate &candidate : candidates)
            {
                if (result.ReadsStarted >= m_Config.MaxReadsStartedPerFrame || inFlight >= m_Config.MaxReadsInFlight)
                {
                    break;
                }
                auto it = m_Records.find(candidate.Key);
                if (it == m_Records.end())
                {
                    continue;
                }
                Record &record = it->second;
                if (record.Desc.ReadBytes > m_Config.MaxReadBytesPerFrame)
                {
                    RejectOversizedLocked(candidate.Key, record, "読み", record.Desc.ReadBytes, m_Config.MaxReadBytesPerFrame);
                    continue;
                }
                if (result.ReadBytes + record.Desc.ReadBytes > m_Config.MaxReadBytesPerFrame)
                {
                    break;
                }
                if (!m_Backend.BeginRead(candidate.Key.MeshId, candidate.Key.PageId))
                {
                    FailLocked(record);
                    continue;
                }
                record.State = GeometryPageState::Reading;
                ++m_Stats.ReadsStarted;
                ++result.ReadsStarted;
                result.ReadBytes += record.Desc.ReadBytes;
                ++inFlight;
            }
        }

        // 要求が途絶えた未常駐のページ（要求済み・読み込み済み・失敗）を忘れる
        void DropStaleLocked()
        {
            for (auto it = m_Records.begin(); it != m_Records.end();)
            {
                const Record &record = it->second;
                const bool bStaleState = record.State == GeometryPageState::Wanted ||
                                         record.State == GeometryPageState::Ready ||
                                         record.State == GeometryPageState::Failed;
                if (bStaleState && m_Frame > record.LastRequestedFrame &&
                    m_Frame - record.LastRequestedFrame > m_Config.WantedMaxAgeFrames)
                {
                    auto victim = it++;
                    EraseLocked(victim);
                    ++m_Stats.StaleDropped;
                }
                else
                {
                    ++it;
                }
            }
        }

        IGeometryPageBackend &m_Backend;
        Config m_Config;
        mutable Thread::Mutex m_Mutex;
        RecordMap m_Records;
        GeometryPageStreamerStats m_Stats;
        uint64_t m_Frame = 0;
        uint64_t m_ResidentBytes = 0;
        bool m_bBudgetLimited = false;
        uint64_t m_BudgetBytes = 0;
        // 直前の Update が、目標に収まらず書き込みを見送ったか（HasPendingWork が使う）
        bool m_bBudgetBlockedLastUpdate = false;
        // 区画・リングの空きが無くて書き込みを見送った Update が、書き込みの進まないまま続いている数
        uint32_t m_UploadBlockedStreak = 0;
    };
} // namespace NorvesLib::Core::Rendering
