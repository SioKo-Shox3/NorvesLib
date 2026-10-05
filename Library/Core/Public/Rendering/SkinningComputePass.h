#pragma once

#include "Container/Containers.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/SkinnedMeshTypes.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;
    struct ViewRenderContext;

    /**
     * @brief 計算シェーダーが書くスキニング済みの頂点（今・前のフレーム共通。ワールド空間）
     *
     * 出力バッファは詰めた float の列で、1 頂点が 32 バイト。シェーダー（skinning_compute.comp）の書き方と同じ並び。
     */
    struct SkinnedOutputVertex
    {
        float Position[3] = {};
        float Normal[3] = {};
        float TexCoord[2] = {};
    };

    static_assert(sizeof(SkinnedOutputVertex) == 32, "skinning_compute.comp の出力の並びと一致しなければならない");

    /** @brief 1 回の dispatch（1 インスタンスぶん）の入力と出力の範囲 */
    struct SkinningComputeDispatch
    {
        /** @brief 入力の頂点（SkinnedMeshVertex の列） */
        RHI::BufferPtr SkinVertices;
        /** @brief 今のフレームの変換とパレット（SkinnedMeshGpuStore の PaletteBuffer） */
        RHI::BufferPtr Palette;
        /** @brief 直前のフレームの変換とパレット（SkinnedMeshGpuStore の PreviousPaletteBuffer） */
        RHI::BufferPtr PreviousPalette;
        /** @brief 今のフレームの頂点の書き込み先（storage） */
        RHI::BufferPtr CurrentVertices;
        /** @brief 直前のフレームの頂点の書き込み先（storage） */
        RHI::BufferPtr PreviousVertices;
        uint32_t VertexCount = 0;
        /** @brief 出力バッファの中で、このインスタンスが書き始める頂点の番号 */
        uint32_t OutputVertexBase = 0;
    };

    /**
     * @brief スキニングの頂点を計算シェーダーで変形する本体（RenderGraph にも GPU のテストにも使う）
     *
     * 1 dispatch ごとに UBO とディスクリプタセットを使うので、飛行中のフレームの数だけ枠を持つ。
     * BeginFrame で枠を選んで使用済みの位置を戻し、Record を呼ぶたびに枠の次の資源を使う。
     * 同じ枠を再び使うのは FrameSlotCount フレーム後で、その GPU の仕事は終わっている前提。
     */
    class SkinningCompute final
    {
    public:
        static constexpr uint32_t FrameSlotCount = 2;
        static constexpr uint32_t ThreadsPerGroup = 64;

        SkinningCompute();
        ~SkinningCompute();

        SkinningCompute(const SkinningCompute&) = delete;
        SkinningCompute& operator=(const SkinningCompute&) = delete;

        /** @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record は何もしない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const { return m_Pipeline != nullptr; }

        /** @brief frameIndex に対応する枠を選び、その使用済みの位置を戻す */
        void BeginFrame(uint64_t frameIndex);

        /**
         * @brief 1 インスタンスぶんの変形を記録する
         * @return 記録できたら true。入力・出力が足りない、範囲が出力バッファに収まらないときは false
         */
        bool Record(RHI::ICommandList* commandList, const SkinningComputeDispatch& dispatch);

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        struct Slot
        {
            Container::VariableArray<Use> Uses;
            uint32_t Cursor = 0;
        };

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        Slot m_Slots[FrameSlotCount];
        uint32_t m_ActiveSlot = 0;
    };

    /** @brief 計算シェーダーでスキニングしたインスタンス 1 つぶんの結果（後のパスが読む） */
    struct SkinningComputeInstance
    {
        SkinnedMeshHandle MeshHandle;
        uint64_t ObjectId = 0;
        uint64_t SourceMeshComponentId = 0;
        /** @brief 元の描画（DrawParams）の材質の番号 */
        uint32_t MaterialIndex = 0;
        /** @brief 出力バッファの中の先頭の頂点番号と頂点数（今・前で同じ） */
        uint32_t VertexBase = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        /** @brief 入力のインデックスのバッファ（頂点番号は出力の先頭からの相対） */
        RHI::BufferPtr IndexBuffer;
        /** @brief 出力バッファの先頭のアドレスを含む、このインスタンスの先頭頂点のアドレス（BDA が無いときは 0） */
        uint64_t CurrentVertexAddress = 0;
        uint64_t PreviousVertexAddress = 0;
    };

    /**
     * @brief スキニングのインスタンスごとに、今と前のフレームの頂点を計算シェーダーで作る RenderGraph のパス
     *
     * そのフレームの不透明描画のうちスキニングのものを集め、全インスタンスの頂点を詰めた 2 本の
     * フレームごとのバッファ（今・前。storage・BDA）を RenderGraph の資源として宣言して書く。後のビジビリティバッファの
     * ラスタと材質の解決は、名前付きの資源（SkinningCurrentVertices・SkinningPreviousVertices）を Read して
     * GetInstances() の範囲を読む。今の GBuffer の経路は頂点シェーダーのスキニングのままで、このパスには依存しない。
     *
     * 既定は無効（SetEnabled(false)）。ビジビリティバッファを使うときだけ有効にする。
     */
    class SkinningComputePass final : public IViewPass, public IRenderGraphPass
    {
    public:
        SkinningComputePass();
        ~SkinningComputePass() override;

        const char* GetName() const override { return "SkinningComputePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 最後の Execute で変形を記録したインスタンス（記録できなかったフレームは空） */
        const Container::VariableArray<SkinningComputeInstance>& GetInstances() const { return m_Instances; }
        RGResourceHandle GetCurrentVerticesHandle() const { return m_CurrentHandle.ToResourceHandle(); }
        RGResourceHandle GetPreviousVerticesHandle() const { return m_PreviousHandle.ToResourceHandle(); }

    private:
        /** @brief Declare が決めた、不透明描画の中のスキニングの 1 描画と出力の範囲 */
        struct PlannedInstance
        {
            uint32_t CommandIndex = 0;
            uint32_t VertexBase = 0;
            uint32_t VertexCount = 0;
        };

        SkinningCompute m_Compute;
        RGBufferHandle m_CurrentHandle;
        RGBufferHandle m_PreviousHandle;
        Container::VariableArray<PlannedInstance> m_Plan;
        Container::VariableArray<SkinningComputeInstance> m_Instances;
        uint64_t m_FrameCounter = 0;
    };

} // namespace NorvesLib::Core::Rendering
