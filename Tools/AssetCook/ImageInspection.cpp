#include "ImageInspection.h"
#include "Resource/GltfImageSource.h"
#include "stb_image.h"
#include <algorithm>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        struct DecodedImage
        {
            void* Data = nullptr;
            ~DecodedImage() { stbi_image_free(Data); }
            DecodedImage() = default;
            DecodedImage(const DecodedImage&) = delete;
            DecodedImage& operator=(const DecodedImage&) = delete;
        };
        template<typename T>
        void InspectChannels(const T* pixels, size_t count, ImageInspection& result) noexcept
        {
            for (uint32_t channel=0;channel<result.Channels;++channel)
            {
                uint32_t minimum=std::numeric_limits<T>::max(),maximum=0;
                uint64_t sum=0;
                for (size_t pixel=0;pixel<count;++pixel)
                {
                    const auto value=static_cast<uint32_t>(pixels[pixel*result.Channels+channel]);
                    minimum=std::min(minimum,value);
                    maximum=std::max(maximum,value);
                    sum+=value;
                }
                result.Channel[channel]={minimum,maximum,static_cast<double>(sum)/static_cast<double>(count)};
            }
        }
    }
    ImageInspectionStatus InspectImage(Core::Container::Span<const uint8_t> encoded,
        ImageInspection& outInspection) noexcept
    {
        if (!encoded.data() || encoded.empty() || encoded.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return ImageInspectionStatus::InvalidInput;
        }
        const auto source=reinterpret_cast<uintptr_t>(encoded.data());
        const auto target=reinterpret_cast<uintptr_t>(&outInspection);
        if (source<=target ? target-source<encoded.size() : source-target<sizeof(outInspection))
        {
            return ImageInspectionStatus::InvalidInput;
        }
        const auto mime=Core::Gltf::ProbeEmbeddedImageMime(encoded);
        if (mime!=Core::Gltf::DataUriMime::Png && mime!=Core::Gltf::DataUriMime::Jpeg)
        {
            return ImageInspectionStatus::UnsupportedFormat;
        }
        int width=0,height=0,channels=0;
        const int sourceSize=static_cast<int>(encoded.size());
        if (!stbi_info_from_memory(encoded.data(),sourceSize,&width,&height,&channels))
        {
            return ImageInspectionStatus::DecodeFailed;
        }
        if (width<=0 || height<=0 || channels<1 || channels>4)
        {
            return ImageInspectionStatus::InvalidDimensions;
        }
        const bool b16=stbi_is_16_bit_from_memory(encoded.data(),sourceSize)!=0;
        const uint64_t pixels=static_cast<uint64_t>(width)*static_cast<uint64_t>(height);
        const uint64_t bytesPerPixel=static_cast<uint64_t>(channels)*(b16 ? 2u : 1u);
        if (pixels>MaximumInspectionDecodedBytes/bytesPerPixel)
        {
            return ImageInspectionStatus::DecodedSizeLimit;
        }
        DecodedImage decoded;
        int decodedWidth=0,decodedHeight=0,decodedChannels=0;
        if (b16)
        {
            decoded.Data=stbi_load_16_from_memory(encoded.data(),sourceSize,&decodedWidth,&decodedHeight,&decodedChannels,0);
        }
        else
        {
            decoded.Data=stbi_load_from_memory(encoded.data(),sourceSize,&decodedWidth,&decodedHeight,&decodedChannels,0);
        }
        if (!decoded.Data || decodedWidth!=width || decodedHeight!=height || decodedChannels!=channels)
        {
            return ImageInspectionStatus::DecodeFailed;
        }
        ImageInspection result;
        result.Width=static_cast<uint32_t>(width);
        result.Height=static_cast<uint32_t>(height);
        result.Channels=static_cast<uint32_t>(channels);
        result.BitsPerChannel=b16 ? 16 : 8;
        if (b16)
        {
            InspectChannels(static_cast<const stbi_us*>(decoded.Data),static_cast<size_t>(pixels),result);
        }
        else
        {
            InspectChannels(static_cast<const stbi_uc*>(decoded.Data),static_cast<size_t>(pixels),result);
        }
        outInspection=result;
        return ImageInspectionStatus::Success;
    }
} // namespace NorvesLib::Tools::AssetCook
