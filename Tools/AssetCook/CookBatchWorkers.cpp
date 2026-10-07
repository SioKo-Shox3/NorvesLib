#include "CookBatchWorkers.h"
#include "Thread/Thread.h"
#include <algorithm>
#include <exception>
namespace NorvesLib::Tools::AssetCook::Detail
{
    bool CookPreparedBatch(Core::Container::Span<const CookPreparedPlan> stages, uint32_t jobs,
                           Core::Container::AnsiString& error)
    {
        namespace C = Core::Container;
        if (jobs == 0 || jobs > MaximumCookBatchJobs)
        {
            error = "batch_jobs_out_of_range";
            return false;
        }
        if (jobs == 1 || stages.size() < 2)
        {
            for (const auto& stage : stages)
            {
                if (!CookSingleAsset(stage.Context.Request, error))
                {
                    return false;
                }
            }
            return true;
        }
        struct Result
        {
            bool bSucceeded = false;
            bool bException = false;
            C::AnsiString Error;
        };
        C::VariableArray<Result> results(stages.size());
        Thread::Atomic<size_t> next(0);
        // wrapperはthisを捕捉するため、開始後に移動させない。破棄順でも結果領域より先にjoinする。
        C::VariableArray<C::TUniquePtr<Thread::Thread>> workers;
        const size_t count = std::min(size_t(jobs), stages.size());
        workers.reserve(count);
        try
        {
            for (size_t i = 0; i < count; ++i)
            {
                workers.push_back(C::MakeUnique<Thread::Thread>());
                workers.back()->Start(
                    [&]()
                    {
                        for (;;)
                        {
                            const size_t index = next.FetchAdd(1, std::memory_order_relaxed);
                            if (index >= stages.size())
                            {
                                break;
                            }
                            auto& result = results[index];
                            try
                            {
                                result.bSucceeded = CookSingleAsset(stages[index].Context.Request, result.Error);
                            }
                            catch (...)
                            {
                                // 例外処理中に文字列を割り当てず、全workerの終了後に呼出元へ返す。
                                result.bException = true;
                            }
                        }
                    });
            }
        }
        catch (...)
        {
            for (auto& worker : workers)
            {
                worker->Join();
            }
            error = "batch_worker_start_failed";
            return false;
        }
        for (auto& worker : workers)
        {
            worker->Join();
        }
        // 実行順ではなく入力順で最初の失敗を選び、診断と公開順を再現可能にする。
        for (const auto& result : results)
        {
            if (!result.bSucceeded)
            {
                error = result.bException ? C::AnsiString("batch_worker_exception") : result.Error;
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
