#include "GameApplicationHandler.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include <cmath>

// GameMode関連
#include "Core/Public/GameMode/TStateMachine.h"
#include "Core/Public/GameMode/GameModeFactory.h"
#include "GameModes/MemoryAgingTest/MemoryAgingTestMode.h"

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Engine;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core;

namespace Game
{

    bool GameApplicationHandler::OnPreInitialize(const VariableArray<String> &args)
    {
        LOG_INFO("GameApplicationHandler::OnPreInitialize()");

        // コマンドライン引数の処理
        for (size_t i = 0; i < args.size(); ++i)
        {
            // コマンドライン引数のログ出力
            LOG_INFO(args[i].c_str());
        }

        return true;
    }

    bool GameApplicationHandler::OnInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnInitialize()");

        // ========================================
        // カメラコントローラーの初期化
        // ========================================
        {
            // 原点を注視点とし、距離5.0、Yaw=0°、Pitch=30°で初期化
            m_CameraController.Initialize(
                NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), // target
                5.0f,                                       // distance
                0.0f,                                       // yaw
                30.0f                                       // pitch
            );

            LOG_INFO("MayaCameraController initialized");
        }

        // ========================================
        // ライトコントローラーの初期化
        // ========================================
        {
            // メインディレクショナルライトの設定
            m_MainLight.LightId = 1;
            m_MainLight.Type = NorvesLib::Core::Rendering::LightType::Directional;
            m_MainLight.DirectionX = 0.0f;
            m_MainLight.DirectionY = -1.0f;
            m_MainLight.DirectionZ = -0.5f;
            m_MainLight.Intensity = 1.0f;
            m_MainLight.ColorR = 1.0f;
            m_MainLight.ColorG = 1.0f;
            m_MainLight.ColorB = 1.0f;
            m_MainLight.bVisible = true;

            // コントローラーにライトを設定
            m_LightController.SetTargetLight(&m_MainLight);
            m_LightController.SetDirection(-45.0f, -45.0f);

            LOG_INFO("LightController initialized");
        }

        // ========================================
        // テスト三角形のWorldObject作成
        // ========================================
        {
            auto &world = GEngine->GetWorld();

            // WorldObjectを作成（Worldにnewで作成してWorld管理下に置く）
            m_pTriangleObject = new WorldObject();
            m_pTriangleObject->Initialize();

            // MeshComponentを作成
            m_pTriangleMeshComponent = new Component::MeshComponent();

            // シェーダー内蔵頂点用のセンチネルハンドル（ID=1はGPUバッファなし）
            m_pTriangleMeshComponent->SetMeshHandle(Rendering::MeshDataHandle{1});
            m_pTriangleMeshComponent->SetMaterial(0, Rendering::MaterialHandle{1});
            m_pTriangleMeshComponent->SetCastShadow(false);

            // コンポーネントをオブジェクトにアタッチ
            m_pTriangleObject->AddComponent(m_pTriangleMeshComponent);

            // Worldに追加（これによりOnAddedToWorldが呼ばれる）
            world.AddObject(m_pTriangleObject);

            LOG_INFO("Triangle WorldObject created and added to World");
        }

        return true;
    }

    void GameApplicationHandler::OnPostInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnPostInitialize()");

        // SceneViewにメインライトを登録
        {
            auto &world = GEngine->GetWorld();
            auto *sceneView = world.GetSceneView();
            if (sceneView)
            {
                sceneView->AddLightProxy(m_MainLight);
                LOG_INFO("Main directional light added to SceneView");
            }
        }
    }

    void GameApplicationHandler::OnUpdate(float deltaTime)
    {
        // ポーズ中は更新をスキップ
        if (m_bIsPaused)
        {
            return;
        }

        // ========================================
        // 入力に基づくカメラ・ライト更新
        // ========================================

        auto &inputSystem = GEngine->GetInputSystem();
        const auto &inputState = inputSystem.GetState();

        // カメラコントローラー更新
        m_CameraController.Update(inputState, deltaTime);

        // デバッグ: スクロール値とカメラ距離を出力
        {
            float scroll = inputState.GetMouseState().ScrollDelta;
            if (std::abs(scroll) > 0.0f)
            {
                float dist = m_CameraController.GetDistance();
                auto pos = m_CameraController.GetPosition();
                NORVES_LOG_INFO("Input", "ScrollDelta={:.3f}, CamDist={:.3f}, CamPos=({:.2f}, {:.2f}, {:.2f})",
                                scroll, dist, pos.x, pos.y, pos.z);
            }
        }

        // カメラ状態をRenderWorldに反映
        {
            NorvesLib::Core::Rendering::CameraProxy cameraProxy;
            m_CameraController.ApplyTo(cameraProxy);
            GEngine->GetRenderWorld().SetMainCamera(cameraProxy);
        }

        // ライトコントローラー更新
        m_LightController.Update(inputState, deltaTime);

        // ライト状態をSceneViewに反映
        {
            auto &world = GEngine->GetWorld();
            auto *sceneView = world.GetSceneView();
            if (sceneView)
            {
                sceneView->UpdateLightProxy(m_MainLight);
            }
        }
    }

    void GameApplicationHandler::OnPreShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnPreShutdown()");

        // 終了前の保存処理など
        // - セーブデータの保存
        // - 設定の保存
    }

    void GameApplicationHandler::OnShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnShutdown()");

        // ゲーム固有の終了処理
        // - リソースの解放
        // - オーディオシステムの終了
        // - ネットワーク切断
    }

    void GameApplicationHandler::OnFocusGained()
    {
        LOG_INFO("GameApplicationHandler::OnFocusGained()");
        m_bIsPaused = false;
    }

    void GameApplicationHandler::OnFocusLost()
    {
        LOG_INFO("GameApplicationHandler::OnFocusLost()");
        // ゲームによってはフォーカスを失った時にポーズ
        // m_bIsPaused = true;
    }

    NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::GameMode::IStateMachine>
    GameApplicationHandler::CreateGameModeStateMachine()
    {
        LOG_INFO("GameApplicationHandler::CreateGameModeStateMachine()");

        // メモリエージングテスト用のステートマシンを作成
        using namespace Game::GameModes;

        auto stateMachine = MakeUnique<TStateMachine<IGameMode, GameModeFactory>>();

        // メモリエージングテストモードを初期ステートとして設定
        stateMachine->ReserveState(MakeUnique<MemoryAgingTestMode>());

        LOG_INFO("メモリエージングテストモードを開始します");

        return stateMachine;
    }

} // namespace Game
