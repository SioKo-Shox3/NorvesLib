// 空の遮蔽クリップマップの配置と描画パスの公開型を定める。
#pragma once

#include "Container/VariableArray.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/SkyOcclusion.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t SkyOcclusionMaxLevelCount = 4u;

    /** @brief 空の遮蔽クリップマップを更新するシェーダーの共通定数。 */
    struct alignas(16) SkyOcclusionMapGpuParams
    {
        // 段数、解像度、作業タイル数、予約欄。
        uint32_t counts[4] = {};
        // 地面高、受け手の高さ、積分距離の上限、予約欄。
        float heights[4] = {};
        // 各段の窓の原点 XZ と予約欄 2 つ。
        int32_t windowOrigin[4][4] = {};
        // 各段のテクセルの幅。未使用の段は 0。
        float texelMeters[4] = {};
        // 地面の高さのワールド座標から UV への尺度と加算値。
        float groundUv[4] = {};
        // 下草の層のワールド座標から UV への尺度と加算値。
        float undergrowthUv[4] = {};
        // 空の明るさの重みの定数項と傾き。残りは予約欄。
        float skyWeight[4] = {};
        // 地面・下草・世界の場の入力の有無と予約欄。
        float inputs[4] = {};
        // 世界の場の窓。
        float worldField[4] = {};
    };

    static_assert(sizeof(SkyOcclusionMapGpuParams) == 192u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, counts) == 0u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, heights) == 16u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, windowOrigin) == 32u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, texelMeters) == 96u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, groundUv) == 112u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, undergrowthUv) == 128u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, skyWeight) == 144u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, inputs) == 160u);
    static_assert(offsetof(SkyOcclusionMapGpuParams, worldField) == 176u);

    /**
     * @brief 8×8 ワールドテクセルの作業と遮蔽物の添字列の位置。
     *
     * 添字列の ListOffset にある 1 語が件数で、その後に遮蔽物の添字が続く。
     */
    struct SkyOcclusionWorkTile
    {
        int32_t TileX = 0;
        int32_t TileZ = 0;
        uint32_t Level = 0u;
        uint32_t ListOffset = 0u;
    };

    static_assert(sizeof(SkyOcclusionWorkTile) == 16u);
    static_assert(offsetof(SkyOcclusionWorkTile, TileX) == 0u);
    static_assert(offsetof(SkyOcclusionWorkTile, TileZ) == 4u);
    static_assert(offsetof(SkyOcclusionWorkTile, Level) == 8u);
    static_assert(offsetof(SkyOcclusionWorkTile, ListOffset) == 12u);

    /** @brief 段 level の幅 BaseTexelMeters × 4^level をメートルで返す。 */
    float GetSkyOcclusionLevelTexelMeters(const SkyOcclusionParameters& params, uint32_t level);

    /** @brief 窓の最小角を各軸で floor(基準点 / テクセル幅) − Resolution / 2 として求める。 */
    void ComputeSkyOcclusionWindowOrigin(const SkyOcclusionParameters& params,
                                         uint32_t level,
                                         float anchorX,
                                         float anchorZ,
                                         int32_t& outX,
                                         int32_t& outZ);

    /** @brief 負の値も含むワールドテクセルを正の剰余でトロイダル番地へ写す。 */
    uint32_t WrapSkyOcclusionTexel(int32_t worldTexel, uint32_t resolution);

    /** @brief 積分で受け手と同じ段を使う距離 D_k = Resolution × テクセル幅 / 8 を返す。 */
    float GetSkyOcclusionOwnLevelReach(const SkyOcclusionParameters& params, uint32_t level);

    struct SkyOcclusionMapPassSettings
    {
        uint32_t RecenterThresholdTexels = 16u;
    };

    /** @brief 空の遮蔽クリップマップを更新し、照明へ公開するパス。 */
    class SkyOcclusionMapPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit SkyOcclusionMapPass(
            const SkyOcclusionMapPassSettings& settings = SkyOcclusionMapPassSettings{});
        ~SkyOcclusionMapPass() override;

        const char* GetName() const override { return "SkyOcclusionMapPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        friend struct SkyOcclusionMapVulkanTestAccess;

        struct PreparedDispatch
        {
            RHI::DescriptorSetPtr DescriptorSet;
            uint32_t TileCount = 0u;
        };

        bool EnsureTextures(const SkyOcclusionParameters& parameters);
        bool PrepareUpdate(const ViewRenderContext& context);
        void PublishContextResources(RenderGraphResources& resources, ViewRenderContext& context) const;

        SkyOcclusionMapPassSettings m_Settings;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_SplatShader;
        RHI::ShaderPtr m_IntegrateShader;
        RHI::PipelinePtr m_SplatPipeline;
        RHI::PipelinePtr m_IntegratePipeline;
        RHI::DescriptorSetLayoutPtr m_SplatDescriptorSetLayout;
        RHI::DescriptorSetLayoutPtr m_IntegrateDescriptorSetLayout;
        RHI::SamplerPtr m_Sampler;
        RHI::SamplerPtr m_GroundSampler;
        RHI::TexturePtr m_GroundTexture;
        RHI::TexturePtr m_EmptyGroundTexture;
        RHI::TexturePtr m_PreparedGroundTexture;
        RHI::TexturePtr m_UndergrowthInputTexture;
        RHI::TexturePtr m_EmptyUndergrowthTexture;
        RHI::TexturePtr m_PreparedUndergrowthTexture;
        RHI::TexturePtr m_UndergrowthTexture;
        RHI::TexturePtr m_OccludersTexture;
        RHI::TexturePtr m_VisibilityTexture;
        RHI::TexturePtr m_AuxTexture;
        RGResourceHandle m_GroundHandle;
        RGResourceHandle m_UndergrowthInputHandle;
        RGResourceHandle m_EmptyUndergrowthHandle;
        RGResourceHandle m_UndergrowthHandle;
        RGTextureHandle m_WorldFieldHandle;
        RGTextureHandle m_OccludersHandle;
        RGTextureHandle m_VisibilityHandle;
        RGTextureHandle m_AuxHandle;
        float m_AnchorX = 0.0f;
        float m_AnchorZ = 0.0f;
        int32_t m_WindowOrigin[SkyOcclusionMaxLevelCount][2] = {};
        Container::VariableArray<SkyOccluderProxy> m_LastOccluders;
        Container::VariableArray<SkyOccluderProxy> m_PreparedOccluders;
        Container::VariableArray<SkyOccluderProxy> m_LastDynamicOccluders;
        Container::VariableArray<SkyOccluderProxy> m_PreparedDynamicOccluders;
        Container::VariableArray<SkyOccluderChunkRange> m_LastChunkRanges;
        Container::VariableArray<SkyOccluderChunkRange> m_PreparedChunkRanges;
        Container::VariableArray<PreparedDispatch> m_SplatDispatches;
        Container::VariableArray<PreparedDispatch> m_IntegrateDispatches;
        SkyOcclusionParameters m_PreparedParameters;
        SkyOcclusionFieldLayout m_PreparedGroundLayout;
        FrameUploadAllocation m_PreparedGroundUpload;
        SkyOcclusionFieldLayout m_PreparedUndergrowthLayout;
        FrameUploadAllocation m_PreparedUndergrowthUpload;
        FrameUploadAllocation m_PreparedEmptyUndergrowthUpload;
        uint64_t m_PreparedUndergrowthLayoutRevision = 0u;
        uint64_t m_PreparedUndergrowthRevision = 0u;
        bool m_bPreparedHasUndergrowth = false;
        uint64_t m_PreparedGroundLayoutRevision = 0u;
        uint64_t m_PreparedGroundHeightsRevision = 0u;
        float m_PreparedSkyWeight[2] = {};
        bool m_bPreparedFullSkyIntegration = false;
        bool m_bPreparedHasGround = false;
        int32_t m_PreparedWindowOrigin[SkyOcclusionMaxLevelCount][2] = {};
        float m_PreparedTexelMeters[SkyOcclusionMaxLevelCount] = {};
        float m_PreparedAnchorX = 0.0f;
        float m_PreparedAnchorZ = 0.0f;
        uint64_t m_PreparedRevision = 0u;
        FrameResourceGeneration m_LastUpdateSubmission;
        uint64_t m_LastSkyOcclusionRevision = 0u;
        SkyOcclusionParameters m_LastParameters;
        SkyOcclusionFieldLayout m_LastGroundLayout;
        uint64_t m_LastGroundLayoutRevision = 0u;
        uint64_t m_LastGroundHeightsRevision = 0u;
        float m_LastSkyWeight[2] = {};
        SkyOcclusionFieldLayout m_LastUndergrowthLayout;
        uint64_t m_LastUndergrowthLayoutRevision = 0u;
        uint64_t m_LastUndergrowthRevision = 0u;
        bool m_bLastHasUndergrowth = false;
        bool m_bEmptyUndergrowthInitialized = false;
        bool m_bLastHasGround = false;
        bool m_bEmptyGroundInitialized = false;
        bool m_bHasAnchor = false;
        bool m_bHasValidData = false;
        bool m_bPrepared = false;
        bool m_bWarnedInvalidChunkRanges = false;
    };
} // namespace NorvesLib::Core::Rendering
