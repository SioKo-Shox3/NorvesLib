// Physics モジュールのテストが PhysicsModule の非公開の状態を読み書きするための入口。
// PhysicsModule が friend として宣言している。テストごとに別の定義を置くと、同じ実行ファイルへ
// 束ねたときに定義が食い違う（ODR）ので、ここに1つだけ置く。
#pragma once

#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/SceneQuery.h"

#include <cassert>
#include <cstdint>

namespace NorvesLib::Modules::Physics
{
    class PhysicsModuleTestAccess
    {
    public:
        static StaticColliderPayloadHandler& GetStaticColliderPayloadHandler(IPhysicsModule& module)
        {
            return static_cast<PhysicsModule&>(module).m_StaticColliderPayloadHandler;
        }

        static EPhysicsResult UnregisterColliderForTest(IPhysicsModule& module, ColliderComponent& collider)
        {
            return GetConcrete(module).UnregisterCollider(collider);
        }

        static bool CopyPublishedProxy(const IPhysicsModule& module, Core::Scene::ColliderHandle handle,
            PhysicsShapeProxy& outProxy)
        {
            for (const auto& proxy : GetConcrete(module).m_PublishedBroadphase.GetProxies())
            {
                if (proxy.Collider == handle)
                {
                    outProxy = proxy;
                    return true;
                }
            }
            outProxy = {};
            return false;
        }

        static const PhysicsBroadphaseStats& GetWorkingBroadphaseStats(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_WorkingBroadphase.GetStats();
        }

        static const PhysicsBroadphaseStats& GetPublishedBroadphaseStats(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PublishedBroadphase.GetStats();
        }

        static const StaticAabbTree* GetWorkingStaticTree(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_WorkingBroadphase.GetStaticTree();
        }

        static const StaticAabbTree* GetPublishedStaticTree(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PublishedBroadphase.GetStaticTree();
        }

        static const Core::Container::VariableArray<PhysicsCandidatePair>& GetWorkingCandidatePairs(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_WorkingBroadphase.GetCandidatePairs();
        }

        static const Core::Container::VariableArray<PhysicsShapeProxy>& GetWorkingProxies(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_WorkingBroadphase.GetProxies();
        }

        static Core::Container::VariableArray<Core::Scene::ColliderHandle> GetDetachedColliderHandles(
            const IPhysicsModule& module, StaticColliderChunkKey key)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            Core::Container::VariableArray<Core::Scene::ColliderHandle> handles;
            const auto it = concrete.m_StaticColliderChunks.find(key.Value);
            if (it == concrete.m_StaticColliderChunks.end())
            {
                return handles;
            }
            handles.reserve(it->second.SlotIndices.size());
            for (uint32_t index : it->second.SlotIndices)
            {
                const PhysicsModule::ColliderSlot& slot = concrete.m_ColliderSlots[index];
                assert(slot.bOccupied && slot.bDetached && slot.ChunkKey == key.Value);
                handles.push_back({index, slot.Generation});
            }
            return handles;
        }

        static uint32_t GetDetachedColliderCount(const IPhysicsModule& module)
        {
            uint32_t count = 0;
            for (const PhysicsModule::ColliderSlot& slot : GetConcrete(module).m_ColliderSlots)
            {
                count += slot.bOccupied && slot.bDetached ? 1u : 0u;
            }
            return count;
        }

