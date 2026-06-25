#pragma once

#include <cstdint>
#include "Container/PointerTypes.h"
#include "Container/Containers.h"

namespace NorvesLib::RHI
{
    using NorvesLib::Core::Container::DynamicPointerCast;
    using NorvesLib::Core::Container::MakeShared;
    using NorvesLib::Core::Container::StaticPointerCast;
    using NorvesLib::Core::Container::TSharedPtr;

    /**
     * @brief レンダリングAPIの種類を定義する列挙型
     */
    enum class API
    {
        None,
        DirectX11,
        DirectX12,
        Vulkan,
        OpenGL
    };

    /**
     * @brief フォーマット列挙型
     */
    enum class Format
    {
        UNKNOWN,
        R8_UNORM,
        R8G8_UNORM,
        R8G8B8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_UNORM,
        B8G8R8A8_SRGB,
        R16_FLOAT,
        R16G16_FLOAT,
        R16G16B16A16_FLOAT,
        R32_FLOAT,
        R32G32_FLOAT,
        R32G32B32_FLOAT,
        R32G32B32A32_FLOAT,
        D16_UNORM,
        D24_UNORM_S8_UINT,
        D32_FLOAT
    };

    /**
     * @brief テクスチャ次元の種類
     */
    enum class TextureDimension
    {
        Texture1D,
        Texture2D,
        Texture3D
    };

    /**
     * @brief リソースの使用方法を定義するフラグ
     */
    enum class ResourceUsage : uint32_t
    {
        None = 0,
        ShaderRead = 1 << 0,
        ShaderWrite = 1 << 1,
        RenderTarget = 1 << 2,
        DepthStencil = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
        VertexBuffer = 1 << 6,
        IndexBuffer = 1 << 7,
        ConstantBuffer = 1 << 8,
        StorageBuffer = 1 << 9,       // ストレージバッファ（SSBO）
        IndirectBuffer = 1 << 10,     // 間接描画引数バッファ
        ShaderResource = ShaderRead,  // エイリアス: 互換性のため
        UnorderedAccess = ShaderWrite // エイリアス: 互換性のため
    };

