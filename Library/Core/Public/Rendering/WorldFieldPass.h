#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/WorldFieldTypes.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t WorldFieldUpdateGroupSize = 8u;
    inline constexpr uint32_t WorldFieldUpdateGroupCount =
        (WorldFieldResolution + WorldFieldUpdateGroupSize - 1u) / WorldFieldUpdateGroupSize;

    inline constexpr uint32_t WorldFieldDrynessGroupSize = 8u;
    inline constexpr uint32_t WorldFieldDrynessGroupCount =
        (WorldFieldDrynessResolution + WorldFieldDrynessGroupSize - 1u) / WorldFieldDrynessGroupSize;

    /** @brief WorldFieldUpdate.comp の binding 3 に渡す std140 定数。 */
    struct alignas(16) WorldFieldUpdateConstants
    {
        int32_t Shift[2] = {};
        uint32_t Resolution[2] = {WorldFieldResolution, WorldFieldResolution};
        float OriginXZ[2] = {};
        float TexelSize = 0.0f;
        float Decay = 1.0f;
        uint32_t InteractorCount = 0u;
        uint32_t bResetHistory = 1u;
        uint32_t Padding[2] = {};
    };
    static_assert(sizeof(WorldFieldUpdateConstants) == 48);
    static_assert(offsetof(WorldFieldUpdateConstants, Shift) == 0);
    static_assert(offsetof(WorldFieldUpdateConstants, Resolution) == 8);
    static_assert(offsetof(WorldFieldUpdateConstants, OriginXZ) == 16);
    static_assert(offsetof(WorldFieldUpdateConstants, TexelSize) == 24);
    static_assert(offsetof(WorldFieldUpdateConstants, Decay) == 28);
    static_assert(offsetof(WorldFieldUpdateConstants, InteractorCount) == 32);
    static_assert(offsetof(WorldFieldUpdateConstants, bResetHistory) == 36);
    static_assert(offsetof(WorldFieldUpdateConstants, Padding) == 40);

    /** @brief WorldFieldDryness.comp の binding 2 に渡す std140 定数。 */
    struct alignas(16) WorldFieldDrynessParams
    {
        float NearOriginXZ[2] = {};
        float NearTexelSize = 0.0f;
        uint32_t SourceCount = 0u;
        float FarOriginXZ[2] = {};
        float FarTexelSize = 0.0f;
        uint32_t Resolution = WorldFieldDrynessResolution;
    };
    static_assert(alignof(WorldFieldDrynessParams) == 16);
    static_assert(sizeof(WorldFieldDrynessParams) == 32);
    static_assert(offsetof(WorldFieldDrynessParams, NearOriginXZ) == 0);
    static_assert(offsetof(WorldFieldDrynessParams, NearTexelSize) == 8);
    static_assert(offsetof(WorldFieldDrynessParams, SourceCount) == 12);
    static_assert(offsetof(WorldFieldDrynessParams, FarOriginXZ) == 16);
    static_assert(offsetof(WorldFieldDrynessParams, FarTexelSize) == 24);
    static_assert(offsetof(WorldFieldDrynessParams, Resolution) == 28);

    /** @brief WorldFieldDryness.comp の binding 3 に渡す源の配列。 */
    struct WorldFieldDrynessSourceBlock
    {
        WorldFieldDrynessSource Sources[WorldFieldDrynessMaxSources];
    };
    static_assert(sizeof(WorldFieldDrynessSourceBlock) == 4096);

    /** @brief 主カメラに追従する世界空間のフィールドを更新し、画像と座標を公開する。 */
    class WorldFieldPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        ~WorldFieldPass() override;
        const char* GetName() const override { return "WorldFieldPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        void SetSettings(const WorldFieldSettings& settings);
        const WorldFieldSettings& GetSettings() const { return m_Settings; }
        WorldFieldState GetFieldState() const;

    private:
        bool EnsureTextures();
        bool PrepareUpdate(const ViewRenderContext& context);
        void RecordUpdate(ViewRenderContext& context);
        bool EnsureDrynessTextures();
        bool PrepareDryness(const ViewRenderContext& context);
        void RecordDryness(ViewRenderContext& context);
        static WorldFieldState MakeDrynessState(const WorldFieldScrollResult& scroll, float texelSize);
        WorldFieldFrameState GetFrameState(uint32_t& currentIndex, uint32_t& previousIndex) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_UpdateShader;
        RHI::PipelinePtr m_UpdatePipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::TexturePtr m_HistoryTextures[2];
        // 次に書く側。最後に記録した「今」は常に 1 - m_HistoryWriteIndex。
        uint32_t m_HistoryWriteIndex = 0u;
        RHI::DescriptorSetPtr m_UpdateSet;
        RHI::DescriptorSetPtr m_ResetSet;
        RHI::ShaderPtr m_DrynessShader;
        RHI::PipelinePtr m_DrynessPipeline;
        RHI::DescriptorSetLayoutPtr m_DrynessDescriptorSetLayout;
        // 0 は近傍、1 は遠景。枯れ度は履歴を持たない。
        RHI::TexturePtr m_DrynessTextures[2];
        RHI::DescriptorSetPtr m_DrynessSet;
        WorldFieldState m_PreparedDryness[2];
        WorldFieldState m_DrynessStates[2];
        WorldFieldSettings m_Settings;
        // 各画像に実際に記録した座標。宣言だけでは更新しない。
        WorldFieldState m_HistoryStates[2];
        WorldFieldOrigin m_Origin;
        WorldFieldScrollResult m_PreparedScroll;
        WorldFieldUpdateConstants m_PreparedConstants;
        WorldFieldRange m_LastRange = WorldFieldRange::Range64m;
        WorldFieldRange m_PreparedRange = WorldFieldRange::Range64m;
        double m_LastUpdateTime = 0.0;
        double m_PreparedTime = 0.0;
        bool m_bPrepared = false;
        bool m_bDrynessReady = false;
        bool m_bDrynessPrepared = false;
    };
} // 名前空間 NorvesLib::Core::Rendering
