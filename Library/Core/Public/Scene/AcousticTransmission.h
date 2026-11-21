// 音源から聞き手までの距離減衰・遮蔽・環境騒音を合成するための型と評価器を定義する。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include "Scene/PhysicsQueryTypes.h"
#include "Thread/Thread.h"
#include <cstdint>

namespace NorvesLib::Core::Environment
{
    class EnvironmentSystem;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainSystem;
}

namespace NorvesLib::Core::Vegetation
{
    class Vegetation;
}

namespace NorvesLib::Core::Scene
{
    class SceneQuery;
    class SurfaceRegistry;

    /// 利得の下限と、環境騒音がないときの騒音床（dB）。
    inline constexpr float kAcousticSilenceDb = -120.0f;
    /// 距離減衰を打ち切る距離（m）。通常の距離では逆二乗の減衰を保つ。
    inline constexpr float kAcousticDistanceMaxM = 1.0e6f;

    /// 距離減衰と遮蔽・マスキングの設定を選ぶ仮の音源分類。
    enum class ESoundClass : uint8_t { Cue, Ambient, Footstep, Voice, Rain, Creature, Count };

    /// 分類に対応する JSON のキーを返す。範囲外は "Unknown"。
    const char* GetSoundClassName(ESoundClass soundClass) noexcept;

    /// 音源の分類ごとに適用する距離・高域・免除の設定。
    struct AcousticClassSettings
    {
        float ReferenceDistanceM = 1.0f;          ///< 音源レベルを定義する基準距離（m、正値）。
        float HighFrequencyLevelOffsetDb = 0.0f;  ///< 音源レベルに対する高域のレベル差（dB）。
        bool bExemptFromOcclusion = false;        ///< 遮蔽を利得と高域損失に適用しない。
        bool bExemptFromMasking = false;          ///< 環境騒音による高域損失を適用しない。
    };

    /// 区分線形の騒音床曲線を定義する点。
    struct AcousticCurvePoint
    {
        float X = 0.0f;                  ///< 曲線上の位置。単位は曲線ごとに定める。
        float Db = kAcousticSilenceDb;   ///< この位置での騒音床（dB）。
    };

    /// 音響評価と各寄与源が共有する設定。曲線を含む既定設定は MakeDefault() で得る。
    struct AcousticSettings
    {
        float VegetationAbsorptionDbPerMeter = 0.5f; ///< 草を通る距離あたりの損失（dB/m）。
        float WallClassFallbackLossDb = 12.0f;       ///< 壁の透過損失を取得できない場合の損失（dB）。
        float OcclusionHighFrequencyRatio = 0.5f;    ///< 遮蔽損失のうち高域に追加する割合。
        float TerrainBaseLossDb = 6.0f;              ///< 地形による遮蔽の基礎損失（dB）。
        float TerrainLossPerMeterDb = 3.0f;          ///< 地形の深さあたりの損失（dB/m）。
        float TerrainMaxLossDb = 24.0f;              ///< 地形による損失の上限（dB）。
        float TerrainMinDepthM = 0.1f;               ///< 地形による遮蔽を数える最小深さ（m）。
        float HfCutoffMinHz = 800.0f;                 ///< 最大損失時の遮断周波数（Hz）。
        float HfCutoffMaxHz = 20000.0f;               ///< 損失がないときの遮断周波数（Hz）。
        float HfCutoffMaxLossDb = 30.0f;              ///< 遮断周波数が下限に達する損失（dB）。
        Container::VariableArray<AcousticCurvePoint> RainNoiseFloorCurve; ///< 雨量 [0, 1] と騒音床の対応。
        float RainShelteredReductionDb = 12.0f;       ///< 雨を遮る場所で騒音床を下げる量（dB）。
        Container::VariableArray<AcousticCurvePoint> WindNoiseFloorCurve; ///< 平均風速（m/s）と騒音床の対応。
        float MaskingCoefficient = 0.5f;              ///< マスキングによる高域損失の係数。
        float UpdateHz = 10.0f;                       ///< 音響経路を更新する頻度（Hz）。
        uint32_t VoiceBudgetPerFrame = 32;            ///< 1 フレームに更新する音声数の上限。
        AcousticClassSettings Classes[static_cast<size_t>(ESoundClass::Count)]; ///< 分類順の設定。

        /// Acoustics.json と同じ曲線・分類別の値を含む既定設定を返す。
        static AcousticSettings MakeDefault();
        /// 分類別の設定を借用する。範囲外は Voice の設定を返す。
        const AcousticClassSettings& GetClass(ESoundClass soundClass) const;
    };

