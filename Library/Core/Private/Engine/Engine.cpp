#include "Engine/Engine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/IApplicationHandler.h"

namespace NorvesLib::Core::Engine
{
    // グローバルエンジンインスタンスの実体
    Engine *GEngine = nullptr;

    Engine::~Engine()
    {
        // TUniquePtrにより自動的に解放される
        // m_GameModeStateMachineのデストラクタが呼ばれる
    }

} // namespace NorvesLib::Core::Engine
