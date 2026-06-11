#pragma once

#include "Boot/BootConfig.h"

namespace NorvesLib::Core::Boot
{
    /**
     * @brief アプリケーション固有の BootConfig を構築する
     *
     * エンジン側エントリポイント（WinMain 等）がこの関数を呼び出します。
     * ゲーム側プロジェクトで必ず定義してください。
     * Arguments フィールドはエンジン側が設定するため、
     * この関数内では設定不要です。
     *
     * @return ゲーム固有の起動設定
     */
    BootConfig CreateApplicationBootConfig();

} // namespace NorvesLib::Core::Boot
