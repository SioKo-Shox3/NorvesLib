#include "Resource/GltfMaterialSource.h"
#include <cmath>
namespace NorvesLib::Core::Gltf
{
    const char* MaterialTextureField(MaterialTextureRole role) noexcept
    {
        switch (role)
        {
        case MaterialTextureRole::BaseColor:
            return "pbrMetallicRoughness.baseColorTexture";
        case MaterialTextureRole::Normal:
            return "normalTexture";
        case MaterialTextureRole::MetallicRoughness:
            return "pbrMetallicRoughness.metallicRoughnessTexture";
        case MaterialTextureRole::Occlusion:
            return "occlusionTexture";
        case MaterialTextureRole::Emissive:
            return "emissiveTexture";
        case MaterialTextureRole::Count:
            break;
        }
        return "textureInfo";
    }
    MaterialReadResult ValidateMaterialSource(const MaterialSource& material, uint32_t textureCount) noexcept
    {
        const auto unit = [](double value)
        {
            return std::isfinite(value) && value >= 0 && value <= 1;
        };
        for (const double value : material.BaseColor)
        {
            if (!unit(value))
            {
                return {MaterialReadStatus::InvalidValue,"pbrMetallicRoughness.baseColorFactor"};
            }
        }
        if (!unit(material.Metallic))
        {
            return {MaterialReadStatus::InvalidValue,"pbrMetallicRoughness.metallicFactor"};
        }
        if (!unit(material.Roughness))
        {
            return {MaterialReadStatus::InvalidValue,"pbrMetallicRoughness.roughnessFactor"};
        }
        for (const double value : material.EmissiveFactor)
        {
            if (!unit(value))
            {
                return {MaterialReadStatus::InvalidValue,"emissiveFactor"};
            }
        }
        if (!std::isfinite(material.EmissiveStrength) || material.EmissiveStrength < 0)
        {
            return {MaterialReadStatus::InvalidValue,"extensions.KHR_materials_emissive_strength.emissiveStrength"};
        }
        if (!std::isfinite(material.NormalScale))
        {
            return {MaterialReadStatus::InvalidValue,"normalTexture.scale"};
        }
        if (!unit(material.OcclusionStrength))
        {
            return {MaterialReadStatus::InvalidValue,"occlusionTexture.strength"};
        }
        if (!std::isfinite(material.AlphaCutoff) || material.AlphaCutoff < 0)
        {
            return {MaterialReadStatus::InvalidValue,"alphaCutoff"};
        }
        if (static_cast<uint8_t>(material.AlphaMode) > static_cast<uint8_t>(MaterialAlphaMode::Blend))
        {
            return {MaterialReadStatus::UnsupportedAlphaMode,"alphaMode"};
        }
        for (size_t index = 0; index < static_cast<size_t>(MaterialTextureRole::Count); ++index)
        {
            if (material.Textures[index].Present && material.Textures[index].Index >= textureCount)
            {
                return {MaterialReadStatus::InvalidTextureIndex,MaterialTextureField(static_cast<MaterialTextureRole>(index))};
            }
        }
        return {};
    }
} // namespace NorvesLib::Core::Gltf
