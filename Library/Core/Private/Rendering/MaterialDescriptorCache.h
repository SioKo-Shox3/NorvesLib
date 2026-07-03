#pragma once

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    template <typename Cache, typename ValuePtr, typename Builder>
    ValuePtr ResolveMaterialDescriptor(Cache& cache, uint64_t key, Builder&& builder)
    {
        auto it = cache.find(key);
        if (it != cache.end())
        {
            return it->second;
        }

        ValuePtr built = builder(key);
        if (!built)
        {
            return built;
        }

        cache.emplace(key, built);
        return built;
    }
} // namespace NorvesLib::Core::Rendering
