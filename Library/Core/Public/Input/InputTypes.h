#pragma once

#include <cstdint>

namespace NorvesLib::Core::Input
{

    // ========================================
    // キーコード
    // ========================================

    /**
     * @brief プラットフォーム非依存のキーコード
     *
     * 各プラットフォームのネイティブキーコードから変換される共通キーコード。
     */
    enum class KeyCode : uint16_t
    {
        None = 0,

        // アルファベット
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        // 数字
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,

        // ファンクションキー
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        // 修飾キー
        LeftShift,
        RightShift,
        LeftCtrl,
        RightCtrl,
        LeftAlt,
        RightAlt,

        // 特殊キー
        Space,
        Escape,
        Tab,
        Enter,
        Backspace,
        Delete,
        Insert,
        Home,
        End,
        PageUp,
        PageDown,

        // 矢印キー
        Up,
        Down,
        Left,
        Right,

        // テンキー
        Numpad0,
        Numpad1,
        Numpad2,
        Numpad3,
        Numpad4,
        Numpad5,
        Numpad6,
        Numpad7,
        Numpad8,
        Numpad9,
        NumpadAdd,
        NumpadSubtract,
        NumpadMultiply,
        NumpadDivide,
        NumpadDecimal,
        NumpadEnter,

        // その他
        CapsLock,
        NumLock,
        ScrollLock,
        PrintScreen,
        Pause,

        // 記号
        Semicolon,    // ;
        Equal,        // =
        Comma,        // ,
        Minus,        // -
        Period,       // .
        Slash,        // /
        GraveAccent,  // `
        LeftBracket,  // [
        Backslash,    //
        RightBracket, // ]
        Apostrophe,   // '

        Count
    };

    // ========================================
    // マウスボタン
    // ========================================

    /**
     * @brief マウスボタンの種類
     */
    enum class MouseButton : uint8_t
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,
        Count
    };

    // ========================================
    // 入力アクション
    // ========================================

    /**
     * @brief 入力アクションの種類
     */
    enum class InputAction : uint8_t
    {
        Pressed,  ///< 押下された瞬間
        Released, ///< 離された瞬間
        Repeat    ///< 押し続けている（キーリピート）
    };

    // ========================================
    // マウス状態
    // ========================================

    /**
     * @brief 現在のマウス状態
     */
    struct MouseState
    {
        float PositionX = 0.0f;   ///< マウスX座標（クライアント座標）
        float PositionY = 0.0f;   ///< マウスY座標（クライアント座標）
        float DeltaX = 0.0f;      ///< 前フレームからのX移動量
        float DeltaY = 0.0f;      ///< 前フレームからのY移動量
        float ScrollDelta = 0.0f; ///< ホイール回転量
    };

    // ========================================
    // イベント構造体
    // ========================================

    /**
     * @brief キーイベント
     */
    struct KeyEvent
    {
        KeyCode Code = KeyCode::None;
        InputAction Action = InputAction::Pressed;
    };

    /**
     * @brief マウスボタンイベント
     */
    struct MouseButtonEvent
    {
        MouseButton Button = MouseButton::Left;
        InputAction Action = InputAction::Pressed;
        float PositionX = 0.0f;
        float PositionY = 0.0f;
    };

    /**
     * @brief マウス移動イベント
     */
    struct MouseMoveEvent
    {
        float PositionX = 0.0f;
        float PositionY = 0.0f;
        float DeltaX = 0.0f;
        float DeltaY = 0.0f;
    };

    /**
     * @brief マウススクロールイベント
     */
    struct MouseScrollEvent
    {
        float Delta = 0.0f;
    };

} // namespace NorvesLib::Core::Input
