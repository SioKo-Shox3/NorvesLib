#pragma once

#include "InputTypes.h"
#include "GamepadTypes.h"
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

        // 全押下とframe累積を解除し、次の絶対マウス位置を新基準にする。
        // Releasedは同frame押下も含めて次のBeginFrameまでラッチする。
        // 同frame再押下時はDownとReleasedが共にtrueになり得る。
        // Pressedは従来のcurrent/previous比較を維持する。
        void ReleaseAll();

        // down→upごとの世代。BeginFrameでは消さず、無効codeは0を返す。
        uint64_t GetKeyReleaseSerial(KeyCode code) const;
        uint64_t GetMouseButtonReleaseSerial(MouseButton button) const;

        // 検証してからsnapshotを一括更新する。invalid時はedge/serialも変更しない。
        // 非LiveはPressedラッチを消す。Baselineは操作正本も実値へ同期、Backgroundは操作のみneutral。
        // 物理sample/serialは全modeで実値を保持する。focusの操作Cancelは別途行う。
        bool SetGamepadState(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode = EGamepadSampleMode::Live);
        GamepadState GetGamepadState(uint8_t slot) const;
        GamepadState GetPreviousGamepadState(uint8_t slot) const;
        // 最後のprovider受理値（現在の操作状態ではない）。ReleaseAllでは消さない。
        GamepadState GetLastGamepadSample(uint8_t slot) const;
        uint64_t GetGamepadSampleSerial(uint8_t slot) const;
        bool IsGamepadButtonDown(uint8_t slot, GamepadButton button) const;
        // Pad edgeはframe内の遷移をラッチし、同frame短押下/解除も両方返す。
        bool IsGamepadButtonPressed(uint8_t slot, GamepadButton button) const;
        bool IsGamepadButtonReleased(uint8_t slot, GamepadButton button) const;
        uint64_t GetGamepadButtonReleaseSerial(uint8_t slot, GamepadButton button) const;
        float GetGamepadAxis(uint8_t slot, GamepadAxis axis) const;
        float GetGamepadTrigger(uint8_t slot, GamepadTrigger trigger) const;

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
        void SetMousePosition(float x, float y, bool accumulateDelta = true);
        /// absolute位置の次回差分を再seedする。Raw/wheel/buttonsは保持する。
        void ResetAbsoluteMouseTracking();

        /**
         * @brief マウススクロールを加算
         */
        void AddMouseScroll(float delta);
        // 両成分を検証後に一括加算。非finite/float範囲外では正本を変えない。
        bool AddMouseScrollAxes(float vertical, float horizontal);
        bool AddRawMouseDelta(float x, float y);
        // 有効な非zero入力を受理するたびに増える。frame内相殺/Resetでも活動を失わない。
        // 静止確認用の履歴であって、消費済みイベントを再配送するための値ではない。
        uint64_t GetRawMouseActivitySerial() const
        {
            return m_RawMouseActivitySerial;
        }
        uint64_t GetMouseScrollActivitySerial() const
        {
            return m_MouseScrollActivitySerial;
        }

        /**
         * @brief スクロールデルタをリセット
         */
        void ResetFrameAccumulators();

    private:
        void ApplyGamepadState(uint8_t slot, const GamepadState& state);
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

        uint64_t m_KeyReleaseSerial[KEY_COUNT]{};
        uint64_t m_MouseReleaseSerial[MOUSE_BUTTON_COUNT]{};
        bool m_KeyReleasedByReset[KEY_COUNT]{};
        bool m_MouseReleasedByReset[MOUSE_BUTTON_COUNT]{};

        GamepadState m_GamepadStates[GamepadSlotCount]{};
        GamepadState m_PrevGamepadStates[GamepadSlotCount]{};
        GamepadState m_LastGamepadSamples[GamepadSlotCount]{};
        uint64_t m_GamepadSampleSerial[GamepadSlotCount]{};
        uint16_t m_GamepadPressed[GamepadSlotCount]{};
        uint16_t m_GamepadReleased[GamepadSlotCount]{};
        uint64_t m_GamepadReleaseSerial[GamepadSlotCount][16]{};

        // マウス状態
        MouseState m_MouseState;
        uint64_t m_RawMouseActivitySerial = 0;
        uint64_t m_MouseScrollActivitySerial = 0;

        // 前フレームのマウス位置（デルタ計算用）
        float m_PrevMouseX;
        float m_PrevMouseY;

        // 初回のマウス位置更新かどうか
        bool m_bFirstMouseUpdate;
    };

} // namespace NorvesLib::Core::Input
