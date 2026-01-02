#include "GameBoot.h"
#include "GameApplicationHandler.h"
#include "Core/Public/Container/PointerTypes.h"

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Boot;
using namespace NorvesLib::Core::Application;

namespace Game::Boot
{

    TSharedPtr<IApplicationHandler> CreateGameHandler()
    {
        return MakeShared<Game::GameApplicationHandler>();
    }

    BootConfig GetBootConfig()
    {
        BootConfig config;

        // ウィンドウ設定
        config.WindowTitle = TEXT("NorvesLib Game");
        config.WindowWidth = 1280;
        config.WindowHeight = 720;
        config.bFullscreen = false;
        config.bResizable = true;

        // エンジン設定
        config.TargetFrameRate = 60.0f;
        config.bVSync = true;
        config.bEnableDebugConsole = true;

        // ハンドラ作成関数
        config.CreateHandler = &CreateGameHandler;

        return config;
    }

} // namespace Game::Boot
