// 高さ入力をメートル単位の地形格子へ変換する。
#pragma once
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    enum class HeightSourceFormat : uint8_t { Png16, Png8, Raw16, Raw32F, Exr };
    enum class HeightFitMode : uint8_t { Error, Crop, Resample };

    struct HeightImportDesc
    {
        HeightSourceFormat Format = HeightSourceFormat::Png16;
        uint32_t RawWidth = 0;          // raw16・raw32f の幅。他の形式では 0。
        uint32_t RawHeight = 0;
        bool bRawBigEndian = false;     // raw16・raw32f のバイト順。
        Core::Container::AnsiString ExrChannel; // 空なら単一チャンネル、または Y。
        bool bFlipZ = false;
        double Scale = 1.0;             // heightM = Offset + Scale * v。
        double Offset = 0.0;
        uint32_t TileCells = 128;       // 2 の冪で 8..1024。
        HeightFitMode Fit = HeightFitMode::Error;
        uint32_t RequireBitDepth = 0;   // 0 は要求なし、16 は 8bit を拒否。
        double SmoothSigmaM = 0.0;      // ガウスの σ [m]。0 は平滑化なし。
        double CellSizeM = 1.0;         // 標本の間隔 [m]。
        bool bKeepSourceSize = false;  // 真なら fit（TileCells の倍数＋1 への合わせ）を通さず、入力の寸法のまま返す。外周の入力用。
    };

    struct TerrainHeightGrid
    {
        uint32_t Width = 0;   // x 方向の標本数（TileCells*N+1）。
        uint32_t Height = 0;  // z 方向の標本数（TileCells*M+1）。
        Core::Container::VariableArray<double> HeightsM; // index = z * Width + x。
    };

    struct HeightImportResult
    {
        TerrainHeightGrid Grid;
        uint32_t SourceWidth = 0;
        uint32_t SourceHeight = 0;
        uint32_t SourceBitDepth = 0; // 8・16・32。EXR は選択チャンネルの型による。
        Core::Container::VariableArray<Core::Container::AnsiString> Warnings;
    };

    [[nodiscard]] bool ImportHeightfield(Core::Container::Span<const uint8_t> fileBytes, const HeightImportDesc& desc,
                                         HeightImportResult& outResult, Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::AssetCook
