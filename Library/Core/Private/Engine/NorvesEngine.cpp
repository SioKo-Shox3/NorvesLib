// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Private\Engine\NorvesEngine.cpp
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core
{

    // グローバルエンジンインスタンスの定義
    NorvesEngine GEngine;

    NorvesEngine::NorvesEngine()
        : m_isRunning(false), m_version(String("1.0.0"))
    {
        // コンストラクタで初期化する処理を追加
        LOG_INFO_F("NorvesEngine created. Version: %s", m_version.c_str());
    }

    NorvesEngine::~NorvesEngine()
    { // エンジンが正しく終了しているか確認
        if (m_isRunning)
        {
            LOG_WARNING("Engine is still running during destruction. Shutting down...");
            Shutdown();
        }

        LOG_INFO("NorvesEngine destroyed");
    }

    bool NorvesEngine::Initialize()
    {
        if (m_isRunning)
        {
            LOG_WARNING("Engine is already running");
            return false;
        }

        LOG_INFO("Initializing NorvesEngine...");

        // サブシステムの初期化

        // 1. ResourceRegistryの初期化
        if (!m_ResourceRegistry.Initialize())
        {
            LOG_ERROR("Failed to initialize ResourceRegistry");
            return false;
        }

        // 2. AssetRegistryの初期化
        if (!m_AssetRegistry.InitializeSubsystem())
        {
            LOG_ERROR("Failed to initialize AssetRegistry");
            m_ResourceRegistry.Shutdown();
            return false;
        }

        // TODO: RenderingCoordinatorの初期化
        // - RHI初期化後に行う
        // - Screenの作成
        // - RenderThreadの開始

        // TODO: 追加サブシステムの初期化
        // - 入力システムの初期化
        // - オーディオシステムの初期化
        // - 物理演算システムの初期化

        m_isRunning = true;
        LOG_INFO("NorvesEngine initialized successfully");

        return true;
    }

    void NorvesEngine::Update(float deltaTime)
    {
        if (!m_isRunning)
        {
            return;
        }

        // サブシステムの更新
        // - 入力の更新
        // - シーンの更新
        // - 物理演算の更新
        // - オーディオの更新

        // リソースのガベージコレクション（定期的に実行）
        m_ResourceRegistry.CollectGarbage();

        // アセットレジストリの更新（GC、非同期ロード完了チェック等）
        m_AssetRegistry.Update(deltaTime);

        // レンダリングフレームの処理
        if (m_RenderingCoordinator.IsInitialized())
        {
            m_RenderingCoordinator.BeginFrame();
            m_RenderingCoordinator.CollectScene();
            m_RenderingCoordinator.GenerateDrawCommands();
            m_RenderingCoordinator.EndFrame();
        }
    }

    void NorvesEngine::Shutdown()
    {
        if (!m_isRunning)
        {
            LOG_WARNING("Engine is not running");
            return;
        }

        LOG_INFO("Shutting down NorvesEngine...");

        // サブシステムのシャットダウン（初期化と逆順）

        // RenderThreadの停止
        if (m_RenderThread.IsRunning())
        {
            m_RenderThread.Stop();
        }
        m_RenderThread.Shutdown();

        // RenderingCoordinatorのシャットダウン
        m_RenderingCoordinator.Shutdown();

        // TODO: 追加サブシステムのシャットダウン
        // - 物理演算システムのシャットダウン
        // - オーディオシステムのシャットダウン
        // - 入力システムのシャットダウン

        // AssetRegistryのシャットダウン
        m_AssetRegistry.ShutdownSubsystem();

        // ResourceRegistryのシャットダウン（最後に行う）
        m_ResourceRegistry.Shutdown();

        m_isRunning = false;
        LOG_INFO("NorvesEngine shutdown complete");
    }

    bool NorvesEngine::IsRunning() const
    {
        return m_isRunning;
    }

    void NorvesEngine::Stop()
    {
        if (m_isRunning)
        {
            LOG_INFO("Stopping NorvesEngine...");
            m_isRunning = false;
        }
    }

    const String &NorvesEngine::GetVersion() const
    {
        return m_version;
    }

} // namespace NorvesLib::Core
