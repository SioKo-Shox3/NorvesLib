#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Locomotion/QuadrupedLocomotionParams.h"

namespace NorvesLib::Modules::AI
{
    class LocomotionProfileCache
    {
    public:
        Core::Locomotion::QuadrupedLocomotionParams Get(Core::Container::AnsiStringView logicalPath);
        void Clear() { m_Entries.clear(); }

    private:
        struct Entry
        {
            Core::Container::String Path;
            Core::Locomotion::QuadrupedLocomotionParams Params;
        };
        Core::Container::VariableArray<Entry> m_Entries;
    };
}
