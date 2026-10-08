#pragma once
#include "Animation/ClipMetadata.h"
#include "Animation/ClipPlayback.h"
#include "Container/FixedArray.h"
#include "Delegate/MulticastDelegate.h"
namespace NorvesLib::Core::Animation
{
    enum class AnimEventKind : uint8_t
    {
        Point,
        Begin,
        End
    };
    struct AnimEventInfo
    {
        Identity Name;
        Identity SyncGroup;
        uint32_t Node = InvalidAnimNode;
        uint32_t EventIndex = 0;
        AnimEventKind Kind = AnimEventKind::Point;
        bool bInterrupted = false;
        float Value = 0;
        int32_t IntValue = 0;
        double OffsetSeconds = 0;
        uint64_t Sequence = 0, Batch = 0, WindowToken = 0;
        int64_t Occurrence = 0;
    };
    static_assert(std::is_trivially_copyable_v<AnimEventInfo>);
    class AnimationEventQueue
    {
      public:
        static constexpr size_t Capacity = 64;
        // 登録/解除と配送はGameThread。Update中はcallbackを呼ばない。
        MulticastDelegate<const AnimEventInfo&> OnEvent;
        void Update(const AnimGraphData&, Container::Span<const AnimClipTraversal>, float dt);
        void InterruptAll();
        void Dispatch();
        void Reset();
        bool IsDispatching() const noexcept
        {
            return m_bDispatching;
        }
        uint64_t DroppedCount() const noexcept
        {
            return m_Dropped;
        }
        size_t PendingCount() const noexcept
        {
            return m_Count;
        }
        size_t ActiveWindowCount() const noexcept
        {
            return m_Active;
        }

      private:
        struct Window
        {
            AnimEventInfo Info;
            uint64_t MetadataRevision = 0;
            uint32_t SourceNode = InvalidAnimNode, SourceEvent = 0;
            bool bActive = false;
        };
        bool Enqueue(AnimEventInfo, bool reserveEnd);
        bool Begin(const AnimEventInfo&, uint64_t revision);
        void End(size_t slot, bool interrupted, double offset);
        size_t FindWindow(uint32_t node, uint32_t event) const;
        Container::FixedArray<AnimEventInfo, Capacity> m_Queue;
        Container::FixedArray<Window, Capacity> m_Windows;
        size_t m_Count = 0, m_Active = 0;
        uint64_t m_Sequence = 0, m_Dropped = 0, m_Batch = 0;
        bool m_bDispatching = false;
        double m_DispatchEndOffset = 0, m_LastDelta = 0;
    };
} // namespace NorvesLib::Core::Animation
