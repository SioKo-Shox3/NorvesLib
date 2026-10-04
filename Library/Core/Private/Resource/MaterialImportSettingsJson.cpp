#include "Resource/MaterialImportSettingsJson.h"
#include "Text/JsonDocument.h"
#include <cmath>

namespace NorvesLib::Core::AssetImport
{
    namespace
    {
        bool Equals(const Container::String& text, const char* word)
        {
            size_t index = 0;
            while (index < text.size() && word[index] != 0)
            {
                if (static_cast<uint32_t>(text[index]) != static_cast<uint8_t>(word[index]))
                {
                    return false;
                }
                ++index;
            }
            return index == text.size() && word[index] == 0;
        }
        template<size_t Count>
        SettingsResult CheckFields(const JsonValue& object, const char* const (&names)[Count])
        {
            if (!object.IsObject())
            {
                return SettingsResult::InvalidType;
            }
            bool seen[Count]{};
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                bool bKnown = false;
                for (size_t field = 0; field < Count; ++field)
                {
                    if (Equals(object.GetMemberName(index), names[field]))
                    {
                        if (seen[field])
                        {
                            return SettingsResult::DuplicateField;
                        }
                        seen[field] = true;
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    return SettingsResult::UnknownField;
                }
            }
            return SettingsResult::Success;
        }
        SettingsResult ReadArm(const JsonValue& arm, MaterialSettingsLayer& layer)
        {
            if (!arm.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"occlusion", "roughness", "metallic"};
            auto status = CheckFields(arm, fields);
            if (status != SettingsResult::Success)
            {
                return status;
            }
            for (size_t index = 0; index < 3; ++index)
            {
                const auto value = arm.FindMember(fields[index]);
                if (!value.IsValid())
                {
                    continue;
                }
                if (!value.IsString())
                {
                    return SettingsResult::InvalidType;
                }
                Container::AnsiString token;
                const auto& text = value.AsString();
                token.reserve(text.size());
                for (const auto unit : text)
                {
                    if (static_cast<uint32_t>(unit) > 127)
                    {
                        return SettingsResult::InvalidValue;
                    }
                    token.push_back(static_cast<char>(unit));
                }
                if (ParseArmModeToken({token.data(), token.size()}, layer.Arm.Channels[index]) != MaterialSettingsStatus::Success)
                {
                    return SettingsResult::InvalidValue;
                }
                layer.ArmMask |= static_cast<uint8_t>(1u << index);
            }
            return SettingsResult::Success;
        }
        SettingsResult ReadLayer(const JsonValue& block, MaterialSettingsLayer& layer)
        {
            auto status = ReadArm(block.FindMember("arm"), layer);
            if (status != SettingsResult::Success)
            {
                return status;
            }
            const auto sided = block.FindMember("doubleSided");
            if (sided.IsValid())
            {
                if (!sided.IsString())
                {
                    return SettingsResult::InvalidType;
                }
                const auto& text = sided.AsString();
                if (Equals(text, "auto"))
                {
                    layer.DoubleSided = DoubleSidedSetting::FromSource;
                }
                else if (Equals(text, "force_true"))
                {
                    layer.DoubleSided = DoubleSidedSetting::ForceTrue;
                }
                else if (Equals(text, "force_false"))
                {
                    layer.DoubleSided = DoubleSidedSetting::ForceFalse;
                }
                else
                {
                    return SettingsResult::InvalidValue;
                }
            }
            const auto alpha = block.FindMember("alphaMode");
            if (alpha.IsValid())
            {
                if (!alpha.IsString())
                {
                    return SettingsResult::InvalidType;
                }
                const auto& text = alpha.AsString();
                if (Equals(text, "from_source"))
                {
                    layer.AlphaMode = AlphaModeSetting::FromSource;
                }
                else if (Equals(text, "force_opaque"))
                {
                    layer.AlphaMode = AlphaModeSetting::ForceOpaque;
                }
                else
                {
                    return SettingsResult::InvalidValue;
                }
            }
            const auto nits = block.FindMember("emissiveNitsPerUnit");
            if (nits.IsValid())
            {
                if (!nits.IsNumber())
                {
                    return SettingsResult::InvalidType;
                }
                const double value = nits.AsNumber();
                if (!std::isfinite(value) || value <= 0)
                {
                    return SettingsResult::InvalidValue;
                }
                layer.Emission = {true, value};
            }
            return ValidateMaterialSettingsLayer(layer) == MaterialSettingsStatus::Success ?
                SettingsResult::Success : SettingsResult::InvalidValue;
        }
    } // namespace
    SettingsResult ParseAssetMaterialSettings(const JsonValue& block,
        MaterialSourceProfile& outProfile, MaterialSettingsLayer& outLayer)
    {
        MaterialSettingsLayer layer;
        auto profile = MaterialSourceProfile::Source;
        if (block.IsValid())
        {
            constexpr const char* fields[] = {"profile", "arm", "doubleSided", "alphaMode", "emissiveNitsPerUnit"};
            auto status = CheckFields(block, fields);
            if (status != SettingsResult::Success)
            {
                return status;
            }
            const auto source = block.FindMember("profile");
            if (source.IsValid())
            {
                if (!source.IsString())
                {
                    return SettingsResult::InvalidType;
                }
                if (Equals(source.AsString(), "ai_generated"))
                {
                    profile = MaterialSourceProfile::AiGenerated;
                }
                else if (!Equals(source.AsString(), "source"))
                {
                    return SettingsResult::InvalidValue;
                }
            }
            status = ReadLayer(block, layer);
            if (status != SettingsResult::Success)
            {
                return status;
            }
        }
        outProfile = profile;
        outLayer = layer;
        return SettingsResult::Success;
    }
    SettingsResult ParseMaterialSettingsLayer(const JsonValue& block, MaterialSettingsLayer& outLayer)
    {
        MaterialSettingsLayer layer;
        if (block.IsValid())
        {
            constexpr const char* fields[] = {"arm", "doubleSided", "alphaMode", "emissiveNitsPerUnit"};
            auto status = CheckFields(block, fields);
            if (status != SettingsResult::Success)
            {
                return status;
            }
            status = ReadLayer(block, layer);
            if (status != SettingsResult::Success)
            {
                return status;
            }
        }
        outLayer = layer;
        return SettingsResult::Success;
    }
} // namespace NorvesLib::Core::AssetImport
