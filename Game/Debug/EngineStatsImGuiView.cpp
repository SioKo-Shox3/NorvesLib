#if defined(NORVES_ENABLE_IMGUI)

#include "EngineStatsImGuiView.h"

#include "Core/Public/Logging/LogMacros.h"

#include "imgui.h"

namespace Game::Debug
{

    void EngineStatsImGuiView::SetRenderingCoordinator(
        NorvesLib::Core::Rendering::RenderingCoordinator* renderingCoordinator)
    {
        m_pRenderingCoordinator = renderingCoordinator;
        ResetCachedState();
    }

    void EngineStatsImGuiView::ClearRenderingCoordinator()
    {
        m_pRenderingCoordinator = nullptr;
        ResetCachedState();
    }

    void EngineStatsImGuiView::OnImGui()
    {
        const bool bWindowVisible = ImGui::Begin("Engine Statistics");
        if (m_pRenderingCoordinator == nullptr)
        {
            if (bWindowVisible)
            {
                ImGui::TextUnformatted("Rendering coordinator unavailable.");
            }
            ImGui::End();
            return;
        }

        if (!m_bFirstOnImGuiLogged)
        {
            LOG_INFO("EngineStats ImGui first OnImGui coordinator_available=1");
            m_bFirstOnImGuiLogged = true;
        }

        const NorvesLib::Core::Rendering::RenderingCoordinatorStatsSnapshot statsSnapshot =
            m_pRenderingCoordinator->GetStatsSnapshot();
        const NorvesLib::Debug::RenderingStats& stats = statsSnapshot.Stats;
        if (!m_bFirstStatsSnapshotLogged && statsSnapshot.PublicationSequence > 0)
        {
            LOG_INFO("EngineStats first stats snapshot sequence=%llu frame=%llu generated_draws=%u draw_calls=%u completed=%d",
                     static_cast<unsigned long long>(statsSnapshot.PublicationSequence),
                     static_cast<unsigned long long>(stats.FrameNumber),
                     statsSnapshot.GeneratedDrawCommandCount,
                     stats.DrawCalls,
                     statsSnapshot.bRenderFrameCompleted ? 1 : 0);
            m_bFirstStatsSnapshotLogged = true;
        }

        NorvesLib::Core::Rendering::RenderGraphDebugDumpSnapshot candidateDump;
        if (m_pRenderingCoordinator->TryGetRenderGraphDebugDumpSnapshot(
                m_KnownDumpPublicationSequence,
                candidateDump))
        {
            m_RenderGraphDump = candidateDump;
            m_KnownDumpPublicationSequence = candidateDump.PublicationSequence;
            m_bDumpRequestPending = false;
            if (!m_bFirstDumpSnapshotLogged)
            {
                LOG_INFO("EngineStats first dump snapshot sequence=%llu available=%d truncated=%d",
                         static_cast<unsigned long long>(candidateDump.PublicationSequence),
                         candidateDump.bAvailable ? 1 : 0,
                         candidateDump.bTruncated ? 1 : 0);
                m_bFirstDumpSnapshotLogged = true;
            }
        }

        const double now = ImGui::GetTime();
        if (!m_bInitialDumpRequested)
        {
            RequestRenderGraphDump(now);
            m_bInitialDumpRequested = true;
        }

        if (!bWindowVisible)
        {
            ImGui::End();
            return;
        }

        if (statsSnapshot.PublicationSequence == 0)
        {
            ImGui::TextUnformatted("Render snapshot: waiting.");
        }
        else if (statsSnapshot.bRenderFrameCompleted)
        {
            ImGui::TextUnformatted("Render snapshot: completed.");
        }
        else
        {
            ImGui::TextUnformatted("Render snapshot: incomplete.");
        }

        ImGui::Text("FPS: %.1f", stats.FPS);
        ImGui::Text("Delta time: %.3f ms", stats.DeltaTime * 1000.0f);
        ImGui::Text("Draw commands: GT generated %u / RT authoritative %u",
                    statsSnapshot.GeneratedDrawCommandCount,
                    stats.DrawCalls);
        ImGui::Text("Triangles: %u", stats.TrianglesRendered);
        ImGui::Text("Visible objects: %u", stats.VisibleObjects);
        ImGui::Text("Batches: %u", stats.BatchCount);
        ImGui::Text("Instanced draw calls: %u", stats.InstancedDrawCalls);
        ImGui::Text("Saved draw calls: %u", stats.SavedDrawCalls);
        ImGui::Text("RenderGraph barriers: %u", stats.RenderGraphBarrierCount);
        ImGui::Text("RenderGraph transient acquires: %u", stats.RenderGraphTransientAcquireCount);

        ImGui::Separator();
        if (statsSnapshot.bGameThreadTimingsAvailable)
        {
            ImGui::Text("Collection: %.3f ms", stats.CollectionTimeMs);
            ImGui::Text("Culling: %.3f ms", stats.CullingTimeMs);
            ImGui::Text("Batching: %.3f ms", stats.BatchingTimeMs);
            ImGui::Text("Command generation: %.3f ms", stats.CommandGenerationTimeMs);
        }
        else
        {
            ImGui::TextUnformatted("Collection: unavailable.");
            ImGui::TextUnformatted("Culling: unavailable.");
            ImGui::TextUnformatted("Batching: unavailable.");
            ImGui::TextUnformatted("Command generation: unavailable.");
        }
        if (statsSnapshot.bRenderFrameTimingAvailable)
        {
            ImGui::Text("Render frame: %.3f ms", stats.RenderFrameTimeMs);
        }
        else
        {
            ImGui::TextUnformatted("Render frame: unavailable.");
        }
        if (statsSnapshot.bGPUTimeAvailable)
        {
            ImGui::Text("Latest completed GPU: %.3f ms", stats.GPUTimeMs);
        }
        else
        {
            ImGui::TextUnformatted("Latest completed GPU: unavailable.");
        }
        if (statsSnapshot.bTotalFrameTimeAvailable)
        {
            ImGui::Text("Total frame: %.3f ms", stats.TotalFrameTimeMs);
        }
        else
        {
            ImGui::TextUnformatted("Total frame: unavailable.");
        }

        const bool bRenderGraphOpen = ImGui::CollapsingHeader("RenderGraph");
        if (bRenderGraphOpen)
        {
            if (ImGui::Button("Refresh"))
            {
                RequestRenderGraphDump(now);
            }
            else if (!m_bDumpRequestPending && now >= m_NextAutomaticDumpRequestTime)
            {
                RequestRenderGraphDump(now);
            }

            if (m_bDumpRequestPending)
            {
                ImGui::TextUnformatted("RenderGraph capture pending.");
            }

            if (m_KnownDumpPublicationSequence == 0)
            {
                ImGui::TextUnformatted("No RenderGraph capture has been published.");
            }
            else if (!m_RenderGraphDump.bAvailable)
            {
                ImGui::TextUnformatted("RenderGraph capture unavailable:");
                ImGui::TextUnformatted(m_RenderGraphDump.UnavailableReason.c_str());
            }
            else
            {
                ImGui::Text("Captured frame: %llu, sequence: %llu",
                            static_cast<unsigned long long>(m_RenderGraphDump.FrameNumber),
                            static_cast<unsigned long long>(m_RenderGraphDump.PublicationSequence));
                if (m_RenderGraphDump.bTruncated)
                {
                    ImGui::TextUnformatted("Warning: RenderGraph dump was truncated.");
                }
                if (m_RenderGraphDump.Text.empty())
                {
                    ImGui::TextUnformatted("RenderGraph dump is empty.");
                }
                else
                {
                    ImGui::BeginChild("RenderGraphDump", ImVec2(0.0f, 240.0f), true,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextUnformatted(m_RenderGraphDump.Text.c_str());
                    ImGui::EndChild();
                }
            }
        }

        ImGui::End();
    }

    void EngineStatsImGuiView::ResetCachedState()
    {
        m_RenderGraphDump = {};
        m_KnownDumpPublicationSequence = 0;
        m_NextAutomaticDumpRequestTime = 0.0;
        m_bDumpRequestPending = false;
        m_bInitialDumpRequested = false;
        m_bFirstOnImGuiLogged = false;
        m_bFirstStatsSnapshotLogged = false;
        m_bFirstDumpSnapshotLogged = false;
    }

    void EngineStatsImGuiView::RequestRenderGraphDump(double now)
    {
        m_pRenderingCoordinator->RequestRenderGraphDebugDump();
        m_bDumpRequestPending = true;
        m_NextAutomaticDumpRequestTime = now + 1.0;
    }

} // namespace Game::Debug

#endif // NORVES_ENABLE_IMGUI
