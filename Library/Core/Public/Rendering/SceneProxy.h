#pragma once

#include "RenderTypes.h"
#include "MeshTypes.h"
#include "MaterialTypes.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief 最大マテリアルスロット数
     */
    constexpr uint32_t MAX_MATERIAL_SLOTS = 8;

    // ========================================
    // MeshProxy
    // ========================================

    /**
     * @brief 描画用メッシュプロキシ
     *
     * GameThreadからRenderThreadへ渡される描画に必要な最小限の情報。
     * MeshComponentから同期され、RenderThreadで読み取られます。
     *
     * このデータはフレームごとにコピーされ、Triple Bufferingにより
     * GameThreadとRenderThreadが独立して動作できます。
     */
    struct MeshProxy
    {
        // ========================================
        // 識別子
        // ========================================

        uint64_t ObjectId = 0;    // 所属するObjectのID
        uint64_t ComponentId = 0; // MeshComponentのID
        uint32_t SortKey = 0;     // ソート用キー

        // ========================================
        // メッシュデータ参照
        // ========================================

        MeshDataHandle MeshHandle; // メッシュデータへのハンドル
        uint8_t LODLevel = 0;      // 現在のLODレベル

        // ========================================
        // トランスフォーム
        // ========================================

        Math::Matrix4x4 WorldTransform;         // ワールド変換行列
        Math::Matrix4x4 PreviousWorldTransform; // 前フレームの変換行列（モーションブラー用）

        // ========================================
        // カリング用バウンディング
        // ========================================

        BoundingSphere WorldBounds; // ワールド空間でのバウンディングスフィア

        // ========================================
        // マテリアル
        // ========================================

        Container::FixedArray<MaterialHandle, MAX_MATERIAL_SLOTS> Materials;
        uint32_t MaterialCount = 0;

        // マテリアルインスタンスオーバーライド（オプション）
        // パフォーマンスのため、オーバーライドがある場合のみ使用
        bool bHasMaterialOverrides = false;

        // ========================================
        // 描画フラグ
        // ========================================

        bool bVisible = true;                       // 描画するか
        bool bCastShadow = true;                    // シャドウを落とすか
        bool bReceiveShadow = true;                 // シャドウを受けるか
        bool bAffectDynamicIndirectLighting = true; // 動的間接光の影響を受けるか
        bool bAffectDistanceFieldLighting = false;  // ディスタンスフィールドライティング

        RenderLayer LayerMask = RenderLayer::Default; // レンダーレイヤーマスク

        // ========================================
        // カスタムデータ
        // ========================================

        float CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // シェーダーに渡すカスタムデータ

        // ========================================
        // ユーティリティ
        // ========================================

        /**
         * @brief プロキシが有効かどうか
         */
        bool IsValid() const
        {
            return MeshHandle.IsValid() && bVisible;
        }

        /**
         * @brief マテリアルを設定
         * @param index スロットインデックス
         * @param material マテリアルハンドル
         */
        void SetMaterial(uint32_t index, MaterialHandle material)
        {
            if (index < MAX_MATERIAL_SLOTS)
            {
                Materials[index] = material;
                if (index >= MaterialCount)
                {
                    MaterialCount = index + 1;
                }
            }
        }

        /**
         * @brief ワールドバウンディングスフィアを更新
         * @param localBounds ローカル空間のバウンディングスフィア
         */
        void UpdateWorldBounds(const BoundingSphere &localBounds)
        {
            // 簡易的な変換（スケールを考慮）
            // より正確にはワールド行列でバウンディングボックスを変換すべき
            WorldBounds.CenterX = WorldTransform.m30 + localBounds.CenterX;
            WorldBounds.CenterY = WorldTransform.m31 + localBounds.CenterY;
            WorldBounds.CenterZ = WorldTransform.m32 + localBounds.CenterZ;

            // スケールの概算（最大成分を使用）
            float scaleX = std::sqrt(WorldTransform.m00 * WorldTransform.m00 +
                                     WorldTransform.m01 * WorldTransform.m01 +
                                     WorldTransform.m02 * WorldTransform.m02);
            float scaleY = std::sqrt(WorldTransform.m10 * WorldTransform.m10 +
                                     WorldTransform.m11 * WorldTransform.m11 +
                                     WorldTransform.m12 * WorldTransform.m12);
            float scaleZ = std::sqrt(WorldTransform.m20 * WorldTransform.m20 +
                                     WorldTransform.m21 * WorldTransform.m21 +
                                     WorldTransform.m22 * WorldTransform.m22);
            float maxScale = scaleX > scaleY ? (scaleX > scaleZ ? scaleX : scaleZ)
                                             : (scaleY > scaleZ ? scaleY : scaleZ);
            WorldBounds.Radius = localBounds.Radius * maxScale;
        }

        /**
         * @brief ソートキーを計算
         * @param cameraPosition カメラ位置
         * @param blendMode ブレンドモード
         */
        void CalculateSortKey(float cameraX, float cameraY, float cameraZ, BlendMode blendMode)
        {
            // ブレンドモードを最上位ビットに
            uint32_t blendBits = static_cast<uint32_t>(blendMode) << 28;

            // 距離を16ビットに圧縮
            float dx = WorldBounds.CenterX - cameraX;
            float dy = WorldBounds.CenterY - cameraY;
            float dz = WorldBounds.CenterZ - cameraZ;
            float distSq = dx * dx + dy * dy + dz * dz;

            // 透明オブジェクトは後ろから前へ（距離大→小）
            // 不透明オブジェクトは前から後ろへ（距離小→大）
            uint32_t distBits;
            if (blendMode == BlendMode::Opaque || blendMode == BlendMode::Masked)
            {
                distBits = static_cast<uint32_t>(distSq) & 0x0FFFFFFF;
            }
            else
            {
                distBits = (0x0FFFFFFF - (static_cast<uint32_t>(distSq) & 0x0FFFFFFF));
            }

            SortKey = blendBits | distBits;
        }
    };

    // ========================================
    // LightProxy
    // ========================================

    /**
     * @brief 描画用ライトプロキシ
     *
     * GameThreadからRenderThreadへ渡されるライト情報
     */
    struct LightProxy
    {
        uint64_t LightId = 0;

        LightType Type = LightType::Directional;

        // 位置/方向
        float PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;
        float DirectionX = 0.0f, DirectionY = -1.0f, DirectionZ = 0.0f;

        // 色と強度
        float ColorR = 1.0f, ColorG = 1.0f, ColorB = 1.0f;
        float Intensity = 1.0f;

        // 減衰
        float Range = 10.0f;
        float AttenuationConstant = 1.0f;
        float AttenuationLinear = 0.09f;
        float AttenuationQuadratic = 0.032f;

        // スポットライト
        float InnerConeAngle = 12.5f;
        float OuterConeAngle = 17.5f;

        // シャドウ
        bool bCastShadows = false;
        float ShadowBias = 0.005f;
        uint32_t ShadowMapResolution = 1024;

        // 可視性
        bool bVisible = true;
        RenderLayer AffectedLayers = RenderLayer::All;

        bool IsValid() const
        {
            return bVisible && Intensity > 0.0f;
        }
    };

    // ========================================
    // CameraProxy
    // ========================================

    /**
     * @brief 描画用カメラプロキシ
     */
    struct CameraProxy
    {
        uint64_t CameraId = 0;

        // ビュー行列用
        float PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;
        float ForwardX = 0.0f, ForwardY = 0.0f, ForwardZ = -1.0f;
        float UpX = 0.0f, UpY = 1.0f, UpZ = 0.0f;
        float RightX = 1.0f, RightY = 0.0f, RightZ = 0.0f;

        // プロジェクション
        ProjectionType Projection = ProjectionType::Perspective;
        float FieldOfView = 60.0f;
        float AspectRatio = 16.0f / 9.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        float OrthoWidth = 10.0f;
        float OrthoHeight = 10.0f;

        // ビューポート
        ViewportRect Viewport;

        // レンダリング設定
        RenderLayer CullingMask = RenderLayer::All;
        uint8_t RenderOrder = 0; // 複数カメラの描画順序

        // ポストプロセス設定（ハンドル参照）
        // PostProcessHandle PostProcess;

        bool IsValid() const
        {
            return Viewport.Width > 0.0f && Viewport.Height > 0.0f;
        }
    };

    // ========================================
    // SceneProxy
    // ========================================

    /**
     * @brief シーン全体のプロキシ
     *
     * 1フレーム分のシーン情報をまとめた構造体
     */
    struct SceneProxy
    {
        // メインカメラ
        CameraProxy MainCamera;

        // 追加カメラ（マルチビュー用）
        Container::VariableArray<CameraProxy> AdditionalCameras;

        // メッシュプロキシリスト
        Container::VariableArray<MeshProxy> MeshProxies;

        // ライトプロキシリスト
        Container::VariableArray<LightProxy> LightProxies;

        // 環境設定
        float AmbientColorR = 0.1f, AmbientColorG = 0.1f, AmbientColorB = 0.1f;
        float AmbientIntensity = 1.0f;

        // フォグ設定
        bool bFogEnabled = false;
        float FogColorR = 0.5f, FogColorG = 0.5f, FogColorB = 0.5f;
        float FogDensity = 0.01f;
        float FogStart = 10.0f;
        float FogEnd = 100.0f;

        /**
         * @brief シーンをクリア
         */
        void Clear()
        {
            MeshProxies.clear();
            LightProxies.clear();
            AdditionalCameras.clear();
        }

        /**
         * @brief メッシュプロキシを追加
         */
        void AddMeshProxy(const MeshProxy &proxy)
        {
            if (proxy.IsValid())
            {
                MeshProxies.push_back(proxy);
            }
        }

        /**
         * @brief ライトプロキシを追加
         */
        void AddLightProxy(const LightProxy &proxy)
        {
            if (proxy.IsValid())
            {
                LightProxies.push_back(proxy);
            }
        }
    };

} // namespace NorvesLib::Core::Rendering
