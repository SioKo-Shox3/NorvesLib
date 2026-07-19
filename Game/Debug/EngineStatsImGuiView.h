#if defined(NORVES_ENABLE_IMGUI)

#pragma once

#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "ImGuiModule/IImGuiView.h"

namespace Game::Debug
{

    /**
     * @brief RenderingCoordinator の公開済み診断スナップショットを表示する ImGui ビュー
     *
     * RenderingCoordinator は Engine が所有し、このビューは登録側から借用したポインタを
     * 保持するだけである。RenderGraph の live object には触れず、RenderThread が公開した
     * 値コピーの mailbox だけをポーリングする。
     */
    class EngineStatsImGuiView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        void SetRenderingCoordinator(NorvesLib::Core::Rendering::RenderingCoordinator* renderingCoordinator);
        void ClearRenderingCoordinator();

        void OnImGui() override;

        const char* GetViewName() const override
        {
            return "EngineStats";
        }

    private:
        void ResetCachedState();
        void RequestRenderGraphDump(double now);

        // Engine/RenderWorld が所有する借用ポインタ。delete しない。
        NorvesLib::Core::Rendering::RenderingCoordinator* m_pRenderingCoordinator = nullptr;
        NorvesLib::Core::Rendering::RenderGraphDebugDumpSnapshot m_RenderGraphDump;
        uint64_t m_KnownDumpPublicationSequence = 0;
        double m_NextAutomaticDumpRequestTime = 0.0;
        bool m_bDumpRequestPending = false;
        bool m_bInitialDumpRequested = false;
        bool m_bFirstOnImGuiLogged = false;
        bool m_bFirstStatsSnapshotLogged = false;
        bool m_bFirstDumpSnapshotLogged = false;
    };

} // namespace Game::Debug

#endif // NORVES_ENABLE_IMGUI
