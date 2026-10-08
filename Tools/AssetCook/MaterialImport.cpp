#include "MaterialImport.h"
#include <limits>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        bool ValidRange(const void* data, size_t bytes) noexcept
        {
            return bytes == 0 || (data != nullptr && bytes <= std::numeric_limits<uintptr_t>::max() - reinterpret_cast<uintptr_t>(data));
        }
        bool Overlap(const void* first, size_t firstBytes, const void* second, size_t secondBytes) noexcept
        {
            if (firstBytes == 0 || secondBytes == 0)
            {
                return false;
            }
            if (!ValidRange(first, firstBytes) || !ValidRange(second, secondBytes))
            {
                return true;
            }
            const auto a = reinterpret_cast<uintptr_t>(first);
            const auto b = reinterpret_cast<uintptr_t>(second);
            return a < b + secondBytes && b < a + firstBytes;
        }
        bool ValidImage(const ArmImageView& image) noexcept
        {
            if (!image.Present)
            {
                return image.Width == 0 && image.Height == 0 && image.Pixels.empty();
            }
            const uint64_t count = uint64_t(image.Width) * image.Height;
            return count != 0 && count <= std::numeric_limits<size_t>::max() / 4 &&
                image.Pixels.size() == static_cast<size_t>(count) * 4 && ValidRange(image.Pixels.data(), image.Pixels.size());
        }
        bool OverlapsInputs(const void* data, size_t bytes, const ArmImageInputs& images,
            const Core::AssetImport::ArmImportPolicy& policy, const double (&factors)[3]) noexcept
        {
            return Overlap(data, bytes, &images, sizeof(images)) || Overlap(data, bytes, &policy, sizeof(policy)) ||
                Overlap(data, bytes, factors, sizeof(factors)) ||
                Overlap(data, bytes, images.Occlusion.Pixels.data(), images.Occlusion.Pixels.size()) ||
                Overlap(data, bytes, images.MetallicRoughness.Pixels.data(), images.MetallicRoughness.Pixels.size());
        }
    } // namespace
    ArmImageStatus AnalyzeArmImages(const ArmImageInputs& images, const Core::AssetImport::ArmImportPolicy& policy,
        const double (&factors)[3], ArmImagePlan& out) noexcept
    {
        if (!ValidImage(images.Occlusion) || !ValidImage(images.MetallicRoughness))
        {
            return ArmImageStatus::InvalidImage;
        }
        if (OverlapsInputs(&out, sizeof(out), images, policy, factors))
        {
            return ArmImageStatus::OverlappingStorage;
        }
        uint64_t bins[3][256]{};
        if (images.Occlusion.Present)
        {
            for (size_t offset = 0; offset < images.Occlusion.Pixels.size(); offset += 4)
            {
                ++bins[0][images.Occlusion.Pixels[offset]];
            }
        }
        else
        {
            bins[0][255] = 1;
        }
        if (images.MetallicRoughness.Present)
        {
            for (size_t offset = 0; offset < images.MetallicRoughness.Pixels.size(); offset += 4)
            {
                ++bins[1][images.MetallicRoughness.Pixels[offset + 1]];
                ++bins[2][images.MetallicRoughness.Pixels[offset + 2]];
            }
        }
        else
        {
            bins[1][255] = bins[2][255] = 1;
        }
        ArmImagePlan result;
        result.HasSourceImage[0] = images.Occlusion.Present;
        result.HasSourceImage[1] = result.HasSourceImage[2] = images.MetallicRoughness.Present;
        for (size_t channel = 0; channel < 3; ++channel)
        {
            if (Core::AssetImport::AnalyzeArmHistogram(bins[channel], static_cast<Core::AssetImport::ArmChannel>(channel),
                policy.Channels[channel], factors[channel], result.Channels[channel]) != Core::AssetImport::MaterialPolicyStatus::Success)
            {
                return ArmImageStatus::InvalidPolicy;
            }
            // 存在しないtextureをtexture modeだけで新規生成せず、glTF既定sampleのスカラーへ戻す。
            result.Channels[channel].UseTexture = result.Channels[channel].UseTexture && result.HasSourceImage[channel];
            if (result.Channels[channel].UseTexture)
            {
                result.TextureMask |= static_cast<uint8_t>(1u << channel);
                const auto& image = channel == 0 ? images.Occlusion : images.MetallicRoughness;
                if (result.Width == 0)
                {
                    result.Width = image.Width;
                    result.Height = image.Height;
                    result.ByteCount = image.Pixels.size();
                }
                else if (result.Width != image.Width || result.Height != image.Height)
                {
                    return ArmImageStatus::IncompatibleDimensions;
                }
            }
        }
        out = result;
        return ArmImageStatus::Success;
    }
    ArmImageStatus BakeArmImages(const ArmImageInputs& images, const Core::AssetImport::ArmImportPolicy& policy,
        const double (&factors)[3], Core::Container::Span<uint8_t> pixels, ArmImagePlan& out) noexcept
    {
        if (!ValidRange(pixels.data(), pixels.size()))
        {
            return ArmImageStatus::InvalidOutput;
        }
        if (OverlapsInputs(pixels.data(), pixels.size(), images, policy, factors) ||
            OverlapsInputs(&out, sizeof(out), images, policy, factors) ||
            Overlap(pixels.data(), pixels.size(), &out, sizeof(out)))
        {
            return ArmImageStatus::OverlappingStorage;
        }
        ArmImagePlan result;
        const auto status = AnalyzeArmImages(images, policy, factors, result);
        if (status != ArmImageStatus::Success)
        {
            return status;
        }
        if (pixels.size() < result.ByteCount)
        {
            return ArmImageStatus::InvalidOutput;
        }
        uint8_t lookup[3][256]{};
        uint8_t constants[3]{};
        for (size_t channel = 0; channel < 3; ++channel)
        {
            constants[channel] = result.Channels[channel].QuantizedScalar;
            for (size_t value = 0; value < 256; ++value)
            {
                if (Core::AssetImport::BakeArmByte(static_cast<uint8_t>(value), static_cast<Core::AssetImport::ArmChannel>(channel),
                    factors[channel], lookup[channel][value]) != Core::AssetImport::MaterialPolicyStatus::Success)
                {
                    return ArmImageStatus::InvalidPolicy;
                }
            }
        }
        const auto& alphaSource = (result.TextureMask & 6) != 0 ? images.MetallicRoughness : images.Occlusion;
        for (size_t offset = 0; offset < result.ByteCount; offset += 4)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                if ((result.TextureMask & (1u << channel)) != 0)
                {
                    const auto& source = channel == 0 ? images.Occlusion : images.MetallicRoughness;
                    pixels[offset + channel] = lookup[channel][source.Pixels[offset + channel]];
                }
                else
                {
                    pixels[offset + channel] = constants[channel];
                }
            }
            pixels[offset + 3] = alphaSource.Pixels[offset + 3];
        }
        out = result;
        return ArmImageStatus::Success;
    }
} // namespace NorvesLib::Tools::AssetCook
