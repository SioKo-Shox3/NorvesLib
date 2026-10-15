#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "RHI/RHITypes.h"
#include "Rendering/MegaGeometry/GeometryPageSource.h"
#include "Rendering/RenderTypes.h"
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

        // 焼き込み済みのLOD階層（NVMESH v1）の親のグループ。Flags の BAKED_LOD ビットが立つクラスタだけが読む。
        // 自分（BoundsCenter・BoundsRadius・LODError）と親のグループ（ここ）を、それぞれの球から画面へ
        // 投影した誤差で判定し、同じグループのクラスタが同じ判断になる。
        float ParentCenterX;
        float ParentCenterY;
        float ParentCenterZ;
        float ParentRadius;

        uint32_t Flags;       // GPU_CLUSTER_FLAG_*
        float ParentError;    // 親のグループの誤差（根は使わない）
        uint32_t GroupId;     // 親のグループの番号（根は INVALID_CLUSTER_GROUP_ID）
        uint32_t PageId;      // このクラスタを持つページの番号（メッシュの中の番号。ページの表の添字は PageTableBase + PageId）

        // このクラスタを作ったグループ（もっと細かい子のクラスタ）を持つページの番号。最も細かい段のクラスタは
        // INVALID_PAGE_ID。もっと細かい子が欲しいのにこのページが常駐していないとき、このクラスタが代わりに描かれる
        uint32_t ChildPageId;
        uint32_t PageReserved[3];
    };
    static_assert(sizeof(GPUClusterData) == 112, "Common/MegaGeometryCull.glsl の GPUClusterData "
                                                 "と大きさが一致しません");

    /** @brief GPUClusterData::Flags:
     * 焼き込み済みの階層のクラスタ（親のグループの球と誤差で段を選ぶ） */
    constexpr uint32_t GPU_CLUSTER_FLAG_BAKED_LOD = 1u;

    /** @brief ページの番号が無いこと（最も細かい段のクラスタの
     * ChildPageId）。Common/MegaGeometryCull.glsl の INVALID_PAGE_ID と同じ */
    constexpr uint32_t INVALID_PAGE_ID = 0xFFFFFFFFu;

    /**
     * @brief クラスタのグループの BVH の1節（NVMESH v1.1 の
     * CookedMeshGroupBVHNode と同じ内容。GPU の節の並び）
     *
     * 内部の節は子の節の連続した範囲（First から Count
     * 個）を、葉はクラスタの連続した範囲（First から Count 個）を持つ。
     * 節は幅優先の並びで、根が 0
     * 番。境界球は下のクラスタの球と親の球をすべて包み、MaxParentError は下の
     * クラスタの親の誤差の最大以上（根のクラスタを持つ節は最大値）。節の球から投影した
     * MaxParentError が許容以下なら、
     * 下のどのクラスタも描かれないので、枝ごと切れる。cluster_bvh_cull.comp の
     * BvhNode と一致させる。
     */
    struct alignas(16) GPUGroupBVHNode
    {
        float BoundsCenterX;
        float BoundsCenterY;
        float BoundsCenterZ;
        float BoundsRadius;
        float MaxParentError;
        uint32_t First;
        uint32_t Count;
        uint32_t Flags; // GPU_GROUP_BVH_NODE_FLAG_*
    };
    static_assert(sizeof(GPUGroupBVHNode) == 32, "cluster_bvh_cull.comp の BvhNode と大きさが一致しません");

    /** @brief GPUGroupBVHNode::Flags: 葉（First から Count 個のクラスタを持つ） */
    constexpr uint32_t GPU_GROUP_BVH_NODE_FLAG_LEAF = 1u;
    /** @brief GPUGroupBVHNode::Flags（GPU へ書いた葉だけ）: First・Count がクラスタの番号ではなく、節の配列の直後に置いた
     * 並べ替えの表（uint32 の列。位置 → クラスタの番号）の位置（実行時に作った BVH。MegaGeometryBvhBuilder.h）。
     * Common/MegaGeometryCull.glsl の BVH_NODE_FLAG_CLUSTER_ORDER と同じ */
    constexpr uint32_t GPU_GROUP_BVH_NODE_FLAG_CLUSTER_ORDER = 2u;
    /** @brief 葉が持つクラスタの最大数・内部の節が持つ子の最大数・BVH
     * の段の最大数（NVMESH v1.1 と同じ） */
    constexpr uint32_t GROUP_BVH_MAX_LEAF_CLUSTERS = 8;
    constexpr uint32_t GROUP_BVH_MAX_CHILDREN = 8;
    constexpr uint32_t GROUP_BVH_MAX_LEVELS = 16;

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
        uint32_t PageId = 0; // このクラスタを持つページの番号（NVMESH v1.1 の Pages の添字。v1.0 以前は 0）
        /**
         * @brief
         * このクラスタを作ったグループ（1つ細かい段）の番号（ClusterGroups
         * の添字）。無ければ 0xFFFFFFFF
         *
         * 焼き込み時に記録された番号。ある間は ComputeGeometryPageLinks
         * が値の照合をせずに、そのグループのページを子のページにする。
         * 最も細かい段のクラスタと、番号を持たない旧い資産は無し（境界球と誤差の値の照合へ戻る）。
         */
        uint32_t SourceGroupId = 0xFFFFFFFFu;
        /**
         * @brief
         * このクラスタを作ったグループ（もっと細かい子のクラスタ）を持つページの番号。無ければ
         * INVALID_PAGE_ID
         *
         * CreateMegaMesh
         * がグループの表から求める（ComputeGeometryPageLinks）。呼び出し側は設定しない。
         */
        uint32_t ChildPageId = 0xFFFFFFFFu;
    };

    /** @brief クラスタのグループが無いことを表す番号（焼き込み済みの階層の根） */
    constexpr uint32_t INVALID_CLUSTER_GROUP_ID = 0xFFFFFFFFu;

    /**
     * @brief メッシュのページ 1 つ（NVMESH v1.1 のページの表の 1 行に対応する）
     *
     * 範囲は MegaMeshCreateInfo
     * の全体の配列（Clusters・VertexData・IndexData）の中の位置。
     * 根のページ（bRoot）は 0
     * 番から連続して並び、メッシュを作るときに区画へ書いて常駐させる。
     * それ以外のページは、頂点とインデックスをストリーマが要求から読んで区画へ書く。
     * IndexData
     * の範囲はクラスタのインデックスだけで、フォールバックのインデックスは含まない。
     */
    struct MeshPageInfo
    {
        bool bRoot = false;
        uint32_t FirstCluster = 0;
        uint32_t ClusterCount = 0;
        uint32_t FirstVertex = 0;
        uint32_t VertexCount = 0;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
    };

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
     * @brief 地形の混ぜ合わせの 1 層（アルベド・法線・ORM の 3 枚）
     */
    struct MegaTerrainLayer
    {
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle ORMTexture; // R=AO・G=粗さ・B=メタリック。AO は高さ（凹凸）の混ぜの高さにも使う
        bool bNormalTwoChannel = false;
        float UVScale = 1.0f; // 材質の UV に掛ける倍率（大きいほど細かく繰り返す）

        bool operator==(const MegaTerrainLayer &other) const
        {
            return AlbedoTexture == other.AlbedoTexture && NormalTexture == other.NormalTexture &&
                   ORMTexture == other.ORMTexture && bNormalTwoChannel == other.bNormalTwoChannel &&
                   UVScale == other.UVScale;
        }
    };

    /**
     * @brief 地形の 3 層（砂・乾いた土・岩肌）の混ぜ合わせ
     *
     * 1 層目（砂）は材質の Albedo・Normal・ORM の枠をそのまま使い、ここは 2 層目（乾いた土）・3 層目（岩肌）と混ぜ方を持つ。
     * 混ぜ方はシェーダー（Assets/Shaders/Common/TerrainLayerBlend.glsl）が画素ごとに決める:
     *   1. 地表の分類の地図（SurfaceMaskTexture）があり、画素が地図の範囲の中なら、地図の重みを 3 層へ写す（露岩 → 岩肌、
     *      崖錐 → 土、地表（細粒）→ 砂、地表（礫）→ SurfaceMaskGravelToDirt の割合で土・残りを砂）。地図はワールドの XZ で引くので、
     *      地形の LOD の段で分類が変わらない。地図の外と、外縁から SurfaceMaskEdgeFadeMeters の帯では下の傾斜の式へ移す。
     *      傾斜の式（予備）: 傾斜（幾何の法線と真上のなす角）に、低周波ノイズの揺らぎと高さによる寄せを足し、土・岩肌へ移る角度の範囲で重みを作る。
     *   2. 各層の高さ（凹凸。土・岩肌は ORM の AO、砂は SandFillHeight の一定の面）に重みを足し、最も高い層から
     *      BlendDepth 以内の層だけを残す（岩肌のくぼみは低いので、境目では砂がくぼみに溜まる）。
     * 標本: 3 層ともトライプラナー（崖でも引き伸ばされない。平らな所は真上からの投影だけ）。カメラからの距離が DetailStartDistance の
     * DetailDistanceRatio 倍になるごとに UV を 1/DetailDistanceRatio にした段へ移り（近くは細かく、遠くは大きく）、隣の 2 段を
     * 混ぜる。混ぜた後のアルベドに、MacroWavelengths の低周波ノイズで明るさ・色味の揺らぎを掛ける。
     * UV はワールドの位置 ÷ TextureTileMeters（メッシュの UV は使わない）。
     * bEnabled が偽、または 2・3 層目のテクスチャがそろわないときは、従来どおり 1 層の材質として描く。
     * 混ぜ合わせが効くのは MegaGeometry の描画（ハードのラスタ・可視バッファの解決）で、影・レイトレーシングは 1 層目で扱う。
     */
    struct MegaTerrainBlend
    {
        bool bEnabled = false;
        MegaTerrainLayer Dirt; // 2 層目（乾いた土）
        MegaTerrainLayer Rock; // 3 層目（岩肌）

        float DirtSlopeDegrees[2] = {12.0f, 22.0f}; // 乾いた土へ移る傾斜（度）の始まりと終わり
        float RockSlopeDegrees[2] = {30.0f, 42.0f}; // 岩肌へ移る傾斜（度）の始まりと終わり
        float DirtHeightRange[2] = {10.0f, 60.0f};  // 高さ（ワールドの y、m）で土へ寄せる範囲
        float DirtHeightBiasDegrees = 8.0f;         // 範囲の上端で土の判定の傾斜へ足す角度（度）
        float NoiseWavelengths[2] = {48.0f, 13.0f}; // 境目を揺らす低周波ノイズの 2 つの波長（m）
        float NoiseAmplitudeDegrees = 7.0f;         // 揺らぎの振幅（度）
        float SandFillHeight = 0.6f;                // 砂の高さ（凹凸の混ぜ。土・岩肌の AO と同じ 0〜1 の尺度）
        float BlendDepth = 0.2f;                    // 高さ（凹凸）の混ぜの深さ（0 に近いほど境目が鋭い）
        float MacroWavelengths[2] = {90.0f, 320.0f}; // 明るさ・色味の揺らぎの 2 つの波長（m）
        float MacroBrightness = 0.2f;               // 明るさの揺らぎの振幅（割合。±）
        float MacroHue = 0.05f;                     // 色味（赤み・黄み）の揺らぎの振幅（割合。±）
        float DetailStartDistance = 12.0f;          // UV の倍率を変え始める距離（m）
        float DetailDistanceRatio = 3.0f;           // 1 段ごとの距離の比（段ごとに UV をこの値の逆数にする）
        float DetailMaxLevel = 3.0f;                // 段の上限
        float TextureTileMeters = 4.0f;             // テクスチャの 1 枚が覆う広さ（m。層の UV の倍率 1 のとき）

        // 地表の分類の地図（線形の RGBA。R = 露岩・G = 崖錐・B = 地表（細粒）・A = 地表（礫）の重み。無効なら傾斜の式だけで混ぜる）
        TextureHandle SurfaceMaskTexture;
        float SurfaceMaskMinXZ[2] = {0.0f, 0.0f};   // 地図の外縁のワールドの X・Z の最小（地図の u = 0・v = 0）
        float SurfaceMaskMaxXZ[2] = {0.0f, 0.0f};   // 地図の外縁のワールドの X・Z の最大（u = 1・v = 1）
        float SurfaceMaskGravelToDirt = 0.5f;       // 地表（礫）の重みのうち土へ回す割合（残りは砂）
        float SurfaceMaskEdgeFadeMeters = 32.0f;    // 地図の外縁から内側へ、傾斜の式から地図の重みへ移す帯の幅（m）
        float SurfaceMaskWarpMeters = 2.0f;         // 地図を引く位置を低周波ノイズでずらす幅（m。画素の格子の直線の境を崩す）

        bool operator==(const MegaTerrainBlend &other) const
        {
            if (bEnabled != other.bEnabled)
            {
                return false;
            }
            if (!bEnabled)
            {
                return true;
            }
            return Dirt == other.Dirt && Rock == other.Rock &&
                   DirtSlopeDegrees[0] == other.DirtSlopeDegrees[0] && DirtSlopeDegrees[1] == other.DirtSlopeDegrees[1] &&
                   RockSlopeDegrees[0] == other.RockSlopeDegrees[0] && RockSlopeDegrees[1] == other.RockSlopeDegrees[1] &&
                   DirtHeightRange[0] == other.DirtHeightRange[0] && DirtHeightRange[1] == other.DirtHeightRange[1] &&
                   DirtHeightBiasDegrees == other.DirtHeightBiasDegrees &&
                   NoiseWavelengths[0] == other.NoiseWavelengths[0] && NoiseWavelengths[1] == other.NoiseWavelengths[1] &&
                   NoiseAmplitudeDegrees == other.NoiseAmplitudeDegrees && SandFillHeight == other.SandFillHeight &&
                   BlendDepth == other.BlendDepth && MacroWavelengths[0] == other.MacroWavelengths[0] &&
                   MacroWavelengths[1] == other.MacroWavelengths[1] && MacroBrightness == other.MacroBrightness &&
                   MacroHue == other.MacroHue && DetailStartDistance == other.DetailStartDistance &&
                   DetailDistanceRatio == other.DetailDistanceRatio && DetailMaxLevel == other.DetailMaxLevel &&
                   TextureTileMeters == other.TextureTileMeters && SurfaceMaskTexture == other.SurfaceMaskTexture &&
                   SurfaceMaskMinXZ[0] == other.SurfaceMaskMinXZ[0] && SurfaceMaskMinXZ[1] == other.SurfaceMaskMinXZ[1] &&
                   SurfaceMaskMaxXZ[0] == other.SurfaceMaskMaxXZ[0] && SurfaceMaskMaxXZ[1] == other.SurfaceMaskMaxXZ[1] &&
                   SurfaceMaskGravelToDirt == other.SurfaceMaskGravelToDirt &&
                   SurfaceMaskEdgeFadeMeters == other.SurfaceMaskEdgeFadeMeters &&
                   SurfaceMaskWarpMeters == other.SurfaceMaskWarpMeters;
        }
    };

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
        // 材質のコピーとともに匿名テクスチャの登録を保持する。名前付きキャッシュは別所有。
        VariableArray<TSharedPtr<const void>> TextureOwners;
        float BaseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // ベースカラー RGBA
        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};   // Y=1 chromaticity
        float EmissiveLuminanceNits = 0.0f;            // 輝度(nits)
        // 負のmetal/roughは従来の黒/白。textureがあればscalarを再乗算しない。
        float Metallic = -1.0f, Roughness = -1.0f, OcclusionStrength = 1.0f;

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
         * @brief
         * 高さの場で変位させたメッシュの、LOD0の頂点の間隔（UV単位。0以下なら変位していない）
         *
         * 正なら、法線マップのうち頂点の間隔より粗い傾き（形で表した分）を差し引いた細部だけを、
         * 変位した形の法線の上に載せる（同じ凹凸の傾きを二重に掛けない）。
         */
        float DisplacementUVSpacing = 0.0f;

        // 地形の 3 層の混ぜ合わせ（bEnabled のときだけ。上の Albedo・Normal・ORM が 1 層目）
        MegaTerrainBlend TerrainBlend;
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

        /** @brief
         * 影とレイトレーシングに使うLODの段（その段のクラスタが統合インデックスで連続している必要がある）
         */
        uint32_t ShadowLODLevel = 0;

        /**
         * @brief 焼き込み済みのLOD階層（NVMESH v1）を渡すとき true
         *
         * true なら Clusters
         * は全段のクラスタ（ParentBounds・ParentError・GroupId
         * つき）で、ClusterGroups が グループの表。bBuildLODHierarchy は false
         * のまま。IndexData には、クラスタのインデックスの後ろに
         * フォールバックの段のインデックスが続く。
         */
        bool bBakedLODHierarchy = false;
        VariableArray<MeshClusterGroup> ClusterGroups;
        uint32_t BakedLODLevelCount = 1;

        /**
         * @brief クラスタのグループの BVH（NVMESH
         * v1.1。焼き込み済みの階層だけ。空なら平らなクラスタの列で判定する）
         *
         * 幅優先の並びで、葉の範囲は Clusters
         * の添字。構造（子の位置・葉の範囲・全クラスタがちょうど1つの葉に入ること）が
         * 正しくなければ CreateMegaMesh が拒否する。
         */
        VariableArray<GPUGroupBVHNode> GroupBVH;

        /**
         * @brief ページごとの範囲（NVMESH
         * v1.1。焼き込み済みの階層だけ。空ならページに分けず全部を常駐させる）
         *
         * Pages と PageSource の両方があり、ページが 2 つ以上ある（かつ
         * SetPageStreamingEnabled が有効な）ときだけ、
         * 根のページの頂点・インデックスだけを区画へ書き、残りのページは
         * PageSource から読んでストリーミングする。 そうでなければ
         * VertexData・IndexData の全体を書く。
         */
        VariableArray<MeshPageInfo> Pages;
        /** @brief ページの中身の読み込み元。null ならストリーミングしない */
        TSharedPtr<IGeometryPageSource> PageSource;

        /**
         * @brief RTと影のための常駐の粗い段のインデックスの範囲（IndexData
         * の要素の位置と数。基点の頂点は 0）
         *
         * Count が 0 なら無し（従来の段の選び方）。FallbackError
         * はその段のローカル空間の誤差。
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
        /** @brief その段のLODの誤差（クラスタの LODError
         * の最大。ローカル空間の長さ） */
        float Error = 0.0f;
    };

    /**
     * @brief MegaMesh GPUデータ
     *
     * GPU上のリソースハンドルを保持する内部構造体。
     *
     * 頂点・インデックス・クラスタは、ジオメトリの共有プール（GeometryPool）の1つの区画に並べて置く。
     * 3つのバッファのハンドルはどれも同じプールの塊のバッファを指し、メッシュの領域はそれぞれ
     * 〜OffsetBytes（塊のバッファの先頭からのバイト）と
     * 〜Bytes（その領域の大きさ）で表す。 バッファの GetSize()
     * は塊の大きさなので、メッシュの大きさには使わない。
     */
    struct MegaMeshGPUData
    {
        RHI::BufferPtr VertexBuffer;  // 統合頂点を持つプールの塊のバッファ
        RHI::BufferPtr IndexBuffer;   // 統合インデックスを持つプールの塊のバッファ
        RHI::BufferPtr ClusterBuffer; // クラスタデータ（SSBO）を持つプールの塊のバッファ

        /** @brief
         * 塊のバッファの中での各領域の先頭（バイト）。頂点は頂点バッファのオフセット、インデックスはインデックスバッファのオフセットに使う
         */
        uint64_t VertexBufferOffsetBytes = 0;
        uint64_t IndexBufferOffsetBytes = 0;
        uint64_t ClusterBufferOffsetBytes = 0;
        /** @brief 各領域の大きさ（バイト。パディングを含まない） */
        uint64_t VertexBufferBytes = 0;
        uint64_t IndexBufferBytes = 0;
        uint64_t ClusterBufferBytes = 0;

        /**
         * @brief クラスタのグループの BVH
         * の領域（塊のバッファの先頭からのバイトと大きさ。BVH が無ければ大きさ
         * 0）
         *
         * クラスタ・頂点・インデックスと同じ塊のバッファにあり、GPU
         * はデバイスアドレス（塊の先頭 + オフセット）で引く。
         * 実行時に作った BVH は、節の配列の直後にクラスタの並べ替えの表（ClusterCount 個の uint32）が続き、大きさに含む。
         */
        uint64_t GroupBVHBufferOffsetBytes = 0;
        uint64_t GroupBVHBufferBytes = 0;
        uint32_t GroupBVHNodeCount = 0;
        uint32_t GroupBVHLeafCount = 0;
        /** @brief BVH の段ごとの節の数（添字が段。段 k
         * の節は幅優先の並びで連続する。BVH が無ければ空） */
        VariableArray<uint32_t> GroupBVHLevelNodeCounts;

        /**
         * @brief
         * ページの表（全メッシュ共通。GeometryPageTable）の中の、このメッシュのページの範囲
         *
         * メッシュの中のページの番号 p の表の位置は PageTableBase + p。PageCount
         * はクラスタの PageId の最大 + 1 （ページを持たないメッシュは 1
         * で、常駐のまま）。
         */
        uint32_t PageTableBase = 0;
        uint32_t PageCount = 0;
        /** @brief
         * 常駐のまま固定したページの数（子のページの生成元を決められないグループのページ。GeometryPageLinks.h）
         */
        uint32_t PinnedPageCount = 0;
        /** @brief
         * 根のページだけを常駐させ、残りのページをストリーマが読み込むメッシュか
         */
        bool bPagesStreamed = false;

        /**
         * @brief プールの区画の共有の持ち主（型を消した参照）
         *
         * 区画はこの参照が全部消え、かつ最後に使った GPU
         * の提出が完了するまで空きへ戻らない。 FramePacket
         * のレイトレーシングのスナップショットは、フレームが消えるまで区画を使うので、これを複製して持つ。
         */
        TSharedPtr<void> RegionOwner;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;
        /** @brief
         * 影とレイトレーシングで使うLODの段の、統合インデックスでの開始位置 */
        uint32_t ShadowFirstIndex = 0;
        /** @brief
         * 影とレイトレーシングで使うLODの段のインデックス数（0なら影へ描かない）
         */
        uint32_t ShadowIndexCount = 0;
        /** @brief
         * 影に使う最も細かい段（MegaMeshCreateInfo::ShadowLODLevel。焼き込み済みの階層ではフォールバックの段）
         */
        uint32_t ShadowLODLevel = 0;
        /** @brief
         * 段ごとの描画の範囲と誤差（添え字が段。影へテクセルに見合う段を選ぶのに使う。焼き込み済みの階層では末尾にフォールバックの段が付く）
         */
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
