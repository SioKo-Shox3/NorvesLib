// ジオメトリのページのストリーマ（GeometryPageStreamer）と、ページの配置の計算（GeometryPageLayout）の契約テスト。
// 読み込み・区画への書き込み・ページの表への公開・追い出しは偽の窓口で置き換え、GPU もファイルも使わずに、次を確かめる。
//   - 優先度（粗い段が先 → 要求の数が多い → 最後に要求されたフレームが新しい）と、1 フレームの上限（読み・コピー）
//   - 根のページを要求の対象にせず、外さないこと
//   - 書き終える前に公開しないこと（積む → 完了 → 公開の順）
//   - 親のページが常駐するまで子のページを公開せず、親を先に読むこと
//   - 目標を超えたら最後に要求されたフレームが古いページから外すこと・子から外すこと・目標以下に収まること・外せないときは見送ること
//   - 外した区画が、使っていた提出の完了まで再利用されず、完了後に同じ場所を使い回せること（本物のプールと GpuRetireQueue）
//   - 読み込みの失敗の再試行と、メッシュの解放
//   - 配置の計算（範囲の検査・親子の関係・クラスタの記録の位置の書き換えが描画の引き方と合うこと）
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Rendering/GeometryPageStreamer.h"
#include "Rendering/GeometryPool.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/MegaGeometry/GeometryPageLayout.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "RHI/IBuffer.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

using Core::Container::MakeShared;
using Core::Container::TSharedPtr;
using Core::Container::VariableArray;
using Core::Rendering::GeometryPageDescriptor;
using Core::Rendering::GeometryPageReadCompletion;
using Core::Rendering::GeometryPageState;
using Core::Rendering::GeometryPageStreamer;
using Core::Rendering::GeometryPageStreamerConfig;
using Core::Rendering::GeometryPageUploadStart;
using Core::Rendering::GeometryPageUploadState;
using Core::Rendering::GeometryPool;
using Core::Rendering::GpuRetireQueue;
using Core::Rendering::IGeometryBlockFactory;
using Core::Rendering::IGeometryPageBackend;
using Core::Rendering::MegaGeometry::GeometryPageRequestSet;

namespace Layout = Core::Rendering::MegaGeometry::GeometryPageLayout;
using Core::Rendering::MegaGeometry::GPUClusterData;
using Core::Rendering::MegaGeometry::INVALID_PAGE_ID;
using Core::Rendering::MegaGeometry::MeshCluster;
using Core::Rendering::MegaGeometry::MeshPageInfo;

int g_failures = 0;
int g_liveBuffers = 0;

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "GeometryPageStreamerTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

// ---- 偽の窓口 ----

enum class EventKind : uint8_t
{
    BeginRead,
    BeginUpload,
    Publish,
    Evict
};

struct Event
{
    EventKind Kind = EventKind::BeginRead;
    uint64_t MeshId = 0;
    uint32_t PageId = 0;
};

class FakeBackend final : public IGeometryPageBackend
{
public:
    struct Mesh
    {
        bool bAlive = true;
        uint64_t AllocatedVersion = 0;
        VariableArray<GeometryPageDescriptor> Pages;
        VariableArray<uint8_t> Resident;
    };

    // 表の位置は 1000 * メッシュ + ページ
    static uint32_t TableIndex(uint64_t meshId, uint32_t pageId) { return static_cast<uint32_t>(meshId * 1000 + pageId); }

    Mesh &AddMesh(uint64_t meshId, uint32_t pageCount)
    {
        Mesh &mesh = Meshes[meshId];
        mesh.Pages.resize(pageCount);
        mesh.Resident.assign(pageCount, 0);
        return mesh;
    }

    // 独立したページ（親子なし）を作る。ページ 0 は根
    void MakeFlatPages(uint64_t meshId, uint32_t pageCount, uint64_t regionBytes, uint32_t level)
    {
        Mesh &mesh = AddMesh(meshId, pageCount);
        for (uint32_t page = 0; page < pageCount; ++page)
        {
            GeometryPageDescriptor &desc = mesh.Pages[page];
            desc.bRoot = page == 0;
            desc.RegionBytes = regionBytes;
            desc.ReadBytes = regionBytes;
            desc.CopyBytes = regionBytes;
            desc.Level = level;
        }
    }

    bool ResolveRequest(uint32_t tableIndex, uint64_t tableVersion, uint64_t &outMeshId, uint32_t &outPageId) const override
    {
        outMeshId = tableIndex / 1000;
        outPageId = tableIndex % 1000;
        const auto it = Meshes.find(outMeshId);
        return it != Meshes.end() && it->second.bAlive && outPageId < it->second.Pages.size() &&
               it->second.AllocatedVersion <= tableVersion;
    }

    bool GetPageDescriptor(uint64_t meshId, uint32_t pageId, GeometryPageDescriptor &out) const override
    {
        const auto it = Meshes.find(meshId);
        if (it == Meshes.end() || !it->second.bAlive || pageId >= it->second.Pages.size())
        {
            return false;
        }
        out = it->second.Pages[pageId];
        return true;
    }

    bool IsMeshStreamed(uint64_t meshId) const override
    {
        const auto it = Meshes.find(meshId);
        return it != Meshes.end() && it->second.bAlive;
    }

    bool BeginRead(uint64_t meshId, uint32_t pageId) override
    {
        Log.push_back(Event{EventKind::BeginRead, meshId, pageId});
        if (bRefuseReads)
        {
            return false;
        }
        PendingReads.push_back(Event{EventKind::BeginRead, meshId, pageId});
        return true;
    }

    void CollectReads(VariableArray<GeometryPageReadCompletion> &out) override
    {
        if (bHoldReads)
        {
            return;
        }
        for (const Event &read : PendingReads)
        {
            GeometryPageReadCompletion completion;
            completion.MeshId = read.MeshId;
            completion.PageId = read.PageId;
            const uint64_t key = read.MeshId * 1000 + read.PageId;
            if (FailuresLeft.find(key) != FailuresLeft.end() && FailuresLeft[key] > 0)
            {
                --FailuresLeft[key];
                completion.bSucceeded = false;
            }
            else
            {
                completion.bSucceeded = true;
                const auto it = Meshes.find(read.MeshId);
                completion.Data.assign(static_cast<size_t>(it->second.Pages[read.PageId].ReadBytes), 0);
            }
            out.push_back(std::move(completion));
        }
        PendingReads.clear();
    }

