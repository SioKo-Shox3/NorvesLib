// ストリーミングで使う時計と仕事の出し先を定義する。
#pragma once

#include "Thread/Task.h"

namespace NorvesLib::Core::Streaming
{
    class IStreamingClock
    {
    public:
        virtual ~IStreamingClock() = default;
        virtual double NowMs() const = 0;
    };

    class IStreamingJobExecutor
    {
    public:
        virtual ~IStreamingJobExecutor() = default;

        // 1 セルにつき 1 仕事。受け付けなければ false を返し、呼び手はそのフレームの投入をやめる。
        virtual bool Submit(Thread::TaskFunction job) = 0;
    };

    // 経過時間の予算判定に単調な実行時の時計を使う。
    class SteadyStreamingClock final : public IStreamingClock
    {
    public:
        double NowMs() const override;
    };

    // 既存のジョブシステムへ仕事を投入する。
    class JobSystemStreamingExecutor final : public IStreamingJobExecutor
    {
    public:
        bool Submit(Thread::TaskFunction job) override;
    };
} // namespace NorvesLib::Core::Streaming
