#pragma once

#include "RenderTypes.h"
#include "MeshTypes.h"
#include "MaterialTypes.h"
#include "SkinnedMeshTypes.h"
#include "MegaGeometry/MegaGeometryTypes.h"
#include "DDGIVolume.h"
#include "SkyAtmosphere.h"
#include "VolumetricFog.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{


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

        Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS> SubMeshes;
        uint32_t SubMeshCount = 0;

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

        Container::FixedArray<MaterialHandle, MAX_MATERIAL_SLOTS> Materials =
            Container::FixedArray<MaterialHandle, MAX_MATERIAL_SLOTS>(MaterialHandle::Invalid());
        Container::FixedArray<BlendMode, MAX_MATERIAL_SLOTS> MaterialBlendModes =
            Container::FixedArray<BlendMode, MAX_MATERIAL_SLOTS>(BlendMode::Opaque);
        uint32_t MaterialCount = 0;
        float SortDepth = 0.0f;

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
            const Math::Vector3 translation = WorldTransform.GetTranslationRow();
            WorldBounds.CenterX = translation.x + localBounds.CenterX;
            WorldBounds.CenterY = translation.y + localBounds.CenterY;
            WorldBounds.CenterZ = translation.z + localBounds.CenterZ;

            // スケールの概算（最大成分を使用）
            const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(WorldTransform);
            float maxScale = scale.x > scale.y ? (scale.x > scale.z ? scale.x : scale.z)
                                               : (scale.y > scale.z ? scale.y : scale.z);
            WorldBounds.Radius = localBounds.Radius * maxScale;
        }

        /**
         * @brief ソートキーを計算
         * @param cameraPosition カメラ位置
         * @param blendMode ブレンドモード
         */
        void CalculateSortKey(float cameraX, float cameraY, float cameraZ, BlendMode blendMode)
        {
            const uint32_t blendBits =
                (blendMode == BlendMode::Opaque || blendMode == BlendMode::Masked) ? 0u : (1u << 31);

            // 距離を16ビットに圧縮
            float dx = WorldBounds.CenterX - cameraX;
            float dy = WorldBounds.CenterY - cameraY;
            float dz = WorldBounds.CenterZ - cameraZ;
            float distSq = dx * dx + dy * dy + dz * dz;

            uint32_t distBits = static_cast<uint32_t>(distSq) & 0x7FFFFFFF;

            SortKey = blendBits | distBits;
        }
    };

    // ========================================
    // MegaGeometryProxy
    // ========================================

    /**
     * @brief 描画用MegaGeometryプロキシ
     *
     * World/Component 系から MegaGeometryPass に渡す最小限のインスタンス情報。
         */
    struct MegaGeometryProxy
    {
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;

        MegaGeometry::MegaMeshHandle MegaMeshHandle;

        Math::Matrix4x4 WorldTransform;
        // 直前のゲームのフレームの変換（velocity 用）。無ければ WorldTransform と同じ。
        Math::Matrix4x4 PreviousWorldTransform;
        BoundingSphere WorldBounds;

        bool bVisible = true;
        bool bCastShadow = true;
        RenderLayer LayerMask = RenderLayer::Default;

        bool IsValid() const
        {
            return MegaMeshHandle.IsValid() && bVisible;
        }
    };

    // ========================================
    // BoardProxy
    // ========================================

    struct BoardProxy
    {
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;
        uint64_t SortKey = 0;
        uint32_t LayerPriority = 0;
        uint32_t OrderInLayer = 0;

        TextureHandle Texture = TextureHandle::Invalid();

        Math::Matrix4x4 WorldTransform;
        Math::Matrix4x4 PreviousWorldTransform;
        BoundingSphere WorldBounds;

        RenderLayer LayerMask = RenderLayer::UI;
        BoardSpace Space = BoardSpace::ScreenSpace;
        BoardRenderSubtype RenderSubtype = BoardRenderSubtype::Standard;
        BlendMode BlendModeProp = BlendMode::Translucent;
        float SortDepth = 0.0f;
        uint64_t SourceMeshComponentId = 0;
        float LODSwitchDistance = 0.0f;
        uint32_t ImpostorCellResolution = 0;
        uint32_t ImpostorAxisCellCountX = 0;
        uint32_t ImpostorAxisCellCountY = 0;
        uint32_t ImpostorAtlasWidth = 0;
        uint32_t ImpostorAtlasHeight = 0;
        Math::Vector4 Tint = Math::Vector4(1.0f, 1.0f, 1.0f, 0.75f);
        bool bFlipX = false;
        bool bFlipY = false;
        Math::Vector2 Pivot = Math::Vector2(0.0f, 0.0f);
        Math::Vector2 SizePx = Math::Vector2(0.0f, 0.0f);
        Math::Vector2 SizeWorld = Math::Vector2(0.0f, 0.0f);
        Math::Vector4 UVRect = Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);

        bool bVisible = true;

        static constexpr uint64_t ComputeSortKey(uint32_t layerPriority, uint32_t orderInLayer)
        {
            return (static_cast<uint64_t>(layerPriority) << 32u) |
                   static_cast<uint64_t>(orderInLayer);
        }

        bool IsValid() const
        {
            return ComponentId != 0 && bVisible;
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
        union
        {
            float CanonicalIntensity = 1.0f;
            float Intensity;
        };

        // 減衰
        float Range = 10.0f;
        float AttenuationConstant = 1.0f;
        float AttenuationLinear = 0.09f;
        float AttenuationQuadratic = 0.032f;

        // スポットライト（half-angleの余弦値）
        float InnerConeAngle = 0.976296f;
        float OuterConeAngle = 0.953717f;

        // シャドウ
        bool bCastShadows = false;
        float ShadowBias = 0.005f;
        uint32_t ShadowMapResolution = 1024;

        // 可視性
        bool bVisible = true;
        RenderLayer AffectedLayers = RenderLayer::All;

        bool IsValid() const
        {
            return bVisible && CanonicalIntensity > 0.0f;
        }
    };

    // ========================================
    // CameraProxy
    // ========================================

    /**
     * @brief カメラの露出の決め方
     *
     * Manual は絞り・シャッター速度・ISO・露出補正から露出を決める。
     * Auto は RenderThread が前のフレームまでの SceneColor の輝度から求めた EV100 で露出を決める
     * （測定が無い間は Manual と同じ値を使う）。
     */
    enum class CameraExposureMode : uint8_t
    {
        Manual,
        Auto
    };

    /**
     * @brief カメラのアンチエイリアスの方式
     *
     * FXAA はトーンマップ後の画像の輪郭をぼかす（既定）。TemporalAA は投影へサブピクセルのジッタを掛け、
     * 前のフレームの履歴を velocity で再投影して混ぜる（TAA のパスを持つ View だけで働き、そのとき FXAA は外す）。
     */
    enum class CameraAntiAliasingMode : uint8_t
    {
        FXAA,
        TemporalAA
    };

    /**
     * @brief カメラごとに View の既定から差し替えるトーンマップ後のグレーディングとビネット
     *
     * 起動画面のように見た目を整えたいカメラだけが有効にし、検証シーンのカメラは既定（無効）のまま
     * View のトーンマップ・ビネットの設定を使う。値はどれも表示のリニア値（トーンマップ後）に掛かる。
     */
    struct CameraGradingOverride
    {
        bool bEnabled = false;
        /** @brief 表示の知覚的な値（2.2乗の逆）の上で、軸と0・1を動かさないS字のコントラスト（1で変えない） */
        float Contrast = 1.0f;
        /** @brief コントラストの軸（表示のリニア値。この明るさは変わらず、これより暗い所は暗く、明るい所は明るくなる） */
        float ContrastPivot = 0.18f;
        /** @brief 彩度（Rec.709の輝度との混ぜ具合。1で変えない） */
        float Saturation = 1.0f;
        /** @brief 輝度を保ってR・Bの倍率を変える色温度（-1〜+1、正で暖色。1あたりR・Bを±10%） */
        float Temperature = 0.0f;
        /** @brief ビネットの強さ（0で無効） */
        float VignetteIntensity = 0.0f;
        /** @brief ビネットが最も強くなる、画面中心からの距離（UVの単位） */
        float VignetteRadius = 0.8f;
        /** @brief ビネットが掛かり始める位置までの幅（Radius - Softness から暗くなり始める） */
        float VignetteSoftness = 0.5f;
    };

    /**
     * @brief カメラごとのレンズの効果（色収差とレンズダート）
     *
     * 既定はどちらも0で無効。起動画面のように演出を足したいカメラだけが値を入れ、検証シーンのカメラは
     * 既定のまま結果を変えない。
     */
    struct CameraLensEffects
    {
        /**
         * @brief 放射方向の色収差の量（画面の左右の端での R・B のずれ、描画解像度の画素数。0で無効）
         *
         * ずれは画面中心からの距離に比例し、R は外へ・B は内へずれる。上限は16画素。
         */
        float ChromaticAberrationPixels = 0.0f;
        /** @brief ブルームに掛けるレンズダートの強さ（0で無効。ブルームのダートのしきい値を超えた分に掛かる） */
        float LensDirtIntensity = 0.0f;
    };

    /**
     * @brief カメラごとに掛けるグレーディング用の見た目の3D LUT
     *
     * トーンマップとグレーディングの後、ビネットの前に、表示の値を sRGB の符号化値の座標で引き、
     * 格子点の座標からの符号化値の差分を返す LUT（`Scripts/BakeLookLut.py` が焼く `NLUTLK02` 形式。
     * 恒等の LUT は差分が全て0で、出力を変えない）。既定（AssetPath が nullptr）は
     * 無効で、検証シーンのカメラは結果を変えない。ACES 2.0 SDR LUT の演算子（表示変換そのもの）には、
     * グレーディングと同じく掛けない。
     */
    struct CameraLookLut
    {
        /** @brief LUT ファイルの `Assets/` 相対のパス（文字列リテラルなど静的な寿命の文字列。nullptr で無効） */
        const char* AssetPath = nullptr;
        /** @brief LUT の結果と元の値の混ぜ具合（0〜1。0で無効、1で LUT の結果だけ） */
        float Intensity = 1.0f;
    };

    /**
     * @brief 描画用カメラプロキシ
     */
    struct CameraProxy
    {
        uint64_t CameraId = 0;
        /**
         * @brief GameThread 側でカメラを識別する値（CameraComponent の ID など。0は不明）
         *
         * CameraId は RenderingCoordinator の登録の枠で、SetMainCamera へ別のカメラを渡しても変わらない。
         * TAA はこの値が替わったらカメラの切り替えとみなし、履歴を捨てる。
         */
        uint64_t SourceCameraId = 0;
        /**
         * @brief 連番のフレーム番号（0は連番でない）
         *
         * 0以外の同じ値の間、GameThreadは各パケットへ同じ前のカメラ・instance変換を書き
         * （ApplyPathTracingSequenceCarry）、パストレーサーの連番の経路は同じフレームとして累積し続ける。
         */
        uint64_t SequenceFrame = 0;

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

        // 物理露出 snapshot
        float Aperture = 4.0f;
        float ShutterSpeed = 1.0f / 60.0f;
        float FocusDistance = 0.0f; // ワールド単位m。0は従来のピンホール光線
        float ISO = 100.0f;
        float ExposureCompensation = 0.0f;
        float EV100 = 9.9068906f;
        float Exposure = 1.0f / 1152.0f;
        float PreExposure = 1.0f / 1152.0f;
        float InvPreExposure = 1152.0f;
        CameraExposureMode ExposureMode = CameraExposureMode::Manual;

        // アンチエイリアス
        CameraAntiAliasingMode AntiAliasing = CameraAntiAliasingMode::FXAA;
        // 投影後の NDC のずらし量（TAA のジッタ）。RenderThread が TAA を掛ける View の描画の間だけ、
        // カメラの複製へ書く。GameThread のカメラでは常に0。
        float ProjectionJitterNdcX = 0.0f;
        float ProjectionJitterNdcY = 0.0f;

        // トーンマップ後のグレーディングのコントラスト。負は View のトーンマップ設定の値を使う。
        float GradingContrast = -1.0f;

        // トーンマップ後のグレーディングとビネットの差し替え。有効なときは GradingContrast より優先する。
        CameraGradingOverride GradingOverride;

        // 色収差とレンズダート（既定は無効）
        CameraLensEffects LensEffects;

        // グレーディング用の見た目の3D LUT（既定は無効）
        CameraLookLut LookLut;

        // ポストプロセス設定（ハンドル参照）
        // PostProcessHandle PostProcess;

        bool IsValid() const
        {
            return Viewport.Width > 0.0f && Viewport.Height > 0.0f;
        }
    };

    /**
     * @brief シーンカラーへ掛けるプリエクスポージャを、描画パスで共通の範囲へ収めて返す。
     *
     * GBufferの発光の書き込みとLightingは、同じViewのカメラからこの値を求めて同じ倍率を使う。
     * カメラが無い・値が有限の正でないときは1。
     */
    inline float ResolveSceneColorPreExposure(const CameraProxy *camera)
    {
        if (camera == nullptr || !std::isfinite(camera->PreExposure) || camera->PreExposure <= 0.0f)
        {
            return 1.0f;
        }
        return std::clamp(camera->PreExposure, 1.0e-6f, 1.0e6f);
    }

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

        // CPUスキニング結果の値スナップショット
        Container::VariableArray<SkinnedMeshProxy> SkinnedMeshProxies;

        // MegaGeometryプロキシリスト
        Container::VariableArray<MegaGeometryProxy> MegaGeometryProxies;

        // ライトプロキシリスト
        Container::VariableArray<LightProxy> LightProxies;

        // 環境設定
        float AmbientColorR = 0.1f, AmbientColorG = 0.1f, AmbientColorB = 0.1f;
        float AmbientIntensity = 1.0f;
        SkyAtmosphereParameters SkyAtmosphere;
        VolumetricFogParameters VolumetricFog;
        // 空が無効なときの静的HDR環境（背景とIBL）の明るさの倍率（1で従来どおり。夜の環境光などに使う）
        float StaticEnvironmentIntensityScale = 1.0f;

        // フォグ設定
        bool bFogEnabled = false;
        float FogColorR = 0.5f, FogColorG = 0.5f, FogColorB = 0.5f;
        float FogDensity = 0.01f;
        float FogStart = 10.0f;
        float FogEnd = 100.0f;
        DDGIVolumeParameters DDGIVolume;

        /**
         * @brief シーンをクリア
         */
        void Clear()
        {
            MeshProxies.clear();
            SkinnedMeshProxies.clear();
            MegaGeometryProxies.clear();
            LightProxies.clear();
            AdditionalCameras.clear();
            DDGIVolume = MakeDefaultDDGIVolumeParameters();
            SkyAtmosphere = SkyAtmosphereParameters{};
            VolumetricFog = MakeDefaultVolumetricFogParameters();
            StaticEnvironmentIntensityScale = 1.0f;
        }

        void SetDDGIVolumeParameters(const DDGIVolumeParameters& parameters)
        {
            DDGIVolume = SanitizeDDGIVolumeParameters(parameters);
        }

        void SetVolumetricFogParameters(const VolumetricFogParameters& parameters)
        {
            VolumetricFog = SanitizeVolumetricFogParameters(parameters);
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

        /**
         * @brief MegaGeometryプロキシを追加
         */
        void AddMegaGeometryProxy(const MegaGeometryProxy &proxy)
        {
            if (proxy.IsValid())
            {
                MegaGeometryProxies.push_back(proxy);
            }
        }
    };

} // namespace NorvesLib::Core::Rendering
