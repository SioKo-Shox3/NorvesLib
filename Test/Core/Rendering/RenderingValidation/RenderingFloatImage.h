#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameCaptureTypes.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    enum class FloatImageStatus : uint8_t
    {
        Success,
        CaptureNotSuccessful,
        UnsupportedFormat,
        InvalidDimensions,
        InvalidPixelData
    };

    struct RgbaFloatImage
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::VariableArray<float> Values;
    };

    enum class NonFiniteKind : uint8_t
    {
        None,
        NaN,
        PositiveInfinity,
        NegativeInfinity
    };

    struct NonFiniteLocation
    {
        NonFiniteKind Kind = NonFiniteKind::None;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Channel = 0;
    };

    enum class RgbaFloatViolationKind : uint8_t
    {
        None,
        NonFinite,
        PositiveSaturation,
        NegativeSaturation
    };

    struct RgbaFloatViolation
    {
        RgbaFloatViolationKind Kind = RgbaFloatViolationKind::None;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Channel = 0;
        float Value = 0.0f;
    };

    float DecodeIeee754Binary16(uint16_t bits);
    uint16_t EncodeIeee754Binary16Rne(float value);
    bool ValidateIeee754Binary16RneTable();
    FloatImageStatus DecodeCapturedRgba16Float(
        const Core::Rendering::CapturedFrame& frame,
        RgbaFloatImage& outImage);
    /**
     * @brief RGBA16F（ラスタのSceneColor）とRGBA32F（パストレーサーの累積画像）の取得結果を読む。
     */
    FloatImageStatus DecodeCapturedRgbaFloat(
        const Core::Rendering::CapturedFrame& frame,
        RgbaFloatImage& outImage);
    /**
     * @brief 比較用にRGBA float画像を書き出す。
     *
     * 形式は1行目 `NLRGBA32F <幅> <高さ> <試料数>`、続けて行優先のRGBA float32（リトルエンディアン）。
     */
    bool WriteRgbaFloatDump(const Core::Container::String& path, const RgbaFloatImage& image,
                            uint32_t sampleCount);
    /** @brief WriteRgbaFloatDumpの形式を読む。 */
    bool ReadRgbaFloatDump(const Core::Container::String& path, RgbaFloatImage& outImage,
                           uint32_t& outSampleCount);
    NonFiniteLocation FindFirstNonFinite(const RgbaFloatImage& image);
    bool IsFiniteImage(const RgbaFloatImage& image);
    RgbaFloatViolation FindFirstRgba16FloatViolation(const RgbaFloatImage& image);
    bool IsFiniteAndWithinRgba16Range(const RgbaFloatImage& image);
}
