#include "Engine/Engine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/IApplicationHandler.h"

namespace NorvesLib::Core::Engine
{
    // グローバルエンジンインスタンスの実体
    Engine *GEngine = nullptr;

    Engine::Engine()
        : m_InputMapper(m_InputSystem.GetState()),
          m_InputRebindCapture(m_InputSystem, m_InputRouter, m_InputMapper)
    {
        m_InputSystem.SetRouter(&m_InputRouter);
        // 設定/active contextが無ければ入力を消費せず、旧controllerへ透過する。
        m_InputMapper.Attach(m_InputRouter);
        // 同一Engineの正本/配線と単一ownerがctorで確定している。
        (void)m_InputRebindCapture.Attach();
    }

    Engine::~Engine()
    {
        m_InputRebindCapture.Detach();
        m_InputMapper.Detach();
        m_InputSystem.SetRouter(nullptr);
        // TUniquePtrにより自動的に解放される
        // m_GameModeStateMachineのデストラクタが呼ばれる
    }

} // namespace NorvesLib::Core::Engine