    inline ResourceUsage operator|(ResourceUsage a, ResourceUsage b)
    {
        return static_cast<ResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ResourceUsage operator&(ResourceUsage a, ResourceUsage b)
    {
        return static_cast<ResourceUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    /**
     * @brief シェーダーのステージを表す列挙型
     */
    enum class ShaderStage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Hull = 1 << 1,
        Domain = 1 << 2,
        Geometry = 1 << 3,
        Pixel = 1 << 4,
        Compute = 1 << 5,
        All = Vertex | Hull | Domain | Geometry | Pixel | Compute
    };

    inline ShaderStage operator|(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ShaderStage operator&(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    /**
     * @brief インデックスバッファの要素型
     */
    enum class IndexType : uint8_t
    {
        Uint16,
        Uint32
    };

    /**
     * @brief プリミティブトポロジーの種類
     */
    enum class PrimitiveTopology
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    /**
     * @brief カラーブレンド係数
     */
    enum class BlendFactor
    {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor
    };

    /**
     * @brief ブレンド操作
     */
    enum class BlendOp
    {
        Add,
        Subtract,
        RevSubtract,
        Min,
        Max
    };

    /**
     * @brief 比較関数
     */
    enum class CompareFunc
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    /**
     * @brief フィルモード
     */
    enum class FillMode
    {
        Wireframe,
        Solid
    };

    /**
     * @brief カリングモード
     */
    enum class CullMode
    {
        None,
        Front,
        Back
    };

    /**
     * @brief ステンシル操作
     */
    enum class StencilOp
    {
        Keep,
        Zero,
        Replace,
        IncrSat,
        DecrSat,
        Invert,
        Incr,
        Decr
    };

    /**
     * @brief ポリゴンモード（塗りつぶし方法）
     */
    enum class PolygonMode
    {
        Fill,
        Line,
        Point
    };

    /**
     * @brief 前面の定義
     */
    enum class FrontFace
    {
        CounterClockwise,
        Clockwise
    };

    /**
     * @brief 比較演算子
     */
    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    /**
     * @brief カラー書き込みマスク
     */
    enum class ColorWriteMask : uint32_t
    {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = R | G | B | A
    };

    inline ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b)
    {
        return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b)
    {
        return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    /**
     * @brief テクスチャアドレスモード
     */
    enum class TextureAddressMode
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
        MirrorOnce
    };

    /**
     * @brief フィルタモード
     */
    enum class FilterMode
    {
        Point,
        Linear,
        Anisotropic
    };

    /**
     * @brief リソースの状態を示す列挙型
     * GPUメモリ管理とバリアに使用
     */
    enum class ResourceState
    {
        Undefined,
        Common,
        VertexBuffer,
        ConstantBuffer,
        IndexBuffer,
        RenderTarget,
        UnorderedAccess,
        DepthWrite,
        DepthRead,
        ShaderResource,
        StreamOut,
        IndirectArgument,
        CopyDest,
        CopySource,
        ResolveDest,
        ResolveSource,
        Present,
        GenericRead
    };

    /**
     * @brief ディスクリプタタイプ
     */
    enum class DescriptorType
    {
        UniformBuffer,
        SampledImage,
        Sampler,
        StorageBuffer,
        StorageImage,
        UniformTexelBuffer,
        StorageTexelBuffer,
        CombinedImageSampler
    };

    /**
     * @brief ディスクリプタバインディング記述子
     */
    struct DescriptorBindingDesc
    {
        uint32_t binding = 0;                                ///< バインディングポイント
        DescriptorType type = DescriptorType::UniformBuffer; ///< ディスクリプタタイプ
        ShaderStage stages = ShaderStage::All;               ///< シェーダーステージ
        uint32_t count = 1;                                  ///< ディスクリプタ数
    };

    /**
     * @brief リソースタイプ
     */
    enum class ResourceType
    {
        Unknown,
        Buffer,
        Texture,
        Sampler,
        RenderPass,
        Framebuffer,
        Pipeline,
        DescriptorSet
    };

    /**
     * @brief パイプラインタイプ
     */
    enum class PipelineType
    {
        Graphics,
        Compute
    };

    /**
     * @brief アタッチメントロード操作
     */
    enum class AttachmentLoadOp
    {
        Load,
        Clear,
        DontCare
    };

    /**
     * @brief アタッチメントストア操作
     */
    enum class AttachmentStoreOp
    {
        Store,
        DontCare
    };

    /**
     * @brief 頂点入力レート
     */
    enum class VertexInputRate
    {
        Vertex,
        Instance
    };

    /**
     * @brief 頂点バインディング記述子
     */
    struct VertexBindingDesc
    {
        uint32_t binding = 0;
        uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::Vertex;
    };

    /**
     * @brief 頂点属性記述子
     */
    struct VertexAttributeDesc
    {
        uint32_t location = 0;
        uint32_t binding = 0;
        Format format = Format::R32G32B32A32_FLOAT;
        uint32_t offset = 0;
    };

    /**
     * @brief ラスタライザステート
     */
    struct RasterState
    {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        bool depthClampEnable = false;
        bool depthBiasEnable = false;
        float depthBiasConstantFactor = 0.0f;
        float depthBiasClamp = 0.0f;
        float depthBiasSlopeFactor = 0.0f;
        float lineWidth = 1.0f;
    };

    /**
     * @brief ステンシル面操作記述子
     */
    struct StencilFaceDesc
    {
        StencilOp failOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        CompareOp compareOp = CompareOp::Never;
        uint32_t compareMask = 0xFF;
        uint32_t writeMask = 0xFF;
        uint32_t reference = 0;
    };

    /**
     * @brief デプスステンシルステート
     */
    struct DepthStencilState
    {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool depthBoundsTestEnable = false;
        bool stencilTestEnable = false;
        StencilFaceDesc frontFace;
        StencilFaceDesc backFace;
    };

    /**
     * @brief ブレンドアタッチメント記述子
     */
    struct BlendAttachmentDesc
    {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
        ColorWriteMask colorWriteMask = ColorWriteMask::All;
    };

    /**
     * @brief ブレンドステート
     */
    struct BlendState
    {
        Core::Container::VariableArray<BlendAttachmentDesc> attachments;
        float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    // 前方宣言
    class IDevice;
    class ICommandList;
    class IBuffer;
    class ITexture;
    class ISampler;
    class IRenderPass;
    class IFramebuffer;
    class IShader;
    class IShaderCompiler;
    class IPipeline;
    class ISwapChain;
    class IDescriptorSet;

    // スマートポインタの定義
    using DevicePtr = TSharedPtr<IDevice>;
    using CommandListPtr = TSharedPtr<ICommandList>;
    using BufferPtr = TSharedPtr<IBuffer>;
    using TexturePtr = TSharedPtr<ITexture>;
    using SamplerPtr = TSharedPtr<ISampler>;
    using RenderPassPtr = TSharedPtr<IRenderPass>;
    using FramebufferPtr = TSharedPtr<IFramebuffer>;
    using ShaderPtr = TSharedPtr<IShader>;
    using PipelinePtr = TSharedPtr<IPipeline>;
    using SwapChainPtr = TSharedPtr<ISwapChain>;
    using DescriptorSetPtr = TSharedPtr<IDescriptorSet>;
    using ShaderCompilerPtr = TSharedPtr<IShaderCompiler>;

} // namespace NorvesLib::RHI
