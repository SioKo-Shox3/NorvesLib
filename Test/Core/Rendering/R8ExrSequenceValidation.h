// R8のEXR連番の検査: 欠番・寸法の不一致・非有限の画素と、ACES 2.0 SDRのLUTでdisplayへ変換した
// 隣接フレームのLDR-FLIP平均によるポッピングを検出する。
#pragma once

#include "Container/Containers.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingGoldenImage.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    // ACES 2.0 SDRのベイク3D LUT（R8-P1の`NLUT3D01`）。値はdisplay-linearのRGBで、R成分が最も速く変わる順。
    struct AcesOutputLut
    {
        uint32_t Size = 0u;
        float ShaperOffset = 0.0f;
        float ShaperMax = 0.0f;
        Core::Container::VariableArray<float> Values;
    };

    bool LoadAcesOutputLut(const char* path, AcesOutputLut& outLut);

    // scene-linearの1画素をdisplay-linearへ写す（tonemapping.fragのTonemapAces20Lutと同じshaperと三線形補間）。
    void ApplyAcesOutputLut(const AcesOutputLut& lut, const float sceneLinear[3], float outDisplayLinear[3]);

    // scene-linearの画像へexposureを掛けてLUTでdisplayへ写し、区分的sRGBで8 bitへ符号化する。
    Rgba8Image ToDisplayLdr(const AcesOutputLut& lut, const RgbaFloatImage& sceneLinear, float exposure);

    // EXRの1枚目のpartのR・G・B（無ければ0）とA（無ければ1）を、行優先のRGBA floatへ読む。
    bool ReadExrRgba(const char* path, RgbaFloatImage& outImage);

    // 検査の規則。連番を見る前に固定する。
    struct ExrSequenceRules
    {
        // 隣接フレームのFLIP平均が「中央値×PoppingMedianScale」と「PoppingFloor」の大きい方を超えたらポッピング。
        double PoppingMedianScale = 3.0;
        double PoppingFloor = 0.01;
    };

    struct ExrSequenceNonFinite
    {
        uint32_t Frame = 0u;
        uint32_t Count = 0u;
        // 最初に見つけた画素（行優先）。
        uint32_t X = 0u;
        uint32_t Y = 0u;
        uint32_t Channel = 0u;
    };

    struct ExrSequenceReport
    {
        uint32_t FirstFrame = 0u;
        uint32_t ExpectedFrames = 0u;
        uint32_t FoundFrames = 0u;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        Core::Container::VariableArray<uint32_t> MissingFrames;
        Core::Container::VariableArray<uint32_t> DuplicateFrames;
        Core::Container::VariableArray<uint32_t> UnreadableFrames;
        Core::Container::VariableArray<uint32_t> DimensionMismatchFrames;
        Core::Container::VariableArray<ExrSequenceNonFinite> NonFiniteFrames;
        // AdjacentFrames[i]とその前のフレームの組のFLIP平均（前後とも読めて寸法が合う組だけ）。
        Core::Container::VariableArray<uint32_t> AdjacentFrames;
        Core::Container::VariableArray<double> AdjacentMeanFlip;
        double MedianFlip = 0.0;
        double PoppingLimit = 0.0;
        // 閾値を超えた組の後ろのフレーム番号。
        Core::Container::VariableArray<uint32_t> PoppingFrames;

        bool Passed() const;
    };

    // directoryの中の`<sceneName>_seed<8桁>_spp<6桁>_frame<6桁>.exr`を、firstFrameからexpectedFrameCount枚検査する。
    // 検査を実行できない（directoryやLUTが無い）ときだけfalseを返し、見つけた問題はoutReportへ入れる。
    bool ValidateExrSequence(const char* directory,
                             const char* sceneName,
                             uint32_t firstFrame,
                             uint32_t expectedFrameCount,
                             const AcesOutputLut& lut,
                             float exposure,
                             const ExrSequenceRules& rules,
                             ExrSequenceReport& outReport);

    void PrintExrSequenceReport(const ExrSequenceReport& report);
} // namespace NorvesLib::Test::RenderingValidation
