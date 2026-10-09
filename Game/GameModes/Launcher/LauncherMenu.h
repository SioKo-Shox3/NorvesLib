#pragma once

// ---------------------------------------------------------------------------
// ランチャーの選択状態と入力
//
// 項目は「シーンのカード（Scenes::kSceneCatalog の並び）」の後ろに「ゲームを終了」を1つ足した並び。
// キーボード（矢印・WASD・Enter・Space）とゲームパッド（十字キー・左スティック・A）は IInputController として
// 受け、マウス（ホバーとクリック）は描画側（LauncherView）が ImGui から受けて同じ状態を更新する。
// 項目の並べ方（列の数）は描画側が決めるので、方向の入力は積んでおき、描画側が ApplyNavigation で適用する。
// ---------------------------------------------------------------------------

#include "Core/Public/Input/IInputController.h"

#include <cstdint>

namespace Game::GameModes
{

    class LauncherMenu final : public NorvesLib::Core::Input::IInputController
    {
    public:
        enum class Direction : uint8_t
        {
            Up,
            Down,
            Left,
            Right,
        };

        /// 項目の数（カードの数 + 「ゲームを終了」）を決めて、先頭を選ぶ
        void Reset(int itemCount);

        int GetItemCount() const
        {
            return m_ItemCount;
        }
        int GetSelected() const
        {
            return m_Selected;
        }
        /// 「ゲームを終了」の項目の添字（= カードの数）
        int GetQuitIndex() const
        {
            return m_ItemCount - 1;
        }

        /// 選択を直接替える（マウスのホバー）。範囲外は無視する
        void SetSelected(int index);

        /// 方向の入力を積む
        void PushDirection(Direction direction);

        /// 積んだ方向の入力を、カードを columns 列に並べた前提で選択へ反映する
        void ApplyNavigation(int columns);

        /// 項目の決定を要求する（Enter・クリック・A）
        void RequestActivate(int index);

        /// 決定の要求があれば添字を返して消す
        bool ConsumeActivation(int &index);

        // --- IInputController ---
        bool OnKey(const NorvesLib::Core::Input::KeyEvent &event) override;
        bool OnGamepadButton(const NorvesLib::Core::Input::GamepadButtonEvent &event) override;
        bool OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent &event) override;
        void OnInputReset() override;
        const char *DebugName() const override
        {
            return "LauncherMenu";
        }

    private:
        static constexpr int kMaxPendingDirections = 16;

        int m_ItemCount = 0;
        int m_Selected = 0;
        /// 「ゲームを終了」から上へ戻るときのカード
        int m_ReturnCard = 0;
        int m_ActivateIndex = -1;
        Direction m_Pending[kMaxPendingDirections] = {};
        int m_PendingCount = 0;
        /// 左スティックが各方向へ倒れているか（倒した瞬間だけ方向の入力にする）
        bool m_StickHeld[4] = {};
    };

} // namespace Game::GameModes
