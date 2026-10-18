#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
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

    struct SkyAtmospherePassSettings
    {
        uint32_t TransmittanceWidth = 128;
        uint32_t TransmittanceHeight = 32;
        uint32_t RadianceWidth = 256;
        uint32_t RadianceHeight = 128;
        RHI::Format LutFormat = RHI::Format::R16G16B16A16_FLOAT;
        float SunDiskSafetyFraction = 0.9f;
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
        bool GenerateTransmittanceLut(const SkyAtmosphereModel& model);
        bool GenerateRadianceLut(const SkyAtmosphereModel& model);
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
        RHI::TexturePtr m_RadianceTexture;
        RHI::TexturePtr m_SunDiskTexture;
        RGTextureHandle m_TransmittanceHandle;
        RGTextureHandle m_RadianceHandle;
        RGTextureHandle m_SunDiskHandle;
        SkyAtmosphereParameters m_LastParameters;
        // 透過率LUT・放射輝度LUTを最後に作ったときのパラメータ。変わったときだけ作り直す。
        SkyAtmosphereParameters m_LutParameters;
        Math::Vector3 m_LutSunGroundTransmittance = Math::Vector3::Zero;
        bool m_bLutValid = false;
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