    GeometryPageUploadStart BeginUpload(uint64_t meshId, uint32_t pageId, const VariableArray<uint8_t> &data) override
    {
        const auto it = Meshes.find(meshId);
        if (it == Meshes.end() || data.size() != it->second.Pages[pageId].ReadBytes)
        {
            return GeometryPageUploadStart::Rejected;
        }
        if (bBlockUploads)
        {
            return GeometryPageUploadStart::Blocked;
        }
        if (Pool != nullptr)
        {
            GeometryPool::RegionLease lease =
                Pool->AllocateInBlock(0, it->second.Pages[pageId].RegionBytes, GeometryPool::DefaultAlignmentBytes);
            if (!lease.IsValid())
            {
                return GeometryPageUploadStart::Blocked;
            }
            LastOffset[meshId * 1000 + pageId] = lease.GetOffsetBytes();
            Leases[meshId * 1000 + pageId] = std::move(lease);
        }
        Log.push_back(Event{EventKind::BeginUpload, meshId, pageId});
        UploadPolls[meshId * 1000 + pageId] = 0;
        return GeometryPageUploadStart::Queued;
    }

    GeometryPageUploadState PollUpload(uint64_t meshId, uint32_t pageId) const override
    {
        const auto it = Meshes.find(meshId);
        if (it == Meshes.end() || !it->second.bAlive)
        {
            return GeometryPageUploadState::Lost;
        }
        uint32_t &polls = const_cast<FakeBackend *>(this)->UploadPolls[meshId * 1000 + pageId];
        return polls++ >= PollsBeforeComplete ? GeometryPageUploadState::Complete : GeometryPageUploadState::Pending;
    }

    bool Publish(uint64_t meshId, uint32_t pageId) override
    {
        Log.push_back(Event{EventKind::Publish, meshId, pageId});
        Meshes[meshId].Resident[pageId] = 1;
        return true;
    }

    bool Evict(uint64_t meshId, uint32_t pageId) override
    {
        Log.push_back(Event{EventKind::Evict, meshId, pageId});
        Meshes[meshId].Resident[pageId] = 0;
        const uint64_t key = meshId * 1000 + pageId;
        if (Queue != nullptr && Leases.find(key) != Leases.end())
        {
            Queue->Retire(std::move(Leases[key]));
            Leases.erase(key);
        }
        return true;
    }

    uint64_t GetCopyBytesAvailable() const override { return CopyBytesAvailable; }

    size_t Count(EventKind kind) const
    {
        size_t count = 0;
        for (const Event &event : Log)
        {
            count += event.Kind == kind ? 1u : 0u;
        }
        return count;
    }

    // ある種類の n 番目（0 始まり）の事象のページ
    uint32_t PageOf(EventKind kind, size_t n) const
    {
        size_t seen = 0;
        for (const Event &event : Log)
        {
            if (event.Kind == kind && seen++ == n)
            {
                return event.PageId;
            }
        }
        return INVALID_PAGE_ID;
    }

    // 最初に起きた事象の位置（無ければ SIZE_MAX）
    size_t FirstIndexOf(EventKind kind, uint32_t pageId) const
    {
        for (size_t i = 0; i < Log.size(); ++i)
        {
            if (Log[i].Kind == kind && Log[i].PageId == pageId)
            {
                return i;
            }
        }
        return SIZE_MAX;
    }

    Core::Container::Map<uint64_t, Mesh> Meshes;
    VariableArray<Event> Log;
    VariableArray<Event> PendingReads;
    Core::Container::Map<uint64_t, int> FailuresLeft;
    Core::Container::Map<uint64_t, uint32_t> UploadPolls;
    Core::Container::Map<uint64_t, uint64_t> LastOffset;
    Core::Container::Map<uint64_t, GeometryPool::RegionLease> Leases;
    bool bRefuseReads = false;
    bool bHoldReads = false;
    bool bBlockUploads = false;
    uint32_t PollsBeforeComplete = 0;
    uint64_t CopyBytesAvailable = 1ull << 40;
    GeometryPool *Pool = nullptr;
    GpuRetireQueue *Queue = nullptr;
};

GeometryPageStreamerConfig RoomyConfig()
{
    GeometryPageStreamerConfig config;
    config.MaxReadsStartedPerFrame = 64;
    config.MaxReadsInFlight = 256;
    config.MaxReadBytesPerFrame = 1ull << 40;
    config.MaxUploadsPerFrame = 64;
    config.MaxCopyBytesPerFrame = 1ull << 40;
    config.MaxRetries = 3;
    config.RetryDelayFrames = 2;
    config.WantedMaxAgeFrames = 1000;
    return config;
}

GeometryPageRequestSet Requests(uint64_t meshId, std::initializer_list<uint32_t> pages, uint64_t version = 1)
{
    GeometryPageRequestSet set;
    for (const uint32_t page : pages)
    {
        set.Add(FakeBackend::TableIndex(meshId, page), 1, version);
    }
    return set;
}

// 要求を出して、読み込み・書き込み・公開が落ち着くまで Update を回す（最後のフレーム番号を返す）
uint64_t RunUntilIdle(GeometryPageStreamer &streamer, uint64_t startFrame, const GeometryPageRequestSet *firstRequests,
                      int maxFrames = 50)
{
    uint64_t frame = startFrame;
    streamer.Update(frame, firstRequests);
    for (int i = 0; i < maxFrames && streamer.HasPendingWork(); ++i)
    {
        streamer.Update(++frame, nullptr);
    }
    return frame;
}

// ---- 優先度 ----

