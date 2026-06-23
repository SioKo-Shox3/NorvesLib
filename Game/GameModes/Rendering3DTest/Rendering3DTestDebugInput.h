#pragma once

#include "Core/Public/Input/IInputController.h"

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::GameModes
{

    /**
     * @brief F1-F5 デバッグビュー切替コントローラ（イベント駆動）
     *
     * Rendering3DTest 専用。InputRouter にゲーム優先度で登録し、F1-F5 の押下で
     * RenderWorld の DebugViewMode を切り替える。LightController と同じ
     * PriorityGame に並ぶが、互いに自分のキーのみ consume・他は透過するため
     * 共存する。ImGui がキーボードを掴んでいる間は上位で consume されるため
     * ここへは届かない（排他）。
     *
     * 写像（従来 inline と同一）:
     * - F1: Normal
     * - F2: Unlit
     * - F3: Wireframe
     * - F4: MegaGeometryClusters
     * - F5: 次モードへ巡回
     *
     * Alt 押下中は従来の `!IsAltDown()` ガードを踏襲し切替を抑止する
     * （Alt はカメラ操作等と競合するため）。Alt 自体は記録のみで透過する。
     */
    class Rendering3DTestDebugInput : public NorvesLib::Core::Input::IInputController
    {
    public:
        Rendering3DTestDebugInput() = default;
        ~Rendering3DTestDebugInput() override = default;

        /**
         * @brief 操作対象の RenderWorld を設定（借用ポインタ・非所有）
         */
        void SetRenderWorld(NorvesLib::Core::Rendering::RenderWorld *renderWorld);

        /**
         * @brief F1-F5 押下で DebugViewMode を切り替える
         *
         * F1-F5 は consume（true）。Alt の押下/解放は内部追跡のため記録するが
         * 透過（false）させる。その他のキーも透過する。
         */
        bool OnKey(const NorvesLib::Core::Input::KeyEvent &event) override;

        const char *DebugName() const override
        {
            return "Rendering3DTestDebugInput";
        }

    private:
        NorvesLib::Core::Rendering::RenderWorld *m_pRenderWorld = nullptr;
        bool m_bAltDown = false; ///< LeftAlt/RightAlt の押下状態（切替抑止に使用）
    };

} // namespace Game::GameModes
