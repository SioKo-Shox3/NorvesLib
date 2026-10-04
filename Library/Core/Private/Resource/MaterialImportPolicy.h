// glTF材質の取り込み方針。画像IO・JSON・RHIから独立したCPU契約。
#pragma once
#include <cstdint>
namespace NorvesLib::Core::AssetImport
{
    enum class ArmChannel : uint8_t { Occlusion, Roughness, Metallic };
    enum class ArmMode : uint8_t { Texture, Ignore, Constant, Auto };
    struct ArmChannelPolicy
    {
        ArmMode Mode = ArmMode::Auto;
        double Constant = 0;
        double AutoWidth = 4.0/255.0;
    };
    struct ArmImportPolicy
    {
        ArmChannelPolicy Channels[3];
    };
    // AI分類は呼出元が明示する。GLB拡張子だけでは生成元を推測しない。
    [[nodiscard]] ArmImportPolicy DefaultArmImportPolicy(bool aiGenerated) noexcept;
    enum class MaterialPolicyStatus : uint8_t { Success, InvalidInput, EmptyHistogram, HistogramOverflow, MissingNitsPerUnit, InvalidNitsPerUnit, EmissiveOutOfRange };
    struct ArmChannelDecision
    {
        uint64_t SampleCount = 0;
        uint8_t Minimum = 0, Maximum = 0;
        double Percentile1 = 0, Percentile99 = 0;
        double Mean = 0;
        double EffectivePercentileWidth = 0;
        double Scalar = 0;
        bool UseTexture = false;
    };
    // 256binは元のlinear UNORM8。percentileは(n-1)*pの隣接順位を線形補間するType 7、定数値は全pixel平均にfactorを適用。
    // Constantは最終値のoverride。IgnoreはAO/roughness=1、metallic=0。失敗時outは保持。
    [[nodiscard]] MaterialPolicyStatus AnalyzeArmHistogram(const uint64_t (&bins)[256], ArmChannel channel,
        const ArmChannelPolicy& policy, double factor, ArmChannelDecision& out) noexcept;
    // Texture出力時も同じ式を使う。AOは1+strength*(sample-1)、他はfactor*sample。
    [[nodiscard]] MaterialPolicyStatus BakeArmByte(uint8_t sample, ArmChannel channel, double factor, uint8_t& out) noexcept;
    struct EmissiveScale
    {
        bool Present = false;
        double NitsPerUnit = 0;
    };
    // 素材 > 資産sidecar > asset-set。存在した値を選び、不正値を低優先値で隠さない。
    [[nodiscard]] EmissiveScale SelectEmissiveScale(EmissiveScale material, EmissiveScale asset, EmissiveScale assetSet) noexcept;
    struct ImportedEmission
    {
        float Color[3]{};
        float Nits = 0;
        bool Emitting = false;
    };
    // factorはglTFの[0,1]RGB、strengthはKHR_materials_emissive_strength（既定1）。
    // textureの有無は発光判定に使わない。非発光は換算指定不要。失敗時outは保持。
    [[nodiscard]] MaterialPolicyStatus ImportEmission(const double (&factor)[3], double strength,
        EmissiveScale scale, ImportedEmission& out) noexcept;
}
