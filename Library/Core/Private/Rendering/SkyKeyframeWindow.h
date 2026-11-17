// 太陽高度に応じた晴天キーフレームの選択と作成状態を管理する。
#pragma once

#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Rendering/IblGeneration.h"
#include "Rendering/SkyAtmosphere.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Thread
{
    class Task;
}

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t SkyKeyframeInvalidIndex = 0xFFFFFFFFu;

    /** 地平線付近を細かくした、狭義の昇順の既定高度表を度単位で返す。 */
    Container::VariableArray<float> MakeDefaultSkyKeyframeAltitudes();

    struct SkyKeyframeWindowSettings
    {
        Container::VariableArray<float> AltitudesDegrees; // 空なら既定表を使い、負の高度も指定できる。
        float JumpThresholdDegrees = 10.0f; // 一度の更新でこの値を超える移動を跳びとする。
        uint32_t MaxPendingBuilds = 2u; // 同時に作成中にできるキーの数。
    };

    struct SkyKeyframeSelection
    {
        uint32_t KeyA = SkyKeyframeInvalidIndex;
        uint32_t KeyB = SkyKeyframeInvalidIndex;
        float WeightA = 1.0f;
        float WeightB = 0.0f;
        bool bClamped = false; // 必要なキーが常駐していないため留まった。
        bool bValid = false; // 選択できる常駐キーがある。
    };

    /** RenderThread 専用の、装置やジョブに依存しないキーフレーム窓。錨が無い初期状態では設定表を使い、直前の高度を 0 度とする。 */
    class SkyKeyframeWindow final
    {
    public:
        /** 既定の設定で窓を作る。最初の錨はまだ無い。 */
        SkyKeyframeWindow();

        /**
         * 高度表は有限値の狭義の昇順で 2 個以上、閾値は有限かつ正、作成数の上限は 1 以上とする。
         * 空の表は既定表を使い、不正な項目は各既定値に置き換えて偽を返す。状態と錨を消して世代を進め、警告は保持する。
         */
        bool Configure(const SkyKeyframeWindowSettings& settings);

        /**
         * 有限な錨の高度を表に挿入する。同値があればその位置を使い、非有限なら 0 度を使う。
         * 状態を消して直前の高度と区間を錨に合わせ、世代を進め、錨の番号を返す。呼び手が錨を同期作成して常駐にする。
         */
        uint32_t Reset(float anchorAltitudeDegrees);

        /** 錨の有無、錨の番号、設定またはリセットごとに進む世代を返す。 */
        bool HasAnchor() const;
        uint32_t GetAnchorIndex() const;
        uint64_t GetGeneration() const;

        /** 実効表の個数と高度を返す。範囲外の高度は 0 度とする。 */
        size_t GetKeyCount() const;
        float GetKeyAltitude(uint32_t index) const;

        /** 実効表の番号が錨なら真を返す。範囲外は偽。 */
        bool IsAnchor(uint32_t index) const;

        /** 錨が無ければ真。錨があれば、非有限の入力は偽、閾値を超える移動は真。 */
        bool IsJump(float altitudeDegrees) const;

        /**
         * 高度を挟む区間を線形補間し、表の外では端のキーを使う。非有限の入力は直前の高度を使う。
         * 重みが 0 の未常駐キーは不要。必要なキーが無ければ常駐する区間の端、
         * どちらも無ければ最も近い常駐キーに留まる。同距離なら小さい番号を選び、常駐が無ければ無効を返す。
         * 留まったときは遅れの印を立て、結果と入力高度から求めた区間を保存する。
         */
        SkyKeyframeSelection Update(float altitudeDegrees);

        /**
         * 出力を空にし、直前の区間の下側から [-1, +2] の範囲で未常駐・未作成・未失敗の
         * 番号を直前の高度に近い順、同距離なら小さい番号順に入れる。作成中と出力の合計は上限以下とする。
         */
        void CollectRequests(Container::VariableArray<uint32_t>& outIndices) const;

        /** 出力を空にし、望む範囲と直前の選択の外にある常駐キーを入れる。 */
        void CollectEvictions(Container::VariableArray<uint32_t>& outIndices) const;

        /**
         * 範囲内のキーの作成中・常駐・失敗の印を操作する。
         * 常駐または失敗を記録すると作成中を外し、失敗は次の設定またはリセットまで保持する。
         * 退去は常駐だけを外す。範囲外の番号は無視する。
         */
        void MarkPending(uint32_t index);
        void MarkResident(uint32_t index);
        void MarkFailed(uint32_t index);
        void MarkEvicted(uint32_t index);

        /** 範囲内の常駐・作成中の状態と、作成中の総数を返す。 */
        bool IsResident(uint32_t index) const;
        bool IsPending(uint32_t index) const;
        size_t GetPendingCount() const;

        /** 遅れが発生していれば、この窓の寿命で一度だけ真を返す。 */
        bool ConsumeLateWarning();

    private:
        struct KeyState
        {
            bool bPending = false;
            bool bResident = false;
            bool bFailed = false;
        };

        void ClearKeyState();
        uint32_t FindIntervalLowerIndex(float altitudeDegrees) const;
        bool IsCloserKey(uint32_t left, uint32_t right, float altitudeDegrees) const;
        void GetDesiredRange(uint32_t& first, uint32_t& last) const;

        SkyKeyframeWindowSettings m_Settings;
        Container::VariableArray<float> m_KeyAltitudes;
        Container::VariableArray<KeyState> m_KeyStates;
        float m_LastAltitudeDegrees = 0.0f;
        uint32_t m_LastIntervalLowerIndex = 0u;
        SkyKeyframeSelection m_LastSelection;
        uint32_t m_AnchorIndex = SkyKeyframeInvalidIndex;
        uint64_t m_Generation = 0u;
        bool m_bLateWarningRaised = false;
        bool m_bLateWarningConsumed = false;
    };

    /** 生成後はモデルを変更せず、別のキーで再利用する。 */
    struct SkyKeyframeCpuData
    {
        SkyAtmosphereParameters Parameters;
        Container::TSharedPtr<const SkyAtmosphereModel> Model;
        Container::VariableArray<uint16_t> RadianceLutTexels;
        uint32_t RadianceWidth = 0u;
        uint32_t RadianceHeight = 0u;
        IblCpuData Ibl;
        SkyAerialProfile AerialProfile;
        Math::Vector3 SunGroundTransmittance = Math::Vector3::Zero;
        Math::Vector3 ClearHorizontalIlluminance = Math::Vector3::Zero;
        bool bValid = false;
    };

    /**
     * 晴天の鍵から CPU の結果を作り、失敗や例外では出力を空にする。
     * previous は変更しない。別々の出力と変更されない previous を使えば、複数のワーカーから同時に呼べる。
     */
    bool BuildSkyKeyframeCpuData(const SkyAtmosphereParameters& keyParameters,
                                 const SkyAtmosphereModel* previous,
                                 uint32_t radianceWidth,
                                 uint32_t radianceHeight,
                                 SkyKeyframeCpuData& outData);

    /** 装置へ転送済みの晴天キーフレーム。 */
    struct SkyKeyframeGpu
    {
        float AltitudeDegrees = 0.0f;
        float AzimuthDegrees = 0.0f;
        RHI::TexturePtr Radiance;
        RHI::TexturePtr Environment;
        RHI::TexturePtr Diffuse;
        RHI::TexturePtr Prefilter;
        float AverageIrradiance[3] = {};
        SkyAerialProfile AerialProfile;
        Math::Vector3 SunGroundTransmittance = Math::Vector3::Zero;
        Math::Vector3 ClearHorizontalIlluminance = Math::Vector3::Zero;
        Container::TSharedPtr<const SkyAtmosphereModel> Model;

        /** ４枚のテクスチャがそろっていれば真。 */
        bool IsValid() const;
    };

    /** RenderThread でコマンド記録の外から呼ぶ。失敗や例外では出力を空にする。 */
    bool UploadSkyKeyframe(RHI::IDevice* device, const SkyKeyframeCpuData& data, SkyKeyframeGpu& outKey);

    struct SkyKeyframeBuildResult
    {
        uint32_t KeyIndex = SkyKeyframeInvalidIndex;
        uint64_t Generation = 0u;
        Container::TSharedPtr<SkyKeyframeCpuData> Data; // 作成に失敗した場合は bValid が偽。
    };

    /**
     * RenderThread だけが投げ手を操作する。ワーカーは値で受け取った入力と結果だけに触れる。
     * JobSystem の終了で未実行の仕事が取り消された場合は、無効な結果として回収する。
     */
    class SkyKeyframeBuilder final
    {
    public:
        SkyKeyframeBuilder() = default;
        ~SkyKeyframeBuilder();
        SkyKeyframeBuilder(const SkyKeyframeBuilder&) = delete;
        SkyKeyframeBuilder& operator=(const SkyKeyframeBuilder&) = delete;

        void Submit(uint32_t keyIndex, uint64_t generation, const SkyAtmosphereParameters& keyParameters,
                    Container::TSharedPtr<const SkyAtmosphereModel> previous,
                    uint32_t radianceWidth, uint32_t radianceHeight);
        void CollectCompleted(Container::VariableArray<SkyKeyframeBuildResult>& outResults);
        void WaitAll();

        /** 完了済みでも、まだ回収していない仕事を含む件数。 */
        size_t GetPendingCount() const;
        /** 作成関数が終わった通算回数。未実行のまま取り消された仕事は含まない。 */
        uint64_t GetBuildCount() const;

    private:
        struct PendingBuild
        {
            SkyKeyframeBuildResult Result;
            Container::TSharedPtr<Thread::Task> Task;
        };

        Container::VariableArray<PendingBuild> m_PendingBuilds;
        uint64_t m_CollectedBuildCount = 0u;
    };
} // namespace NorvesLib::Core::Rendering の終端
