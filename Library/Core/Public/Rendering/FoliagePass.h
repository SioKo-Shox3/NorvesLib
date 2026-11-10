#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/WorldFieldTypes.h"
#include "Rendering/GBufferOverlayTargets.h"
#include "Rendering/ViewClearance.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ITexture;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{
    /** @brief 株の頂点・断片シェーダーに渡す std140 定数。末尾のフィールドは頂点段だけが読む。 */
    struct alignas(16) FoliageUniform
    {
        float view[16];
        float projection[16];
        float previousView[16];
        float previousProjection[16];
        float cameraPosition[4];
        float wind[4];
        float windParams[4];
        float stemColor[4];
        float tipColorA[4];
        float tipColorB[4];
        float frameParams[4];
        float windField[4];
        ViewClearanceGpuData viewClearance;
        WorldFieldSampleConstants worldField;
    };
    static_assert(offsetof(FoliageUniform, view) == 0u);
    static_assert(offsetof(FoliageUniform, projection) == 64u);
    static_assert(offsetof(FoliageUniform, previousView) == 128u);
    static_assert(offsetof(FoliageUniform, previousProjection) == 192u);
    static_assert(offsetof(FoliageUniform, cameraPosition) == 256u);
    static_assert(offsetof(FoliageUniform, wind) == 272u);
    static_assert(offsetof(FoliageUniform, windParams) == 288u);
    static_assert(offsetof(FoliageUniform, stemColor) == 304u);
    static_assert(offsetof(FoliageUniform, tipColorA) == 320u);
    static_assert(offsetof(FoliageUniform, tipColorB) == 336u);
    static_assert(offsetof(FoliageUniform, frameParams) == 352u);
    static_assert(offsetof(FoliageUniform, windField) == 368u);
    static_assert(offsetof(FoliageUniform, viewClearance) == 384u);
    static_assert(offsetof(FoliageUniform, worldField) == 448u);
    static_assert(sizeof(FoliageUniform) == 496u);

    /**
     * @brief 草・低木の株（FoliageTypes.h の束）を、インスタンス描画で GBuffer へ直接ラスタするパス
     *
     * ビジビリティバッファの解決（または GBuffer の描画）の後・SSAO と影の印付けの前に置き、GBuffer の 5 枚と深度へ重ねて描く。
     * 頂点シェーダーで風に揺らすので、頂点を読み直す解決の経路には載せない。束ごとに 1 回の DrawIndexedInstanced で描き、
     * 帯は両面（カリングなし）。影は照明が GBuffer の深度から引くので受ける。太陽の影を落とすのは、カメラに近い株を同じ置き方で
     * 仮想シャドウマップへ描く VirtualShadowMapPass（VirtualShadowMapFoliageCasters.h）。
     *
     * メッシュとインスタンスの配列は作った後に書き換えない値なので、初めて描くときに一度だけ GPU のバッファへ置き、束が
     * 一覧から外れてから十分な回数の描画を待って捨てる。定数・記述子セットは毎フレーム文脈の FrameResources（フレームの持ち主）
     * から借りる（パスを書く人向けの約束は Docs/Architecture/FrameResources.md）。
     */
    class FoliagePass : public IViewPass, public IRenderGraphPass
    {
    public:
        FoliagePass() = default;
        ~FoliagePass() override;

        const char *GetName() const override { return "FoliagePass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

    private:
        /// 書き換えない頂点・インデックス・インスタンスの配列を置いた GPU のバッファ（元の値の参照を持ち、同じ番地の再利用を防ぐ）
        struct StaticBuffer
        {
            Container::TSharedPtr<const FoliageMeshData> MeshSource;
            Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> InstanceSource;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            RHI::BufferPtr InstanceBuffer;
            float MaxHeightScale = 0.0f;
            bool bHasMaxHeightScale = false;
            uint64_t LastUsedExecute = 0;
        };

        /// メッシュの頂点・インデックスのバッファを outVertex・outIndex へ返す（無ければ作る）
        bool AcquireMeshBuffers(const Container::TSharedPtr<const FoliageMeshData> &mesh, RHI::BufferPtr &outVertex,
                                RHI::BufferPtr &outIndex);
        /// 書き換えないインスタンス配列から最大の高さ倍率を一度だけ求める
        float AcquireMaxHeightScale(const Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> &instances);
        /// インスタンスの配列のバッファを返す（無ければ作る）
        RHI::BufferPtr AcquireInstanceBuffer(const Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> &instances);
        /// 長く使われていないバッファを捨てる
        void ReleaseUnusedStaticBuffers();

        RHI::IDevice *m_Device = nullptr;
        Container::VariableArray<StaticBuffer> m_StaticBuffers;
        // Execute の回数（バッファを捨てる時期の判定に使う）
        uint64_t m_ExecuteCount = 0;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        GBufferOverlayTargets m_OverlayTargets;
        GBufferOverlayPipelineDesc m_OverlayPipelineDesc;
        // 共有の記述子セットのレイアウト（セットは毎フレーム借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::TexturePtr m_NeutralWorldField;
        RHI::SamplerPtr m_WorldFieldSampler;
        RGResourceHandle m_WorldFieldCurrentHandle;
        RGResourceHandle m_WorldFieldPreviousHandle;
        RGResourceHandle m_ColorHandles[GBufferOverlayTargets::kColorTargetCount];
        RGResourceHandle m_DepthHandle;
        // フィールドの余白を保つため、最後に干渉子を見た時刻を覚える。
        double m_LastWorldFieldInteractorTime = 0.0;
        bool m_bSawWorldFieldInteractor = false;
        // 最後にログへ出した束の数（変わったときだけ 1 行出す）
        uint32_t m_LoggedBatchCount = 0;
        uint64_t m_LoggedInstanceCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
