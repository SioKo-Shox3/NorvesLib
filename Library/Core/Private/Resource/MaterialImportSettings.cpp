#include "Resource/MaterialImportSettings.h"
#include <bit>
#include <charconv>
#include <cstring>
#include <cmath>
#include <limits>
#include <initializer_list>

namespace NorvesLib::Core::AssetImport
{
    namespace
    {
        bool Unit(double value) noexcept
        {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }
        bool ValidScale(EmissiveScale scale) noexcept
        {
            return !scale.Present || (std::isfinite(scale.NitsPerUnit) && scale.NitsPerUnit > 0);
        }
        bool ValidArm(const ArmChannelPolicy& policy) noexcept
        {
            return static_cast<uint8_t>(policy.Mode) <= static_cast<uint8_t>(ArmMode::Auto) &&
                Unit(policy.Constant) && Unit(policy.AutoWidth);
        }
        void Apply(const MaterialSettingsLayer& layer, ResolvedMaterialSettings& out) noexcept
        {
            for (size_t index = 0; index < 3; ++index)
            {
                if ((layer.ArmMask & (1u << index)) != 0)
                {
                    out.Arm.Channels[index] = layer.Arm.Channels[index];
                }
            }
            if (layer.DoubleSided != DoubleSidedSetting::Inherit)
            {
                out.DoubleSided = layer.DoubleSided;
            }
            if (layer.AlphaMode != AlphaModeSetting::Inherit)
            {
                out.AlphaMode = layer.AlphaMode;
            }
        }
    }
    MaterialSettingsStatus ParseArmModeToken(Container::Span<const char> token, ArmChannelPolicy& out) noexcept
    {
        if (token.empty() || token.data() == nullptr)
        {
            return MaterialSettingsStatus::InvalidArmPolicy;
        }
        const auto equals = [&](const char* literal, size_t size)
        {
            return token.size() == size && std::memcmp(token.data(), literal, size) == 0;
        };
        ArmChannelPolicy result;
        if (equals("texture", 7))
        {
            result.Mode = ArmMode::Texture;
        }
        else if (equals("ignore", 6))
        {
            result.Mode = ArmMode::Ignore;
        }
        else if (equals("auto", 4))
        {
            result.Mode = ArmMode::Auto;
        }
        else
        {
            constexpr char prefix[] = "constant:";
            constexpr size_t prefixSize = sizeof(prefix) - 1;
            if (token.size() <= prefixSize || std::memcmp(token.data(), prefix, prefixSize) != 0)
            {
                return MaterialSettingsStatus::InvalidArmPolicy;
            }
            const auto parsed = std::from_chars(token.data() + prefixSize, token.data() + token.size(), result.Constant);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !Unit(result.Constant))
            {
                return MaterialSettingsStatus::InvalidArmPolicy;
            }
            result.Mode = ArmMode::Constant;
        }
        out = result;
        return MaterialSettingsStatus::Success;
    }
    MaterialSettingsStatus ValidateMaterialSettingsLayer(const MaterialSettingsLayer& layer) noexcept
    {
        if ((layer.ArmMask & ~uint8_t(7)) != 0)
        {
            return MaterialSettingsStatus::InvalidArmMask;
        }
        for (size_t index = 0; index < 3; ++index)
        {
            if ((layer.ArmMask & (1u << index)) != 0 && !ValidArm(layer.Arm.Channels[index]))
            {
                return MaterialSettingsStatus::InvalidArmPolicy;
            }
        }
        if (static_cast<uint8_t>(layer.DoubleSided) > static_cast<uint8_t>(DoubleSidedSetting::ForceFalse))
        {
            return MaterialSettingsStatus::InvalidDoubleSided;
        }
        if (static_cast<uint8_t>(layer.AlphaMode) > static_cast<uint8_t>(AlphaModeSetting::ForceOpaque))
        {
            return MaterialSettingsStatus::InvalidAlphaMode;
        }
        return ValidScale(layer.Emission) ? MaterialSettingsStatus::Success : MaterialSettingsStatus::InvalidNitsPerUnit;
    }
    MaterialSettingsStatus ResolveMaterialSettings(MaterialSourceProfile profile,
        const MaterialSettingsLayer& asset, const MaterialSettingsLayer& material,
        EmissiveScale assetSet, ResolvedMaterialSettings& out) noexcept
    {
        if (static_cast<uint8_t>(profile) > static_cast<uint8_t>(MaterialSourceProfile::AiGenerated))
        {
            return MaterialSettingsStatus::InvalidProfile;
        }
        for (const auto* layer : {&asset, &material})
        {
            const auto status = ValidateMaterialSettingsLayer(*layer);
            if (status != MaterialSettingsStatus::Success)
            {
                return status;
            }
        }
        if (!ValidScale(assetSet))
        {
            return MaterialSettingsStatus::InvalidNitsPerUnit;
        }
        ResolvedMaterialSettings result;
        result.Profile = profile;
        result.Arm = DefaultArmImportPolicy(profile == MaterialSourceProfile::AiGenerated);
        Apply(asset, result);
        Apply(material, result);
        result.Emission = SelectEmissiveScale(material.Emission, asset.Emission, assetSet);
        out = result;
        return MaterialSettingsStatus::Success;
    }
    CanonicalMaterialSettings EncodeCanonicalMaterialSettings(const ResolvedMaterialSettings& settings) noexcept
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        CanonicalMaterialSettings result;
        MaterialSettingsLayer layer;
        layer.ArmMask = 7;
        layer.Arm = settings.Arm;
        layer.Emission = settings.Emission;
        layer.DoubleSided = settings.DoubleSided;
        layer.AlphaMode = settings.AlphaMode;
        if (static_cast<uint8_t>(settings.Profile) > static_cast<uint8_t>(MaterialSourceProfile::AiGenerated) ||
            settings.DoubleSided == DoubleSidedSetting::Inherit || settings.AlphaMode == AlphaModeSetting::Inherit ||
            ValidateMaterialSettingsLayer(layer) != MaterialSettingsStatus::Success)
        {
            return result;
        }
        size_t cursor = 0;
        const auto append = [&](uint64_t value, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                result.Bytes[cursor++] = static_cast<uint8_t>(value >> (index * 8));
            }
        };
        const auto number = [&](double value)
        {
            append(std::bit_cast<uint64_t>(value == 0 ? 0.0 : value), 8);
        };
        append(MaterialSettingsCanonicalVersion, 4);
        append(static_cast<uint8_t>(settings.Profile), 1);
        for (const auto& channel : settings.Arm.Channels)
        {
            append(static_cast<uint8_t>(channel.Mode), 1);
            number(channel.Mode == ArmMode::Constant ? channel.Constant : 0);
            number(channel.Mode == ArmMode::Auto ? channel.AutoWidth : 0);
        }
        append(static_cast<uint8_t>(settings.DoubleSided), 1);
        append(static_cast<uint8_t>(settings.AlphaMode), 1);
        append(settings.Emission.Present ? 1 : 0, 1);
        number(settings.Emission.Present ? settings.Emission.NitsPerUnit : 0);
        result.Size = cursor;
        return result;
    }
    MaterialSettingsHashResult AppendMaterialSettingsHash(uint64_t hash, const ResolvedMaterialSettings& settings) noexcept
    {
        const auto canonical = EncodeCanonicalMaterialSettings(settings);
        if (canonical.Size == 0)
        {
            return {};
        }
        const auto append = [&](uint8_t value)
        {
            hash = (hash ^ value) * UINT64_C(1099511628211);
        };
        constexpr char domain[] = "NVMATERIALSETTINGS";
        for (size_t index = 0; index < sizeof(domain) - 1; ++index)
        {
            append(static_cast<uint8_t>(domain[index]));
        }
        for (size_t index = 0; index < 8; ++index)
        {
            append(static_cast<uint8_t>(static_cast<uint64_t>(canonical.Size) >> (index * 8)));
        }
        for (const auto byte : canonical.Bytes)
        {
            append(byte);
        }
        return {true, hash};
    }
    const char* MaterialSettingsErrorKey(MaterialSettingsStatus status) noexcept
    {
        switch (status)
        {
        case MaterialSettingsStatus::Success:
            return "";
        case MaterialSettingsStatus::InvalidProfile:
            return "material.profile";
        case MaterialSettingsStatus::InvalidArmMask:
        case MaterialSettingsStatus::InvalidArmPolicy:
            return "material.arm";
        case MaterialSettingsStatus::InvalidDoubleSided:
            return "material.doubleSided";
        case MaterialSettingsStatus::InvalidAlphaMode:
            return "material.alphaMode";
        case MaterialSettingsStatus::InvalidNitsPerUnit:
            return "emissiveNitsPerUnit";
        }
        return "material";
    }
} // namespace NorvesLib::Core::AssetImport
