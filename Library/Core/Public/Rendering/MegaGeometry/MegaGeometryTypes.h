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
        uint32_t IndexOffset;   // 統合IB中の開始インデックス
        uint32_t IndexCount;    // このクラスタの三角形数 × 3
        int32_t VertexOffset;   // 統合VB中のベース頂点オフセット
        uint32_t MaterialIndex; // マテリアルスロット

        // LOD情報
        uint32_t LODLevel;
        float LODError;       // このクラスタの簡略化誤差
        uint32_t ParentStart; // 親クラスタグループの開始インデックス
        uint32_t ParentCount; // 親クラスタグループ数
    };

    /**
     * @brief GPUインスタンスデータ
     *
     * Mega Geometryインスタンスごとのデータ。
     * インスタンスバッファに格納されます。
     */
    struct alignas(16) GPUInstanceData
    {
        float WorldMatrix[16];  // 4x4ワールド変換行列（列優先）
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

        // 親クラスタリンク（LOD DAG用）
        uint32_t ParentStart = 0; // 全体クラスタ配列中の親クラスタ開始インデックス
        uint32_t ParentCount = 0; // 親クラスタ数

        // マテリアル
        uint32_t MaterialIndex = 0;

        // 焼き込み済みのLOD階層（NVMESH v1）の親のグループ。Bounds・LODError が自分の境界球と誤差で、
        // ParentBounds・ParentError が「このクラスタを含むグループを簡略化した結果」の境界球と誤差。
        // 根（親のグループが無い）は GroupId が INVALID_CLUSTER_GROUP_ID で、ParentError が最大値。
        // 実行時に構築した階層と v0 のクラスタでは使わない（既定のまま）。
        BoundingSphere ParentBounds;
        float ParentError = 3.402823466e+38f;
        uint32_t GroupId = 0xFFFFFFFFu;
        uint32_t PageId = 0; // ページのストリーミング（段5）まで 0
    };

    /** @brief クラスタのグループが無いことを表す番号（焼き込み済みの階層の根） */
    constexpr uint32_t INVALID_CLUSTER_GROUP_ID = 0xFFFFFFFFu;

    /**
     * @brief 焼き込み済みの階層のクラスタのグループ
     *
     * 同じグループのクラスタは同じ親の境界球と誤差を持ち、同じ判断で描く・描かないが決まる。
     * メンバのクラスタは MegaMeshCreateInfo::Clusters の連続した範囲に並ぶ。
     */
    struct MeshClusterGroup
    {
        BoundingSphere Bounds;
        float Error = 0.0f;
        uint32_t ClusterOffset = 0;
        uint32_t ClusterCount = 0;
        uint32_t LODLevel = 0;
    };

    // ========================================
    // クラスタグループ（LOD階層構築用）
    // ========================================

    /**
     * @brief クラスタグループ
     *
     * 空間的に近いクラスタをグループ化し、LOD階層構築時の
     * 簡略化単位として使用します。
     */
    struct ClusterGroup
    {
        VariableArray<uint32_t> ClusterIndices; // グループ内のクラスタインデックス
        BoundingSphere Bounds;                  // グループ全体のバウンディング
        uint32_t LODLevel = 0;                  // このグループの元LODレベル
    };

    // ========================================
    // LOD階層（構築結果）
    // ========================================

    /**
     * @brief LOD階層データ
     *
     * LODHierarchyBuilderの構築結果として返される。
     * 全LODレベルの統合クラスタ・頂点・インデックスデータを含みます。
     */
    struct LODHierarchy
    {
        VariableArray<MeshCluster> AllClusters; // 全LODレベルのクラスタ
        VariableArray<uint32_t> AllIndices;     // 全LODレベルの統合インデックスデータ
        VariableArray<uint8_t> AllVertices;     // 全LODレベルの統合頂点データ
        uint32_t TotalVertexCount = 0;
        uint32_t VertexStride = 0;  // 頂点ストライド
        uint32_t LODLevelCount = 0; // LODレベル数
        BoundingSphere TotalBounds; // 全体のバウンディング
    };

    // ========================================
    // MegaMesh マテリアル
    // ========================================

    /**
     * @brief MegaMeshマテリアル
     *
     * MegaMesh単位のPBRマテリアル設定。
     * テクスチャが未設定（無効なハンドル）の場合はデフォルトの1x1テクスチャが使用されます。
     * テクスチャは強参照を持たず、描画のたびにハンドルから引き直す。解放済みのハンドルは
     * 未設定と同じにデフォルトへ落ちるので、材質の持ち主が先にテクスチャを解放しても
     * GPU が使っているテクスチャを巻き込まない。
     */
    struct MegaMeshMaterial
    {
        float BaseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};     // ベースカラー RGBA
        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};       // Y=1 chromaticity
        float EmissiveLuminanceNits = 0.0f;                 // 輝度(nits)

        // PBRテクスチャ（無効なハンドルの場合はデフォルトテクスチャを使用）
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle MetallicTexture;
        TextureHandle RoughnessTexture;
        TextureHandle AOTexture;
        TextureHandle ORMTexture;           // ORMを1枚に詰めたテクスチャ（R=AO・G=粗さ・B=メタリック）。有効なら別々の枠より優先
        TextureHandle HeightTexture;

        // NormalTexture が2チャンネル（BC5）のとき true。Zはシェーダーが XY から戻す
        bool bNormalTwoChannel = false;

        // POMパラメータ
        float HeightScale = 0.0f;
        bool bHasHeightMap = false;

        /**
         * @brief 高さの場で変位させたメッシュの、LOD0の頂点の間隔（UV単位。0以下なら変位していない）
         *
         * 正なら、法線マップのうち頂点の間隔より粗い傾き（形で表した分）を差し引いた細部だけを、
         * 変位した形の法線の上に載せる（同じ凹凸の傾きを二重に掛けない）。
         */
        float DisplacementUVSpacing = 0.0f;
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
        const void *VertexData = nullptr;
        size_t VertexDataSize = 0;
        uint32_t VertexCount = 0;
        uint32_t VertexStride = 0;

        // クラスタ化済みのインデックスデータ
        const uint32_t *IndexData = nullptr;
        uint32_t IndexCount = 0;

        // クラスタ情報
        VariableArray<MeshCluster> Clusters;

        // バウンディング
        BoundingSphere TotalBounds;

        // LOD階層構築オプション
        bool bBuildLODHierarchy = true;      // LOD階層を自動構築するか
        float LODSimplificationRatio = 0.5f; // 各LODでの三角形削減比率
        uint32_t MaxLODLevels = 8;           // 最大LODレベル数
        uint32_t MinTrianglesForLOD = 128;   // LOD構築終了条件

        /**
         * @brief LODの選択に使う、メッシュ全体で共通の境界球（ローカル空間）
         *
         * 無効（半径0）なら、各クラスタが自分の境界球の中心までの距離でLODを選ぶ（従来どおり）。
         * 有効なら全クラスタがこの球の表面までの距離で選ぶため、どのLODも閉じたメッシュとして
         * 作ってあれば、メッシュ全体で同じ段が選ばれて段の境目に割れ目ができない。
         */
        BoundingSphere LODBounds;

        /** @brief 影とレイトレーシングに使うLODの段（その段のクラスタが統合インデックスで連続している必要がある） */
        uint32_t ShadowLODLevel = 0;

        /**
         * @brief 焼き込み済みのLOD階層（NVMESH v1）を渡すとき true
         *
         * true なら Clusters は全段のクラスタ（ParentBounds・ParentError・GroupId つき）で、ClusterGroups が
         * グループの表。bBuildLODHierarchy は false のまま。IndexData には、クラスタのインデックスの後ろに
         * フォールバックの段のインデックスが続く。
         */
        bool bBakedLODHierarchy = false;
        VariableArray<MeshClusterGroup> ClusterGroups;
        uint32_t BakedLODLevelCount = 1;

        /**
         * @brief RTと影のための常駐の粗い段のインデックスの範囲（IndexData の要素の位置と数。基点の頂点は 0）
         *
         * Count が 0 なら無し（従来の段の選び方）。FallbackError はその段のローカル空間の誤差。
         */
        uint32_t FallbackIndexOffset = 0;
        uint32_t FallbackIndexCount = 0;
        float FallbackError = 0.0f;

        // マテリアル
        MegaMeshMaterial Material;

        // デバッグ
        String DebugName;
    };

    /**
     * @brief MegaMeshのLODの1段を1回の描画で描くための範囲と誤差
     *
     * その段のクラスタが、頂点の基点が全て0で統合インデックスの中に隙間なく並んでいるときだけ
     * IndexCount が0でない（影へ段を選んで描くのに使う）。
     */
    struct MegaMeshLevelRange
    {
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        /** @brief その段のLODの誤差（クラスタの LODError の最大。ローカル空間の長さ） */
        float Error = 0.0f;
    };

    /**
     * @brief MegaMesh GPUデータ
     *
     * GPU上のリソースハンドルを保持する内部構造体。
     */
    struct MegaMeshGPUData
    {
        RHI::BufferPtr VertexBuffer;  // 統合頂点バッファ
        RHI::BufferPtr IndexBuffer;   // 統合インデックスバッファ
        RHI::BufferPtr ClusterBuffer; // クラスタデータSSBO

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;
        /** @brief 影とレイトレーシングで使うLODの段の、統合インデックスでの開始位置 */
        uint32_t ShadowFirstIndex = 0;
        /** @brief 影とレイトレーシングで使うLODの段のインデックス数（0なら影へ描かない） */
        uint32_t ShadowIndexCount = 0;
        /** @brief 影に使う最も細かい段（MegaMeshCreateInfo::ShadowLODLevel） */
        uint32_t ShadowLODLevel = 0;
        /** @brief 段ごとの描画の範囲と誤差（添え字が段。影へテクセルに見合う段を選ぶのに使う） */
        VariableArray<MegaMeshLevelRange> LevelRanges;

        BoundingSphere TotalBounds;
        /** @brief LODの選択に使う共通の境界球（無効ならクラスタごとの中心で選ぶ） */
        BoundingSphere LODBounds;

        MegaMeshMaterial Material;

        String DebugName;
    };

    // ========================================
    // ハンドル型
    // ========================================

    /** @brief MegaMeshリソースのハンドルタグ */
    struct MegaMeshHandleTag
    {
    };

    /** @brief MegaMeshリソースハンドル */
    using MegaMeshHandle = ResourceHandle<MegaMeshHandleTag>;

} // namespace NorvesLib::Core::Rendering::MegaGeometry
