// 材質設定の優先順位と解決済み設定の正規形。JSON・ファイル・RHIには依存しない。
#pragma once
#include "Resource/MaterialImportPolicy.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    enum class MaterialSourceProfile : uint8_t { Source, AiGenerated };
    enum class DoubleSidedSetting : uint8_t { Inherit, FromSource, ForceTrue, ForceFalse };
    enum class AlphaModeSetting : uint8_t { Inherit, FromSource, ForceOpaque };
    struct MaterialSettingsLayer
    {
        uint8_t ArmMask = 0;
        ArmImportPolicy Arm;
        EmissiveScale Emission;
        DoubleSidedSetting DoubleSided = DoubleSidedSetting::Inherit;
        AlphaModeSetting AlphaMode = AlphaModeSetting::Inherit;
    };
    struct ResolvedMaterialSettings
    {
        MaterialSourceProfile Profile = MaterialSourceProfile::Source;
        ArmImportPolicy Arm = DefaultArmImportPolicy(false);
        EmissiveScale Emission;
        DoubleSidedSetting DoubleSided = DoubleSidedSetting::FromSource;
        AlphaModeSetting AlphaMode = AlphaModeSetting::FromSource;
    };
    enum class MaterialSettingsStatus : uint8_t
    {
        Success, InvalidProfile, InvalidArmMask, InvalidArmPolicy,
        InvalidDoubleSided, InvalidAlphaMode, InvalidNitsPerUnit
    };
    [[nodiscard]] MaterialSettingsStatus ValidateMaterialSettingsLayer(const MaterialSettingsLayer& layer) noexcept;
    // 素材 > 資産 > 既定。asset-setは発光換算値だけを補う。明示された不正値は上書きで隠さない。
    // 不在の換算値はここでは有効。発光factor/strength判定後にImportEmissionが必須指定を検査する。
    // 名前から素材を選ぶ処理は呼出元の責務。失敗時outは保持する。
    [[nodiscard]] MaterialSettingsStatus ResolveMaterialSettings(MaterialSourceProfile profile,
        const MaterialSettingsLayer& asset, const MaterialSettingsLayer& material,
        EmissiveScale assetSet, ResolvedMaterialSettings& out) noexcept;
    inline constexpr uint32_t MaterialSettingsCanonicalVersion = 1;
    inline constexpr size_t CanonicalMaterialSettingsSize = 67;
    struct CanonicalMaterialSettings
    {
        uint8_t Bytes[CanonicalMaterialSettingsSize]{};
        size_t Size = 0;
    };
    // LE/IEEE binary64。未使用のconstant/auto幅/不在nitsは0、-0は+0へ揃える。
    // 既存の幾何設定52Bとは別の正規形。Size=0は不正設定を表す。
    [[nodiscard]] CanonicalMaterialSettings EncodeCanonicalMaterialSettings(const ResolvedMaterialSettings& settings) noexcept;
    struct MaterialSettingsHashResult
    {
        bool Valid = false;
        uint64_t Value = 0;
    };
    // 解決済み材質1件をdomain tag・長さ・正規形付きで既存hashへ連結する。
    // 材質列は呼出元がsource material index順に処理する。旧v0経路には自動適用しない。
    [[nodiscard]] MaterialSettingsHashResult AppendMaterialSettingsHash(uint64_t hash,
        const ResolvedMaterialSettings& settings) noexcept;
    [[nodiscard]] const char* MaterialSettingsErrorKey(MaterialSettingsStatus status) noexcept;
} // namespace NorvesLib::Core::AssetImport
