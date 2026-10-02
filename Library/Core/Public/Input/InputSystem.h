#pragma once

#include "InputTypes.h"
#include "InputState.h"
#include "ActiveDeviceKind.h"
#include "Delegate/MulticastDelegate.h"

// MulticastDelegateはNorvesLib::Core名前空間にある
using NorvesLib::Core::MulticastDelegate;

namespace NorvesLib::Core::Input
{

    class InputRouter;
    class InputRebindCaptureManager;
    class InputDebugOverlayController;

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
         * 保留された操作resetがあれば、前フレーム保存の前に通知して回収する。
         *
         * 前フレームのキー/ボタン状態をprevにコピーし、
         * フレーム累積値（デルタ、スクロール）をリセットします。
         * ApplicationProcessor::Tick()の先頭で呼び出してください。
         */
        void BeginFrame();
        // 有限非負・非減少の非scaled秒。失敗時は正本/活動stateとも非変更。
        // 旧入口はsteady_clockを使う。異なる時刻原点の入口を混用しない。
        bool CanBeginFrame(double unscaledTimeSeconds) const noexcept { return m_ActiveDevice.CanBeginFrame(unscaledTimeSeconds); }
        bool BeginFrame(double unscaledTimeSeconds);
        EInputDeviceKind GetActiveDeviceKind() const noexcept { return m_ActiveDevice.GetKind(); }
        bool ConfigureDeviceActivity(const InputDeviceActivitySettings& settings) noexcept { return m_ActiveDevice.Configure(settings); }
        InputDeviceActivitySettings GetDeviceActivitySettings() const noexcept { return m_ActiveDevice.GetSettings(); }
        // focus変更自体はkind/通知を合成しない。OS callbackから呼べる値操作のみ。
        void SetInputFocused(bool focused) noexcept { m_ActiveDevice.SetFocused(focused); }
        // EndFrameで最終kindの変更だけ通知。UI消費とは独立、GameThread専用。
        // callbackはframe再入/本体破棄/例外送出を行わない。Delegate購読変更規約に従う。
        MulticastDelegate<EInputDeviceKind>& OnActiveDeviceKindChanged() { return m_OnActiveDeviceKindChanged; }

        /**
         * @brief フレーム終了処理
         *
         * 入力配送後、最後に通知した値からkindが変わっている場合だけ通知する。
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

        /**
         * @brief 文字入力イベントデリゲート（IME 確定後の Unicode 文字）
         */
        MulticastDelegate<const CharEvent &> &OnCharEvent();

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
        void InjectMouseMove(float x, float y, bool accumulateDelta = true);
        // absolute基準化だけ。通常イベントや取消通知を合成しない。
        void ResetAbsoluteMouseTracking() { m_State.ResetAbsoluteMouseTracking(); }

        /**
         * @brief マウススクロールイベントを注入
         * @param delta スクロール量（正:上、負:下）
         */
        void InjectMouseScroll(float delta);
        // 検証失敗時は正本/通知とも非変更。成功時は正本→Delegate→Routerの順。
        bool InjectMouseScrollAxes(float vertical, float horizontal);
        bool InjectRawMouseDelta(float x, float y);
        // 成功時は同値でも最後にsampleイベントを配送する。ReleaseAllはsampleを合成しない。
        // 非Liveはconnection/物理sampleだけを通知し、通常button eventは配送しない。
        // focus喪失時のMapper停止/ReleaseAllは、この同期modeとは別に先行させる。
        bool InjectGamepadState(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode = EGamepadSampleMode::Live);
        // 通常Releasedを合成せず、全controllerへ取消通知を届ける。
        // OS callback内でも呼ばれる。購読Delegate/Router observerはwindow表示/activation、
        // window/Engineの破棄、登録変更、再入配送、例外送出を行わない。
        // 高位のfocus処理はApplicationHandlerへ遅延される。
        void ReleaseAll();

        // 所有終了時はcallbackを呼ばず操作正本を即時中立化する。
        // 次の安全なBeginFrame/AttachでReleaseAll通知を回収。GameThread・配送外専用。
        void DeferReleaseAll() noexcept
        {
            m_State.ReleaseAll();
            m_bDeferredInputReset = true;
        }
        MulticastDelegate<const MouseRawMoveEvent&>& OnMouseRawMoveEvent() { return m_OnMouseRawMoveEvent; }
        MulticastDelegate<const GamepadButtonEvent&>& OnGamepadButtonEvent() { return m_OnGamepadButtonEvent; }
        MulticastDelegate<const GamepadSampleEvent&>& OnGamepadSampleEvent() { return m_OnGamepadSampleEvent; }
        MulticastDelegate<const GamepadConnectionEvent&>& OnGamepadConnectionEvent() { return m_OnGamepadConnectionEvent; }
        // 上記ReleaseAllのOS callback制約に従う即時取消通知。
        MulticastDelegate<>& OnInputResetEvent() { return m_OnInputResetEvent; }
        // 全Inject/ReleaseAllとRouter登録操作はGameThread、通知callbackからの再入は禁止。

        /**
         * @brief 文字入力イベントを注入（プラットフォームの WM_CHAR 相当から）
         * @param codepoint Unicode コードポイント（UTF-32）
         *
         * 状態は持たず OnCharEvent を Broadcast するのみ（テキスト入力は瞬間イベント）。
         */
        void InjectCharEvent(uint32_t codepoint);

        // ========================================
        // イベントルーティング
        // ========================================

        /**
         * @brief イベント配送ルーターを設定
         * @param router 借用ポインタ（非所有）。nullptr で配送を無効化。
         *
         * 設定後、各 Inject* は InputState 更新・MulticastDelegate Broadcast に
         * 加えて Router へ Dispatch する。Router は GameThread 専用。
         */
        void SetRouter(InputRouter *router);

    private:
        friend class InputRebindCaptureManager;
        friend class InputDebugOverlayController;
        // 入力状態
        InputState m_State;
        ActiveDeviceKindState m_ActiveDevice;
        EInputDeviceKind m_NotifiedDeviceKind = EInputDeviceKind::KeyboardMouse;
        bool m_bNotifyingDeviceKind = false;
        MulticastDelegate<EInputDeviceKind> m_OnActiveDeviceKindChanged;
        // controller所有終了時の通知は次の安全なAttach/frame開始まで保留する。
        bool m_bDeferredInputReset = false;

        // イベントデリゲート
        MulticastDelegate<const KeyEvent &> m_OnKeyEvent;
        MulticastDelegate<const MouseButtonEvent &> m_OnMouseButtonEvent;
        MulticastDelegate<const MouseMoveEvent &> m_OnMouseMoveEvent;
        MulticastDelegate<const MouseScrollEvent &> m_OnMouseScrollEvent;
        MulticastDelegate<const CharEvent &> m_OnCharEvent;
        MulticastDelegate<const MouseRawMoveEvent&> m_OnMouseRawMoveEvent;
        MulticastDelegate<const GamepadButtonEvent&> m_OnGamepadButtonEvent;
        MulticastDelegate<const GamepadConnectionEvent&> m_OnGamepadConnectionEvent;
        MulticastDelegate<const GamepadSampleEvent&> m_OnGamepadSampleEvent;
        MulticastDelegate<> m_OnInputResetEvent;

        // イベント配送ルーター（借用ポインタ・非所有）
        InputRouter *m_Router = nullptr;
    };

} // namespace NorvesLib::Core::Input
