#include "Rendering/FrameCaptureThumbnailEncoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "stb_image_write.h"

namespace NorvesLib::Core::Rendering
{
namespace
{
    using Container::VariableArray;

    constexpr uint32_t ThumbnailBytesPerPixel = 4;

    struct ThumbnailDimensions
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    struct PixelDataValidation
    {
        FrameCaptureThumbnailStatus Status = FrameCaptureThumbnailStatus::Success;
        uint32_t TightRowPitchBytes = 0;
        size_t RequiredByteCount = 0;
    };

    struct PngWriteContext
    {
        VariableArray<uint8_t>* Bytes = nullptr;
        size_t RetainedByteLimit = 0;
        bool bAllocationFailed = false;
        bool bTruncated = false;
    };

    void CopyFrameMetadata(const CapturedFrame& frame, FrameCaptureThumbnailResult& result)
    {
        result.RequestId = frame.RequestId;
        result.FrameNumber = frame.FrameNumber;
        result.SourceFormat = frame.Format;
    }

    [[nodiscard]] bool IsSupportedFormat(RHI::Format format)
    {
        switch (format)
        {
        case RHI::Format::R8G8B8A8_UNORM:
        case RHI::Format::R8G8B8A8_SRGB:
        case RHI::Format::B8G8R8A8_UNORM:
        case RHI::Format::B8G8R8A8_SRGB:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsBgraFormat(RHI::Format format)
    {
        return format == RHI::Format::B8G8R8A8_UNORM || format == RHI::Format::B8G8R8A8_SRGB;
    }

    [[nodiscard]] PixelDataValidation ValidatePixelData(const CapturedFrame& frame)
    {
        PixelDataValidation validation;
        if (frame.Width == 0 || frame.Height == 0)
        {
            validation.Status = FrameCaptureThumbnailStatus::InvalidDimensions;
            return validation;
        }
        if (frame.BytesPerPixel != ThumbnailBytesPerPixel)
        {
            validation.Status = FrameCaptureThumbnailStatus::UnsupportedFormat;
            return validation;
        }
        if (!IsSupportedFormat(frame.Format))
        {
            validation.Status = FrameCaptureThumbnailStatus::UnsupportedFormat;
            return validation;
        }

        const uint64_t tightRowPitchBytes = static_cast<uint64_t>(frame.Width) * ThumbnailBytesPerPixel;
        if (tightRowPitchBytes > std::numeric_limits<uint32_t>::max())
        {
            validation.Status = FrameCaptureThumbnailStatus::InvalidDimensions;
            return validation;
        }
        if (frame.RowPitchBytes < tightRowPitchBytes)
        {
            validation.Status = FrameCaptureThumbnailStatus::InvalidPixelData;
            return validation;
        }

        const uint64_t requiredByteCount = static_cast<uint64_t>(frame.RowPitchBytes) * static_cast<uint64_t>(frame.Height);
        if (requiredByteCount > std::numeric_limits<size_t>::max())
        {
            validation.Status = FrameCaptureThumbnailStatus::InvalidPixelData;
            return validation;
        }
        if (frame.Pixels.size() < static_cast<size_t>(requiredByteCount))
        {
            validation.Status = FrameCaptureThumbnailStatus::InvalidPixelData;
            return validation;
        }

        validation.TightRowPitchBytes = static_cast<uint32_t>(tightRowPitchBytes);
        validation.RequiredByteCount = static_cast<size_t>(requiredByteCount);
        return validation;
    }

    [[nodiscard]] ThumbnailDimensions CalculateInitialDimensions(const CapturedFrame& frame, const FrameCaptureThumbnailOptions& options)
    {
        const uint32_t maxWidth = std::min(options.MaxWidth, FrameCaptureThumbnailHardMaxWidth);
        const uint32_t maxHeight = std::min(options.MaxHeight, FrameCaptureThumbnailHardMaxHeight);
        if (frame.Width <= maxWidth && frame.Height <= maxHeight)
        {
            return { frame.Width, frame.Height };
        }

        const uint64_t widthLimitedHeight = static_cast<uint64_t>(frame.Height) * static_cast<uint64_t>(maxWidth);
        const uint64_t heightLimitedWidth = static_cast<uint64_t>(frame.Width) * static_cast<uint64_t>(maxHeight);
        if (widthLimitedHeight <= heightLimitedWidth)
        {
            const uint32_t height = static_cast<uint32_t>(std::max<uint64_t>(1u, widthLimitedHeight / frame.Width));
            return { maxWidth, height };
        }

        const uint32_t width = static_cast<uint32_t>(std::max<uint64_t>(1u, heightLimitedWidth / frame.Height));
        return { width, maxHeight };
    }

    [[nodiscard]] ThumbnailDimensions ReduceDimensions(ThumbnailDimensions dimensions)
    {
        ThumbnailDimensions reduced = dimensions;
        if (reduced.Width > 1u)
        {
            const uint32_t delta = std::max(1u, reduced.Width / 4u);
            reduced.Width = std::max(1u, reduced.Width - delta);
        }
        if (reduced.Height > 1u)
        {
            const uint32_t delta = std::max(1u, reduced.Height / 4u);
            reduced.Height = std::max(1u, reduced.Height - delta);
        }

        const uint64_t oldPixelCount = static_cast<uint64_t>(dimensions.Width) * static_cast<uint64_t>(dimensions.Height);
        const uint64_t newPixelCount = static_cast<uint64_t>(reduced.Width) * static_cast<uint64_t>(reduced.Height);
        if (newPixelCount < oldPixelCount)
        {
            return reduced;
        }

        if (reduced.Width >= reduced.Height && reduced.Width > 1u)
        {
            --reduced.Width;
        }
        else if (reduced.Height > 1u)
        {
            --reduced.Height;
        }

        return reduced;
    }

    void ReadSourcePixel(const CapturedFrame& frame, uint32_t x, uint32_t y, uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA)
    {
        const size_t sourceOffset = static_cast<size_t>(y) * static_cast<size_t>(frame.RowPitchBytes)
                                  + static_cast<size_t>(x) * ThumbnailBytesPerPixel;
        if (IsBgraFormat(frame.Format))
        {
            outB = frame.Pixels[sourceOffset + 0u];
            outG = frame.Pixels[sourceOffset + 1u];
            outR = frame.Pixels[sourceOffset + 2u];
            outA = frame.Pixels[sourceOffset + 3u];
            return;
        }

        outR = frame.Pixels[sourceOffset + 0u];
        outG = frame.Pixels[sourceOffset + 1u];
        outB = frame.Pixels[sourceOffset + 2u];
        outA = frame.Pixels[sourceOffset + 3u];
    }

    void WriteTargetPixel(
        VariableArray<uint8_t>& targetPixels,
        uint32_t targetWidth,
        uint32_t x,
        uint32_t y,
        uint8_t r,
        uint8_t g,
        uint8_t b,
        uint8_t a)
    {
        const size_t targetOffset = (static_cast<size_t>(y) * static_cast<size_t>(targetWidth) + static_cast<size_t>(x))
                                  * ThumbnailBytesPerPixel;
        targetPixels[targetOffset + 0u] = r;
        targetPixels[targetOffset + 1u] = g;
        targetPixels[targetOffset + 2u] = b;
        targetPixels[targetOffset + 3u] = a;
    }

    void CopyTightRgba(const CapturedFrame& frame, ThumbnailDimensions dimensions, VariableArray<uint8_t>& targetPixels)
    {
        for (uint32_t y = 0; y < dimensions.Height; ++y)
        {
            for (uint32_t x = 0; x < dimensions.Width; ++x)
            {
                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;
                uint8_t a = 0;
                ReadSourcePixel(frame, x, y, r, g, b, a);
                WriteTargetPixel(targetPixels, dimensions.Width, x, y, r, g, b, a);
            }
        }
    }

    void BoxAverageRgba(const CapturedFrame& frame, ThumbnailDimensions dimensions, VariableArray<uint8_t>& targetPixels)
    {
        for (uint32_t y = 0; y < dimensions.Height; ++y)
        {
            const uint64_t sourceYBegin = static_cast<uint64_t>(y) * frame.Height;
            const uint64_t sourceYEnd = static_cast<uint64_t>(y + 1u) * frame.Height;
            const uint32_t firstSourceY = static_cast<uint32_t>(sourceYBegin / dimensions.Height);
            const uint32_t lastSourceY = static_cast<uint32_t>((sourceYEnd - 1u) / dimensions.Height);

            for (uint32_t x = 0; x < dimensions.Width; ++x)
            {
                const uint64_t sourceXBegin = static_cast<uint64_t>(x) * frame.Width;
                const uint64_t sourceXEnd = static_cast<uint64_t>(x + 1u) * frame.Width;
                const uint32_t firstSourceX = static_cast<uint32_t>(sourceXBegin / dimensions.Width);
                const uint32_t lastSourceX = static_cast<uint32_t>((sourceXEnd - 1u) / dimensions.Width);

                uint64_t rSum = 0;
                uint64_t gSum = 0;
                uint64_t bSum = 0;
                uint64_t aSum = 0;
                uint64_t weightSum = 0;

                for (uint32_t sourceY = firstSourceY; sourceY <= lastSourceY; ++sourceY)
                {
                    const uint64_t pixelYBegin = static_cast<uint64_t>(sourceY) * dimensions.Height;
                    const uint64_t pixelYEnd = static_cast<uint64_t>(sourceY + 1u) * dimensions.Height;
                    const uint64_t overlapY = std::min(sourceYEnd, pixelYEnd) - std::max(sourceYBegin, pixelYBegin);
                    for (uint32_t sourceX = firstSourceX; sourceX <= lastSourceX; ++sourceX)
                    {
                        const uint64_t pixelXBegin = static_cast<uint64_t>(sourceX) * dimensions.Width;
                        const uint64_t pixelXEnd = static_cast<uint64_t>(sourceX + 1u) * dimensions.Width;
                        const uint64_t overlapX = std::min(sourceXEnd, pixelXEnd) - std::max(sourceXBegin, pixelXBegin);
                        const uint64_t weight = overlapX * overlapY;

                        uint8_t r = 0;
                        uint8_t g = 0;
                        uint8_t b = 0;
                        uint8_t a = 0;
                        ReadSourcePixel(frame, sourceX, sourceY, r, g, b, a);

                        rSum += static_cast<uint64_t>(r) * weight;
                        gSum += static_cast<uint64_t>(g) * weight;
                        bSum += static_cast<uint64_t>(b) * weight;
                        aSum += static_cast<uint64_t>(a) * weight;
                        weightSum += weight;
                    }
                }

                WriteTargetPixel(
                    targetPixels,
                    dimensions.Width,
                    x,
                    y,
                    static_cast<uint8_t>(rSum / weightSum),
                    static_cast<uint8_t>(gSum / weightSum),
                    static_cast<uint8_t>(bSum / weightSum),
                    static_cast<uint8_t>(aSum / weightSum));
            }
        }
    }

    [[nodiscard]] bool BuildTargetPixels(const CapturedFrame& frame, ThumbnailDimensions dimensions, VariableArray<uint8_t>& targetPixels)
    {
        const uint64_t targetByteCount = static_cast<uint64_t>(dimensions.Width)
                                       * static_cast<uint64_t>(dimensions.Height)
                                       * ThumbnailBytesPerPixel;
        if (targetByteCount > std::numeric_limits<size_t>::max())
        {
            return false;
        }

        try
        {
            targetPixels.clear();
            targetPixels.resize(static_cast<size_t>(targetByteCount));
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }

        if (dimensions.Width == frame.Width && dimensions.Height == frame.Height)
        {
            CopyTightRgba(frame, dimensions, targetPixels);
            return true;
        }

        BoxAverageRgba(frame, dimensions, targetPixels);
        return true;
    }

    void StbiPngWriteCallback(void* context, void* data, int size) noexcept
    {
        PngWriteContext* writeContext = static_cast<PngWriteContext*>(context);
        if (writeContext == nullptr || writeContext->Bytes == nullptr || data == nullptr || size <= 0)
        {
            return;
        }

        const size_t byteCount = static_cast<size_t>(size);
        const size_t currentSize = writeContext->Bytes->size();
        const size_t remainingBytes = currentSize < writeContext->RetainedByteLimit
                                    ? writeContext->RetainedByteLimit - currentSize
                                    : 0u;
        const size_t appendByteCount = std::min(byteCount, remainingBytes);
        if (appendByteCount < byteCount)
        {
            writeContext->bTruncated = true;
        }
        if (appendByteCount == 0)
        {
            return;
        }

        try
        {
            writeContext->Bytes->resize(currentSize + appendByteCount);
        }
        catch (...)
        {
            writeContext->bAllocationFailed = true;
            return;
        }

        std::memcpy(writeContext->Bytes->data() + currentSize, data, appendByteCount);
    }

    [[nodiscard]] bool EncodePng(
        const VariableArray<uint8_t>& targetPixels,
        ThumbnailDimensions dimensions,
        size_t maxEncodedBytes,
        VariableArray<uint8_t>& pngBytes,
        bool& bOutExceededByteLimit)
    {
        pngBytes.clear();
        bOutExceededByteLimit = false;

        const size_t retainedByteLimit = maxEncodedBytes == std::numeric_limits<size_t>::max()
                                       ? maxEncodedBytes
                                       : maxEncodedBytes + 1u;
        PngWriteContext writeContext;
        writeContext.Bytes = &pngBytes;
        writeContext.RetainedByteLimit = retainedByteLimit;

        const int strideBytes = static_cast<int>(static_cast<uint64_t>(dimensions.Width) * ThumbnailBytesPerPixel);
        const int writeResult = stbi_write_png_to_func(
            StbiPngWriteCallback,
            &writeContext,
            static_cast<int>(dimensions.Width),
            static_cast<int>(dimensions.Height),
            static_cast<int>(ThumbnailBytesPerPixel),
            targetPixels.data(),
            strideBytes);

        if (writeContext.bAllocationFailed || writeResult == 0)
        {
            pngBytes.clear();
            return false;
        }

        bOutExceededByteLimit = writeContext.bTruncated || pngBytes.size() > maxEncodedBytes;
        if (bOutExceededByteLimit && pngBytes.size() > retainedByteLimit)
        {
            pngBytes.resize(retainedByteLimit);
        }
        return true;
    }
} // namespace

    FrameCaptureThumbnailResult EncodeCapturedFrameThumbnail(const CapturedFrame& frame, const FrameCaptureThumbnailOptions& options)
    {
        FrameCaptureThumbnailResult result;
        CopyFrameMetadata(frame, result);

        if (frame.Status != FrameCaptureResultStatus::Success)
        {
            result.Status = FrameCaptureThumbnailStatus::CapturedFrameNotReady;
            return result;
        }

        if (options.MaxWidth == 0 || options.MaxHeight == 0 || options.MaxEncodedBytes == 0)
        {
            result.Status = FrameCaptureThumbnailStatus::InvalidOptions;
            return result;
        }

        const PixelDataValidation validation = ValidatePixelData(frame);
        if (validation.Status != FrameCaptureThumbnailStatus::Success)
        {
            result.Status = validation.Status;
            return result;
        }

        ThumbnailDimensions dimensions = CalculateInitialDimensions(frame, options);
        while (true)
        {
            VariableArray<uint8_t> targetPixels;
            if (!BuildTargetPixels(frame, dimensions, targetPixels))
            {
                result.Status = FrameCaptureThumbnailStatus::EncodeFailed;
                result.PngBytes.clear();
                return result;
            }

            bool bExceededByteLimit = false;
            VariableArray<uint8_t> pngBytes;
            if (!EncodePng(targetPixels, dimensions, options.MaxEncodedBytes, pngBytes, bExceededByteLimit))
            {
                result.Status = FrameCaptureThumbnailStatus::EncodeFailed;
                result.PngBytes.clear();
                return result;
            }

            result.Width = dimensions.Width;
            result.Height = dimensions.Height;
            if (!bExceededByteLimit)
            {
                result.Status = FrameCaptureThumbnailStatus::Success;
                result.PngBytes = std::move(pngBytes);
                return result;
            }

            if (dimensions.Width == 1u && dimensions.Height == 1u)
            {
                result.Status = FrameCaptureThumbnailStatus::EncodedBytesTooLarge;
                result.PngBytes.clear();
                return result;
            }

            dimensions = ReduceDimensions(dimensions);
        }
    }

    const char* GetFrameCaptureThumbnailMimeType()
    {
        return "image/png";
    }

} // namespace NorvesLib::Core::Rendering
