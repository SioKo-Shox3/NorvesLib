#include "AI/Motor/LocomotionProfileCache.h"

#include "Asset/AssetFileReader.h"
#include "Locomotion/QuadrupedLocomotionJson.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Modules::AI
{
    Core::Locomotion::QuadrupedLocomotionParams LocomotionProfileCache::Get(Core::Container::AnsiStringView logicalPath)
    {
        if (logicalPath.empty())
        {
            return {};
        }
        for (const auto& entry : m_Entries)
        {
            if (Core::Container::AnsiStringView(entry.Path) == logicalPath)
            {
                return entry.Params;
            }
        }

        auto& entry = m_Entries.emplace_back();
        entry.Path = Core::Container::String(logicalPath);
        const auto asset = Core::Asset::AssetFileReader().Read(logicalPath);
        Core::Container::String error;
        if (!asset.Succeeded())
        {
            NORVES_LOG_WARNING("AI", "歩様設定 %s を読み込めないため既定値を使います。", entry.Path.c_str());
        }
        else if (!Core::Locomotion::ParseQuadrupedLocomotionJson(asset.Blob.GetSpan(), entry.Params, error))
        {
            NORVES_LOG_WARNING("AI", "歩様設定 %s が不正なため既定値を使います。%s", entry.Path.c_str(), error.c_str());
        }
        return entry.Params;
    }
}
