#include "Save/SaveService.h"

#include "Logging/LogMacros.h"
#include "Thread/Atomic.h"
#include "Thread/JobSystem.h"
#include "Thread/Task.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace NorvesLib::Core::Save
{
    struct SaveService::WriteJob
    {
        // 保存先はサービスが所有し、完了印を確認するまで差し替えない。
        SaveSlotStore* Store = nullptr;
        Container::VariableArray<uint8_t> PayloadUtf8;
        Container::VariableArray<SaveWriteReport> Reports;
        Thread::Atomic<bool> bCompleted{false};
    };

    SaveService::~SaveService()
    {
        WaitForWrite();
    }

    void SaveService::Configure(const Container::String& directory)
    {
        WaitForWrite();
        m_Store = Container::MakeUnique<SaveSlotStore>(directory);
    }

    void SaveService::Configure(const Container::String& directory, Platform::IFileOperations& ops)
    {
        WaitForWrite();
        m_Store = Container::MakeUnique<SaveSlotStore>(directory, ops);
    }

    void SaveService::SetSnapshotProvider(Delegate<bool, Container::String&> provider)
    {
        m_SnapshotProvider = std::move(provider);
    }

    void SaveService::SetLoadConsumer(Delegate<bool, Container::Span<const uint8_t>> consumer)
    {
        m_LoadConsumer = std::move(consumer);
    }

    void SaveService::Save(uint32_t slot)
    {
        if (!m_Store)
        {
            NORVES_LOG_WARNING("SaveService", "保存先が未設定のため、保存要求を受け付けません。");
            return;
        }
        for (uint32_t pendingSlot : m_PendingSlots)
        {
            if (pendingSlot == slot)
            {
                return;
            }
        }
        m_PendingSlots.push_back(slot);
    }

    void SaveService::RequestAutosave(const Container::String& reason)
    {
        if (!m_Store)
        {
            NORVES_LOG_WARNING("SaveService", "保存先が未設定のため、自動保存要求を受け付けません。");
            return;
        }
        if (m_bPendingAutosave)
        {
            m_PendingReasons.append(", ");
        }
        m_PendingReasons.append(reason);
        m_bPendingAutosave = true;
    }

    void SaveService::Update(bool bModeTransitionPending)
    {
        CollectCompletedWrite();
        if (!m_bCapturingSnapshot && !m_Write && HasPendingRequests() &&
            !bModeTransitionPending && m_SaveInhibitCount == 0)
        {
            StartPendingWrite();
            CollectCompletedWrite();
        }
    }

    void SaveService::PushSaveInhibit()
    {
        if (m_SaveInhibitCount == (std::numeric_limits<uint32_t>::max)())
        {
            NORVES_LOG_WARNING("SaveService", "保存禁止の参照数が上限に達しました。");
            return;
        }
        ++m_SaveInhibitCount;
    }

    void SaveService::PopSaveInhibit()
    {
        if (m_SaveInhibitCount == 0)
        {
            NORVES_LOG_WARNING("SaveService", "保存禁止の参照数は既に 0 です。");
            return;
        }
        --m_SaveInhibitCount;
    }

    uint32_t SaveService::GetSaveInhibitCount() const
    {
        return m_SaveInhibitCount;
    }

    SaveLoadResult SaveService::Load(uint32_t slot)
    {
        WaitForWrite();
        if (!m_Store)
        {
            NORVES_LOG_WARNING("SaveService", "保存先が未設定のため、読み込めません。");
            SaveLoadResult result;
            result.Status = SaveLoadStatus::IoError;
            result.Message = "保存先が未設定です。";
            return result;
        }
        auto result = m_Store->LoadSlot(slot);
        if (result.Status == SaveLoadStatus::Loaded)
        {
            const auto consumer = m_LoadConsumer;
            if (!consumer.IsBound())
            {
                NORVES_LOG_WARNING("SaveService", "読み込み結果の受け取り先が未設定です。");
            }
            else if (!consumer.Invoke(Container::Span<const uint8_t>(result.PayloadUtf8)))
            {
                NORVES_LOG_WARNING("SaveService", "読み込み結果を適用できませんでした。");
            }
        }
        return result;
    }

    void SaveService::FlushAndWait()
    {
        if (m_bCapturingSnapshot)
        {
            NORVES_LOG_WARNING("SaveService", "スナップショット作成中は保存の完了を待てません。");
            return;
        }
        for (;;)
        {
            WaitForWrite();
            CollectCompletedWrite();
            // 通知先が新たな書き込みを開始した場合も、その完了を待つ。
            if (m_Write)
            {
                continue;
            }
            if (!HasPendingRequests() || !StartPendingWrite())
            {
                return;
            }
        }
    }

    bool SaveService::IsWriteInFlight() const
    {
        return m_Write && !m_Write->bCompleted.Load(std::memory_order_acquire);
    }

    bool SaveService::HasPendingRequests() const
    {
        return !m_PendingSlots.empty() || m_bPendingAutosave;
    }

    uint64_t SaveService::GetCompletedWriteCount() const
    {
        return m_CompletedWriteCount +
            (m_Write && m_Write->bCompleted.Load(std::memory_order_acquire) ? 1 : 0);
    }

    MulticastDelegate<const SaveWriteReport&>& SaveService::OnWriteCompleted()
    {
        return m_OnWriteCompleted;
    }

    bool SaveService::StartPendingWrite()
    {
        auto job = Container::MakeShared<WriteJob>();
        job->Reports.reserve(m_PendingSlots.size() + (m_bPendingAutosave ? 1 : 0));
        for (uint32_t slot : m_PendingSlots)
        {
            SaveWriteReport report;
            report.Target = SaveWriteTarget::Slot;
            report.Slot = slot;
            report.Reasons = m_PendingReasons;
            job->Reports.push_back(std::move(report));
        }
        if (m_bPendingAutosave)
        {
            SaveWriteReport report;
            report.Target = SaveWriteTarget::Autosave;
            report.Reasons = m_PendingReasons;
            job->Reports.push_back(std::move(report));
        }
        m_PendingSlots.clear();
        m_PendingReasons.clear();
        m_bPendingAutosave = false;

        if (!m_Store || !m_SnapshotProvider.IsBound())
        {
            NORVES_LOG_WARNING("SaveService", "保存先またはスナップショットの提供元が未設定です。");
            return false;
        }

        Container::String payload;
        const auto provider = m_SnapshotProvider;
        bool bSnapshotReady = false;
        m_bCapturingSnapshot = true;
        try
        {
            bSnapshotReady = provider.Invoke(payload);
        }
        catch (...)
        {
            m_bCapturingSnapshot = false;
            NORVES_LOG_WARNING("SaveService", "スナップショットの作成中に例外が発生しました。");
            return false;
        }
        m_bCapturingSnapshot = false;
        if (!bSnapshotReady)
        {
            NORVES_LOG_WARNING("SaveService", "スナップショットを作成できませんでした。");
            return false;
        }

        job->PayloadUtf8.resize(payload.size());
        if (!payload.empty())
        {
            std::memcpy(job->PayloadUtf8.data(), payload.data(), payload.size());
        }
        job->Store = m_Store.get();
        m_Write = job;

        bool bSubmitted = false;
        try
        {
            auto& jobSystem = Thread::JobSystem::Get();
            // 未初期化の JobSystem はワーカーがいなくても投入を受理する。
            if (jobSystem.GetWorkerThreadCount() != 0)
            {
                auto task = Thread::Task::Create([job]() { ExecuteWrite(job); });
                bSubmitted = jobSystem.SubmitTask(task);
            }
        }
        catch (...)
        {
            NORVES_LOG_WARNING("SaveService", "保存タスクを投入できませんでした。");
        }
        if (!bSubmitted)
        {
            NORVES_LOG_WARNING("SaveService", "保存を GameThread で同期実行します。");
            ExecuteWrite(job);
        }
        return true;
    }

    void SaveService::ExecuteWrite(const Container::TSharedPtr<WriteJob>& job)
    {
        for (auto& report : job->Reports)
        {
            try
            {
                if (report.Target == SaveWriteTarget::Slot)
                {
                    report.Result = job->Store->SaveSlot(report.Slot, job->PayloadUtf8);
                }
                else
                {
                    report.Result = job->Store->SaveAutosave(job->PayloadUtf8, &report.AutosaveIndex);
                }
            }
            catch (...)
            {
                // 例外でも残りの対象を処理し、終了待ちが永久に続くのを防ぐ。
                report.Result.bSuccess = false;
                report.Result.Kind = Platform::FileErrorKind::IoError;
                report.Result.NativeError = 0;
                report.Result.Message.clear();
            }
        }
        // 以後は保存先に触れない。結果はこの印を介して GameThread に公開する。
        job->bCompleted.Store(true, std::memory_order_release);
    }

    void SaveService::CollectCompletedWrite()
    {
        const auto completed = m_Write;
        if (!completed || !completed->bCompleted.Load(std::memory_order_acquire))
        {
            return;
        }
        m_Write.reset();
        ++m_CompletedWriteCount;
        for (const auto& report : completed->Reports)
        {
            if (!report.Result.bSuccess)
            {
                NORVES_LOG_WARNING("SaveService", "セーブの書き込みに失敗しました: %s", report.Result.Message.c_str());
            }
            // 通知中の購読変更で列挙が壊れないよう、今回の通知先を保持する。
            const auto listeners = m_OnWriteCompleted;
            listeners.Broadcast(report);
        }
    }

    void SaveService::WaitForWrite() const
    {
        const auto job = m_Write;
        while (job && !job->bCompleted.Load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    SaveInhibitScope::SaveInhibitScope(SaveService& service)
        : m_Service(service), m_bActive(service.GetSaveInhibitCount() != (std::numeric_limits<uint32_t>::max)())
    {
        m_Service.PushSaveInhibit();
    }

    SaveInhibitScope::~SaveInhibitScope()
    {
        if (m_bActive)
        {
            m_Service.PopSaveInhibit();
        }
    }
} // namespace NorvesLib::Core::Save
