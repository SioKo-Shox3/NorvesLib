#include "RenderingValidation/RenderingGoldenImage.h"

#include "FileStream/FileStream.h"
#include "Rendering/FrameCaptureThumbnailEncoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include "stb_image.h"
#include "stb_image_write.h"

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        constexpr uint32_t RgbaChannelCount = 4u;
        constexpr uint32_t GoldenWidth = 256u;
        constexpr uint32_t GoldenHeight = 256u;

        struct ImageValidation
        {
            GoldenImageStatus Status = GoldenImageStatus::Success;
            uint64_t RequiredByteCount = 0;
        };

        struct PngWriteContext
        {
            Core::Container::VariableArray<uint8_t>* pBytes = nullptr;
            bool bAllocationFailed = false;
        };

        ImageValidation ValidateImage(const Rgba8Image& image)
        {
            ImageValidation validation;
            if (image.Width == 0u || image.Height == 0u ||
                image.Width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                image.Height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            {
                validation.Status = GoldenImageStatus::InvalidDimensions;
                return validation;
            }

            const uint64_t tightRowPitch = static_cast<uint64_t>(image.Width) * RgbaChannelCount;
            if (tightRowPitch > std::numeric_limits<uint32_t>::max())
            {
                validation.Status = GoldenImageStatus::InvalidDimensions;
                return validation;
            }
            if (image.RowPitchBytes < tightRowPitch ||
                image.RowPitchBytes > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            {
                validation.Status = GoldenImageStatus::InvalidPixelData;
                return validation;
            }

            validation.RequiredByteCount =
                static_cast<uint64_t>(image.RowPitchBytes) * static_cast<uint64_t>(image.Height);
            if (validation.RequiredByteCount > std::numeric_limits<size_t>::max() ||
                image.Pixels.size() < static_cast<size_t>(validation.RequiredByteCount))
            {
                validation.Status = GoldenImageStatus::InvalidPixelData;
            }
            return validation;
        }

        void AppendPngBytes(void* context, void* data, int byteCount) noexcept
        {
            PngWriteContext* writeContext = static_cast<PngWriteContext*>(context);
            if (writeContext == nullptr || writeContext->pBytes == nullptr || data == nullptr || byteCount <= 0 ||
                writeContext->bAllocationFailed)
            {
                return;
            }

            const size_t oldSize = writeContext->pBytes->size();
            const size_t appendedSize = static_cast<size_t>(byteCount);
            if (appendedSize > std::numeric_limits<size_t>::max() - oldSize)
            {
                writeContext->bAllocationFailed = true;
                return;
            }
            try
            {
                writeContext->pBytes->resize(oldSize + appendedSize);
            }
            catch (...)
            {
                writeContext->bAllocationFailed = true;
                return;
            }
            std::memcpy(writeContext->pBytes->data() + oldSize, data, appendedSize);
        }

        GoldenImageStatus ConvertThumbnailStatus(Core::Rendering::FrameCaptureThumbnailStatus status)
        {
            using Core::Rendering::FrameCaptureThumbnailStatus;
            switch (status)
            {
            case FrameCaptureThumbnailStatus::Success:
                return GoldenImageStatus::Success;
            case FrameCaptureThumbnailStatus::CapturedFrameNotReady:
                return GoldenImageStatus::CaptureNotSuccessful;
            case FrameCaptureThumbnailStatus::InvalidDimensions:
                return GoldenImageStatus::InvalidDimensions;
            case FrameCaptureThumbnailStatus::UnsupportedFormat:
                return GoldenImageStatus::UnsupportedFormat;
            case FrameCaptureThumbnailStatus::InvalidPixelData:
                return GoldenImageStatus::InvalidPixelData;
            default:
                return GoldenImageStatus::EncodeFailed;
            }
        }

        uint8_t AbsoluteChannelDelta(uint8_t left, uint8_t right)
        {
            return left >= right ? static_cast<uint8_t>(left - right) : static_cast<uint8_t>(right - left);
        }
    }

    GoldenImageStatus EncodeCapturedFramePng(
        const Core::Rendering::CapturedFrame& frame,
        Core::Container::VariableArray<uint8_t>& outPng)
    {
        outPng.clear();
        Core::Rendering::FrameCaptureThumbnailOptions options;
        options.MaxWidth = GoldenWidth;
        options.MaxHeight = GoldenHeight;
        options.MaxEncodedBytes = 1024u * 1024u;
        const Core::Rendering::FrameCaptureThumbnailResult encoded =
            Core::Rendering::EncodeCapturedFrameThumbnail(frame, options);
        if (!encoded.IsSuccess())
        {
            return ConvertThumbnailStatus(encoded.Status);
        }
        if (encoded.Width != GoldenWidth || encoded.Height != GoldenHeight)
        {
            return GoldenImageStatus::InvalidDimensions;
        }
        outPng = encoded.PngBytes;
        return GoldenImageStatus::Success;
    }

    GoldenImageStatus EncodeRgba8Png(
        const Rgba8Image& image,
        Core::Container::VariableArray<uint8_t>& outPng)
    {
        outPng.clear();
        const ImageValidation validation = ValidateImage(image);
        if (validation.Status != GoldenImageStatus::Success)
        {
            return validation.Status;
        }

        PngWriteContext context;
        context.pBytes = &outPng;
        const int writeResult = stbi_write_png_to_func(
            AppendPngBytes,
            &context,
            static_cast<int>(image.Width),
            static_cast<int>(image.Height),
            static_cast<int>(RgbaChannelCount),
            image.Pixels.data(),
            static_cast<int>(image.RowPitchBytes));
        if (writeResult == 0 || context.bAllocationFailed || outPng.empty())
        {
            outPng.clear();
            return GoldenImageStatus::EncodeFailed;
        }
        return GoldenImageStatus::Success;
    }

    GoldenImageStatus DecodePng(Core::Container::Span<const uint8_t> png, Rgba8Image& outImage)
    {
        outImage = {};
        if (png.empty() || png.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return GoldenImageStatus::DecodeFailed;
        }

        int width = 0;
        int height = 0;
        int sourceChannelCount = 0;
        stbi_uc* decodedPixels = stbi_load_from_memory(
            png.data(), static_cast<int>(png.size()), &width, &height, &sourceChannelCount,
            static_cast<int>(RgbaChannelCount));
        if (decodedPixels == nullptr)
        {
            return GoldenImageStatus::DecodeFailed;
        }

        if (width <= 0 || height <= 0 ||
            static_cast<uint64_t>(width) * RgbaChannelCount > std::numeric_limits<uint32_t>::max())
        {
            stbi_image_free(decodedPixels);
            return GoldenImageStatus::InvalidDimensions;
        }
        const uint64_t pixelByteCount =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * RgbaChannelCount;
        if (pixelByteCount > std::numeric_limits<size_t>::max())
        {
            stbi_image_free(decodedPixels);
            return GoldenImageStatus::InvalidDimensions;
        }

        try
        {
            outImage.Pixels.resize(static_cast<size_t>(pixelByteCount));
        }
        catch (const std::bad_alloc&)
        {
            stbi_image_free(decodedPixels);
            outImage = {};
            return GoldenImageStatus::DecodeFailed;
        }
        std::memcpy(outImage.Pixels.data(), decodedPixels, static_cast<size_t>(pixelByteCount));
        stbi_image_free(decodedPixels);
        outImage.Width = static_cast<uint32_t>(width);
        outImage.Height = static_cast<uint32_t>(height);
        outImage.RowPitchBytes = outImage.Width * RgbaChannelCount;
        return GoldenImageStatus::Success;
    }

    GoldenImageStatus LoadPng(const Core::Container::String& path, Rgba8Image& outImage)
    {
        outImage = {};
        FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
            path, FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (!stream)
        {
            return GoldenImageStatus::FileOpenFailed;
        }

        const int64_t fileSize = stream->GetSize();
        if (fileSize < 0 || static_cast<uint64_t>(fileSize) > std::numeric_limits<size_t>::max())
        {
            return GoldenImageStatus::FileOpenFailed;
        }
        Core::Container::VariableArray<uint8_t> png;
        try
        {
            png.resize(static_cast<size_t>(fileSize));
        }
        catch (const std::bad_alloc&)
        {
            return GoldenImageStatus::DecodeFailed;
        }
        if (!png.empty() && stream->Read(png.data(), png.size()) != png.size())
        {
            return GoldenImageStatus::FileOpenFailed;
        }
        return DecodePng(Core::Container::Span<const uint8_t>(png), outImage);
    }

    GoldenImageStatus SavePng(
        const Core::Container::String& path,
        Core::Container::Span<const uint8_t> png)
    {
        FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
            path, FileStream::FileMode::Write, FileStream::FileAccess::Write,
            FileStream::FileShare::None);
        if (!stream)
        {
            return GoldenImageStatus::FileOpenFailed;
        }
        if (!png.empty() && stream->Write(png.data(), png.size()) != png.size())
        {
            return GoldenImageStatus::FileWriteFailed;
        }
        stream->Flush();
        return GoldenImageStatus::Success;
    }

    GoldenImageStatus CompareRgba8(
        const Rgba8Image& reference,
        const Rgba8Image& candidate,
        RawImageDifferenceMetrics& outMetrics)
    {
        outMetrics = {};
        const ImageValidation referenceValidation = ValidateImage(reference);
        if (referenceValidation.Status != GoldenImageStatus::Success)
        {
            return referenceValidation.Status;
        }
        const ImageValidation candidateValidation = ValidateImage(candidate);
        if (candidateValidation.Status != GoldenImageStatus::Success)
        {
            return candidateValidation.Status;
        }
        if (reference.Width != candidate.Width || reference.Height != candidate.Height)
        {
            return GoldenImageStatus::InvalidDimensions;
        }

        uint64_t absoluteChannelDeltaSum = 0;
        for (uint32_t y = 0; y < reference.Height; ++y)
        {
            for (uint32_t x = 0; x < reference.Width; ++x)
            {
                const size_t referenceOffset = static_cast<size_t>(y) * reference.RowPitchBytes +
                                               static_cast<size_t>(x) * RgbaChannelCount;
                const size_t candidateOffset = static_cast<size_t>(y) * candidate.RowPitchBytes +
                                               static_cast<size_t>(x) * RgbaChannelCount;
                bool bPixelDiffers = false;
                for (uint32_t channel = 0; channel < RgbaChannelCount; ++channel)
                {
                    const uint8_t delta = AbsoluteChannelDelta(
                        reference.Pixels[referenceOffset + channel],
                        candidate.Pixels[candidateOffset + channel]);
                    absoluteChannelDeltaSum += delta;
                    bPixelDiffers = bPixelDiffers || delta != 0u;
                    if (delta > outMetrics.MaxChannelDelta)
                    {
                        outMetrics.MaxChannelDelta = delta;
                        outMetrics.MaxDifferenceX = x;
                        outMetrics.MaxDifferenceY = y;
                    }
                }
                if (bPixelDiffers)
                {
                    ++outMetrics.DifferingPixelCount;
                }
            }
        }

        const uint64_t channelCount =
            static_cast<uint64_t>(reference.Width) * reference.Height * RgbaChannelCount;
        outMetrics.MeanAbsoluteChannelDelta =
            static_cast<double>(absoluteChannelDeltaSum) / static_cast<double>(channelCount);
        return GoldenImageStatus::Success;
    }

    bool MeetsRawGoldenThresholds(
        const RawImageDifferenceMetrics& metrics,
        const RawGoldenThresholds& thresholds)
    {
        return metrics.DifferingPixelCount <= thresholds.MaximumDifferingPixelCount &&
               metrics.MaxChannelDelta <= thresholds.MaximumChannelDelta;
    }
} // namespace NorvesLib::Test::RenderingValidation
