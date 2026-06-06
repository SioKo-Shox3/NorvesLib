#include "CookedTextureUpload.h"

#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool MapCookedTextureFormat(
            Asset::CookedTexturePixelFormat pixelFormat,
            Asset::CookedTextureColorSpace colorSpace,
            TextureCreateInfo::Format &outTextureFormat,
            RHI::Format &outRHIFormat)
        {
            using Asset::CookedTextureColorSpace;
            using Asset::CookedTexturePixelFormat;

            switch (pixelFormat)
            {
            case CookedTexturePixelFormat::R8UNorm:
                if (colorSpace != CookedTextureColorSpace::Linear)
                {
                    return false;
                }
                outTextureFormat = TextureCreateInfo::Format::R8_UNORM;
                outRHIFormat = RHI::Format::R8_UNORM;
                return true;

            case CookedTexturePixelFormat::RG8UNorm:
                if (colorSpace != CookedTextureColorSpace::Linear)
                {
                    return false;
                }
                outTextureFormat = TextureCreateInfo::Format::RG8_UNORM;
                outRHIFormat = RHI::Format::R8G8_UNORM;
                return true;

            case CookedTexturePixelFormat::RGBA8UNorm:
                if (colorSpace == CookedTextureColorSpace::SRGB)
                {
                    outTextureFormat = TextureCreateInfo::Format::RGBA8_SRGB;
                    outRHIFormat = RHI::Format::R8G8B8A8_SRGB;
                    return true;
                }
                if (colorSpace == CookedTextureColorSpace::Linear)
                {
                    outTextureFormat = TextureCreateInfo::Format::RGBA8_UNORM;
                    outRHIFormat = RHI::Format::R8G8B8A8_UNORM;
                    return true;
                }
                return false;
            }

            return false;
        }

        bool MultiplyChecked(uint64_t left, uint64_t right, uint64_t &outValue)
        {
            if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
            {
                return false;
            }

            outValue = left * right;
            return true;
        }

        CookedTextureUploadStatus BuildRHITextureDesc(
            const Asset::CookedTextureData &texture,
            const TextureCreateInfo &createInfo,
            RHI::TextureDesc &outDesc)
        {
            RHI::Format rhiFormat = RHI::Format::UNKNOWN;
            TextureCreateInfo::Format textureFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            if (!MapCookedTextureFormat(texture.PixelFormat, texture.ColorSpace, textureFormat, rhiFormat))
            {
                return CookedTextureUploadStatus::UnsupportedFormat;
            }

            outDesc.Width = createInfo.Width;
            outDesc.Height = createInfo.Height;
            outDesc.Depth = 1;
            outDesc.MipLevels = createInfo.MipLevels;
            outDesc.ArraySize = createInfo.ArraySize;
            outDesc.TextureFormat = rhiFormat;
            outDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            outDesc.Dimension = RHI::TextureDimension::Texture2D;
            outDesc.IsCubemap = false;
            outDesc.DebugName = createInfo.DebugName.c_str();
            return CookedTextureUploadStatus::Success;
        }

        CookedTextureUploadStatus ValidateMipLayout(
            const Asset::CookedTextureData &texture,
            size_t bytesPerPixel,
            Container::VariableArray<uint32_t> &outRowPitches,
            Container::VariableArray<uint32_t> &outSlicePitches)
        {
            if (texture.Mips.size() != texture.MipCount)
            {
                return CookedTextureUploadStatus::InvalidMipData;
            }

            outRowPitches.clear();
            outSlicePitches.clear();
            outRowPitches.reserve(texture.MipCount);
            outSlicePitches.reserve(texture.MipCount);

            for (uint32_t mipIndex = 0; mipIndex < texture.MipCount; ++mipIndex)
            {
                const Asset::CookedTextureMip &mip = texture.Mips[mipIndex];
                if (mip.Width == 0 || mip.Height == 0)
                {
                    return CookedTextureUploadStatus::InvalidMipData;
                }

                uint64_t rowPitch64 = 0;
                uint64_t slicePitch64 = 0;
                uint64_t expectedMipBytes64 = 0;
                if (!MultiplyChecked(mip.Width, bytesPerPixel, rowPitch64) ||
                    !MultiplyChecked(rowPitch64, mip.Height, slicePitch64) ||
                    !MultiplyChecked(slicePitch64, texture.LayerCount, expectedMipBytes64))
                {
                    return CookedTextureUploadStatus::IntegerOverflow;
                }

                if (rowPitch64 > std::numeric_limits<uint32_t>::max() ||
                    slicePitch64 > std::numeric_limits<uint32_t>::max() ||
                    expectedMipBytes64 > std::numeric_limits<size_t>::max())
                {
                    return CookedTextureUploadStatus::IntegerOverflow;
                }

                const Container::Span<const uint8_t> mipBytes = texture.GetMipBytes(mipIndex);
                if (mipBytes.size() != static_cast<size_t>(expectedMipBytes64))
                {
                    return CookedTextureUploadStatus::InvalidMipData;
                }

                outRowPitches.push_back(static_cast<uint32_t>(rowPitch64));
                outSlicePitches.push_back(static_cast<uint32_t>(slicePitch64));
            }

            return CookedTextureUploadStatus::Success;
        }
    }

    CookedTextureUploadStatus BuildCookedTextureCreateInfo(
        const Asset::CookedTextureData &texture,
        const Container::String &debugName,
        TextureCreateInfo &outCreateInfo)
    {
        if (!texture.SourceBlob.IsValid())
        {
            return CookedTextureUploadStatus::InvalidTexture;
        }

        if (texture.Width == 0 || texture.Height == 0 || texture.LayerCount == 0 || texture.MipCount == 0)
        {
            return CookedTextureUploadStatus::InvalidDimensions;
        }

        TextureCreateInfo::Format textureFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        RHI::Format rhiFormat = RHI::Format::UNKNOWN;
        if (!MapCookedTextureFormat(texture.PixelFormat, texture.ColorSpace, textureFormat, rhiFormat))
        {
            return CookedTextureUploadStatus::UnsupportedFormat;
        }

        TextureCreateInfo createInfo;
        createInfo.Width = texture.Width;
        createInfo.Height = texture.Height;
        createInfo.Depth = 1;
        createInfo.MipLevels = texture.MipCount;
        createInfo.ArraySize = texture.LayerCount;
        createInfo.PixelFormat = textureFormat;
        createInfo.Type = texture.LayerCount > 1 ? TextureType::Texture2DArray : TextureType::Texture2D;
        createInfo.bRenderTarget = false;
        createInfo.bDepthStencil = false;
        createInfo.DebugName = debugName;

        outCreateInfo = std::move(createInfo);
        return CookedTextureUploadStatus::Success;
    }

    CookedTextureUploadResult CreateAndUploadCookedTexture(
        RHI::IDevice *device,
        const Asset::CookedTextureData &texture,
        const Container::String &debugName)
    {
        CookedTextureUploadResult result;
        if (device == nullptr)
        {
            result.Status = CookedTextureUploadStatus::InvalidDevice;
            return result;
        }

        result.Status = BuildCookedTextureCreateInfo(texture, debugName, result.CreateInfo);
        if (result.Status != CookedTextureUploadStatus::Success)
        {
            return result;
        }

        const size_t bytesPerPixel = Asset::GetCookedTextureBytesPerPixel(texture.PixelFormat);
        if (bytesPerPixel == 0)
        {
            result.Status = CookedTextureUploadStatus::UnsupportedFormat;
            return result;
        }

        Container::VariableArray<uint32_t> rowPitches;
        Container::VariableArray<uint32_t> slicePitches;
        result.Status = ValidateMipLayout(texture, bytesPerPixel, rowPitches, slicePitches);
        if (result.Status != CookedTextureUploadStatus::Success)
        {
            return result;
        }

        RHI::TextureDesc textureDesc;
        result.Status = BuildRHITextureDesc(texture, result.CreateInfo, textureDesc);
        if (result.Status != CookedTextureUploadStatus::Success)
        {
            return result;
        }

        RHI::TexturePtr rhiTexture = device->CreateTexture(textureDesc);
        if (!rhiTexture)
        {
            result.Status = CookedTextureUploadStatus::TextureCreationFailed;
            return result;
        }

        try
        {
            for (uint32_t mipIndex = 0; mipIndex < texture.MipCount; ++mipIndex)
            {
                const Container::Span<const uint8_t> mipBytes = texture.GetMipBytes(mipIndex);
                const uint32_t slicePitch = slicePitches[mipIndex];
                const uint8_t *mipData = mipBytes.data();

                for (uint32_t layerIndex = 0; layerIndex < texture.LayerCount; ++layerIndex)
                {
                    const uint8_t *layerData = mipData + static_cast<size_t>(slicePitch) * layerIndex;
                    rhiTexture->Update(layerData, rowPitches[mipIndex], slicePitch, mipIndex, layerIndex);
                    result.UploadedBytes += slicePitch;
                }
            }
        }
        catch (const std::exception &)
        {
            result.Texture.reset();
            result.Status = CookedTextureUploadStatus::UploadFailed;
            result.UploadedBytes = 0;
            return result;
        }
        catch (...)
        {
            result.Texture.reset();
            result.Status = CookedTextureUploadStatus::UploadFailed;
            result.UploadedBytes = 0;
            return result;
        }

        result.Texture = std::move(rhiTexture);
        result.Status = CookedTextureUploadStatus::Success;
        return result;
    }
}