void TestPriorityOrder()
{
    FakeBackend backend;
    backend.AddMesh(1, 8);
    // 0 は根。1〜2 は段 3（粗い）、3〜5 は段 2、6〜7 は段 1（細かい）
    const uint32_t levels[8] = {0, 3, 3, 2, 2, 2, 1, 1};
    for (uint32_t page = 0; page < 8; ++page)
    {
        GeometryPageDescriptor &desc = backend.Meshes[1].Pages[page];
        desc.bRoot = page == 0;
        desc.RegionBytes = desc.ReadBytes = desc.CopyBytes = 100;
        desc.Level = levels[page];
    }

    GeometryPageStreamerConfig config = RoomyConfig();
    config.MaxReadsStartedPerFrame = 0; // まず要求の数・時刻だけを溜める
    GeometryPageStreamer streamer(backend, config);

    // フレーム 1: 全部を 1 回。フレーム 2: 4 を足す。フレーム 3: 4 と 5 を足す（4 は 3 回、5 は 2 回、他は 1 回）
    GeometryPageRequestSet all = Requests(1, {1, 2, 3, 4, 5, 6, 7});
    streamer.Update(1, &all);
    GeometryPageRequestSet only4 = Requests(1, {4});
    streamer.Update(2, &only4);
    GeometryPageRequestSet pages45 = Requests(1, {4, 5});
    streamer.Update(3, &pages45);
    // 3 が 7 より後に要求されたことにする（同じ段・同じ数なら、最後に要求されたフレームが新しいものが先）
    GeometryPageRequestSet only3 = Requests(1, {3});
    streamer.Update(4, &only3);
    GeometryPageRequestSet only6 = Requests(1, {6});
    streamer.Update(5, &only6);
    // 要求の数: 1:1 2:1 3:2 4:3 5:2 6:2 7:1

    Expect(backend.Count(EventKind::BeginRead) == 0, "読みを始めない設定の間は読み込みを始めない");

    config.MaxReadsStartedPerFrame = 64;
    streamer.SetConfig(config);
    streamer.Update(6, nullptr);

    // 期待: 段 3（1, 2 は数も時刻も同じ → 番号順）→ 段 2（数 3 の 4 → 数 2 の 3, 5 は新しい 3 が先）→ 段 1（数 2 の 6 → 数 1 の 7）
    const uint32_t expected[7] = {1, 2, 4, 3, 5, 6, 7};
    Expect(backend.Count(EventKind::BeginRead) == 7, "全てのページの読み込みを始める");
    for (size_t i = 0; i < 7; ++i)
    {
        Expect(backend.PageOf(EventKind::BeginRead, i) == expected[i], "粗い段 → 要求の数 → 新しさの順に読み込みを始める");
    }
}

// ---- 1 フレームの上限 ----

void TestFrameLimits()
{
    // 読み込みの量: 100 バイトのページを 250 バイトまで（最初の 1 件は必ず通る）
    {
        FakeBackend backend;
        backend.MakeFlatPages(1, 8, 100, 2);
        GeometryPageStreamerConfig config = RoomyConfig();
        config.MaxReadBytesPerFrame = 250;
        backend.bHoldReads = true;
        GeometryPageStreamer streamer(backend, config);
        GeometryPageRequestSet set = Requests(1, {1, 2, 3, 4, 5, 6, 7});
        const auto result = streamer.Update(1, &set);
        Expect(result.ReadsStarted == 2 && result.ReadBytes == 200, "1 フレームの読みの量の上限（250）に収まる 2 件だけ始める");
        streamer.Update(2, nullptr);
        Expect(backend.Count(EventKind::BeginRead) == 4, "次のフレームでさらに 2 件");
    }
    // 読み込みの件数と、読み込み中の数
    {
        FakeBackend backend;
        backend.MakeFlatPages(1, 12, 100, 2);
        GeometryPageStreamerConfig config = RoomyConfig();
        config.MaxReadsStartedPerFrame = 3;
        config.MaxReadsInFlight = 5;
        backend.bHoldReads = true;
        GeometryPageStreamer streamer(backend, config);
        GeometryPageRequestSet set = Requests(1, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
        Expect(streamer.Update(1, &set).ReadsStarted == 3, "1 フレームに始める件数の上限");
        Expect(streamer.Update(2, nullptr).ReadsStarted == 2, "読み込み中の数の上限（5）まで");
        Expect(streamer.Update(3, nullptr).ReadsStarted == 0, "読み込み中が上限なら始めない");
    }
    // コピーの量・件数・リングの空き
    {
        FakeBackend backend;
        backend.MakeFlatPages(1, 8, 100, 2);
        GeometryPageStreamerConfig config = RoomyConfig();
        config.MaxCopyBytesPerFrame = 250;
        GeometryPageStreamer streamer(backend, config);
        GeometryPageRequestSet set = Requests(1, {1, 2, 3, 4, 5, 6});
        streamer.Update(1, &set);            // 6 件を読み始める（完了は次の Update で拾う）
        const auto result = streamer.Update(2, nullptr);
        Expect(result.UploadsStarted == 2 && result.CopyBytes == 200, "1 フレームのコピーの量（250）に収まる 2 件だけ積む");
        Expect(streamer.Update(3, nullptr).UploadsStarted == 2, "残りは次のフレームへ持ち越す");

        FakeBackend backend2;
        backend2.MakeFlatPages(1, 8, 100, 2);
        GeometryPageStreamerConfig config2 = RoomyConfig();
        config2.MaxUploadsPerFrame = 1;
        GeometryPageStreamer streamer2(backend2, config2);
        GeometryPageRequestSet set2 = Requests(1, {1, 2, 3});
        streamer2.Update(1, &set2);
        Expect(streamer2.Update(2, nullptr).UploadsStarted == 1, "1 フレームに積む件数の上限");

        FakeBackend backend3;
        backend3.MakeFlatPages(1, 4, 100, 2);
        backend3.CopyBytesAvailable = 150;
        GeometryPageStreamer streamer3(backend3, RoomyConfig());
        GeometryPageRequestSet set3 = Requests(1, {1, 2});
        streamer3.Update(1, &set3);
        Expect(streamer3.Update(2, nullptr).UploadsStarted == 1, "リングの空き（150）を超えて積まない");
        Expect(streamer3.GetStats().UploadBlockedFrames == 1, "空きが足りなくて見送ったフレームを数える");
    }
}

// ---- 根のページ ----

void TestRootPagesAreIgnored()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 3, 100, 2);
    GeometryPageStreamer streamer(backend, RoomyConfig());
    streamer.SetResidentBudget(true, 0); // 目標 0 でも、根のページは外さない（そもそも記録しない）

    GeometryPageRequestSet set = Requests(1, {0, 1});
    streamer.Update(1, &set);
    Expect(streamer.GetPageState(1, 0) == GeometryPageState::None, "根のページは要求の対象にしない");
    Expect(backend.Count(EventKind::BeginRead) == 0 || backend.PageOf(EventKind::BeginRead, 0) != 0,
           "根のページは読み込まない");
    Expect(streamer.GetStats().InvalidRequests == 1, "根のページの要求は無効として数える");
    for (int frame = 2; frame < 12; ++frame)
    {
        streamer.Update(frame, nullptr);
    }
    Expect(backend.PageOf(EventKind::Evict, 0) == INVALID_PAGE_ID, "根のページを外さない");

    // 解決できない要求（古い表の版・解放されたメッシュ）は捨てる
    FakeBackend backend2;
    backend2.MakeFlatPages(1, 3, 100, 2);
    backend2.Meshes[1].AllocatedVersion = 10;
    GeometryPageStreamer streamer2(backend2, RoomyConfig());
    GeometryPageRequestSet stale = Requests(1, {1}, 5);
    streamer2.Update(1, &stale);
    Expect(streamer2.GetStats().InvalidRequests == 1 && backend2.Count(EventKind::BeginRead) == 0,
           "要求を書いた版より後に割り当てた範囲の要求は捨てる");
}

