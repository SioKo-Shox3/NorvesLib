#pragma once

#include "Rendering/GrassGenerateReference.h"
#include "Rendering/GrassBladeMesh.h"
#include "Rendering/WorldFieldTypes.h"
#include "Rendering/GBufferOverlayTargets.h"
#include "Rendering/GrassFieldGpuWindow.h"
#include "Rendering/GrassTileSelector.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/ICommandList.h"
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

    // bit2 があるときだけ UBO の末尾を読む。464 バイトの既存入力ではこのビットを立てない。
    inline constexpr uint32_t kGrassDrawFlagWorldField = 4u;

    /** @brief 既存の描画定数に、世界フィールドの標本定数を末尾へ追加した std140 配置。 */
    struct alignas(16) GrassDrawWorldFieldConstants
    {
        GrassDrawConstants Draw;
        WorldFieldSampleConstants WorldField;
    };
    static_assert(offsetof(GrassDrawWorldFieldConstants, WorldField) == sizeof(GrassDrawConstants));
    static_assert(sizeof(GrassDrawWorldFieldConstants) == 512);

    /** @brief 草の描画の 1 回の記録に使う、呼び出し側が用意した資源。 */
    struct GrassDrawRecordInputs
    {
        RHI::TexturePtr ColorTargets[GBufferOverlayTargets::kColorTargetCount];
        RHI::TexturePtr DepthTarget;
        RHI::Viewport Viewport;
        // 空の矩形なら Viewport 全体を使う。
        RHI::ScissorRect Scissor;
        RHI::DescriptorSetPtr DescriptorSets[kGrassLodCount];
        GrassGenerateBufferRange Constants;
        GrassGenerateBufferRange Species;
        RHI::BufferPtr RecordBuffer;
        uint32_t LodBase[kGrassLodCount] = {};
        uint32_t LodCapacity[kGrassLodCount] = {};
        RHI::BufferPtr ArgsBuffer;
        RHI::TexturePtr LayerA;
        RHI::SamplerPtr Sampler;
        // 両方を指定するときは拡張定数を渡す。未指定なら中立画像を使い、既存の定数も受け付ける。
        RHI::TexturePtr WorldFieldCurrent;
        RHI::TexturePtr WorldFieldPrevious;
    };

    /** @brief 生成済みのブレードを、LOD ごとに GBuffer へ重ねて描く GPU の補助。 */
    class GrassDrawGpu
    {
    public:
        bool Initialize(RHI::IDevice* device);
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager,
                        const GrassGenerateSettings& settings = GrassGenerateSettings{});
        void Shutdown();

        /**
         * @brief 3 つの LOD の間接描画を記録する。記述子セットは互いに異なるものを渡す。
         * 各 LodCapacity は正数、LodBase * 16 はデバイスのストレージの整列を満たすこと。
         * 記録バッファは ShaderResource で渡し、戻ったときも ShaderResource である。
         * 引数は IndirectArgument、LayerA と世界フィールドの画像は ShaderResource とする。
         * 描き先は RenderTarget / DepthWrite で渡し、終了時に ShaderResource へ移る。
         * 分割数は Initialize の settings と生成定数で一致させる。
         */
        bool Record(RHI::ICommandList* commandList, const GrassDrawRecordInputs& inputs);

        const RHI::DescriptorSetLayoutPtr& GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

    private:
        RHI::IDevice* m_Device = nullptr;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::BufferPtr m_IndexBuffer;
        RHI::TexturePtr m_NeutralWorldField;
        RHI::SamplerPtr m_WorldFieldSampler;
        GBufferOverlayTargets m_OverlayTargets;
        GBufferOverlayPipelineDesc m_OverlayPipelineDesc;
    };

    /** @brief 草の場からブレードを生成し、GBuffer へ重ねて描く。 */
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
        void ExecuteInternal(ViewRenderContext& context, RenderGraphResources* resources);

        GrassGenerateGpu m_Generator;
        GrassDrawGpu m_Drawer;
        GrassFieldGpuWindow m_FieldWindow;
        GrassTileSelector m_TileSelector;
        GrassGenerateSettings m_Settings;
        Container::VariableArray<GrassTileInfo> m_SelectedTiles;
        RHI::TexturePtr m_DummyHiZ;
        RHI::SamplerPtr m_Sampler;
        const MegaGeometryPass* m_MegaGeometryPass = nullptr;
        RGResourceHandle m_ColorHandles[GBufferOverlayTargets::kColorTargetCount];
        RGResourceHandle m_DepthHandle;
        RGResourceHandle m_WorldFieldCurrentHandle;
        RGResourceHandle m_WorldFieldPreviousHandle;
        WorldFieldSampleConstants m_WorldFieldConstants;
        bool m_bHasGrass = false;
#if NORVES_BUILD_DEVELOPMENT
        uint32_t m_LastLoggedTileCount = UINT32_MAX;
#endif
    };
} // namespace NorvesLib::Core::Rendering
