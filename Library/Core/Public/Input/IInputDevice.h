#pragma once

#include <cstdint>

namespace NorvesLib::Core::Input
{

    class InputSystem;

    /**
     * @brief 入力デバイスインターフェース
     *
     * プラットフォーム固有の入力ソースを抽象化するインターフェース。
     *
     * 現在のWindowsWindow実装ではWindowProc内で直接InjectするためIInputDeviceは必須ではありませんが、
     * 将来のプラットフォーム拡張（Linux/X11/Wayland、コンソール、ゲームパッド等）に備えて
     * インターフェースを定義しておきます。
     *
     * 使用例:
     * - LinuxInputDevice: X11/Waylandのイベントを変換してInject
     * - GamepadInputDevice: XInput/DirectInputのゲームパッド入力を変換
     */
    // 全操作はGameThread。InputSystemはPoll中だけ借用し、後へ保持しない。
    // Initialize/Shutdown/SetFocused/破棄から入力通知やownerへの再入を行わない。
    // Shutdownは部分初期化後も呼べること。例外時はownerが所有を維持し再停止する。
    // ProvidesGamepadStateの値は所有期間中不変とする。
    class IInputDevice
    {
    public:
        virtual ~IInputDevice() = default;

        /**
         * @brief デバイスの初期化
         * @return 初期化成功時true
         */
        virtual bool Initialize() = 0;

        /**
         * @brief デバイスの終了処理
         */
        virtual void Shutdown() = 0;

        // 終了結果を返すowner用入口。旧Shutdown実装の例外も失敗として保持する。
        virtual bool TryShutdown() noexcept
        {
            try
            {
                Shutdown();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // 左右低/高周波motorの0..1。trueは今回の送信受理、未対応の既定実装はfalse。
        virtual bool SetVibration(uint8_t slot, float low, float high) noexcept
        {
            (void)slot;
            (void)low;
            (void)high;
            return false;
        }

        /**
         * @brief 入力イベントをポーリングしてInputSystemに注入
         * @param system 注入先のInputSystem
         *
         * フレームごとに呼ばれ、プラットフォーム固有のイベントを
         * InputSystemのInject*メソッドに変換して渡します。
         */
        virtual void PollEvents(InputSystem &system) = 0;

        // GameThreadの非scaled単調秒。旧実装は従来のpollへ委譲する。
        // 時刻付き/旧pollを同一初期化期間で混用しない。falseは入力源の未回復errorを示す。
        virtual bool PollEvents(InputSystem& system, double unscaledTimeSeconds)
        {
            (void)unscaledTimeSeconds;
            PollEvents(system);
            return true;
        }

        // ownerはfocus喪失時のMapper取消/正本resetも別途実施する。
        virtual void SetFocused(bool focused) noexcept
        {
            (void)focused;
        }

        // trueのdeviceは全gamepad slotを供給する。ownerは同時に1つだけ登録する。
        virtual bool ProvidesGamepadState() const noexcept
        {
            return false;
        }
    };

} // namespace NorvesLib::Core::Input
