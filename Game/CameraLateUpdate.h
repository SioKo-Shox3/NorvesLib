#pragma once

#include "Component/CameraComponent.h"
#include "Component/SpringArmComponent.h"
#include "Delegate/Delegate.h"
#include "Object/World.h"
#include <utility>

namespace Game
{
    struct CameraLateUpdateBinding
    {
        NorvesLib::Core::Delegate<void, float> Callback;
    };

    // ゲームスレッド専用。当フレームのcamera確定を一回だけ予約する。
    // OnUpdateのResetで前フレーム残件を破棄し、OnLateUpdateでDispatchする。
    class CameraLateUpdateSlot
    {
    public:
        void Arm(const NorvesLib::Core::Container::TSharedPtr<CameraLateUpdateBinding>& binding) { m_Pending = binding; }
        void Reset() { m_Pending.reset(); }
        void Dispatch(float deltaTime)
        {
            auto binding = m_Pending.lock();
            m_Pending.reset();
            if (binding) binding->Callback.InvokeIfBound(deltaTime);
        }
    private:
        NorvesLib::Core::Container::TWeakPtr<CameraLateUpdateBinding> m_Pending;
    };

    // Dataが所有し、予約callbackはweak参照だけを持つ。Leaveでresetして無効化する。
    struct CameraLateUpdateState : CameraLateUpdateBinding
    {
        uint64_t OwnerId = 0;
        uint64_t SpringArmId = 0;
        uint64_t CameraId = 0;
        bool bSmokeSyncEmitted = false;
        bool bLensEffects = false, bLookLut = false;

        bool Resolve(NorvesLib::Core::World& world, NorvesLib::Core::Entity*& outOwner,
            NorvesLib::Core::Component::SpringArmComponent*& outArm,
            NorvesLib::Core::Component::CameraComponent*& outCamera) const
        {
            using namespace NorvesLib::Core;
            outOwner = nullptr; outArm = nullptr; outCamera = nullptr;
            Entity* owner = world.FindEntityByObjectId(OwnerId);
            if (!owner || !owner->IsTickEnabled()) return false;
            for (Entity* current = owner; current; current = current->GetParentEntity())
                if (!current->IsActive() || current->IsPendingDestroy()) return false;
            Component::SpringArmComponent* arm = nullptr;
            Component::CameraComponent* camera = nullptr;
            for (auto* inner : owner->GetInners())
            {
                auto* component = CastTo<Component::Component>(inner);
                if (!component || !component->IsActive() || component->IsPendingDestroy()) continue;
                if (component->GetComponentId() == SpringArmId) arm = CastTo<Component::SpringArmComponent>(component);
                if (component->GetComponentId() == CameraId) camera = CastTo<Component::CameraComponent>(component);
            }
            if (!arm || !camera || !arm->IsTickEnabled() || !camera->IsActiveCamera() || !arm->HasValidPivot()) return false;
            outOwner = owner; outArm = arm; outCamera = camera;
            return true;
        }

        bool BuildSnapshot(NorvesLib::Core::World& world, NorvesLib::Core::Rendering::CameraProxy& outProxy) const
        {
            NorvesLib::Core::Entity* owner = nullptr;
            NorvesLib::Core::Component::SpringArmComponent* arm = nullptr;
            NorvesLib::Core::Component::CameraComponent* camera = nullptr;
            if (!Resolve(world, owner, arm, camera)) return false;
            // Module Late後のchild pivotと、SpringArmが書くchild cameraの両方を確定する。
            world.UpdateWorldTransforms();
            arm->RefreshOwnerTransform();
            world.UpdateWorldTransforms();
            return camera->BuildCameraProxy(outProxy);
        }
    };
} // namespace Game
