#pragma once

#include "BootConfig.h"
#include "Container/VariableArray.h"
#include "Container/String.h"

namespace NorvesLib::Core::Boot
{

    /**
     * @brief プラットフォーム固有の起動処理インターフェース
     *
     * 各プラットフォームはこのインターフェースを実装し、
     * OS固有の初期化処理を行います。
     */
    class IPlatformBoot
    {
    public:
        virtual ~IPlatformBoot() = default;

        /**
         * @brief プラットフォーム固有の初期化
         * @param config 起動設定
         * @return 成功時true
         */
        virtual bool Initialize(const BootConfig &config) = 0;

        /**
         * @brief プラットフォーム固有の終了処理
         */
        virtual void Shutdown() = 0;

        /**
         * @brief コマンドライン引数を解析
         * @return 解析済み引数配列
         */
        virtual Container::VariableArray<Container::String> ParseCommandLine() = 0;

        /**
         * @brief プラットフォームメッセージを処理
         * @return 続行する場合true、終了要求がある場合false
         */
        virtual bool ProcessMessages() = 0;

        /**
         * @brief 実行ファイルのパスを取得
         * @return 実行ファイルのフルパス
         */
        virtual Container::String GetExecutablePath() = 0;

        /**
         * @brief 作業ディレクトリを設定
         * @param path 新しい作業ディレクトリ
         * @return 成功時true
         */
        virtual bool SetWorkingDirectory(const Container::String &path) = 0;
    };

} // namespace NorvesLib::Core::Boot
