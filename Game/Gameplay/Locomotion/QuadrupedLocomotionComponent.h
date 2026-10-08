#pragma once
#include "Asset/TextAssetReloadTracker.h"
#include "Component/Component.h"
#include "Delegate/Delegate.h"
#include "Input/InputMapper.h"
#include "Locomotion/QuadrupedLocomotionModel.h"
#include "Physics/CharacterBodyComponent.h"
namespace Game::Gameplay
{
    // 同じEntityのCharacterBodyを駆動するGame側の接着層。入力・設定・見た目を物理moduleへ持ち込まない。
    class QuadrupedLocomotionComponent : public NorvesLib::Core::Component::Component
    {
        REFLECTION_CLASS(QuadrupedLocomotionComponent, NorvesLib::Core::Component::Component)
      public:
        NorvesLib::Core::Engine::TimeChannel GetTimeChannel() const noexcept override
        {
            return NorvesLib::Core::Engine::TimeChannel::Unscaled;
        }
        ~QuadrupedLocomotionComponent() override;
        void Initialize() override;
        void EndPlay() override;
        void Finalize() override;
        void Enable() override;
        void Disable() override;
        void OnTickGroup(NorvesLib::Core::Component::ETickGroup group, float dt) override;
        // MapperのJumpイベントを単独で消費する。Mapperはこのcomponentより長生きすること。
        void BindInput(NorvesLib::Core::Input::InputMapper* mapper);
        bool SetProfilePath(NorvesLib::Core::Container::AnsiStringView path);
        bool SetDriveMode(NorvesLib::Modules::Physics::CharacterDriveMode mode);
        // VisualRootはownerの子、VisualPoseはその子孫。可変の傾きはPose側へ置き補間履歴を切らない。
        bool SetVisualRoots(NorvesLib::Core::Entity* visualRoot, NorvesLib::Core::Entity* visualPose);
        bool SetViewSource(NorvesLib::Core::Entity* view);
        const NorvesLib::Core::Locomotion::LocomotionOutput& GetOutput() const
        {
            return m_Output;
        }
        const NorvesLib::Core::Container::String& GetProfileError() const
        {
            return m_ProfileError;
        }
        uint64_t GetConsumedJumpEventCount() const
        {
            return m_ConsumedJumpEvents;
        }

      private:
        NorvesLib::Modules::Physics::CharacterBodyComponent* Body() const;
        NorvesLib::Core::Entity* Resolve(uint64_t id) const;
        bool EnsureBinding();
        void Detach();
        void CollectInput(float dt);
        void Simulate(float dt);
        void PublishVisual(float dt);
        void ClearIntent();
        bool ValidateInput();
        NorvesLib::Core::Input::InputMapper* m_Input = nullptr;
        uint64_t m_BoundOwnerId = 0, m_BoundBodyId = 0, m_BindingGeneration = 0;
        NorvesLib::Core::Delegate<void, float> m_Before;
        NorvesLib::Core::Asset::TextAssetReloadTracker m_Profile;
        NorvesLib::Core::Container::String m_ProfileError;
        NorvesLib::Core::Locomotion::QuadrupedLocomotionParams m_Params;
        NorvesLib::Core::Locomotion::LocomotionState m_State;
        NorvesLib::Core::Locomotion::LocomotionIntent m_Intent;
        NorvesLib::Core::Locomotion::LocomotionOutput m_Output;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Input::InputButtonEvent> m_JumpEvents;
        uint64_t m_InputGeneration = 0, m_JumpGeneration = 0;
        uint64_t m_ConsumedJumpEvents = 0, m_VisualRootId = 0, m_VisualPoseId = 0, m_ViewId = 0;
        NorvesLib::Modules::Physics::CharacterDriveMode m_LastDrive =
            NorvesLib::Modules::Physics::CharacterDriveMode::Fixed;
        NorvesLib::Math::Quaternion m_VisualBaseRotation;
        bool m_bProfileReadFailed = false;
    };
} // namespace Game::Gameplay
