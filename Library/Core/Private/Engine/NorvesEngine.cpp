// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Private\Engine\NorvesEngine.cpp
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Logging/LogMacros.h"

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

        // TODO: サブシステムの初期化
        // - RHIの初期化
        // - レンダラーの初期化
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

        // TODO: サブシステムの更新
        // - 入力の更新
        // - シーンの更新
        // - 物理演算の更新
        // - オーディオの更新
        // - レンダリングの更新
    }

    void NorvesEngine::Shutdown()
    {
        if (!m_isRunning)
        {
            LOG_WARNING("Engine is not running");
            return;
        }

        LOG_INFO("Shutting down NorvesEngine...");

        // TODO: サブシステムのシャットダウン（初期化と逆順）
        // - レンダラーのシャットダウン
        // - 物理演算システムのシャットダウン
        // - オーディオシステムのシャットダウン
        // - 入力システムのシャットダウン
        // - RHIのシャットダウン

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