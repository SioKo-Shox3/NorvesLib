// 解凍済みlinear RGBA8からARMの判定と焼込を行う。画像IO・RHI・ファイル出力は含まない。
#pragma once
#include "Container/Span.h"
#include "Resource/MaterialImportPolicy.h"
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Tools::AssetCook
{
    struct ArmImageView
    {
        bool Present = false;
        uint32_t Width = 0, Height = 0;
        Core::Container::Span<const uint8_t> Pixels;
    };
    struct ArmImageInputs
    {
        ArmImageView Occlusion;
        ArmImageView MetallicRoughness;
    };
    struct ArmImagePlan
    {
        Core::AssetImport::ArmChannelDecision Channels[3];
        bool HasSourceImage[3]{};
        uint8_t TextureMask = 0; // bit0 AO、bit1 roughness、bit2 metallic
        uint32_t Width = 0, Height = 0;
        size_t ByteCount = 0; // 全channelが定数なら0で、ARM画像を生成しない。
    };
    enum class ArmImageStatus : uint8_t
    {
        Success, InvalidImage, InvalidPolicy, IncompatibleDimensions, InvalidOutput, OverlappingStorage
    };
    // Presentなら密なWidth*Height*4 bytes必須。省略textureはglTFの白sampleを使い、実測とは区別する。
    // factorsはAO strength/roughnessFactor/metallicFactor。呼出中の入力変更禁止。
    // textureとして残るAOとMRだけ同寸法が必要。定数化後に使わない画像の寸法差は許す。
    [[nodiscard]] ArmImageStatus AnalyzeArmImages(const ArmImageInputs& images,
        const Core::AssetImport::ArmImportPolicy& policy, const double (&factors)[3], ArmImagePlan& out) noexcept;
    // 再解析で全入力/出力を検証してから書く。失敗時はpixelsとplanを保持。余剰末尾は非変更。
    // 入力・出力・planの重複は禁止。alphaはactive MR、なければactive AOの元値を保持する（glTF ARMでは未使用）。
    [[nodiscard]] ArmImageStatus BakeArmImages(const ArmImageInputs& images,
        const Core::AssetImport::ArmImportPolicy& policy, const double (&factors)[3],
        Core::Container::Span<uint8_t> pixels, ArmImagePlan& out) noexcept;
} // namespace NorvesLib::Tools::AssetCook
