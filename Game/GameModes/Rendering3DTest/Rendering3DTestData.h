#pragma once

#include "CameraLateUpdate.h"

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Input/LightController.h"
#include "Core/Public/Input/MayaCameraController.h"
#include "Input/CameraInputCollector.h"
#include "Input/PickingController.h"
#include "Core/Public/Object/Entity.h"
#include "GameModes/Rendering3DTest/Rendering3DTestDebugInput.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Core/Public/Rendering/MegaGeometry/ProceduralMegaSphere.h"
#include "Core/Public/Rendering/RenderTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "Core/Public/Thread/Mutex.h"
#include "Core/Public/Thread/Task.h"
#include "Core/Public/Particle/ParticleSystem.h"
#include "GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.h"
#include "GameModes/Rendering3DTest/M9WorldAcceptance.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"

#if defined(NORVES_GAME_AUDIO)
#include "Audio/AudioTypes.h"
#endif

namespace NorvesLib::Core
{
    namespace Asset
    {
        class AssetSystem;
        struct CookedMeshData;
    } // namespace Asset

    namespace Component
    {
        class MeshComponent;
        class BoardComponent;
        class BillboardComponent;
        class ImpostorComponent;
        class MegaGeometryComponent;
        class SkinnedMeshComponent;
        class LightComponent;
        class PointLightComponent;
        class CameraComponent;
        class SpringArmComponent;
    } // namespace Component
} // namespace NorvesLib::Core

