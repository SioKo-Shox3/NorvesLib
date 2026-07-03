#include "Rendering/MaterialDescriptorCache.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    using DescriptorValue = Container::TSharedPtr<int>;
    using DescriptorCache = Container::UnorderedMap<uint64_t, DescriptorValue>;

    DescriptorValue MakeDescriptorValue(uint64_t key)
    {
        return Container::MakeShared<int>(static_cast<int>(key));
    }

    void HitReusesDescriptorWithoutCallingBuilder()
    {
        DescriptorCache cache;
        uint32_t builderCalls = 0;

        auto builder = [&](uint64_t key) -> DescriptorValue
        {
            ++builderCalls;
            return MakeDescriptorValue(key);
        };

        DescriptorValue first = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 42, builder);
        DescriptorValue second = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 42, builder);
        DescriptorValue third = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 42, builder);

        assert(Container::IsValid(first));
        assert(first.get() == second.get());
        assert(second.get() == third.get());
        assert(builderCalls == 1);
        assert(cache.size() == 1);
    }

    void DifferentMaterialIdsCreateDistinctEntries()
    {
        DescriptorCache cache;
        uint32_t builderCalls = 0;

        auto builder = [&](uint64_t key) -> DescriptorValue
        {
            ++builderCalls;
            return MakeDescriptorValue(key);
        };

        DescriptorValue first = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 10, builder);
        DescriptorValue second = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 20, builder);
        DescriptorValue third = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 30, builder);

        assert(Container::IsValid(first));
        assert(Container::IsValid(second));
        assert(Container::IsValid(third));
        assert(first.get() != second.get());
        assert(second.get() != third.get());
        assert(builderCalls == 3);
        assert(cache.size() == 3);
    }

    void InvalidMaterialIdZeroIsCachedAsOneEntry()
    {
        DescriptorCache cache;
        uint32_t builderCalls = 0;

        auto builder = [&](uint64_t key) -> DescriptorValue
        {
            ++builderCalls;
            return MakeDescriptorValue(key);
        };

        DescriptorValue first = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 0, builder);
        DescriptorValue second = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 0, builder);

        assert(Container::IsValid(first));
        assert(first.get() == second.get());
        assert(*first == 0);
        assert(builderCalls == 1);
        assert(cache.size() == 1);
    }

    void NullBuilderResultIsNotCached()
    {
        DescriptorCache cache;
        uint32_t builderCalls = 0;

        auto builder = [&](uint64_t) -> DescriptorValue
        {
            ++builderCalls;
            return DescriptorValue{};
        };

        DescriptorValue first = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 99, builder);
        DescriptorValue second = Rendering::ResolveMaterialDescriptor<DescriptorCache, DescriptorValue>(cache, 99, builder);

        assert(Container::IsNull(first));
        assert(Container::IsNull(second));
        assert(builderCalls == 2);
        assert(cache.empty());
    }
} // namespace

int main()
{
    HitReusesDescriptorWithoutCallingBuilder();
    DifferentMaterialIdsCreateDistinctEntries();
    InvalidMaterialIdZeroIsCachedAsOneEntry();
    NullBuilderResultIsNotCached();

    std::cout << "GBufferMaterialDescriptorCacheTest passed" << std::endl;
    return 0;
}