// ---- 書き終える前に公開しない ----

void TestPublishAfterUploadComplete()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 3, 100, 2);
    backend.PollsBeforeComplete = 2;
    GeometryPageStreamer streamer(backend, RoomyConfig());

    GeometryPageRequestSet set = Requests(1, {1});
    streamer.Update(1, &set);
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Reading, "要求のフレームで読み込みを始める");
    streamer.Update(2, nullptr);
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Uploading, "読み終えたら書き込みを積む");
    Expect(backend.Meshes[1].Resident[1] == 0, "書き込み中は公開しない");
    streamer.Update(3, nullptr); // 1 回目の問い合わせ: まだ
    streamer.Update(4, nullptr); // 2 回目: まだ
    Expect(backend.Meshes[1].Resident[1] == 0 && backend.Count(EventKind::Publish) == 0,
           "GPU で書き終わったと言われるまで公開しない");
    streamer.Update(5, nullptr); // 3 回目: 完了
    Expect(backend.Meshes[1].Resident[1] == 1 && streamer.GetPageState(1, 1) == GeometryPageState::Resident,
           "書き終えたら公開して常駐にする");
    Expect(backend.FirstIndexOf(EventKind::BeginUpload, 1) < backend.FirstIndexOf(EventKind::Publish, 1),
           "積む → 公開の順");
    Expect(!streamer.HasPendingWork(), "全て常駐したら落ち着く");
}

// ---- 親のページが先 ----

void TestParentsFirst()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 4, 100, 2);
    // ページ 2 の親は 1。ページ 3 の親は 1 と 2
    backend.Meshes[1].Pages[2].Parents.push_back(1);
    backend.Meshes[1].Pages[1].Children.push_back(2);
    backend.Meshes[1].Pages[3].Parents.push_back(1);
    backend.Meshes[1].Pages[3].Parents.push_back(2);
    backend.Meshes[1].Pages[1].Children.push_back(3);
    backend.Meshes[1].Pages[2].Children.push_back(3);
    GeometryPageStreamer streamer(backend, RoomyConfig());

    // 一番細かいページ 3 だけを要求しても、親を先に読んで常駐させ、親が揃ってから公開する
    GeometryPageRequestSet set = Requests(1, {3});
    RunUntilIdle(streamer, 1, &set);

    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Resident &&
               streamer.GetPageState(1, 2) == GeometryPageState::Resident &&
               streamer.GetPageState(1, 3) == GeometryPageState::Resident,
           "親が先に常駐し、子も常駐する");
    const size_t publish1 = backend.FirstIndexOf(EventKind::Publish, 1);
    const size_t publish2 = backend.FirstIndexOf(EventKind::Publish, 2);
    const size_t upload2 = backend.FirstIndexOf(EventKind::BeginUpload, 2);
    const size_t upload3 = backend.FirstIndexOf(EventKind::BeginUpload, 3);
    const size_t publish3 = backend.FirstIndexOf(EventKind::Publish, 3);
    Expect(publish1 < upload2 && publish1 < upload3, "親が公開されるまで、子の書き込みを積まない");
    Expect(publish2 < upload3 && publish2 < publish3, "全ての親が公開されるまで、子を公開しない");
    Expect(streamer.GetStats().ParentWaitSkips > 0, "親を待ったことを数える");
}

// ---- 追い出し ----

// 独立した 4 つのページ（1〜4）を、要求のフレームをずらして常駐させる。最後に要求されたフレーム: 1:1 2:2 3:3 4:4
void LoadFourPages(GeometryPageStreamer &streamer, FakeBackend &backend, uint64_t &nextFrame)
{
    for (uint32_t page = 1; page <= 4; ++page)
    {
        GeometryPageRequestSet set = Requests(1, {page});
        nextFrame = RunUntilIdle(streamer, nextFrame, &set) + 1;
    }
    for (uint32_t page = 1; page <= 4; ++page)
    {
        Expect(streamer.GetPageState(1, page) == GeometryPageState::Resident, "4 つのページが常駐している");
    }
    Expect(backend.Count(EventKind::Evict) == 0, "目標が無い間は外さない");
}

void TestEvictionOrderAndBudget()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 5, 100, 2);
    GeometryPageStreamer streamer(backend, RoomyConfig());
    uint64_t frame = 1;
    LoadFourPages(streamer, backend, frame);
    Expect(streamer.GetResidentBytes() == 400, "常駐の量は 400");

    // ページ 2 をもう一度要求して、最後に要求されたフレームを新しくする（古い順: 1, 3, 4, 2）
    GeometryPageRequestSet refresh = Requests(1, {2});
    streamer.Update(frame++, &refresh);

    // 目標を 250 へ下げる → 古い 1 と 3 を外して 200（≤ 250）に収める
    streamer.SetResidentBudget(true, 250);
    streamer.Update(frame++, nullptr);
    Expect(backend.Count(EventKind::Evict) == 2, "目標を超えた分だけ外す（2 件）");
    Expect(backend.PageOf(EventKind::Evict, 0) == 1 && backend.PageOf(EventKind::Evict, 1) == 3,
           "最後に要求されたフレームが古いページから外す");
    Expect(streamer.GetResidentBytes() == 200 && streamer.GetResidentBytes() <= 250, "目標以下に収まる");
    Expect(streamer.GetPageState(1, 2) == GeometryPageState::Resident && streamer.GetPageState(1, 4) == GeometryPageState::Resident,
           "要求が新しいページは残る");
    Expect(streamer.GetStats().EvictedPages == 2, "追い出した数を数える");
    Expect(backend.Meshes[1].Resident[1] == 0 && backend.Meshes[1].Resident[3] == 0, "外したページは表で非常駐になる");

    // 目標が 0 でも根のページは外さない。非根のページだけがなくなる
    streamer.SetResidentBudget(true, 0);
    streamer.Update(frame++, nullptr);
    Expect(streamer.GetResidentBytes() == 0 && backend.Count(EventKind::Evict) == 4, "目標 0 なら非根のページを全部外す");
    Expect(backend.PageOf(EventKind::Evict, 2) == 4 && backend.PageOf(EventKind::Evict, 3) == 2, "残りも古い順");
    for (size_t i = 0; i < backend.Count(EventKind::Evict); ++i)
    {
        Expect(backend.PageOf(EventKind::Evict, i) != 0, "根のページを外さない");
    }

    // 外したページは、要求があればもう一度読み込める
    GeometryPageRequestSet again = Requests(1, {1});
    streamer.SetResidentBudget(false, 0);
    RunUntilIdle(streamer, frame, &again);
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Resident, "外したページを再び読み込める");
}

