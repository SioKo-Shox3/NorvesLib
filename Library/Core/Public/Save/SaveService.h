#pragma once

#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"
#include "Delegate/MulticastDelegate.h"
#include "Save/SaveSlotStore.h"
#include <cstdint>

namespace NorvesLib::Core::Save
{
    enum class SaveWriteTarget : uint8_t { Slot, Autosave };

    struct SaveWriteReport
    {
        SaveWriteTarget Target = SaveWriteTarget::Slot;
        uint32_t Slot = 0;
        // 自動保存先が決まらなかった場合は SaveAutosaveRingSize のまま。
        uint32_t AutosaveIndex = SaveAutosaveRingSize;
        Platform::FileOperationResult Result;
        Container::String Reasons;
    };

    // 公開操作とデリゲートは GameThread で使う。ファイル操作の参照は設定中ずっと保持する。
    class SaveService
    {
    public:
        SaveService() = default;
        ~SaveService();
        SaveService(const SaveService&) = delete;
        SaveService& operator=(const SaveService&) = delete;
        SaveService(SaveService&&) = delete;
        SaveService& operator=(SaveService&&) = delete;

        void Configure(const Container::String& directory);
        void Configure(const Container::String& directory, Platform::IFileOperations& ops);
        void SetSnapshotProvider(Delegate<bool, Container::String&> provider);
        void SetLoadConsumer(Delegate<bool, Container::Span<const uint8_t>> consumer);

        void Save(uint32_t slot);
        // 撃破・地点通過・休息などの契機から自動保存を要求する。
        void RequestAutosave(const Container::String& reason);
        void Update(bool bModeTransitionPending);

        void PushSaveInhibit();
        void PopSaveInhibit();
        uint32_t GetSaveInhibitCount() const;

        SaveLoadResult Load(uint32_t slot);
        void FlushAndWait();

        bool IsWriteInFlight() const;
        bool HasPendingRequests() const;
        uint64_t GetCompletedWriteCount() const;
        MulticastDelegate<const SaveWriteReport&>& OnWriteCompleted();

    private:
        struct WriteJob;

        bool StartPendingWrite();
        void CollectCompletedWrite();
        void WaitForWrite() const;
        static void ExecuteWrite(const Container::TSharedPtr<WriteJob>& job);

        Container::TUniquePtr<SaveSlotStore> m_Store;
        Delegate<bool, Container::String&> m_SnapshotProvider;
        Delegate<bool, Container::Span<const uint8_t>> m_LoadConsumer;
        MulticastDelegate<const SaveWriteReport&> m_OnWriteCompleted;
        Container::VariableArray<uint32_t> m_PendingSlots;
        Container::String m_PendingReasons;
        Container::TSharedPtr<WriteJob> m_Write;
        uint64_t m_CompletedWriteCount = 0;
        uint32_t m_SaveInhibitCount = 0;
        bool m_bPendingAutosave = false;
        bool m_bCapturingSnapshot = false;
    };

    class SaveInhibitScope
    {
    public:
        explicit SaveInhibitScope(SaveService& service);
        ~SaveInhibitScope();
        SaveInhibitScope(const SaveInhibitScope&) = delete;
        SaveInhibitScope& operator=(const SaveInhibitScope&) = delete;
        SaveInhibitScope(SaveInhibitScope&&) = delete;
        SaveInhibitScope& operator=(SaveInhibitScope&&) = delete;

    private:
        SaveService& m_Service;
        bool m_bActive;
    };
} // namespace NorvesLib::Core::Save