    /// 音響設定の読み込み結果。
    struct AcousticSettingsLoadResult
    {
        bool bSucceeded = false;   ///< 設定を反映できた場合に真。
        Container::String Detail;  ///< 失敗の理由と、検査対象のキー。
        /// 設定を反映できたかを返す。
        bool Succeeded() const { return bSucceeded; }
    };

    /// 256 KiB 以内の JSON を既定設定に重ねる。失敗時は outSettings を保持し、理由を返す。
    AcousticSettingsLoadResult ParseAcousticSettingsJson(const Container::String& json, AcousticSettings& outSettings);

    /// 音源から聞き手への経路。座標と音源レベルは有限値を指定する。
    struct AcousticPathQuery
    {
        Math::Vector3 Source;                      ///< 音源のワールド座標（m）。
        Math::Vector3 Listener;                    ///< 聞き手のワールド座標（m）。
        ESoundClass SoundClass = ESoundClass::Voice; ///< 評価する音源の分類。
        float SourceLevelDb = 60.0f;               ///< 基準距離での音源レベル。騒音床と同じ尺度（dB）。
        PhysicsQueryFilter Filter;                 ///< 静的幾何の照会条件。評価器は常にトリガーを除く。
    };

    /// 距離・遮蔽・騒音床を合成した結果。遮蔽の内訳は分類による免除前の値。
    struct AcousticPathResult
    {
        float DistanceM = 0.0f;                  ///< 音源から聞き手までの直線距離（m）。
        float GainDb = kAcousticSilenceDb;       ///< 距離と遮蔽を合わせた利得（dB、0 以下）。
        float HighFrequencyLossDb = 0.0f;        ///< 高域だけに追加する損失（dB、0 以上）。
        float StaticOcclusionDb = 0.0f;          ///< 静的幾何による遮蔽損失（dB）。
        float VegetationLossDb = 0.0f;           ///< 草による透過損失（dB）。
        float TerrainOcclusionDb = 0.0f;         ///< 地形による遮蔽損失（dB）。
        float NoiseFloorDb = kAcousticSilenceDb; ///< 聞き手に達する騒音床（dB）。
        bool bLineOfSight = true;                ///< 見通しを遮る寄与源がなければ真。
    };

    /// 寄与の合成先。マスキングは全ての遮蔽を計算してから適用する。
    enum class EAcousticMediumKind : uint8_t { StaticGeometry, Vegetation, Terrain, Masking };

    /// ひとつの寄与を求める間だけ有効な入力。
    struct AcousticContributionContext
    {
        const AcousticPathQuery& Query;       ///< トリガーを除外した経路の照会条件。
        const AcousticSettings& Settings;    ///< 評価器の現在の設定。
        const AcousticClassSettings& Class;  ///< 照会した音源の分類別設定。
        Math::Vector3 Direction;             ///< 音源から聞き手への単位ベクトル。距離 0 なら零ベクトル。
        float DistanceM;                     ///< 経路の長さ（m）。
        const AcousticPathResult& Partial;   ///< 合成途中の結果。マスキングでは遮蔽の適用後の値。
    };

    /// ひとつの寄与源による経路への作用。
    struct AcousticContribution
    {
        float BroadbandLossDb = 0.0f;            ///< 全帯域の透過損失（dB、0 以上）。
        float HighFrequencyLossDb = 0.0f;        ///< 高域だけの追加損失（dB、0 以上）。
        float NoiseFloorDb = kAcousticSilenceDb; ///< マスキングで与える騒音床（dB）。
        bool bBlocksLineOfSight = false;         ///< この寄与源が見通しを遮る場合に真。
    };

    /// GameThread 上で経路に対する遮蔽またはマスキングを計算する寄与源。
    class IAcousticMediumContributor
    {
    public:
        /// 評価器への登録を外してから破棄する。
        virtual ~IAcousticMediumContributor() = default;
        /// 寄与の合成先を返す。登録中は同じ種類を返すこと。
        virtual EAcousticMediumKind GetKind() const = 0;
        /// 指定の経路での寄与を既定値から書き込む。同じ入力で同じ値を返し、登録を変更しない。
        virtual void Contribute(const AcousticContributionContext& context, AcousticContribution& outContribution) const = 0;
    };

    /// 基準距離と距離（m）から [−120, 0] dB の利得を求める。無効な基準距離は 1 m、距離が NaN なら無音。
    float ComputeAcousticDistanceGainDb(float referenceDistanceM, float distanceM) noexcept;
    /// 開けた地面で可聴半径（m）にちょうど達したときの利得を返す。これ以上の GainDb なら聞こえる。
    float ComputeHearingRadiusThresholdDb(float referenceDistanceM, float radiusM) noexcept;
    /// 有効な設定の周波数範囲を対数補間して遮断周波数（Hz）を返す。損失が NaN なら上限。
    float ComputeHighFrequencyCutoffHz(const AcousticSettings& settings, float highFrequencyLossDb) noexcept;
    /// X が狭義昇順の曲線を線形補間する。範囲外は端の値、NaN は先頭、空なら無音を返す。
    float EvaluateAcousticCurveDb(Container::Span<const AcousticCurvePoint> curve, float x) noexcept;

