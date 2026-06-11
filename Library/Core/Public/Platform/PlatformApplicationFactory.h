#pragma once

#include "Container/PointerTypes.h"

namespace NorvesLib
{
    class IApplication;
    class IWindow;
} // namespace NorvesLib

namespace NorvesLib::Core::Platform
{

    /**
     * @brief プラットフォーム向けアプリケーション生成関数群
     *
     * 宣言は共通。実装はプラットフォーム別翻訳単位（Windows は
     * Private/Platform/Windows/WindowsPlatformApplicationFactory.cpp）で行う。
     */

    /**
     * @brief プラットフォーム向けアプリケーションインスタンスを生成する
     *
     * Windows では WindowsApplication を生成する。
     *
     * @return アプリケーションのユニークポインタ
     */
    Container::TUniquePtr<NorvesLib::IApplication> CreatePlatformApplication();

    /**
     * @brief プラットフォーム向けウィンドウインスタンスを生成する
     *
     * Windows では WindowsWindow を生成する。
     *
     * @return ウィンドウの共有ポインタ
     */
    Container::TSharedPtr<NorvesLib::IWindow> CreatePlatformWindow();

} // namespace NorvesLib::Core::Platform
