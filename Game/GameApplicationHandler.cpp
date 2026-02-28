#include "GameApplicationHandler.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"

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

        // 初期化完了後の処理
        // - ロード画面の表示
        // - 初期データのプリロード
        // など
    }

    void GameApplicationHandler::OnUpdate(float deltaTime)
    {
        // ポーズ中は更新をスキップ
        if (m_bIsPaused)
        {
            return;
        }

        // ゲームロジックの更新
        // Note: GameModeの更新はApplicationProcessorが自動的に行うため、
        //       ここではGameMode以外の更新処理を行う

        // 入力処理
        // オーディオ更新
        // UI更新
        // など

        (void)deltaTime; // 未使用警告の抑制
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
