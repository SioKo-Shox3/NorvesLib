// FramePacketのRTスナップショットを使う独立パストレーシングパス。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief パストレーサーの検証出力
     *
     * None以外では、1次命中面の材質値を放射輝度の代わりに累積画像へ書く。
     */
    enum class PathTracingDebugOutput : uint32_t
    {
        None = 0,
        Albedo = 1,
        ShadingNormal = 2,
        MetallicRoughness = 3
    };

    /** @brief パストレーサーが1フレームで束ねる材質textureの上限（重複を除いた数） */
    inline constexpr uint32_t PathTracingMaterialTextureCapacity = 256u;

    class PathTracingPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "PathTracingPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        uint32_t GetAccumulatedSampleCount() const;
        RHI::TexturePtr GetAccumulatedTexture() const;

        /** @brief 検証出力を切り替える。変更すると累積履歴を捨てる。 */
        void SetDebugOutput(PathTracingDebugOutput output) { m_DebugOutput = output; }

        /** @brief 直近フレームで束ねた材質texture数（先頭の既定texture4個を含む） */
        uint32_t GetBoundMaterialTextureCount() const { return m_BoundMaterialTextureCount; }

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::BufferPtr InstanceBuffer;
            uint64_t InstanceBufferCapacity = 0u;
            RHI::AccelerationStructurePtr MotionTopLevel;
            uint32_t MotionInstanceCapacity = 0u;
            /** @brief このフレームの材質texture表。先頭4要素は既定texture。 */
            Container::VariableArray<RHI::TexturePtr> MaterialTextures;
        };

        struct History
        {
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint32_t Width = 0u;
            uint32_t Height = 0u;
            uint32_t CurrentIndex = 1u;
            uint32_t SampleCount = 0u;
            uint64_t SceneRevision = 0u;
            uint64_t LightRevision = 0u;
            uint64_t CameraSignature = 0u;
            uint64_t GeometrySignature = 0u;
            uint64_t SkySignature = 0u;
            uint64_t FogSignature = 0u;
            /** @brief 解決後の材質texture実体と各instanceの表番号の署名 */
            uint64_t MaterialTextureSignature = 0u;
            PathTracingDebugOutput DebugOutput = PathTracingDebugOutput::None;
            bool bSkyValid = false;
            RHI::TexturePtr Textures[2];
            RHI::ResourceState TextureStates[2] = {
                RHI::ResourceState::Undefined, RHI::ResourceState::Undefined};
            Container::VariableArray<FrameResources> FrameSlots;
        };

        History* FindOrCreateHistory(const ViewRenderContext& context, uint32_t width, uint32_t height);
        FrameResources* FindOrCreateFrameResources(const ViewRenderContext& context,
                                                  History& history);
        bool PrepareInstances(const ViewRenderContext& context,
                              FrameResources& frameResources);

        Container::VariableArray<History> m_Histories;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_RayGenerationShader;
        RHI::ShaderPtr m_MissShader;
        RHI::ShaderPtr m_ClosestHitShader;
        RHI::SamplerPtr m_Sampler;
        /** @brief 材質texture用のsampler（GBufferと同じWrap・異方性） */
        RHI::SamplerPtr m_MaterialSampler;
        /** @brief GBufferと同じ既定texture（白アルベド・平坦法線・metallic 0・roughness中間灰） */
        RHI::TexturePtr m_DefaultWhiteTexture;
        RHI::TexturePtr m_DefaultFlatNormalTexture;
        RHI::TexturePtr m_DefaultBlackTexture;
        RHI::TexturePtr m_DefaultMidGrayTexture;
        PathTracingDebugOutput m_DebugOutput = PathTracingDebugOutput::None;
        uint32_t m_BoundMaterialTextureCount = 0u;
        bool m_bMaterialTextureOverflowReported = false;
        RGTextureHandle m_OutputHandle;
        RGTextureHandle m_SkyRadianceHandle;
        RGTextureHandle m_SkyTransmittanceHandle;
        RGTextureHandle m_SunDiskHandle;
        uint32_t m_ActiveHistoryIndex = UINT32_MAX;
        uint32_t m_ActiveFrameResourceIndex = UINT32_MAX;
        uint32_t m_TargetIndex = 0u;
        uint64_t m_DeclaredCameraSignature = 0u;
        uint64_t m_DeclaredGeometrySignature = 0u;
        uint64_t m_DeclaredSkySignature = 0u;
        uint64_t m_DeclaredFogSignature = 0u;
        uint64_t m_DeclaredMaterialTextureSignature = 0u;
        bool m_bPrepared = false;
    };
}
