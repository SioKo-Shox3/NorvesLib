#include "Ecs/EcsTypes.h"

#include "Ecs/EcsWorld.h"

#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        std::atomic<int32_t> g_ParallelDepth{0};
        std::atomic<uint32_t> g_UnregisteredReports{0};
        std::atomic<uint32_t> g_ParallelAccessReports{0};
        std::atomic<EcsParallelAccessHandler> g_ParallelAccessHandler{nullptr};
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

        bool EcsCheckSerialAccess()
        {
#if NORVES_BUILD_DEVELOPMENT
            if (!EcsIsParallelActive())
            {
                return true;
            }
            g_ParallelAccessReports.fetch_add(1, std::memory_order_relaxed);
            if (const EcsParallelAccessHandler handler = g_ParallelAccessHandler.load(std::memory_order_acquire))
            {
                handler();
                return false;
            }
            // RelWithDebInfo は NDEBUG で assert が消えるため、assert に頼らず止める
            std::fputs("ECS_PARALLEL_PROPERTY_ACCESS ECS_PROPERTY を並列の中で読み書きした。GameThread かつ並列の外だけが触ること\n", stderr);
            std::fflush(stderr);
            std::abort();
#else
            return true;
#endif
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
            // RelWithDebInfo は NDEBUG で assert が消えるため、assert に頼らず止める
            std::fputs("ECS_UNREGISTERED_COLUMN 未登録の ECS の列の型を並列の中で使った。EcsRegisterColumn<T>() を並列の前に呼ぶこと\n", stderr);
            std::fflush(stderr);
            std::abort();
#endif
        }
    }

    EcsUnregisteredColumnHandler EcsSetUnregisteredColumnHandler(EcsUnregisteredColumnHandler handler)
    {
        return g_UnregisteredHandler.exchange(handler, std::memory_order_acq_rel);
    }

    EcsParallelAccessHandler EcsSetParallelAccessHandler(EcsParallelAccessHandler handler)
    {
        return g_ParallelAccessHandler.exchange(handler, std::memory_order_acq_rel);
    }

    uint32_t EcsGetParallelAccessReportCount()
    {
        return g_ParallelAccessReports.load(std::memory_order_relaxed);
    }

    uint32_t EcsGetUnregisteredColumnReportCount()
    {
        return g_UnregisteredReports.load(std::memory_order_relaxed);
    }

    const void *EcsGetColumnPartPtr(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size)
    {
        if (!Detail::EcsCheckSerialAccess() || !row.IsBound())
        {
            return nullptr;
        }
        const EcsWorld *world = row.World;
        const EcsArchetypeId archetype = world->GetArchetypeOf(row.Entity);
        if (archetype == kInvalidEcsArchetype)
        {
            return nullptr;
        }
        const int32_t index = world->FindColumnIndex(archetype, column);
        if (index < 0 || static_cast<size_t>(offset) + size > world->GetArchetypeColumns(archetype)[static_cast<size_t>(index)].Size)
        {
            return nullptr;
        }
        const void *base = world->GetColumnValue(row.Entity, column);
        return base != nullptr ? static_cast<const std::byte *>(base) + offset : nullptr;
    }

    bool EcsRowHasColumn(const EcsRowRef &row, TypeId column)
    {
        return Detail::EcsCheckSerialAccess() && row.IsBound() && static_cast<const EcsWorld *>(row.World)->GetColumnValue(row.Entity, column) != nullptr;
    }

    bool EcsReadColumnPart(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size, void *out)
    {
        const void *source = EcsGetColumnPartPtr(row, column, offset, size);
        if (source == nullptr || out == nullptr)
        {
            return false;
        }
        std::memcpy(out, source, size);
        return true;
    }

    bool EcsWriteColumnPart(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size, const void *value)
    {
        return Detail::EcsCheckSerialAccess() && row.IsBound() && row.World->WriteColumnPart(row.Entity, column, offset, size, value);
    }

}