void TestEvictChildrenFirst()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 5, 100, 2);
    // 1 -> 2 -> 3 の鎖（1 が最も粗い親）。4 は独立
    backend.Meshes[1].Pages[2].Parents.push_back(1);
    backend.Meshes[1].Pages[1].Children.push_back(2);
    backend.Meshes[1].Pages[3].Parents.push_back(2);
    backend.Meshes[1].Pages[2].Children.push_back(3);
    GeometryPageStreamer streamer(backend, RoomyConfig());

    uint64_t frame = 1;
    GeometryPageRequestSet first = Requests(1, {3}); // 親の 1・2 も呼ばれて、1 → 2 → 3 の順に常駐する
    frame = RunUntilIdle(streamer, frame, &first) + 1;
    GeometryPageRequestSet other = Requests(1, {4});
    frame = RunUntilIdle(streamer, frame, &other) + 1;
    Expect(streamer.GetResidentBytes() == 400, "鎖の 3 つと独立の 1 つが常駐");

    // 1 つ外す: 最も古いのは親の 1 だが、子の 2 が常駐しているので外せない。鎖の端（3）か 4 のうち古い 3 が先
    streamer.SetResidentBudget(true, 300);
    streamer.Update(frame++, nullptr);
    Expect(backend.Count(EventKind::Evict) == 1 && backend.PageOf(EventKind::Evict, 0) == 3,
           "子（細かい側）から外す。親は子が常駐している間は外さない");

    // もう 1 つ: 今度は 2 が子を持たない。1 は 2 がいるので外せない
    streamer.SetResidentBudget(true, 200);
    streamer.Update(frame++, nullptr);
    Expect(backend.Count(EventKind::Evict) == 2 && backend.PageOf(EventKind::Evict, 1) == 2,
           "子が外れた後で、その親を外せる");

    // 鎖の親は、子の書き込み中も外さない: 3 を要求し直す間、目標を満たすために親を外そうとしても 2 は守る
    streamer.SetResidentBudget(false, 0);
    GeometryPageRequestSet rebuild = Requests(1, {3});
    frame = RunUntilIdle(streamer, frame, &rebuild) + 1;
    Expect(streamer.GetPageState(1, 3) == GeometryPageState::Resident && streamer.GetResidentBytes() == 400,
           "鎖が再び常駐する");
}

void TestBudgetBlockedWhenNothingEvictable()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 3, 100, 2);
    backend.Meshes[1].Pages[2].Parents.push_back(1);
    backend.Meshes[1].Pages[1].Children.push_back(2);
    GeometryPageStreamer streamer(backend, RoomyConfig());

    // 目標は 100（ページ 1 つ分）。ページ 2 を要求すると、親の 1 だけが常駐できる
    streamer.SetResidentBudget(true, 100);
    GeometryPageRequestSet set = Requests(1, {2});
    uint64_t frame = RunUntilIdle(streamer, 1, &set);
    for (int i = 0; i < 5; ++i)
    {
        streamer.Update(++frame, &set);
    }
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Resident, "親は目標に収まるので常駐する");
    Expect(streamer.GetPageState(1, 2) == GeometryPageState::Ready, "子は目標に収まらず、読み込み済みのまま待つ");
    Expect(backend.PageOf(EventKind::Evict, 0) == INVALID_PAGE_ID, "子を入れるために親を外さない");
    Expect(streamer.GetStats().BudgetBlockedFrames > 0, "目標に収まらず見送ったフレームを数える");
    Expect(streamer.GetResidentBytes() <= 100, "目標を超えない");
    Expect(!streamer.HasPendingWork(), "目標に収まらず進められないものは、落ち着かない待ちとして数えない");
}

// ---- 外した区画の再利用の順序（本物のプールと GpuRetireQueue） ----

class FakeBlockBuffer final : public RHI::IBuffer
{
public:
    explicit FakeBlockBuffer(uint64_t size) : Size(size) { ++g_liveBuffers; }
    ~FakeBlockBuffer() override { --g_liveBuffers; }

    uint64_t GetSize() const override { return Size; }
    void *Map(uint64_t, uint64_t) override { return nullptr; }
    void Unmap() override {}
    void Update(const void *, uint64_t, uint64_t) override {}
    RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::StorageBuffer; }

    uint64_t Size = 0;
};

class FakeBlockFactory final : public IGeometryBlockFactory
{
public:
    RHI::BufferPtr CreateBlock(uint64_t sizeBytes) override { return MakeShared<FakeBlockBuffer>(sizeBytes); }
};

