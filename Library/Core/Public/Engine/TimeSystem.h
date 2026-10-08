#pragma once
#include "Container/VariableArray.h"
#include "Engine/TimeChannels.h"
#include "Text/IdentityPool.h"
namespace NorvesLib::Core::Engine
{
    struct TimeScaleHandle
    {
        uint64_t Value = 0;
        bool IsValid() const
        {
            return Value != 0;
        }
    };
    struct TimeScaleRequest
    {
        TimeChannelMask Channels = TimeChannelBit(TimeChannel::World);
        double Scale = 1, DurationSeconds = .1, FadeInSeconds = 0, FadeOutSeconds = 0;
        // 全要求の積を適用するため上書き優先度ではない。診断用の分類として保持する。
        int32_t Priority = 0;
        Identity Tag;
    };
    enum class TimeSystemResult : uint8_t
    {
        Success,
        InvalidArgument,
        Overflow,
        StaleHandle
    };
    // Engineが所有するGameThread専用の論理時計。実時計やsingletonに依存しない。
    class TimeSystem
    {
      public:
        // durationはfade込みの総実時間。fadeは線形、ns変換後の両fade合計がdurationを超える要求は拒否する。
        // 秒は最近傍ns（half-up）へ丸める。Unscaled要求は拒否。Removeは即時取消し。
        TimeSystemResult PushScale(const TimeScaleRequest& request, TimeScaleHandle& out);
        TimeSystemResult RemoveScale(TimeScaleHandle handle);
        // 呼出時点の要求でframeを確定。以後のPush/Removeは次回BeginFrameから効く。
        // World等にはclampedDtをraw各区間の比率で配賦、Physicsにはraw nsを使う。
        // pauseは期限だけ進め、Physics端数を保持する。失敗時は時計/要求/frame/端数不変。
        // Physicsの定倍率はQ16要求の積を最後に1回丸め、frame分割不変。
        // fade中は積の区間平均をQ16に丸める近似（最大区間時間/131072の丸め誤差）。
        TimeSystemResult BeginFrame(int64_t rawNanoseconds, float clampedDt, bool advanceSimulation = true);
        const FrameTimes& GetFrameTimes() const
        {
            return m_Frame;
        }
        int64_t GetElapsedNanoseconds() const
        {
            return m_Now;
        }
        uint32_t GetPhysicsRemainder() const
        {
            return m_PhysicsCarry;
        }
        size_t GetActiveRequestCount() const
        {
            return m_Requests.size();
        }
        void Reset();

      private:
        struct Request
        {
            TimeScaleHandle Handle;
            TimeScaleRequest Settings;
            int64_t Start = 0, End = 0, FadeInEnd = 0, FadeOutStart = 0;
            uint32_t Numerator = 65536;
        };
        Container::VariableArray<Request> m_Requests;
        FrameTimes m_Frame;
        int64_t m_Now = 0;
        uint64_t m_NextId = 1;
        uint32_t m_PhysicsCarry = 0;
    };
} // namespace NorvesLib::Core::Engine
