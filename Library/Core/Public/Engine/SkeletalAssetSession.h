#pragma once
// 実application ownerの骨格runtime寿命。別rendererのlifecycleは駆動しない。
#include "Animation/SkeletalAssetRuntime.h"
namespace NorvesLib::Core::Engine::Detail
{
    struct SkeletalSessionTestAccess;
}
namespace NorvesLib::Core::Application
{
    class IApplicationHandler;
}
namespace NorvesLib::Core
{
    class SkeletalAssetSession final
    {
      public:
        SkeletalAssetSession() = default;
        ~SkeletalAssetSession();
        SkeletalAssetSession(const SkeletalAssetSession&) = delete;
        SkeletalAssetSession& operator=(const SkeletalAssetSession&) = delete;
        // lifecycle操作は明示ownerのみ。Registry/JobsはEndまで借用する。
        [[nodiscard]] SkeletalRuntimeStatus Begin(ResourceRegistry&, Thread::JobSystem&, Thread::Thread::ThreadId);
        [[nodiscard]] SkeletalRuntimeStatus BindSnapshot(Container::TSharedPtr<const Asset::AssetSystem>);
        [[nodiscard]] SkeletalFlushResult Flush(uint32_t maxGroups = 4);
        [[nodiscard]] SkeletalRuntimeStatus Close();
        [[nodiscard]] SkeletalRuntimeStatus Drain();
        [[nodiscard]] bool End();
        [[nodiscard]] bool IsActive() const noexcept
        {
            return m_bActive;
        }
        [[nodiscard]] bool IsClosed() const noexcept
        {
            return m_bClosed;
        }
        [[nodiscard]] bool OwnsRegistry() const noexcept
        {
            return m_bOwnsRegistry;
        }
        [[nodiscard]] bool HasPinnedSnapshot() const noexcept
        {
            return bool(m_Snapshot);
        }
        [[nodiscard]] size_t GetPendingCount() const;
        [[nodiscard]] SkeletalAssetRuntime* GetRuntime() const noexcept
        {
            return m_Snapshot ? m_Runtime.get() : nullptr;
        }

      private:
        friend struct Engine::Detail::SkeletalSessionTestAccess;
        void (*m_PrepareHook)(void*) = nullptr;
        void* m_pPrepareHookContext = nullptr;
        bool IsOwner() const;
        void ResetReceipt();
        ResourceRegistry* m_pRegistry = nullptr;
        Thread::JobSystem* m_pJobs = nullptr;
        Thread::Thread::ThreadId m_Owner;
        Container::TUniquePtr<SkeletalAssetRuntime> m_Runtime;
        Container::TSharedPtr<const Asset::AssetSystem> m_Snapshot;
        bool m_bActive = false, m_bOwnsRegistry = false, m_bClosed = false, m_bDrained = false;
    };
    // production TickとCPU試験で同じ配送順を使う。simulation pauseの外側で呼ぶ。
    [[nodiscard]] SkeletalFlushResult TickSkeletalOwnerAssetsAndHandler(SkeletalAssetSession& session,
                                                                        Application::IApplicationHandler* handler,
                                                                        float deltaTime);
    [[nodiscard]] bool HasPendingSkeletalConsumers(const SkeletalAssetSession& session,
                                                   const Application::IApplicationHandler* handler);
} // namespace NorvesLib::Core