void TestRetiredRegionReuseOrder()
{
    constexpr uint64_t PageBytes = 16 * 1024;
    GpuRetireQueue queue;
    {
        GeometryPool pool(TSharedPtr<IGeometryBlockFactory>(MakeShared<FakeBlockFactory>()), 4 * PageBytes);
        // メッシュの区画（根のページ）が塊の先頭を占める。塊は 4 区画分で、ページに使えるのは残りの 3 つ
        GeometryPool::RegionLease meshRegion = pool.Allocate(PageBytes, 256);
        Expect(meshRegion.IsValid() && meshRegion.GetBlockIndex() == 0, "メッシュの区画が塊 0 を作る");

        FakeBackend backend;
        backend.MakeFlatPages(1, 5, PageBytes, 2);
        backend.Pool = &pool;
        backend.Queue = &queue;
        GeometryPageStreamer streamer(backend, RoomyConfig());
        streamer.SetResidentBudget(true, 3 * PageBytes);

        // 提出 serial: フレーム f を f で提出する。GPU の完了は completed まで進んでいる
        uint64_t completed = 0;
        auto runFrame = [&](uint64_t frame, const GeometryPageRequestSet *set)
        {
            queue.BeginFrame(completed);
            streamer.Update(frame, set);
            queue.CommitFrame(frame);
        };

        // フレーム 1〜7: ページ 1・2・3 を順に常駐させる（最後に要求したのは 1 が最も古い）。
        // 要求のフレームで読み始め、次のフレームで読み終えて書き込みを積み、その次のフレームで公開する
        for (uint32_t page = 1; page <= 3; ++page)
        {
            GeometryPageRequestSet set = Requests(1, {page});
            runFrame(page * 2 - 1, &set);
            runFrame(page * 2, nullptr);
            completed = page * 2;
        }
        runFrame(7, nullptr);
        completed = 7;
        Expect(streamer.GetPageState(1, 1) == GeometryPageState::Resident &&
                   streamer.GetPageState(1, 2) == GeometryPageState::Resident &&
                   streamer.GetPageState(1, 3) == GeometryPageState::Resident,
               "ページ 3 つが常駐している");
        Expect(pool.GetStats().UsedBytes == 4 * PageBytes, "塊が全て使われている");
        const uint64_t page1Offset = backend.LastOffset[1 * 1000 + 1];

        // フレーム 10: GPU の完了は 7 のまま、ページ 4 を要求する（読み終えるのは 11）。目標を超えるので最も古い 1 を外すが、
        // 外した区画はまだ GPU が使っているかもしれず、使い回せない
        GeometryPageRequestSet want4 = Requests(1, {4});
        runFrame(10, &want4);
        runFrame(11, nullptr);
        Expect(backend.PageOf(EventKind::Evict, 0) == 1, "最後に要求されたフレームが最も古い 1 を外す");
        Expect(streamer.GetPageState(1, 4) == GeometryPageState::Ready,
               "外した区画が返却待ちの間は、ページ 4 の区画を借りられず書き込めない");
        Expect(pool.GetStats().UsedBytes == 4 * PageBytes && queue.GetPendingCount() == 1,
               "外した区画は完了待ちで、プールの空きへ戻らない");
        Expect(streamer.GetStats().UploadBlockedFrames > 0, "区画が無くて見送ったフレームを数える");

        // 外したページを使っていた提出（外したフレーム 11 まで）が完了すると、区画が戻って、同じ場所をページ 4 が使う
        completed = 11;
        runFrame(12, nullptr);
        runFrame(13, nullptr);
        Expect(streamer.GetPageState(1, 4) == GeometryPageState::Resident, "完了後にページ 4 が常駐する");
        Expect(backend.LastOffset[1 * 1000 + 4] == page1Offset, "外したページの区画の場所を使い回す");
        Expect(streamer.GetResidentBytes() <= 3 * PageBytes, "目標以下に収まる");
        Expect(queue.GetPendingCount() == 0, "返却待ちが無くなる");

        backend.Leases.clear();
        queue.Clear();
    }
    Expect(g_liveBuffers == 0, "片付けたら塊のバッファは残らない");
}

// ---- 失敗とメッシュの解放 ----

void TestReadFailureRetry()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 3, 100, 2);
    backend.FailuresLeft[1 * 1000 + 1] = 2;
    GeometryPageStreamerConfig config = RoomyConfig();
    config.MaxRetries = 3;
    config.RetryDelayFrames = 2;
    GeometryPageStreamer streamer(backend, config);

    GeometryPageRequestSet set = Requests(1, {1});
    streamer.Update(1, &set);
    streamer.Update(2, nullptr); // 失敗を拾う
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Failed, "読み込みに失敗したら失敗の待ちへ");
    Expect(streamer.GetStats().ReadsFailed == 1, "失敗を数える");
    streamer.Update(3, nullptr);
    Expect(backend.Count(EventKind::BeginRead) == 1, "待ちの間は再試行しない");
    streamer.Update(4, nullptr); // 2 フレーム後（RetryDelayFrames × 失敗 1 回）
    Expect(backend.Count(EventKind::BeginRead) == 2, "待ちが明けたら再試行する");
    // 2 回目も失敗し、3 回目で成功する
    for (uint64_t frame = 5; frame < 20; ++frame)
    {
        streamer.Update(frame, nullptr);
    }
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::Resident, "再試行で成功したら常駐する");
    Expect(backend.Count(EventKind::BeginRead) == 3, "再試行は失敗の回数だけ");

    // 失敗が上限に達したら諦める
    FakeBackend backend2;
    backend2.MakeFlatPages(1, 3, 100, 2);
    backend2.FailuresLeft[1 * 1000 + 1] = 100;
    GeometryPageStreamer streamer2(backend2, config);
    GeometryPageRequestSet set2 = Requests(1, {1});
    streamer2.Update(1, &set2);
    for (uint64_t frame = 2; frame < 40; ++frame)
    {
        streamer2.Update(frame, nullptr);
    }
    Expect(streamer2.GetStats().PermanentFailures == 1 && backend2.Count(EventKind::BeginRead) == 3,
           "失敗が上限（3 回）に達したら諦めて、それ以上読まない");
    Expect(!streamer2.HasPendingWork(), "諦めたページは待ちに数えない");

    // 読み込みを始められない（ジョブを積めない）ときも失敗として再試行の待ちへ
    FakeBackend backend3;
    backend3.MakeFlatPages(1, 3, 100, 2);
    backend3.bRefuseReads = true;
    GeometryPageStreamer streamer3(backend3, config);
    GeometryPageRequestSet set3 = Requests(1, {1});
    streamer3.Update(1, &set3);
    Expect(streamer3.GetPageState(1, 1) == GeometryPageState::Failed, "読み込みを始められなければ失敗の待ちへ");
}

