#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // ハンドル型定義
    // ========================================

    /**
     * @brief リソースハンドルの基底テンプレート
     * @tparam Tag ハンドルの種類を区別するためのタグ型
     */
    template <typename Tag>
    struct ResourceHandle
    {
        uint64_t Id = 0;

        bool IsValid() const { return Id != 0; }
        explicit operator bool() const { return IsValid(); }

        bool operator==(const ResourceHandle &other) const { return Id == other.Id; }
        bool operator!=(const ResourceHandle &other) const { return Id != other.Id; }
        bool operator<(const ResourceHandle &other) const { return Id < other.Id; }

        static ResourceHandle Invalid() { return ResourceHandle{0}; }
    };

    // ハンドルタグ定義
    struct BufferHandleTag
    {
    };
    struct TextureHandleTag
    {
    };
    struct SamplerHandleTag
    {
    };
    struct ShaderHandleTag
    {
    };
    struct PipelineHandleTag
    {
    };
    struct RenderPassHandleTag
    {
    };
    struct FramebufferHandleTag
    {
    };
    struct MeshDataHandleTag
    {
    };
    struct MaterialHandleTag
    {
    };

    /**
     * @brief バッファリソースへのハンドル
     */
    using BufferHandle = ResourceHandle<BufferHandleTag>;

    /**
     * @brief テクスチャリソースへのハンドル
     */
    using TextureHandle = ResourceHandle<TextureHandleTag>;

    /**
     * @brief サンプラーリソースへのハンドル
     */
    using SamplerHandle = ResourceHandle<SamplerHandleTag>;

    /**
     * @brief シェーダーリソースへのハンドル
     */
    using ShaderHandle = ResourceHandle<ShaderHandleTag>;

    /**
     * @brief パイプラインリソースへのハンドル
     */
    using PipelineHandle = ResourceHandle<PipelineHandleTag>;

    /**
     * @brief レンダーパスリソースへのハンドル
     */
    using RenderPassHandle = ResourceHandle<RenderPassHandleTag>;

    /**
     * @brief フレームバッファリソースへのハンドル
     */
    using FramebufferHandle = ResourceHandle<FramebufferHandleTag>;

    /**
     * @brief メッシュデータへのハンドル
     */
    using MeshDataHandle = ResourceHandle<MeshDataHandleTag>;

    /**
     * @brief マテリアルへのハンドル
     */
    using MaterialHandle = ResourceHandle<MaterialHandleTag>;

    // ========================================
    // 描画用列挙型
    // ========================================

    /**
     * @brief プリミティブトポロジー
     */
    enum class PrimitiveTopology : uint8_t
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    /**
     * @brief インデックスフォーマット
     */
    enum class IndexFormat : uint8_t
    {
        UInt16,
        UInt32
    };

    /**
     * @brief レンダーレイヤー（ビットマスク）
     */
    enum class RenderLayer : uint8_t
    {
        Default = 1 << 0,
        Transparent = 1 << 1,
        UI = 1 << 2,
        PostProcess = 1 << 3,
        Shadow = 1 << 4,
        Debug = 1 << 5,
        All = 0xFF
    };

    inline RenderLayer operator|(RenderLayer a, RenderLayer b)
    {
        return static_cast<RenderLayer>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline RenderLayer operator&(RenderLayer a, RenderLayer b)
    {
        return static_cast<RenderLayer>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline bool HasFlag(RenderLayer mask, RenderLayer flag)
    {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0;
    }

    // ========================================
    // 境界ボックス/スフィア
    // ========================================

    /**
     * @brief 軸平行境界ボックス (AABB)
     */
    struct BoundingBox
    {
        float MinX = 0.0f, MinY = 0.0f, MinZ = 0.0f;
        float MaxX = 0.0f, MaxY = 0.0f, MaxZ = 0.0f;

        bool IsValid() const
        {
            return MinX <= MaxX && MinY <= MaxY && MinZ <= MaxZ;
        }

        void Expand(float x, float y, float z)
        {
            if (x < MinX)
                MinX = x;
            if (y < MinY)
                MinY = y;
            if (z < MinZ)
                MinZ = z;
            if (x > MaxX)
                MaxX = x;
            if (y > MaxY)
                MaxY = y;
            if (z > MaxZ)
                MaxZ = z;
        }

        void Merge(const BoundingBox &other)
        {
            if (other.MinX < MinX)
                MinX = other.MinX;
            if (other.MinY < MinY)
                MinY = other.MinY;
            if (other.MinZ < MinZ)
                MinZ = other.MinZ;
            if (other.MaxX > MaxX)
                MaxX = other.MaxX;
            if (other.MaxY > MaxY)
                MaxY = other.MaxY;
            if (other.MaxZ > MaxZ)
                MaxZ = other.MaxZ;
        }

        static BoundingBox CreateInvalid()
        {
            constexpr float maxFloat = 3.402823466e+38f;
            return BoundingBox{maxFloat, maxFloat, maxFloat, -maxFloat, -maxFloat, -maxFloat};
        }
    };

    /**
     * @brief 境界スフィア
     */
    struct BoundingSphere
    {
        float CenterX = 0.0f, CenterY = 0.0f, CenterZ = 0.0f;
        float Radius = 0.0f;

        bool IsValid() const
        {
            return Radius > 0.0f;
        }

        static BoundingSphere FromBoundingBox(const BoundingBox &box)
        {
            BoundingSphere sphere;
            sphere.CenterX = (box.MinX + box.MaxX) * 0.5f;
            sphere.CenterY = (box.MinY + box.MaxY) * 0.5f;
            sphere.CenterZ = (box.MinZ + box.MaxZ) * 0.5f;

            float dx = box.MaxX - sphere.CenterX;
            float dy = box.MaxY - sphere.CenterY;
            float dz = box.MaxZ - sphere.CenterZ;
            sphere.Radius = std::sqrt(dx * dx + dy * dy + dz * dz);

            return sphere;
        }
    };

    // ========================================
    // ビューポート/シザー
    // ========================================

    /**
     * @brief ビューポート矩形定義（RHI用）
     */
    struct ViewportRect
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    /**
     * @brief シザー矩形定義
     */
    struct ScissorRect
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;
    };

    // ========================================
    // カメラデータ
    // ========================================

    /**
     * @brief カメラ投影タイプ
     */
    enum class ProjectionType : uint8_t
    {
        Perspective,
        Orthographic
    };

    /**
     * @brief カメラデータ（フレームパケット用）
     */
    struct CameraData
    {
        // ビュー行列用データ
        float PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;
        float ForwardX = 0.0f, ForwardY = 0.0f, ForwardZ = -1.0f;
        float UpX = 0.0f, UpY = 1.0f, UpZ = 0.0f;

        // プロジェクション用データ
        ProjectionType Projection = ProjectionType::Perspective;
        float FieldOfView = 60.0f; // degrees
        float AspectRatio = 16.0f / 9.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        float OrthoWidth = 10.0f;  // Orthographic用
        float OrthoHeight = 10.0f; // Orthographic用

        // ビューポート
        ViewportRect Viewport;
    };

    // ========================================
    // ライトデータ
    // ========================================

    /**
     * @brief ライトタイプ
     */
    enum class LightType : uint8_t
    {
        Directional,
        Point,
        Spot
    };

    /**
     * @brief ライトデータ（フレームパケット用）
     */
    struct LightData
    {
        LightType Type = LightType::Directional;

        // 位置/方向
        float PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;
        float DirectionX = 0.0f, DirectionY = -1.0f, DirectionZ = 0.0f;

        // 色と強度
        float ColorR = 1.0f, ColorG = 1.0f, ColorB = 1.0f;
        float Intensity = 1.0f;

        // 減衰 (Point/Spot用)
        float Range = 10.0f;
        float AttenuationConstant = 1.0f;
        float AttenuationLinear = 0.09f;
        float AttenuationQuadratic = 0.032f;

        // スポットライト用
        float InnerConeAngle = 12.5f; // degrees
        float OuterConeAngle = 17.5f; // degrees

        // シャドウ
        bool bCastShadows = false;
        float ShadowBias = 0.005f;
    };

} // namespace NorvesLib::Core::Rendering
