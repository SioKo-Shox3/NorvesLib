#pragma once

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

        /**
         * @brief 入力イベントをポーリングしてInputSystemに注入
         * @param system 注入先のInputSystem
         *
         * フレームごとに呼ばれ、プラットフォーム固有のイベントを
         * InputSystemのInject*メソッドに変換して渡します。
         */
        virtual void PollEvents(InputSystem &system) = 0;
    };

} // namespace NorvesLib::Core::Input
