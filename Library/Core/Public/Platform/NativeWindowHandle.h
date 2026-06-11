#pragma once

#include <cstdint>

namespace NorvesLib::Core::Platform
{

    /**
     * @brief プラットフォーム非依存のネイティブウィンドウハンドル
     *
     * プラットフォーム固有のウィンドウハンドルを型情報付きで保持します。
     * Windows.h 等のプラットフォームヘッダに依存しません。
     *
     * 各プラットフォームでのハンドルの意味:
     * - Win32:   Handle1 = HWND, Handle2 = HINSTANCE
     * - X11:     Handle1 = Window (XID), Handle2 = Display*
     * - Wayland: Handle1 = wl_surface*, Handle2 = wl_display*
     * - Cocoa:   Handle1 = NSWindow*, Handle2 = 未使用
     */
    struct NativeWindowHandle
    {
        /**
         * @brief ウィンドウハンドルの種別
         */
        enum class Type : uint8_t
        {
            None,    // 無効（未設定）
            Win32,   // Windows (HWND / HINSTANCE)
            X11,     // X11 (Window / Display*)
            Wayland, // Wayland (wl_surface* / wl_display*)
            Cocoa,   // macOS (NSWindow*)
        };

        Type WindowType = Type::None; // ハンドル種別
        void* Handle1 = nullptr;      // 主ハンドル（Win32: HWND）
        void* Handle2 = nullptr;      // 副ハンドル（Win32: HINSTANCE）

        /**
         * @brief ハンドルが有効かどうか
         * @return 種別が設定され、主ハンドルが非nullならtrue
         */
        bool IsValid() const
        {
            return WindowType != Type::None && Handle1 != nullptr;
        }
    };

} // namespace NorvesLib::Core::Platform
