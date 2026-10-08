#include "Resource/GltfMaterialSource.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <initializer_list>

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        bool Equals(const Container::String& text, const char* word)
        {
            size_t index=0;
            while (index<text.size() && word[index]!=0)
            {
                if (static_cast<uint32_t>(text[index])!=static_cast<uint8_t>(word[index]))
                {
                    return false;
                }
                ++index;
            }
            return index==text.size() && word[index]==0;
        }
        template<size_t Count>
        MaterialReadResult CheckMembers(const JsonValue& object, const char* const (&fields)[Count], const char* parent)
        {
            if (!object.IsObject())
            {
                return {MaterialReadStatus::InvalidType,parent};
            }
            bool seen[Count]{};
            for (size_t index=0;index<object.GetObjectSize();++index)
            {
                const auto& name=object.GetMemberName(index);
                for (const auto unit:name)
                {
                    if (unit==0)
                    {
                        return {MaterialReadStatus::InvalidKey,parent};
                    }
                }
                for (size_t field=0;field<Count;++field)
                {
                    if (Equals(name,fields[field]))
                    {
                        if (seen[field])
                        {
                            return {MaterialReadStatus::DuplicateField,fields[field]};
                        }
                        seen[field]=true;
                        break;
                    }
                }
            }
            return {};
        }
        MaterialReadResult Number(const JsonValue& value, double& out, const char* field)
        {
            if (!value.IsValid())
            {
                return {};
            }
            if (!value.IsNumber())
            {
                return {MaterialReadStatus::InvalidType,field};
            }
            const double number=value.AsNumber();
            if (!std::isfinite(number))
            {
                return {MaterialReadStatus::InvalidValue,field};
            }
            out=number;
            return {};
        }
        template<size_t Count>
        MaterialReadResult Color(const JsonValue& value, double (&out)[Count], const char* field)
        {
            if (!value.IsValid())
            {
                return {};
            }
            if (!value.IsArray())
            {
                return {MaterialReadStatus::InvalidType,field};
            }
            if (value.GetArraySize()!=Count)
            {
                return {MaterialReadStatus::InvalidValue,field};
            }
            for (size_t index=0;index<Count;++index)
            {
                const auto status=Number(value.GetArrayElement(index),out[index],field);
                if (!status.Succeeded())
                {
                    return status;
                }
            }
            return {};
        }
        MaterialReadResult Texture(const JsonValue& info, MaterialTextureRole role, MaterialSource& out)
        {
            if (!info.IsValid())
            {
                return {};
            }
            const char* field=MaterialTextureField(role);
            constexpr const char* fields[]={"index","texCoord","extensions","scale","strength"};
            auto status=CheckMembers(info,fields,field);
            if (!status.Succeeded())
            {
                return status;
            }
            const auto index=info.FindMember("index");
            if (!index.IsNumber() || !std::isfinite(index.AsNumber()) || index.AsNumber()<0 ||
                index.AsNumber()>UINT32_MAX || std::floor(index.AsNumber())!=index.AsNumber())
            {
                return {MaterialReadStatus::InvalidTextureIndex,field};
            }
            const auto coord=info.FindMember("texCoord");
            if (coord.IsValid())
            {
                if (!coord.IsNumber() || !std::isfinite(coord.AsNumber()) || coord.AsNumber()<0 ||
                    std::floor(coord.AsNumber())!=coord.AsNumber())
                {
                    return {MaterialReadStatus::InvalidValue,field};
                }
                if (coord.AsNumber()!=0)
                {
                    return {MaterialReadStatus::UnsupportedTexCoord,field};
                }
            }
            if (info.FindMember("extensions").IsValid())
            {
                return {MaterialReadStatus::UnsupportedTextureExtension,field};
            }
            out.Textures[static_cast<size_t>(role)]={true,static_cast<uint32_t>(index.AsNumber())};
            if (role==MaterialTextureRole::Normal)
            {
                return Number(info.FindMember("scale"),out.NormalScale,"normalTexture.scale");
            }
            if (role==MaterialTextureRole::Occlusion)
            {
                return Number(info.FindMember("strength"),out.OcclusionStrength,"occlusionTexture.strength");
            }
            return {};
        }
        MaterialReadResult EmissionExtension(const JsonValue& extensions, MaterialSource& out)
        {
            if (!extensions.IsValid())
            {
                return {};
            }
            constexpr const char* fields[]={"KHR_materials_emissive_strength"};
            auto status=CheckMembers(extensions,fields,"extensions");
            if (!status.Succeeded())
            {
                return status;
            }
            for (size_t index=0;index<extensions.GetObjectSize();++index)
            {
                if (!extensions.GetMemberValue(index).IsObject())
                {
                    return {MaterialReadStatus::InvalidType,"extensions"};
                }
            }
            const auto emission=extensions.FindMember("KHR_materials_emissive_strength");
            if (!emission.IsValid())
            {
                return {};
            }
            constexpr const char* emissionFields[]={"emissiveStrength"};
            status=CheckMembers(emission,emissionFields,"extensions.KHR_materials_emissive_strength");
            if (!status.Succeeded())
            {
                return status;
            }
            return Number(emission.FindMember("emissiveStrength"),out.EmissiveStrength,
                "extensions.KHR_materials_emissive_strength.emissiveStrength");
        }
    } // namespace
    MaterialReadResult ReadMaterialSource(const JsonValue& material, uint32_t textureCount, MaterialSource& out)
    {
        MaterialSource candidate;
        if (!material.IsValid())
        {
            out=candidate;
            return {};
        }
        constexpr const char* fields[]={"name","pbrMetallicRoughness","normalTexture","occlusionTexture",
            "emissiveTexture","emissiveFactor","alphaMode","alphaCutoff","doubleSided","extensions"};
        auto status=CheckMembers(material,fields,"material");
        if (!status.Succeeded())
        {
            return status;
        }
        const auto name=material.FindMember("name");
        if (name.IsValid())
        {
            if (!name.IsString())
            {
                return {MaterialReadStatus::InvalidType,"name"};
            }
            for (const auto unit:name.AsString())
            {
                if (unit==0)
                {
                    return {MaterialReadStatus::InvalidValue,"name"};
                }
            }
        }
        const auto pbr=material.FindMember("pbrMetallicRoughness");
        if (pbr.IsValid())
        {
            constexpr const char* pbrFields[]={"baseColorFactor","baseColorTexture","metallicFactor",
                "roughnessFactor","metallicRoughnessTexture"};
            status=CheckMembers(pbr,pbrFields,"pbrMetallicRoughness");
            if (!status.Succeeded())
            {
                return status;
            }
            status=Color(pbr.FindMember("baseColorFactor"),candidate.BaseColor,"pbrMetallicRoughness.baseColorFactor");
            if (!status.Succeeded())
            {
                return status;
            }
            status=Number(pbr.FindMember("metallicFactor"),candidate.Metallic,"pbrMetallicRoughness.metallicFactor");
            if (!status.Succeeded())
            {
                return status;
            }
            status=Number(pbr.FindMember("roughnessFactor"),candidate.Roughness,"pbrMetallicRoughness.roughnessFactor");
            if (!status.Succeeded())
            {
                return status;
            }
            status=Texture(pbr.FindMember("baseColorTexture"),MaterialTextureRole::BaseColor,candidate);
            if (!status.Succeeded())
            {
                return status;
            }
            status=Texture(pbr.FindMember("metallicRoughnessTexture"),MaterialTextureRole::MetallicRoughness,candidate);
            if (!status.Succeeded())
            {
                return status;
            }
        }
        for (const auto role:{MaterialTextureRole::Normal,MaterialTextureRole::Occlusion,MaterialTextureRole::Emissive})
        {
            status=Texture(material.FindMember(MaterialTextureField(role)),role,candidate);
            if (!status.Succeeded())
            {
                return status;
            }
        }
        status=Color(material.FindMember("emissiveFactor"),candidate.EmissiveFactor,"emissiveFactor");
        if (!status.Succeeded())
        {
            return status;
        }
        status=EmissionExtension(material.FindMember("extensions"),candidate);
        if (!status.Succeeded())
        {
            return status;
        }
        status=Number(material.FindMember("alphaCutoff"),candidate.AlphaCutoff,"alphaCutoff");
        if (!status.Succeeded())
        {
            return status;
        }
        const auto alpha=material.FindMember("alphaMode");
        if (alpha.IsValid())
        {
            if (!alpha.IsString())
            {
                return {MaterialReadStatus::InvalidType,"alphaMode"};
            }
            if (Equals(alpha.AsString(),"MASK"))
            {
                candidate.AlphaMode=MaterialAlphaMode::Mask;
            }
            else if (Equals(alpha.AsString(),"BLEND"))
            {
                candidate.AlphaMode=MaterialAlphaMode::Blend;
            }
            else if (!Equals(alpha.AsString(),"OPAQUE"))
            {
                return {MaterialReadStatus::UnsupportedAlphaMode,"alphaMode"};
            }
        }
        const auto sided=material.FindMember("doubleSided");
        if (sided.IsValid())
        {
            if (!sided.IsBoolean())
            {
                return {MaterialReadStatus::InvalidType,"doubleSided"};
            }
            candidate.DoubleSided=sided.AsBool();
        }
        status=ValidateMaterialSource(candidate,textureCount);
        if (status.Succeeded())
        {
            out=candidate;
        }
        return status;
    }
} // namespace NorvesLib::Core::Gltf