    /// GameThread で聞き手の位置の雨の遮蔽率を返す。寿命は登録側が持つ。
    class IRainShelterSource
    {
    public:
        virtual ~IRainShelterSource() = default;
        /// 開けた空は 0、完全に覆われた場所は 1 を返す。
        virtual float QueryShelter01(const Math::Vector3& position) const = 0;
    };

    /// G11 の雨の遮蔽は GPU の深度マップで、CPU から屋根の下かを引く口が今は無い。既定は開けた空（0）。
    class OpenSkyRainShelterSource final : public IRainShelterSource
    {
    public:
        float QueryShelter01(const Math::Vector3&) const override { return 0.0f; }
    };

    /// 雨量・風速と雨の遮蔽率から、聞き手の高域の騒音床を求める。
    class AcousticNoiseFloorModel
    {
    public:
        /// 気象を借用する。未接続または無効な気象は雨量・風速ともに 0 とする。
        void SetEnvironment(const Environment::EnvironmentSystem* environment) { m_Environment = environment; }
        /// 雨の遮蔽率の供給元を借用する。nullptr で開けた空に戻す。
        void SetShelterSource(const IRainShelterSource* source) { m_Shelter = source; }
        float EvaluateHighFrequencyNoiseFloorDb(const Math::Vector3& listener, const AcousticSettings& settings) const;

    private:
        const Environment::EnvironmentSystem* m_Environment = nullptr;
        const IRainShelterSource* m_Shelter = nullptr;
        OpenSkyRainShelterSource m_OpenSky;
    };

    /// 評価器が借用する供給元。各欄は nullptr で解除でき、接続中の寿命は呼び出し側が持つ。
    struct AcousticSources
    {
        const Scene::SceneQuery* SceneQuery = nullptr;
        const SurfaceRegistry* Surfaces = nullptr;
        const Vegetation::Vegetation* Vegetation = nullptr;
        const Rendering::Terrain::TerrainSystem* Terrain = nullptr;
        const Environment::EnvironmentSystem* Environment = nullptr;
    };

    inline constexpr int32_t kAcousticStaticGeometryOrder = 100;
    inline constexpr int32_t kAcousticVegetationOrder = 200;
    inline constexpr int32_t kAcousticTerrainOrder = 300;
    inline constexpr int32_t kAcousticMaskingOrder = 1000;

    /// 経路上の地形を除く物理の当たりから、表面ごとの透過損失を求める。
    class StaticGeometryAcousticContributor final : public IAcousticMediumContributor
    {
    public:
        void SetSources(const AcousticSources& sources);
        EAcousticMediumKind GetKind() const override { return EAcousticMediumKind::StaticGeometry; }
        void Contribute(const AcousticContributionContext& context, AcousticContribution& outContribution) const override;

    private:
        const SceneQuery* m_SceneQuery = nullptr;
        const SurfaceRegistry* m_Surfaces = nullptr;
        const Rendering::Terrain::TerrainSystem* m_Terrain = nullptr;
        mutable Container::VariableArray<PhysicsQueryHit> m_Hits;
    };

    /// 草丈の範囲内を通る経路で、草の密度に応じた透過損失を積算する。
    class VegetationAcousticContributor final : public IAcousticMediumContributor
    {
    public:
        void SetVegetation(const Vegetation::Vegetation* vegetation) { m_Vegetation = vegetation; }
        EAcousticMediumKind GetKind() const override { return EAcousticMediumKind::Vegetation; }
        void Contribute(const AcousticContributionContext& context, AcousticContribution& outContribution) const override;

    private:
        const Vegetation::Vegetation* m_Vegetation = nullptr;
    };

    /// 経路が地表より下を通る最大深さから丘の遮蔽を求める。
    class TerrainAcousticContributor final : public IAcousticMediumContributor
    {
    public:
        void SetTerrain(const Rendering::Terrain::TerrainSystem* terrain) { m_Terrain = terrain; }
        EAcousticMediumKind GetKind() const override { return EAcousticMediumKind::Terrain; }
        void Contribute(const AcousticContributionContext& context, AcousticContribution& outContribution) const override;

    private:
        const Rendering::Terrain::TerrainSystem* m_Terrain = nullptr;
    };

    /// 遮蔽後の受音レベルと気象による騒音床の差から、高域のマスキングを求める。
    class RainMaskingAcousticContributor final : public IAcousticMediumContributor
    {
    public:
        void SetNoiseFloorModel(const AcousticNoiseFloorModel* model) { m_Model = model; }
        EAcousticMediumKind GetKind() const override { return EAcousticMediumKind::Masking; }
        void Contribute(const AcousticContributionContext& context, AcousticContribution& outContribution) const override;

