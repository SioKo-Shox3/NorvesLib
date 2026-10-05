#include "Resource/ImportedOpaqueRuntime.h"
#include "Asset/AssetPath.h"
#include <algorithm>
#include <limits>
#include <utility>
namespace NorvesLib::Core::ResourceIO::ModelStaging
{
    namespace
    {
        bool Fail(Container::AnsiString& reason, const char* field)
        {
            reason = "imported_opaque: ";
            reason += field;
            return false;
        }
        bool RuntimePath(const Container::AnsiString& text)
        {
            if (text.empty())
            {
                return true;
            }
            // 製品NVMESH readerのASCII契約を維持し、Windowsの別stream表記も受理しない。
            for (unsigned char value : text)
            {
                if (value < 0x20 || value > 0x7e || value == ':')
                {
                    return false;
                }
            }
            const auto path = Asset::AssetPath::Normalize(Container::AnsiStringView(text));
            return path.HasLogicalPath() && path.GetLogicalPath() == text;
        }
    } // namespace
    bool ValidateImportedOpaqueMaterial(const ImportedMaterialStaging& material, Container::AnsiString& reason)
    {
        reason.clear();
        if (material.Layout != ImportedMaterialLayout::PackedArmV1)
        {
            return Fail(reason, "layout");
        }
        if (material.Shading != Rendering::ShadingModel::DefaultLit)
        {
            return Fail(reason, "shading_model");
        }
        Asset::CookedMaterialRecord record;
        for (size_t i = 0; i < 4; ++i)
        {
            record.BaseColor[i] = material.BaseColor[i];
        }
        for (size_t i = 0; i < 3; ++i)
        {
            record.EmissiveColor[i] = material.EmissiveColor[i];
        }
        record.EmissiveNits = material.EmissiveLuminanceNits;
        record.Metallic = material.Metallic;
        record.Roughness = material.Roughness;
        record.OcclusionStrength = material.OcclusionStrength;
        record.NormalScale = material.NormalScale;
        record.AlphaCutoff = material.AlphaCutoff;
        record.Flags =
            (uint32_t(material.ArmMask) << 3) | (uint32_t(material.Alpha) << 1) | (material.bDoubleSided ? 1u : 0u);
        record.Arm.Length = material.ArmPath.empty() ? 0 : 1;
        if (Asset::ValidateCookedMaterialRecord(record, 1) != Asset::CookedMaterialStatus::Success)
        {
            return Fail(reason, "invalid_material_values");
        }
        if (!RuntimePath(material.AlbedoPath) || !RuntimePath(material.NormalPath) || !RuntimePath(material.ArmPath) ||
            !RuntimePath(material.EmissivePath))
        {
            return Fail(reason, "logical_path");
        }
        if (material.Alpha != ImportedAlphaMode::Opaque)
        {
            return Fail(reason, "alphaMode_requires_GR27");
        }
        if (material.bDoubleSided)
        {
            return Fail(reason, "doubleSided_requires_GR27");
        }
        if (material.NormalScale != 1)
        {
            return Fail(reason, "normalScale_requires_shader_support");
        }
        if (material.EmissiveLuminanceNits > 0 && !material.EmissivePath.empty())
        {
            return Fail(reason, "emissiveTexture_requires_shader_support");
        }
        // OPAQUEのfactor alphaは仕様どおり無視。nits0の発光画像も寄与0で、読み込まない。
        return true;
    }
    bool ValidateImportedTexture(const Asset::CookedTextureData& texture, Asset::CookedTextureColorSpace colorSpace,
                                 bool bRequireOpaqueAlpha, Container::AnsiString& reason)
    {
        reason.clear();
        if (colorSpace != Asset::CookedTextureColorSpace::Linear && colorSpace != Asset::CookedTextureColorSpace::SRGB)
        {
            return Fail(reason, "texture_color_space");
        }
        if (texture.LayerCount != 1 || texture.PixelFormat != Asset::CookedTexturePixelFormat::RGBA8UNorm ||
            texture.ColorSpace != colorSpace)
        {
            return Fail(reason, "texture_profile");
        }
        if (!texture.Width || !texture.Height || texture.Mips.empty() || texture.MipCount != texture.Mips.size())
        {
            return Fail(reason, "texture_mip_count");
        }
        uint32_t width = texture.Width, height = texture.Height;
        for (size_t i = 0; i < texture.Mips.size(); ++i)
        {
            const auto& mip = texture.Mips[i];
            const uint64_t count = uint64_t(width) * height;
            if (mip.Width != width || mip.Height != height ||
                (count > std::numeric_limits<size_t>::max() / 4 || count > UINT32_MAX / 4))
            {
                return Fail(reason, "texture_mip_dimensions");
            }
            const auto bytes = texture.GetMipBytes(i);
            if (bytes.size() != static_cast<size_t>(count) * 4)
            {
                return Fail(reason, "texture_mip_bytes");
            }
            if (bRequireOpaqueAlpha)
            {
                for (size_t pixel = 3; pixel < bytes.size(); pixel += 4)
                {
                    if (bytes[pixel] != 255)
                    {
                        return Fail(reason, "opaque_albedo_alpha_requires_shader_support");
                    }
                }
            }
            if (i + 1 < texture.Mips.size() && width == 1 && height == 1)
            {
                return Fail(reason, "texture_mip_count");
            }
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        if (texture.Mips.back().Width != 1 || texture.Mips.back().Height != 1)
        {
            return Fail(reason, "texture_incomplete_mip_chain");
        }
        return true;
    }
    bool SplitImportedArmChannels(const Asset::CookedTextureData& texture, uint8_t mask, ImportedArmChannels& out,
                                  Container::AnsiString& reason)
    {
        reason.clear();
        if (mask > 7)
        {
            return Fail(reason, "arm_mask");
        }
        ImportedArmChannels candidate;
        if (mask == 0)
        {
            out = std::move(candidate);
            return true;
        }
        if (!ValidateImportedTexture(texture, Asset::CookedTextureColorSpace::Linear, false, reason))
        {
            return false;
        }
        candidate.Mips.reserve(texture.Mips.size());
        for (size_t mipIndex = 0; mipIndex < texture.Mips.size(); ++mipIndex)
        {
            const auto& source = texture.Mips[mipIndex];
            ImportedArmMip mip;
            mip.Width = source.Width;
            mip.Height = source.Height;
            const size_t count = static_cast<size_t>(mip.Width) * mip.Height;
            const auto pixels = texture.GetMipBytes(mipIndex);
            for (size_t channel = 0; channel < 3; ++channel)
            {
                if ((mask & (1u << channel)) != 0)
                {
                    auto& destination = mip.Pixels[channel];
                    destination.resize(count);
                    for (size_t i = 0; i < count; ++i)
                    {
                        destination[i] = pixels[i * 4 + channel];
                    }
                }
            }
            candidate.Mips.push_back(std::move(mip));
        }
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::ResourceIO::ModelStaging
