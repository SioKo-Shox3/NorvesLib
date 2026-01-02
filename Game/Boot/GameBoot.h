#pragma once

#include "Core/Public/Boot/BootConfig.h"
#include "Core/Public/Container/PointerTypes.h"

namespace Game
{
    class GameApplicationHandler;
}

namespace Game::Boot
{

    /**
     * @brief ゲームのApplicationHandlerを作成
     * @return 作成されたハンドラ
     */
    NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Application::IApplicationHandler> CreateGameHandler();

    /**
     * @brief ゲームのBootConfigを取得
     * @return 起動設定
     */
    NorvesLib::Core::Boot::BootConfig GetBootConfig();

} // namespace Game::Boot
