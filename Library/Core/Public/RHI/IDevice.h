#pragma once

#include "RHITypes.h"
#include "DeviceCapabilities.h"
#include "IGPUResourceAllocator.h"
#include "ITexture.h"
#include "IDescriptorSet.h"
#include "IPipeline.h"
#include "IAccelerationStructure.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include "Platform/NativeWindowHandle.h"

namespace NorvesLib::RHI
{

    /**
     * @brief サンプラー作成情報
     */
    struct SamplerDesc
    {
        FilterMode filterMin = FilterMode::Point;
        FilterMode filterMag = FilterMode::Point;
        FilterMode filterMip = FilterMode::Point;
        TextureAddressMode addressU = TextureAddressMode::Wrap;
        TextureAddressMode addressV = TextureAddressMode::Wrap;
        TextureAddressMode addressW = TextureAddressMode::Wrap;
        float mipLODBias = 0.0f;
        uint32_t maxAnisotropy = 1;
        CompareFunc compareFunc = CompareFunc::Never;
        float borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float minLOD = 0.0f;
        float maxLOD = FLT_MAX;
    };

    /**
     * @brief シェーダー作成情報
     */
    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::None;
        NorvesLib::Core::Container::String entryPoint = "main";
        NorvesLib::Core::Container::VariableArray<uint8_t> byteCode;
        NorvesLib::Core::Container::String sourceCode; // ソースコード（コンパイル済みでない場合）
    };

    /**
     * @brief スワップチェーン作成情報
     */
    struct SwapChainDesc
    {
        Core::Platform::NativeWindowHandle windowHandle;
        uint32_t width = 0;
        uint32_t height = 0;
        Format format = Format::R8G8B8A8_UNORM;
        uint32_t bufferCount = 2;
        bool vsync = true;
    };

    /**
     * @brief レンダーターゲット設定
     */
    struct RenderTargetBlendDesc
    {
        bool blendEnable = false;
        BlendFactor srcBlend = BlendFactor::One;
        BlendFactor dstBlend = BlendFactor::Zero;
        BlendOp blendOp = BlendOp::Add;
        BlendFactor srcBlendAlpha = BlendFactor::One;
        BlendFactor dstBlendAlpha = BlendFactor::Zero;
        BlendOp blendOpAlpha = BlendOp::Add;
        uint8_t renderTargetWriteMask = 0xF; // R|G|B|A
    };

    /**
     * @brief グラフィックパイプライン作成情報
     */
    struct GraphicsPipelineDesc
    {
        // シェーダー
        ShaderPtr vertexShader;
        ShaderPtr pixelShader;
        ShaderPtr geometryShader;
        ShaderPtr hullShader;   // Vulkan: tessControlShader
        ShaderPtr domainShader; // Vulkan: tessEvalShader

        // 頂点入力
        Core::Container::VariableArray<VertexBindingDesc> vertexBindings;
        Core::Container::VariableArray<VertexAttributeDesc> vertexAttributes;

        // プリミティブトポロジー
        PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
        uint32_t patchControlPoints = 3; // テッセレーション用

        // ラスタライザステート
        RasterState rasterState;

        // デプスステンシルステート
        DepthStencilState depthStencilState;

        // ブレンドステート
        BlendState blendState;

        // レンダーパス
        RenderPassPtr renderPass;

        // ディスクリプタセットレイアウト（パイプラインレイアウト用）
        Core::Container::VariableArray<DescriptorSetDesc> descriptorSetLayouts;
    };

    /**
     * @brief コンピュートパイプライン作成情報
     */
    struct ComputePipelineDesc
    {
        ShaderPtr computeShader;

        // ディスクリプタセットレイアウト（パイプラインレイアウト用）
        Core::Container::VariableArray<DescriptorSetDesc> descriptorSetLayouts;
    };

    /**
     * @brief アタッチメント記述子
     */
    struct AttachmentDesc
    {
        Format format = Format::UNKNOWN;
        bool isDepthStencil = false;
        bool clear = true;
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        /** @brief 整数形式（IsUnsignedIntegerFormat）の添付のクリア値。浮動小数の clearColor は使わない */
        uint32_t clearColorUint[4] = {0u, 0u, 0u, 0u};
        float clearDepth = 1.0f;
        uint32_t clearStencil = 0;
        AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
        AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
        ResourceState initialState = ResourceState::Undefined;
        ResourceState finalState = ResourceState::Present;
    };

    /**
     * @brief レンダーパス作成情報
     */
    struct RenderPassDesc
    {
        NorvesLib::Core::Container::VariableArray<AttachmentDesc> colorAttachments;
        AttachmentDesc depthStencilAttachment;
        bool hasDepthStencil = false;
    };

    /**
     * @brief フレームバッファ作成情報
     */
    struct FramebufferDesc
    {
        NorvesLib::Core::Container::VariableArray<TexturePtr> colorTargets;
        TexturePtr depthStencilTarget;
        RenderPassPtr renderPass;
        uint32_t width = 0;
        uint32_t height = 0;
        /** @brief 配列デプスアタッチメントで使用する0-based layer（キューブ配列はキューブの番号 * 6 + 面） */
        uint32_t depthStencilArrayLayer = 0;
    };

    /**
     * @brief デバイスインターフェース
     */
    class IDevice
    {
    public:
        virtual ~IDevice() = default;

        /**
         * @brief バッファを作成
         * @param desc バッファ記述子
         * @return 作成されたバッファオブジェクト
         */
        virtual BufferPtr CreateBuffer(const BufferDesc &desc) = 0;

        /**
         * @brief 加速構造リソースを作成
         * @return 基底の未対応実装はnullptrを返す。対応バックエンドはoverrideする。
         */
        virtual AccelerationStructurePtr CreateAccelerationStructure(const AccelerationStructureDesc &)
        {
            return {};
        }

        /**
         * @brief テクスチャを作成
         * @param desc テクスチャ記述子
         * @return 作成されたテクスチャオブジェクト
         */
        virtual TexturePtr CreateTexture(const TextureDesc &desc) = 0;

        /**
         * @brief サンプラーを作成
         * @param desc サンプラー記述子
         * @return 作成されたサンプラーオブジェクト
         */
        virtual SamplerPtr CreateSampler(const SamplerDesc &desc) = 0;

        /**
         * @brief シェーダーを作成
         * @param desc シェーダー記述子
         * @return 作成されたシェーダーオブジェクト
         */
        virtual ShaderPtr CreateShader(const ShaderDesc &desc) = 0;

        /**
         * @brief コマンドリストを作成
         * @return 作成されたコマンドリストオブジェクト
         */
        virtual CommandListPtr CreateCommandList() = 0;

        /**
         * @brief スワップチェーンを作成
         * @param desc スワップチェーン記述子
         * @return 作成されたスワップチェーンオブジェクト
         */
        virtual SwapChainPtr CreateSwapChain(const SwapChainDesc &desc) = 0;

        /**
         * @brief レンダーパスを作成
         * @param desc レンダーパス記述子
         * @return 作成されたレンダーパスオブジェクト
         */
        virtual RenderPassPtr CreateRenderPass(const RenderPassDesc &desc) = 0;

        /**
         * @brief フレームバッファを作成
         * @param desc フレームバッファ記述子
         * @return 作成されたフレームバッファオブジェクト
         */
        virtual FramebufferPtr CreateFramebuffer(const FramebufferDesc &desc) = 0;

        /**
         * @brief グラフィックパイプラインを作成
         * @param desc パイプライン記述子
         * @return 作成されたパイプラインオブジェクト
         */
        virtual PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;

        /**
         * @brief コンピュートパイプラインを作成
         * @param desc パイプライン記述子
         * @return 作成されたパイプラインオブジェクト
         */
        virtual PipelinePtr CreateComputePipeline(const ComputePipelineDesc &desc) = 0;

        /**
         * @brief レイトレーシングパイプラインを作成
         * @return 未対応の描画バックエンドではnullptrを返す
         */
        virtual PipelinePtr CreateRayTracingPipeline(const RayTracingPipelineDesc&)
        {
            return {};
        }

        /**
         * @brief ディスクリプタセットを作成
         * @param desc ディスクリプタセット記述子
         * @return 作成されたディスクリプタセットオブジェクト
         */
        virtual DescriptorSetPtr CreateDescriptorSet(const DescriptorSetDesc &desc) = 0;

        /**
         * @brief シェーダーコンパイラを作成
         * @return 対応するRHI用シェーダーコンパイラ
         */
        virtual ShaderCompilerPtr CreateShaderCompiler() = 0;

        /**
         * @brief Slangシェーダーコンパイラを作成
         * @return 対応するRHI用Slangコンパイラ。未対応の場合はnullptr
         */
        virtual ShaderCompilerPtr CreateSlangShaderCompiler() { return nullptr; }

        /**
         * @brief GPUリソースアロケーターを取得
         * @return GPUリソースアロケーター。未対応の場合はnullptr
         */
        virtual IGPUResourceAllocator* GetResourceAllocator() = 0;

        /**
         * @brief sparse テクスチャへ結ぶ物理メモリの塊を作成（DeviceLocal）
         * @param sizeBytes 大きさ（SparsePageSizeBytes の倍数）
         * @param debugName デバッグ名
         * @return 作成した塊。sparse に対応しないバックエンドや確保に失敗したときは nullptr
         */
        virtual SparseMemoryBlockPtr CreateSparseMemoryBlock(uint64_t sizeBytes, const char *debugName = nullptr)
        {
            (void)sizeBytes;
            (void)debugName;
            return nullptr;
        }

        /**
         * @brief sparse テクスチャのタイル・ミップテイルへのページの結び付け・外しを1回の提出で出す
         *
         * 結び付けはグラフィックスのキューとセマフォで順序付けられ、次にグラフィックスのキューへ送る提出は
         * この結び付けが終わってから始まる（結んだタイルを読む描画より前に結び付けが終わる）。
         * 要求は全部が有効なときだけ提出し、1つでも不正なら何も変えずに false を返す。
         * コマンドの送信・プレゼントと同じ呼び出し側の直列化の下で呼ぶ（キューへの外部同期）。
         * タイルを結び直す・外すときは、そのタイルを読む提出中のフレームが無いことを呼び出し側が保証する
         * （外したページの再利用は、使った提出の serial の完了まで GpuRetireQueue が止める）。
         * @param request 結び付け・外しの集合
         * @return 提出できたら true（空の要求も true）
         */
        virtual bool BindSparse(const SparseBindRequest &request)
        {
            (void)request;
            return false;
        }

        /**
         * @brief コマンドキューを待機
         */
        virtual void WaitIdle() = 0;

        /**
         * @brief 使用しているAPIを取得
         * @return 使用中のRHI API
         */
        virtual API GetAPI() const = 0;

        /**
         * @brief デバイスの能力情報を取得
         * @return DeviceCapabilities への const 参照
         */
        virtual const DeviceCapabilities &GetCapabilities() const = 0;

        /**
         * @brief DeviceLocal ヒープの予算と使用量を取得
         * @return 取得できないバックエンドでは bValid が false の値を返す
         */
        virtual VideoMemoryBudget GetVideoMemoryBudget() const { return {}; }

        /**
         * @brief 描画API固有のクリップ空間に合わせてプロジェクション行列を補正
         *
         * 右手系座標のプロジェクション行列を現在の描画APIのクリップ空間に変換します。
         * Y軸反転（Vulkan等）や深度範囲の調整をAPI側で吸収します。
         *
         * @param projection 補正前のプロジェクション行列
         * @param bApplyYFlip Y軸反転を適用するか（シャドウマップではfalse）
         * @return 補正済みのプロジェクション行列
         */
        virtual Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4 &projection, bool bApplyYFlip = true) const = 0;
    };

} // namespace NorvesLib::RHI
