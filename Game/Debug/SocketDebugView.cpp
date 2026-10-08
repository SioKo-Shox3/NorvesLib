#if defined(NORVES_ENABLE_IMGUI)
#include "SocketDebugView.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Component/SocketAttachmentComponent.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Math/MatrixUtils.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/World.h"
#include "imgui.h"
namespace Game::Debug
{
    using namespace NorvesLib::Core;
    namespace M = NorvesLib::Math;
    SocketDebugView::~SocketDebugView()
    {
        Detach();
    }
    void SocketDebugView::Attach(World& world, uint64_t owner, uint64_t item)
    {
        Detach();
        m_World = &world;
        m_Owner = owner;
        m_Item = item;
        NorvesLib::Modules::Gui::RegisterImGuiView(this);
    }
    void SocketDebugView::Detach()
    {
        NorvesLib::Modules::Gui::UnregisterImGuiView(this);
        m_World = nullptr;
        m_Owner = 0;
        m_Item = 0;
    }
    void SocketDebugView::SubmitDebugDraw()
    {
        if (!m_bAxes || !m_World)
            return;
        auto* owner = m_World->FindEntityByObjectId(m_Owner);
        auto* mesh = owner ? owner->GetComponent<Component::SkinnedMeshComponent>() : nullptr;
        if (!mesh || mesh->IsPendingDestroy() || !mesh->GetSkeletalAsset() || !mesh->GetSkeletalAsset()->GetSkeleton())
            return;
        const M::Vector4 colors[] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
        for (const auto& socket : mesh->GetSkeletalAsset()->GetSkeleton()->GetSockets())
        {
            M::Transform world;
            if (!mesh->GetSocketWorldTransform(socket.Name, world))
                continue;
            const auto matrix = M::MatrixUtils::CreateWorldRowVector(world.position, world.rotation, M::Vector3::One);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const M::Vector3 direction(matrix.m[axis][0], matrix.m[axis][1], matrix.m[axis][2]);
                GEngine.GetDebugDraw().AddLine(world.position, world.position + direction * .2f, colors[axis]);
            }
        }
    }
    void SocketDebugView::OnImGui()
    {
        const bool visible = ImGui::Begin("ソケットと保持");
        auto* item = m_World ? m_World->FindEntityByObjectId(m_Item) : nullptr;
        auto* attachment = item ? item->GetComponent<Component::SocketAttachmentComponent>() : nullptr;
        if (visible && attachment && !attachment->IsPendingDestroy())
        {
            ImGui::Checkbox("ソケット軸", &m_bAxes);
            const char* states[] = {"未保持", "補間中", "保持中"};
            ImGui::Text("状態: %s", states[unsigned(attachment->GetAttachState())]);
            const auto& velocity = attachment->GetReleaseVelocity();
            ImGui::Text("離した速度: %.3f %.3f %.3f", velocity.Linear.x, velocity.Linear.y, velocity.Linear.z);
            const auto profiles = attachment->GetProfiles();
            for (uint32_t i = 0; i < profiles.size(); ++i)
            {
                ImGui::PushID(int(i));
                const auto name = profiles[i].Name.ToString();
                if (ImGui::Button(name.c_str()))
                    (void)attachment->SetProfileByIndex(i);
                auto value = profiles[i].Offset;
                float position[] = {value.position.x, value.position.y, value.position.z};
                float rotation[] = {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w};
                bool changed = ImGui::DragFloat3("位置", position, .005f);
                changed = ImGui::DragFloat4("回転 xyzw", rotation, .005f) || changed;
                if (changed)
                {
                    value.position = {position[0], position[1], position[2]};
                    value.rotation = M::QuaternionUtils::Normalize(
                        M::Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]));
                    m_bInvalid = !attachment->SetProfileOffset(i, value);
                }
                ImGui::PopID();
            }
            if (m_bInvalid)
                ImGui::TextUnformatted("値が不正なため変更を適用できません");
        }
        else if (visible)
            ImGui::TextUnformatted("保持コンポーネントがありません");
        ImGui::End();
        SubmitDebugDraw();
    }
} // namespace Game::Debug
#endif
