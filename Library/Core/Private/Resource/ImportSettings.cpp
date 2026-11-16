#include "Resource/ImportSettings.h"
#include "Resource/ClipImportSettings.h"
#include "Asset/AssetPackageFormat.h"
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

    Container::VariableArray<uint8_t> EncodeCanonicalClipSettings(const ClipImportSettings& settings)
    {
        Container::VariableArray<uint8_t> result;
        if (!settings.IsPresent())
        {
            return result;
        }
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        const auto append = [&](uint64_t value, size_t count)
        {
            for (size_t byte = 0; byte < count; ++byte)
            {
                result.push_back(static_cast<uint8_t>(value >> (byte * 8)));
            }
        };
        const auto appendDouble = [&](double value)
        {
            append(std::bit_cast<uint64_t>(value == 0.0 ? 0.0 : value), 8);
        };
        append(ClipImportSettingsCanonicalVersion, 4);
        append(settings.Keys, 4);
        if (settings.Keys & ClipKeyRootJoint)
        {
            append(static_cast<uint32_t>(settings.RootJoint.size()), 4);
            for (size_t index = 0; index < settings.RootJoint.size(); ++index)
            {
                append(static_cast<uint8_t>(settings.RootJoint[index]), 1);
            }
        }
        if (settings.Keys & ClipKeyLoop) { append(static_cast<uint8_t>(settings.Loop), 1); }
        if (settings.Keys & ClipKeyLoopThreshold) { appendDouble(settings.LoopThreshold); }
        if (settings.Keys & ClipKeyAuthoredFps) { appendDouble(settings.AuthoredFps); }
        if (settings.Keys & ClipKeySourceFps) { appendDouble(settings.SourceFps); }
        if (settings.Keys & ClipKeyTimeScale) { appendDouble(settings.TimeScale); }
        if (settings.Keys & ClipKeyAnalysis) { append(settings.bAnalysis ? 1 : 0, 1); }
        return result;
    }

    uint64_t AppendClipImportSettingsHash(uint64_t hash, const ClipImportSettings& settings)
    {
        if (!settings.IsPresent())
        {
            return hash;
        }
        const auto canonical = EncodeCanonicalClipSettings(settings);
        const auto append = [&](uint8_t byte)
        {
            hash = (hash ^ byte) * Asset::AssetPackageFormatV1::Fnv1a64Prime;
        };
        for (size_t byte = 0; byte < sizeof(uint64_t); ++byte)
        {
            append(static_cast<uint8_t>(static_cast<uint64_t>(canonical.size()) >> (byte * 8)));
        }
        for (const uint8_t byte : canonical)
        {
            append(byte);
        }
        return hash;
    }
} // namespace NorvesLib::Core::AssetImport
