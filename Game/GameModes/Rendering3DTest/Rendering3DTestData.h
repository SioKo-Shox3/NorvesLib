#pragma once

#include "Core/Public/Container/Containers.h"
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
#include "Core/Public/Rendering/RenderTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "Core/Public/Thread/Mutex.h"
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
        NorvesLib::Core::Rendering::MeshDataHandle m_GroundMeshHandle{101};
        NorvesLib::Core::Rendering::MeshDataHandle m_LightSphereMeshHandle{102};

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
        bool m_bCameraSmokeSyncEmitted = false;
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
        // --render-scale で指定した内部解像度の倍率（0.5〜1、既定は1で画面解像度のまま描く）。
        float m_StartupRenderScale = 1.0f;
        // --debug-draw-test-lines の指定で true にする。大きな球を囲む箱をデバッグの線で毎フレーム描く。
        bool m_bDebugDrawTestLines = false;
        // 起動時のアンチエイリアシングが TAA なら true（既定は TAA、--anti-aliasing=fxaa の指定で false）。
        bool m_bStartupTemporalAA = true;
        // --night の指定で true にする。空と空の太陽を消し、静的HDRの環境光を月明かり程度へ落とす
        // （点光源の影を見る撮影用。既定は昼）。
        bool m_bStartupNight = false;

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
