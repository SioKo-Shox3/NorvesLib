// ストリーミング用の時計と仕事の出し先を実装する。
#include "Streaming/IStreamingEnvironment.h"

#include "Thread/JobSystem.h"

#include <chrono>
#include <utility>

namespace NorvesLib::Core::Streaming
{
    double SteadyStreamingClock::NowMs() const
    {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool JobSystemStreamingExecutor::Submit(Thread::TaskFunction job)
    {
        return Thread::JobSystem::Get().SubmitTask(Thread::Task::Create(std::move(job)));
    }
} // namespace NorvesLib::Core::Streaming
