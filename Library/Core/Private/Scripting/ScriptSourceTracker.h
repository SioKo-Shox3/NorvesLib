#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Scripting
{
    using ScriptContentHash = uint64_t;

    enum class EScriptSourceReadResult
    {
        Success,
        InvalidPath,
        ReadFailed
    };

    enum class EScriptSourcePollResult
    {
        NotDue,
        NoChanges,
        Changes
    };

    struct ScriptSourceBindingView
    {
        uint32_t SlotIndex = ~uint32_t{0};
        uint32_t Generation = 0;
        Container::StringView ScriptPath;
        Container::StringView ScriptClassName;
    };

    struct ScriptSourceSnapshot
    {
        EScriptSourceReadResult Result = EScriptSourceReadResult::ReadFailed;
        Container::String RequestedPath;
        Container::String LogicalPath;
        Container::AnsiString Bytes;
        ScriptContentHash ContentHash = 0;
    };

    struct ScriptSourceChange
    {
        uint32_t SlotIndex = ~uint32_t{0};
        uint32_t Generation = 0;
        uint32_t SourceIndex = ~uint32_t{0};
        Container::String ScriptClassName;
    };

    struct ScriptSourcePollBatch
    {
        Container::VariableArray<ScriptSourceSnapshot> Sources;
        Container::VariableArray<ScriptSourceChange> Changes;
        uint64_t Fingerprint = 0;

        void Reset();
    };

    struct ScriptSourceApproval
    {
        uint32_t SlotIndex = ~uint32_t{0};
        uint32_t PreviousGeneration = 0;
        uint32_t Generation = 0;
        Container::String ApprovedPropertyPath;
        Container::String ApprovedClassName;
        Container::String ApprovedLogicalPath;
        ScriptContentHash ApprovedContentHash = 0;
    };

    class ScriptSourceTracker final
    {
    public:
        static constexpr float PollIntervalSeconds = 0.25f;

        void Reset();
        EScriptSourceReadResult ReadSource(
            Container::StringView requestedPath,
            ScriptSourceSnapshot& outSnapshot) const;
        EScriptSourcePollResult Poll(
            float deltaSeconds,
            Container::Span<const ScriptSourceBindingView> bindings,
            ScriptSourcePollBatch& outBatch);
        void ReserveBindingCapacity(size_t requiredCapacity);
        void RegisterBinding(ScriptSourceApproval&& approval);
        void UnregisterBinding(uint32_t slotIndex, uint32_t generation);
        void RejectBatch(uint64_t fingerprint);
        [[nodiscard]] bool CanApproveBatch(
            Container::Span<const ScriptSourceApproval> approvals) const noexcept;
        void ApproveBatch(Container::Span<ScriptSourceApproval> approvals) noexcept;

    private:
        struct BindingRecord
        {
            ScriptSourceApproval Approved;
        };

        Container::VariableArray<BindingRecord> m_Bindings;
        float m_AccumulatedSeconds = 0.0f;
        uint64_t m_LastRejectedFingerprint = 0;
        bool m_bHasRejectedFingerprint = false;
    };
} // namespace NorvesLib::Core::Scripting