namespace Game::GameModes
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief 非同期マテリアル更新情報
     *
     * テクスチャの非同期読み込み完了時に、
     * どのマテリアルのどのスロットを更新するかを保持します。
     */
    struct PendingMaterialUpdate
    {
        NorvesLib::Core::Rendering::MaterialHandle TargetMaterial;
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0; ///< 残り読み込み中テクスチャ数
    };

    /**
     * @brief 地面の1区画（x方向に並べた、奥行きが地面全体の帯）
     *
     * 石畳の区画（中央と外側）と、見本の材質の区画がある。区画ごとにメッシュ（UVはテクスチャの実寸で
     * 繰り返す）・材質・Entityを1つずつ持つ。
     */
    struct GroundPiece
    {
        NorvesLib::Core::Rendering::MeshDataHandle MeshHandle;
        NorvesLib::Core::Rendering::MaterialHandle Material;
        float CenterX = 0.0f;    ///< 帯の中心のx（m）
        float Width = 0.0f;      ///< 帯の幅（m）
        float TileMeters = 2.0f; ///< テクスチャ1枚の実寸（m）
        int32_t SwatchIndex = -1; ///< 見本の材質の表の番号（石畳なら-1）
        NorvesLib::Core::Entity *pObject = nullptr;
    };

    /**
     * @brief Boulder の非同期ロード共有状態
     *
     * コールバックが Data 本体ではなくこの共有状態を値キャプチャすることで、
     * Routine の Leave 後にコールバックが到着しても use-after-free を起こさない。
     */
    struct BoulderAsyncState
    {
        NorvesLib::Thread::Atomic<bool> m_bCancelled{false}; ///< Leave で立てる。到着結果を破棄する
        NorvesLib::Thread::Atomic<bool> m_bCompleted{false}; ///< コールバック到着フラグ（Do が消費）
        NorvesLib::Core::Rendering::ModelHandle m_Handle;    ///< 結果ハンドル
        bool m_bLoaded = false;                              ///< 有効ハンドルが得られたか
        float m_BoundsMinY = 0.0f; ///< クック済みのメッシュの最下点の Y（スキャン資産を地面に据えるのに使う。それ以外は 0）
    };

    /**
     * @brief クック済み（NVMESH v1）で読む起動画面のモデル1つ分の読み込み状態
     *
     * メッシュは起動時に読み込んで解析し、材質のテクスチャ（VT）がそろってから MegaMesh を作る。
     * 完了すると State を BoulderAsyncState と同じ手順で埋め、岩・小屋の組み立てが続きを受け持つ。
     */
    struct CookedStartupModelLoad
    {
        String DebugName;
        String LogicalPath; ///< メッシュの論理パス（"Assets/Models/...gltf"）。テクスチャが読めないとき glTF の経路へ戻すのに使う
        bool bBoulder = false; ///< 岩か（false なら小屋）。glTF の経路へ戻すとき、どちらの要求番号を更新するか決める
        bool bAllowGltfFallback = true; ///< 材質のテクスチャが読めないとき glTF の経路へ戻すか（false なら失敗として State を埋める）
        TSharedPtr<NorvesLib::Core::Asset::CookedMeshData> Mesh;
        /// ページ（NVMESH v1.1）の読み込み元。ページを 2 つ以上持つメッシュだけが持つ（無ければ全て常駐で作る）
        TSharedPtr<NorvesLib::Core::Rendering::MegaGeometry::IGeometryPageSource> PageSource;
        TSharedPtr<PendingMaterialUpdate> Material;
        TSharedPtr<BoulderAsyncState> State;
    };

    /**
     * @brief 起動画面の地面の外周に並べる、高ポリのスキャン資産1つ分の読み込み状態
     *
     * 資産は Rendering3DTestRoutine.cpp の kStartupScanProps の表の番号で引く。クック済み（NVMESH v1・BC・VT）が
     * 無い資産は読み込みを始めず、glTF の経路へも戻さない（置かずに警告する）。
     */
    struct StartupScanPropLoad
    {
        uint32_t SpecIndex = 0;
        TSharedPtr<BoulderAsyncState> State;
    };

    /**
     * @brief --stress-mega-instances で複製する元（置いたスキャン資産のメッシュと据え方）
     */
    struct StressMegaInstanceSource
    {
        NorvesLib::Core::Rendering::MegaGeometry::MegaMeshHandle Handle;
        float PositionY = 0.0f; ///< 最下点を地面へ据えた Y
        float Scale = 1.0f;
        // --stress-geometry で、拡大率を変えて据え直すための値。拡大率 s のとき Y = -1 - BoundsMinY * s - SinkMeters。
        float BoundsMinY = 0.0f; ///< メッシュの最下点の Y（拡大前）
        float SinkMeters = 0.0f; ///< 地面へ埋める深さ
        float ScaleMin = 1.0f;   ///< --stress-geometry で振る拡大率の範囲
        float ScaleMax = 1.0f;
    };

    /**
     * @brief 3Dレンダリングテストのデータクラス
     *
     * 球体と地面のEntityおよびメッシュハンドルを保持します。
     */
    struct Rendering3DTestData
    {
        // メッシュハンドル
        NorvesLib::Core::Rendering::MeshDataHandle m_SphereMeshHandle{100};
        NorvesLib::Core::Rendering::MeshDataHandle m_LightSphereMeshHandle{102};
        // 地面の区画のメッシュ（110から区画ごとに1つずつ使う）
        static constexpr uint32_t kGroundPieceMeshHandleBase = 110u;
        VariableArray<GroundPiece> m_GroundPieces;
        // 見本の材質（Rendering3DTestRoutine.cpp の表と同じ並び。テクスチャが無い材質は無効のまま）
        VariableArray<NorvesLib::Core::Rendering::MaterialHandle> m_GroundSwatchMaterials;

        // テクスチャの負荷モード（--stress-textures）。地面の外側へ、負荷用の材質を貼った板を格子に並べる。
        // 板のメッシュは 200 から板ごとに1つずつ使う。材質は Rendering3DTestRoutine.cpp の kStressMaterials と同じ並びで、
        // テクスチャが無い材質は無効のまま（その板は置かない）。
        static constexpr uint32_t kStressPanelMeshHandleBase = 200u;
        bool m_bStressTextures = false;
        VariableArray<NorvesLib::Core::Rendering::MaterialHandle> m_StressMaterials;

        // テクスチャハンドル
        NorvesLib::Core::Rendering::TextureHandle m_CheckerTextureHandle;
        NorvesLib::Core::Rendering::TextureHandle m_F6AtlasTextureHandle;

        // マテリアルハンドル
        NorvesLib::Core::Rendering::MaterialHandle m_SilverMaterial;      // Silver PBR マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_CobbleStoneMaterial; // 石畳マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_GroundMaterial;      // 地面マテリアル
        NorvesLib::Core::Rendering::MaterialHandle m_LightSphereMaterial; // 光源球体マテリアル
        // 非同期ロード用：マテリアル更新ペンディングリスト
        VariableArray<TSharedPtr<PendingMaterialUpdate>> m_PendingMaterialUpdates;
        // 石畳の材質の読み込み状態（大きな球のMegaGeometryは、テクスチャがそろってから作る）
        TSharedPtr<PendingMaterialUpdate> m_CobbleStoneMaterialUpdate;
        // 大きな球の高ポリのMegaGeometry（起動時に作った頂点・クラスタ。MegaMeshを作ったら手放す）
        TSharedPtr<NorvesLib::Core::Rendering::MegaGeometry::ProceduralMegaSphereData> m_pBigSphereMegaData;
        // 大きな球の頂点・変位を別スレッドで作るジョブ（完了するまで m_pBigSphereMegaData の中身を読まない）
        NorvesLib::Thread::TaskPtr m_BigSphereBuildTask;
        NorvesLib::Core::Rendering::ModelHandle m_BigSphereModelHandle;

        // Entity参照（Worldが所有）
        NorvesLib::Core::Entity *m_pSphereObject = nullptr;
        NorvesLib::Core::Entity *m_pGroundObject = nullptr;
        NorvesLib::Core::Entity *m_pLightSphereObject = nullptr;
        NorvesLib::Core::Entity *m_pBoulderObject = nullptr;
        NorvesLib::Core::Entity *m_pBoulderPlaceholderObject = nullptr;
        VariableArray<NorvesLib::Core::Entity *> m_F4BoardObjects;
        VariableArray<NorvesLib::Core::Entity *> m_F9BillboardObjects;
        VariableArray<NorvesLib::Core::Entity *> m_F11ImpostorSmokeObjects;

        // MeshComponent参照（Entityが所有）
        NorvesLib::Core::Component::MeshComponent *m_pSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pGroundMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pLightSphereMeshComponent = nullptr;
        NorvesLib::Core::Component::MeshComponent *m_pBoulderPlaceholderMeshComponent = nullptr;
        VariableArray<NorvesLib::Core::Component::MeshComponent *> m_F11ImpostorSourceMeshComponents;
        VariableArray<NorvesLib::Core::Component::BoardComponent *> m_F4BoardComponents;
        VariableArray<NorvesLib::Core::Component::BillboardComponent *> m_F9BillboardComponents;
        VariableArray<NorvesLib::Core::Component::ImpostorComponent *> m_F11ImpostorComponents;
        NorvesLib::Core::Component::MegaGeometryComponent *m_pBoulderMegaGeometryComponent = nullptr;
        NorvesLib::Core::Component::MegaGeometryComponent *m_pSphereMegaGeometryComponent = nullptr;

        // LightComponent参照（Entityが所有）
        NorvesLib::Core::Component::PointLightComponent *m_pPointLightComponent = nullptr;

        // 物理空（R2 SkyAtmosphere）の設定。空が有効なとき、エンジンが空の太陽の方向光を光源表へ加え、
        // IBLも空から作る。シーン独自の方向光は置かない。
        NorvesLib::Core::Rendering::SkyAtmosphereParameters m_SkyAtmosphere;

        // 方向ライト操作（LightController）が書き込む進行方向の置き場。光源表へは登録せず、
        // Tick で操作の角度を空の太陽の仰角・方位へ写す。
        NorvesLib::Core::Rendering::LightProxy m_SkySunControlLight;

        // World 所有の pivot/camera Entity と、その camera Entity が Inner 所有する Component。
        NorvesLib::Core::Entity *m_pCameraPivotObject = nullptr;
        NorvesLib::Core::Entity *m_pCameraObject = nullptr;
        NorvesLib::Core::Component::SpringArmComponent *m_pSpringArmComponent = nullptr;
        NorvesLib::Core::Component::CameraComponent *m_pCameraComponent = nullptr;

        // 経過時間
        float m_ElapsedTime = 0.0f;

        // 球体回転速度（rad/s）
        float m_RotationSpeed = 0.5f;

        // 入力値を SpringArmIntent へ変換する感度換算層。Router へ直接登録しない。
        NorvesLib::Core::Input::MayaCameraController m_CameraController;

        // InputRouter の consume 順序を維持して値 InputState を構築する収集層。
        Game::Input::CameraInputCollector m_CameraInputCollector;

        // 左クリック選択コントローラー（シーン所有・イベント駆動）
        Game::Input::PickingController m_PickingController;

        // 空の太陽の仰角・方位を動かす操作コントローラー（矢印、シーン所有・イベント駆動）
        NorvesLib::Core::Input::LightController m_LightController;

        // F1-F5 デバッグビュー切替コントローラー（シーン所有・イベント駆動）
        Rendering3DTestDebugInput m_DebugInput;

        // メッシュ登録済みフラグ
        bool m_bMeshesRegistered = false;
        TWeakPtr<Game::CameraLateUpdateSlot> m_LateCameraSlot;
        TSharedPtr<Game::CameraLateUpdateState> m_LateCameraState;
        bool m_bCameraSmokeCompleteEmitted = false;
        // --startup-camera で指定した起動時のカメラ（SpringArm の yaw・pitch[度]と腕の長さ）
        bool m_bHasStartupCamera = false;
        float m_StartupCameraYaw = 0.0f;
        float m_StartupCameraPitch = 0.0f;
        float m_StartupCameraArmLength = 0.0f;
        // --sun-elevation / --sun-azimuth で指定した起動時の空の太陽の仰角・方位（度）
        bool m_bHasStartupSunElevation = false;
        float m_StartupSunElevation = 0.0f;
        bool m_bHasStartupSunAzimuth = false;
        float m_StartupSunAzimuth = 0.0f;
        // --exposure-ev100 で指定した起動時の手動露出
        bool m_bHasStartupExposureEV100 = false;
        float m_StartupExposureEV100 = 0.0f;
        // --height-fog-density で指定した高さフォグの地面での密度（0で無効）
        bool m_bHasStartupHeightFogDensity = false;
        float m_StartupHeightFogDensity = 0.0f;
        // --height-fog-falloff で指定した高さフォグの高さ方向の減衰（1/m）
        bool m_bHasStartupHeightFogFalloff = false;
        float m_StartupHeightFogFalloff = 0.0f;
        // --orbit-degrees-per-second で指定したカメラの周回の速さ（度/秒、0で止まったまま）。撮影で
        // 動くカメラの TAA の残像を確かめるのに使う。
        float m_OrbitDegreesPerSecond = 0.0f;
        // 決定的な撮影の旋回は、最初に回した時点のヨー（度）に、エポックからの時間に比例した角度を足して決める。
        float m_OrbitBaseYaw = 0.0f;
        bool m_bOrbitBaseYawLatched = false;
        // --render-scale で指定した内部解像度の倍率（0.5〜1、既定は1で画面解像度のまま描く）。
        float m_StartupRenderScale = 1.0f;
        // --debug-draw-test-lines の指定で true にする。大きな球を囲む箱をデバッグの線で毎フレーム描く。
        bool m_bDebugDrawTestLines = false;
        // --startup-skinned-probe の指定で true にする。検証用の骨付きのパネルを地面の上へ 2 体置く（既定は置かない）。
        bool m_bStartupSkinnedProbe = false;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> m_StartupSkinnedProbeAsset;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::SkinnedMeshComponent *> m_StartupSkinnedProbeComponents;
        // 地面の外周に高ポリのスキャン資産を置くか（--startup-scan-props=off で false。既定は true）。
        bool m_bStartupScanProps = true;
        // --stress-mega-instances=<N> の個数（0 は置かない）。スキャン資産を置いた後、そのメッシュを N 個格子に複製する。
        uint32_t m_StressMegaInstanceCount = 0;
        bool m_bStressMegaInstancesPlaced = false;
        // ジオメトリの負荷モード（--stress-geometry[=<N>]。N は既定 300）。スキャン資産・岩・小屋・大きな球を、変換を変えて
        // 地面の外側の格子に N 個並べ、カメラの軸を格子の中心へ移す。個数は m_StressMegaInstanceCount に入る。
        bool m_bStressGeometry = false;
        VariableArray<StressMegaInstanceSource> m_StressMegaSources;
        // 起動時のアンチエイリアシングが TAA なら true（既定は TAA、--anti-aliasing=fxaa の指定で false）。
        bool m_bStartupTemporalAA = true;
        // --night の指定で true にする。空と空の太陽を消し、静的HDRの環境光を月明かり程度へ落とす
        // （点光源の影を見る撮影用。既定は昼）。
        bool m_bStartupNight = false;
        // --debug-view の指定（起動時のデバッグの表示。既定は Normal）。development ビルドだけが適用する。
        NorvesLib::Core::Rendering::DebugViewMode m_StartupDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Normal;
        // 材質のアルベド・法線・ORM・高さを VT（sparse）で描くか（--virtual-texture=off で false）。sparse に対応しない GPU・クック済みに無い
        // テクスチャは、true でも全常駐で読む。
        bool m_bVirtualTexture = true;
        // VT のタイルがそろうのを待つ状態（決定的な撮影が、タイルがそろってから数え直すため）。組み立てが終わってから、
        // ストリーマが落ち着いている連続のティック数と、待った合計のティック数。一度落ち着いたら戻さない。
        uint32_t m_VirtualTextureIdleTicks = 0;
        uint32_t m_VirtualTextureWaitTicks = 0;
        bool m_bVirtualTextureSettled = false;

        // クック済みのマニフェストに、論理パス（"Assets/..." から始まる）の項目があるか。起動画面の材質は、
        // クック済みの BC のテクスチャと ORM があればそれを、無ければばらの元画像を読む。未設定ならすべてばらで読む。
        NorvesLib::Core::Delegate<bool, const NorvesLib::Core::Container::String &> m_IsTextureCooked;

        // 起動画面の岩・小屋をクック済み（NVMESH v1・BC・VT）で読むか。false が既定で、クック済みが無ければ glTF の実行時の経路へ戻して警告する。
        // true（--rendering3dtest-model-source=gltf、--no-cooked-textures）は、最初から glTF の経路で読む（見た目・VRAM の比較用）。
        bool m_bStartupModelsFromGltf = false;
        // 起動画面の大きな球をクック済み（NVMESH v1。クッカーが変位した球の階層を焼いてある）で読むか。false が既定で、
        // クック済みが無ければ実行時の生成（約3.6秒）へ戻して警告する。true（--rendering3dtest-big-sphere-source=runtime、
        // --no-cooked-textures）は、最初から実行時に生成する（見た目・起動時間の比較用）。
        bool m_bBigSphereFromRuntime = false;
        // 読み込んだクック済みの大きな球（石畳の材質がそろって MegaMesh を作るまで持つ）
        TSharedPtr<NorvesLib::Core::Asset::CookedMeshData> m_pBigSphereCooked;
        // 大きな球のページの読み込み元（読み込みのジョブが埋める。ジョブの完了後に読む）
        TSharedPtr<TSharedPtr<NorvesLib::Core::Rendering::MegaGeometry::IGeometryPageSource>> m_pBigSpherePageSource;
        // クック済みのメッシュ（NVMESH）を解決する AssetSystem（無ければ null。クック済みのマニフェストを読んでいないとき）。
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>> m_GetAssetSystem;
        // クック済みで読んでいる岩・小屋の、材質（VT）がそろうのを待っている状態。そろったら MegaMesh を作って取り除く。
        VariableArray<CookedStartupModelLoad> m_CookedStartupModelLoads;
        // 地面の外周に並べるスキャン資産のうち、読み込み中のもの。完了したものから World へ置いて取り除く。
        VariableArray<StartupScanPropLoad> m_ScanPropLoads;

        // 手動露出（EV100）。ImGui のスライダーが書き、Tick が絞り・ISO を保ったままシャッター速度へ写す。
        float m_ExposureEV100 = 0.0f;
        float m_AppliedExposureEV100 = 0.0f;
        // 自動露出（起動画面の既定）。ImGui のチェックボックスが書き、Tick がカメラの露出の方式へ写す。
        bool m_bAutoExposure = true;
        bool m_bAppliedAutoExposure = true;
        // アンチエイリアシング（起動画面の既定は TAA、切ると FXAA）。ImGui のチェックボックスが書き、
        // Tick がカメラのアンチエイリアシングの方式へ写す。
        bool m_bTemporalAA = true;
        bool m_bAppliedTemporalAA = true;
        // レンズの効果（色収差とレンズダート。起動画面の既定は有効、環境変数 NORVES_STARTUP_LENS_EFFECTS=0 で
        // 無効で起動する）。ImGui のチェックボックスが書き、Tick がカメラのレンズ効果へ写す。
        bool m_bLensEffects = true;
        // 見た目の3D LUT（暖かみのある映画調。起動画面の既定は有効、環境変数 NORVES_STARTUP_LOOK_LUT=0 で
        // 無効で起動する）。ImGui のチェックボックスが書き、Tick がカメラの LUT へ写す。
        bool m_bLookLut = true;
        // RenderThread が読み戻した自動露出の測定。Tick が統計のスナップショットから写し、ImGui が表示する。
        NorvesLib::Core::Rendering::AutoExposureMeasurement m_AutoExposureMeasurement;

        // 環境変数 NORVES_STARTUP_SUN_STEP="<仰角>,<秒>" の指定。起動からその秒数の後に一度だけ、
        // 空の太陽の仰角を急に変える（自動露出の順応の確かめ用）。
        bool m_bHasSunStep = false;
        bool m_bSunStepApplied = false;
        float m_SunStepElevation = 0.0f;
        float m_SunStepDelaySeconds = 0.0f;
        double m_SunStepElapsedSeconds = 0.0;

        // ========================================
        // glTF Model（Boulder）
        // ========================================
        String m_ModelPath;
        bool m_bUseCookedModel = false;
        uint32_t m_BoardSmokeCount = 0;
        uint32_t m_BillboardSmokeCount = 0;
        uint32_t m_ImpostorSmokeCount = 0;
        uint32_t m_InstancedMeshCount = 0;
        bool m_bLayerCompositeSmoke = false;
        bool m_bPhysicsSmoke = false;
        M8MinimalPhysicsSmoke m_M8MinimalPhysicsSmoke;
        TSharedPtr<M9WorldAcceptanceConfig> m_M9WorldAcceptance;
        NorvesLib::Core::Entity *m_pM9SkinnedObject = nullptr;
        NorvesLib::Core::Component::SkinnedMeshComponent *m_pM9SkinnedMeshComponent = nullptr;
        bool m_bM9Attached = false;
        uint32_t m_M9TickCount = 0;
        uint32_t m_M9CapturePhase = 0;
        uint32_t m_M9StatsWaitTicks = 0;
        bool m_bM9NegativeCaptureRequested = false;
        bool m_bM9VisualComplete = false;
        bool m_bM9AudioComplete = false;
        bool m_bM9Completed = false;
        NorvesLib::Core::Rendering::CapturedFrame m_M9FirstCapture;
        NorvesLib::Core::Rendering::CapturedFrame m_M9NegativeCapture;
        NorvesLib::Core::Rendering::CapturedFrame m_M9SecondCapture;
        uint64_t m_M9T0PoseFingerprint = 0;
        uint64_t m_M9T1PoseFingerprint = 0;
        VariableArray<NorvesLib::Core::Rendering::RenderingCoordinatorStatsSnapshot> m_M9StatsHistory;
#if defined(NORVES_GAME_AUDIO)
        NorvesLib::Modules::Audio::VoiceHandle m_M9EffectVoice;
        NorvesLib::Modules::Audio::VoiceHandle m_M9LoopVoice;
        bool m_bM9AudioStarted = false;
        bool m_bM9AudioStopIssued = false;
#endif
        VariableArray<NorvesLib::Core::Rendering::TextureHandle> m_F11ImpostorSmokeAtlasHandles;
        NorvesLib::Core::Rendering::ModelHandle m_BoulderModelHandle;
        uint32_t m_BoulderLoadRequestId = 0;
        bool m_bBoulderModelLoaded = false;
        bool m_bBoulderModelLoadPending = false;
        TSharedPtr<BoulderAsyncState> m_BoulderAsyncState; ///< 非同期ロード共有状態

        // 展示物: 材質見本の球の列（金属0と1の2列 × 粗さ5段）と小屋（Cottage_Clean の glTF）
        VariableArray<NorvesLib::Core::Rendering::MaterialHandle> m_ShowcaseMaterials;
        VariableArray<NorvesLib::Core::Entity *> m_ShowcaseSphereObjects;
        NorvesLib::Core::Entity *m_pCottageObject = nullptr;
        NorvesLib::Core::Component::MegaGeometryComponent *m_pCottageMegaGeometryComponent = nullptr;
        NorvesLib::Core::Rendering::ModelHandle m_CottageModelHandle;
        uint32_t m_CottageLoadRequestId = 0;
        TSharedPtr<BoulderAsyncState> m_CottageAsyncState; ///< 小屋の非同期ロード共有状態（Boulder と同じ型）
        NorvesLib::Core::Particle::ParticleEmitterHandle m_ParticleEmitter;
    };

} // namespace Game::GameModes
