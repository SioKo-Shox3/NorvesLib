#include "Engine/SkeletalAssetSession.h"
#include "Object/ResourceRegistry.h"
#include "Application/IApplicationHandler.h"
#include <cassert>
namespace NorvesLib::Core
{
    using Status = SkeletalRuntimeStatus;
    SkeletalAssetSession::~SkeletalAssetSession()
    {
        if (m_bActive)
        {
            const auto close = Close();
            const auto drain = Drain();
            assert(close == Status::Success && drain == Status::Drained);
            const bool ended = End();
            assert(ended);
            (void)close;
            (void)drain;
            (void)ended;
        }
    }
    bool SkeletalAssetSession::IsOwner() const
    {
        return m_Owner != Thread::Thread::ThreadId{} && m_Owner == Thread::Thread::GetCurrentThreadId();
    }
    void SkeletalAssetSession::ResetReceipt()
    {
        m_pRegistry = nullptr;
        m_pJobs = nullptr;
        m_Owner = {};
        m_bActive = false;
        m_bOwnsRegistry = false;
        m_bClosed = false;
        m_bDrained = false;
    }
    Status SkeletalAssetSession::Begin(ResourceRegistry& registry, Thread::JobSystem& jobs,
                                       Thread::Thread::ThreadId owner)
    {
        if (m_bActive)
        {
            return Status::AlreadyBound;
        }
        if (owner == Thread::Thread::ThreadId{} || owner != Thread::Thread::GetCurrentThreadId())
        {
            return Status::WrongOwner;
        }
        // 未初期化の既存登録をID resetで壊さない。未登録候補もcallerがsession外で保持しない。
        if (!registry.IsInitialized() && (registry.GetResourceCount() || registry.GetCachedPathCount()))
        {
            return Status::Busy;
        }
        m_pRegistry = &registry;
        m_pJobs = &jobs;
        m_Owner = owner;
        m_bActive = true;
        try
        {
            if (!registry.IsInitialized())
            {
                // Initialize後のログ例外でも取得責任を失わないよう先にreceiptへ記録する。
                m_bOwnsRegistry = true;
                if (!registry.Initialize())
                {
                    ResetReceipt();
                    return Status::RegistrySessionChanged;
                }
            }
            if (m_PrepareHook)
            {
                m_PrepareHook(m_pPrepareHookContext);
            }
            m_Runtime = Container::MakeUnique<SkeletalAssetRuntime>();
            return Status::Success;
        }
        catch (...)
        {
            m_Runtime.reset();
            if (m_bOwnsRegistry && registry.IsInitialized())
            {
                registry.Shutdown();
            }
            ResetReceipt();
            return Status::PreparationException;
        }
    }
    Status SkeletalAssetSession::BindSnapshot(Container::TSharedPtr<const Asset::AssetSystem> snapshot)
    {
        if (!m_bActive)
        {
            return Status::NotBound;
        }
        if (!IsOwner())
        {
            return Status::WrongOwner;
        }
        if (m_bClosed)
        {
            return Status::Closed;
        }
        if (!snapshot)
        {
            return Status::LimitExceeded;
        }
        if (m_Snapshot == snapshot)
        {
            return Status::Unchanged;
        }
        if (m_Snapshot)
        {
            return Status::Busy;
        }
        const auto result = m_Runtime->Bind(*m_pRegistry, *m_pJobs, snapshot, m_Owner);
        if (result == Status::Success)
        {
            m_Snapshot = std::move(snapshot);
        }
        return result;
    }
    SkeletalFlushResult SkeletalAssetSession::Flush(uint32_t maxGroups)
    {
        if (!m_bActive)
        {
            return {Status::Success};
        }
        if (!IsOwner())
        {
            return {Status::WrongOwner};
        }
        if (m_bClosed)
        {
            return {Status::Closed};
        }
        SkeletalFlushResult result{Status::Success};
        if (m_Snapshot)
        {
            result = m_Runtime->FlushCompleted(maxGroups);
        }
        if (m_bClosed)
        {
            result.Status = Status::Closed;
            return result;
        }
        if (result.Status != Status::Success)
        {
            return result;
        }
        if (m_bOwnsRegistry)
        {
            m_pRegistry->CollectGarbage();
        }
        return result;
    }
    Status SkeletalAssetSession::Close()
    {
        if (!m_bActive)
        {
            return Status::Success;
        }
        if (!IsOwner())
        {
            return Status::WrongOwner;
        }
        m_bClosed = true;
        if (m_Runtime)
        {
            m_Runtime->Close();
        }
        return Status::Success;
    }
    Status SkeletalAssetSession::Drain()
    {
        if (!m_bActive)
        {
            return Status::Drained;
        }
        if (!IsOwner())
        {
            return Status::WrongOwner;
        }
        if (!m_bClosed)
        {
            return Status::NotClosed;
        }
        const auto result = m_Runtime ? m_Runtime->Drain() : Status::Drained;
        m_bDrained = result == Status::Drained;
        return result;
    }
    bool SkeletalAssetSession::End()
    {
        if (!m_bActive)
        {
            return true;
        }
        if (!IsOwner() || !m_bClosed || !m_bDrained)
        {
            return false;
        }
        m_Runtime.reset();
        m_Snapshot.reset();
        if (m_bOwnsRegistry)
        {
            m_pRegistry->Shutdown();
        }
        ResetReceipt();
        return true;
    }
    size_t SkeletalAssetSession::GetPendingCount() const
    {
        return m_Runtime ? m_Runtime->GetPendingCount() : 0;
    }
    SkeletalFlushResult TickSkeletalOwnerAssetsAndHandler(SkeletalAssetSession& session,
                                                          Application::IApplicationHandler* handler, float deltaTime)
    {
        const auto result = session.Flush();
        if (result.Status != Status::Success)
        {
            return result;
        }
        if (handler)
        {
            handler->OnUpdate(deltaTime);
        }
        return result;
    }
    bool HasPendingSkeletalConsumers(const SkeletalAssetSession& session,
                                     const Application::IApplicationHandler* handler)
    {
        return session.GetPendingCount() != 0 || (handler && handler->HasPendingAssetConsumers());
    }
} // namespace NorvesLib::Core
