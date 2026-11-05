#pragma once
#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#include "Core/Public/Animation/FootIKModifier.h"
#include "Core/Public/Component/AnimatorComponent.h"
#include "Core/Public/Text/JsonDocument.h"

namespace NorvesLib::Core
{
    class World;
}
namespace Game::Debug
{
    // 場面より短く生存する。World の終了前に Detach して補正と問い合わせの借用を解く。
    class PoseModifierDebugView final : public NorvesLib::Modules::Gui::IImGuiView
    {
      public:
        ~PoseModifierDebugView() override;
        void Attach(NorvesLib::Core::World& world, uint64_t entityObjectId,
                    const NorvesLib::Core::Container::String& json,
                    const NorvesLib::Core::Animation::IGroundQuery* ground, bool bEnabled);
        void Detach();
        void OnImGui() override;
        void SubmitDebugDraw();
        const char* GetViewName() const override
        {
            return "PoseModifiers";
        }

      private:
        class RecordingGroundQuery final : public NorvesLib::Core::Animation::IGroundQuery
        {
          public:
            const NorvesLib::Core::Animation::IGroundQuery* Source = nullptr;
            mutable NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Animation::GroundProbe> Probes;
            NorvesLib::Core::Animation::EGroundQueryStatus Query(
                const NorvesLib::Math::Vector3& origin, float maxDown,
                NorvesLib::Core::Animation::GroundHit& out) const override;
            void QueryBatch(NorvesLib::Core::Container::Span<NorvesLib::Core::Animation::GroundProbe> probes) const override;
        };
        struct NumberField
        {
            NorvesLib::Core::Container::String Path;
            double Value = 0;
            double AppliedValue = 0;
        };
        struct ModifierRow
        {
            NorvesLib::Core::Container::String Type, Parameter;
            NorvesLib::Core::Container::VariableArray<NumberField> Fields;
            int32_t Order = 0;
            float Weight = 1;
            bool bEnabled = true;
        };
        NorvesLib::Core::Component::AnimatorComponent* FindAnimator() const;
        bool ApplyJson(const NorvesLib::Core::Container::String& json, bool bInstall);
        bool EditNumber(const NorvesLib::Core::Container::String& path, double value);
        bool EnableConfiguredRow(size_t index);
        void RestoreQueries();
        void UpdateWeight(size_t index);
        void BuildRows();
        NorvesLib::Core::World* m_World = nullptr;
        uint64_t m_EntityId = 0;
        NorvesLib::Core::Container::String m_Json;
        NorvesLib::Core::JsonDocument m_Document;
        NorvesLib::Core::Container::VariableArray<ModifierRow> m_Rows;
        NorvesLib::Core::Container::VariableArray<
            NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Animation::IPoseModifier>> m_Modifiers;
        NorvesLib::Core::Animation::Procedural::PoseModifierConfigReport m_Report;
        RecordingGroundQuery m_Ground;
        bool m_bEnabled = false;
        bool m_bDraw = true;
    };
} // namespace Game::Debug
#endif
