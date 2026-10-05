#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
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

    /**
     * @brief dispatch の 1 次元目・2 次元目のグループ数の上限
     *
     * Vulkan が保証する maxComputeWorkGroupCount の最小値。1 インスタンスのグループ数がこれを超えるときは
     * 2 次元目へ広げる。
     */
    constexpr uint32_t SKINNING_MAX_GROUP_COUNT = 65535;

    /**
     * @brief storage buffer 1 本の束縛の大きさの上限（バイト）
     *
     * Vulkan が保証する maxStorageBufferRange の最小値（2^27）。RHI は実機の値を公開していないので、保証された最小値で抑える。
     * 出力の今・前のバッファ（1 頂点 32 バイト）はこの範囲に収まる頂点数まで、入力の頂点（1 頂点 64 バイト）も同じ。
     */
    constexpr uint64_t SKINNING_MAX_BINDING_BYTES = 1ull << 27;

    /** @brief 出力のバッファ 1 本に詰められる頂点数の上限（Declare が合計を抑える値） */
    constexpr uint32_t SKINNING_MAX_OUTPUT_VERTICES =
        static_cast<uint32_t>(SKINNING_MAX_BINDING_BYTES / sizeof(SkinnedOutputVertex));

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
        /** @brief dispatch の 1 次元目のグループ数の上限（実機では SKINNING_MAX_GROUP_COUNT。小さくして 2 次元目へ広げる動きを試せる） */
        uint32_t GroupCountXLimit = SKINNING_MAX_GROUP_COUNT;
    };

    /**
     * @brief スキニングの頂点を計算シェーダーで変形する本体（RenderGraph にも GPU のテストにも使う）
     *
     * 1 dispatch ごとに UBO とディスクリプタセットを使う。資源は FrameUseRing が持ち、BeginFrame で
     * 飛行中のフレームの番号の枠を選ぶ。Record を呼ぶたびに枠の次の資源を使い、足りなければ増やす。
     * 使用済みの位置が戻るのはフレームの通し番号が変わったときだけなので、1 フレームに何回 Record しても
     * （同じパスが複数のビューポートで何回 Execute されても）、提出前の資源を上書きしない。
     */
    class SkinningCompute final
    {
    public:
        static constexpr uint32_t ThreadsPerGroup = 64;

        SkinningCompute();
        ~SkinningCompute();

        SkinningCompute(const SkinningCompute&) = delete;
        SkinningCompute& operator=(const SkinningCompute&) = delete;

        /** @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record は何もしない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const { return m_Pipeline != nullptr; }

        /**
         * @brief 飛行中のフレームの番号の枠を選ぶ。フレームの通し番号が前回と違えば、その枠の使用済みの位置を戻す
         * @param inFlightIndex ViewRenderContext::FrameIndex
         * @param frameSerial ViewRenderContext::ResolveRenderFrameSerial()（同じフレームの間は同じ値）
         */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /** @brief 選んだ枠で、今のフレームにここまで使った資源の数（観測用） */
        uint32_t GetUsedCount() const { return m_Uses.GetUsedCount(); }

        /**
         * @brief 頂点数から dispatch のグループ数（x, y）を求める
         *
         * グループ数 g = ceil(vertexCount / ThreadsPerGroup) が groupCountXLimit に収まれば x = g、y = 1。
         * 収まらなければ x = groupCountXLimit、y = ceil(g / x) に広げる（シェーダーは y * (x * 64) + x 方向の番号で頂点を求める）。
         * @param groupCountXLimit 1 次元目の上限。SKINNING_MAX_GROUP_COUNT を超える値は SKINNING_MAX_GROUP_COUNT に抑える
         * @return 求められたら true。頂点数が 0、上限が 0、y が SKINNING_MAX_GROUP_COUNT を超えるときは false
         */
        static bool ComputeGroupCounts(uint32_t vertexCount, uint32_t groupCountXLimit, uint32_t& outX, uint32_t& outY);

        /**
         * @brief 1 インスタンスぶんの変形を記録する
         * @return 記録できたら true。入力・出力が足りない、範囲が出力バッファに収まらない、束縛が SKINNING_MAX_BINDING_BYTES を
         *         超える、グループ数が dispatch の上限を超えるときは false
         */
        bool Record(RHI::ICommandList* commandList, const SkinningComputeDispatch& dispatch);

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        FrameUseRing<Use> m_Uses;
    };

    /** @brief 計算シェーダーでスキニングしたインスタンス 1 つぶんの結果（後のパスが読む） */
    struct SkinningComputeInstance
    {
        SkinnedMeshHandle MeshHandle;
        uint64_t ObjectId = 0;
        uint64_t SourceMeshComponentId = 0;
        /** @brief 元の描画（DrawParams）の材質の番号 */
        uint32_t MaterialIndex = 0;
        /** @brief 元の描画（DrawParams）の材質のハンドル（ビジビリティバッファの材質の表が実物の材質を引く） */
        MaterialHandle Material;
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

        /**
         * @brief 出力バッファに詰める頂点の合計の上限（既定は SKINNING_MAX_OUTPUT_VERTICES。小さくして溢れた動きを試せる）
         *
         * 超える値は SKINNING_MAX_OUTPUT_VERTICES に抑える。
         */
        void SetMaxOutputVertices(uint32_t maxOutputVertices);

        /**
         * @brief 最後の Declare が、頂点の合計の上限か束縛の大きさの上限のために計算スキニングから外したインスタンスの数
         *
         * 外したインスタンスは、このパスの出力に載らない（GetInstances() にも入らない）。毎回の Declare で数え直す
         * （累計ではない）。RenderingCoordinator がフレームの統計（RenderingStats::SkinningComputeDroppedInstances）へ
         * 設定する。ログはパスの寿命で初めて外したときに 1 回だけ出す。
         */
        uint32_t GetDroppedInstanceCount() const { return m_DroppedInstanceCount; }

        /** @brief 最後の Execute で変形を記録したインスタンス（記録できなかったフレームは空） */
        const Container::VariableArray<SkinningComputeInstance>& GetInstances() const { return m_Instances; }
        RGResourceHandle GetCurrentVerticesHandle() const { return m_CurrentHandle.ToResourceHandle(); }
        RGResourceHandle GetPreviousVerticesHandle() const { return m_PreviousHandle.ToResourceHandle(); }

    private:
        void RecordInstances(ViewRenderContext& context,
                             const RHI::BufferPtr& currentVertices,
                             const RHI::BufferPtr& previousVertices);

        /** @brief Declare が決めた、不透明描画の中のスキニングの 1 描画と出力の範囲 */
        struct PlannedInstance
        {
            uint32_t CommandIndex = 0;
            uint32_t VertexBase = 0;
            uint32_t VertexCount = 0;
        };

        SkinningCompute m_Compute;
        uint32_t m_MaxOutputVertices = SKINNING_MAX_OUTPUT_VERTICES;
        uint32_t m_DroppedInstanceCount = 0;
        bool m_bLoggedDrop = false;
        RGBufferHandle m_CurrentHandle;
        RGBufferHandle m_PreviousHandle;
        Container::VariableArray<PlannedInstance> m_Plan;
        Container::VariableArray<SkinningComputeInstance> m_Instances;
    };

} // namespace NorvesLib::Core::Rendering
