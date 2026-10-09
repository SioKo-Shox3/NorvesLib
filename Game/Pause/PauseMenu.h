#pragma once

// ---------------------------------------------------------------------------
// ポーズメニューの状態と入力
//
// ランチャー以外の全シーンで、Esc（ゲームパッドの Start）で開き、もう一度 Esc（Start・B）か「続ける」で閉じる。
// 項目は「続ける」「ランチャーへ戻る」「ゲームを終了」の3つ。キーボード（矢印・WASD・Enter・Space）と
// ゲームパッド（十字キー・左スティック・A）は IInputController として受け、マウス（ホバーとクリック）は
// 描画側（PauseMenuView）が ImGui から受けて同じ状態を更新する。
//
// 開いている間は、押下・移動・スクロールなどの入力を下位（シーンのカメラ・デバッグ表示の切替・ピッキングなど）へ
// 渡さない。エンジンの入力文脈やカーソルの切り替え、遷移の要求はこの型の外（GameApplicationHandler）が行う。
// ---------------------------------------------------------------------------

#include "Core/Public/Input/IInputController.h"

#include <cstdint>

namespace Game::Pause
{

    class PauseMenu final : public NorvesLib::Core::Input::IInputController
    {
    public:
        /// 項目の並び（添字がそのまま表示の順）
        enum class Item : int
        {
            Resume = 0,
            ReturnToLauncher = 1,
            Quit = 2,
        };

        static constexpr int kItemCount = 3;

        /// Esc で開けるか。ランチャーの間は false にする。false にした時点で開いていれば閉じる
        void SetEnabled(bool bEnabled);
        bool IsEnabled() const
        {
            return m_bEnabled;
        }

        bool IsOpen() const
        {
            return m_bOpen;
        }

        /// メニューを開く（--open-pause-menu の撮影用）。無効なときは何もしない
        void Open();

        /// メニューを閉じる
        void Close();

        int GetSelected() const
        {
            return m_Selected;
        }

        /// 選択を直接替える（マウスのホバー）。範囲外は無視する
        void SetSelected(int index);

        /// 添字を指して項目の決定を要求する（マウスのクリック）
        void RequestActivate(int index);

        /// 決定の要求があれば項目を返して消す
        bool ConsumeActivation(Item &item);

        // --- IInputController ---
        bool OnKey(const NorvesLib::Core::Input::KeyEvent &event) override;
        bool OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent &event) override;
        bool OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent &event) override;
        bool OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent &event) override;
        bool OnMouseRawMove(const NorvesLib::Core::Input::MouseRawMoveEvent &event) override;
        bool OnGamepadButton(const NorvesLib::Core::Input::GamepadButtonEvent &event) override;
        bool OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent &event) override;
        void OnInputReset() override;
        const char *DebugName() const override
        {
            return "PauseMenu";
        }

    private:
        void Move(int delta);
        void Activate();

        bool m_bEnabled = false;
        bool m_bOpen = false;
        int m_Selected = 0;
        int m_ActivateIndex = -1;
        /// 左スティックが上・下へ倒れているか（倒した瞬間だけ移動にする）
        bool m_StickUpHeld = false;
        bool m_StickDownHeld = false;
        /// Start の押下を受けてから、物理的に離れたのを見るまで true。押しっぱなしの再押下で閉じ直さないため
        bool m_bStartLatched = false;
    };

} // namespace Game::Pause
