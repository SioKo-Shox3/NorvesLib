#pragma once

#include <cstdint>
#include "Container/PointerTypes.h"
#include "Container/Containers.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::RHI
{

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
        ConstantBuffer = 1 << 8
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

    // 前方宣言
    class IDevice;
    class ICommandList;
    class IBuffer;
    class ITexture;
    class ISampler;
    class IRenderPass;
    class IFramebuffer;
    class IShader;
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

} // namespace NorvesLib::RHI
