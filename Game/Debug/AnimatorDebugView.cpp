#if defined(NORVES_ENABLE_IMGUI)
#include "AnimatorDebugView.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/Entity.h"
#include "imgui.h"
namespace Game::Debug
{
    using namespace NorvesLib::Core;
    AnimatorDebugView::~AnimatorDebugView()
    {
        Detach();
    }
    void AnimatorDebugView::Attach(World& world, uint64_t id)
    {
        Detach();
        m_World = &world;
        m_EntityId = id;
        NorvesLib::Modules::Gui::RegisterImGuiView(this);
    }
    void AnimatorDebugView::Detach()
    {
        NorvesLib::Modules::Gui::UnregisterImGuiView(this);
        m_World = nullptr;
        m_EntityId = 0;
        m_Snapshot = {};
    }
    void AnimatorDebugView::OnImGui()
    {
        const bool visible = ImGui::Begin("アニメーション");
        Entity* entity = m_World ? m_World->FindEntityByObjectId(m_EntityId) : nullptr;
        auto* animator =
            entity && !entity->IsPendingDestroy() ? entity->GetComponent<Component::AnimatorComponent>() : nullptr;
        if (visible && animator)
        {
            animator->BuildDebugSnapshot(m_Snapshot);
            bool frozen = m_Snapshot.bFrozen;
            if (ImGui::Checkbox("停止", &frozen))
            {
                animator->SetFrozen(frozen);
            }
            ImGui::SameLine();
            if (ImGui::Button("1コマ"))
            {
                animator->SetFrozen(true);
                (void)animator->Step(1.f / 60);
            }
            for (size_t i = 0; i < m_Snapshot.Parameters.size(); ++i)
            {
                auto& p = m_Snapshot.Parameters[i];
                ImGui::PushID(int(i));
                const auto name = p.Name.ToString();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                switch (p.Type)
                {
                case Animation::AnimParamType::Float:
                    if (ImGui::DragFloat("##value", &p.Value.Float, .01f))
                    {
                        (void)animator->SetFloat(uint32_t(i), p.Value.Float);
                    }
                    break;
                case Animation::AnimParamType::Int:
                    if (ImGui::InputInt("##value", &p.Value.Int))
                    {
                        (void)animator->SetInt(uint32_t(i), p.Value.Int);
                    }
                    break;
                case Animation::AnimParamType::Bool:
                    if (ImGui::Checkbox("##value", &p.Value.Bool))
                    {
                        (void)animator->SetBool(uint32_t(i), p.Value.Bool);
                    }
                    break;
                case Animation::AnimParamType::Trigger:
                    if (ImGui::Button("発火"))
                    {
                        (void)animator->SetTrigger(uint32_t(i));
                    }
                    break;
                }
                ImGui::PopID();
            }
            for (const auto& group : m_Snapshot.SyncGroups)
            {
                const auto name = group.Name.ToString();
                ImGui::Text("同期: %s", name.c_str());
                ImGui::ProgressBar(group.Phase);
            }
            for (const auto& node : m_Snapshot.Nodes)
            {
                const auto name = node.Name.ToString();
                ImGui::TextUnformatted(name.c_str());
                ImGui::ProgressBar(node.Weight);
                if (node.bStateMachine)
                {
                    const auto current = node.State.Current.ToString();
                    ImGui::Text("状態: %s  時間: %.3f", current.c_str(), node.State.NormalizedTime);
                    if (node.State.bTransitioning)
                    {
                        ImGui::ProgressBar(node.State.Transition);
                    }
                    ImGui::PushID(name.c_str());
                    const auto graph = animator->GetGraph();
                    if (graph && graph->GetData())
                        for (const auto& definition : graph->GetData()->Nodes)
                        {
                            if (definition.Name != node.Name)
                                continue;
                            for (const auto& state : definition.States)
                            {
                                const auto target = state.Name.ToString();
                                if (ImGui::SmallButton(target.c_str()))
                                    (void)animator->RequestState(definition.Name, state.Name, .15f);
                                ImGui::SameLine();
                            }
                            ImGui::NewLine();
                        }
                    ImGui::PopID();
                }
            }
        }
        else if (visible)
        {
            ImGui::TextUnformatted("Animatorがありません");
        }
        ImGui::End();
    }
} // namespace Game::Debug
#endif