    private:
        const AcousticNoiseFloorModel* m_Model = nullptr;
    };

    /**
     * GameThread 専用。シングルトンにせず Engine::Engine のメンバとして持つ。
     * 寄与源は借用で、登録した側が外すまで破棄しない（SceneQuery::BindPhysicsProvider と同じ寿命の規則）。
     * Contribute の中から登録を変えない。
     * 構築時に既定の 4 つの寄与源を登録する。個別に外すには対応する GetDefault…Contributor() の参照を
     * UnbindContributor に渡す。SetSources で供給元を変えても登録順や解除状態は保たれる。
     */
    class AcousticTransmissionEvaluator
    {
    public:
        /// 構築したスレッドを所有スレッドとし、既定設定で開始する。
        AcousticTransmissionEvaluator();
        /// GameThread 上で評価器を破棄する。寄与源の所有権は登録側にある。
        ~AcousticTransmissionEvaluator();
        /// 所有スレッドと借用登録を共有する複製を禁止する。
        AcousticTransmissionEvaluator(const AcousticTransmissionEvaluator&) = delete;
        /// 他の評価器からのコピー代入を禁止する。
        AcousticTransmissionEvaluator& operator=(const AcousticTransmissionEvaluator&) = delete;

        /// 経路を評価する。所有スレッド外や入力不正では理由を返し、例外時も出力を既定値に戻す。
        EPhysicsSceneQueryResult Evaluate(const AcousticPathQuery& query, AcousticPathResult& outResult) const;
        /// 所有スレッドで order の昇順、同順なら登録順に借用する。重複は AlreadyBound。
        EPhysicsSceneQueryResult BindContributor(IAcousticMediumContributor& contributor, int32_t order);
        /// 所有スレッドで登録を外す。空なら Unavailable、別の寄与源なら ProviderMismatch。
        EPhysicsSceneQueryResult UnbindContributor(IAcousticMediumContributor& contributor);

        /// 所有スレッドで既定の寄与源の供給元を差し替える。気象が未接続なら雨のマスキングは寄与しない。
        void SetSources(const AcousticSources& sources);
        AcousticNoiseFloorModel& GetNoiseFloorModel() { return m_NoiseFloor; }
        const AcousticNoiseFloorModel& GetNoiseFloorModel() const { return m_NoiseFloor; }
        StaticGeometryAcousticContributor& GetDefaultStaticGeometryContributor() { return m_StaticGeometry; }
        VegetationAcousticContributor& GetDefaultVegetationContributor() { return m_Vegetation; }
        TerrainAcousticContributor& GetDefaultTerrainContributor() { return m_Terrain; }
        RainMaskingAcousticContributor& GetDefaultRainMaskingContributor() { return m_RainMasking; }

        /// 所有スレッドで現在の設定を借用する。
        const AcousticSettings& GetSettings() const;
        /// 所有スレッドで検査済みの設定をコピーする。
        void SetSettings(const AcousticSettings& settings);
        /// 所有スレッドで JSON の設定を読み込む。失敗時は現設定を保持して理由を返す。
        AcousticSettingsLoadResult LoadSettingsFromJson(const Container::String& json);
        /// 所有スレッドでアセット相対パスから設定を読む。失敗時は現設定を保持して理由を返す。
        AcousticSettingsLoadResult LoadSettingsFromAssetFile(const Container::AnsiString& inputPath);

        /// 所有スレッドで分類別の距離利得（dB）を返す。範囲外の分類は Voice。
        float GetDistanceGainDb(ESoundClass soundClass, float distanceM) const;
        /// 所有スレッドで分類別の可聴半径（m）を利得のしきい値（dB）に換算する。
        float HearingRadiusToThresholdDb(ESoundClass soundClass, float radiusM) const;
        /// 所有スレッドで高域の追加損失（dB）を遮断周波数（Hz）に換算する。
        float HighFrequencyLossToCutoffHz(float highFrequencyLossDb) const;

    private:
        bool IsOwnerThread() const;
        struct ContributorEntry
        {
            IAcousticMediumContributor* Contributor;
            int32_t Order;
        };
        AcousticNoiseFloorModel m_NoiseFloor;
        StaticGeometryAcousticContributor m_StaticGeometry;
        VegetationAcousticContributor m_Vegetation;
        TerrainAcousticContributor m_Terrain;
        RainMaskingAcousticContributor m_RainMasking;
        Container::VariableArray<ContributorEntry> m_Contributors;
        AcousticSettings m_Settings;
        Thread::Thread::ThreadId m_OwnerThreadId;
    };
}
