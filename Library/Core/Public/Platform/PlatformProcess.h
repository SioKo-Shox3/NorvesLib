#pragma once

#include "Container/String.h"

namespace NorvesLib::Core::Platform
{

    /**
     * @brief プロセス関連のプラットフォーム関数群
     *
     * 宣言は共通。実装はプラットフォーム別翻訳単位（Windows は
     * Private/Platform/Windows/WindowsPlatformProcess.cpp）で行う。
     */

    /**
     * @brief 実行ファイルのフルパスを取得する
     *
     * Windows では GetModuleFileName を使用する。
     * 取得に失敗した場合は空文字列を返す。
     *
     * @return 実行ファイルのフルパス。失敗時は空文字列。
     */
    Container::String GetExecutablePath();

    /**
     * @brief カレント作業ディレクトリを設定する
     *
     * Windows では SetCurrentDirectory を使用する。
     *
     * @param path 設定するディレクトリのパス
     * @return 成功した場合 true
     */
    bool SetWorkingDirectory(const Container::String& path);

    /**
     * @brief 実行ファイルが存在するディレクトリを取得する
     *
     * GetExecutablePath() から最後のパス区切り文字以前の部分を切り出す。
     * 失敗した場合は空文字列を返す。
     *
     * @return 実行ファイルが存在するディレクトリのパス。失敗時は空文字列。
     */
    Container::String GetExecutableDirectory();

} // namespace NorvesLib::Core::Platform
