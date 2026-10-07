// 統合骨格のworker/ready/owner公開を一つの有限instanceで所有する。
#include "Animation/SkeletalAssetRuntime.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
#include "Resource/SkeletalAssetPublication.h"
#include "Resource/SkeletalAssetPublicationTestAccess.h"
#include "Asset/AssetPath.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Thread/JobSystem.h"
#include "Thread/ConditionVariable.h"
#include "Container/UnorderedMap.h"
#include "Text/IdentityPool.h"
#include <cassert>
#include <chrono>
#include <charconv>
#include <limits>
#include <type_traits>
#include <utility>
namespace NorvesLib::Core
{
    namespace C = Container;
    namespace R = ResourceIO;
    namespace D = ResourceIO::Detail;
    using S = SkeletalRuntimeStatus;
    using F = SkeletalRuntimeFailure;
    namespace
    {
        // instance単位の再入検出。別runtimeのowner配送は妨げない。
        struct RuntimeScope
        {
            const void* State;
            RuntimeScope* Previous;
            static thread_local RuntimeScope* Top;
            explicit RuntimeScope(const void* state) : State(state), Previous(Top)
            {
                Top = this;
            }
            ~RuntimeScope()
            {
                Top = Previous;
            }
            static bool Contains(const void* state)
            {
                for (auto* p = Top; p; p = p->Previous)
                {
                    if (p->State == state)
                    {
                        return true;
                    }
                }
                return false;
            }
        };
        thread_local RuntimeScope* RuntimeScope::Top = nullptr;
        bool CoreText(C::AnsiStringView input, C::String& out)
        {
            using Char = C::String::value_type;
            const C::Span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(input.data()), input.size()};
            const auto m = Asset::MeasureSkeletalNameDecoding<Char>(2, bytes);
            if (!m.Succeeded())
            {
                return false;
            }
            C::VariableArray<Char> chars(m.CodeUnitCount);
            if (!Asset::DecodeSkeletalWireName<Char>(2, bytes, C::Span<Char>{chars.data(), chars.size()}).Succeeded())
            {
                return false;
            }
            out = C::String(C::TStringView<Char>{chars.data(), chars.size()});
            return true;
        }
        bool AppendNumber(C::AnsiString& text, uint64_t value)
        {
            char chars[32];
            const auto r = std::to_chars(chars, chars + sizeof(chars), value);
            if (r.ec != std::errc())
            {
                return false;
            }
            text.append(chars, static_cast<size_t>(r.ptr - chars));
            text += ":";
            return true;
        }
        F LoadFailure(R::SkeletalAssetLoadStatus s)
        {
            using L = R::SkeletalAssetLoadStatus;
            switch (s)
            {
            case L::Success:
                return F::None;
            case L::InvalidRequest:
                return F::InvalidRequest;
            case L::ResolveRejected:
                return F::ResolveRejected;
            case L::FormatRejected:
                return F::FormatRejected;
            case L::ParseRejected:
                return F::ParseRejected;
            case L::MetadataMismatch:
                return F::MetadataMismatch;
            case L::InvalidCpuResult:
                return F::InvalidCpu;
            case L::WrongOwnerThread:
                return F::WrongOwner;
            case L::RegistryNotReady:
                return F::RegistryNotReady;
            case L::ResourceCreateFailed:
                return F::ResourceCreateFailed;
            case L::ResourceLoadFailed:
                return F::ResourceLoadFailed;
            case L::InjectedFailure:
                return F::AssemblyInjectedFailure;
            case L::Exception:
                return F::AssemblyException;
            }
            return F::OwnerException;
        }
        F SplitLoadFailure(R::RigSplitLoadStatus s)
        {
            using L = R::RigSplitLoadStatus;
            switch (s)
            {
            case L::Success:
                return F::None;
            case L::InvalidRequest:
                return F::InvalidRequest;
            case L::ResolveRejected:
                return F::ResolveRejected;
            case L::FormatRejected:
                return F::FormatRejected;
            case L::ParseRejected:
                return F::ParseRejected;
            case L::MetadataMismatch:
                return F::MetadataMismatch;
            case L::BindingRejected:
                return F::BindingRejected;
            case L::Exception:
                return F::WorkerException;
            }
            return F::WorkerException;
        }
        F SplitAssemblyFailure(Skeletal::RigV1Status s)
        {
            switch (s)
            {
            case Skeletal::RigV1Status::WrongOwner:
                return F::WrongOwner;
            case Skeletal::RigV1Status::RegistryNotReady:
                return F::RegistryNotReady;
            case Skeletal::RigV1Status::ResourceFailure:
                return F::ResourceLoadFailed;
            case Skeletal::RigV1Status::Exception:
                return F::AssemblyException;
            default:
                return F::InvalidCpu;
            }
        }
        F PublicationFailure(R::SkeletalPublicationStatus s)
        {
            using P = R::SkeletalPublicationStatus;
            switch (s)
            {
            case P::Success:
                return F::None;
            case P::WrongOwnerThread:
                return F::WrongOwner;
            case P::RegistryNotReady:
                return F::RegistryNotReady;
            case P::SessionChanged:
                return F::PublicationSessionChanged;
            case P::InvalidCandidate:
                return F::PublicationInvalidCandidate;
            case P::KeyCollision:
                return F::PublicationKeyCollision;
            case P::IdCollision:
                return F::PublicationIdCollision;
            case P::InvalidCachedAsset:
                return F::PublicationInvalidCache;
            case P::BudgetExceeded:
                return F::PublicationBudget;
            case P::InjectedFailure:
                return F::PublicationInjectedFailure;
            case P::Exception:
                return F::PublicationException;
            default:
                return F::PublicationInvalidRequest;
            }
        }
    } // namespace
    struct SkeletalAssetRuntime::State
    {
        static_assert(std::is_nothrow_move_assignable_v<Callback>);
        static_assert(std::is_nothrow_move_assignable_v<C::VariableArray<Callback>>);
        static_assert(
            noexcept(std::declval<C::VariableArray<Callback>&>().swap(std::declval<C::VariableArray<Callback>&>())));
        static_assert(std::is_nothrow_move_assignable_v<R::CookedSkeletalCpuAsset>);
        enum class Outcome
        {
            Preparing,
            Accepted,
            Rejected
        };
        struct Request
        {
            uint64_t Id = 0, Generation = 0;
            R::CookedSkeletalLoadPlan Plan;
            R::RigSplitRequestIdentity SplitIdentity;
            C::TSharedPtr<const R::RigSplitPublicationReceipt> SplitReceipt;
            C::TSharedPtr<RigSplitAssetDiagnostics> MutableSplitDiagnostics;
            R::SkeletalPublicationLimits PublicationLimits;
            bool bSplit = false;
            C::String Path, Key;
            Identity KeyIdentity;
            R::CookedSkeletalCpuAsset Cpu;
            R::SkeletalAssetLoadReport LoadReport;
            SkeletalAssetCompletion Completion;
            C::VariableArray<Callback> PendingCallbacks, BatchCallbacks;
            Thread::TaskPtr Task;
            Thread::Atomic<bool> Canceled{false};
            Outcome Admission = Outcome::Preparing;
            bool Terminal = false, ReadyLinked = false, InBatch = false, Finalized = false, CancelIssued = false;
            Request* OwnedPrev = nullptr;
            Request* OwnedNext = nullptr;
            Request* ReadyPrev = nullptr;
            Request* ReadyNext = nullptr;
            Request* BatchNext = nullptr;
        };
        using RequestPtr = C::TSharedPtr<Request>;
        mutable Thread::Mutex Mutex;
        Thread::ConditionVariable Condition;
        R::SkeletalAssetCreateContext Context;
        Thread::JobSystem* Jobs = nullptr;
        C::TSharedPtr<const Asset::AssetSystem> Snapshot;
        R::SkeletalCacheDomain Domain;
        SkeletalRuntimeLimits Limits;
        R::SkeletalPublicationLimits PublicationLimits;
        uint64_t Generation = 1, NextId = 1;
        bool Bound = false, Closing = false, Flushing = false, Cleaning = false, Drained = false;
        size_t Admissions = 0, Handoffs = 0, Accepted = 0, CancelScopes = 0, Callbacks = 0, ReadyCount = 0;
        uint64_t WorkerCalls = 0, AssemblyCalls = 0, CommitCalls = 0;
        C::UnorderedMap<uint64_t, RequestPtr> ById;
        C::UnorderedMap<Identity, Request*, Identity::Hasher> ByKey;
        Request* OwnedHead = nullptr;
        Request* ReadyHead = nullptr;
        Request* ReadyTail = nullptr;
        D::SkeletalRuntimeHooks Hooks;
        C::TWeakPtr<SkeletalAssetResource> LastPreparedForTest;
        bool Owner() const
        {
            return Context.OwnerThread != Thread::Thread::ThreadId{} &&
                   Context.OwnerThread == Thread::Thread::GetCurrentThreadId();
        }
        void Fault(D::SkeletalRuntimePoint p, uint64_t id) const
        {
            if (Hooks.Fault)
            {
                Hooks.Fault(p, id, Hooks.Context);
            }
        }
        void Observe(D::SkeletalRuntimeObservation p, uint64_t id) const noexcept
        {
            if (Hooks.Observe)
            {
                Hooks.Observe(p, id, Hooks.Context);
            }
        }
        bool Session() const
        {
            R::SkeletalPublicationReport report;
            return R::ValidateSkeletalCacheDomain(Context, Domain, report);
        }
        bool Live(const Request& r) const
        {
            return Bound && !Closing && !r.Canceled.Load() && r.Admission == Outcome::Accepted &&
                   r.Generation == Generation && r.Plan.Assets == Snapshot;
        }
        void LinkReady(Request& r)
        {
            if (r.ReadyLinked || r.InBatch || r.Admission != Outcome::Accepted || !r.Terminal)
            {
                return;
            }
            r.ReadyPrev = ReadyTail;
            r.ReadyNext = nullptr;
            if (ReadyTail)
            {
                ReadyTail->ReadyNext = &r;
            }
            else
            {
                ReadyHead = &r;
            }
            ReadyTail = &r;
            r.ReadyLinked = true;
            ++ReadyCount;
        }
        void UnlinkReady(Request& r)
        {
            if (!r.ReadyLinked)
            {
                return;
            }
            if (r.ReadyPrev)
            {
                r.ReadyPrev->ReadyNext = r.ReadyNext;
            }
            else
            {
                ReadyHead = r.ReadyNext;
            }
            if (r.ReadyNext)
            {
                r.ReadyNext->ReadyPrev = r.ReadyPrev;
            }
            else
            {
                ReadyTail = r.ReadyPrev;
            }
            r.ReadyPrev = r.ReadyNext = nullptr;
            r.ReadyLinked = false;
            --ReadyCount;
        }
        void EraseKey(Request& r)
        {
            const auto i = ByKey.find(r.KeyIdentity);
            if (i != ByKey.end() && i->second == &r)
            {
                ByKey.erase(i);
            }
        }
        void Insert(const RequestPtr& r)
        {
            // 部分map挿入の例外では、local強参照を持ったまま索引だけを戻す。
            ById.emplace(r->Id, r);
            try
            {
                Fault(D::SkeletalRuntimePoint::BetweenIndexes, r->Id);
                ByKey.emplace(r->KeyIdentity, r.get());
            }
            catch (...)
            {
                ById.erase(r->Id);
                throw;
            }
            r->OwnedNext = OwnedHead;
            if (OwnedHead)
            {
                OwnedHead->OwnedPrev = r.get();
            }
            OwnedHead = r.get();
        }
        void Finish(Request& r)
        {
            UnlinkReady(r);
            EraseKey(r);
            if (r.OwnedPrev)
            {
                r.OwnedPrev->OwnedNext = r.OwnedNext;
            }
            else if (OwnedHead == &r)
            {
                OwnedHead = r.OwnedNext;
            }
            if (r.OwnedNext)
            {
                r.OwnedNext->OwnedPrev = r.OwnedPrev;
            }
            r.OwnedPrev = r.OwnedNext = nullptr;
            const auto i = ById.find(r.Id);
            if (i != ById.end() && i->second.get() == &r)
            {
                if (r.Admission == Outcome::Accepted)
                {
                    --Accepted;
                }
                ById.erase(i);
            }
        }
        struct AdmissionGuard
        {
            C::TSharedPtr<State> Value;
            ~AdmissionGuard()
            {
                Thread::ScopedLock lock(Value->Mutex);
                --Value->Admissions;
                Value->Condition.NotifyAll();
            }
        };
        static void Worker(const C::TSharedPtr<State>& state, const C::TWeakPtr<Request>& weak) noexcept
        {
            auto r = weak.lock();
            if (!r)
            {
                return;
            }
            try
            {
                {
                    Thread::ScopedLock lock(state->Mutex);
                    ++state->WorkerCalls;
                }
                if (r->Canceled.Load())
                {
                    return;
                }
                state->Fault(D::SkeletalRuntimePoint::WorkerBeforeLoad, r->Id);
                if (r->bSplit)
                {
                    const bool success =
                        R::LoadRigSplitForPublication(r->SplitIdentity, r->SplitReceipt, *r->MutableSplitDiagnostics);
                    state->Fault(D::SkeletalRuntimePoint::WorkerAfterLoad, r->Id);
                    r->Completion.Failure =
                        success ? F::None : SplitLoadFailure(r->MutableSplitDiagnostics->Load.Status);
                    r->Completion.ResolveStatus = r->MutableSplitDiagnostics->Load.ResolveStatus;
                }
                else
                {
                    const bool success = R::LoadCookedSkeletalForWorker(r->Plan, r->Cpu, r->LoadReport);
                    state->Fault(D::SkeletalRuntimePoint::WorkerAfterLoad, r->Id);
                    r->Completion.Failure = success ? F::None
                                                    : (r->LoadReport.Status == R::SkeletalAssetLoadStatus::Exception
                                                           ? F::WorkerException
                                                           : LoadFailure(r->LoadReport.Status));
                    r->Completion.ResolveStatus = r->LoadReport.ResolveStatus;
                    r->Completion.ParseStatus = r->LoadReport.ParseStatus;
                }
            }
            catch (...)
            {
                r->Cpu = {};
                r->SplitReceipt.reset();
                if (r->MutableSplitDiagnostics)
                {
                    r->MutableSplitDiagnostics->Load.Status = R::RigSplitLoadStatus::Exception;
                }
                r->Completion.Failure = F::WorkerException;
            }
        }
        static void Terminal(const C::TSharedPtr<State>& state, const C::TWeakPtr<Request>& weak) noexcept
        {
            auto r = weak.lock();
            uint64_t id = 0;
            {
                Thread::ScopedLock lock(state->Mutex);
                if (r)
                {
                    id = r->Id;
                    r->Terminal = true;
                    state->LinkReady(*r);
                }
            }
            state->Observe(D::SkeletalRuntimeObservation::BeforeHandoffAck, id);
            r.reset();
            // この最後の状態操作後にrequest/CPU/snapshotへ触らない。
            Thread::ScopedLock lock(state->Mutex);
            assert(state->Handoffs > 0);
            --state->Handoffs;
            state->Condition.NotifyAll();
        }
        SkeletalAdmissionResult Admit(const C::TSharedPtr<State>& self, C::AnsiStringView input, Callback callback,
                                      const Skeletal::RigSplitRequest* splitRequest = nullptr)
        {
            RequestPtr r;
            bool handoff = false, registered = false, indexed = false, submitted = false;
            S exceptionStatus = S::PreparationException;
            try
            {
                Fault(D::SkeletalRuntimePoint::Admission, 0);
                if (!callback.IsBound())
                {
                    return {S::EmptyCallback, 0};
                }
                C::String coreKey, corePath;
                C::AnsiString logicalPath;
                R::RigSplitRequestIdentity splitIdentity;
                auto publicationLimits = PublicationLimits;
                if (splitRequest)
                {
                    const auto result =
                        R::BuildRigSplitRequestIdentity(*splitRequest, Snapshot, Domain.Session, Domain.Ordinal,
                                                        Generation, Limits.MaxSplitKeyBytes, splitIdentity);
                    if (result != R::RigSplitIdentityStatus::Success)
                    {
                        return {result == R::RigSplitIdentityStatus::LimitExceeded ? S::LimitExceeded
                                : result == R::RigSplitIdentityStatus::Exception   ? S::PreparationException
                                                                                   : S::InvalidPath,
                                0};
                    }
                    coreKey = splitIdentity.GetData()->Key;
                    corePath = splitIdentity.GetData()->BundleUri;
                    publicationLimits.MaxKeyBytes = Limits.MaxSplitKeyBytes;
                }
                else
                {
                    if (input.empty() || input.size() > Limits.MaxPathBytes ||
                        !Asset::MeasureSkeletalNameDecoding<char>(
                             2, {reinterpret_cast<const uint8_t*>(input.data()), input.size()})
                             .Succeeded())
                    {
                        return {S::InvalidPath, 0};
                    }
                    const auto path = Asset::AssetPath::Normalize(input);
                    if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
                    {
                        return {S::InvalidPath, 0};
                    }
                    C::AnsiString key = "skeletal_asset:";
                    if (!AppendNumber(key, Domain.Session) || !AppendNumber(key, Domain.Ordinal) ||
                        !AppendNumber(key, Generation))
                    {
                        return {S::InvalidPath, 0};
                    }
                    key += "default:";
                    key += path.GetLogicalPath();
                    logicalPath = path.GetLogicalPath();
                    if (key.size() > Limits.MaxKeyBytes || !CoreText(key, coreKey) ||
                        !CoreText(path.GetLogicalPath(), corePath))
                    {
                        return {S::InvalidPath, 0};
                    }
                }
                const Identity keyIdentity(coreKey);
                if (!keyIdentity.IsValid() || keyIdentity.GetView() != C::StringView(coreKey))
                {
                    return {S::CacheRejected, 0};
                }
                R::SkeletalPublishedAsset cached;
                bool hit = false;
                {
                    Thread::ScopedLock lock(Mutex);
                    if (Closing)
                    {
                        return {S::Closed, 0};
                    }
                    if (!Session())
                    {
                        return {S::RegistrySessionChanged, 0};
                    }
                    const auto existing = ByKey.find(keyIdentity);
                    if (existing != ByKey.end())
                    {
                        auto& target = *existing->second;
                        if (target.Key != coreKey || target.bSplit != bool(splitRequest) ||
                            (splitRequest && !R::SameRigSplitRequestIdentity(target.SplitIdentity, splitIdentity)))
                        {
                            return {S::CacheRejected, 0};
                        }
                        if (target.PendingCallbacks.size() + target.BatchCallbacks.size() >=
                            Limits.MaxSubscribersPerGroup)
                        {
                            return {S::LimitExceeded, 0};
                        }
                        // 成功通知中の再購読でも、直前のcallbackによる内容変更をcache検査から逃がさない。
                        // 受理済みsubscriberは保持し、新しい要求だけを公開済み状態と再照合する。
                        if (splitRequest && target.Finalized && target.Completion.Failure == F::None)
                        {
                            R::SkeletalPublishedAsset validated;
                            R::SkeletalPublicationReport validation;
                            if (!R::FindPublishedRigSplitAsset(Context, splitIdentity, publicationLimits, validated,
                                                               validation) ||
                                validated.Asset != target.Completion.Asset)
                            {
                                return {S::CacheRejected, 0};
                            }
                        }
                        target.PendingCallbacks.push_back(std::move(callback));
                        LinkReady(target);
                        return {S::Accepted, target.Id};
                    }
                    if (ById.size() >= Limits.MaxPendingGroups)
                    {
                        return {S::LimitExceeded, 0};
                    }
                    if (NextId == UINT64_MAX)
                    {
                        return {S::IdExhausted, 0};
                    }
                    R::SkeletalPublicationReport report;
                    if (splitRequest)
                    {
                        if (splitIdentity.GetData()->bCanCache)
                        {
                            hit = R::FindPublishedRigSplitAsset(Context, splitIdentity, publicationLimits, cached,
                                                                report);
                        }
                        else
                        {
                            report.Status = R::SkeletalPublicationStatus::CacheMiss;
                        }
                    }
                    else
                    {
                        hit = R::FindPublishedSkeletalAsset(Context, coreKey, corePath, publicationLimits, cached,
                                                            report);
                    }
                    if (!hit && report.Status != R::SkeletalPublicationStatus::CacheMiss)
                    {
                        return {S::CacheRejected, 0};
                    }
                    r = C::MakeShared<Request>();
                    r->Id = NextId++;
                    r->Generation = Generation;
                    r->Plan = {Snapshot, logicalPath};
                    r->bSplit = bool(splitRequest);
                    r->SplitIdentity = std::move(splitIdentity);
                    r->PublicationLimits = publicationLimits;
                    if (r->bSplit)
                    {
                        r->MutableSplitDiagnostics = C::MakeShared<RigSplitAssetDiagnostics>();
                        r->Completion.SplitDiagnostics = r->MutableSplitDiagnostics;
                    }
                    r->Key = std::move(coreKey);
                    r->KeyIdentity = keyIdentity;
                    r->Path = std::move(corePath);
                    r->Completion.RequestId = r->Id;
                }
                r->PendingCallbacks.push_back(std::move(callback));
                if (hit)
                {
                    if (r->bSplit)
                    {
                        r->Completion.SplitDiagnostics = R::RigSplitAssetAccess::Get(*cached.Asset)->GetDiagnostics();
                    }
                    r->Completion.Asset = std::move(cached.Asset);
                    r->Completion.Failure = F::None;
                    r->Completion.bCacheHit = true;
                    r->Terminal = true;
                    r->Finalized = true;
                    Thread::ScopedLock lock(Mutex);
                    if (Closing)
                    {
                        return {S::Closed, 0};
                    }
                    Insert(r);
                    indexed = true;
                    r->Admission = Outcome::Accepted;
                    ++Accepted;
                    LinkReady(*r);
                    return {S::Accepted, r->Id};
                }
                const C::TWeakPtr<Request> weak = r;
                r->Task = Thread::Task::Create([self, weak]() noexcept { Worker(self, weak); });
                {
                    Thread::ScopedLock lock(Mutex);
                    ++Handoffs;
                    handoff = true;
                }
                Fault(D::SkeletalRuntimePoint::BeforeHandler, r->Id);
                r->Task->OnComplete([self, weak](const Thread::TaskPtr&) noexcept { Terminal(self, weak); });
                registered = true;
                Fault(D::SkeletalRuntimePoint::AfterHandler, r->Id);
                {
                    Thread::ScopedLock lock(Mutex);
                    if (!Closing)
                    {
                        Insert(r);
                        indexed = true;
                    }
                }
                if (!indexed)
                {
                    r->Task->Cancel();
                    return {S::Closed, 0};
                }
                exceptionStatus = S::SubmitException;
                Fault(D::SkeletalRuntimePoint::BeforeSubmit, r->Id);
                submitted = Jobs->SubmitTask(r->Task);
                Observe(D::SkeletalRuntimeObservation::AfterSubmit, r->Id);
                {
                    Thread::ScopedLock lock(Mutex);
                    if (submitted)
                    {
                        r->Admission = Outcome::Accepted;
                        ++Accepted;
                        if (Closing)
                        {
                            r->Canceled.Store(true);
                        }
                        LinkReady(*r);
                    }
                    else
                    {
                        r->Admission = Outcome::Rejected;
                        Finish(*r);
                        indexed = false;
                    }
                }
                if (!submitted)
                {
                    r->Task->Cancel();
                    return {S::SubmitRejected, 0};
                }
                return {S::Accepted, r->Id};
            }
            catch (...)
            {
                // Submit成功後にthrowing操作を置かない。未受理の自分のTaskだけを取消す。
                assert(!submitted);
                {
                    Thread::ScopedLock lock(Mutex);
                    if (r)
                    {
                        r->Admission = Outcome::Rejected;
                        if (indexed)
                        {
                            Finish(*r);
                        }
                    }
                    if (handoff && !registered)
                    {
                        --Handoffs;
                        Condition.NotifyAll();
                    }
                }
                if (r && registered)
                {
                    r->Task->Cancel();
                }
                return {exceptionStatus, 0};
            }
        }
        void Finalize(const RequestPtr& r)
        {
            if (r->Finalized)
            {
                return;
            }
            r->Finalized = true;
            if (r->Completion.Failure != F::None)
            {
                return;
            }
            try
            {
                {
                    Thread::ScopedLock lock(Mutex);
                    if (!Live(*r))
                    {
                        return;
                    }
                    if (!Session())
                    {
                        r->Completion.Failure = F::PublicationSessionChanged;
                        return;
                    }
                    ++AssemblyCalls;
                }
                Fault(D::SkeletalRuntimePoint::BeforeAssembly, r->Id);
                R::SkeletalPreparedPublication prepared;
                R::SkeletalPublicationReport report;
                const bool assembled = r->bSplit
                                           ? R::PrepareRigSplitPublication(r->SplitReceipt, Context, prepared, report)
                                           : R::PrepareSkeletalPublication(r->Cpu, Context, prepared, report);
                if (r->bSplit)
                {
                    r->MutableSplitDiagnostics->Assembly = report.SplitAssembly;
                }
                if (!assembled)
                {
                    r->Completion.Failure = report.Status == R::SkeletalPublicationStatus::AssemblyFailed
                                                ? (r->bSplit ? SplitAssemblyFailure(report.SplitAssembly.Status)
                                                             : LoadFailure(report.Assembly.Status))
                                                : PublicationFailure(report.Status);
                    return;
                }
                if (Hooks.Fault || Hooks.Observe)
                {
                    Thread::ScopedLock lock(Mutex);
                    LastPreparedForTest = D::GetPreparedSkeletalAsset(prepared);
                }
                Fault(D::SkeletalRuntimePoint::AfterAssembly, r->Id);
                Fault(D::SkeletalRuntimePoint::BeforeCommit, r->Id);
                R::SkeletalPublishedAsset published;
                {
                    Thread::ScopedLock lock(Mutex);
                    if (!Live(*r))
                    {
                        return;
                    }
                    if (report.Session != Domain.Session || !Session())
                    {
                        r->Completion.Failure = F::PublicationSessionChanged;
                        return;
                    }
                    ++CommitCalls;
                    struct CommitObservation
                    {
                        State* Owner;
                        uint64_t Id;
                    } observation{this, r->Id};
                    const auto probe = +[](D::SkeletalPublicationPoint point, uint32_t,
                                           const C::TSharedPtr<SkeletalAssetResource>&, void* context) -> bool
                    {
                        auto& value = *static_cast<CommitObservation*>(context);
                        if (point == D::SkeletalPublicationPoint::BeforeCommit)
                        {
                            value.Owner->Observe(D::SkeletalRuntimeObservation::InsidePublicationCommit, value.Id);
                        }
                        return true;
                    };
                    // 非throwing観測だけを許す。ここではStateとRegistryを保持し、再入禁止。
                    const bool committed =
                        Hooks.Observe
                            ? D::CommitSkeletalPublicationWithProbe(prepared, r->Key, r->PublicationLimits, published,
                                                                    report, probe, &observation)
                            : R::CommitSkeletalPublication(prepared, r->Key, r->PublicationLimits, published, report);
                    if (!committed)
                    {
                        r->Completion.Failure = PublicationFailure(report.Status);
                        return;
                    }
                    if (r->bSplit)
                    {
                        r->Completion.SplitDiagnostics =
                            R::RigSplitAssetAccess::Get(*published.Asset)->GetDiagnostics();
                    }
                    r->Completion.Asset = std::move(published.Asset);
                }
                Observe(D::SkeletalRuntimeObservation::AfterCommit, r->Id);
            }
            catch (...)
            {
                if (r->MutableSplitDiagnostics)
                {
                    r->MutableSplitDiagnostics->Assembly.Status = Skeletal::RigV1Status::Exception;
                }
                r->Completion.Failure = F::OwnerException;
                r->Completion.Asset.reset();
            }
        }
    };
    SkeletalAssetRuntime::SkeletalAssetRuntime() : m_State(C::MakeShared<State>())
    {
    }
    SkeletalAssetRuntime::~SkeletalAssetRuntime()
    {
        Close();
        const auto result = Drain();
        assert(result == S::Drained); // 自身のcallbackから同期破棄する運用は非対応。
        (void)result;
    }
    S SkeletalAssetRuntime::Bind(ResourceRegistry& registry, Thread::JobSystem& jobs,
                                 C::TSharedPtr<const Asset::AssetSystem> snapshot, Thread::Thread::ThreadId owner,
                                 const SkeletalRuntimeLimits& limits)
    {
        auto state = m_State;
        Thread::ScopedLock lock(state->Mutex);
        if (state->Bound)
        {
            return S::AlreadyBound;
        }
        if (state->Closing)
        {
            return S::Closed;
        }
        if (owner == Thread::Thread::ThreadId{} || owner != Thread::Thread::GetCurrentThreadId())
        {
            return S::WrongOwner;
        }
        if (!snapshot || !limits.MaxPendingGroups || limits.MaxPendingGroups > UINT32_MAX ||
            !limits.MaxSubscribersPerGroup || limits.MaxSubscribersPerGroup > UINT32_MAX || !limits.MaxPathBytes ||
            !limits.MaxKeyBytes || !limits.MaxSplitKeyBytes || limits.MaxSplitKeyBytes > R::RigSplitMaximumKeyBytes ||
            !limits.MaxBundleClips || limits.MaxBundleClips > UINT32_MAX - 3 || !limits.MaxRegistrySlots ||
            !limits.MaxRegistryMapEntries || !limits.MaxRegistryBuckets)
        {
            return S::LimitExceeded;
        }
        R::SkeletalCacheDomain domain;
        R::SkeletalPublicationReport report;
        if (!R::AllocateSkeletalCacheDomain({&registry, owner}, domain, report))
        {
            return report.Status == R::SkeletalPublicationStatus::BudgetExceeded ? S::LimitExceeded
                                                                                 : S::RegistrySessionChanged;
        }
        state->Context = {&registry, owner};
        state->Jobs = &jobs;
        state->Snapshot = std::move(snapshot);
        state->Domain = domain;
        state->Limits = limits;
        state->PublicationLimits = {limits.MaxBundleClips, limits.MaxKeyBytes, limits.MaxRegistrySlots,
                                    limits.MaxRegistryMapEntries, limits.MaxRegistryBuckets};
        state->Bound = true;
        return S::Success;
    }
    SkeletalAdmissionResult SkeletalAssetRuntime::LoadAsync(C::AnsiStringView path, Callback callback)
    {
        auto state = m_State;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (!state->Bound)
            {
                return {S::NotBound, 0};
            }
            if (!state->Owner())
            {
                return {S::WrongOwner, 0};
            }
            if (state->Closing)
            {
                return {S::Closed, 0};
            }
            ++state->Admissions;
        }
        RuntimeScope scope(state.get());
        State::AdmissionGuard guard{state};
        return state->Admit(state, path, std::move(callback));
    }
    SkeletalAdmissionResult SkeletalAssetRuntime::LoadRigSplitAsync(const Skeletal::RigSplitRequest& request,
                                                                    Callback callback)
    {
        auto state = m_State;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (!state->Bound)
            {
                return {S::NotBound, 0};
            }
            if (!state->Owner())
            {
                return {S::WrongOwner, 0};
            }
            if (state->Closing)
            {
                return {S::Closed, 0};
            }
            ++state->Admissions;
        }
        RuntimeScope scope(state.get());
        State::AdmissionGuard guard{state};
        return state->Admit(state, {}, std::move(callback), &request);
    }
    S SkeletalAssetRuntime::SetSnapshot(C::TSharedPtr<const Asset::AssetSystem> snapshot)
    {
        auto state = m_State;
        Thread::ScopedLock lock(state->Mutex);
        if (!state->Bound)
        {
            return S::NotBound;
        }
        if (!state->Owner())
        {
            return S::WrongOwner;
        }
        if (state->Closing)
        {
            return S::Closed;
        }
        if (!snapshot)
        {
            return S::LimitExceeded;
        }
        if (state->Admissions || state->Handoffs || !state->ById.empty() || state->Flushing || state->CancelScopes)
        {
            return S::Busy;
        }
        if (!state->Session())
        {
            return S::RegistrySessionChanged;
        }
        if (snapshot == state->Snapshot)
        {
            return S::Unchanged;
        }
        if (state->Generation == UINT64_MAX)
        {
            return S::GenerationExhausted;
        }
        ++state->Generation;
        state->Snapshot = std::move(snapshot);
        return S::Success;
    }
    bool SkeletalAssetRuntime::Cancel(uint64_t id)
    {
        auto state = m_State;
        RuntimeScope scope(state.get());
        State::RequestPtr r;
        Thread::TaskPtr task;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (state->Cleaning || state->Drained)
            {
                return false;
            }
            const auto i = state->ById.find(id);
            if (i == state->ById.end())
            {
                return false;
            }
            r = i->second;
            r->Canceled.Store(true);
            state->EraseKey(*r);
            ++state->CancelScopes;
            if (!r->CancelIssued)
            {
                r->CancelIssued = true;
                task = r->Task;
            }
        }
        if (task)
        {
            task->Cancel();
        }
        task.reset();
        r.reset();
        Thread::ScopedLock lock(state->Mutex);
        --state->CancelScopes;
        state->Condition.NotifyAll();
        return true;
    }
    void SkeletalAssetRuntime::Close()
    {
        auto state = m_State;
        RuntimeScope scope(state.get());
        {
            Thread::ScopedLock lock(state->Mutex);
            if (state->Cleaning || state->Drained)
            {
                return;
            }
            state->Closing = true;
            ++state->CancelScopes;
        }
        for (;;)
        {
            State::RequestPtr r;
            Thread::TaskPtr task;
            {
                Thread::ScopedLock lock(state->Mutex);
                auto* p = state->OwnedHead;
                while (p && p->CancelIssued)
                {
                    p = p->OwnedNext;
                }
                if (!p)
                {
                    break;
                }
                r = state->ById.find(p->Id)->second;
                r->CancelIssued = true;
                r->Canceled.Store(true);
                state->EraseKey(*r);
                task = r->Task;
            }
            if (task)
            {
                task->Cancel();
            }
        }
        Thread::ScopedLock lock(state->Mutex);
        --state->CancelScopes;
        state->Condition.NotifyAll();
    }
    S SkeletalAssetRuntime::Drain()
    {
        auto state = m_State;
        if (RuntimeScope::Contains(state.get()))
        {
            return S::Deferred;
        }
        RuntimeScope scope(state.get());
        {
            Thread::ScopedLock lock(state->Mutex);
            if (!state->Closing)
            {
                return S::NotClosed;
            }
            state->Condition.Wait(state->Mutex,
                                  [&]()
                                  {
                                      const bool ready = state->Drained || (!state->Admissions && !state->Handoffs &&
                                                                            !state->Flushing && !state->Callbacks &&
                                                                            !state->CancelScopes && !state->Cleaning);
                                      if (!ready)
                                      {
                                          state->Observe(D::SkeletalRuntimeObservation::DrainWaiting, 0);
                                      }
                                      return ready;
                                  });
            if (state->Drained)
            {
                return S::Drained;
            }
            state->Cleaning = true;
        }
        for (;;)
        {
            State::RequestPtr r;
            {
                Thread::ScopedLock lock(state->Mutex);
                if (!state->OwnedHead)
                {
                    break;
                }
                r = state->ById.find(state->OwnedHead->Id)->second;
                state->Finish(*r);
            }
        }
        C::TSharedPtr<const Asset::AssetSystem> snapshot;
        {
            Thread::ScopedLock lock(state->Mutex);
            snapshot = std::move(state->Snapshot);
        }
        snapshot.reset();
        {
            Thread::ScopedLock lock(state->Mutex);
            state->Drained = true;
            state->Cleaning = false;
            state->Condition.NotifyAll();
        }
        return S::Drained;
    }
    size_t SkeletalAssetRuntime::GetPendingCount() const
    {
        Thread::ScopedLock lock(m_State->Mutex);
        return m_State->Accepted;
    }
    SkeletalFlushResult SkeletalAssetRuntime::FlushCompleted(uint32_t maxLoads)
    {
        auto state = m_State;
        SkeletalFlushResult result;
        if (RuntimeScope::Contains(state.get()))
        {
            result.Status = S::Reentrant;
            return result;
        }
        State::Request* batch = nullptr;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (!state->Bound)
            {
                return result;
            }
            if (!state->Owner())
            {
                result.Status = S::WrongOwner;
                return result;
            }
            if (state->Closing)
            {
                result.Status = S::Closed;
                return result;
            }
            if (state->Flushing)
            {
                result.Status = S::Reentrant;
                return result;
            }
            state->Flushing = true;
            auto** next = &batch;
            for (uint32_t n = 0; state->ReadyHead && (!maxLoads || n < maxLoads); ++n)
            {
                auto* r = state->ReadyHead;
                state->UnlinkReady(*r);
                r->InBatch = true;
                r->BatchNext = nullptr;
                r->BatchCallbacks.swap(r->PendingCallbacks);
                *next = r;
                next = &r->BatchNext;
            }
        }
        RuntimeScope scope(state.get());
        struct FlushGuard
        {
            C::TSharedPtr<State> Value;
            ~FlushGuard()
            {
                Thread::ScopedLock lock(Value->Mutex);
                Value->Flushing = false;
                Value->Condition.NotifyAll();
            }
        } guard{state};
        result.Status = S::Success;
        state->Observe(D::SkeletalRuntimeObservation::AfterDetach, 0);
        while (batch)
        {
            State::RequestPtr r;
            {
                Thread::ScopedLock lock(state->Mutex);
                r = state->ById.find(batch->Id)->second;
                batch = batch->BatchNext;
            }
            ++result.Groups;
            state->Finalize(r);
            r->Cpu = {};
            for (size_t i = 0; i < r->BatchCallbacks.size(); ++i)
            {
                Callback callback;
                try
                {
                    state->Fault(D::SkeletalRuntimePoint::BeforeCallback, r->Id);
                }
                catch (...)
                {
                    ++result.CallbackExceptions;
                }
                {
                    Thread::ScopedLock lock(state->Mutex);
                    if (!state->Live(*r))
                    {
                        break;
                    }
                    callback = std::move(r->BatchCallbacks[i]);
                    ++state->Callbacks;
                }
                state->Observe(D::SkeletalRuntimeObservation::CallbackReserved, r->Id);
                ++result.Callbacks;
                try
                {
                    callback.Invoke(r->Completion);
                }
                catch (...)
                {
                    ++result.CallbackExceptions;
                }
                callback.Clear();
                {
                    Thread::ScopedLock lock(state->Mutex);
                    --state->Callbacks;
                    state->Condition.NotifyAll();
                }
            }
            C::VariableArray<Callback> discardedBatch, discardedPending;
            {
                Thread::ScopedLock lock(state->Mutex);
                discardedBatch = std::move(r->BatchCallbacks);
                r->InBatch = false;
                if (state->Live(*r) && !r->PendingCallbacks.empty())
                {
                    state->LinkReady(*r);
                }
                else
                {
                    discardedPending = std::move(r->PendingCallbacks);
                    state->Finish(*r);
                }
            }
        }
        return result;
    }
    void D::SkeletalRuntimeTestAccess::SetHooks(SkeletalAssetRuntime& runtime, const SkeletalRuntimeHooks& hooks)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        runtime.m_State->Hooks = hooks;
    }
    D::SkeletalRuntimeCounts D::SkeletalRuntimeTestAccess::Counts(const SkeletalAssetRuntime& runtime)
    {
        auto& s = *runtime.m_State;
        Thread::ScopedLock lock(s.Mutex);
        return {s.WorkerCalls, s.AssemblyCalls, s.CommitCalls,    s.ReadyCount,     s.Handoffs,
                s.Admissions,  s.Accepted,      s.Domain.Session, s.Domain.Ordinal, s.Generation};
    }
    C::TWeakPtr<void> D::SkeletalRuntimeTestAccess::WeakState(const SkeletalAssetRuntime& runtime)
    {
        return runtime.m_State;
    }
    C::TWeakPtr<void> D::SkeletalRuntimeTestAccess::WeakRequest(const SkeletalAssetRuntime& runtime, uint64_t id)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        const auto i = runtime.m_State->ById.find(id);
        return i == runtime.m_State->ById.end() ? C::TWeakPtr<void>{} : i->second;
    }
    C::TWeakPtr<SkeletalAssetResource> D::SkeletalRuntimeTestAccess::WeakPrepared(const SkeletalAssetRuntime& runtime)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        return runtime.m_State->LastPreparedForTest;
    }
    Thread::TaskPtr D::SkeletalRuntimeTestAccess::PeekTask(const SkeletalAssetRuntime& runtime, uint64_t id)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        const auto i = runtime.m_State->ById.find(id);
        return i == runtime.m_State->ById.end() ? Thread::TaskPtr{} : i->second->Task;
    }
    bool D::SkeletalRuntimeTestAccess::TryStateLock(SkeletalAssetRuntime& runtime)
    {
        const bool acquired = runtime.m_State->Mutex.TryLock();
        if (acquired)
        {
            runtime.m_State->Mutex.Unlock();
        }
        return acquired;
    }
    void D::SkeletalRuntimeTestAccess::AlterStoredKey(SkeletalAssetRuntime& runtime, uint64_t id, const C::String& key)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        runtime.m_State->ById.at(id)->Key = key;
    }
    bool D::SkeletalRuntimeTestAccess::WaitReady(SkeletalAssetRuntime& runtime, size_t count)
    {
        auto state = runtime.m_State;
        Thread::ScopedLock lock(state->Mutex);
        return state->Condition.WaitFor(state->Mutex, std::chrono::seconds(10),
                                        [&]() { return state->ReadyCount >= count && state->Handoffs == 0; });
    }
    void D::SkeletalRuntimeTestAccess::SetNextId(SkeletalAssetRuntime& runtime, uint64_t id)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        runtime.m_State->NextId = id;
    }
    void D::SkeletalRuntimeTestAccess::SetGeneration(SkeletalAssetRuntime& runtime, uint64_t generation)
    {
        Thread::ScopedLock lock(runtime.m_State->Mutex);
        runtime.m_State->Generation = generation;
    }
} // namespace NorvesLib::Core
