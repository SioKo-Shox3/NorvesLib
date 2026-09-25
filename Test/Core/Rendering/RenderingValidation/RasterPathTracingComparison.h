#pragma once

// ラスタとパストレーサーの同一シーンの画像を知覚差（LDR-FLIP）で比べる部品。
// 判定は原寸のFLIP平均、原寸の画素単位FLIP最大（幾何が一致する画素だけ）、区画平均画像のFLIP最大の三つ。

#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingPerceptualDiff.h"

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{

    struct FlipMeasurement
    {
        double Mean = 0.0;
        float PixelMax = 0.0f;
        uint32_t PixelX = 0u;
        uint32_t PixelY = 0u;
        float BlockMax = 0.0f;
        uint32_t BlockX = 0u;
        uint32_t BlockY = 0u;
        // 幾何が一致する画素だけの画素単位最大と、一致しない画素の最大（記録用）。
        float AgreeingPixelMax = 0.0f;
        uint32_t AgreeingPixelX = 0u;
        uint32_t AgreeingPixelY = 0u;
        float DisagreeingPixelMax = 0.0f;
        uint32_t DisagreeingPixelX = 0u;
        uint32_t DisagreeingPixelY = 0u;
        // 一致しない画素を参照の値へ置き換えた画像の画素ごとのFLIP誤差（診断用）。
        Core::Container::VariableArray<float> AgreeingErrorMap;
    };

    // プリエクスポージャ済みのHDRをx/(1+x)で[0,1]へ写し、sRGBの8bitにする（両画像で同じ写像）。
    Rgba8Image ToneMapToLdr(const RgbaFloatImage& image);

    // blockSize×blockSizeの区画の平均の画像。
    RgbaFloatImage DownsampleBlocks(const RgbaFloatImage& image, uint32_t blockSize);

    // agreementは画素ごとの幾何の一致（1=一致）。空なら全画素を一致として扱う。FLIPは近傍の画素も
    // 見て誤差を出すため、一致しない画素の差が隣の一致する画素の値へ広がる。画素単位最大は、一致
    // しない画素を参照の値へ置き換えた画像で求め、一致しない画素の影響を判定から外す。平均と区画は
    // 置き換えない画像で求める。
    bool MeasureFlip(const RgbaFloatImage& reference,
                     const RgbaFloatImage& candidate,
                     const Core::Container::VariableArray<uint8_t>& agreement,
                     uint32_t blockSize,
                     FlipMeasurement& outMeasurement);

    // ラスタの検証表示（法線は0.5n+0.5、距離はd/(d+25)）とPTの1次命中（法線・距離）から、画素ごとの
    // 幾何の一致を作る。距離1%以内・法線の内積0.99以上で一致とし、両方とも不交差（0）の画素も一致とする。
    Core::Container::VariableArray<uint8_t> BuildGeometryAgreement(const RgbaFloatImage& rasterNormal,
                                                                   const RgbaFloatImage& rasterDepth,
                                                                   const RgbaFloatImage& pathNormal,
                                                                   const RgbaFloatImage& pathDistance,
                                                                   uint32_t& outDisagreeingPixels);

    // ラスタの太陽の可視（CSM・RT影の係数、検証表示245）とPTの太陽の可視（光線、検証出力sun-visibility）
    // の差がtoleranceを超え、かつPTの可視の縁（edgeRadius画素以内に可視0.5以上と未満の両方がある）に近い
    // 画素を、一致の画素から外す。影の縁で画素中心が縁のどちら側に入るかの判定の分かれで、幾何の不一致と
    // 同じく画素単位最大から除く。縁から離れた可視の食い違い（影の内側の局所欠陥）は外さない。新たに外した
    // 画素の数を返す。
    uint32_t ExcludeSunVisibilityDisagreement(const RgbaFloatImage& rasterVisibility,
                                              const RgbaFloatImage& pathVisibility,
                                              float tolerance,
                                              uint32_t edgeRadius,
                                              Core::Container::VariableArray<uint8_t>& inOutAgreement);

    // 負の対照: PTの可視が0の一様な影の内側（縁からedgeRadius+1画素以上離れた一致画素）の1画素で、ラスタの
    // 可視を1にした画像を作り、ExcludeSunVisibilityDisagreementがその画素を外さないことを確かめる。対象の
    // 画素が見つからなければfalse。
    bool SunVisibilityExclusionKeepsInteriorDefect(const RgbaFloatImage& rasterVisibility,
                                                   const RgbaFloatImage& pathVisibility,
                                                   float tolerance,
                                                   uint32_t edgeRadius,
                                                   const Core::Container::VariableArray<uint8_t>& agreement);

    // base + scale × (total - base)。閾値の物差しで、参照の一成分だけを一様に変えた画像を作る。
    RgbaFloatImage ScaleComponent(const RgbaFloatImage& base, const RgbaFloatImage& total, double scale);

    // 参照の内側（外周blockSize画素を除く）で幾何が一致する最も暗いpatchSize四方の画素へ、画像の
    // 平均輝度のleakScale倍の光を足す（局所欠陥の負の対照）。
    RgbaFloatImage AddLocalLeak(const RgbaFloatImage& image,
                                double meanLuminance,
                                const Core::Container::VariableArray<uint8_t>& agreement,
                                uint32_t blockSize,
                                uint32_t patchSize,
                                double leakScale);

    double MeanLuminance(const RgbaFloatImage& image);

    void PrintFlipMeasurement(const char* label, const FlipMeasurement& measurement);

    // 画素単位の閾値を超える一致画素の数と、16画素以上離れた上位8か所の位置（診断用）を出力する。
    void PrintAgreeingPixelsOverLimit(const FlipMeasurement& measurement, float pixelLimit, uint32_t width);

} // namespace NorvesLib::Test::RenderingValidation
