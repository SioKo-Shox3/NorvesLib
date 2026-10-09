#pragma once

#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Input/InputState.h"

namespace Game::Input
{
    /**
     * @brief InputRouter で consume されず到達したカメラ入力を値状態へ集約します。
     *
     * MayaCameraController 自体を Router に登録せず、本 collector が受け取った
     * イベントだけでフレーム限定 InputState を構築します。これにより上位 UI や
     * PickingController が consume した入力をカメラ経路へ混入させません。
     */
    class CameraInputCollector : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent& event) override;
        bool OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent& event) override;
        bool OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent& event) override;
        bool OnKey(const NorvesLib::Core::Input::KeyEvent& event) override;
        /// ゲームパッドの Y ボタンの押下を、カメラパスの再生・停止の要求として覚える（消費しない）
        bool OnGamepadButton(const NorvesLib::Core::Input::GamepadButtonEvent& event) override;
        /// ゲームパッドのスティック・トリガーの値を保持する（自由飛行カメラ用。消費しない）
        bool OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent& event) override;

        NorvesLib::Core::Input::InputState BuildFrameInputState() const;
        /// P キー（ゲームパッドは Y）が押されていたら true を返し、要求を下ろす。押しっぱなしの繰り返しは数えない
        bool ConsumePathToggleRequest();
        void ResetFrame();
        void ResetAll();
        void OnInputReset() override { ResetAll(); }

        const char* DebugName() const override
        {
            return "CameraInputCollector";
        }

    private:
        /// 自由飛行カメラが読むキー（WASD・QE・Shift）。Alt は Maya 式のカメラ用に別に持つ
        static constexpr int kTrackedKeyCount = 8;
        static const NorvesLib::Core::Input::KeyCode kTrackedKeys[kTrackedKeyCount];

        bool m_bTrackedKeyDown[kTrackedKeyCount] = {};
        NorvesLib::Core::Input::GamepadState m_GamepadStates[NorvesLib::Core::Input::GamepadSlotCount] = {};
        bool m_bLeftDown = false;
        bool m_bMiddleDown = false;
        bool m_bRightDown = false;
        bool m_bLeftAltDown = false;
        bool m_bRightAltDown = false;
        bool m_bPathToggleRequested = false;
        float m_AccumDeltaX = 0.0f;
        float m_AccumDeltaY = 0.0f;
        float m_AccumScrollDelta = 0.0f;
    };
} // namespace Game::Input