void TestMeshReleased()
{
    FakeBackend backend;
    backend.MakeFlatPages(1, 4, 100, 2);
    backend.MakeFlatPages(2, 4, 100, 2);
    GeometryPageStreamer streamer(backend, RoomyConfig());
    GeometryPageRequestSet set;
    set.Add(FakeBackend::TableIndex(1, 1), 1, 1);
    set.Add(FakeBackend::TableIndex(1, 2), 1, 1);
    set.Add(FakeBackend::TableIndex(2, 1), 1, 1);
    RunUntilIdle(streamer, 1, &set);
    Expect(streamer.GetResidentBytes() == 300, "2 つのメッシュの 3 ページが常駐");

    backend.Meshes[1].bAlive = false; // メッシュ 1 が解放された
    streamer.Update(100, nullptr);
    Expect(streamer.GetResidentBytes() == 100, "解放されたメッシュのページの分が常駐の量から引かれる");
    Expect(streamer.GetPageState(1, 1) == GeometryPageState::None && streamer.GetPageState(2, 1) == GeometryPageState::Resident,
           "解放されたメッシュの記録だけを捨てる");
    Expect(backend.Count(EventKind::Evict) == 0, "解放されたメッシュのページを外す呼び出しはしない（窓口が片付ける）");
}

// ---- 配置の計算 ----

MeshCluster MakeCluster(uint32_t pageId, uint32_t indexOffset, uint32_t indexCount, int32_t vertexOffset, uint32_t vertexCount)
{
    MeshCluster cluster;
    cluster.PageId = pageId;
    cluster.IndexOffset = indexOffset;
    cluster.IndexCount = indexCount;
    cluster.VertexOffset = vertexOffset;
    cluster.VertexCount = vertexCount;
    return cluster;
}

void TestValidatePages()
{
    // ページ 0・1 が根、2・3 が通常。頂点: 10/20/30/40、インデックス: 30/60/90/120 + フォールバック 6
    VariableArray<MeshPageInfo> pages(4);
    uint32_t firstCluster = 0;
    uint32_t firstVertex = 0;
    uint32_t firstIndex = 0;
    const uint32_t vertexCounts[4] = {10, 20, 30, 40};
    const uint32_t indexCounts[4] = {30, 60, 90, 120};
    const uint32_t clusterCounts[4] = {1, 2, 3, 4};
    for (uint32_t i = 0; i < 4; ++i)
    {
        pages[i].bRoot = i < 2;
        pages[i].FirstCluster = firstCluster;
        pages[i].ClusterCount = clusterCounts[i];
        pages[i].FirstVertex = firstVertex;
        pages[i].VertexCount = vertexCounts[i];
        pages[i].FirstIndex = firstIndex;
        pages[i].IndexCount = indexCounts[i];
        firstCluster += clusterCounts[i];
        firstVertex += vertexCounts[i];
        firstIndex += indexCounts[i];
    }
    VariableArray<MeshCluster> clusters;
    for (uint32_t i = 0; i < 4; ++i)
    {
        // ページの中で、クラスタが頂点・インデックスの範囲を等分する
        for (uint32_t c = 0; c < clusterCounts[i]; ++c)
        {
            const uint32_t vertexSlice = vertexCounts[i] / clusterCounts[i];
            const uint32_t indexSlice = indexCounts[i] / clusterCounts[i];
            clusters.push_back(MakeCluster(i, pages[i].FirstIndex + c * indexSlice, indexSlice,
                                           static_cast<int32_t>(pages[i].FirstVertex + c * vertexSlice), vertexSlice));
        }
    }
    const uint32_t clusterIndexTotal = firstIndex; // 300
    VariableArray<uint32_t> indices(clusterIndexTotal + 6, 0);
    for (uint32_t i = 0; i < 6; ++i)
    {
        indices[clusterIndexTotal + i] = i % 30; // フォールバックの三角形は根のページの頂点（0〜29）だけを指す
    }

    Layout::RootExtent root;
    Expect(Layout::ValidatePages(pages, clusters, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, root),
           "整ったページの範囲は受け入れる");
    Expect(root.PageCount == 2 && root.ClusterCount == 3 && root.VertexCount == 30 && root.IndexCount == 90,
           "根のページが占める範囲（ページ・クラスタ・頂点・インデックス）を返す");
    Expect(Layout::ComputeResidentIndexCount(root, 6) == 96 && Layout::ComputeResidentFallbackOffset(root) == 90,
           "区画へ書くインデックスは、根のインデックスにフォールバックを続ける");

    {
        VariableArray<MeshPageInfo> broken = pages;
        broken[1].bRoot = false;
        broken[2].bRoot = true; // 根のページが先頭から連続しない
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(broken, clusters, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, unused),
               "根のページが連続しなければ拒否する");
    }
    {
        VariableArray<MeshPageInfo> broken = pages;
        broken[2].FirstVertex += 1; // 隙間
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(broken, clusters, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, unused),
               "頂点の範囲に隙間があれば拒否する");
    }
    {
        VariableArray<MeshCluster> broken = clusters;
        broken[5].VertexCount += 100; // ページの頂点の範囲を超える
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(pages, broken, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, unused),
               "クラスタがページの頂点の範囲を超えたら拒否する");
    }
    {
        VariableArray<MeshCluster> broken = clusters;
        broken[4].PageId = 3; // クラスタのページの番号が、並びと食い違う
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(pages, broken, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, unused),
               "クラスタのページの番号が範囲と食い違えば拒否する");
    }
    {
        VariableArray<uint32_t> broken = indices;
        broken[clusterIndexTotal] = 30; // 根のページでない頂点を指す
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(pages, clusters, firstVertex, clusterIndexTotal + 6, broken.data(), clusterIndexTotal, 6, unused),
               "フォールバックが常駐しない頂点を指せば拒否する");
    }
    {
        VariableArray<MeshPageInfo> single(1);
        single[0].bRoot = true;
        Layout::RootExtent unused;
        Expect(!Layout::ValidatePages(single, clusters, firstVertex, clusterIndexTotal + 6, indices.data(), clusterIndexTotal, 6, unused),
               "ページが 1 つなら（ストリーミングの対象ではないので）拒否する");
    }
}

