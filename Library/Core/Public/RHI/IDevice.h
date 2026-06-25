#pragma once

#include "RHITypes.h"
#include "DeviceCapabilities.h"
#include "IGPUResourceAllocator.h"
#include "IDescriptorSet.h"
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
