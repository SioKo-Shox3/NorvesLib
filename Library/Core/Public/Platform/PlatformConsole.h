#pragma once

namespace NorvesLib::Core::Platform
{

    /**
     * @brief デバッグコンソール関連のプラットフォーム関数群
     *
     * 宣言は共通。実装はプラットフォーム別翻訳単位（Windows は
     * Private/Platform/Windows/WindowsPlatformConsole.cpp）で行う。
     */

    /**
     * @brief デバッグコンソールを開く
     *
     * Windows では AttachConsole(ATTACH_PARENT_PROCESS) を優先し、失敗時に
     * AllocConsole() でコンソールを割り当てる。stdout/stderr/stdin を
     * freopen_s でリダイレクトし、コンソールタイトルを
     * "NorvesLib Debug Console" に設定する。
     * NORVES_ENABLE_DEBUG_OUTPUT が 0 の場合は no-op で false を返す。
     *
     * @return コンソールが正常に開かれた場合 true
     */
    bool OpenDebugConsole();

    /**
     * @brief デバッグコンソールを閉じる
     *
     * Windows では FreeConsole() を呼び出す。
     * NORVES_ENABLE_DEBUG_OUTPUT が 0 の場合は no-op。
     */
    void CloseDebugConsole();

} // namespace NorvesLib::Core::Platform
