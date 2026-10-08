#pragma once
#include "Animation/RootMotion.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Core::Animation
{
    // 呼出側が選んだ整数時間単位で、Animationの差分をsimulationの区間へ配る。
    // 単位の変換・停止中の破棄・schedulerのdrop・世代切替は呼出側の責務。
    class RootMotionTimeQueue final
    {
      public:
        // 消費cursorの複製で同じ変位を二重適用しないよう、所有先を固定する。
        RootMotionTimeQueue() = default;
        RootMotionTimeQueue(const RootMotionTimeQueue&) = delete;
        RootMotionTimeQueue& operator=(const RootMotionTimeQueue&) = delete;
        RootMotionTimeQueue(RootMotionTimeQueue&&) = delete;
        RootMotionTimeQueue& operator=(RootMotionTimeQueue&&) = delete;
        static constexpr size_t MaximumSegments = 4096;
        // 巨大角度の分割で三角関数の位相精度が失われる範囲を受理しない。
        static constexpr double MaximumAbsoluteYaw = 1024;
        // 0時間・非有限差分・角度範囲外・時間overflow・容量超過は拒否し、以前の区間を保持する。
        bool Push(const RootMotionDelta& delta, uint64_t duration);
        // 不足した時間はidentityとする。時間の借金を作らない。失敗時outとqueueを保持する。
        bool Peek(uint64_t duration, RootMotionDelta& out) const;
        bool Consume(uint64_t duration, RootMotionDelta& out);
        // 衝突で阻止された変位やschedulerが捨てた時間は、再適用せず消費する。
        void Discard(uint64_t duration);
        void Clear();
        uint64_t GetRemainingTime() const
        {
            return m_Remaining;
        }
        size_t GetPendingSegmentCount() const
        {
            return m_Segments.size() - m_Begin;
        }

      private:
        struct Segment
        {
            RootMotionDelta Delta;
            uint64_t Duration = 0, Consumed = 0;
        };
        Container::VariableArray<Segment> m_Segments;
        size_t m_Begin = 0;
        uint64_t m_Remaining = 0;
    };
} // namespace NorvesLib::Core::Animation
