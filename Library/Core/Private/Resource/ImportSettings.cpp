#include "Resource/ImportSettings.h"
#include <bit>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::AssetImport
{
    SettingsResult ValidateSettings(const ImportSettings& settings) noexcept
    {
        if (!std::isfinite(settings.Scale) || settings.Scale <= 0 ||
            !std::isfinite(settings.FitMeters) || settings.FitMeters <= 0 ||
            static_cast<uint8_t>(settings.Fit) > static_cast<uint8_t>(FitAxis::Longest) ||
            static_cast<uint8_t>(settings.Up) > static_cast<uint8_t>(SignedAxis::NegativeZ) ||
            static_cast<uint8_t>(settings.Forward) > static_cast<uint8_t>(SignedAxis::NegativeZ) ||
            static_cast<uint8_t>(settings.Up) / 2 == static_cast<uint8_t>(settings.Forward) / 2 ||
            static_cast<uint8_t>(settings.Origin) > static_cast<uint8_t>(OriginMode::Custom) ||
            static_cast<uint8_t>(settings.Winding) > static_cast<uint8_t>(WindingMode::Auto))
        {
            return SettingsResult::InvalidValue;
        }
        if ((settings.Fit == FitAxis::None && settings.FitMeters != 1.0) ||
            (settings.Fit != FitAxis::None && settings.Scale != 1.0))
        {
            return SettingsResult::InvalidValue;
        }
        for (const double value : settings.OriginOffset)
        {
            if (!std::isfinite(value) || (settings.Origin != OriginMode::Custom && value != 0.0))
            {
                return SettingsResult::InvalidValue;
            }
        }
        if (settings.Origin == OriginMode::SurfaceCentroid)
        {
            return SettingsResult::UnsupportedFeature;
        }
        return SettingsResult::Success;
    }

    CanonicalSettings EncodeCanonicalSettings(const ImportSettings& settings) noexcept
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        CanonicalSettings result;
        if (ValidateSettings(settings) != SettingsResult::Success)
        {
            return result;
        }
        size_t index = 0;
        const auto append = [&](uint64_t value, size_t count)
        {
            for (size_t byte = 0; byte < count; ++byte)
            {
                result.Bytes[index++] = static_cast<uint8_t>(value >> (byte * 8));
            }
        };
        const auto appendDouble = [&](double value)
        {
            append(std::bit_cast<uint64_t>(value == 0.0 ? 0.0 : value), 8);
        };
        append(ImportSettingsSchemaVersion, 4);
        appendDouble(settings.Scale);
        append(static_cast<uint8_t>(settings.Fit), 1);
        appendDouble(settings.FitMeters);
        append(static_cast<uint8_t>(settings.Up), 1);
        append(static_cast<uint8_t>(settings.Forward), 1);
        append(settings.bMirrorX ? 1 : 0, 1);
        append(static_cast<uint8_t>(settings.Origin), 1);
        for (const double value : settings.OriginOffset)
        {
            appendDouble(value);
        }
        append(static_cast<uint8_t>(settings.Winding), 1);
        append(settings.bFlipU ? 1 : 0, 1);
        append(settings.bFlipV ? 1 : 0, 1);
        result.Size = index;
        return result;
    }
} // namespace NorvesLib::Core::AssetImport