        static size_t GetPublishedProxyCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PublishedBroadphase.GetProxies().size();
        }

        static uint32_t GetColliderSlotCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_ColliderSlots.size());
        }

        static uint32_t GetBodySlotCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_BodySlots.size());
        }

        static uint32_t GetActiveColliderCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::ColliderSlot& slot : concrete.m_ColliderSlots)
            {
                if (slot.bOccupied && slot.bActive)
                {
                    ++count;
                }
            }
            return count;
        }

        static uint32_t GetRegisteredColliderCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::ColliderSlot& slot : concrete.m_ColliderSlots)
            {
                count += slot.bOccupied ? 1u : 0u;
            }
            return count;
        }

        static uint32_t GetRegisteredBodyCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            uint32_t count = 0;
            for (const PhysicsModule::BodySlot& slot : concrete.m_BodySlots)
            {
                count += slot.bOccupied ? 1u : 0u;
            }
            return count;
        }

        static bool IsBodyActive(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_BodySlots.size()
                && concrete.m_BodySlots[handle.Index].bOccupied
                && concrete.m_BodySlots[handle.Index].Generation == handle.Generation
                && concrete.m_BodySlots[handle.Index].bActive;
        }

        static bool IsColliderAlive(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_ColliderSlots.size()
                && concrete.m_ColliderSlots[handle.Index].bOccupied
                && concrete.m_ColliderSlots[handle.Index].Generation == handle.Generation;
        }

        static bool IsBodyAlive(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_BodySlots.size()
                && concrete.m_BodySlots[handle.Index].bOccupied
                && concrete.m_BodySlots[handle.Index].Generation == handle.Generation;
        }

        static uint32_t GetDuplicateDiagnosticCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DuplicateDiagnosticCount;
        }

        static EPhysicsDiagnostic GetLastDiagnostic(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_LastDiagnostic;
        }

        static uint32_t GetCallbackCount(const IPhysicsModule& module, const ColliderComponent& component)
        {
            return GetConcrete(module).GetCallbackCount(component);
        }

        static EPhysicsResult GetLastRegistrationResult(
            const IPhysicsModule& module,
            const ColliderComponent& component)
        {
            return GetConcrete(module).GetColliderRegistrationResult(component);
        }

        static void PrepareColliderGenerationWrap(IPhysicsModule& module, ColliderComponent& component)
        {
            GetConcrete(module).PrepareColliderGenerationWrap(component);
        }

        static void PrepareBodyGenerationWrap(IPhysicsModule& module, RigidBodyComponent& component)
        {
            GetConcrete(module).PrepareBodyGenerationWrap(component);
        }

        static void PrepareOverlapBeginGenerationWrap(
            IPhysicsModule& module,
            ColliderComponent& component,
            PhysicsCallbackHandle& handle)
        {
            GetConcrete(module).PrepareOverlapBeginGenerationWrap(component, handle);
        }

        static float GetColliderRadius(const IPhysicsModule& module, const ColliderComponent& component)
        {
            return GetConcrete(module).GetColliderRadius(component);
        }

        static uint32_t GetDispatchedEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DispatchedEventCount;
        }

        static uint32_t GetPendingEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PendingEventCount;
        }

        static uint32_t GetLifecycleStateCount(const IPhysicsModule& module)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return (concrete.m_bBound ? 1u : 0u)
                + (concrete.m_bInitialized ? 1u : 0u)
                + (concrete.m_bHasPublishedSnapshot ? 1u : 0u);
        }

        static Math::Vector3 GetPendingImpulse(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            assert(handle.IsValid() && handle.Index < concrete.m_BodySlots.size());
            return concrete.m_BodySlots[handle.Index].PendingImpulse;
        }

        static uint32_t GetPreviousPairCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_PreviousTriggerPairs.size());
        }

        static bool IsColliderActive(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            return handle.IsValid() && handle.Index < concrete.m_ColliderSlots.size()
                && concrete.m_ColliderSlots[handle.Index].bOccupied
                && concrete.m_ColliderSlots[handle.Index].Generation == handle.Generation
                && concrete.m_ColliderSlots[handle.Index].bActive;
        }

        static Math::Vector3 GetPreStepPosition(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            return GetConcrete(module).m_BodySlots[handle.Index].PreStepPosition;
        }

        static bool HasPreStepSnapshot(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            return GetConcrete(module).m_BodySlots[handle.Index].bHadPreStepSnapshot;
        }

        static uint32_t GetColliderGeneration(const IPhysicsModule& module, Core::Scene::ColliderHandle handle)
        {
            return GetConcrete(module).m_ColliderSlots[handle.Index].Generation;
        }

        static uint32_t GetBodyGeneration(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            return GetConcrete(module).m_BodySlots[handle.Index].Generation;
        }

        static uint32_t GetCurrentPairCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_CurrentTriggerPairs.size());
        }

        static bool HasPublishedSnapshot(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_bHasPublishedSnapshot;
        }

        static bool ShutdownAndInitialize(IPhysicsModule& module)
        {
            PhysicsModule& concrete = GetConcrete(module);
            concrete.Shutdown();
            return concrete.Initialize();
        }

        static Core::Scene::EPhysicsSceneQueryResult RefreshDynamicSnapshot(IPhysicsModule& module)
        {
            return GetConcrete(module).RefreshDynamicSnapshot();
        }

    private:
        static const PhysicsModule& GetConcrete(const IPhysicsModule& module)
        {
            const auto* concrete = dynamic_cast<const PhysicsModule*>(&module);
            assert(concrete != nullptr);
            return *concrete;
        }

        static PhysicsModule& GetConcrete(IPhysicsModule& module)
        {
            auto* concrete = dynamic_cast<PhysicsModule*>(&module);
            assert(concrete != nullptr);
            return *concrete;
        }
    };
} // namespace NorvesLib::Modules::Physics
