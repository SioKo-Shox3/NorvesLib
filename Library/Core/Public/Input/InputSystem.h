#pragma once

#include "InputTypes.h"
#include "InputState.h"
#include "Delegate/MulticastDelegate.h"

// MulticastDelegateはNorvesLib::Core名前空間にある
using NorvesLib::Core::MulticastDelegate;

namespace NorvesLib::Core::Input
{

    /**
     * @brief 入力システム
     *
     * GEngine配下のサブシステムとして、プラットフォーム非依存な入力管理を提供します。
     *
     * 機能:
     * - ポーリングAPI: InputStateを通じた毎フレームの状態取得
     * - イベントAPI: MulticastDelegateによるコールバック通知
     * - プラットフォーム入力のInject: WindowProc等からキー/マウスイベントを注入
     *
     * フレームフロー:
     * 1. BeginFrame() - 前フレーム状態の保存、累積値のリセット
     * 2. Inject*() - プラットフォームメッセージ処理中に呼ばれる
     * 3. (Update処理: ゲーム側がInputStateを参照)
     * 4. EndFrame() - フレーム終了処理
     */
    class InputSystem
    {
    public:
        InputSystem() = default;
        ~InputSystem() = default;

        // コピー・ムーブ禁止
        InputSystem(const InputSystem &) = delete;
        InputSystem &operator=(const InputSystem &) = delete;
        InputSystem(InputSystem &&) = delete;
        InputSystem &operator=(InputSystem &&) = delete;

        // ========================================
        // フレーム制御
        // ========================================

        /**
         * @brief フレーム開始処理
         *
         * 前フレームのキー/ボタン状態をprevにコピーし、
         * フレーム累積値（デルタ、スクロール）をリセットします。
         * ApplicationProcessor::Tick()の先頭で呼び出してください。
         */
        void BeginFrame();

        /**
         * @brief フレーム終了処理
         *
         * 現時点では予約。将来のバッファリング対応用。
         */
        void EndFrame();

        // ========================================
        // ポーリングAPI
        // ========================================

        /**
         * @brief 現在の入力状態を取得
         * @return 入力状態へのconst参照
         */
        const InputState &GetState() const;

        // ========================================
        // イベントAPI（MulticastDelegate）
        // ========================================

        /**
         * @brief キーイベントデリゲート
         */
        MulticastDelegate<const KeyEvent &> &OnKeyEvent();

        /**
         * @brief マウスボタンイベントデリゲート
         */
        MulticastDelegate<const MouseButtonEvent &> &OnMouseButtonEvent();

        /**
         * @brief マウス移動イベントデリゲート
         */
        MulticastDelegate<const MouseMoveEvent &> &OnMouseMoveEvent();

        /**
         * @brief マウススクロールイベントデリゲート
         */
        MulticastDelegate<const MouseScrollEvent &> &OnMouseScrollEvent();

        // ========================================
        // 入力注入API（プラットフォーム層から呼ばれる）
        // ========================================

        /**
         * @brief キーイベントを注入
         * @param code キーコード
         * @param action Pressed/Released/Repeat
         */
        void InjectKeyEvent(KeyCode code, InputAction action);

        /**
         * @brief マウスボタンイベントを注入
         * @param button マウスボタン
         * @param action Pressed/Released
         * @param x クライアントX座標
         * @param y クライアントY座標
         */
        void InjectMouseButton(MouseButton button, InputAction action, float x, float y);

        /**
         * @brief マウス移動イベントを注入
         * @param x クライアントX座標
         * @param y クライアントY座標
         */
        void InjectMouseMove(float x, float y);

        /**
         * @brief マウススクロールイベントを注入
         * @param delta スクロール量（正:上、負:下）
         */
        void InjectMouseScroll(float delta);

    private:
        // 入力状態
        InputState m_State;

        // イベントデリゲート
        MulticastDelegate<const KeyEvent &> m_OnKeyEvent;
        MulticastDelegate<const MouseButtonEvent &> m_OnMouseButtonEvent;
        MulticastDelegate<const MouseMoveEvent &> m_OnMouseMoveEvent;
        MulticastDelegate<const MouseScrollEvent &> m_OnMouseScrollEvent;
    };

} // namespace NorvesLib::Core::Input
