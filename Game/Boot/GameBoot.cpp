#include "GameBoot.h"
#include "GameApplicationHandler.h"
#include "Core/Public/Boot/EntryPoint.h"
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

namespace NorvesLib::Core::Boot
{
    /**
     * @brief ゲーム固有の BootConfig を構築する（エンジン EntryPoint から呼ばれる）
     *
     * Arguments フィールドはエンジン側エントリポイントが設定するため、
     * ここでは設定しません。
     *
     * @return ゲーム固有の起動設定
     */
    BootConfig CreateApplicationBootConfig()
    {
        return Game::Boot::GetBootConfig();
    }
} // namespace NorvesLib::Core::Boot
