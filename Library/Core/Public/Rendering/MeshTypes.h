#pragma once

#include "RenderTypes.h"
#include "VertexLayout.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // サブメッシュ情報
    // ========================================

    /**
     * @brief サブメッシュ定義
     *
     * 1つのメッシュ内で異なるマテリアルを使用する部分を定義します。
     */
    struct SubMesh
    {
        uint32_t IndexStart = 0;    // インデックスバッファ内の開始位置
        uint32_t IndexCount = 0;    // インデックス数
        uint32_t VertexStart = 0;   // ベース頂点オフセット
        uint32_t MaterialIndex = 0; // 使用するマテリアルスロットのインデックス
        BoundingBox Bounds;         // サブメッシュのローカルバウンディングボックス

        SubMesh() = default;

        SubMesh(uint32_t indexStart, uint32_t indexCount,
                uint32_t vertexStart = 0, uint32_t materialIndex = 0)
            : IndexStart(indexStart), IndexCount(indexCount),
              VertexStart(vertexStart), MaterialIndex(materialIndex)
        {
        }
    };

    // ========================================
    // マテリアルスロット
    // ========================================

    /**
     * @brief マテリアルスロット定義
     *
     * メッシュが使用するマテリアルのスロット情報を定義します。
     * 実際のマテリアルはMeshComponentでオーバーライド可能です。
     */
    struct MaterialSlot
    {
        Container::String Name;          // スロット名（例: "Body", "Face"）
        MaterialHandle DefaultMaterial;  // デフォルトマテリアル

        MaterialSlot() = default;

        explicit MaterialSlot(const Container::String &name)
            : Name(name)
        {
        }

        MaterialSlot(const Container::String &name, MaterialHandle material)
            : Name(name), DefaultMaterial(material)
        {
        }
    };

    // ========================================
    // メッシュ作成情報
    // ========================================

    /**
     * @brief メッシュ作成情報
     *
     * MeshResourceManagerにメッシュを作成させるための情報。
     * Game側からはこの構造体を通じてのみメッシュを作成できます。
     */
    struct MeshCreateInfo
    {
        // 頂点データ（不透明なポインタ - 内部でコピーされる）
        const void *VertexData = nullptr;
        size_t VertexDataSize = 0;
        uint32_t VertexCount = 0;

        // インデックスデータ（不透明なポインタ - 内部でコピーされる）
        const void *IndexData = nullptr;
        size_t IndexDataSize = 0;
        uint32_t IndexCount = 0;

        // メッシュ設定
        VertexLayout Layout;
        IndexFormat IndexType = IndexFormat::UInt32;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        // サブメッシュ情報
        Container::VariableArray<SubMesh> SubMeshes;

        // マテリアルスロット
        Container::VariableArray<MaterialSlot> MaterialSlots;

        // バウンディング情報
        BoundingBox Bounds;

        // オプション
        bool bKeepCPUData = false; // CPU側にデータを保持するか
        Container::String DebugName; // デバッグ用名前
    };

    // ========================================
    // メッシュGPUデータ（内部用）
    // ========================================

    /**
     * @brief GPU上のメッシュデータ
     *
     * RenderingシステムがGPUリソースを管理するための内部構造体。
     * Game側からは直接アクセスできません。
     */
    struct MeshGPUData
    {
        BufferHandle VertexBuffer;
        BufferHandle IndexBuffer;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;

        VertexLayoutHandle LayoutHandle;
        IndexFormat IndexType = IndexFormat::UInt32;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        // サブメッシュ情報のコピー
        Container::VariableArray<SubMesh> SubMeshes;

        // バウンディング情報
        BoundingBox Bounds;
        BoundingSphere BoundingSphere;

        // 参照カウント（リソース管理用）
        uint32_t RefCount = 0;

        // デバッグ情報
        Container::String DebugName;
    };

    // ========================================
    // メッシュ記述子（Game側向け）
    // ========================================

    /**
     * @brief メッシュ記述子
     *
     * Game側がメッシュの情報を参照するための読み取り専用構造体。
     * 実際のGPUリソースへのアクセスは提供しません。
     */
    struct MeshDescriptor
    {
        MeshDataHandle Handle;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t SubMeshCount = 0;
        uint32_t MaterialSlotCount = 0;

        VertexLayout Layout;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        BoundingBox Bounds;
        BoundingSphere BoundSphere;

        Container::String Name;

        bool IsValid() const { return Handle.IsValid(); }
    };

    // ========================================
    // LOD情報
    // ========================================

    /**
     * @brief LODレベル定義
     */
    struct LODLevel
    {
        MeshDataHandle MeshHandle;   // このLODレベルのメッシュ
        float ScreenSizeThreshold;   // このLODに切り替わる画面サイズしきい値
        float TransitionWidth;       // トランジション幅（ディザリング用）
    };

    /**
     * @brief LODグループ
     *
     * 複数のLODレベルを持つメッシュグループ
     */
    struct LODGroup
    {
        Container::VariableArray<LODLevel> Levels;

        /**
         * @brief 画面サイズに基づいて適切なLODレベルを取得
         * @param screenSize 画面上のサイズ（0.0～1.0）
         * @return LODレベルインデックス
         */
        uint32_t GetLODLevel(float screenSize) const
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(Levels.size()); ++i)
            {
                if (screenSize >= Levels[i].ScreenSizeThreshold)
                {
                    return i;
                }
            }
            return static_cast<uint32_t>(Levels.size()) - 1;
        }
    };

    // ========================================
    // メッシュインスタンシング情報
    // ========================================

    /**
     * @brief インスタンスデータレイアウト
     *
     * GPUインスタンシング用のインスタンスごとのデータレイアウト
     */
    struct InstanceDataLayout
    {
        Container::FixedArray<VertexElement, 8> Elements;
        uint32_t ElementCount = 0;
        uint32_t Stride = 0;

        /**
         * @brief 標準インスタンスレイアウト（Transform行列）
         */
        static InstanceDataLayout CreateStandard()
        {
            InstanceDataLayout layout;
            uint32_t offset = 0;

            // ワールド行列（4xFloat4）
            layout.Elements[0] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 0);
            offset += 16;
            layout.Elements[1] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 1);
            offset += 16;
            layout.Elements[2] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 2);
            offset += 16;
            layout.Elements[3] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 3);
            offset += 16;

            layout.ElementCount = 4;
            layout.Stride = offset;
            return layout;
        }
    };

} // namespace NorvesLib::Core::Rendering
