#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ImpostorMath.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderTypes.h"
#include "Math/Vector3.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
    class TextureResources;
    class ShaderManager;

    /** @brief オブジェクト空間の境界とアトラスの格子。セル番号は cy * AxisCellCountX + cx。 */
    struct ImpostorBakeMetadata
    {
        uint32_t CellResolution = 0;
        uint32_t AxisCellCountX = 0;
        uint32_t AxisCellCountY = 0;
        uint32_t AtlasWidth = 0;
        uint32_t AtlasHeight = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        TextureCreateInfo::Format PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        ImpostorOctahedralLayout Layout = ImpostorOctahedralLayout::HemiOctahedral;
        Math::Vector3 BoundsCenter;
        float BoundsRadius = 0.0f;
        Math::Vector3 BoundsMin;
        Math::Vector3 BoundsMax;

        /** @brief 描画と実行時の復元に必要な寸法・境界がそろっているかを返す。 */
        bool IsValid() const
        {
            return CellResolution > 0 &&
                   AxisCellCountX > 0 &&
                   AxisCellCountY > 0 &&
                   AtlasWidth > 0 &&
                   AtlasHeight > 0 &&
                   VertexCount > 0 &&
                   IndexCount > 0 &&
                   BoundsRadius > 0.0f;
        }
    };

    /** @brief ベイクする材質。テクスチャが無い場合は既定値または指定した定数を使う。 */
    struct ImpostorBakeMaterial
    {
        TextureHandle AlbedoTexture = TextureHandle::Invalid(); // 無ければ白。
        TextureHandle NormalTexture = TextureHandle::Invalid(); // 接空間。無ければ平らな法線。
        TextureHandle OrmTexture = TextureHandle::Invalid();    // R=AO、G=粗さ、B=金属度。
        bool bNormalTwoChannel = false;                        // BC5 の2チャンネル法線。
        Math::Vector3 BaseColor = Math::Vector3(1.0f, 1.0f, 1.0f);
        float Metallic = -1.0f;         // ORM が無い場合の定数。負は黒。
        float Roughness = -1.0f;        // ORM が無い場合の定数。負は白。
        float OcclusionStrength = 1.0f; // ORM が無い場合の定数。1 は白。
        float AlphaCutoff = 0.0f;       // 0 は切り抜かない。
        bool bApplyNormalScale = false;
        float NormalScale = 1.0f;
    };

    /** @brief 連続した三角形インデックスの区間と、その材質。 */
    struct ImpostorBakeSection
    {
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        ImpostorBakeMaterial Material;
    };

    /**
     * @brief 八面体の格子点から正射影するメッシュ。区間が空なら全体を既定材質で描く。
     * セルは X 優先で並び、各セルの行 0 が物体の上。PixelFormat はアルベドの色の扱いを指定する。
     */
    struct ImpostorBakeInput
    {
        Container::Span<const Mesh3DVertex> Vertices;
        Container::Span<const uint32_t> Indices;
        Container::VariableArray<ImpostorBakeSection> Sections;
        ImpostorOctahedralLayout Layout = ImpostorOctahedralLayout::HemiOctahedral;
        uint32_t CellResolution = 192;
        uint32_t AxisCellCountX = 8;
        uint32_t AxisCellCountY = 8;
        TextureCreateInfo::Format PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        Container::String DebugName = "ProceduralImpostorAtlas";
    };

    /** @brief 必須のテクスチャ管理・デバイス・シェーダー管理と、同期対象の描画世界。 */
    struct ImpostorBakeRequest
    {
        TextureResources* Textures = nullptr;
        RHI::IDevice* Device = nullptr;
        RenderWorld* RenderWorldForSync = nullptr;
        ShaderManager* Shaders = nullptr;
        ImpostorBakeInput Input;
    };

    /**
     * @brief アルベドと被覆率・オブジェクト空間法線・ORM・深度の4枚と境界。
     * 深度 0 は見る人の側、1 は奥または背景。成功時は全アトラスが ShaderResource 状態で、
     * GPU の書き込みも完了している。返された4ハンドルは呼び出し側が ReleaseTexture する。
     */
    struct ImpostorBakeResult
    {
        TextureHandle AtlasTexture = TextureHandle::Invalid();
        TextureHandle NormalAtlasTexture = TextureHandle::Invalid();
        TextureHandle OrmAtlasTexture = TextureHandle::Invalid();
        TextureHandle DepthAtlasTexture = TextureHandle::Invalid();
        ImpostorBakeMetadata Metadata;

        /** @brief 4枚すべてと復元用のメタデータが有効かを返す。 */
        bool Succeeded() const
        {
            return AtlasTexture.IsValid() && NormalAtlasTexture.IsValid() &&
                   OrmAtlasTexture.IsValid() && DepthAtlasTexture.IsValid() && Metadata.IsValid();
        }
    };

    /** @brief 各セルを単一の MRT パスで描画し、同期完了したアトラスを返す。 */
    class ImpostorBakeService
    {
    public:
        /** @brief X 優先のセルを描く。失敗時は作成したアトラスも解放し、結果を無効にする。 */
        bool BakeProceduralAtlas(const ImpostorBakeRequest& request,
                                 ImpostorBakeResult& outResult) const;

    private:
        /** @brief 参照された頂点の有限性・境界と、寸法・区間・八面体配置を検証する。 */
        bool ValidateInput(const ImpostorBakeRequest& request,
                           ImpostorBakeMetadata& outMetadata) const;
    };

} // namespace NorvesLib::Core::Rendering
