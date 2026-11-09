#pragma once

#include "Rendering/GrassGenerateReference.h"
#include "Rendering/GrassFieldGpuWindow.h"
#include "Rendering/GrassTileSelector.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;
    class MegaGeometryPass;

    /** @brief 記述子へ渡すバッファの範囲。フレームから借りたオフセットも含む。 */
    struct GrassGenerateBufferRange
    {
        RHI::BufferPtr Buffer;
        uint32_t Offset = 0;
        uint32_t Size = 0;
    };

    /** @brief 草の生成の 1 回の記録に使う、呼び出し側が用意した資源。 */
    struct GrassGenerateRecordInputs
    {
        RHI::DescriptorSetPtr DescriptorSet;
        GrassGenerateBufferRange Constants;
        GrassGenerateBufferRange Tiles;
        GrassGenerateBufferRange Species;
        RHI::TexturePtr LayerA;
        RHI::TexturePtr LayerB;
        RHI::TexturePtr HiZ;
        // HZB を使わない生成でも、束縛 7 には読み取り可能な 2D テクスチャが要る。
        RHI::TexturePtr FallbackHiZ;
        RHI::SamplerPtr Sampler;
        uint32_t TileCount = 0;
    };

    /** @brief ブレードの生成と、LOD ごとの間接引数を記録する GPU の補助。 */
    class GrassGenerateGpu
    {
    public:
        bool Initialize(RHI::IDevice* device);
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();

        /** @brief 設定の容量に合う GPU 専用バッファを用意する。 */
        bool EnsureBuffers(const GrassGenerateSettings& settings);

        /**
         * @brief 引数の初期化と生成を記録する。TileCount が 0 でも初期化する。
         * 定数の HZB フラグと HiZ の有無は呼び出し側で一致させ、HiZ と FallbackHiZ は
         * いずれも束縛するとき ShaderResource の状態にしておく。
         * 読み戻しを行う側は使用後に記録を ShaderResource、引数を IndirectArgument に戻す。
         */
        bool Record(RHI::ICommandList* commandList, const GrassGenerateRecordInputs& inputs);

        const RHI::DescriptorSetLayoutPtr& GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
        const RHI::BufferPtr& GetRecordBuffer() const { return m_RecordBuffer; }
        const RHI::BufferPtr& GetArgsBuffer() const { return m_ArgsBuffer; }
        uint32_t GetLodBase(uint32_t lod) const { return lod < kGrassLodCount ? m_LodBase[lod] : 0; }
        uint32_t GetLodCapacity(uint32_t lod) const { return lod < kGrassLodCount ? m_LodCapacity[lod] : 0; }

    private:
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ArgsInitShader;
        RHI::ShaderPtr m_GenerateShader;
        RHI::PipelinePtr m_ArgsInitPipeline;
        RHI::PipelinePtr m_GeneratePipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::BufferPtr m_RecordBuffer;
        RHI::BufferPtr m_ArgsBuffer;
        uint32_t m_RecordBytes = 0;
        uint32_t m_LodBase[kGrassLodCount] = {};
        uint32_t m_LodCapacity[kGrassLodCount] = {};
    };

    /** @brief 草の場からカメラ周辺のブレードと間接描画の引数を生成する。 */
    class GrassPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        ~GrassPass() override;

        const char* GetName() const override { return "GrassPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        void SetMegaGeometryPass(const MegaGeometryPass* pass) { m_MegaGeometryPass = pass; }
        const RHI::BufferPtr& GetRecordBuffer() const { return m_Generator.GetRecordBuffer(); }
        const RHI::BufferPtr& GetArgsBuffer() const { return m_Generator.GetArgsBuffer(); }
        uint32_t GetLodBase(uint32_t lod) const { return m_Generator.GetLodBase(lod); }
        uint32_t GetLodCapacity(uint32_t lod) const { return m_Generator.GetLodCapacity(lod); }

    private:
        GrassGenerateGpu m_Generator;
        GrassFieldGpuWindow m_FieldWindow;
        GrassTileSelector m_TileSelector;
        GrassGenerateSettings m_Settings;
        Container::VariableArray<GrassTileInfo> m_SelectedTiles;
        RHI::TexturePtr m_DummyHiZ;
        RHI::SamplerPtr m_Sampler;
        const MegaGeometryPass* m_MegaGeometryPass = nullptr;
        bool m_bHasGrass = false;
#if NORVES_BUILD_DEVELOPMENT
        uint32_t m_LastLoggedTileCount = UINT32_MAX;
#endif
    };
} // namespace NorvesLib::Core::Rendering
