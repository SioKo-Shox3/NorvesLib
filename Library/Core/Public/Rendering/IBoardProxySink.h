#pragma once

#include "Container/UnorderedSet.h"
#include "Rendering/SceneProxy.h"

namespace NorvesLib::Core::Rendering
{
    class IBoardProxySink
    {
    public:
        virtual ~IBoardProxySink() = default;

        virtual void UpdateBoardProxy(uint64_t componentId, const BoardProxy &proxy) = 0;
        virtual void RemoveBoardProxy(uint64_t componentId) = 0;
        virtual void RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds) = 0;
    };
} // namespace NorvesLib::Core::Rendering
