#pragma once

#include "Container/StringView.h"
#include "Scene/SurfaceRegistry.h"

namespace NorvesLib::Modules::Physics
{
    inline bool IsValidSurfaceName(Core::Container::StringView name)
    {
        if (name.empty() || name.size() > Core::Scene::SurfaceRegistry::MaximumNameBytes || !name.data())
        {
            return false;
        }
        for (const auto ch : name)
        {
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_'))
            {
                return false;
            }
        }
        return true;
    }
}
