#pragma once

#include "Container/PointerTypes.h"
#include "BootConfig.h"

namespace NorvesLib
{

    // 前方宣言
    class IApplication;

} // namespace NorvesLib

namespace NorvesLib::Core::Boot
{

    /**
     * @brief アプリケーション起動ユーティリティ関数群
     *
     * ロガー初期化・アプリケーション実行・終了処理を統括します。
     * InitializePlatform / ShutdownPlatform は内部ヘルパとして
     * AppLauncher.cpp 内の無名名前空間に閉じています。
     */

    /**
     * @brief デフォルトアプリケーションを作成する
     *
     * ApplicationFactory を使用してプラットフォーム固有の実装を返す。
     *
     * @return 作成されたアプリケーションのインスタンス
     */
    Container::TUniquePtr<IApplication> CreateDefaultApplication();

    /**
     * @brief プラットフォーム初期化後にアプリケーションを実行する
     *
     * プラットフォーム初期化（作業ディレクトリ設定等）を行い、
     * ApplicationProcessor を通じてアプリケーションを実行する。
     *
     * @param config 起動設定
     * @return アプリケーションの終了コード
     */
    int RunApplication(const BootConfig& config);

    /**
     * @brief アプリケーションを起動する統合エントリポイント
     *
     * コンソール割当・ロガー初期化・アプリケーション実行・終了処理を
     * 一括で行います。プラットフォーム固有のエントリポイント
     * （WindowsEntryPoint の WinMain 等）から呼び出されます。
     *
     * @param config 起動設定（Arguments フィールドには呼び出し前に引数を設定すること）
     * @return アプリケーションの終了コード
     */
    int LaunchApplication(const BootConfig& config);

} // namespace NorvesLib::Core::Boot
