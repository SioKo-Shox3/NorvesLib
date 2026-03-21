#pragma once

#include "Rendering/RenderTypes.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    // ========================================
    // 定数
    // ========================================

    /** @brief 1クラスタあたりの最大三角形数 */
    constexpr uint32_t MAX_TRIANGLES_PER_CLUSTER = 128;

    /** @brief 1クラスタあたりの最大頂点数 */
    constexpr uint32_t MAX_VERTICES_PER_CLUSTER = 128;

    // ========================================
    // GPU構造体（シェーダーと共有）
    // ========================================

    /**
     * @brief GPUクラスタデータ
     *
     * コンピュートシェーダー上でカリング判定に使用する構造体。
     * シェーダーSSBO内のレイアウトと一致させる必要があります。
     */
    struct alignas(16) GPUClusterData
    {
        // バウンディングスフィア（center.xyz + radius）
        float BoundsCenterX;
        float BoundsCenterY;
        float BoundsCenterZ;
        float BoundsRadius;

        // バックフェースカリング用法線コーン（axis.xyz + cos(angle)）
        float ConeAxisX;
        float ConeAxisY;
        float ConeAxisZ;
        float ConeCutoff; // cos(coneHalfAngle)。-1.0fでカリング無効

        // インデックスバッファ参照
        uint32_t IndexOffset;  // 統合IB中の開始インデックス
        uint32_t IndexCount;   // このクラスタの三角形数 × 3
        int32_t VertexOffset;  // 統合VB中のベース頂点オフセット
        uint32_t MaterialIndex; // マテリアルスロット

        // LOD情報
        uint32_t LODLevel;
        float LODError;          // このクラスタの簡略化誤差
        uint32_t ParentStart;    // 親クラスタグループの開始インデックス
        uint32_t ParentCount;    // 親クラスタグループ数
    };

    /**
     * @brief GPUインスタンスデータ
     *
     * Mega Geometryインスタンスごとのデータ。
     * インスタンスバッファに格納されます。
     */
    struct alignas(16) GPUInstanceData
    {
        float WorldMatrix[16]; // 4x4ワールド変換行列（列優先）
        uint32_t ClusterOffset; // ClusterBufferの開始インデックス
        uint32_t ClusterCount;  // このインスタンスのクラスタ数
        float BoundsRadius;     // インスタンスのバウンディング半径
        uint32_t Pad0;
    };

    /**
     * @brief VkDrawIndexedIndirectCommand互換構造体
     */
    struct DrawIndexedIndirectCommand
    {
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        int32_t VertexOffset;
        uint32_t FirstInstance;
    };

    // ========================================
    // CPU側メッシュクラスタ
    // ========================================

    /**
     * @brief メッシュクラスタ（CPU側）
     *
     * クラスタリング処理の結果として生成される中間データ。
     * GPU転送前の段階で使用します。
     */
    struct MeshCluster
    {
        // インデックス範囲（統合バッファ内）
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        int32_t VertexOffset = 0;
        uint32_t VertexCount = 0;

        // バウンディング
        BoundingSphere Bounds;

        // バックフェースカリング用法線コーン
        float ConeAxisX = 0.0f;
        float ConeAxisY = 0.0f;
        float ConeAxisZ = 0.0f;
        float ConeCutoff = -1.0f; // -1.0f = カリング無効

        // LOD
        uint32_t LODLevel = 0;
        float LODError = 0.0f;

        // マテリアル
        uint32_t MaterialIndex = 0;
    };

    // ========================================
    // MegaMesh記述子
    // ========================================

    /**
     * @brief MegaMesh作成情報
     *
     * メッシュをMegaGeometryとして登録する際の入力データ。
     */
    struct MegaMeshCreateInfo
    {
        // クラスタ化済みの頂点データ
        const void* VertexData = nullptr;
        size_t VertexDataSize = 0;
        uint32_t VertexCount = 0;
        uint32_t VertexStride = 0;

        // クラスタ化済みのインデックスデータ
        const uint32_t* IndexData = nullptr;
        uint32_t IndexCount = 0;

        // クラスタ情報
        VariableArray<MeshCluster> Clusters;

        // バウンディング
        BoundingSphere TotalBounds;

        // デバッグ
        String DebugName;
    };

    /**
     * @brief MegaMesh GPUデータ
     *
     * GPU上のリソースハンドルを保持する内部構造体。
     */
    struct MegaMeshGPUData
    {
        RHI::BufferPtr VertexBuffer;   // 統合頂点バッファ
        RHI::BufferPtr IndexBuffer;    // 統合インデックスバッファ
        RHI::BufferPtr ClusterBuffer;  // クラスタデータSSBO

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;

        BoundingSphere TotalBounds;

        String DebugName;
    };

    // ========================================
    // ハンドル型
    // ========================================

    /** @brief MegaMeshリソースのハンドルタグ */
    struct MegaMeshHandleTag {};

    /** @brief MegaMeshリソースハンドル */
    using MegaMeshHandle = ResourceHandle<MegaMeshHandleTag>;

} // namespace NorvesLib::Core::Rendering::MegaGeometry
