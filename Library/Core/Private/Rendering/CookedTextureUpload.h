#pragma once

#include "Asset/CookedTextureFormat.h"
#include "Rendering/GpuResourceTypes.h"
#include "RHI/IDevice.h"

#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    enum class CookedTextureUploadStatus : uint8_t
    {
        Success,
        InvalidDevice,
        InvalidTexture,
        InvalidDimensions,
        UnsupportedFormat,
        InvalidMipData,
        IntegerOverflow,
        TextureCreationFailed,
        UploadFailed
    };

    struct CookedTextureUploadResult
    {
        CookedTextureUploadStatus Status = CookedTextureUploadStatus::InvalidTexture;
        TextureCreateInfo CreateInfo;
        RHI::TexturePtr Texture;
        size_t UploadedBytes = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CookedTextureUploadStatus::Success && Texture != nullptr;
        }
    };

    // bRequireSourceBlob が false のときは、ソースのバイト列を持たないメタデータだけの解析結果（範囲読みの
    // ReadCookedTextureLayout の結果）からも作成情報を作る。
    [[nodiscard]] CookedTextureUploadStatus BuildCookedTextureCreateInfo(
        const Asset::CookedTextureData &texture,
        const Container::String &debugName,
        TextureCreateInfo &outCreateInfo,
        bool bRequireSourceBlob = true);

    [[nodiscard]] CookedTextureUploadResult CreateAndUploadCookedTexture(
        RHI::IDevice *device,
        const Asset::CookedTextureData &texture,
        const Container::String &debugName = {});
}
