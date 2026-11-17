#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 霧と空気遠近の視線の歩の配置。
    enum class VolumetricFogStepDistribution : uint8_t
    {
        Uniform,     // 等間隔（中点則。既定）
        Exponential, // 手前を細かくし、隣の歩の長さを ExponentialStepRatio 倍ずつ伸ばす
    };

    /// 霧だけの経路の歩数。
    inline constexpr uint32_t VolumetricFogOnlyStepCount = 24u;
    /// 霧と空気遠近を一緒に積む歩数。
    inline constexpr uint32_t VolumetricAtmosphereStepCount = 32u;
    /// 霧と空気遠近の経路で、上限より奥の区間に割く歩数。
    inline constexpr uint32_t VolumetricAtmosphereFarStepCount = 8u;
    /// 開発用の参照の歩数（等間隔・ずらし無し・上限無し）。
    inline constexpr uint32_t VolumetricReferenceStepCount = 256u;
    /// 比がこれ未満なら等間隔として扱う。
    inline constexpr float VolumetricMinimumExponentialStepRatio = 1.0001f;

    /**
     * @brief 高さに応じて指数関数的に密度が減る霧の設定。
     *
     * 高さ y の密度は DensityAtBaseHeight * exp(-HeightFalloffPerUnit * (y - BaseHeight))。
     * 高さと距離はワールドの単位、HeightFalloffPerUnit はその逆数を使う。
     */
    struct VolumetricFogParameters
    {
        bool bEnabled = false;
        float DensityAtBaseHeight = 0.01f;
        float BaseHeight = 0.0f;
        float HeightFalloffPerUnit = 0.01f;
        /// 霧を積む距離の上限（m）。0 以下では視線全体。
        float MaxDistanceMeters = 0.0f;
        /// 霧の周りの光に掛ける倍率。
        float AmbientScale = 1.0f;
        /// 霧の色に掛ける色味（線形 RGB の倍率）。
        float Tint[3] = {1.0f, 1.0f, 1.0f};
        /// 霧の色を、視線方向の空の放射輝度から、方位を保って仰角を基準の仰角へ傾けた方向の空の放射輝度へ寄せる割合（0..1）。
        /// 空の放射輝度 LUT を引く経路だけで効く。
        float SkyBlend = 0.0f;
        /// SkyBlend が寄せる先の仰角（度）。
        float ReferenceElevationDegrees = 10.0f;
        /// 歩の配置。
        VolumetricFogStepDistribution StepDistribution = VolumetricFogStepDistribution::Uniform;
        /// 指数の配置で隣の歩の長さの比（1〜2）。VolumetricMinimumExponentialStepRatio 未満は等間隔と同じ。
        float ExponentialStepRatio = 1.15f;
        /// 影つきの散乱を歩で積む距離の上限（m）。0 以下で上限なし。
        /// 霧だけの経路では上限より奥を影なしの解析の透過率の差で足し、
        /// 霧と空気遠近の経路では上限より奥を等間隔の少ない歩で進める。
        float ScatteringCutoffMeters = 0.0f;
        /// 歩の始まりを画素ごとに Interleaved Gradient Noise でずらす。
        bool bPixelJitter = false;
        /// TAA が掛かるフレームだけ、画素ごとのずらしを時間方向にも動かす（真なら画素ごとのずらしも有効）。
        bool bTemporalJitter = false;
    };

    VolumetricFogParameters MakeDefaultVolumetricFogParameters();

    VolumetricFogParameters SanitizeVolumetricFogParameters(
        const VolumetricFogParameters& parameters);

    /// 視線の上の 1 歩（カメラからの距離 m）。
    struct VolumetricFogStep
    {
        float StartMeters = 0.0f;
        float LengthMeters = 0.0f;
    };

    /// [0, spanMeters] を stepCount 歩に分けたときの stepIndex 番目の歩（GLSL の PlaceFogStep と同じ式）。
    VolumetricFogStep ComputeVolumetricFogStep(VolumetricFogStepDistribution distribution, float ratio,
                                             uint32_t stepIndex, uint32_t stepCount, float spanMeters);

    /// 霧だけの経路で、影つきの散乱を歩で積む距離（上限があれば霧の距離と上限の小さい方）。
    float ComputeVolumetricFogScatteringSpan(const VolumetricFogParameters& parameters, float fogDistanceMeters);

    /// 霧と空気遠近を一緒に積む経路の stepIndex 番目の歩（GLSL の PlaceAtmosphereMarchStep と同じ式）。
    VolumetricFogStep ComputeVolumetricAtmosphereMarchStep(const VolumetricFogParameters& parameters,
                                                        uint32_t stepIndex, uint32_t stepCount,
                                                        float rayDistanceMeters);

    /// 霧の色を寄せる基準の仰角の正弦と余弦。
    struct FogReferenceElevationSinCos
    {
        float Sin = 0.0f;
        float Cos = 1.0f;
    };

    /// 安全化した ReferenceElevationDegrees の正弦と余弦を返す（体積の霧・前向きの霧・パストレーサーが同じ値を使う）。
    FogReferenceElevationSinCos ComputeFogReferenceElevationSinCos(const VolumetricFogParameters& parameters);

    /**
     * @brief Evaluate analytic Beer-Lambert transmittance through exponential height fog.
     *
     * rayDirectionY is the vertical component of a normalized ray direction. Invalid ray
     * inputs and disabled fog return identity transmittance (1).
     */
    float ComputeHeightFogTransmittance(
        const VolumetricFogParameters& parameters,
        float rayOriginHeight,
        float rayDirectionY,
        float rayDistance);

    struct WindParameters;

    /// 1 フレームに描く局所霧の上限。
    inline constexpr uint32_t MaxLocalFogVolumes = 8u;
    /// 突風で濃淡の流れをずらす時間（秒）。
    inline constexpr float LocalFogGustDisplacementSeconds = 1.5f;

    /// 楕円体の局所霧 1 個。密度は ρ0·exp(−½|M(p−c)|²)、M の行 i は回転した軸 i を Radius[i] / Falloff で割った値。
    struct LocalFogVolumeProxy
    {
        float Center[3] = {0.0f, 0.0f, 0.0f}; // 中心（ワールド、m）
        float Radius[3] = {1.0f, 1.0f, 1.0f}; // ローカルの半径（持ち主の拡大を掛けた値、m）
        float Rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // ワールドの回転（四元数 x, y, z, w）
        float Density = 0.05f; // 中心の消散係数 ρ0（1/m）
        float Color[3] = {1.0f, 1.0f, 1.0f}; // 散乱の色（線形 RGB、0..1）
        float Falloff = 2.0f; // 半径における標準偏差の倍数（0.5..8）
        float NoiseAmount = 0.0f; // 散乱の放射だけを変える濃淡の量（0..1）
        float NoiseScaleMeters = 4.0f; // 濃淡の雑音の大きさ（0.1..1000 m）
    };

    LocalFogVolumeProxy MakeDefaultLocalFogVolumeProxy();
    /// 非有限は既定値へ、範囲外は端へ寄せ、四元数を正規化する。四元数がゼロまたは非有限なら単位四元数。
    /// Radius は [0.01, 100000]、Density は [0, 10]、Color は [0, 1]、Falloff は [0.5, 8]、
    /// NoiseAmount は [0, 1]、NoiseScaleMeters は [0.1, 1000]。
    LocalFogVolumeProxy SanitizeLocalFogVolumeProxy(const LocalFogVolumeProxy& proxy);
    /// 全ての値が有限で、Density と 3 軸の Radius が正なら真。
    bool IsValidLocalFogVolumeProxy(const LocalFogVolumeProxy& proxy);
    /// 全欄の値を float の == で比べる。
    bool AreEqualLocalFogVolumeProxy(const LocalFogVolumeProxy& a, const LocalFogVolumeProxy& b);

    /// 単位方向の視線の区間 [t0, t1]（m、t1 は正の無限大も可）の光学的深さ。区間が空、入力が非有限、または霧が無効なら 0。
    /// 霧の値は安全化してから使う。無限大の上端は、視線方向の変化が極めて小さい場合に限り 1e7 m で打ち切る。
    float ComputeLocalFogOpticalDepth(const LocalFogVolumeProxy& proxy, const float origin[3], const float direction[3],
                                      float t0, float t1);

    /// 有効な霧をカメラから中心までの距離が近い順に最大 8 個写す。同じ距離は元の並び順。
    /// outDroppedCount は上限で落とした有効な霧の個数。カメラ位置が非有限なら 0 個。
    uint32_t SelectNearestLocalFogVolumes(const LocalFogVolumeProxy* volumes, uint32_t volumeCount,
                                          const float cameraPosition[3],
                                          LocalFogVolumeProxy* outVolumes, uint32_t& outDroppedCount);

    /// 局所霧 1 個のシェーダー定数（std140、80 バイト）。
    struct alignas(16) GPULocalFogVolume
    {
        float CenterAndDensity[4]; // xyz=中心（ワールド、m）、w=ρ0（1/m）
        float InverseAxisX[4]; // xyz=回転した X 軸 / s_x（1/m）、w=濃淡の量
        float InverseAxisY[4]; // xyz=回転した Y 軸 / s_y（1/m）、w=雑音の周波数（1/m）
        float InverseAxisZ[4]; // xyz=回転した Z 軸 / s_z（1/m）、w=0
        float ColorAndReserved[4]; // rgb=散乱の色、w=0
    };

    /// 局所霧のシェーダー定数（std140、688 バイト）。
    struct alignas(16) GPULocalFogParams
    {
        float CountAndWindClock[4]; // x=個数、y=風の時計、z=突風による時間のずれ、w=風の有効値
        float WindDirectionSpeedGust[4]; // xy=風の XZ 方向、z=平均風速、w=突風の比
        float WindGustShape[4]; // x=突風の周波数、y=塊の大きさ、z=乱流の比、w=0
        GPULocalFogVolume Volumes[MaxLocalFogVolumes];
    };
    static_assert(sizeof(GPULocalFogVolume) == 80u);
    static_assert(offsetof(GPULocalFogVolume, CenterAndDensity) == 0u);
    static_assert(offsetof(GPULocalFogVolume, InverseAxisX) == 16u);
    static_assert(offsetof(GPULocalFogVolume, InverseAxisY) == 32u);
    static_assert(offsetof(GPULocalFogVolume, InverseAxisZ) == 48u);
    static_assert(offsetof(GPULocalFogVolume, ColorAndReserved) == 64u);
    static_assert(sizeof(GPULocalFogParams) == 688u);
    static_assert(offsetof(GPULocalFogParams, CountAndWindClock) == 0u);
    static_assert(offsetof(GPULocalFogParams, WindDirectionSpeedGust) == 16u);
    static_assert(offsetof(GPULocalFogParams, WindGustShape) == 32u);
    static_assert(offsetof(GPULocalFogParams, Volumes) == 48u);

    /// 近い順に選択済みの霧の先頭最大 8 個を安全化して詰め、無効な霧は飛ばす。
    /// 有効な風の方向・風速・突風・乱流と時計を詰め、無効な風の欄は全て 0 にする。
    GPULocalFogParams BuildLocalFogGpuParams(const LocalFogVolumeProxy* volumes, uint32_t volumeCount,
                                             const WindParameters& wind, float windClockSeconds);

} // namespace NorvesLib::Core::Rendering
