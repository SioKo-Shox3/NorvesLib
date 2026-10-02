#include "Engine/Engine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/IApplicationHandler.h"

namespace NorvesLib::Core::Engine
{
    // グローバルエンジンインスタンスの実体
    Engine *GEngine = nullptr;

    Engine::Engine()
        : m_InputMapper(m_InputSystem.GetState())
    {
        m_InputSystem.SetRouter(&m_InputRouter);
        // 設定/active contextが無ければ入力を消費せず、旧controllerへ透過する。
        m_InputMapper.Attach(m_InputRouter);
    }

    Engine::~Engine()
    {
        m_InputMapper.Detach();
        m_InputSystem.SetRouter(nullptr);
        // TUniquePtrにより自動的に解放される
        // m_GameModeStateMachineのデストラクタが呼ばれる
    }

} // namespace NorvesLib::Core::Engine