void TestPageRelations()
{
    // クラスタとその子のページ（自分を作ったグループを持つページ。最も細かい段のクラスタは INVALID_PAGE_ID）。
    //   0: ページ 0 → 子 1    1: ページ 0 → 子 2    2: ページ 1 → 子 3    3: ページ 3 → 無し    4: ページ 2 → 子 3
    VariableArray<MeshCluster> clusters;
    VariableArray<uint32_t> childPages;
    const uint32_t clusterPages[5] = {0, 0, 1, 3, 2};
    const uint32_t clusterChildren[5] = {1, 2, 3, INVALID_PAGE_ID, 3};
    for (int i = 0; i < 5; ++i)
    {
        clusters.push_back(MakeCluster(clusterPages[i], 0, 0, 0, 0));
        childPages.push_back(clusterChildren[i]);
    }
    // 同じ関係の重複（0 → 1）、自分自身のページ（1 → 1）、範囲外の子（2 → 99）
    clusters.push_back(MakeCluster(0, 0, 0, 0, 0));
    childPages.push_back(1u);
    clusters.push_back(MakeCluster(1, 0, 0, 0, 0));
    childPages.push_back(1u);
    clusters.push_back(MakeCluster(2, 0, 0, 0, 0));
    childPages.push_back(99u);

    Layout::PageRelations relations;
    Layout::ComputePageRelations(clusters, childPages, 4, relations);
    Expect(relations.Children[0].size() == 2 && relations.Children[0][0] == 1 && relations.Children[0][1] == 2,
           "ページ 0 の子は 1 と 2（重複なし・昇順）");
    Expect(relations.Children[1].size() == 1 && relations.Children[1][0] == 3, "ページ 1 の子は 3（自分自身は子にしない）");
    Expect(relations.Children[2].size() == 1 && relations.Children[2][0] == 3, "ページ 2 の子は 3（範囲外の子は無視する）");
    Expect(relations.Children[3].empty(), "最も細かいページは子を持たない");
    Expect(relations.Parents[3].size() == 2 && relations.Parents[3][0] == 1 && relations.Parents[3][1] == 2,
           "ページ 3 の親は 1 と 2（親が複数ありうる）");
    Expect(relations.Parents[1].size() == 1 && relations.Parents[1][0] == 0, "ページ 1 の親は 0");
    Expect(relations.Parents[2].size() == 1 && relations.Parents[2][0] == 0, "ページ 2 の親は 0");
    Expect(relations.Parents[0].empty(), "最も粗いページは親を持たない");
}

void TestPageRegionLayoutAndPatch()
{
    const Layout::PageRegionLayout layout = Layout::ComputePageRegionLayout(10, 30);
    Expect(layout.VertexBytes == 320 && layout.IndexBytes == 120, "頂点 10 個（320 バイト）・インデックス 30 個（120 バイト）");
    Expect(layout.IndexOffsetBytes == 512 && layout.RegionBytes == 512 + 120, "インデックスは頂点の後ろに 256 バイト整列で置く");

    // メッシュの区画: 塊の先頭から 4096 バイト目（頂点 128・インデックス 1024 の基点）。ページの区画は塊の 256 バイト目
    // （メッシュより前にある。基点の差が負になる）
    Layout::RegionBases meshBases;
    meshBases.VertexBase = 4096 / 32 + 8;  // メッシュの頂点領域は区画の先頭から 256 バイト後（8 頂点）
    meshBases.IndexBase = (4096 + 1024) / 4;
    const uint64_t pageOffset = 256;
    const Layout::RegionBases pageBases = Layout::ComputePageRegionBases(pageOffset, layout);
    Expect(pageBases.VertexBase == 8 && pageBases.IndexBase == (256 + 512) / 4, "ページの区画の基点（頂点・インデックスの単位）");

    // ページ（全体の位置: 頂点 100〜、インデックス 500〜）のクラスタ 2 つ
    MeshPageInfo page;
    page.FirstCluster = 7;
    page.ClusterCount = 2;
    page.FirstVertex = 100;
    page.VertexCount = 10;
    page.FirstIndex = 500;
    page.IndexCount = 30;
    GPUClusterData records[2] = {};
    records[0].IndexOffset = 500;
    records[0].VertexOffset = 100;
    records[1].IndexOffset = 515;
    records[1].VertexOffset = 105;
    records[1].BoundsRadius = 3.5f;
    GPUClusterData patched[2] = {};
    Layout::PatchClusterRecords(records, page, pageBases, meshBases, patched);

    // 描画: firstIndex = メッシュのインデックスの基点 + クラスタの位置、vertexOffset = メッシュの頂点の基点 + クラスタの位置
    Expect(Layout::ResolveFirstIndex(meshBases.IndexBase, patched[0].IndexOffset) ==
               static_cast<uint32_t>(pageBases.IndexBase + 0),
           "描画の firstIndex がページの区画のインデックスを指す（基点の差が負でも剰余で合う）");
    Expect(Layout::ResolveFirstIndex(meshBases.IndexBase, patched[1].IndexOffset) ==
               static_cast<uint32_t>(pageBases.IndexBase + 15),
           "ページの中の位置（15）が保たれる");
    Expect(Layout::ResolveVertexOffset(meshBases.VertexBase, patched[0].VertexOffset) == pageBases.VertexBase,
           "描画の vertexOffset がページの区画の頂点を指す");
    Expect(Layout::ResolveVertexOffset(meshBases.VertexBase, patched[1].VertexOffset) == pageBases.VertexBase + 5,
           "ページの中の頂点の位置（5）が保たれる");
    Expect(patched[1].BoundsRadius == 3.5f && patched[1].IndexCount == records[1].IndexCount,
           "位置以外のクラスタの記録は変えない");
    Expect(patched[0].IndexOffset > 0x80000000u, "基点の差が負のとき、クラスタのインデックスの位置は 2 の補数の大きな値になる");

    // ページの区画がメッシュより後ろにある場合
    const Layout::RegionBases later = Layout::ComputePageRegionBases(1 << 20, layout);
    GPUClusterData patchedLater[2] = {};
    Layout::PatchClusterRecords(records, page, later, meshBases, patchedLater);
    Expect(Layout::ResolveFirstIndex(meshBases.IndexBase, patchedLater[1].IndexOffset) == static_cast<uint32_t>(later.IndexBase + 15) &&
               Layout::ResolveVertexOffset(meshBases.VertexBase, patchedLater[1].VertexOffset) == later.VertexBase + 5,
           "ページの区画がメッシュの後ろでも描画の引き方と合う");
}

int RunTest()
{
    TestPriorityOrder();
    TestFrameLimits();
    TestRootPagesAreIgnored();
    TestPublishAfterUploadComplete();
    TestParentsFirst();
    TestEvictionOrderAndBudget();
    TestEvictChildrenFirst();
    TestBudgetBlockedWhenNothingEvictable();
    TestRetiredRegionReuseOrder();
    TestReadFailureRetry();
    TestMeshReleased();
    TestValidatePages();
    TestPageRelations();
    TestPageRegionLayoutAndPatch();

    if (g_failures != 0)
    {
        return 1;
    }
    std::cout << "GeometryPageStreamerTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
