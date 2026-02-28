#pragma once

#include "InputTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Input
{

    /**
     * @brief 入力状態クラス（ポーリングAPI）
     *
     * 現在のキーボード・マウスの状態を保持し、毎フレーム参照可能にします。
     * InputSystemが内部で管理し、外部にはconst参照で公開します。
     */
    class InputState
    {
    public:
        InputState();

        // ========================================
        // キーボード（ポーリング）
        // ========================================

        /**
         * @brief 指定キーが現在押されているか
         */
        bool IsKeyDown(KeyCode code) const;

        /**
         * @brief 指定キーがこのフレームで押されたか（立ち上がりエッジ）
         */
        bool IsKeyPressed(KeyCode code) const;

        /**
         * @brief 指定キーがこのフレームで離されたか（立ち下がりエッジ）
         */
        bool IsKeyReleased(KeyCode code) const;

        // ========================================
        // マウスボタン（ポーリング）
        // ========================================

        /**
         * @brief 指定マウスボタンが現在押されているか
         */
        bool IsMouseButtonDown(MouseButton button) const;

        /**
         * @brief 指定マウスボタンがこのフレームで押されたか
         */
        bool IsMouseButtonPressed(MouseButton button) const;

        /**
         * @brief 指定マウスボタンがこのフレームで離されたか
         */
        bool IsMouseButtonReleased(MouseButton button) const;

        // ========================================
        // マウス状態
        // ========================================

        /**
         * @brief 現在のマウス状態を取得
         */
        const MouseState &GetMouseState() const;

        // ========================================
        // 修飾キーの便利メソッド
        // ========================================

        /**
         * @brief Altキーが押されているか
         */
        bool IsAltDown() const;

        /**
         * @brief Ctrlキーが押されているか
         */
        bool IsCtrlDown() const;

        /**
         * @brief Shiftキーが押されているか
         */
        bool IsShiftDown() const;

        // ========================================
        // 内部更新API（InputSystem専用）
        // ========================================

        /**
         * @brief フレーム開始時に前フレームの状態を保存
         */
        void BeginFrame();

        /**
         * @brief キー状態を更新
         */
        void SetKeyState(KeyCode code, bool bDown);

        /**
         * @brief マウスボタン状態を更新
         */
        void SetMouseButtonState(MouseButton button, bool bDown);

        /**
         * @brief マウス位置を更新
         */
        void SetMousePosition(float x, float y);

        /**
         * @brief マウススクロールを加算
         */
        void AddMouseScroll(float delta);

        /**
         * @brief スクロールデルタをリセット
         */
        void ResetFrameAccumulators();

    private:
        static constexpr uint32_t KEY_COUNT = static_cast<uint32_t>(KeyCode::Count);
        static constexpr uint32_t MOUSE_BUTTON_COUNT = static_cast<uint32_t>(MouseButton::Count);

        // 現在フレームのキー状態
        bool m_KeyStates[KEY_COUNT];

        // 前フレームのキー状態
        bool m_PrevKeyStates[KEY_COUNT];

        // 現在フレームのマウスボタン状態
        bool m_MouseButtonStates[MOUSE_BUTTON_COUNT];

        // 前フレームのマウスボタン状態
        bool m_PrevMouseButtonStates[MOUSE_BUTTON_COUNT];

        // マウス状態
        MouseState m_MouseState;

        // 前フレームのマウス位置（デルタ計算用）
        float m_PrevMouseX;
        float m_PrevMouseY;

        // 初回のマウス位置更新かどうか
        bool m_bFirstMouseUpdate;
    };

} // namespace NorvesLib::Core::Input
