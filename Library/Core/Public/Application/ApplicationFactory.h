#pragma once

#include <memory>
#include "../IApplication.h"

namespace NorvesLib {
namespace Core {
namespace Boot {

/**
 * @brief アプリケーションインスタンスを作成するファクトリークラス
 *
 * エントリーポイントからアプリケーションインスタンスを作成するための
 * ユーティリティクラスです。
 */
class ApplicationFactory
{
public:
    /**
     * @brief デフォルトアプリケーションインスタンスを作成
     * @return 作成されたアプリケーションインスタンス
     */
    static std::unique_ptr<IApplication> CreateDefaultApplication();

    /**
     * @brief カスタムアプリケーションインスタンスを作成
     *
     * アプリケーション固有の実装を生成する際に使用します。
     * ゲーム側で実装するCreateApplication関数から呼び出されることを想定しています。
     *
     * @return 作成されたカスタムアプリケーションインスタンス
     */
    static std::unique_ptr<IApplication> CreateCustomApplication();
};

} // namespace Boot
} // namespace Core
} // namespace NorvesLib