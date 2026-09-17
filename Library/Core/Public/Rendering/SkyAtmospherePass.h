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
                                  float preExposure);
        bool GenerateTransmittanceLut(const SkyAtmosphereParameters& parameters);
        bool GenerateRadianceLut(const SkyAtmosphereParameters& parameters);
        bool GenerateSunDiskTexture(const SkyAtmosphereParameters& parameters,
                                    float preExposure);
        bool CreateTexture(RHI::TexturePtr& outTexture,
                           uint32_t width,
                           uint32_t height,
                           const char* debugName);
        void PublishContextResources(ViewRenderContext& context, bool bValid);

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
        float m_LastSunDiskPreExposedLuminance = 0.0f;
        bool m_bLastSunDiskSaturated = false;
        bool m_bSnapshotEnabled = false;
        bool m_bPrepared = false;
    };

} // namespace NorvesLib::Core::Rendering
