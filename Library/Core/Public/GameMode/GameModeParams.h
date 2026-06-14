#pragma once

#include "Container/String.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモード起動パラメータ
     *
     * GameModeRegistryのCreate / IGameModeControllerのRequest*メソッドに渡す
     * 汎用パラメータ構造体。Phase 3以降で必要に応じてフィールドを追加する。
     */
    struct GameModeParams
    {
        Container::String ModelPath;
    };

} // namespace NorvesLib::Core::GameMode
