// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Private\Engine\NorvesEngine.cpp
#include "Core/Public/Engine/NorvesEngine.h"
#include <iostream>

namespace NorvesLib::Core
{

// グローバルエンジンインスタンスの定義
NorvesEngine GEngine;

NorvesEngine::NorvesEngine()
    : m_isRunning(false)
    , m_version("1.0.0")
{
    // コンストラクタで初期化する処理を追加
    std::cout << "NorvesEngine created. Version: " << m_version << std::endl;
}

NorvesEngine::~NorvesEngine()
{
    // エンジンが正しく終了しているか確認
    if (m_isRunning)
    {
        std::cout << "Warning: Engine is still running during destruction. Shutting down..." << std::endl;
        Shutdown();
    }
    
    std::cout << "NorvesEngine destroyed." << std::endl;
}

bool NorvesEngine::Initialize()
{
    if (m_isRunning)
    {
        std::cout << "Engine is already running." << std::endl;
        return false;
    }
    
    std::cout << "Initializing NorvesEngine..." << std::endl;
    
    // TODO: サブシステムの初期化
    // - RHIの初期化
    // - レンダラーの初期化
    // - 入力システムの初期化
    // - オーディオシステムの初期化
    // - 物理演算システムの初期化
    
    m_isRunning = true;
    std::cout << "NorvesEngine initialized successfully." << std::endl;
    
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
        std::cout << "Engine is not running." << std::endl;
        return;
    }
    
    std::cout << "Shutting down NorvesEngine..." << std::endl;
    
    // TODO: サブシステムのシャットダウン（初期化と逆順）
    // - レンダラーのシャットダウン
    // - 物理演算システムのシャットダウン
    // - オーディオシステムのシャットダウン
    // - 入力システムのシャットダウン
    // - RHIのシャットダウン
    
    m_isRunning = false;
    std::cout << "NorvesEngine shutdown complete." << std::endl;
}

bool NorvesEngine::IsRunning() const
{
    return m_isRunning;
}

void NorvesEngine::Stop()
{
    if (m_isRunning)
    {
        std::cout << "Stopping NorvesEngine..." << std::endl;
        m_isRunning = false;
    }
}

const std::string& NorvesEngine::GetVersion() const
{
    return m_version;
}

} // namespace NorvesLib::Core