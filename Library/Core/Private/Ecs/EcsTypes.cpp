#include "Ecs/EcsTypes.h"

#include <cassert>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        std::atomic<int32_t> g_ParallelDepth{0};
        std::atomic<uint32_t> g_UnregisteredReports{0};
        std::atomic<EcsUnregisteredColumnHandler> g_UnregisteredHandler{nullptr};
    }

    namespace Detail
    {
        void EcsEnterParallel()
        {
            g_ParallelDepth.fetch_add(1, std::memory_order_acq_rel);
        }

        void EcsLeaveParallel()
        {
            const int32_t before = g_ParallelDepth.fetch_sub(1, std::memory_order_acq_rel);
            assert(before > 0 && "EcsLeaveParallel が EcsEnterParallel より多い");
            (void)before;
        }

        bool EcsIsParallelActive()
        {
            return g_ParallelDepth.load(std::memory_order_acquire) > 0;
        }

        void EcsReportUnregisteredColumn()
        {
            g_UnregisteredReports.fetch_add(1, std::memory_order_relaxed);
            if (const EcsUnregisteredColumnHandler handler = g_UnregisteredHandler.load(std::memory_order_acquire))
            {
                handler();
                return;
            }
#if NORVES_BUILD_DEVELOPMENT
            assert(false && "未登録の ECS の列の型を並列の中で使った。EcsRegisterColumn<T>() を並列の前に呼ぶこと");
#endif
        }
    }

    EcsUnregisteredColumnHandler EcsSetUnregisteredColumnHandler(EcsUnregisteredColumnHandler handler)
    {
        return g_UnregisteredHandler.exchange(handler, std::memory_order_acq_rel);
    }

    uint32_t EcsGetUnregisteredColumnReportCount()
    {
        return g_UnregisteredReports.load(std::memory_order_relaxed);
    }

}
