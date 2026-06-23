#include "Engine/Engine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/IApplicationHandler.h"

namespace NorvesLib::Core::Engine
{
    // グローバルエンジンインスタンスの実体
    Engine *GEngine = nullptr;

    Engine::Engine()
    {
        // InputSystem の Inject* が InputRouter へ配送するよう配線する。
        // この時点では Controller は未登録のため配送は空振り（挙動不変）。
        m_InputSystem.SetRouter(&m_InputRouter);
    }

    Engine::~Engine()
    {
        // TUniquePtrにより自動的に解放される
        // m_GameModeStateMachineのデストラクタが呼ばれる
    }

} // namespace NorvesLib::Core::Engine
