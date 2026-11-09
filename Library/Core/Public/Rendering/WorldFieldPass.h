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

    /** @brief 最後に記録したフィールドの座標。uv = (worldXZ - OriginXZ) / ExtentMeters。 */
    struct WorldFieldState
    {
        double OriginXZ[2] = {};
        float ExtentMeters = 0.0f;
        float TexelSizeMeters = 0.0f;
        bool bValid = false;
    };

    /** @brief 主カメラに追従する世界空間のフィールドを更新し、今と前の画像を公開する。 */
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

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_UpdateShader;
        RHI::PipelinePtr m_UpdatePipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::TexturePtr m_HistoryTextures[2];
        // 次に書く側。最後に記録した「今」は常に 1 - m_HistoryWriteIndex。
        uint32_t m_HistoryWriteIndex = 0u;
        RHI::DescriptorSetPtr m_UpdateSet;
        RHI::DescriptorSetPtr m_ResetSet;
        WorldFieldSettings m_Settings;
        WorldFieldState m_FieldState;
        WorldFieldOrigin m_Origin;
        WorldFieldScrollResult m_PreparedScroll;
        WorldFieldUpdateConstants m_PreparedConstants;
        WorldFieldRange m_LastRange = WorldFieldRange::Range64m;
        WorldFieldRange m_PreparedRange = WorldFieldRange::Range64m;
        double m_LastUpdateTime = 0.0;
        double m_PreparedTime = 0.0;
        bool m_bPrepared = false;
    };
} // 名前空間 NorvesLib::Core::Rendering
