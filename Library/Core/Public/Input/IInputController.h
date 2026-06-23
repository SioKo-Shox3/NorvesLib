#pragma once

#include "Input/InputTypes.h"

namespace NorvesLib::Core::Input
{

    /**
     * @brief 入力コントローラインターフェース（イベント dispatch の受け手）
     *
     * InputRouter に優先度付きで登録される入力配送の受け手。各 On* は
     * 「自分がそのイベントを consume したか」を bool で返す。true を返すと
     * Router は以降の（より低優先度の）コントローラへ配送せず停止する。
     * false（既定）なら下位へ伝播する。
     *
     * @note 本インターフェースは Core 内に閉じ、特定の UI/オーバーレイ実装に
     *       非依存。オーバーレイ等の上位 UI は Game/Module 側で本 IF を実装し
     *       Router へ登録する。
     */
    class IInputController
    {
    public:
        virtual ~IInputController() = default;

        /**
         * @brief マウスボタンイベントを受け取る
         * @return true = consume（下位へ伝播させない）
         */
        virtual bool OnMouseButton(const MouseButtonEvent &)
        {
            return false;
        }

        /**
         * @brief マウス移動イベントを受け取る
         * @return true = consume（下位へ伝播させない）
         */
        virtual bool OnMouseMove(const MouseMoveEvent &)
        {
            return false;
        }

        /**
         * @brief マウススクロールイベントを受け取る
         * @return true = consume（下位へ伝播させない）
         */
        virtual bool OnMouseScroll(const MouseScrollEvent &)
        {
            return false;
        }

        /**
         * @brief キーイベントを受け取る
         * @return true = consume（下位へ伝播させない）
         */
        virtual bool OnKey(const KeyEvent &)
        {
            return false;
        }

        /**
         * @brief 文字入力イベントを受け取る
         * @return true = consume（下位へ伝播させない）
         */
        virtual bool OnChar(const CharEvent &)
        {
            return false;
        }

        /**
         * @brief デバッグ用のコントローラ名
         */
        virtual const char *DebugName() const = 0;
    };

} // namespace NorvesLib::Core::Input
