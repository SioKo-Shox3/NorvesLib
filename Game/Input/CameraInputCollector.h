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

        NorvesLib::Core::Input::InputState BuildFrameInputState() const;
        void ResetFrame();
        void ResetAll();

        const char* DebugName() const override
        {
            return "CameraInputCollector";
        }

    private:
        bool m_bLeftDown = false;
        bool m_bMiddleDown = false;
        bool m_bRightDown = false;
        bool m_bLeftAltDown = false;
        bool m_bRightAltDown = false;
        float m_AccumDeltaX = 0.0f;
        float m_AccumDeltaY = 0.0f;
        float m_AccumScrollDelta = 0.0f;
    };
} // namespace Game::Input
