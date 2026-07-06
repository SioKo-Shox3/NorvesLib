#pragma once

#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Input/InputState.h"

namespace Game::Input
{

    /**
     * @brief カメラ操作用の入力収集器（InputRouter 配下・イベント駆動）
     *
     * MayaCameraController::BuildIntent（poll 版）は生の InputState を直接読むが、
     * InputState 自体は InputRouter の優先度/consume（ImGui オーバーレイや
     * PickingController の Ctrl/Shift 選択開始 consume 等）とは独立した「常に更新
     * され続ける生状態」である。BuildIntent に生の InputState をそのまま渡すと、
     * 上位コントローラが入力を consume していてもカメラが反応してしまう
     * （入力排他の退行）。
     *
     * 本クラスは InputRouter に PriorityGame で登録し、イベント駆動
     * （OnMouseButton/OnMouseMove/OnMouseScroll）で「自分に届いた（＝上位で
     * consume されなかった）」入力だけをフレーム単位で蓄積する。GameMode の Tick
     * は BuildFrameInputState() で一時的な InputState を組み立て、それを
     * MayaCameraController::BuildIntent へ渡すことで、旧来のイベント駆動カメラと
     * 同じ入力排他（Overlay/Picking が consume した入力はカメラに届かない）を
     * 維持する。
     */
    class CameraInputCollector : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent &event) override;
        bool OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent &event) override;
        bool OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent &event) override;

        /**
         * @brief 蓄積済みの入力から、この1フレーム分の InputState を組み立てる
         *
         * ボタン押下状態と、フレーム内で蓄積した移動量・スクロール量を反映した
         * InputState を返す。呼び出し後は ResetFrame() で移動量・スクロール量を
         * クリアすること（ボタン押下状態は次フレームも継続するためクリアしない）。
         */
        NorvesLib::Core::Input::InputState BuildFrameInputState() const;

        /**
         * @brief フレーム内で蓄積した移動量・スクロール量をリセットする
         *
         * GameMode の Tick が BuildFrameInputState() を消費した直後に呼ぶ。
         * ボタン押下状態（m_bLeftDown 等）はリセットしない
         * （OnMouseButton の Released イベントでのみ解除されるため）。
         */
        void ResetFrame();

        const char *DebugName() const override
        {
            return "CameraInputCollector";
        }

    private:
        bool m_bLeftDown = false;
        bool m_bMiddleDown = false;
        bool m_bRightDown = false;
        float m_AccumDeltaX = 0.0f;
        float m_AccumDeltaY = 0.0f;
        float m_AccumScrollDelta = 0.0f;
    };

} // namespace Game::Input
