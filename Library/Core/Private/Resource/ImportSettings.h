#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class JsonValue;
}
namespace NorvesLib::Core::AssetImport
{
    enum class SignedAxis : uint8_t { PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ };
    enum class FitAxis : uint8_t { None, Up, Forward, Longest };
    enum class OriginMode : uint8_t { Keep, BoundsCenter, BoundsBottomCenter, SurfaceCentroid, Custom };
    enum class WindingMode : uint8_t { Keep, Flip, Auto };

    struct ImportSettings
    {
        double Scale = 1.0;
        FitAxis Fit = FitAxis::None;
        double FitMeters = 1.0;
        SignedAxis Up = SignedAxis::PositiveY;
        SignedAxis Forward = SignedAxis::PositiveZ;
        bool bMirrorX = false;
        OriginMode Origin = OriginMode::Keep;
        double OriginOffset[3] = {};
        WindingMode Winding = WindingMode::Keep;
        bool bFlipU = false;
        bool bFlipV = false;
    };

    enum class SettingsResult : uint8_t
    {
        Success, InvalidRoot, InvalidVersion, UnknownField, DuplicateField,
        InvalidType, InvalidValue, UnsupportedFeature
    };
    [[nodiscard]] SettingsResult ValidateSettings(const ImportSettings& settings) noexcept;
    // JSON解析は実JsonDocumentを使用する。未知/重複fieldを拒否し、成功時だけ出力を置換する。
    // metaは検証だけ行い保存/hashへ含めない。未実装機能blockは空objectだけ受理する。
    [[nodiscard]] SettingsResult ParseSettings(const JsonValue& root, ImportSettings& outSettings);

    inline constexpr uint32_t ImportSettingsSchemaVersion = 1;
    inline constexpr size_t CanonicalSettingsSize = 52;
    struct CanonicalSettings
    {
        uint8_t Bytes[CanonicalSettingsSize] = {};
        size_t Size = 0;
    };
    // 固定幅LE、浮動小数はIEEE binary64。-0を+0へ揃え、structのpaddingを含めない。
    // 不正設定はSize=0。sidecarの有無は呼出側が保持し、無しなら既存hashへ連結しない。
    [[nodiscard]] CanonicalSettings EncodeCanonicalSettings(const ImportSettings& settings) noexcept;
} // namespace NorvesLib::Core::AssetImport
