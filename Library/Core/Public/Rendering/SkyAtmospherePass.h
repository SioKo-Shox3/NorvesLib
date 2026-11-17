#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Rendering/SkyAtmosphere.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class FrameResourceLane;
    class SkyAtmosphereSourceCache;
    class SkyCloudBlendRecorder;
    class GpuRetireQueue;

    /**
     * @brief 空の透過率 LUT の texel（RGBA の半精度。SkyAtmospherePass が GPU へ上げる値そのもの）を作る
     *
     * 横 x は天頂角の余弦 (x + 0.5) / width（0〜1、上の半球だけ）、縦 y は高度の割合 y / (height - 1)（0 が地表、1 が大気の上端）。
     * rgb は model.GetTransmittance の透過率、a は 1。行は y = 0 から。
     */
    void BuildSkyTransmittanceLutTexels(const SkyAtmosphereModel& model,
                                        uint32_t width,
                                        uint32_t height,
                                        Container::VariableArray<uint16_t>& outTexels);

    /**
     * @brief 空の放射輝度 LUT の texel（RGBA の半精度。SkyAtmospherePass が GPU へ上げる値そのもの）を作る
     *
     * 正距円筒: texel の中心 u = (x + 0.5) / width、v = (y + 0.5) / height の向きは
     * (cos(φ)cos(λ), -sin(φ), cos(φ)sin(λ))、λ = (u - 0.5) 2π、φ = (v - 0.5) π（v = 0 が天頂）。
     * rgb は model.SampleSkyView の放射輝度（cd/m²）、a は太陽円盤（SolarDiskSolidAngleSteradians）の中なら 1。
     */
    void BuildSkyRadianceLutTexels(const SkyAtmosphereModel& model,
                                   uint32_t width,
                                   uint32_t height,
                                   Container::VariableArray<uint16_t>& outTexels);

    /**
     * @brief 空気遠近の高度ごとの表（SkyAerialProfile）を空の model から作る
     *
     * 太陽への透過率は model.LookupSunTransmittanceAndMultipleScattering の直接の積分、Ψ_ms は空のレイマーチと同じ表の引き方。
     * 空が無効なら bValid が偽。
     */
    void BuildSkyAerialProfile(const SkyAtmosphereModel& model, SkyAerialProfile& outProfile);

    struct SkyAtmospherePassSettings
    {
        uint32_t TransmittanceWidth = 128;
        uint32_t TransmittanceHeight = 32;
        uint32_t RadianceWidth = 256;
        uint32_t RadianceHeight = 128;
        RHI::Format LutFormat = RHI::Format::R16G16B16A16_FLOAT;
        float SunDiskSafetyFraction = 0.9f;
        // 晴天のキーフレームの太陽高度の表（度、狭義の昇順）。空なら地平線付近を細かくした既定表を使う。
        Container::VariableArray<float> KeyframeAltitudesDegrees;
        // 1 フレームでこの角度を超えて太陽高度が動いたら、今の高度でキーフレームを作り直す。
        float KeyframeJumpThresholdDegrees = 10.0f;
    };

    /**
     * @brief 空スナップショットから空LUTと太陽ディスクを生成する前段パス。
     *
     * CPUで生成した固定解像度の参照LUTをRenderGraphのnamed resourceへ公開し、
     * LightingPassが同一スナップショットを入力として読む。空が無効な場合は
     * named resourceを公開せず、LightingPass側の既存フォールバックへ戻す。
     */
    class SkyAtmospherePass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit SkyAtmospherePass(
            const SkyAtmospherePassSettings& settings = SkyAtmospherePassSettings{});
        ~SkyAtmospherePass() override;

        const char* GetName() const override { return "SkyAtmospherePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context) override;

        const SkyAtmospherePassSettings& GetSettings() const { return m_Settings; }
        const SkyAtmosphereParameters& GetLastParameters() const { return m_LastParameters; }
        float GetLastSunDiskPreExposedLuminance() const
        {
            return m_LastSunDiskPreExposedLuminance;
        }
        bool WasLastSunDiskSaturated() const { return m_bLastSunDiskSaturated; }
        /** @brief 保持体・ワーカー・同期の錨が空のモデルを構築した通算回数の合計を返す。 */
        uint64_t GetSkyModelBuildCount() const;

        RGResourceHandle GetTransmittanceHandle() const
        {
            return m_TransmittanceHandle.ToResourceHandle();
        }

        RGResourceHandle GetRadianceHandle() const
        {
            return m_RadianceHandle.ToResourceHandle();
        }

        RGResourceHandle GetSunDiskHandle() const
        {
            return m_SunDiskHandle.ToResourceHandle();
        }

    private:
        const SkyAtmosphereParameters& ResolveSnapshot(const ViewRenderContext& context) const;
        float ResolvePreExposure(const ViewRenderContext& context) const;
        bool EnsureResources();
        bool GenerateLutResources(const SkyAtmosphereParameters& parameters,
                                  float preExposure,
                                  FrameResourceLane* frameResources);
        void GenerateUnitOvercastIbl(const SkyAtmosphereParameters& parameters);
        bool EnsureCloudBlendOutput(RHI::TexturePtr& output,
                                    const RHI::TexturePtr& clear,
                                    const char* name);
        bool PrepareCloudBlend(const SkyAtmosphereParameters& parameters, FrameResourceLane& lane);
        void LogSunSweep();
        void DiscardCloudBlend();
        /** @brief 完了したフレームまでの古い IBL を破棄し、このフレームの保持期限を設定する。 */
        void AdvanceRetireQueue(const ViewRenderContext& context);
        bool GenerateTransmittanceLut(const SkyAtmosphereModel& model);
        bool GenerateSunDiskTexture(const SkyAtmosphereParameters& parameters,
                                    float preExposure,
                                    FrameResourceLane* frameResources);
        bool CreateTexture(RHI::TexturePtr& outTexture,
                           uint32_t width,
                           uint32_t height,
                           const char* debugName);
        void PublishContextResources(ViewRenderContext& context, bool bValid);
        bool AreGraphResourcesResolved(RenderGraphResources& resources) const;

        SkyAtmospherePassSettings m_Settings;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_Sampler;
        RHI::TexturePtr m_TransmittanceTexture;
        // 今の選択で重みの大きい晴天キーの放射輝度 LUT への共有参照。
        RHI::TexturePtr m_RadianceTexture;
        RHI::TexturePtr m_SunDiskTexture;
        RGTextureHandle m_TransmittanceHandle;
        RGTextureHandle m_RadianceHandle;
        RGTextureHandle m_SunDiskHandle;
        SkyAtmosphereParameters m_LastParameters;
        Math::Vector3 m_LutSunGroundTransmittance = Math::Vector3::Zero;
        // 晴天キーの表を混ぜ、直達光へ雲の係数を掛けた公開用の空気遠近の表。
        SkyAerialProfile m_AerialProfile;
        // 透過率 LUT が今の大気に対応する場合に真。
        bool m_bLutValid = false;
        struct KeyframeState;
        // キーフレーム窓とワーカーの結果を RenderThread で管理する。
        Container::TUniquePtr<KeyframeState> m_Keyframes;
        // 単位曇天の放射輝度の源を保持する。
        Container::TUniquePtr<SkyAtmosphereSourceCache> m_SourceCache;
        // 差し替えたテクスチャを、読んだフレームが GPU で終わるまで預ける。
        Container::TUniquePtr<GpuRetireQueue> m_RetireQueue;
        // 今の選択で重みの大きい晴天キーの環境・拡散照度・鏡面への共有参照。
        RHI::TexturePtr m_IblEnvironmentTexture;
        RHI::TexturePtr m_IblDiffuseIrradianceTexture;
        RHI::TexturePtr m_IblPrefilteredSpecularTexture;
        // 強度を掛ける前の拡散照度の全球の平均照度。
        float m_IblAverageIrradiance[3] = {};
        // IBL の 3 枚がすべて作れた場合に真。
        bool m_bIblValid = false;
        // 単位曇天の環境・拡散・鏡面。環境は放射輝度 LUT の混ぜにも使う。
        RHI::TexturePtr m_UnitOvercastTextures[3];
        float m_UnitOvercastAverageIrradiance[3] = {};
        uint64_t m_UnitOvercastIblBuildCount = 0u;
        bool m_bUnitOvercastIblAttempted = false;
        bool m_bUnitOvercastWarningLogged = false;
        bool m_bMissingOvercastWarningLogged = false;
        // 永続出力は放射輝度 LUT・環境・拡散・鏡面の順。雲量だけでは差し替えない。
        RHI::TexturePtr m_CloudBlendOutputs[4];
        RGResourceHandle m_CloudBlendHandles[4];
        float m_CloudBlendAverageIrradiance[3] = {};
        Container::TUniquePtr<SkyCloudBlendRecorder> m_CloudBlend;
        // Declare から Execute までの準備を捨てるときだけ使う借用口。
        FrameResourceLane* m_CloudBlendLane = nullptr;
        bool m_bCloudBlendDirty = true;
        bool m_bUseCloudBlend = false;
        float m_LastSunDiskPreExposedLuminance = 0.0f;
        bool m_bLastSunDiskSaturated = false;
        // 太陽の円盤は事前露出でフレームごとに変わるので、フレームの持ち主から借りた領域に書き、
        // グラフの記録の中でコピーする。借りた切れ端はこのフレームの Execute でだけ使う。
        // texture へのコピーは参照の範囲を元のバッファの終わりまで取るので、借りた切れ端から GPU だけが使う
        // 中継のバッファへ大きさどおりに写してから texture へ写す（同じページの後の切れ端を参照済みにしない）。
        RHI::BufferPtr m_SunDiskStagingBuffer;
        RHI::BufferPtr m_SunDiskUploadBuffer;
        uint64_t m_SunDiskUploadOffset = 0;
        bool m_bSunDiskCopyPending = false;
        // 作った直後の texture は layout が未定義なので、最初の 1 回だけ同期の書き込みで読み取りの layout にする。
        bool m_bSunDiskTextureInitialized = false;
        bool m_bSnapshotEnabled = false;
        bool m_bPrepared = false;
    };

} // namespace NorvesLib::Core::Rendering
