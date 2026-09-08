#include "RenderingValidation/RenderingGoldenImage.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Test::RenderingValidation;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "RenderingGoldenImageComparatorTest failed: " << message << '\n';
            std::exit(1);
        }
    }

    Rgba8Image MakeSolidImage(uint32_t width, uint32_t height, const uint8_t (&rgba)[4])
    {
        Rgba8Image image;
        image.Width = width;
        image.Height = height;
        image.RowPitchBytes = width * 4u;
        image.Pixels.resize(static_cast<size_t>(image.RowPitchBytes) * height);
        for (size_t offset = 0; offset < image.Pixels.size(); offset += 4u)
        {
            image.Pixels[offset + 0u] = rgba[0];
            image.Pixels[offset + 1u] = rgba[1];
            image.Pixels[offset + 2u] = rgba[2];
            image.Pixels[offset + 3u] = rgba[3];
        }
        return image;
    }

    Core::Container::String BuildPath(const TCHAR* suffix)
    {
        Core::Container::String path(NORVES_BINARY_ROOT);
        path += suffix;
        return path;
    }

    void EnsureValidationDirectories()
    {
        const Core::Container::String validationRoot = BuildPath(TEXT("/RenderingValidation"));
        const Core::Container::String stagingRoot =
            BuildPath(TEXT("/RenderingValidation/BaselineStaging"));
        CreateDirectory(validationRoot.c_str(), nullptr);
        CreateDirectory(stagingRoot.c_str(), nullptr);
    }

    Core::Container::String IndoorStagingPath()
    {
        return BuildPath(TEXT("/RenderingValidation/BaselineStaging/Indoor.png.tmp"));
    }

    Core::Container::String OutdoorStagingPath()
    {
        return BuildPath(TEXT("/RenderingValidation/BaselineStaging/Outdoor.png.tmp"));
    }

    const char* StatusName(GoldenImageStatus status)
    {
        switch (status)
        {
        case GoldenImageStatus::Success:
            return "Success";
        case GoldenImageStatus::CaptureNotSuccessful:
            return "CaptureNotSuccessful";
        case GoldenImageStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case GoldenImageStatus::InvalidDimensions:
            return "InvalidDimensions";
        case GoldenImageStatus::InvalidPixelData:
            return "InvalidPixelData";
        case GoldenImageStatus::EncodeFailed:
            return "EncodeFailed";
        case GoldenImageStatus::DecodeFailed:
            return "DecodeFailed";
        case GoldenImageStatus::FileOpenFailed:
            return "FileOpenFailed";
        case GoldenImageStatus::FileWriteFailed:
            return "FileWriteFailed";
        }
        return "Unknown";
    }

    GoldenImageStatus ValidateStagingImage(const Core::Container::String& path)
    {
        Rgba8Image image;
        const GoldenImageStatus loadStatus = LoadPng(path, image);
        if (loadStatus != GoldenImageStatus::Success)
        {
            return loadStatus;
        }
        if (image.Width != 256u || image.Height != 256u)
        {
            return GoldenImageStatus::InvalidDimensions;
        }
        if (image.RowPitchBytes != 1024u || image.Pixels.size() != 256u * 256u * 4u)
        {
            return GoldenImageStatus::InvalidPixelData;
        }
        return GoldenImageStatus::Success;
    }

    GoldenImageStatus ValidateFixedStaging()
    {
        const GoldenImageStatus indoorStatus = ValidateStagingImage(IndoorStagingPath());
        if (indoorStatus != GoldenImageStatus::Success)
        {
            return indoorStatus;
        }
        return ValidateStagingImage(OutdoorStagingPath());
    }

    void TestStrictComparatorRejectsOnePixelDifference()
    {
        const uint8_t solidColor[4] = {16u, 32u, 64u, 255u};
        Rgba8Image candidate = MakeSolidImage(4u, 4u, solidColor);
        Rgba8Image reference = candidate;
        RawImageDifferenceMetrics metrics;

        Require(CompareRgba8(reference, candidate, metrics) == GoldenImageStatus::Success,
                "identical images must compare successfully");
        Require(MeetsRawGoldenThresholds(metrics, {0u, 0u}),
                "identical images must meet strict thresholds");

        candidate.Pixels[0] ^= 0xFFu;
        Require(CompareRgba8(reference, candidate, metrics) == GoldenImageStatus::Success,
                "one-pixel-different images must produce metrics");
        Require(metrics.DifferingPixelCount == 1u,
                "one changed channel must count as one differing pixel");
        Require(metrics.MaxChannelDelta == 223u,
                "maximum channel delta must preserve the exact raw delta");
        Require(metrics.MaxDifferenceX == 0u && metrics.MaxDifferenceY == 0u,
                "first maximum difference coordinate must be row-major deterministic");
        Require(metrics.MeanAbsoluteChannelDelta == 3.484375,
                "mean absolute delta must cover all RGBA channels");
        Require(!MeetsRawGoldenThresholds(metrics, {0u, 0u}),
                "strict thresholds must reject a one-pixel difference");

        std::cout << "one_pixel_negative=rejected max_coordinate=(0,0)\n";
    }

    void TestCompareRejectsDimensionMismatch()
    {
        const uint8_t solidColor[4] = {1u, 2u, 3u, 255u};
        const Rgba8Image reference = MakeSolidImage(4u, 4u, solidColor);
        const Rgba8Image candidate = MakeSolidImage(3u, 4u, solidColor);
        RawImageDifferenceMetrics metrics;

        Require(CompareRgba8(reference, candidate, metrics) == GoldenImageStatus::InvalidDimensions,
                "dimension mismatch must be rejected");
    }

    void TestCompareRejectsInvalidRowPitch()
    {
        const uint8_t solidColor[4] = {1u, 2u, 3u, 255u};
        const Rgba8Image reference = MakeSolidImage(4u, 4u, solidColor);
        Rgba8Image candidate = reference;
        candidate.RowPitchBytes = 15u;
        RawImageDifferenceMetrics metrics;

        Require(CompareRgba8(reference, candidate, metrics) == GoldenImageStatus::InvalidPixelData,
                "row pitch shorter than RGBA width must be rejected");
    }

    void TestDecodeRejectsEmptyPng()
    {
        Container::VariableArray<uint8_t> emptyPng;
        Rgba8Image decoded;

        Require(DecodePng(Container::Span<const uint8_t>(emptyPng.data(), emptyPng.size()), decoded) ==
                    GoldenImageStatus::DecodeFailed,
                "empty PNG must fail decoding");
    }

    void TestPngRoundTripNormalizesToTightRgbaAndPersists()
    {
        Rgba8Image image;
        image.Width = 2u;
        image.Height = 1u;
        image.RowPitchBytes = 12u;
        image.Pixels = {10u, 20u, 30u, 255u, 40u, 50u, 60u, 128u, 1u, 2u, 3u, 4u};

        Core::Container::VariableArray<uint8_t> png;
        Require(EncodeRgba8Png(image, png) == GoldenImageStatus::Success,
                "valid padded RGBA8 must encode");
        Rgba8Image decoded;
        Require(DecodePng(Core::Container::Span<const uint8_t>(png), decoded) == GoldenImageStatus::Success,
                "encoded PNG must decode");
        Require(decoded.Width == 2u && decoded.Height == 1u && decoded.RowPitchBytes == 8u,
                "decoded PNG must normalize dimensions and tight row pitch");
        Require(decoded.Pixels.size() == 8u && decoded.Pixels[0] == 10u && decoded.Pixels[7] == 128u,
                "decoded PNG must preserve decoded RGBA values and omit source padding");

        EnsureValidationDirectories();
        const Core::Container::String path = BuildPath(TEXT("/RenderingValidation/ComparatorRoundTrip.png"));
        Require(SavePng(path, Core::Container::Span<const uint8_t>(png)) == GoldenImageStatus::Success,
                "PNG must save under the binary root");
        Rgba8Image loaded;
        Require(LoadPng(path, loaded) == GoldenImageStatus::Success && loaded.Pixels == decoded.Pixels,
                "saved PNG must load with identical decoded RGBA values");
    }

    void TestCapturedFramePngRequiresExactGoldenDimensions()
    {
        Core::Rendering::CapturedFrame frame;
        frame.Status = Core::Rendering::FrameCaptureResultStatus::Success;
        frame.RequestId = 17u;
        frame.FrameNumber = 29u;
        frame.Width = 256u;
        frame.Height = 256u;
        frame.Format = RHI::Format::R8G8B8A8_UNORM;
        frame.BytesPerPixel = 4u;
        frame.RowPitchBytes = 1024u;
        frame.Pixels.resize(256u * 256u * 4u, 64u);

        Core::Container::VariableArray<uint8_t> png;
        Require(EncodeCapturedFramePng(frame, png) == GoldenImageStatus::Success && !png.empty(),
                "successful 256x256 capture must encode");
        frame.Width = 128u;
        frame.RowPitchBytes = 512u;
        frame.Pixels.resize(128u * 256u * 4u);
        Require(EncodeCapturedFramePng(frame, png) == GoldenImageStatus::InvalidDimensions,
                "capture normalization must reject output that is not exactly 256x256");
        frame.Status = Core::Rendering::FrameCaptureResultStatus::SourceUnavailable;
        Require(EncodeCapturedFramePng(frame, png) == GoldenImageStatus::CaptureNotSuccessful,
                "unsuccessful capture must retain a specific status");
    }

    Core::Rendering::CapturedFrame MakeCapturedFrame(
        RHI::Format format,
        uint32_t rowPitchBytes,
        bool bBgra)
    {
        Core::Rendering::CapturedFrame frame;
        frame.Status = Core::Rendering::FrameCaptureResultStatus::Success;
        frame.Width = 256u;
        frame.Height = 256u;
        frame.Format = format;
        frame.BytesPerPixel = 4u;
        frame.RowPitchBytes = rowPitchBytes;
        frame.Pixels.resize(static_cast<size_t>(rowPitchBytes) * frame.Height, 0u);
        for (uint32_t y = 0u; y < frame.Height; ++y)
        {
            const size_t rowOffset = static_cast<size_t>(y) * rowPitchBytes;
            for (uint32_t x = 0u; x < frame.Width; ++x)
            {
                const size_t offset = rowOffset + static_cast<size_t>(x) * 4u;
                frame.Pixels[offset + (bBgra ? 2u : 0u)] = 30u;
                frame.Pixels[offset + 1u] = 60u;
                frame.Pixels[offset + (bBgra ? 0u : 2u)] = 90u;
                frame.Pixels[offset + 3u] = 255u;
            }
        }
        return frame;
    }

    void TestCapturedFrameFormatAndPitchMatrix()
    {
        struct FormatCase
        {
            RHI::Format Format;
            uint32_t RowPitchBytes;
            bool bBgra;
        };
        const FormatCase cases[] = {
            {RHI::Format::R8G8B8A8_UNORM, 1024u, false},
            {RHI::Format::B8G8R8A8_UNORM, 1024u, true},
            {RHI::Format::R8G8B8A8_SRGB, 1032u, false},
            {RHI::Format::B8G8R8A8_SRGB, 1032u, true}};
        for (const FormatCase& formatCase : cases)
        {
            Core::Rendering::CapturedFrame frame = MakeCapturedFrame(
                formatCase.Format, formatCase.RowPitchBytes, formatCase.bBgra);
            Core::Container::VariableArray<uint8_t> png;
            Require(EncodeCapturedFramePng(frame, png) == GoldenImageStatus::Success,
                    "RGBA/BGRA tight and padded captures must encode");
            Rgba8Image decoded;
            Require(DecodePng(Core::Container::Span<const uint8_t>(png), decoded) ==
                        GoldenImageStatus::Success,
                    "format matrix PNG must decode");
            Require(decoded.Width == 256u && decoded.Height == 256u &&
                        decoded.RowPitchBytes == 1024u && decoded.Pixels.size() == 256u * 256u * 4u,
                    "format matrix output must be tight 256x256 RGBA8");
            constexpr uint32_t sampleCoordinates[4][2] = {
                {0u, 0u}, {255u, 0u}, {0u, 255u}, {255u, 255u}};
            for (const auto& coordinate : sampleCoordinates)
            {
                const size_t offset =
                    (static_cast<size_t>(coordinate[1]) * decoded.RowPitchBytes) +
                    static_cast<size_t>(coordinate[0]) * 4u;
                Require(decoded.Pixels[offset + 0u] == 30u &&
                            decoded.Pixels[offset + 1u] == 60u &&
                            decoded.Pixels[offset + 2u] == 90u &&
                            decoded.Pixels[offset + 3u] == 255u,
                        "format matrix must preserve logical RGBA order across padded rows");
            }
        }

        Core::Rendering::CapturedFrame unsupported = MakeCapturedFrame(
            RHI::Format::R16G16B16A16_FLOAT, 2048u, false);
        Core::Container::VariableArray<uint8_t> png;
        Require(EncodeCapturedFramePng(unsupported, png) == GoldenImageStatus::UnsupportedFormat,
                "unsupported capture format must be rejected");

        Core::Rendering::CapturedFrame insufficient = MakeCapturedFrame(
            RHI::Format::R8G8B8A8_UNORM, 1024u, false);
        insufficient.Pixels.resize(insufficient.Pixels.size() - 1u);
        Require(EncodeCapturedFramePng(insufficient, png) == GoldenImageStatus::InvalidPixelData,
                "insufficient capture bytes must be rejected");

        Core::Rendering::CapturedFrame wrongDimensions = MakeCapturedFrame(
            RHI::Format::R8G8B8A8_UNORM, 1028u, false);
        wrongDimensions.Width = 257u;
        Require(EncodeCapturedFramePng(wrongDimensions, png) == GoldenImageStatus::InvalidDimensions,
                "257x256 capture must be rejected without thumbnail downscale");
    }

    int RunFixedStagingValidator()
    {
        const GoldenImageStatus status = ValidateFixedStaging();
        std::cout << "fixed_staging_validation=" << StatusName(status) << '\n';
        return status == GoldenImageStatus::Success ? 0 : 1;
    }

    int RunFixedStagingNegative(const char* mode)
    {
        EnsureValidationDirectories();
        GoldenImageStatus expectedStatus = GoldenImageStatus::Success;
        if (std::strcmp(mode, "corrupt-indoor") == 0)
        {
            const uint8_t corruptBytes[] = {0x4Eu, 0x4Fu, 0x54u, 0x50u, 0x4Eu, 0x47u};
            Require(SavePng(IndoorStagingPath(), Core::Container::Span<const uint8_t>(corruptBytes)) ==
                        GoldenImageStatus::Success,
                    "corrupt fixture must be written only to fixed binary staging");
            expectedStatus = GoldenImageStatus::DecodeFailed;
        }
        else if (std::strcmp(mode, "wrong-size-outdoor") == 0)
        {
            const uint8_t solidColor[4] = {8u, 16u, 32u, 255u};
            const Rgba8Image wrongSize = MakeSolidImage(128u, 256u, solidColor);
            Core::Container::VariableArray<uint8_t> png;
            Require(EncodeRgba8Png(wrongSize, png) == GoldenImageStatus::Success,
                    "wrong-size fixture must remain a valid decodable PNG");
            Require(SavePng(OutdoorStagingPath(), Core::Container::Span<const uint8_t>(png)) ==
                        GoldenImageStatus::Success,
                    "wrong-size fixture must be written only to fixed binary staging");
            expectedStatus = GoldenImageStatus::InvalidDimensions;
        }
        else
        {
            std::cerr << "unsupported fixed staging negative mode\n";
            return 1;
        }

        const Core::Container::String path = expectedStatus == GoldenImageStatus::DecodeFailed
                                               ? IndoorStagingPath()
                                               : OutdoorStagingPath();
        const GoldenImageStatus actualStatus = ValidateStagingImage(path);
        std::cout << "fixed_staging_negative=" << mode << " status=" << StatusName(actualStatus) << '\n';
        return actualStatus == expectedStatus ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--validate-fixed-staging") == 0)
    {
        return RunFixedStagingValidator();
    }
    constexpr const char* NegativePrefix = "--self-test-fixed-staging-negative=";
    if (argc == 2 && std::strncmp(argv[1], NegativePrefix, std::strlen(NegativePrefix)) == 0)
    {
        return RunFixedStagingNegative(argv[1] + std::strlen(NegativePrefix));
    }
    if (argc != 1)
    {
        std::cerr << "unsupported argument\n";
        return 1;
    }

    TestStrictComparatorRejectsOnePixelDifference();
    TestCompareRejectsDimensionMismatch();
    TestCompareRejectsInvalidRowPitch();
    TestDecodeRejectsEmptyPng();
    TestPngRoundTripNormalizesToTightRgbaAndPersists();
    TestCapturedFramePngRequiresExactGoldenDimensions();
    TestCapturedFrameFormatAndPitchMatrix();
    std::cout << "RenderingGoldenImageComparatorTest passed\n";
    return 0;
}
