#pragma once

#include "RHITypes.h"
#include "IGPUResourceAllocator.h"
#include "IDescriptorSet.h"
#include "Container/Containers.h"

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
        ShaderStage stage;
        NorvesLib::Core::Container::String entryPoint;
        NorvesLib::Core::Container::VariableArray<uint8_t> byteCode;
    };

    /**
     * @brief スワップチェーン作成情報
     */
    struct SwapChainDesc
    {
        void *windowHandle = nullptr;
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
        ShaderPtr vertexShader;
        ShaderPtr pixelShader;
        ShaderPtr geometryShader;
        ShaderPtr hullShader;
        ShaderPtr domainShader;

        PrimitiveTopology topology = PrimitiveTopology::TriangleList;

        // ラスタライザステート
        FillMode fillMode = FillMode::Solid;
        CullMode cullMode = CullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClipEnable = true;
        bool scissorEnable = false;
        bool multisampleEnable = false;
        bool antialiasedLineEnable = false;

        // デプスステンシルステート
        bool depthEnable = true;
        bool depthWriteEnable = true;
        CompareFunc depthFunc = CompareFunc::Less;
        bool stencilEnable = false;
        uint8_t stencilReadMask = 0xFF;
        uint8_t stencilWriteMask = 0xFF;

        // ブレンドステート
        Core::Container::FixedArray<RenderTargetBlendDesc, 8> renderTargetBlendDesc;
        bool alphaToCoverageEnable = false;
        bool independentBlendEnable = false;

        RenderPassPtr renderPass;
    };

    /**
     * @brief コンピュートパイプライン作成情報
     */
    struct ComputePipelineDesc
    {
        ShaderPtr computeShader;
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
         * @brief コマンドキューを待機
         */
        virtual void WaitIdle() = 0;

        /**
         * @brief 使用しているAPIを取得
         * @return 使用中のRHI API
         */
        virtual API GetAPI() const = 0;
    };

} // namespace NorvesLib::RHI
