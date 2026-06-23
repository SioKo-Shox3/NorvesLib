#pragma once

#include "CoreTypes.h"
#include "Input/InputTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Input
{

    class IInputController;

    /**
     * @brief 優先度付きイベント配送ルーター
     *
     * 登録された IInputController 群へ、優先度降順（高→低）に入力イベントを
     * 配送する。あるコントローラの対応 On* が true（consume）を返した時点で
     * 配送を停止し、それ以降（より低優先度）のコントローラへは届けない。
     * 同一優先度内は登録順で安定。
     *
     * 所有権: コントローラは借用ポインタ（非所有）。寿命は登録側が管理し、
     * 破棄前に必ず UnregisterController すること。
     *
     * スレッド: GameThread 専用。ロックは持たない（登録・配送は同一スレッド前提）。
     */
    class InputRouter
    {
    public:
        /// オーバーレイ UI 向けの標準優先度。ゲームより上位。
        static constexpr int32_t PriorityOverlay = 1000;
        /// ゲームロジック（カメラ等）向けの標準優先度。
        static constexpr int32_t PriorityGame = 0;

        InputRouter() = default;
        ~InputRouter() = default;

        // コピー・ムーブ禁止（借用ポインタ集合を保持するため）
        InputRouter(const InputRouter &) = delete;
        InputRouter &operator=(const InputRouter &) = delete;
        InputRouter(InputRouter &&) = delete;
        InputRouter &operator=(InputRouter &&) = delete;

        /**
         * @brief コントローラを優先度付きで登録する
         * @param controller 借用ポインタ（非所有）。nullptr は無視。
         * @param priority 配送優先度（大きいほど先に配送）
         *
         * 同一ポインタが既に登録済みなら priority を更新し再挿入する
         * （重複登録はしない）。
         */
        void RegisterController(IInputController *controller, int32_t priority);

        /**
         * @brief コントローラの登録を解除する（冪等）
         * @param controller 解除対象。未登録・nullptr なら no-op。
         */
        void UnregisterController(IInputController *controller);

        // 各イベントを優先度降順に配送する。consume された時点で停止。
        void DispatchMouseButton(const MouseButtonEvent &event);
        void DispatchMouseMove(const MouseMoveEvent &event);
        void DispatchMouseScroll(const MouseScrollEvent &event);
        void DispatchKey(const KeyEvent &event);
        void DispatchChar(const CharEvent &event);

    private:
        struct Entry
        {
            IInputController *Controller = nullptr;
            int32_t Priority = 0;
        };

        // 優先度降順・同優先度は登録順を維持する（挿入時にソート済みを保つ）。
        Container::VariableArray<Entry> m_Controllers;
    };

} // namespace NorvesLib::Core::Input
