#include "Object/World.h"
#include "Component/AnimatorComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/BoardComponent.h"
#include "Component/CameraComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/HoldSlotComponent.h"
#include "Component/ImpostorComponent.h"
#include "Component/LightComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SocketAttachmentComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/TextComponent.h"
#include "Container/UnorderedSet.h"
#include "Debug/Stats.h"
#include "Ecs/EcsRenderTypes.h"
#include "Ecs/EcsScheduler.h"
#include "Ecs/EcsTransformSystem.h"
#include "Ecs/EcsWorld.h"
#include "Engine/ComponentDataRegistry.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/PrefabAsset.h"
#include "Rendering/CanvasView.h"
#include "Rendering/IBoardProxySink.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneProxyHash.h"
#include "Rendering/SceneView.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Core
{
    namespace
    {
        constexpr uint32_t MaxPrefabNestedDepth = 16;
        /// Entity::m_EcsCreateSlot の「予約なし」の値
        constexpr uint32_t kNoEntityCreateSlot = 0xFFFFFFFFu;

        /// 逆引きの集合に入れるため、行を 1 つの値に詰める
        constexpr uint64_t PackEcsEntity(Ecs::EcsEntity entity)
        {
            return (static_cast<uint64_t>(entity.Generation) << 32) | entity.Index;
        }

        constexpr Ecs::EcsEntity UnpackEcsEntity(uint64_t packed)
        {
            return Ecs::EcsEntity{static_cast<uint32_t>(packed & 0xFFFFFFFFull), static_cast<uint32_t>(packed >> 32)};
        }

        /// 2 つの行列が bit まで同じか
        bool SameMatrixBits(const Math::Matrix4x4& lhs, const Math::Matrix4x4& rhs)
        {
            return std::memcmp(&lhs, &rhs, sizeof(Math::Matrix4x4)) == 0;
        }

        /// 検査 MEGA_SLOTS が有効か
        bool IsGpuSceneSlotsCheckActive()
        {
            NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
                ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
                : nullptr;
            return checks != nullptr && checks->IsEnabled("MEGA_SLOTS");
        }

        /// 球の境界を包む AABB
        void SphereToBox(const Rendering::BoundingSphere& sphere, Math::Vector3& outMin, Math::Vector3& outMax)
        {
            outMin = Math::Vector3(sphere.CenterX - sphere.Radius, sphere.CenterY - sphere.Radius, sphere.CenterZ - sphere.Radius);
            outMax = Math::Vector3(sphere.CenterX + sphere.Radius, sphere.CenterY + sphere.Radius, sphere.CenterZ + sphere.Radius);
        }

        /// MegaGeometry の proxy を公開したとき、行の席の範囲の AABB を proxy の境界を包むところまで広げる
        void NoteMegaSlotBounds(Ecs::EcsWorld& ecs, Ecs::EcsEntity row, const Rendering::MegaGeometryProxy& proxy)
        {
            const Ecs::EcsGpuSceneSlot* slot = ecs.GetColumnValue<Ecs::EcsGpuSceneSlot>(row);
            if (slot == nullptr)
            {
                return;
            }
            Math::Vector3 boundsMin;
            Math::Vector3 boundsMax;
            SphereToBox(proxy.WorldBounds, boundsMin, boundsMax);
            ecs.GetGpuSceneSlots().NoteBounds(*slot, boundsMin, boundsMax);
        }

        /// MegaGeometry の proxy を SceneView へ公開する。行の席を proxy に書き、席の範囲の AABB を広げる。
        /// 差分の同期では前の変換の履歴をつけて公開し、全量の同期（--mega-scene-full-sync）では前の変換をパケットが付け直す
        void PublishMegaProxyToView(Rendering::SceneView& view, Ecs::EcsWorld* ecs, Ecs::EcsEntity row,
                                    Rendering::MegaGeometryProxy proxy)
        {
            const bool bHasRow = ecs != nullptr && !row.IsNull() && ecs->IsAlive(row);
            if (bHasRow)
            {
                if (const Ecs::EcsGpuSceneSlot* slot = ecs->GetColumnValue<Ecs::EcsGpuSceneSlot>(row))
                {
                    proxy.SceneSlot = slot->Value;
                }
            }
            proxy.PreviousWorldTransform = proxy.WorldTransform;
            if (view.IsMegaFullSync())
            {
                view.UpdateMegaGeometryProxy(proxy);
            }
            else
            {
                view.PublishMegaGeometryProxy(proxy);
            }
            if (bHasRow)
            {
                NoteMegaSlotBounds(*ecs, row, proxy);
            }
        }

        /// 検査 MEGA_SLOTS の本体: 席の表と全行の突き合わせに加え、SceneView が持つ MegaGeometry の proxy の境界が
        /// 行の席の範囲の AABB に包まれることを確かめる
        void RunGpuSceneSlotsCheck(Ecs::EcsWorld& ecs, const Rendering::SceneView* sceneView,
                                   NorvesLib::Debug::EngineChecks& checks, uint64_t calls)
        {
            Ecs::EcsGpuSceneSlotAudit audit = ecs.AuditGpuSceneSlots();
            uint32_t proxyMisses = 0;
            uint32_t proxiesExamined = 0;
            if (sceneView != nullptr)
            {
                Container::UnorderedMap<uint64_t, const Rendering::MegaGeometryProxy*> proxyByObject;
                proxyByObject.reserve(sceneView->GetMegaGeometryProxies().size());
                for (const Rendering::MegaGeometryProxy& proxy : sceneView->GetMegaGeometryProxies())
                {
                    proxyByObject[proxy.ObjectId] = &proxy;
                }
                const TypeId slotType = Ecs::EcsColumnInfoOf<Ecs::EcsGpuSceneSlot>().Type;
                const TypeId refType = Ecs::EcsColumnInfoOf<Ecs::EcsObjectRef>().Type;
                for (Ecs::EcsArchetypeId archetype = 0; archetype < ecs.GetArchetypeCount(); ++archetype)
                {
                    const int32_t slotColumn = ecs.FindColumnIndex(archetype, slotType);
                    const int32_t refColumn = ecs.FindColumnIndex(archetype, refType);
                    if (slotColumn < 0 || refColumn < 0)
                    {
                        continue;
                    }
                    for (const Ecs::EcsChunk* chunk : ecs.GetArchetypeChunks(archetype))
                    {
                        const Ecs::EcsGpuSceneSlot* slots =
                            static_cast<const Ecs::EcsGpuSceneSlot*>(ecs.GetChunkColumn(chunk, static_cast<uint32_t>(slotColumn)));
                        const Ecs::EcsObjectRef* refs =
                            static_cast<const Ecs::EcsObjectRef*>(ecs.GetChunkColumn(chunk, static_cast<uint32_t>(refColumn)));
                        for (uint32_t row = 0; row < chunk->Count; ++row)
                        {
                            const Component::Component* component = static_cast<const Component::Component*>(refs[row].Object);
                            const Entity* owner = component != nullptr ? component->GetOwner() : nullptr;
                            if (owner == nullptr || !slots[row].IsAssigned())
                            {
                                continue;
                            }
                            const auto found = proxyByObject.find(owner->GetObjectId());
                            if (found == proxyByObject.end())
                            {
                                continue;
                            }
                            ++proxiesExamined;
                            Math::Vector3 boundsMin;
                            Math::Vector3 boundsMax;
                            SphereToBox(found->second->WorldBounds, boundsMin, boundsMax);
                            Math::Vector3 rangeMin;
                            Math::Vector3 rangeMax;
                            const bool bContained =
                                ecs.GetGpuSceneSlots().GetRangeBounds(slots[row].Range, rangeMin, rangeMax) &&
                                rangeMin.x <= boundsMin.x && rangeMin.y <= boundsMin.y && rangeMin.z <= boundsMin.z &&
                                rangeMax.x >= boundsMax.x && rangeMax.y >= boundsMax.y && rangeMax.z >= boundsMax.z;
                            if (!bContained)
                            {
                                ++proxyMisses;
                                audit.Note("公開した proxy の境界が、行の席の範囲の AABB に包まれない");
                            }
                        }
                    }
                }
            }
            const uint32_t violations = audit.Violations() + proxyMisses;
            const bool bPass = violations == 0;
            if (!bPass && checks.GetFailCount() <= 8)
            {
                LOG_ERROR("MEGA_SLOTS: 席の規則が破れた (未割り当て=%u, 重複=%u, 表と不一致=%u, 解放漏れ=%u, 包含の外れ=%u, proxy の外れ=%u, 早すぎる再利用=%llu, 不正な解放=%llu, 規則違反=%llu): %s",
                          audit.UnassignedRows, audit.DuplicateSlots, audit.UnknownSlots, audit.OrphanLiveSlots,
                          audit.ContainmentMisses, proxyMisses, static_cast<unsigned long long>(audit.PrematureReuse),
                          static_cast<unsigned long long>(audit.InvalidReleases),
                          static_cast<unsigned long long>(audit.RuleViolations), audit.FirstDetail);
            }
            if (calls % 128 == 0)
            {
                const Ecs::EcsGpuSceneSlotTable& table = ecs.GetGpuSceneSlots();
                LOG_INFO("MEGA_SLOTS rows=%u slots=%u ranges=%u live=%u pending=%zu ready=%zu allocated=%llu released=%llu reused=%llu proxies=%u",
                         audit.Rows, table.GetSlotCount(), table.GetRangeCount(), table.GetLiveCount(), table.GetPendingCount(),
                         table.GetReadyCount(), static_cast<unsigned long long>(table.GetAllocatedCount()),
                         static_cast<unsigned long long>(table.GetReleasedCount()),
                         static_cast<unsigned long long>(table.GetReusedCount()), proxiesExamined);
            }
            // 行が無い間は照合するものが無いので pass は出さない（fail は常に出す）
            if (!bPass || audit.Rows > 0)
            {
                checks.Report("MEGA_SLOTS", bPass, audit.Rows + proxiesExamined);
            }
        }

        /// 描画側の進み具合を席の表へ渡す。検査 MEGA_SLOTS が有効なら、一定の間隔で表と全行を突き合わせる
        void AdvanceGpuSceneSlots(Ecs::EcsWorld& ecs, const Rendering::SceneView* sceneView)
        {
            Ecs::EcsGpuSceneSlotTable& table = ecs.GetGpuSceneSlots();
            table.Advance(sceneView != nullptr ? sceneView->GetSlotGate() : Ecs::EcsSlotGate{});
            if (NorvesLib::Core::Engine::GEngine == nullptr)
            {
                return;
            }
            NorvesLib::Debug::EngineChecks& checks = NorvesLib::Core::Engine::GEngine->GetEngineChecks();
            if (!checks.IsEnabled("MEGA_SLOTS"))
            {
                return;
            }
            // World を作ったあとで検査が有効になった場合も履歴を持ち始める
            table.SetAuditEnabled(true);
            const uint64_t calls = table.GetAdvanceCount();
            if (calls % 32 == 0)
            {
                RunGpuSceneSlotsCheck(ecs, sceneView, checks, calls);
            }
        }

        class ScopedWorldFlag
        {
        public:
            explicit ScopedWorldFlag(bool& flag) : m_Flag(flag), m_Previous(flag)
            {
                flag = true;
            }
            ~ScopedWorldFlag()
            {
                m_Flag = m_Previous;
            }
        private:
            bool& m_Flag;
            bool m_Previous;
        };

        /// 行の階層を読んで fn で書き換え、書き戻す（変更として記録される）。生きていない行は何もしない
        template <typename Fn>
        void EditHierarchy(Ecs::EcsWorld& ecs, Ecs::EcsEntity row, Fn&& fn)
        {
            if (const Ecs::EcsHierarchy* current = ecs.GetColumnValue<Ecs::EcsHierarchy>(row))
            {
                Ecs::EcsHierarchy next = *current;
                fn(next);
                ecs.WriteColumnValueOf(row, next);
            }
        }

        /// 検査 ECS_TRANSFORM の再帰: 階層のシステムが求めた EcsWorldTransform が、根から局所の変換をたどって求め直した値
        /// （親の列の値 × 局所の変換）と、祖先をたどる従来の Active 判定に一致するか。
        /// 行の無い Entity（段の最中に予約中）は ECS_BIND の担当なので飛ばす
        bool VerifyEntityTransforms(Ecs::EcsWorld& ecs, Entity& entity, const Ecs::EcsWorldTransform& parentWorld,
                                    bool parentActive, uint32_t& count, const char*& error)
        {
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (row.IsNull() || !ecs.IsAlive(row))
            {
                return true;
            }
            const Ecs::EcsPropagatedTransform* propagated = ecs.GetColumnValue<Ecs::EcsPropagatedTransform>(row);
            const Ecs::EcsWorldTransform* stored = ecs.GetColumnValue<Ecs::EcsWorldTransform>(row);
            if (!propagated || !stored)
            {
                error = "行に EcsPropagatedTransform か EcsWorldTransform が無い";
                return false;
            }
            ++count;
            if (propagated->bValid == 0)
            {
                error = "階層のシステムが求めていない行がある";
                return false;
            }
            const Math::Transform expected =
                Ecs::EcsComposeTransform(Math::Transform(parentWorld.position, parentWorld.rotation, parentWorld.scale),
                                         entity.GetLocalTransform());
            if (!Ecs::EcsEqualWorldTransform(*stored,
                                             Ecs::EcsWorldTransform{expected.position, expected.rotation, expected.scale}))
            {
                error = "階層のシステムのワールドの変換が根から求め直した値と一致しない";
                return false;
            }
            const bool bActive = parentActive && entity.IsActive() && !entity.IsPendingDestroy();
            if ((propagated->bEffectiveActive != 0) != bActive)
            {
                error = "階層のシステムの EffectiveActive が祖先をたどる判定と一致しない";
                return false;
            }
            for (Entity* child : entity.GetChildEntities())
            {
                if (!VerifyEntityTransforms(ecs, *child, *stored, bActive, count, error))
                {
                    return false;
                }
            }
            return true;
        }

        /// 検査 ECS_BIND の再帰: entity の行と、その部分木が Outer/Inner と一致するか。食い違いは error に理由を入れて false
        bool VerifyEntityRows(Ecs::EcsWorld& ecs, Entity& entity, Ecs::EcsEntity parentRow, uint32_t depth,
                              uint32_t& count, uint32_t& componentCount, const char*& error)
        {
            ++count;
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (row.IsNull() || !ecs.IsAlive(row))
            {
                error = "Entity の行が無い（または死んでいる）";
                return false;
            }
            if (ecs.IsOrphan(row))
            {
                error = "生きた Entity の行が孤児になっている";
                return false;
            }
            const Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row);
            if (!ref || ref->Object != static_cast<void*>(&entity) || ref->Generation != row.Generation)
            {
                error = "行の EcsObjectRef が Entity を指していない";
                return false;
            }
            const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(row);
            if (!hierarchy)
            {
                error = "行に EcsHierarchy が無い";
                return false;
            }
            if (hierarchy->Parent != parentRow || hierarchy->Depth != depth ||
                hierarchy->Kind != Ecs::kEcsHierarchyKindEntity)
            {
                error = "EcsHierarchy の Parent・Depth・Kind が Outer と一致しない";
                return false;
            }
            if (parentRow.IsNull() && (!hierarchy->PrevSibling.IsNull() || !hierarchy->NextSibling.IsNull()))
            {
                error = "根の行が兄弟を持っている";
                return false;
            }
            const Ecs::EcsEntity firstChild = hierarchy->FirstChild;
            const Ecs::EcsEntity lastChild = hierarchy->LastChild;
            const Container::VariableArray<Entity*> children = entity.GetChildEntities();
            Ecs::EcsEntity expected = firstChild;
            Ecs::EcsEntity previous;
            for (Entity* child : children)
            {
                const Ecs::EcsEntity childRow = child->GetEcsEntity();
                const Ecs::EcsHierarchy* childHierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(childRow);
                if (expected != childRow || !childHierarchy || childHierarchy->PrevSibling != previous)
                {
                    error = "EcsHierarchy の子の並びが Inner の並びと一致しない";
                    return false;
                }
                previous = childRow;
                expected = childHierarchy->NextSibling;
            }
            if (!expected.IsNull() || lastChild != previous)
            {
                error = "EcsHierarchy の子の連結の端が一致しない";
                return false;
            }
            // Component の行: 所有者の子の葉で、所有者の Component の連結に Inner の並びで入っている
            Ecs::EcsEntity expectedComponent = hierarchy->FirstComponent;
            Ecs::EcsEntity previousComponent;
            for (Component::Component* component : entity.GetComponents())
            {
                ++componentCount;
                const Ecs::EcsEntity componentRow = component->GetEcsEntity();
                if (componentRow.IsNull() || !ecs.IsAlive(componentRow))
                {
                    error = "Component の行が無い（または死んでいる）";
                    return false;
                }
                if (ecs.IsOrphan(componentRow))
                {
                    error = "生きた Component の行が孤児になっている";
                    return false;
                }
                const Ecs::EcsObjectRef* componentRef = ecs.GetColumnValue<Ecs::EcsObjectRef>(componentRow);
                if (!componentRef || componentRef->Object != static_cast<void*>(component) ||
                    componentRef->Generation != componentRow.Generation)
                {
                    error = "行の EcsObjectRef が Component を指していない";
                    return false;
                }
                const Ecs::EcsHierarchy* componentHierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(componentRow);
                if (!componentHierarchy || ecs.GetColumnValue<Ecs::EcsComponentFlags>(componentRow) == nullptr ||
                    ecs.GetColumnValue<Ecs::EcsTickConfig>(componentRow) == nullptr)
                {
                    error = "Component の行に EcsHierarchy・EcsComponentFlags・EcsTickConfig が無い";
                    return false;
                }
                {
                    const Ecs::EcsComponentRowKind rowKind = component->GetEcsRowKind();
                    const bool bHasMesh = ecs.GetColumnValue<Ecs::EcsMeshRender>(componentRow) != nullptr;
                    const bool bHasMega = ecs.GetColumnValue<Ecs::EcsMegaMesh>(componentRow) != nullptr;
                    const bool bHasSkinned = ecs.GetColumnValue<Ecs::EcsSkinnedRender>(componentRow) != nullptr;
                    const bool bHasLight = ecs.GetColumnValue<Ecs::EcsLightRender>(componentRow) != nullptr;
                    const bool bHasFalloff = ecs.GetColumnValue<Ecs::EcsLightFalloff>(componentRow) != nullptr;
                    const bool bHasSpotCone = ecs.GetColumnValue<Ecs::EcsSpotCone>(componentRow) != nullptr;
                    const bool bHasBoard = ecs.GetColumnValue<Ecs::EcsBoardRender>(componentRow) != nullptr;
                    const bool bHasBillboardSize = ecs.GetColumnValue<Ecs::EcsBillboardSize>(componentRow) != nullptr;
                    const bool bHasPublish = ecs.GetColumnValue<Ecs::EcsPublishState>(componentRow) != nullptr;
                    const bool bHasPose = ecs.GetColumnValue<Ecs::EcsPoseVersion>(componentRow) != nullptr;
                    const bool bExpectBoard = rowKind == Ecs::EcsComponentRowKind::Board || rowKind == Ecs::EcsComponentRowKind::Billboard;
                    const bool bExpectMesh = rowKind == Ecs::EcsComponentRowKind::Mesh || rowKind == Ecs::EcsComponentRowKind::MegaGeometry;
                    const bool bExpectLight = rowKind == Ecs::EcsComponentRowKind::Light || rowKind == Ecs::EcsComponentRowKind::PointLight ||
                                              rowKind == Ecs::EcsComponentRowKind::SpotLight;
                    const bool bExpectFalloff = rowKind == Ecs::EcsComponentRowKind::PointLight || rowKind == Ecs::EcsComponentRowKind::SpotLight;
                    // 公開の状態は描画へ出す種類（Basic 以外）の行だけが持ち、ポーズの版は SkinnedMesh の行だけが持つ
                    if (bHasPublish != (rowKind != Ecs::EcsComponentRowKind::Basic) ||
                        bHasPose != (rowKind == Ecs::EcsComponentRowKind::SkinnedMesh))
                    {
                        error = "Component の行の公開の状態・ポーズの版の列が、Component の種類と一致しない";
                        return false;
                    }
                    if (bHasMesh != bExpectMesh || bHasMega != (rowKind == Ecs::EcsComponentRowKind::MegaGeometry) ||
                        bHasSkinned != (rowKind == Ecs::EcsComponentRowKind::SkinnedMesh) || bHasLight != bExpectLight ||
                        bHasFalloff != bExpectFalloff || bHasSpotCone != (rowKind == Ecs::EcsComponentRowKind::SpotLight) ||
                        bHasBoard != bExpectBoard ||
                        bHasBillboardSize != (rowKind == Ecs::EcsComponentRowKind::Billboard))
                    {
                        error = "Component の行の描画の値の列が、Component の種類と一致しない";
                        return false;
                    }
                }
                if (componentHierarchy->Kind != Ecs::kEcsHierarchyKindComponent || componentHierarchy->Parent != row ||
                    componentHierarchy->Depth != depth + 1 || !componentHierarchy->FirstChild.IsNull() ||
                    !componentHierarchy->LastChild.IsNull())
                {
                    error = "Component の EcsHierarchy が所有者の子の葉になっていない";
                    return false;
                }
                if (expectedComponent != componentRow || componentHierarchy->PrevSibling != previousComponent)
                {
                    error = "EcsHierarchy の Component の並びが Inner の並びと一致しない";
                    return false;
                }
                previousComponent = componentRow;
                expectedComponent = componentHierarchy->NextSibling;
            }
            if (!expectedComponent.IsNull() || hierarchy->LastComponent != previousComponent)
            {
                error = "EcsHierarchy の Component の連結の端が一致しない";
                return false;
            }
            for (Entity* child : children)
            {
                if (!VerifyEntityRows(ecs, *child, row, depth + 1, count, componentCount, error))
                {
                    return false;
                }
            }
            return true;
        }

        /// 順序を無視して、2 つのポインタの集合が一致するか（検査 WORLD_CLEANUP の照合用）
        template <typename T>
        bool SamePointerSet(const Container::VariableArray<T*>& lhs, const Container::VariableArray<T*>& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            Container::VariableArray<T*> sortedLhs(lhs);
            Container::VariableArray<T*> sortedRhs(rhs);
            std::sort(sortedLhs.begin(), sortedLhs.end());
            std::sort(sortedRhs.begin(), sortedRhs.end());
            return sortedLhs == sortedRhs;
        }

        struct PrefabLiveObject
        {
            Object* ObjectPtr = nullptr;
            Entity* EntityPtr = nullptr;
            Component::Component* ComponentPtr = nullptr;
            ObjectHandle Handle;
        };

        struct PrefabBuiltObject
        {
            PrefabTargetPath Target;
            PrefabLiveObject LiveObject;
        };

        struct PrefabBuildState
        {
            Container::VariableArray<PrefabBuiltObject> Objects;
        };

        using PrefabAliasMap = Container::UnorderedMap<SubtreeSnapshotAliasId, PrefabLiveObject>;

        Entity* FindEntityByObjectIdRecursive(Entity* entity, uint64_t id)
        {
            if (!entity || entity->IsPendingDestroy())
            {
                return nullptr;
            }

            if (entity->GetObjectId() == id)
            {
                return entity;
            }

            for (Entity* child : entity->GetChildEntities())
            {
                if (Entity* found = FindEntityByObjectIdRecursive(child, id))
                {
                    return found;
                }
            }

            return nullptr;
        }

        template <typename T>
        class TRawObjectGuard
        {
        public:
            explicit TRawObjectGuard(T* object)
                : m_Object(object)
            {
            }

            ~TRawObjectGuard()
            {
                if (m_Object)
                {
                    if (m_Object->HasFlag(OF_Initialized))
                    {
                        m_Object->Finalize();
                    }
                    delete m_Object;
                }
            }

            T* Get() const
            {
                return m_Object;
            }

            T* Release()
            {
                T* object = m_Object;
                m_Object = nullptr;
                return object;
            }

        private:
            T* m_Object = nullptr;
        };

        StableClassId MakePrefabStableClassId(const IClass& cls)
        {
            return MakeStableSchemaId("NorvesLib", "Class", cls.GetClassName().GetView());
        }

        StablePropertyId MakePrefabStablePropertyId(const IClass& cls, const ClassProperty& property)
        {
            return MakeStableSchemaId(
                "NorvesLib",
                "Property",
                cls.GetClassName().GetView(),
                property.GetName().GetView());
        }

        const IClass* ResolvePrefabClass(StableClassId stableClassId)
        {
            Container::VariableArray<const IClass*> classes = ClassRegistry::Get().GetAllClasses();
            for (const IClass* cls : classes)
            {
                if (cls && MakePrefabStableClassId(*cls) == stableClassId)
                {
                    return cls;
                }
            }
            return nullptr;
        }

        const ClassProperty* FindPrefabProperty(const IClass& cls, StablePropertyId propertyId)
        {
            Container::VariableArray<const ClassProperty*> properties = cls.GetAllProperties();
            for (const ClassProperty* property : properties)
            {
                if (property && MakePrefabStablePropertyId(cls, *property) == propertyId)
                {
                    return property;
                }
            }
            return nullptr;
        }

        bool ContainsPrefabInStack(
            const Container::VariableArray<const PrefabAsset*>& stack,
            const PrefabAsset& prefab)
        {
            for (const PrefabAsset* existing : stack)
            {
                if (existing == &prefab)
                {
                    return true;
                }
            }
            return false;
        }

        bool ContainsEntityAlias(const EntitySubtreeSnapshotNode& node, SubtreeSnapshotAliasId alias)
        {
            if (node.Alias == alias)
            {
                return true;
            }

            for (const EntitySubtreeSnapshotNode& child : node.Children)
            {
                if (ContainsEntityAlias(child, alias))
                {
                    return true;
                }
            }
            return false;
        }

        bool ValidatePrefabGraph(
            const PrefabAsset& prefab,
            Container::VariableArray<const PrefabAsset*>& stack,
            uint32_t depth)
        {
            if (!prefab.HasTree() ||
                depth > MaxPrefabNestedDepth ||
                ContainsPrefabInStack(stack, prefab))
            {
                return false;
            }

            stack.push_back(&prefab);

            for (const PrefabNestedPrefabInstance& nested : prefab.GetNestedPrefabs())
            {
                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (nested.InstanceId == InvalidPrefabNestedInstanceId ||
                    nested.ParentAlias == InvalidSubtreeSnapshotAliasId ||
                    !nestedPrefab ||
                    !nestedPrefab->HasTree() ||
                    !ContainsEntityAlias(prefab.GetTree().Root, nested.ParentAlias) ||
                    !ValidatePrefabGraph(*nestedPrefab, stack, depth + 1))
                {
                    stack.pop_back();
                    return false;
                }
            }

            stack.pop_back();
            return true;
        }

        bool PathsEqual(
            const Container::VariableArray<PrefabNestedInstanceId>& lhs,
            const Container::VariableArray<PrefabNestedInstanceId>& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.size(); ++index)
            {
                if (lhs[index] != rhs[index])
                {
                    return false;
                }
            }
            return true;
        }

        PrefabTargetPath MakeAbsoluteTarget(
            const Container::VariableArray<PrefabNestedInstanceId>& basePath,
            const PrefabTargetPath& localTarget)
        {
            PrefabTargetPath result;
            result.NestedInstances = basePath;
            for (PrefabNestedInstanceId nestedInstance : localTarget.NestedInstances)
            {
                result.NestedInstances.push_back(nestedInstance);
            }
            result.Alias = localTarget.Alias;
            return result;
        }

        PrefabLiveObject* FindBuiltObject(PrefabBuildState& state, const PrefabTargetPath& target)
        {
            for (PrefabBuiltObject& object : state.Objects)
            {
                if (object.Target.Alias == target.Alias &&
                    PathsEqual(object.Target.NestedInstances, target.NestedInstances))
                {
                    return &object.LiveObject;
                }
            }
            return nullptr;
        }

        bool RegisterLiveObject(
            SubtreeSnapshotAliasId alias,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabLiveObject& liveObject,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            if (alias == InvalidSubtreeSnapshotAliasId ||
                !liveObject.ObjectPtr ||
                aliases.find(alias) != aliases.end())
            {
                return false;
            }

            aliases[alias] = liveObject;

            PrefabBuiltObject builtObject;
            builtObject.Target.NestedInstances = path;
            builtObject.Target.Alias = alias;
            builtObject.LiveObject = liveObject;
            state.Objects.push_back(builtObject);
            return true;
        }

        bool ApplyPrefabValue(Object& object, const ProjectedPropertyValue& projectedValue)
        {
            const IClass* cls = object.GetClass();
            if (!cls)
            {
                return false;
            }

            const ClassProperty* property = FindPrefabProperty(*cls, projectedValue.Property);
            if (!property)
            {
                return false;
            }

            if (ShouldSkipPrefabRestoreProperty(*cls, *property))
            {
                return true;
            }

            const TypeId runtimeType = property->GetRuntimeTypeId();
            const TypeInfo* runtimeTypeInfo = TypeRegistry::Get().Find(runtimeType);
            if (!runtimeTypeInfo || runtimeTypeInfo->StableId != projectedValue.Type)
            {
                return false;
            }

            PropertyValue value;
            if (!value.DeserializeStable(projectedValue.Type, projectedValue.SerializedValue))
            {
                return false;
            }

            if (Entity* entity = CastTo<Entity>(&object))
            {
                const Identity& propertyName = property->GetName();
                if (propertyName == Identity("Position"))
                {
                    const Math::Vector3* position = value.Get<Math::Vector3>();
                    if (!position)
                    {
                        return false;
                    }
                    entity->SetLocalPosition(*position);
                    return true;
                }

                if (propertyName == Identity("Rotation"))
                {
                    const Math::Quaternion* rotation = value.Get<Math::Quaternion>();
                    if (!rotation)
                    {
                        return false;
                    }
                    entity->SetLocalRotation(*rotation);
                    return true;
                }

                if (propertyName == Identity("Scale"))
                {
                    const Math::Vector3* scale = value.Get<Math::Vector3>();
                    if (!scale)
                    {
                        return false;
                    }
                    entity->SetLocalScale(*scale);
                    return true;
                }
            }

            return property->ApplyValue(&object, value);
        }

        bool ApplyPrefabSnapshot(Object& object, const ObjectSnapshot& snapshot)
        {
            for (const ProjectedPropertyValue& value : snapshot.Properties)
            {
                if (!ApplyPrefabValue(object, value))
                {
                    return false;
                }
            }
            return true;
        }

        bool ApplyPrefabOverrideSet(
            const PrefabOverrideSet& overrides,
            const Container::VariableArray<PrefabNestedInstanceId>& basePath,
            PrefabBuildState& state)
        {
            for (const PrefabPropertyOverride& overrideValue : overrides.Properties)
            {
                PrefabTargetPath target = MakeAbsoluteTarget(basePath, overrideValue.Target);
                if (target.Alias == InvalidSubtreeSnapshotAliasId)
                {
                    return false;
                }

                PrefabLiveObject* liveObject = FindBuiltObject(state, target);
                if (!liveObject || !liveObject->ObjectPtr)
                {
                    return false;
                }

                if (!ApplyPrefabValue(*liveObject->ObjectPtr, overrideValue.Value))
                {
                    return false;
                }
            }

            return true;
        }

        Entity* BuildPrefabInstance(
            const PrefabAsset& prefab,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabOverrideSet* instanceOverrides,
            PrefabBuildState& state);

        Component::Component* BuildPrefabComponent(
            const ComponentSubtreeSnapshot& snapshot,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            const IClass* cls = ResolvePrefabClass(snapshot.Object.Class);
            if (!cls || !cls->IsChildOf(Component::Component::StaticClass()))
            {
                return nullptr;
            }

            IUnknown* instance = cls->NewInstance(nullptr);
            Component::Component* component = CastTo<Component::Component>(instance);
            if (!component)
            {
                delete instance;
                return nullptr;
            }

            TRawObjectGuard<Component::Component> componentGuard(component);
            if (!ApplyPrefabSnapshot(*component, snapshot.Object))
            {
                return nullptr;
            }

            PrefabLiveObject liveObject;
            liveObject.ObjectPtr = component;
            liveObject.ComponentPtr = component;
            if (!RegisterLiveObject(snapshot.Alias, path, liveObject, aliases, state))
            {
                return nullptr;
            }

            return componentGuard.Release();
        }

        Entity* BuildPrefabEntityNode(
            const EntitySubtreeSnapshotNode& node,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            PrefabAliasMap& aliases,
            PrefabBuildState& state)
        {
            const IClass* cls = ResolvePrefabClass(node.Object.Class);
            if (!cls || !cls->IsChildOf(Entity::StaticClass()))
            {
                return nullptr;
            }

            IUnknown* instance = cls->NewInstance(nullptr);
            Entity* entity = CastTo<Entity>(instance);
            if (!entity)
            {
                delete instance;
                return nullptr;
            }

            TRawObjectGuard<Entity> entityGuard(entity);
            if (!ApplyPrefabSnapshot(*entity, node.Object))
            {
                return nullptr;
            }

            PrefabLiveObject liveObject;
            liveObject.ObjectPtr = entity;
            liveObject.EntityPtr = entity;
            if (!RegisterLiveObject(node.Alias, path, liveObject, aliases, state))
            {
                return nullptr;
            }

            for (const ComponentSubtreeSnapshot& componentSnapshot : node.Components)
            {
                if (componentSnapshot.OwnerAlias != node.Alias)
                {
                    return nullptr;
                }

                Component::Component* component = BuildPrefabComponent(
                    componentSnapshot,
                    path,
                    aliases,
                    state);
                TRawObjectGuard<Component::Component> componentGuard(component);
                if (!component || !entity->AddInner(component))
                {
                    return nullptr;
                }
                componentGuard.Release();
            }

            for (const EntitySubtreeSnapshotNode& childNode : node.Children)
            {
                if (childNode.ParentAlias != node.Alias)
                {
                    return nullptr;
                }

                Entity* child = BuildPrefabEntityNode(childNode, path, aliases, state);
                TRawObjectGuard<Entity> childGuard(child);
                if (!child || !entity->AddInner(child))
                {
                    return nullptr;
                }
                childGuard.Release();
            }

            return entityGuard.Release();
        }

        Entity* BuildPrefabInstance(
            const PrefabAsset& prefab,
            const Container::VariableArray<PrefabNestedInstanceId>& path,
            const PrefabOverrideSet* instanceOverrides,
            PrefabBuildState& state)
        {
            if (!prefab.HasTree() ||
                prefab.GetTree().RootAlias == InvalidSubtreeSnapshotAliasId ||
                prefab.GetTree().Root.Alias != prefab.GetTree().RootAlias ||
                prefab.GetTree().Root.ParentAlias != InvalidSubtreeSnapshotAliasId)
            {
                return nullptr;
            }

            PrefabAliasMap localAliases;
            Entity* root = BuildPrefabEntityNode(prefab.GetTree().Root, path, localAliases, state);
            TRawObjectGuard<Entity> rootGuard(root);
            if (!root)
            {
                return nullptr;
            }

            for (const PrefabNestedPrefabInstance& nested : prefab.GetNestedPrefabs())
            {
                auto parentIt = localAliases.find(nested.ParentAlias);
                const PrefabAsset* nestedPrefab = nested.Prefab.Get();
                if (parentIt == localAliases.end() ||
                    !parentIt->second.EntityPtr ||
                    !nestedPrefab ||
                    nested.InstanceId == InvalidPrefabNestedInstanceId)
                {
                    return nullptr;
                }

                Container::VariableArray<PrefabNestedInstanceId> nestedPath = path;
                nestedPath.push_back(nested.InstanceId);

                Entity* nestedRoot = BuildPrefabInstance(
                    *nestedPrefab,
                    nestedPath,
                    &nested.Overrides,
                    state);
                TRawObjectGuard<Entity> nestedRootGuard(nestedRoot);
                if (!nestedRoot || !parentIt->second.EntityPtr->AddInner(nestedRoot))
                {
                    return nullptr;
                }
                nestedRootGuard.Release();
            }

            if (instanceOverrides && !ApplyPrefabOverrideSet(*instanceOverrides, path, state))
            {
                return nullptr;
            }

            return rootGuard.Release();
        }

        void RegisterEntitySubtreeForComponentData(World& world, Entity& entity)
        {
            GEngine.GetComponentDataRegistry().RegisterEntity(world, entity);

            auto children = entity.GetChildEntities();
            for (auto* child : children)
            {
                RegisterEntitySubtreeForComponentData(world, *child);
            }
        }

        void UnregisterEntitySubtreeForComponentData(Entity& entity)
        {
            auto children = entity.GetChildEntities();
            for (auto* child : children)
            {
                UnregisterEntitySubtreeForComponentData(*child);
            }

            GEngine.GetComponentDataRegistry().UnregisterEntity(entity);
        }
    } // namespace

    IMPLEMENT_CLASS(World, Object)

    World::World()
        : Object()
    {
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
    }

    World::World(const FieldInitializer* initializer)
        : Object(initializer)
    {
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
    }

    World::World(const IUnknown* sourceObject)
        : Object(sourceObject)
    {
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        InitializeSchedule();
    }

    World::~World()
    {
        Finalize();
    }

    void World::Initialize()
    {
        if (HasFlag(OF_Initialized))
        {
            return;
        }

        LOG_INFO("World::Initialize()");

        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;
        m_SceneView = nullptr;

        Object::Initialize();
    }

    void World::Finalize()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        if (IsDeferringObjectRemoval())
        {
            LOG_ERROR("World::Finalize cannot run during update or cleanup");
            return;
        }
        EndTickCollection();
        ClearOrderedTicks();
        LOG_INFO("World::Finalize() - Destroying %llu objects",
                 static_cast<uint64_t>(GetObjectCount()));

        while (!m_Inners.empty())
        {
            IUnknown* inner = m_Inners.back();
            if (auto* entity = CastTo<Entity>(inner))
            {
                DestroyEntitySubtree(*entity);
                continue;
            }

            EraseInnerReference(*this, *inner);
            SetOuterReference(*inner, nullptr);
            if (inner->HasFlag(OF_HeapOwned))
            {
                inner->SetFlag(OF_PendingDestroy, true);
            }
            else
            {
                inner->Finalize();
                delete inner;
            }
        }

        GEngine.GetComponentDataRegistry().UnregisterWorld(*this);

        FlushDeferredEntityRows();
        RunEcsBindingCheck(true);

        // 行はすべて消えた。補間の一覧は空にする
        m_RenderRows.clear();
        m_RenderReconcile.clear();
        m_RenderChangedRows.clear();
        m_bRenderRowsUnsorted = false;
        m_RenderSyncDirtyLog.clear();
        m_RenderSyncTransformLog.clear();
        m_RenderSyncDestroyed.clear();
        m_RenderSyncList.Clear();
        m_RenderSyncChangedRows.clear();
        m_RenderSyncChangedSeen.clear();
        m_RenderSyncUnpublishRows.clear();
        m_RenderSettleRows.clear();
        m_RenderSettleNextRows.clear();
        m_RenderTextRows.clear();
        m_RenderTextSeen.clear();
        m_RenderTextSettleRows.clear();
        m_TextBoardRows.clear();
        m_TextBoardSeen.clear();
        m_bTextBoardOrderDirty = false;

        m_PendingDestroyEntities.clear();
        m_PendingDestroyComponents.clear();

        m_SceneView = nullptr;
        m_ScreenSpaceBoardSink = nullptr;
        NextObjectId = 1;
        m_RenderInterpolationAlpha = 1;
        m_bRenderInterpolationAllowed = true;

        Object::Finalize();

        LOG_INFO("World::Finalize() - Complete");
    }

    bool World::AddInner(IUnknown* inner)
    {
        if (CastTo<Entity>(inner))
        {
            return false;
        }

        return UnknownImpl::AddInner(inner);
    }

    bool World::RemoveInner(IUnknown* inner)
    {
        // 公開の RemoveInner で切り離すものも、待ち参照と破棄予約の一覧から外す（外した後に削除されても壊れない）
        if (inner && inner->GetOuter() == this)
        {
            if (auto* entity = CastTo<Entity>(inner))
            {
                InvalidateTickEntitySubtree(*entity);
                ForgetPendingDestroySubtree(*entity);
                UnbindEntitySubtreeRows(*entity);
            }
            else if (auto* component = CastTo<Component::Component>(inner))
            {
                InvalidateTickComponent(*component);
                ForgetPendingDestroy(*inner);
            }
        }

        return UnknownImpl::RemoveInner(inner);
    }

    bool World::AddObject(Entity* object)
    {
        if (!AttachRootEntity(object))
        {
            return false;
        }

        NORVES_LOG_DEBUG("World", "Object added (ID=%llu), total: %llu",
                         object->GetObjectId(),
                         static_cast<uint64_t>(GetObjectCount()));
        return true;
    }

    Entity* World::SpawnPrefab(const PrefabAsset& prefab, Entity* parent, const PrefabOverrideSet* overrides)
    {
        if (!HasFlag(OF_Initialized))
        {
            return nullptr;
        }

        if (parent && parent->GetWorld() != this)
        {
            return nullptr;
        }

        Container::VariableArray<const PrefabAsset*> prefabStack;
        if (!ValidatePrefabGraph(prefab, prefabStack, 1))
        {
            return nullptr;
        }

        PrefabBuildState buildState;
        Container::VariableArray<PrefabNestedInstanceId> rootPath;
        Entity* root = BuildPrefabInstance(prefab, rootPath, nullptr, buildState);
        TRawObjectGuard<Entity> rootGuard(root);
        if (!root)
        {
            return nullptr;
        }

        if (overrides && !ApplyPrefabOverrideSet(*overrides, rootPath, buildState))
        {
            return nullptr;
        }

        const bool bAttached = parent
            ? AttachChildEntity(parent, root)
            : AttachRootEntity(root);
        if (!bAttached)
        {
            return nullptr;
        }

        return rootGuard.Release();
    }

    void World::RemoveObject(Entity* object)
    {
        if (!object || !IsEntityRoot(*object))
        {
            return;
        }

        if (IsDeferringObjectRemoval())
        {
            object->MarkForDestroy();
            return;
        }
        DestroyEntitySubtree(*object);

        NORVES_LOG_DEBUG("World", "Object removed, remaining: %llu",
                         static_cast<uint64_t>(GetObjectCount()));
    }

    bool World::RemoveEntity(Entity* entity)
    {
        if (!entity || entity->GetWorld() != this || entity->GetOuter() == nullptr)
        {
            return false;
        }

        if (IsDeferringObjectRemoval())
        {
            entity->MarkForDestroy();
            return true;
        }
        return DestroyEntitySubtree(*entity);
    }

    bool World::ReparentEntity(Entity* entity, Entity* newParent)
    {
        if (!entity || entity->GetWorld() != this || entity->GetOuter() == nullptr)
        {
            return false;
        }

        if (entity->IsPendingDestroy())
        {
            return false;
        }

        if (HasPendingEntityAncestor(*entity))
        {
            return false;
        }

        if (newParent)
        {
            if (newParent->GetWorld() != this ||
                newParent->IsPendingDestroy() ||
                HasPendingEntityAncestor(*newParent) ||
                newParent == entity ||
                IsEntityInSubtree(*entity, *newParent))
            {
                return false;
            }
        }

        IUnknown* newOwner = newParent ? static_cast<IUnknown*>(newParent) : static_cast<IUnknown*>(this);
        if (entity->GetOuter() == newOwner)
        {
            return true;
        }

        UpdateWorldTransforms();
        const Math::Transform savedWorldTransform = entity->GetWorldTransform();

        if (!MoveEntityInnerAtomic(*entity, *newOwner))
        {
            return false;
        }

        entity->SetWorldTransform(savedWorldTransform);
        MarkEntitySubtreeTransformDirty(*entity);
        MarkEntitySubtreeRenderStateDirty(*entity);
        return true;
    }

    Container::VariableArray<Entity*> World::GetRootEntities() const
    {
        Container::VariableArray<Entity*> result;
        for (auto* inner : m_Inners)
        {
            if (auto* obj = CastTo<Entity>(inner))
            {
                result.push_back(obj);
            }
        }
        return result;
    }

    Entity* World::FindEntityByObjectId(uint64_t id) const
    {
        if (id == 0)
        {
            return nullptr;
        }

        for (Entity* root : GetRootEntities())
        {
            if (Entity* found = FindEntityByObjectIdRecursive(root, id))
            {
                return found;
            }
        }

        return nullptr;
    }

    size_t World::GetObjectCount() const
    {
        size_t count = 0;
        for (auto* inner : m_Inners)
        {
            if (CastTo<Entity>(inner))
            {
                ++count;
            }
        }
        return count;
    }

    void World::Tick(float deltaTime)
    {
        TickWithFrameTimes(nullptr, deltaTime);
    }
    void World::Tick(const Engine::FrameTimes& times)
    {
        if (!times.IsValid())
            return;
        const auto snapshot = times;
        TickWithFrameTimes(&snapshot, 0);
    }
    void World::TickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime)
    {
        if (!HasFlag(OF_Initialized) || IsDeferringObjectRemoval())
        {
            return;
        }
        // 前のフレームの LateTick が来なかったときの収集を閉じてから、このフレームの収集を開く
        EndTickCollection();
        if (++m_TickSerial == 0)
        {
            ++m_TickSerial;
        }
        // 検査 ECS_TICK_DUP は収集ごとに数え直す（有効かどうかも収集の先頭で決める）
        m_TickDupSeen.clear();
        m_TickDupCalls = 0;
        m_bTickDupCheck = NorvesLib::Core::Engine::GEngine != nullptr &&
                          NorvesLib::Core::Engine::GEngine->GetEngineChecks().IsEnabled("ECS_TICK_DUP");
        // 呼び先の付け直しは収集を開く直前に行い、この収集から効く（収集の中で起きた変更は次の収集から）
        if (m_bOrderedTicksDirty)
        {
            SyncOrderedTicks();
        }
        m_Scheduler->BeginCollection();
        m_bTickCollectionOpen = true;
        m_StageTimes = times;
        m_StageDelta = deltaTime;
        RunWorldStage(m_StageTick);
    }

    void World::LateTick(float deltaTime)
    {
        LateTickWithFrameTimes(nullptr, deltaTime);
    }
    void World::LateTick(const Engine::FrameTimes& times)
    {
        if (!times.IsValid())
            return;
        const auto snapshot = times;
        LateTickWithFrameTimes(&snapshot, 0);
    }
    void World::LateTickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime)
    {
        if (!HasFlag(OF_Initialized) || !m_bTickCollectionOpen || IsDeferringObjectRemoval())
        {
            return;
        }
        m_StageTimes = times;
        m_StageDelta = deltaTime;
        RunWorldStage(m_StageLateTick);
        EndTickCollection();
        RunEcsBindingCheck(false);
    }

    void World::EndTickCollection()
    {
        if (m_bTickCollectionOpen)
        {
            m_bTickCollectionOpen = false;
            m_Scheduler->EndCollection();
        }
    }

    void World::RunWorldStage(uint32_t stage)
    {
        m_Scheduler->RunStage(stage);
    }

    void World::InitializeSchedule()
    {
        m_EcsTransformSystem.reset();
        // 呼び先の持ち手は作り直すスケジューラでは使えない
        m_OrderedTicks.clear();
        m_bOrderedTicksDirty = true;
        // 行は作り直す ECS の世界では引けない
        m_TextBoardRows.clear();
        m_TextBoardSeen.clear();
        m_bTextBoardOrderDirty = false;
        RegisterCoreComponentTicks();
        m_EcsWorld = Container::MakeUnique<Ecs::EcsWorld>();
        // 階層のシステムのきっかけになる列は追跡する（最初のアーキタイプを作る前に決める）。消費者がいない間はログを積まない
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsLocalTransform>();
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsHierarchy>();
        m_EcsWorld->SetColumnTrackedOf<Ecs::EcsEntityFlags>();
        m_Scheduler = Container::MakeUnique<Ecs::SystemScheduler>(*m_EcsWorld);
        Ecs::SystemScheduler& scheduler = *m_Scheduler;

        m_StageTick = scheduler.AddStage("Tick");
        m_StageLateTick = scheduler.AddStage("LateTick");
        m_StageFixedBegin = scheduler.AddStage("FixedBegin");
        m_StageFixedTick = scheduler.AddStage("FixedTick");
        m_StageFixedEnd = scheduler.AddStage("FixedEnd");
        m_StageSync = scheduler.AddStage("RenderSync");

        // 段の GameThreadOnly のシステムは登録順に単独の波で走る。従来の処理を同じ順・同じ位置で呼ぶ。
        auto add = [&](uint32_t stage, const char* name, Ecs::EcsSystemFn fn)
        {
            Ecs::SystemDesc desc;
            desc.Name = name;
            desc.Stage = stage;
            desc.Thread = ThreadPolicy::GameThreadOnly;
            desc.Run = fn;
            desc.User = this;
            const Ecs::EcsSystemId id = scheduler.RegisterSystem(desc);
            if (id == Ecs::kInvalidEcsSystem)
            {
                LOG_ERROR("World: failed to register ECS system %s", name);
            }
        };
        using Context = Ecs::EcsSystemContext;

        add(m_StageTick, "Tick.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageTick, "Tick.DispatchGroups",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::Input, Component::ETickGroup::PoseFinalize,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageTick, "Tick.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });

        add(m_StageLateTick, "LateTick.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageLateTick, "LateTick.DispatchPostPhysics",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::PostPhysics, Component::ETickGroup::PostPhysics,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageLateTick, "LateTick.UpdateRenderTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateRenderTransforms(); });
        add(m_StageLateTick, "LateTick.DispatchCameraToPreRender",
            [](void* user, const Context&)
            {
                auto* world = static_cast<World*>(user);
                world->DispatchTickGroups(Component::ETickGroup::Camera, Component::ETickGroup::PreRender,
                                          world->m_StageDelta, world->m_StageTimes);
            });
        add(m_StageLateTick, "LateTick.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });

        // 登録のある型の呼び先を持つ順序付きの系。呼ぶのは同じ段の Dispatch のシステムで、
        // （群, 優先度, 深さ優先の順）に呼び、群の切れ目で変換を公開する
        auto addOrdered = [&](uint32_t stage, const char* name, Component::ETickGroup first, Component::ETickGroup last)
        {
            Ecs::OrderedSystemDesc desc;
            desc.Name = name;
            desc.Stage = stage;
            desc.Groups = 0;
            for (uint8_t index = static_cast<uint8_t>(first); index <= static_cast<uint8_t>(last); ++index)
            {
                desc.Groups |= Component::TickGroupBit(static_cast<Component::ETickGroup>(index));
            }
            desc.bCallerDriven = true;
            const Ecs::EcsSystemId id = scheduler.RegisterOrderedSystem(desc);
            if (id == Ecs::kInvalidEcsSystem)
            {
                LOG_ERROR("World: 順序付きの系 %s を登録できなかった", name);
            }
            return id;
        };
        m_OrderedTickSystems[0] = addOrdered(m_StageTick, "Tick.OrderedInputToPoseFinalize",
                                             Component::ETickGroup::Input, Component::ETickGroup::PoseFinalize);
        m_OrderedTickSystems[1] = addOrdered(m_StageLateTick, "LateTick.OrderedPostPhysics",
                                             Component::ETickGroup::PostPhysics, Component::ETickGroup::PostPhysics);
        m_OrderedTickSystems[2] = addOrdered(m_StageLateTick, "LateTick.OrderedCameraToPreRender",
                                             Component::ETickGroup::Camera, Component::ETickGroup::PreRender);
        add(m_StageLateTick, "LateTick.ReportTickDup",
            [](void* user, const Context&) { static_cast<World*>(user)->ReportTickDupCheck(); });

        add(m_StageFixedBegin, "FixedBegin.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageFixedBegin, "FixedBegin.PrepareInterpolation",
            [](void* user, const Context&) { static_cast<World*>(user)->PrepareRenderInterpolationStep(); });

        // 段 FixedTick のシステムは、物理などのモジュールが登録する（World は持たない）

        add(m_StageFixedEnd, "FixedEnd.UpdateTransforms",
            [](void* user, const Context&) { static_cast<World*>(user)->UpdateWorldTransforms(); });
        add(m_StageFixedEnd, "FixedEnd.CaptureInterpolation",
            [](void* user, const Context&) { static_cast<World*>(user)->CaptureRenderInterpolationStep(); });
        add(m_StageFixedEnd, "FixedEnd.Cleanup",
            [](void* user, const Context&) { static_cast<World*>(user)->CleanupDestroyedObjects(); });

        add(m_StageSync, "RenderSync.SyncToSceneView",
            [](void* user, const Context&) { static_cast<World*>(user)->SyncToSceneViewBody(); });

        // 補間の列は行へ後から付け外しされる。型の番号は並列が始まる前に固定しておく
        (void)Ecs::EcsRegisterColumn<Ecs::EcsRenderTransform>();
        (void)Ecs::EcsRegisterColumn<Ecs::EcsRenderInterpolated>();
        (void)Ecs::EcsRegisterColumn<Ecs::EcsTransformHistory>();

        // World の木にいる全 Entity の行: Object への弱い参照・階層・flags・時間倍率・変換（局所とワールドのキャッシュ）・階層のシステムの比較用の列
        const Ecs::EcsColumnInfo entityColumns[] = {
            Ecs::EcsRegisterColumn<Ecs::EcsObjectRef>(),
            Ecs::EcsRegisterColumn<Ecs::EcsHierarchy>(),
            Ecs::EcsRegisterColumn<Ecs::EcsEntityFlags>(),
            Ecs::EcsRegisterColumn<Ecs::EcsTimeDilation>(),
            Ecs::EcsRegisterColumn<Ecs::EcsLocalTransform>(),
            Ecs::EcsRegisterColumn<Ecs::EcsWorldTransform>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPropagatedTransform>(),
        };
        m_EntityArchetype = m_EcsWorld->GetOrCreateArchetype(
            Container::Span<const Ecs::EcsColumnInfo>(entityColumns, 7));
        if (m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            LOG_ERROR("World: Entity 用の ECS のアーキタイプを作れなかった");
        }

        // Component の行: 所有者の子の葉。flags と Tick の設定を列に持ち、Mesh 系は描画の値の列も持つ
        const Ecs::EcsColumnInfo basicColumns[] = {
            Ecs::EcsRegisterColumn<Ecs::EcsObjectRef>(),
            Ecs::EcsRegisterColumn<Ecs::EcsHierarchy>(),
            Ecs::EcsRegisterColumn<Ecs::EcsComponentFlags>(),
            Ecs::EcsRegisterColumn<Ecs::EcsTickConfig>(),
        };
        // 描画へ出す行は、描画の値の列のほかに公開の状態（EcsPublishState）を持つ
        const Ecs::EcsColumnInfo meshColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsMeshRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const Ecs::EcsColumnInfo megaColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsMeshRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsMegaMesh>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
            Ecs::EcsRegisterColumn<Ecs::EcsGpuSceneSlot>(),
        };
        const Ecs::EcsColumnInfo skinnedColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsSkinnedRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPoseVersion>(),
        };
        const Ecs::EcsColumnInfo lightColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsLightRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const Ecs::EcsColumnInfo pointLightColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsLightRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsLightFalloff>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const Ecs::EcsColumnInfo spotLightColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsLightRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsLightFalloff>(),
            Ecs::EcsRegisterColumn<Ecs::EcsSpotCone>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const Ecs::EcsColumnInfo boardColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsBoardRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const Ecs::EcsColumnInfo billboardColumns[] = {
            basicColumns[0], basicColumns[1], basicColumns[2], basicColumns[3],
            Ecs::EcsRegisterColumn<Ecs::EcsBoardRender>(),
            Ecs::EcsRegisterColumn<Ecs::EcsBillboardSize>(),
            Ecs::EcsRegisterColumn<Ecs::EcsPublishState>(),
        };
        const auto createComponentArchetype = [this](Ecs::EcsComponentRowKind kind, Container::Span<const Ecs::EcsColumnInfo> columns)
        {
            Ecs::EcsArchetypeId& archetype = m_ComponentArchetypes[static_cast<size_t>(kind)];
            archetype = m_EcsWorld->GetOrCreateArchetype(columns);
            if (archetype == Ecs::kInvalidEcsArchetype)
            {
                LOG_ERROR("World: Component 用の ECS のアーキタイプを作れなかった");
            }
        };
        createComponentArchetype(Ecs::EcsComponentRowKind::Basic, Container::Span<const Ecs::EcsColumnInfo>(basicColumns, 4));
        createComponentArchetype(Ecs::EcsComponentRowKind::Mesh, Container::Span<const Ecs::EcsColumnInfo>(meshColumns, 6));
        createComponentArchetype(Ecs::EcsComponentRowKind::MegaGeometry, Container::Span<const Ecs::EcsColumnInfo>(megaColumns, 8));
        createComponentArchetype(Ecs::EcsComponentRowKind::SkinnedMesh, Container::Span<const Ecs::EcsColumnInfo>(skinnedColumns, 7));
        createComponentArchetype(Ecs::EcsComponentRowKind::Light, Container::Span<const Ecs::EcsColumnInfo>(lightColumns, 6));
        createComponentArchetype(Ecs::EcsComponentRowKind::PointLight, Container::Span<const Ecs::EcsColumnInfo>(pointLightColumns, 7));
        createComponentArchetype(Ecs::EcsComponentRowKind::SpotLight, Container::Span<const Ecs::EcsColumnInfo>(spotLightColumns, 8));
        createComponentArchetype(Ecs::EcsComponentRowKind::Board, Container::Span<const Ecs::EcsColumnInfo>(boardColumns, 6));
        createComponentArchetype(Ecs::EcsComponentRowKind::Billboard, Container::Span<const Ecs::EcsColumnInfo>(billboardColumns, 7));

        // 行の削除で、出していた proxy の Remove を積む（段 RenderSync の一覧）
        Ecs::EcsColumnHooks publishHooks;
        publishHooks.OnDestroy = &World::OnPublishStateDestroyed;
        publishHooks.User = this;
        m_EcsWorld->SetColumnHooks(Ecs::EcsColumnInfoOf<Ecs::EcsPublishState>().Type, publishHooks);
        m_bRenderSyncLogEnabled = IsRenderSyncCheckActive();
        // 検査 MEGA_SLOTS は席ごとの境界の履歴を使うので、最初の割り当ての前に有効にする
        m_EcsWorld->GetGpuSceneSlots().SetAuditEnabled(IsGpuSceneSlotsCheckActive());

        // 段の最中に孤児にした行は、各段の同期点で消す
        const uint32_t stages[] = {m_StageTick, m_StageLateTick, m_StageFixedBegin,
                                   m_StageFixedTick, m_StageFixedEnd, m_StageSync};
        for (const uint32_t stage : stages)
        {
            scheduler.SetSyncHook(stage,
                [](void* user, Ecs::EcsWorld&) { static_cast<World*>(user)->FlushDeferredEntityRows(); }, this);
        }
    }

    void World::BeginFixedStep()
    {
        RunWorldStage(m_StageFixedBegin);
    }

    void World::EndFixedStep()
    {
        RunWorldStage(m_StageFixedEnd);
    }

    void World::SyncOrderedTicks()
    {
        m_bOrderedTicksDirty = false;
        if (m_Scheduler == nullptr)
        {
            return;
        }
        // 登録のある型の呼び先を、木の深さ優先の番号で足し・付け替える（変わったものだけ）。見つからなかったものは外す
        ++m_OrderedTickWalk;
        size_t ordinal = 0;
        for (IUnknown* inner : GetInners())
        {
            if (auto* entity = CastTo<Entity>(inner))
            {
                CollectOrderedTicks(*entity, ordinal);
            }
        }
        SweepOrderedTicks();
    }

    void World::CollectOrderedTicks(Entity& entity, size_t& ordinal)
    {
        if (entity.IsPendingDestroy())
        {
            return;
        }
        // 番号は Entity 自身に 1 つ、Component に 1 つずつ振る（深さ優先）
        ++ordinal;
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                const size_t order = ordinal++;
                // 型に本体を登録した型だけ、本体を呼ぶ群ごとに順序付きの系の呼び先にする（本体が null の型は載せない）
                const Component::ComponentTickRegistration* registration = FindComponentTick(component->GetClass());
                if (registration == nullptr || !registration->HasBody())
                {
                    continue;
                }
                const auto primary = component->GetTickGroup();
                const auto mask = component->GetTickGroupMask();
                for (uint8_t index = 0; index < Component::TickGroupCount; ++index)
                {
                    const auto group = static_cast<Component::ETickGroup>(index);
                    if ((mask & Component::TickGroupBit(group)) == 0 ||
                        (registration->bPrimaryGroupOnly ? group != primary
                                                         : (registration->Groups & Component::TickGroupBit(group)) == 0) ||
                        registration->BodyFor(group) == nullptr)
                    {
                        continue;
                    }
                    SyncOrderedTick(entity, *component, group, component->GetTickPriority(), order, *registration);
                }
            }
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                CollectOrderedTicks(*child, ordinal);
            }
        }
    }

    bool World::CanDispatchEntity(const Entity& entity) const
    {
        if (entity.GetWorld() != this || !entity.IsTickEnabled())
        {
            return false;
        }
        // 自分の今の状態は常に見る。祖先を含めた判定は、階層のシステムが求めた EffectiveActive を読む
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return false;
        }
        // 作成予約中の行・付け替え待ちがある間と、Active・PendingDestroy の変更がまだ伝播されていない間は、キャッシュが古いので祖先をたどる
        if (!m_bLegacyTransformPropagation && m_EcsWorld && m_PendingEntityRelinks.empty() && m_PendingEntityCreates.empty() &&
            m_EcsTransformSystem && !m_EcsTransformSystem->HasUnpropagatedFlagChanges())
        {
            const Ecs::EcsEntity row = entity.GetEcsEntity();
            if (!row.IsNull() && m_EcsWorld->IsAlive(row))
            {
                const Ecs::EcsPropagatedTransform* propagated = m_EcsWorld->GetColumnValue<Ecs::EcsPropagatedTransform>(row);
                if (propagated != nullptr && propagated->bValid != 0)
                {
                    return propagated->bEffectiveActive != 0;
                }
            }
        }
        // 行が無い・まだ求めていない・旧の経路のときは、祖先をたどる
        for (const Entity* current = &entity; current; current = current->GetParentEntity())
        {
            if (!current->IsActive() || current->IsPendingDestroy())
            {
                return false;
            }
        }
        return true;
    }

    bool World::TryResolveChannelDelta(const Entity& owner, Engine::TimeChannel channel, float& out) const
    {
        if (!m_bHasActiveTickTime || owner.GetWorld() != this)
            return false;
        if (!Engine::IsValidTimeChannel(channel))
            return false;
        const float base = m_ActiveTickTimes ? m_ActiveTickTimes->GetDelta(channel) : m_ActiveTickDelta;
        const bool local = channel == Engine::TimeChannel::World || channel == Engine::TimeChannel::Animation;
        const double delta = double(base) * (local ? owner.GetCustomTimeDilation() : 1.f);
        if (!std::isfinite(delta) || delta < 0 || delta > std::numeric_limits<float>::max())
            return false;
        out = static_cast<float>(delta);
        return true;
    }
    bool World::TryGetComponentTickDelta(const Component::Component& component, float& out) const
    {
        const auto* owner = component.GetOwner();
        return owner && TryResolveChannelDelta(*owner, GetComponentTimeChannel(component), out);
    }
    Engine::TimeChannel World::GetComponentTimeChannel(const Component::Component& component) const
    {
        const Component::ComponentTickRegistration* registration = FindComponentTick(component.GetClass());
        return registration != nullptr ? registration->TimeChannel : Engine::TimeChannel::World;
    }
    void World::DispatchTickGroups(Component::ETickGroup first, Component::ETickGroup last, float deltaTime,
                                   const Engine::FrameTimes* times)
    {
        ScopedWorldFlag dispatch(m_bDispatchingTicks);
        struct TickTimeContext
        {
            World& Owner;
            const Engine::FrameTimes* PreviousTimes;
            float PreviousDelta;
            bool PreviousActive;
            TickTimeContext(World& owner, const Engine::FrameTimes* times, float delta)
                : Owner(owner), PreviousTimes(owner.m_ActiveTickTimes), PreviousDelta(owner.m_ActiveTickDelta),
                  PreviousActive(owner.m_bHasActiveTickTime)
            {
                owner.m_ActiveTickTimes = times;
                owner.m_ActiveTickDelta = delta;
                owner.m_bHasActiveTickTime = true;
            }
            ~TickTimeContext()
            {
                Owner.m_ActiveTickTimes = PreviousTimes;
                Owner.m_ActiveTickDelta = PreviousDelta;
                Owner.m_bHasActiveTickTime = PreviousActive;
            }
        } context(*this, times, deltaTime);
        auto publishedGroup = Component::ETickGroup::Count;
        auto publish = [&](Component::ETickGroup group)
        {
            if (publishedGroup != Component::ETickGroup::Count && group != publishedGroup)
            {
                // 前の群で変更された親の姿勢を子階層へ公開する。同群内では確定しない。
                UpdateWorldTransforms();
            }
            publishedGroup = group;
        };
        // 順序付きの系の順の一覧（収集の先頭でだけ変わる）を（群, 優先度, 深さ優先の順）に呼ぶ。
        // 呼び先の中で外されたものは、呼ぶ直前に読み直して飛ばす
        const uint32_t orderedSystem = OrderedTickSystemOf(first);
        const uint32_t orderedCount = orderedSystem != Ecs::kInvalidEcsSystem
            ? m_Scheduler->GetOrderedRunCount(orderedSystem) : 0;
        Ecs::EcsSystemContext orderedContext;
        orderedContext.World = m_EcsWorld.get();
        orderedContext.CollectionSerial = m_Scheduler->GetCollectionSerial();
        for (uint32_t orderedIndex = 0; orderedIndex < orderedCount; ++orderedIndex)
        {
            Ecs::OrderedRunView view;
            if (!m_Scheduler->GetOrderedRun(orderedSystem, orderedIndex, view) || view.Group < first || view.Group > last)
            {
                continue;
            }
            publish(view.Group);
            view.Run(view.User, orderedContext);
        }
    }

    void World::RegisterComponentTick(const IClass* cls, const Component::ComponentTickRegistration& registration)
    {
        if (cls == nullptr)
        {
            return;
        }
        m_ComponentTicks[cls] = registration;
        // 型の本体・群・channel が変わりうるので、次の収集で呼び先を付け直す
        m_bOrderedTicksDirty = true;
    }

    const Component::ComponentTickRegistration* World::FindComponentTick(const IClass* cls) const
    {
        const auto it = m_ComponentTicks.find(cls);
        return it != m_ComponentTicks.end() ? &it->second : nullptr;
    }

    void World::RegisterCoreComponentTicks()
    {
        // 設計 1.12 の行き先。順・群・mask・優先度は Component の設定のまま（順序付きの系の呼び先として呼ぶ）
        // Mesh: 変換のキャッシュは描画の同期（RefreshRenderTransformCache）が求めるので、毎フレームの処理は無い。
        // Camera・Light 系: 中身が空だった。Camera の channel は Unscaled（TryGetComponentTickDelta が使う）
        Component::ComponentTickRegistration none;
        none.Name = "NoTick";
        RegisterComponentTick(Component::MeshComponent::StaticClass(), none);
        RegisterComponentTick(Component::MegaGeometryComponent::StaticClass(), none);
        Component::ComponentTickRegistration camera = none;
        camera.TimeChannel = Engine::TimeChannel::Unscaled;
        RegisterComponentTick(Component::CameraComponent::StaticClass(), camera);
        RegisterComponentTick(Component::LightComponent::StaticClass(), none);
        RegisterComponentTick(Component::DirectionalLightComponent::StaticClass(), none);
        RegisterComponentTick(Component::PointLightComponent::StaticClass(), none);
        RegisterComponentTick(Component::SpotLightComponent::StaticClass(), none);

        // Board の Flipbook は Default（主群）の系。派生の Billboard・Impostor・Text も同じ本体を持っていた
        Component::ComponentTickRegistration board;
        board.Name = "Board.Flipbook";
        board.Run = [](Component::Component& component, float deltaTime)
        { static_cast<Component::BoardComponent&>(component).TickFlipbook(deltaTime); };
        board.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsBoardRender>().Type);
        RegisterComponentTick(Component::BoardComponent::StaticClass(), board);
        RegisterComponentTick(Component::BillboardComponent::StaticClass(), board);
        RegisterComponentTick(Component::ImpostorComponent::StaticClass(), board);
        RegisterComponentTick(Component::TextComponent::StaticClass(), board);

        // SpringArm は Camera の群（主群）
        Component::ComponentTickRegistration arm;
        arm.Name = "SpringArm.Camera";
        arm.Run = [](Component::Component& component, float deltaTime)
        { static_cast<Component::SpringArmComponent&>(component).TickArm(deltaTime); };
        arm.TimeChannel = Engine::TimeChannel::Unscaled;
        arm.Reads.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsWorldTransform>().Type);
        arm.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type);
        RegisterComponentTick(Component::SpringArmComponent::StaticClass(), arm);

        // HoldSlot・SocketAttachment は PostPhysics の群でだけ呼ぶ（主群によらない）
        Component::ComponentTickRegistration hold;
        hold.Name = "HoldSlot.PostPhysics";
        hold.bPrimaryGroupOnly = false;
        hold.Groups = Component::TickGroupBit(Component::ETickGroup::PostPhysics);
        hold.Run = [](Component::Component& component, float deltaTime)
        { static_cast<Component::HoldSlotComponent&>(component).TickHolds(deltaTime); };
        hold.Reads.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsWorldTransform>().Type);
        hold.Reads.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type);
        hold.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type);
        RegisterComponentTick(Component::HoldSlotComponent::StaticClass(), hold);

        Component::ComponentTickRegistration socket;
        socket.Name = "SocketAttachment.PostPhysics";
        socket.bPrimaryGroupOnly = false;
        socket.Groups = Component::TickGroupBit(Component::ETickGroup::PostPhysics);
        socket.Run = [](Component::Component& component, float deltaTime)
        { static_cast<Component::SocketAttachmentComponent&>(component).TickAttachment(deltaTime); };
        socket.Reads = hold.Reads;
        socket.Writes = hold.Writes;
        RegisterComponentTick(Component::SocketAttachmentComponent::StaticClass(), socket);

        // Animator・SkinnedMesh: 更新は Animation、評価は PoseFinalize の群（channel は Animation）。
        // Animator のイベントは PoseFinalize の本体の最後に GameThread で配る（ゲームのコードを呼ぶため）
        const auto animationBit = Component::TickGroupBit(Component::ETickGroup::Animation);
        const auto poseFinalizeBit = Component::TickGroupBit(Component::ETickGroup::PoseFinalize);
        Component::ComponentTickRegistration animator;
        animator.Name = "Animator.AnimationPoseFinalize";
        animator.bPrimaryGroupOnly = false;
        animator.Groups = animationBit | poseFinalizeBit;
        animator.GroupRuns[static_cast<size_t>(Component::ETickGroup::Animation)] =
            [](Component::Component& component, float deltaTime)
        { static_cast<Component::AnimatorComponent&>(component).TickAnimationGroup(deltaTime); };
        animator.GroupRuns[static_cast<size_t>(Component::ETickGroup::PoseFinalize)] =
            [](Component::Component& component, float)
        { static_cast<Component::AnimatorComponent&>(component).TickPoseFinalizeGroup(); };
        animator.TimeChannel = Engine::TimeChannel::Animation;
        animator.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type);
        RegisterComponentTick(Component::AnimatorComponent::StaticClass(), animator);

        Component::ComponentTickRegistration skinned;
        skinned.Name = "SkinnedMesh.AnimationPoseFinalize";
        skinned.bPrimaryGroupOnly = false;
        skinned.Groups = animationBit | poseFinalizeBit;
        skinned.GroupRuns[static_cast<size_t>(Component::ETickGroup::Animation)] =
            [](Component::Component& component, float deltaTime)
        { static_cast<Component::SkinnedMeshComponent&>(component).AdvanceAnimationTime(deltaTime); };
        skinned.GroupRuns[static_cast<size_t>(Component::ETickGroup::PoseFinalize)] =
            [](Component::Component& component, float)
        { static_cast<Component::SkinnedMeshComponent&>(component).TickPoseFinalizeGroup(); };
        skinned.TimeChannel = Engine::TimeChannel::Animation;
        skinned.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type);
        RegisterComponentTick(Component::SkinnedMeshComponent::StaticClass(), skinned);
    }

    struct World::OrderedTickRecord
    {
        World* Owner = nullptr;
        Entity* OwnerEntity = nullptr;
        Component::Component* Target = nullptr;
        Component::ComponentTickFn Body = nullptr;
        Component::ETickGroup Group = Component::ETickGroup::Default;
        int16_t Priority = 0;
        size_t Ordinal = 0;
        Engine::TimeChannel Channel = Engine::TimeChannel::World;
        Ecs::EcsOrderedHandle Handle;
        /// 最後に収集で見つけた回（m_OrderedTickWalk）
        uint64_t SeenWalk = 0;
    };

    uint32_t World::OrderedTickSystemOf(Component::ETickGroup group) const
    {
        if (group <= Component::ETickGroup::PoseFinalize)
        {
            return m_OrderedTickSystems[0];
        }
        if (group == Component::ETickGroup::PostPhysics)
        {
            return m_OrderedTickSystems[1];
        }
        return group < Component::ETickGroup::Count ? m_OrderedTickSystems[2] : Ecs::kInvalidEcsSystem;
    }

    bool World::AddOrderedTickEntry(OrderedTickRecord& record)
    {
        const uint32_t system = OrderedTickSystemOf(record.Group);
        if (system == Ecs::kInvalidEcsSystem)
        {
            return false;
        }
        Ecs::OrderedEntryDesc desc;
        desc.Group = record.Group;
        desc.Priority = record.Priority;
        desc.Ordinal = record.Ordinal;
        desc.Run = &World::RunOrderedTick;
        desc.User = &record;
        desc.TimeChannel = static_cast<uint8_t>(record.Channel);
        record.Handle = m_Scheduler->AddOrderedEntry(system, desc);
        return record.Handle.IsValid();
    }

    void World::SyncOrderedTick(Entity& entity, Component::Component& component, Component::ETickGroup group,
                                int16_t priority, size_t ordinal, const Component::ComponentTickRegistration& registration)
    {
        const Engine::TimeChannel channel = registration.TimeChannel;
        auto& records = m_OrderedTicks[&component];
        for (auto& record : records)
        {
            if (record->Group != group)
            {
                continue;
            }
            record->SeenWalk = m_OrderedTickWalk;
            record->OwnerEntity = &entity;
            record->Body = registration.BodyFor(group);
            if (record->Channel != channel)
            {
                // channel は項目に持つので、足し直す（収集を開く前なので、この収集から呼ばれる）
                m_Scheduler->RemoveOrderedEntry(record->Handle);
                record->Channel = channel;
                record->Priority = priority;
                record->Ordinal = ordinal;
                if (!AddOrderedTickEntry(*record))
                {
                    LOG_ERROR("World: 順序付きの系へ %s の呼び先を足し直せなかった", registration.Name);
                }
            }
            else if (record->Priority != priority || record->Ordinal != ordinal)
            {
                m_Scheduler->SetOrderedEntryKey(record->Handle, group, priority, ordinal);
                record->Priority = priority;
                record->Ordinal = ordinal;
            }
            return;
        }
        auto record = Container::MakeUnique<OrderedTickRecord>();
        record->Owner = this;
        record->OwnerEntity = &entity;
        record->Target = &component;
        record->Body = registration.BodyFor(group);
        record->Group = group;
        record->Priority = priority;
        record->Ordinal = ordinal;
        record->Channel = channel;
        record->SeenWalk = m_OrderedTickWalk;
        if (!AddOrderedTickEntry(*record))
        {
            LOG_ERROR("World: 順序付きの系へ %s の呼び先を足せなかった", registration.Name);
            return;
        }
        records.push_back(std::move(record));
    }

    void World::SweepOrderedTicks()
    {
        // 収集で見つからなかった呼び先（破棄予約の部分木・群や mask から外れたもの）を外す
        for (auto it = m_OrderedTicks.begin(); it != m_OrderedTicks.end();)
        {
            auto& records = it->second;
            for (size_t index = records.size(); index-- > 0;)
            {
                if (records[index]->SeenWalk != m_OrderedTickWalk)
                {
                    m_Scheduler->RemoveOrderedEntry(records[index]->Handle);
                    records.erase(records.begin() + static_cast<std::ptrdiff_t>(index));
                }
            }
            it = records.empty() ? m_OrderedTicks.erase(it) : std::next(it);
        }
    }

    void World::RemoveOrderedTicks(const Component::Component& component)
    {
        const auto it = m_OrderedTicks.find(&component);
        if (it == m_OrderedTicks.end())
        {
            return;
        }
        // 外した呼び先は、走っている収集の中でもすぐに呼ばれなくなる
        for (auto& record : it->second)
        {
            m_Scheduler->RemoveOrderedEntry(record->Handle);
        }
        m_OrderedTicks.erase(it);
    }

    void World::ClearOrderedTicks()
    {
        if (m_Scheduler)
        {
            for (auto& [component, records] : m_OrderedTicks)
            {
                for (auto& record : records)
                {
                    m_Scheduler->RemoveOrderedEntry(record->Handle);
                }
            }
        }
        m_OrderedTicks.clear();
    }

    void World::RunOrderedTick(void* user, const Ecs::EcsSystemContext&)
    {
        auto* record = static_cast<OrderedTickRecord*>(user);
        record->Owner->ExecuteOrderedTick(*record);
    }

    void World::ExecuteOrderedTick(OrderedTickRecord& record)
    {
        // 本体の中で自分が外されると記録も消えるので、使う値は先に写す
        Entity* owner = record.OwnerEntity;
        Component::Component* target = record.Target;
        const Component::ComponentTickFn body = record.Body;
        const Component::ETickGroup group = record.Group;
        const Engine::TimeChannel channel = record.Channel;
        if (owner == nullptr || target == nullptr || body == nullptr || !CanDispatchEntity(*owner))
        {
            return;
        }
        if (target->GetOwner() != owner || !target->IsActive() || !target->IsTickEnabled() || target->IsPendingDestroy())
        {
            return;
        }
        float tickDelta = 0;
        if (!TryResolveChannelDelta(*owner, channel, tickDelta))
        {
            return;
        }
        if (m_bTickDupCheck)
        {
            NoteTickDispatchForCheck(*target, group);
        }
        body(*target, tickDelta);
    }

    void World::NoteTickDispatchForCheck(const Component::Component& component, Component::ETickGroup group)
    {
        ++m_TickDupCalls;
        // 呼び先の番地と群を 1 つの鍵にする（ユーザー空間の番地は 2^56 未満）
        const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&component)) << 8) |
                             static_cast<uint64_t>(group);
        if (!m_TickDupSeen.insert(key).second)
        {
            ++m_TickDupFailures;
            LOG_ERROR("ECS_TICK_DUP: 同じ収集で同じ呼び先と群を 2 回呼んだ (component=%llu group=%u)",
                      static_cast<unsigned long long>(component.GetComponentId()), static_cast<unsigned>(group));
        }
    }

    void World::ReportTickDupCheck()
    {
        if (!m_bTickDupCheck)
        {
            return;
        }
        m_bTickDupCheck = false;
        if (NorvesLib::Core::Engine::GEngine == nullptr || (m_TickDupCalls == 0 && m_TickDupFailures == 0))
        {
            return;
        }
        // fail は毎回、pass は最初と 64 回ごとに出す
        const bool bPass = m_TickDupFailures == 0;
        ++m_TickDupCollections;
        if (!bPass || m_TickDupCollections == 1 || m_TickDupCollections % 64 == 0)
        {
            NorvesLib::Core::Engine::GEngine->GetEngineChecks().Report("ECS_TICK_DUP", bPass, m_TickDupCalls);
        }
        m_TickDupFailures = 0;
    }

    void World::InvalidateTickComponent(Component::Component& component)
    {
        RemoveOrderedTicks(component);
        for (auto& pending : m_PendingTickComponents)
        {
            if (pending == &component)
            {
                pending = nullptr;
            }
        }
    }

    void World::InvalidateTickEntitySubtree(Entity& entity)
    {
        for (auto& pending : m_PendingTickEntities)
        {
            if (pending == &entity)
            {
                pending = nullptr;
            }
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                InvalidateTickEntitySubtree(*child);
            }
            else if (auto* component = CastTo<Component::Component>(inner))
            {
                InvalidateTickComponent(*component);
            }
        }
    }

    void World::SetSceneView(Rendering::SceneView* sceneView)
    {
        m_SceneView = sceneView;
        // 接続先が替わったので、全部の行を公開し直す
        NotifyRenderRepublishAll();
    }

    void World::SetScreenSpaceBoardSink(Rendering::IBoardProxySink* sink)
    {
        if (m_ScreenSpaceBoardSink == sink)
        {
            return;
        }

        m_ScreenSpaceBoardSink = sink;
        NotifyRenderRepublishAll();
    }

    void World::SyncToSceneView(const Rendering::MaterialResources* materials,
                                const Rendering::MeshResources* meshes)
    {
        m_StageMaterials = materials;
        m_StageMeshes = meshes;
        if (m_Scheduler->IsRunning())
        {
            // 段の最中に呼ばれたときは再入できないので、段を通さずに同じ処理を行う
            SyncToSceneViewBody();
            return;
        }
        RunWorldStage(m_StageSync);
    }

    void World::SyncToSceneViewBody()
    {
        const Rendering::MaterialResources* materials = m_StageMaterials;
        const Rendering::MeshResources* meshes = m_StageMeshes;
        m_RenderSyncVisitedRows = 0;
#if NORVES_ENABLE_STATS
        // 計測: 同期は 1 フレーム 1 回なので、ここでフレームを数え、変換の行の数の 1 フレームあたりの平均を一定のフレームごとに出す
        m_TransformCounts.SyncRows += m_RenderSyncTransformLog.size();
        if (++m_TransformCounts.Frames >= TransformCounts::kReportFrames)
        {
            const double frames = static_cast<double>(m_TransformCounts.Frames);
            LOG_INFO("ECS_TRANSFORM_COUNTS frames=%u seeds=%.1f processed=%.1f changed=%.1f render_rows=%.1f sync_rows=%.1f",
                     m_TransformCounts.Frames, m_TransformCounts.Seeds / frames, m_TransformCounts.Processed / frames,
                     m_TransformCounts.Changed / frames, m_TransformCounts.RenderRows / frames,
                     m_TransformCounts.SyncRows / frames);
            m_TransformCounts = TransformCounts{};
        }
#endif
        if (m_EcsWorld)
        {
            // 席の再利用の条件を取り込む（SceneView が無い間は消費者がいないものとして、解放した席を直ちに再利用する）。
            // 前の同期の後に解放した席は、ここで解放を運ぶパケットの番号が決まるので、削除を SceneView へ反映するより先に呼ぶ
            AdvanceGpuSceneSlots(*m_EcsWorld, m_SceneView);
        }
        if (!HasFlag(OF_Initialized) ||
            (!m_SceneView && !m_ScreenSpaceBoardSink))
        {
            m_RenderSyncTransformLog.clear();
            return;
        }

        // ComponentDataRegistry の捕捉は旧の全走査だけが行う（全行をフレームごとに公開し直す仕組みなので、
        // 既定の同期では消去も公開もしない）
        ComponentDataRegistry& componentDataRegistry = GEngine.GetComponentDataRegistry();
        ComponentDataRegistry* enabledComponentDataRegistry = (m_bLegacyRenderSync && componentDataRegistry.IsEnabled())
            ? &componentDataRegistry
            : nullptr;
        if (enabledComponentDataRegistry)
        {
            enabledComponentDataRegistry->BeginFrameCapture();
        }

        UpdateRenderTransforms();

        // MegaGeometry: 前回の同期で動いた proxy の前の変換を今の変換へ揃える（差分の同期のとき。履歴の落ち着き）
        if (m_SceneView && !m_SceneView->IsMegaFullSync())
        {
            m_SceneView->BeginMegaGeometrySync();
        }

        // 資源の変化を行へ知らせてから走査する（通知を受けた行は走査で proxy を作り直す）
        // 既定の同期では、検査 ECS_RENDER_SYNC も公開した内容の覚えを使う
        m_bInvalidationCheckFrame = IsRenderInvalidationCheckActive() ||
                                    (!m_bLegacyRenderSync && IsRenderSyncCheckActive());
        m_InvalidationFrameExamined = 0;
        m_InvalidationFrameViolations = 0;
        DrainRenderResourceChanges(materials, meshes);

        // 段 RenderSync の一覧: 変更ログ・通知・公開の状態から作る
        m_bRenderSyncCheckFrame = BeginRenderSyncFrame();
        if (!m_bLegacyRenderSync)
        {
            // 検査が止まっている間に覚えが古くなるので、止まっているフレームでは捨てる
            if (!m_bInvalidationCheckFrame && !m_InvalidationHashes.empty())
            {
                m_InvalidationHashes.clear();
            }

            // 変わった行だけを公開し、Remove の一覧だけを外す。Text は変換のキャッシュを求め直す行だけを見る。
            // Entity の木はたどらない（--mega-scene-full-sync だけ、MegaGeometry の全行を数えて外す）
            SyncRenderRowsDelta(materials, meshes);

            FinishRenderInvalidationFrame();
            return;
        }

        // 旧の全走査では、一覧を作って走査の結果と照合する
        if (m_bRenderSyncCheckFrame)
        {
            BuildRenderSyncList();
        }

        Container::UnorderedSet<uint64_t> liveMeshComponentIds;
        Container::UnorderedSet<uint64_t> liveSkinnedMeshComponentIds;
        Container::UnorderedSet<uint64_t> liveMegaGeometryObjectIds;
        Container::UnorderedSet<uint64_t> liveLightIds;
        Container::UnorderedSet<uint64_t> liveScreenBoardComponentIds;
        Container::UnorderedSet<uint64_t> liveWorldBoardComponentIds;
        liveMeshComponentIds.reserve(m_Inners.size());
        liveSkinnedMeshComponentIds.reserve(m_Inners.size());
        liveMegaGeometryObjectIds.reserve(m_Inners.size());
        liveLightIds.reserve(m_Inners.size());
        liveScreenBoardComponentIds.reserve(m_Inners.size());
        liveWorldBoardComponentIds.reserve(m_Inners.size());

        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            SyncEntityRecursive(
                *entity,
                materials,
                meshes,
                liveMeshComponentIds,
                liveSkinnedMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                enabledComponentDataRegistry);
        }

        if (m_bRenderSyncCheckFrame)
        {
            FinishRenderSyncFrame(liveMeshComponentIds, liveSkinnedMeshComponentIds, liveMegaGeometryObjectIds,
                                  liveLightIds, liveScreenBoardComponentIds, liveWorldBoardComponentIds);
        }

        if (m_SceneView)
        {
            m_SceneView->RemoveStaleMeshProxies(liveMeshComponentIds);
            m_SceneView->RemoveStaleSkinnedMeshProxies(liveSkinnedMeshComponentIds);
            m_SceneView->RemoveStaleMegaGeometryProxies(liveMegaGeometryObjectIds);
            m_SceneView->RemoveStaleLightProxies(liveLightIds);
            m_SceneView->RemoveStaleBoardProxies(liveWorldBoardComponentIds);
        }

        if (m_ScreenSpaceBoardSink)
        {
            m_ScreenSpaceBoardSink->RemoveStaleBoardProxies(liveScreenBoardComponentIds);
        }

        FinishRenderInvalidationFrame();
    }

    // ========================================
    // 描画の無効化の通知（設計 1.10）
    // ========================================

    Component::Component* World::ComponentOfRow(Ecs::EcsEntity row) const
    {
        if (!m_EcsWorld || row.IsNull() || !m_EcsWorld->IsAlive(row))
        {
            return nullptr;
        }
        const Ecs::EcsObjectRef* ref = m_EcsWorld->GetColumnValue<Ecs::EcsObjectRef>(row);
        if (ref == nullptr || ref->Object == nullptr || ref->Generation != row.Generation)
        {
            return nullptr;
        }
        return static_cast<Component::Component*>(ref->Object);
    }

    void World::NotifyRenderResourceChanged(Rendering::RenderResourceKind kind, uint64_t id)
    {
        const Rendering::RenderResourceRef ref{kind, id};
        auto usersIt = m_RenderResourceUsers.find(ref.Key());
        if (usersIt == m_RenderResourceUsers.end())
        {
            return;
        }
        Container::UnorderedSet<uint64_t>& users = usersIt->second;
        for (auto it = users.begin(); it != users.end();)
        {
            Component::Component* component = ComponentOfRow(UnpackEcsEntity(*it));
            if (component == nullptr)
            {
                // 行が消えている。逆引きから外す
                m_RenderRowRefKeys.erase(*it);
                it = users.erase(it);
                continue;
            }
            // 走査はこの印で proxy を作り直す。検査は通知を受けた行を「載っている」と数える
            component->MarkRenderStateDirty();
            m_RenderNotified.insert(component->GetComponentId());
            ++it;
        }
    }

    void World::NotifyRenderRepublishAll()
    {
        m_bRenderRepublishAll = true;
        auto roots = GetRootEntities();
        for (auto* entity : roots)
        {
            MarkEntitySubtreeRenderStateDirty(*entity);
        }
    }

    void World::SetRenderInvalidationCheckForced(bool bForced)
    {
        m_bInvalidationCheckForced = bForced;
        if (!bForced)
        {
            m_InvalidationHashes.clear();
        }
    }

    bool World::IsRenderInvalidationCheckActive() const
    {
        if (m_bInvalidationCheckForced)
        {
            return true;
        }
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        return checks != nullptr && checks->IsEnabled("ECS_RENDER_INVALIDATION");
    }

    void World::DrainRenderResourceChanges(const Rendering::MaterialResources* materials,
                                           const Rendering::MeshResources* meshes)
    {
        Rendering::ResourceChangeLog* logs[2] = {
            meshes != nullptr ? meshes->GetChangeLog() : nullptr,
            materials != nullptr ? materials->GetChangeLog() : nullptr,
        };
        for (int i = 0; i < 2; ++i)
        {
            Rendering::ResourceChangeLog* log = logs[i];
            if (log == nullptr || (i == 1 && log == logs[0]))
            {
                continue;
            }
            if (log->Drain(m_RenderRefScratch))
            {
                // 記録が溢れて個別の handle が分からない。全部を公開し直す
                NotifyRenderRepublishAll();
                continue;
            }
            for (const Rendering::RenderResourceRef& ref : m_RenderRefScratch)
            {
                NotifyRenderResourceChanged(ref.Kind, ref.Id);
            }
        }
    }

    World::RenderRowInfo World::ReadRenderRowInfo(const Component::Component& component, const Entity& owner) const
    {
        RenderRowInfo info;
        info.bNotified = m_bRenderRepublishAll ||
                         m_RenderNotified.find(component.GetComponentId()) != m_RenderNotified.end();
        if (!m_EcsWorld)
        {
            return info;
        }
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (row.IsNull() || !m_EcsWorld->IsAlive(row))
        {
            return info;
        }
        const Ecs::EcsPublishState* publish = m_EcsWorld->GetColumnValue<Ecs::EcsPublishState>(row);
        if (publish != nullptr)
        {
            info.bRenderTransformChanged = publish->RenderTransformVersion != owner.GetRenderTransformVersion();
        }
        if (const Ecs::EcsPoseVersion* pose = m_EcsWorld->GetColumnValue<Ecs::EcsPoseVersion>(row))
        {
            info.PoseSerial = pose->Serial;
            info.bPoseChanged = publish != nullptr && publish->PoseSerial != pose->Serial;
        }
        return info;
    }

    void World::RecordRenderPublish(const Component::Component& component, const Entity& owner, bool bPublished,
                                    uint64_t proxyHash, Ecs::EcsRenderSyncKind sink)
    {
        if (m_EcsWorld)
        {
            const Ecs::EcsEntity row = component.GetEcsEntity();
            if (!row.IsNull() && m_EcsWorld->IsAlive(row))
            {
                if (Ecs::EcsPublishState* publish = m_EcsWorld->GetColumnValue<Ecs::EcsPublishState>(row))
                {
                    publish->bPublished = bPublished;
                    if (bPublished)
                    {
                        ++publish->Version;
                        // 出している先と鍵を覚える（行が消えるとき、フックが Remove を積むのに使う）
                        publish->ProxyKey = RenderSyncProxyKey(component);
                        publish->Kind = static_cast<uint8_t>(sink != Ecs::EcsRenderSyncKind::None
                                                                 ? sink
                                                                 : RenderSyncKindOf(component));
                        publish->RenderTransformVersion = owner.GetRenderTransformVersion();
                        const Ecs::EcsPoseVersion* pose = m_EcsWorld->GetColumnValue<Ecs::EcsPoseVersion>(row);
                        publish->PoseSerial = pose != nullptr ? pose->Serial : 0;
                    }
                    else if (const Ecs::EcsPoseVersion* pose = m_EcsWorld->GetColumnValue<Ecs::EcsPoseVersion>(row))
                    {
                        // 出さなかった行も、見た時点のポーズの版を覚える（非表示の行が毎フレーム「ポーズが変わった」にならない）
                        publish->PoseSerial = pose->Serial;
                    }
                }
            }
        }
        if (!m_bInvalidationCheckFrame)
        {
            return;
        }
        if (bPublished)
        {
            m_InvalidationHashes[component.GetComponentId()] = proxyHash;
            ++m_InvalidationStats.Published;
            ++m_InvalidationFrameExamined;
        }
        else
        {
            m_InvalidationHashes.erase(component.GetComponentId());
        }
    }

    void World::NoteRenderRetry(const Component::Component& component)
    {
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (!row.IsNull())
        {
            m_RenderRetryNext.push_back(row);
        }
    }

    void World::RegisterRenderResourceUsers(const Component::Component& component)
    {
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (row.IsNull())
        {
            return;
        }
        const uint64_t packed = PackEcsEntity(row);

        // 交換前の資源の集合から、この行を外す（外さないと、交換前の資源の変化でこの行が無関係に通知を受ける）
        auto oldIt = m_RenderRowRefKeys.find(packed);
        if (oldIt != m_RenderRowRefKeys.end())
        {
            for (const uint64_t oldKey : oldIt->second)
            {
                auto usersIt = m_RenderResourceUsers.find(oldKey);
                if (usersIt == m_RenderResourceUsers.end())
                {
                    continue;
                }
                usersIt->second.erase(packed);
                if (usersIt->second.empty())
                {
                    m_RenderResourceUsers.erase(usersIt);
                }
            }
            oldIt->second.clear();
        }

        m_RenderRefScratch.clear();
        component.CollectRenderResourceRefs(m_RenderRefScratch);
        if (m_RenderRefScratch.empty())
        {
            if (oldIt != m_RenderRowRefKeys.end())
            {
                m_RenderRowRefKeys.erase(oldIt);
            }
            return;
        }
        Container::VariableArray<uint64_t>& keys = m_RenderRowRefKeys[packed];
        for (const Rendering::RenderResourceRef& ref : m_RenderRefScratch)
        {
            m_RenderResourceUsers[ref.Key()].insert(packed);
            keys.push_back(ref.Key());
        }
    }

    void World::CheckRenderRowUnchanged(const Component::Component& component, bool bBuilt, uint64_t proxyHash)
    {
        ++m_InvalidationStats.Probed;
        ++m_InvalidationFrameExamined;
        const auto shadowIt = m_InvalidationHashes.find(component.GetComponentId());
        const bool bHadShadow = shadowIt != m_InvalidationHashes.end();
        const bool bSame = bBuilt ? (bHadShadow && shadowIt->second == proxyHash) : !bHadShadow;
        if (bSame)
        {
            return;
        }
        ++m_InvalidationStats.Violations;
        ++m_InvalidationFrameViolations;
        if (m_InvalidationStats.Violations <= 8)
        {
            LOG_ERROR("ECS_RENDER_INVALIDATION: 変更も通知も無いのに proxy が変わった (component=%llu, 作れた=%d, 公開済み=%d)",
                      static_cast<unsigned long long>(component.GetComponentId()), bBuilt ? 1 : 0, bHadShadow ? 1 : 0);
        }
    }

    void World::FinishRenderInvalidationFrame()
    {
        if (m_bInvalidationCheckFrame && m_EcsWorld)
        {
            // 再試行の一覧に入った行は、公開済みの状態であってはならない
            for (const Ecs::EcsEntity row : m_RenderRetryNext)
            {
                const Ecs::EcsPublishState* publish =
                    m_EcsWorld->IsAlive(row) ? m_EcsWorld->GetColumnValue<Ecs::EcsPublishState>(row) : nullptr;
                if (publish != nullptr && publish->bPublished)
                {
                    ++m_InvalidationStats.RetryMismatches;
                    ++m_InvalidationFrameViolations;
                }
            }
        }
        m_RenderRetryRows.swap(m_RenderRetryNext);
        m_RenderRetryNext.clear();
        // 通知は 1 回の同期で使い切る。公開しても前フレームの変換が落ち着いていない行は、次の同期の通知になる
        m_RenderNotified.swap(m_RenderSettleNext);
        m_RenderSettleNext.clear();
        m_bRenderRepublishAll = false;

        if (!m_bInvalidationCheckFrame)
        {
            return;
        }
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_RENDER_INVALIDATION"))
        {
            return;
        }
        ++m_InvalidationCalls;
        const bool bPass = m_InvalidationFrameViolations == 0;
        // 呼び出しが多いので、fail は毎回、pass は最初に行を照合できた回と 64 回ごとに出す
        const bool bReportPass = bPass && m_InvalidationFrameExamined > 0 &&
                                 (m_InvalidationLastReport == 0 || m_InvalidationCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_RENDER_INVALIDATION", bPass, m_InvalidationFrameExamined);
            m_InvalidationLastReport = m_InvalidationCalls;
        }
    }

    // ========================================
    // 段 RenderSync の一覧（設計 1.10。準備: 同期は全走査のまま、一覧と全走査を照合する）
    // ========================================

    bool World::IsRenderSyncCheckActive() const
    {
        if (m_bRenderSyncCheckForced)
        {
            return true;
        }
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        return checks != nullptr && checks->IsEnabled("ECS_RENDER_SYNC");
    }

    void World::SetRenderSyncCheckForced(bool bForced)
    {
        m_bRenderSyncCheckForced = bForced;
        if (bForced)
        {
            if (!m_bRenderSyncLogEnabled)
            {
                m_bRenderSyncLogEnabled = true;
                m_RenderSyncDirtyLog.clear();
                m_RenderSyncTransformLog.clear();
                for (Entity* root : GetRootEntities())
                {
                    SeedRenderSyncLog(*root);
                }
            }
        }
        else if (m_bRenderSyncLogEnabled && !IsRenderSyncCheckActive() && m_bLegacyRenderSync)
        {
            m_bRenderSyncLogEnabled = false;
            m_RenderSyncDirtyLog.clear();
            m_RenderSyncTransformLog.clear();
            m_RenderSyncDestroyed.clear();
        }
    }

    void World::SeedRenderSyncLog(Entity& entity)
    {
        // 取り始める前から必要だった分を、変更ログの種として積む
        const Ecs::EcsEntity row = entity.GetEcsEntity();
        if (!row.IsNull())
        {
            m_RenderSyncTransformLog.push_back(PackEcsEntity(row));
        }
        for (Component::Component* component : entity.GetComponents())
        {
            if (component->IsRenderStateDirty() && !component->GetEcsEntity().IsNull())
            {
                m_RenderSyncDirtyLog.push_back(PackEcsEntity(component->GetEcsEntity()));
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            SeedRenderSyncLog(*child);
        }
    }

    bool World::BeginRenderSyncFrame()
    {
        // 旧の全走査では、検査が有効なときだけ変更ログを取る。既定の同期では常に取る
        const bool bCheck = IsRenderSyncCheckActive();
        const bool bActive = bCheck || !m_bLegacyRenderSync;
        if (bActive && !m_bRenderSyncLogEnabled)
        {
            m_bRenderSyncLogEnabled = true;
            m_RenderSyncDirtyLog.clear();
            m_RenderSyncTransformLog.clear();
            for (Entity* root : GetRootEntities())
            {
                SeedRenderSyncLog(*root);
            }
        }
        else if (!bActive && m_bRenderSyncLogEnabled)
        {
            m_bRenderSyncLogEnabled = false;
            m_RenderSyncDirtyLog.clear();
            m_RenderSyncTransformLog.clear();
            m_RenderSyncDestroyed.clear();
        }
        m_RenderSyncScanChanged.clear();
        return bCheck;
    }

    void World::NoteComponentRenderDirty(Ecs::EcsEntity row)
    {
        if (m_bRenderSyncLogEnabled && !row.IsNull())
        {
            m_RenderSyncDirtyLog.push_back(PackEcsEntity(row));
        }
    }

    void World::NoteRenderSyncTransform(Ecs::EcsEntity row)
    {
        if (m_bRenderSyncLogEnabled && !row.IsNull())
        {
            m_RenderSyncTransformLog.push_back(PackEcsEntity(row));
        }
    }

    void World::OnPublishStateDestroyed(void* user, Ecs::EcsEntity entity, const void* value, Ecs::EcsHookReason reason)
    {
        (void)entity;
        (void)reason;
        World* world = static_cast<World*>(user);
        const auto* state = static_cast<const Ecs::EcsPublishState*>(value);
        if (world == nullptr || state == nullptr || !world->m_bRenderSyncLogEnabled || !state->bPublished)
        {
            return;
        }
        world->m_RenderSyncDestroyed.insert(
            Ecs::EcsRenderSyncKey(static_cast<Ecs::EcsRenderSyncKind>(state->Kind), state->ProxyKey));
    }

    bool World::IsEntityRenderScanned(const Entity& entity) const
    {
        // 走査は、アクティブでない・破棄待ちの Entity の部分木に入らない
        for (const Entity* cursor = &entity; cursor != nullptr; cursor = cursor->GetParentEntity())
        {
            if (!cursor->IsActive() || cursor->IsPendingDestroy())
            {
                return false;
            }
        }
        return true;
    }

    Ecs::EcsRenderSyncKind World::RenderSyncKindOf(const Component::Component& component) const
    {
        if (CastTo<Component::SkinnedMeshComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::SkinnedMesh;
        }
        if (CastTo<Component::MegaGeometryComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::MegaGeometry;
        }
        if (CastTo<Component::MeshComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::Mesh;
        }
        if (CastTo<Component::LightComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::Light;
        }
        if (CastTo<Component::TextComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::None;
        }
        if (CastTo<Component::BoardComponent>(&component))
        {
            return Ecs::EcsRenderSyncKind::Board;
        }
        return Ecs::EcsRenderSyncKind::None;
    }

    uint64_t World::RenderSyncProxyKey(const Component::Component& component) const
    {
        if (CastTo<Component::MegaGeometryComponent>(&component))
        {
            const Entity* owner = component.GetOwner();
            return owner != nullptr ? owner->GetObjectId() : 0;
        }
        return component.GetComponentId();
    }

    void World::NoteRenderScanChanged(const Component::Component& component)
    {
        if (!m_bRenderSyncCheckFrame || !m_EcsWorld)
        {
            return;
        }
        // 行が付く前の Component は変更ログに載らない（付いたときに積む）。照合の対象にしない
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (row.IsNull() || !m_EcsWorld->IsAlive(row))
        {
            return;
        }
        const Ecs::EcsRenderSyncKind kind = RenderSyncKindOf(component);
        if (kind != Ecs::EcsRenderSyncKind::None)
        {
            m_RenderSyncScanChanged.insert(Ecs::EcsRenderSyncKey(kind, RenderSyncProxyKey(component)));
        }
    }

    void World::AddRenderSyncChanged(Component::Component& component, bool bFromSettle)
    {
        if (CastTo<Component::TextComponent>(&component) != nullptr)
        {
            AddRenderTextRow(component);
            return;
        }
        const Ecs::EcsRenderSyncKind kind = RenderSyncKindOf(component);
        if (kind == Ecs::EcsRenderSyncKind::None)
        {
            return;
        }
        // 板は SceneView が無くても画面空間の sink へ出す。他は SceneView が要る
        if (kind != Ecs::EcsRenderSyncKind::Board && m_SceneView == nullptr)
        {
            return;
        }
        const Entity* owner = component.GetOwner();
        if (owner == nullptr)
        {
            return;
        }
        if (!IsEntityRenderScanned(*owner))
        {
            // 走査に入らない。出していた proxy があれば Remove
            AddRenderSyncRemoval(component);
            return;
        }
        // 履歴が落ち着くまでの公開し直しは、変わった行ではない（公開する行の一覧にだけ入れる）
        if (!bFromSettle)
        {
            m_RenderSyncList.Changed.insert(Ecs::EcsRenderSyncKey(kind, RenderSyncProxyKey(component)));
        }
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (!row.IsNull() && m_RenderSyncChangedSeen.insert(PackEcsEntity(row)).second)
        {
            m_RenderSyncChangedRows.push_back(PackEcsEntity(row));
        }
    }

    void World::AddRenderTextRow(const Component::Component& component)
    {
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (!row.IsNull() && m_RenderTextSeen.insert(PackEcsEntity(row)).second)
        {
            m_RenderTextRows.push_back(PackEcsEntity(row));
        }
    }

    void World::RefreshRenderTextRows()
    {
        for (const uint64_t packed : m_RenderTextRows)
        {
            auto* text = CastTo<Component::TextComponent>(ComponentOfRow(UnpackEcsEntity(packed)));
            const Entity* owner = text != nullptr ? text->GetOwner() : nullptr;
            if (owner == nullptr || !IsEntityRenderScanned(*owner))
            {
                // 走査に入らない部分木の行は、旧の走査と同じく求め直さない。dirty の行はログに残り、
                // 再びアクティブになったときは dirty になって求め直す
                continue;
            }
            ++m_RenderSyncVisitedRows;
            text->RefreshRenderTransformCache();
            text->ClearRenderStateDirty();
            text->SetLastSyncedTransformVersion(owner->GetTransformVersion());
            // 旧の走査は毎フレーム求め直して前フレームの変換を今の変換に揃えていた。揃うまで次の同期でも求め直す
            if (!SameMatrixBits(text->GetPreviousWorldTransformCache(), text->GetWorldTransformCache()))
            {
                m_RenderTextSettleRows.push_back(packed);
            }
        }
        m_RenderTextRows.clear();
        m_RenderTextSeen.clear();
    }

    void World::AddRenderSyncRemoval(Component::Component& component)
    {
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (!m_EcsWorld || row.IsNull() || !m_EcsWorld->IsAlive(row))
        {
            return;
        }
        const Ecs::EcsPublishState* publish = m_EcsWorld->GetColumnValue<Ecs::EcsPublishState>(row);
        if (publish == nullptr || !publish->bPublished)
        {
            return;
        }
        m_RenderSyncList.Removed.insert(
            Ecs::EcsRenderSyncKey(static_cast<Ecs::EcsRenderSyncKind>(publish->Kind), publish->ProxyKey));
        m_RenderSyncUnpublishRows.push_back(PackEcsEntity(row));
    }

    void World::NoteSubtreeRenderRemoval(Entity& entity)
    {
        if (!m_bRenderSyncLogEnabled)
        {
            return;
        }
        for (Component::Component* component : entity.GetComponents())
        {
            const Ecs::EcsEntity row = component->GetEcsEntity();
            if (!row.IsNull())
            {
                m_RenderSyncDirtyLog.push_back(PackEcsEntity(row));
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            NoteSubtreeRenderRemoval(*child);
        }
    }

    void World::BuildRenderSyncList()
    {
        m_RenderSyncList.Clear();
        m_RenderSyncChangedRows.clear();
        m_RenderSyncChangedSeen.clear();
        m_RenderSyncUnpublishRows.clear();
        m_RenderTextRows.clear();
        m_RenderTextSeen.clear();
        if (!m_EcsWorld)
        {
            return;
        }

        // 板のフリップブックは、一覧を作る前に進めておく（進めた結果で描画の同期が必要になった板を変更ログに載せる）。
        // 走査での呼び出しは、すでに進めてあるので何もしない
        {
            Ecs::EcsQuery query;
            query.All.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsBoardRender>().Type);
            Container::VariableArray<Ecs::EcsChunk*> chunks;
            m_EcsWorld->CollectChunks(query, chunks);
            for (const Ecs::EcsChunk* chunk : chunks)
            {
                const Ecs::EcsEntity* rows = chunk->GetEntities();
                for (uint32_t i = 0; i < chunk->Count; ++i)
                {
                    Component::Component* component = ComponentOfRow(rows[i]);
                    if (component == nullptr || RenderSyncKindOf(*component) != Ecs::EcsRenderSyncKind::Board)
                    {
                        continue;
                    }
                    const Entity* owner = component->GetOwner();
                    if (owner != nullptr && IsEntityRenderScanned(*owner))
                    {
                        static_cast<Component::BoardComponent*>(component)->PrepareFlipbookForRenderSync();
                    }
                }
            }
        }

        // 変更ログ: 描画の同期が必要になった Component
        for (const uint64_t packed : m_RenderSyncDirtyLog)
        {
            if (Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed)))
            {
                AddRenderSyncChanged(*component);
            }
        }

        // 変更ログ: ワールドの変換の版が進んだ Entity の Component のうち、公開した版と違うもの
        for (const uint64_t packed : m_RenderSyncTransformLog)
        {
            Entity* entity = ResolveRenderEntity(UnpackEcsEntity(packed));
            if (entity == nullptr)
            {
                continue;
            }
            for (Component::Component* component : entity->GetComponents())
            {
                if (component->GetLastSyncedTransformVersion() != entity->GetTransformVersion())
                {
                    AddRenderSyncChanged(*component);
                }
            }
        }

        // 公開の状態: ポーズの版が公開した版と違う SkinnedMesh
        {
            const TypeId poseType = Ecs::EcsColumnInfoOf<Ecs::EcsPoseVersion>().Type;
            const TypeId publishType = Ecs::EcsColumnInfoOf<Ecs::EcsPublishState>().Type;
            Ecs::EcsQuery query;
            query.All.push_back(poseType);
            query.All.push_back(publishType);
            Container::VariableArray<Ecs::EcsChunk*> chunks;
            m_EcsWorld->CollectChunks(query, chunks);
            for (Ecs::EcsChunk* chunk : chunks)
            {
                const int32_t poseColumn = m_EcsWorld->FindColumnIndex(chunk->Archetype, poseType);
                const int32_t publishColumn = m_EcsWorld->FindColumnIndex(chunk->Archetype, publishType);
                if (poseColumn < 0 || publishColumn < 0)
                {
                    continue;
                }
                const Ecs::EcsChunk* constChunk = chunk;
                const auto* poses = static_cast<const Ecs::EcsPoseVersion*>(
                    m_EcsWorld->GetChunkColumn(constChunk, static_cast<uint32_t>(poseColumn)));
                const auto* states = static_cast<const Ecs::EcsPublishState*>(
                    m_EcsWorld->GetChunkColumn(constChunk, static_cast<uint32_t>(publishColumn)));
                const Ecs::EcsEntity* rows = chunk->GetEntities();
                for (uint32_t i = 0; i < chunk->Count; ++i)
                {
                    if (states[i].PoseSerial != poses[i].Serial)
                    {
                        if (Component::Component* component = ComponentOfRow(rows[i]))
                        {
                            AddRenderSyncChanged(*component);
                        }
                    }
                }
            }
        }

        // Remove: 行の削除のフックが積んだもの。走査に入らない部分木の行は、変更ログの行を見るときに載る
        for (const uint64_t packed : m_RenderSyncDestroyed)
        {
            m_RenderSyncList.Removed.insert(packed);
        }

        m_RenderSyncStats.ChangedListed += static_cast<uint32_t>(m_RenderSyncList.Changed.size());
        m_RenderSyncStats.RemovedListed += static_cast<uint32_t>(m_RenderSyncList.Removed.size());
    }

    void World::TrimRenderSyncLogs()
    {
        // 変換と削除は今回で使い切り、描画の値は、まだ同期が必要な行（走査に入らなかった行など）だけ残す
        m_RenderSyncTransformLog.clear();
        m_RenderSyncDestroyed.clear();
        Container::UnorderedSet<uint64_t> kept;
        size_t write = 0;
        for (const uint64_t packed : m_RenderSyncDirtyLog)
        {
            const Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed));
            if (component != nullptr && component->IsRenderStateDirty() && kept.insert(packed).second)
            {
                m_RenderSyncDirtyLog[write++] = packed;
            }
        }
        m_RenderSyncDirtyLog.resize(write);
    }

    void World::FinishRenderSyncFrame(const Container::UnorderedSet<uint64_t>& liveMesh,
                                      const Container::UnorderedSet<uint64_t>& liveSkinned,
                                      const Container::UnorderedSet<uint64_t>& liveMega,
                                      const Container::UnorderedSet<uint64_t>& liveLight,
                                      const Container::UnorderedSet<uint64_t>& liveScreenBoard,
                                      const Container::UnorderedSet<uint64_t>& liveWorldBoard)
    {
        using Kind = Ecs::EcsRenderSyncKind;
        Container::UnorderedSet<uint64_t> liveAfter;
        const auto addLive = [&liveAfter](Kind kind, const Container::UnorderedSet<uint64_t>& live)
        {
            for (const uint64_t key : live)
            {
                liveAfter.insert(Ecs::EcsRenderSyncKey(kind, key));
            }
        };
        addLive(Kind::Mesh, liveMesh);
        addLive(Kind::SkinnedMesh, liveSkinned);
        addLive(Kind::MegaGeometry, liveMega);
        addLive(Kind::Light, liveLight);
        addLive(Kind::ScreenBoard, liveScreenBoard);
        addLive(Kind::WorldBoard, liveWorldBoard);

        // 走査の終わりに外される proxy: SceneView が持っているのに、生きていないもの（画面空間の sink は数えられないので見ない）
        Container::UnorderedSet<uint64_t> scanStale;
        if (m_SceneView)
        {
            const auto addStale = [&scanStale](Kind kind, uint64_t key, const Container::UnorderedSet<uint64_t>& live)
            {
                if (live.find(key) == live.end())
                {
                    scanStale.insert(Ecs::EcsRenderSyncKey(kind, key));
                }
            };
            for (const Rendering::MeshProxy& proxy : m_SceneView->GetMeshProxies())
            {
                addStale(Kind::Mesh, proxy.ComponentId, liveMesh);
            }
            for (const Rendering::SkinnedMeshProxy& proxy : m_SceneView->GetSkinnedMeshProxies())
            {
                addStale(Kind::SkinnedMesh, proxy.ComponentId, liveSkinned);
            }
            for (const Rendering::MegaGeometryProxy& proxy : m_SceneView->GetMegaGeometryProxies())
            {
                // 散布の行の proxy は散布の Component が公開と取り下げを持つ（照合は検査 MASS_RANGES）
                if (Rendering::IsMegaScatterProxyKey(proxy.ObjectId))
                {
                    continue;
                }
                addStale(Kind::MegaGeometry, proxy.ObjectId, liveMega);
            }
            for (const Rendering::LightProxy& proxy : m_SceneView->GetLightProxies())
            {
                addStale(Kind::Light, proxy.LightId, liveLight);
            }
            for (const Rendering::BoardProxy& proxy : m_SceneView->GetBoardProxies())
            {
                addStale(Kind::WorldBoard, proxy.ComponentId, liveWorldBoard);
            }
        }

        const Ecs::EcsRenderSyncReport report =
            Ecs::EcsRenderSyncCompare(m_RenderSyncList, m_RenderSyncScanChanged, scanStale, liveAfter);
        ++m_RenderSyncStats.Checked;
        m_RenderSyncStats.Examined += report.Examined;
        m_RenderSyncStats.Violations += report.Violations();
        if (report.Violations() != 0 && m_RenderSyncStats.Violations <= 8)
        {
            LOG_ERROR("ECS_RENDER_SYNC: 変わった行・Remove の一覧が全走査と食い違う (変更の欠け=%u, 余り=%u, 外れた proxy の説明なし=%u, Remove なのに生存=%u): %s",
                      report.ChangedMissing, report.ChangedExtra, report.RemovedUnexplained, report.RemovedStillLive,
                      report.FirstDetail);
        }

        TrimRenderSyncLogs();

        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_RENDER_SYNC"))
        {
            return;
        }
        ++m_RenderSyncCalls;
        const bool bPass = report.Violations() == 0;
        // 呼び出しが多いので、fail は毎回、pass は最初に行を照合できた回と 64 回ごとに出す
        const bool bReportPass = bPass && report.Examined > 0 &&
                                 (m_RenderSyncLastReport == 0 || m_RenderSyncCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_RENDER_SYNC", bPass, report.Examined);
            m_RenderSyncLastReport = m_RenderSyncCalls;
        }
    }

    // ========================================
    // 既定の同期（変わった行だけを公開し、Remove の一覧だけを外す）
    // ========================================

    void World::SyncRenderRowsDelta(const Rendering::MaterialResources* materials, const Rendering::MeshResources* meshes)
    {
        BuildRenderSyncList();

        // 前回の公開で履歴が落ち着いていない Mesh は、変わった行の後ろで公開し直す
        for (const uint64_t packed : m_RenderSettleRows)
        {
            if (Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed)))
            {
                AddRenderSyncChanged(*component, true);
            }
        }

        // 外す proxy が先。同じ鍵を公開し直す行があっても、外してから出せば残る
        ApplyRenderSyncRemovals();

        for (const uint64_t packed : m_RenderSyncChangedRows)
        {
            if (Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed)))
            {
                PublishRenderRow(*component, materials, meshes);
            }
        }
        m_RenderSyncVisitedRows += static_cast<uint32_t>(m_RenderSyncChangedRows.size() + m_RenderSyncList.Removed.size());

        // Text: 前回の同期で前フレームの変換が揃っていない行を足してから、変換のキャッシュを求め直す。
        // 変更ログの整理より先に dirty を落とす（落とした行はログから外れる）
        for (const uint64_t packed : m_RenderTextSettleRows)
        {
            if (const Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed)))
            {
                AddRenderTextRow(*component);
            }
        }
        m_RenderTextSettleRows.clear();
        RefreshRenderTextRows();

        // 全量の同期（--mega-scene-full-sync）: MegaGeometry の ECS の全行を数え、変わっていない行も公開し直して、
        // 生存しない proxy を外す。差分の同期の結果と見比べる基準（Object の走査には戻らない）
        if (m_SceneView != nullptr && m_SceneView->IsMegaFullSync() && m_EcsWorld)
        {
            Container::UnorderedSet<uint64_t> liveMegaObjectIds;
            Ecs::EcsQuery query;
            query.All.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsMegaMesh>().Type);
            Container::VariableArray<Ecs::EcsChunk*> chunks;
            m_EcsWorld->CollectChunks(query, chunks);
            for (const Ecs::EcsChunk* chunk : chunks)
            {
                const Ecs::EcsEntity* rows = chunk->GetEntities();
                for (uint32_t i = 0; i < chunk->Count; ++i)
                {
                    auto* megaComp = CastTo<Component::MegaGeometryComponent>(ComponentOfRow(rows[i]));
                    const Entity* owner = megaComp != nullptr ? megaComp->GetOwner() : nullptr;
                    if (owner == nullptr || !IsEntityRenderScanned(*owner))
                    {
                        continue;
                    }
                    liveMegaObjectIds.insert(owner->GetObjectId());
                    PublishRenderRow(*megaComp, materials, meshes);
                    ++m_RenderSyncVisitedRows;
                }
            }
            m_SceneView->RemoveStaleMegaGeometryProxies(liveMegaObjectIds);
        }

        if (m_bRenderSyncCheckFrame || m_bInvalidationCheckFrame)
        {
            VerifyRenderSyncDelta(materials, meshes);
        }

        TrimRenderSyncLogs();
        m_RenderSettleRows.swap(m_RenderSettleNextRows);
        m_RenderSettleNextRows.clear();
    }

    void World::ApplyRenderSyncRemovals()
    {
        using Kind = Ecs::EcsRenderSyncKind;
        for (const uint64_t packed : m_RenderSyncList.Removed)
        {
            const uint64_t key = Ecs::EcsRenderSyncKeyOf(packed);
            switch (Ecs::EcsRenderSyncKindOf(packed))
            {
            case Kind::Mesh:
                if (m_SceneView)
                {
                    m_SceneView->RemoveMeshProxy(key);
                }
                break;
            case Kind::SkinnedMesh:
                if (m_SceneView)
                {
                    m_SceneView->RemoveSkinnedMeshProxy(key);
                }
                break;
            case Kind::Light:
                if (m_SceneView)
                {
                    m_SceneView->RemoveLightProxy(key);
                }
                break;
            case Kind::WorldBoard:
                if (m_SceneView)
                {
                    m_SceneView->RemoveBoardProxy(key);
                }
                break;
            case Kind::ScreenBoard:
                if (m_ScreenSpaceBoardSink)
                {
                    m_ScreenSpaceBoardSink->RemoveBoardProxy(key);
                }
                break;
            case Kind::MegaGeometry:
                if (m_SceneView)
                {
                    m_SceneView->RemoveMegaGeometryProxy(key);
                }
                break;
            default:
                break;
            }
        }

        // 走査に入らない部分木の行は、公開していない状態へ戻す（再びアクティブにすると、dirty になって公開し直す）
        for (const uint64_t packed : m_RenderSyncUnpublishRows)
        {
            Component::Component* component = ComponentOfRow(UnpackEcsEntity(packed));
            Entity* owner = component != nullptr ? component->GetOwner() : nullptr;
            if (component != nullptr && owner != nullptr)
            {
                RecordRenderPublish(*component, *owner, false, 0);
            }
        }
    }

    void World::PublishRenderRow(Component::Component& component, const Rendering::MaterialResources* materials,
                                 const Rendering::MeshResources* meshes)
    {
        Entity* owner = component.GetOwner();
        if (owner == nullptr)
        {
            return;
        }
        Entity& entity = *owner;
        const uint64_t ownerVersion = entity.GetTransformVersion();
        const bool bCheckInvalidation = m_bInvalidationCheckFrame;

        if (auto* skinnedMeshComp = CastTo<Component::SkinnedMeshComponent>(&component))
        {
            if (m_SceneView == nullptr)
            {
                return;
            }
            if (!skinnedMeshComp->IsVisible())
            {
                // 非表示は公開しない状態として受理済みで、再試行すべき失敗ではない
                skinnedMeshComp->ClearRenderStateDirty();
                skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                RecordRenderPublish(*skinnedMeshComp, entity, false, 0);
                m_SceneView->RemoveSkinnedMeshProxy(skinnedMeshComp->GetComponentId());
                return;
            }
            Rendering::SkinnedMeshProxy skinnedMeshProxy;
            if (skinnedMeshComp->BuildSkinnedMeshProxy(skinnedMeshProxy))
            {
                skinnedMeshProxy.ObjectId = entity.GetObjectId();
                skinnedMeshProxy.ComponentId = skinnedMeshComp->GetComponentId();
                m_SceneView->UpdateSkinnedMeshProxy(skinnedMeshProxy);
                skinnedMeshComp->ClearRenderStateDirty();
                skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                RecordRenderPublish(*skinnedMeshComp, entity, true,
                                    bCheckInvalidation ? Rendering::HashProxy(skinnedMeshProxy) : 0);
            }
            else
            {
                // 公開したいのに proxy を作れなかった（資産の読み込み待ちなど）。dirty のまま残し、次の同期でもう一度試す
                RecordRenderPublish(*skinnedMeshComp, entity, false, 0);
                NoteRenderRetry(*skinnedMeshComp);
                m_SceneView->RemoveSkinnedMeshProxy(skinnedMeshComp->GetComponentId());
            }
            RegisterRenderResourceUsers(*skinnedMeshComp);
            return;
        }

        if (auto* megaComp = CastTo<Component::MegaGeometryComponent>(&component))
        {
            if (m_SceneView == nullptr)
            {
                return;
            }
            megaComp->RefreshRenderTransformCache();

            Rendering::MegaGeometryProxy megaProxy;
            if (megaComp->BuildMegaGeometryProxy(megaProxy))
            {
                megaProxy.ObjectId = entity.GetObjectId();
                megaProxy.ComponentId = megaComp->GetComponentId();
                // 内容の覚えは席と前の変換を書く前の値で取る（検査が作り直した proxy と比べるため）
                const uint64_t proxyHash = bCheckInvalidation ? Rendering::HashProxy(megaProxy) : 0;
                PublishMegaProxyToView(*m_SceneView, m_EcsWorld.get(), megaComp->GetEcsEntity(), megaProxy);
                RecordRenderPublish(*megaComp, entity, true, proxyHash);
            }
            else
            {
                RecordRenderPublish(*megaComp, entity, false, 0);
                m_SceneView->RemoveMegaGeometryProxy(entity.GetObjectId());
            }

            megaComp->ClearRenderStateDirty();
            megaComp->SetLastSyncedTransformVersion(ownerVersion);
            return;
        }

        if (auto* meshComp = CastTo<Component::MeshComponent>(&component))
        {
            if (m_SceneView == nullptr)
            {
                return;
            }
            meshComp->RefreshRenderTransformCache();

            Rendering::MeshProxy meshProxy;
            if (meshComp->BuildMeshProxy(meshProxy, materials, meshes))
            {
                meshProxy.ObjectId = entity.GetObjectId();
                meshProxy.ComponentId = meshComp->GetComponentId();
                m_SceneView->UpdateMeshProxy(meshProxy);
                RecordRenderPublish(*meshComp, entity, true, bCheckInvalidation ? Rendering::HashProxy(meshProxy) : 0);
                // 前フレームの変換が現在と違う間は、次の同期で公開し直して履歴を現在に揃える
                if (!SameMatrixBits(meshProxy.PreviousWorldTransform, meshProxy.WorldTransform))
                {
                    m_RenderSettleNext.insert(meshComp->GetComponentId());
                    m_RenderSettleNextRows.push_back(PackEcsEntity(meshComp->GetEcsEntity()));
                }
            }
            else
            {
                RecordRenderPublish(*meshComp, entity, false, 0);
                m_SceneView->RemoveMeshProxy(meshComp->GetComponentId());
            }
            RegisterRenderResourceUsers(*meshComp);

            meshComp->ClearRenderStateDirty();
            meshComp->SetLastSyncedTransformVersion(ownerVersion);
            meshComp->CommitRenderTransformHistory();
            return;
        }

        if (auto* lightComp = CastTo<Component::LightComponent>(&component))
        {
            if (m_SceneView == nullptr)
            {
                return;
            }
            Rendering::LightProxy lightProxy;
            if (lightComp->BuildLightProxy(lightProxy))
            {
                lightProxy.LightId = lightComp->GetComponentId();
                m_SceneView->UpdateLightProxy(lightProxy);
                RecordRenderPublish(*lightComp, entity, true, bCheckInvalidation ? Rendering::HashProxy(lightProxy) : 0);
            }
            else
            {
                RecordRenderPublish(*lightComp, entity, false, 0);
                m_SceneView->RemoveLightProxy(lightComp->GetComponentId());
            }

            lightComp->ClearRenderStateDirty();
            lightComp->SetLastSyncedTransformVersion(ownerVersion);
            return;
        }

        if (CastTo<Component::TextComponent>(&component) != nullptr)
        {
            return;
        }

        if (auto* boardComp = CastTo<Component::BoardComponent>(&component))
        {
            boardComp->PrepareFlipbookForRenderSync();
            boardComp->RefreshRenderTransformCache();

            Rendering::BoardProxy boardProxy;
            if (boardComp->BuildBoardProxy(boardProxy))
            {
                boardProxy.ObjectId = entity.GetObjectId();
                boardProxy.ComponentId = boardComp->GetComponentId();
                RecordRenderPublish(*boardComp, entity, true,
                                    bCheckInvalidation ? Rendering::HashProxy(boardProxy) : 0,
                                    boardProxy.Space == Rendering::BoardSpace::WorldSpace
                                        ? Ecs::EcsRenderSyncKind::WorldBoard
                                        : Ecs::EcsRenderSyncKind::ScreenBoard);
                if (boardProxy.Space == Rendering::BoardSpace::WorldSpace)
                {
                    if (m_SceneView)
                    {
                        m_SceneView->UpdateBoardProxy(boardProxy);
                    }
                    if (m_ScreenSpaceBoardSink)
                    {
                        m_ScreenSpaceBoardSink->RemoveBoardProxy(boardProxy.ComponentId);
                    }
                }
                else
                {
                    if (m_ScreenSpaceBoardSink)
                    {
                        m_ScreenSpaceBoardSink->UpdateBoardProxy(boardProxy.ComponentId, boardProxy);
                    }
                    if (m_SceneView)
                    {
                        m_SceneView->RemoveBoardProxy(boardProxy.ComponentId);
                    }
                }
            }
            else
            {
                RecordRenderPublish(*boardComp, entity, false, 0);
                if (m_ScreenSpaceBoardSink)
                {
                    m_ScreenSpaceBoardSink->RemoveBoardProxy(boardComp->GetComponentId());
                }
                if (m_SceneView)
                {
                    m_SceneView->RemoveBoardProxy(boardComp->GetComponentId());
                }
            }

            boardComp->ClearRenderStateDirty();
            boardComp->SetLastSyncedTransformVersion(ownerVersion);
        }
    }

    void World::VerifyRenderSyncDelta(const Rendering::MaterialResources* materials, const Rendering::MeshResources* meshes)
    {
        if (m_SceneView == nullptr)
        {
            return;
        }

        // SceneView が持っている proxy の鍵
        Container::UnorderedSet<uint64_t> viewMesh;
        Container::UnorderedSet<uint64_t> viewSkinned;
        Container::UnorderedSet<uint64_t> viewLight;
        Container::UnorderedSet<uint64_t> viewBoard;
        Container::UnorderedSet<uint64_t> viewMega;
        for (const Rendering::MegaGeometryProxy& proxy : m_SceneView->GetMegaGeometryProxies())
        {
            // 散布の行の proxy は散布の Component が公開と取り下げを持つ（照合は検査 MASS_RANGES）
            if (Rendering::IsMegaScatterProxyKey(proxy.ObjectId))
            {
                continue;
            }
            viewMega.insert(proxy.ObjectId);
        }
        for (const Rendering::MeshProxy& proxy : m_SceneView->GetMeshProxies())
        {
            viewMesh.insert(proxy.ComponentId);
        }
        for (const Rendering::SkinnedMeshProxy& proxy : m_SceneView->GetSkinnedMeshProxies())
        {
            viewSkinned.insert(proxy.ComponentId);
        }
        for (const Rendering::LightProxy& proxy : m_SceneView->GetLightProxies())
        {
            viewLight.insert(proxy.LightId);
        }
        for (const Rendering::BoardProxy& proxy : m_SceneView->GetBoardProxies())
        {
            viewBoard.insert(proxy.ComponentId);
        }

        // 履歴が落ち着くまでの公開し直し待ちの行は、前フレームの変換だけが違う。内容の比較から外す
        Container::UnorderedSet<uint64_t> settling;
        for (const uint64_t packed : m_RenderSettleNextRows)
        {
            settling.insert(packed);
        }

        uint32_t examined = 0;
        uint32_t violations = 0;
        char detail[160] = {};
        const auto fail = [&](const char* what, const Component::Component& component)
        {
            ++violations;
            if (detail[0] == '\0')
            {
                std::snprintf(detail, sizeof(detail), "%s (component=%llu)", what,
                              static_cast<unsigned long long>(component.GetComponentId()));
            }
        };
        const auto contentDiffers = [this](const Component::Component& component, uint64_t builtHash)
        {
            // 公開した内容の覚えがあるときだけ比べる（検査を途中から有効にした場合は、覚えが無い）
            const auto shadowIt = m_InvalidationHashes.find(component.GetComponentId());
            return shadowIt != m_InvalidationHashes.end() && shadowIt->second != builtHash;
        };

        Container::UnorderedSet<uint64_t> expectMesh;
        Container::UnorderedSet<uint64_t> expectSkinned;
        Container::UnorderedSet<uint64_t> expectLight;
        Container::UnorderedSet<uint64_t> expectBoard;
        Container::UnorderedSet<uint64_t> expectMega;

        Container::VariableArray<Entity*> stack;
        for (Entity* root : GetRootEntities())
        {
            stack.push_back(root);
        }
        while (!stack.empty())
        {
            Entity* entity = stack.back();
            stack.pop_back();
            if (!entity->IsActive() || entity->IsPendingDestroy())
            {
                continue;
            }
            const uint64_t ownerVersion = entity->GetTransformVersion();
            for (Component::Component* comp : entity->GetComponents())
            {
                const Ecs::EcsEntity row = comp->GetEcsEntity();
                const bool bSettling = !row.IsNull() && settling.find(PackEcsEntity(row)) != settling.end();

                if (auto* skinned = CastTo<Component::SkinnedMeshComponent>(comp))
                {
                    if (!skinned->IsVisible())
                    {
                        if (viewSkinned.find(skinned->GetComponentId()) != viewSkinned.end())
                        {
                            ++examined;
                            fail("非表示の SkinnedMesh の proxy が残っている", *skinned);
                        }
                        continue;
                    }
                    if (!skinned->IsPoseCurrent())
                    {
                        // 姿勢が最新でない行は、構築が姿勢を求め直して状態を変えるので作り直せない
                        expectSkinned.insert(skinned->GetComponentId());
                        continue;
                    }
                    ++examined;
                    Rendering::SkinnedMeshProxy probe;
                    const bool bBuilt = skinned->BuildSkinnedMeshProxy(probe);
                    const bool bPresent = viewSkinned.find(skinned->GetComponentId()) != viewSkinned.end();
                    if (bBuilt)
                    {
                        expectSkinned.insert(skinned->GetComponentId());
                        probe.ObjectId = entity->GetObjectId();
                        probe.ComponentId = skinned->GetComponentId();
                        if (!bPresent)
                        {
                            fail("SkinnedMesh の proxy が公開されていない", *skinned);
                        }
                        else if (contentDiffers(*skinned, Rendering::HashProxy(probe)))
                        {
                            fail("SkinnedMesh の proxy が公開した内容と違う", *skinned);
                        }
                    }
                    else if (bPresent)
                    {
                        fail("作れない SkinnedMesh の proxy が残っている", *skinned);
                    }
                    continue;
                }

                if (auto* mesh = CastTo<Component::MeshComponent>(comp))
                {
                    if (auto* mega = CastTo<Component::MegaGeometryComponent>(mesh))
                    {
                        ++examined;
                        if (mega->IsRenderStateDirty() || mega->GetLastSyncedTransformVersion() != ownerVersion)
                        {
                            fail("MegaGeometry が同期されていない", *mega);
                            continue;
                        }
                        Rendering::MegaGeometryProxy probe;
                        const bool bBuilt = mega->BuildMegaGeometryProxy(probe);
                        const bool bPresent = viewMega.find(entity->GetObjectId()) != viewMega.end();
                        if (bBuilt)
                        {
                            expectMega.insert(entity->GetObjectId());
                            probe.ObjectId = entity->GetObjectId();
                            probe.ComponentId = mega->GetComponentId();
                            if (!bPresent)
                            {
                                fail("MegaGeometry の proxy が公開されていない", *mega);
                            }
                            else if (contentDiffers(*mega, Rendering::HashProxy(probe)))
                            {
                                fail("MegaGeometry の proxy が公開した内容と違う", *mega);
                            }
                        }
                        else if (bPresent)
                        {
                            fail("作れない MegaGeometry の proxy が残っている", *mega);
                        }
                        continue;
                    }
                    ++examined;
                    if (mesh->IsRenderStateDirty() || mesh->GetLastSyncedTransformVersion() != ownerVersion)
                    {
                        fail("Mesh が同期されていない", *mesh);
                        continue;
                    }
                    Rendering::MeshProxy probe;
                    const bool bBuilt = mesh->BuildMeshProxy(probe, materials, meshes);
                    const bool bPresent = viewMesh.find(mesh->GetComponentId()) != viewMesh.end();
                    if (bBuilt)
                    {
                        expectMesh.insert(mesh->GetComponentId());
                        probe.ObjectId = entity->GetObjectId();
                        probe.ComponentId = mesh->GetComponentId();
                        if (!bPresent)
                        {
                            fail("Mesh の proxy が公開されていない", *mesh);
                        }
                        else if (!bSettling && contentDiffers(*mesh, Rendering::HashProxy(probe)))
                        {
                            fail("Mesh の proxy が公開した内容と違う", *mesh);
                        }
                    }
                    else if (bPresent)
                    {
                        fail("作れない Mesh の proxy が残っている", *mesh);
                    }
                    continue;
                }

                if (auto* light = CastTo<Component::LightComponent>(comp))
                {
                    ++examined;
                    if (light->IsRenderStateDirty() || light->GetLastSyncedTransformVersion() != ownerVersion)
                    {
                        fail("Light が同期されていない", *light);
                        continue;
                    }
                    Rendering::LightProxy probe;
                    const bool bBuilt = light->BuildLightProxy(probe);
                    const bool bPresent = viewLight.find(light->GetComponentId()) != viewLight.end();
                    if (bBuilt)
                    {
                        expectLight.insert(light->GetComponentId());
                        probe.LightId = light->GetComponentId();
                        if (!bPresent)
                        {
                            fail("Light の proxy が公開されていない", *light);
                        }
                        else if (contentDiffers(*light, Rendering::HashProxy(probe)))
                        {
                            fail("Light の proxy が公開した内容と違う", *light);
                        }
                    }
                    else if (bPresent)
                    {
                        fail("作れない Light の proxy が残っている", *light);
                    }
                    continue;
                }

                if (CastTo<Component::TextComponent>(comp) != nullptr)
                {
                    continue;
                }

                if (auto* board = CastTo<Component::BoardComponent>(comp))
                {
                    ++examined;
                    if (board->IsRenderStateDirty() || board->GetLastSyncedTransformVersion() != ownerVersion)
                    {
                        fail("Board が同期されていない", *board);
                        continue;
                    }
                    Rendering::BoardProxy probe;
                    const bool bBuilt = board->BuildBoardProxy(probe);
                    const bool bPresent = viewBoard.find(board->GetComponentId()) != viewBoard.end();
                    const bool bWorld = bBuilt && probe.Space == Rendering::BoardSpace::WorldSpace;
                    if (bWorld)
                    {
                        expectBoard.insert(board->GetComponentId());
                        probe.ObjectId = entity->GetObjectId();
                        probe.ComponentId = board->GetComponentId();
                        if (!bPresent)
                        {
                            fail("Board の proxy が公開されていない", *board);
                        }
                        else if (contentDiffers(*board, Rendering::HashProxy(probe)))
                        {
                            fail("Board の proxy が公開した内容と違う", *board);
                        }
                    }
                    else if (bPresent)
                    {
                        fail("World に出さない Board の proxy が残っている", *board);
                    }
                }
            }
            for (Entity* child : entity->GetChildEntities())
            {
                stack.push_back(child);
            }
        }

        // 行が無いのに残っている proxy
        const auto countStale = [&](const Container::UnorderedSet<uint64_t>& view,
                                    const Container::UnorderedSet<uint64_t>& expect, const char* what)
        {
            for (const uint64_t id : view)
            {
                ++examined;
                if (expect.find(id) == expect.end())
                {
                    ++violations;
                    if (detail[0] == '\0')
                    {
                        std::snprintf(detail, sizeof(detail), "%s (component=%llu)", what,
                                      static_cast<unsigned long long>(id));
                    }
                }
            }
        };
        countStale(viewMesh, expectMesh, "行が無いのに Mesh の proxy が残っている");
        countStale(viewSkinned, expectSkinned, "行が無いのに SkinnedMesh の proxy が残っている");
        countStale(viewLight, expectLight, "行が無いのに Light の proxy が残っている");
        countStale(viewBoard, expectBoard, "行が無いのに Board の proxy が残っている");
        countStale(viewMega, expectMega, "行が無いのに MegaGeometry の proxy が残っている");

        ++m_RenderSyncStats.Checked;
        m_RenderSyncStats.Examined += examined;
        m_RenderSyncStats.Violations += violations;
        m_RenderSyncStats.ChangedListed += static_cast<uint32_t>(m_RenderSyncList.Changed.size());
        m_RenderSyncStats.RemovedListed += static_cast<uint32_t>(m_RenderSyncList.Removed.size());
        if (violations != 0 && m_RenderSyncStats.Violations <= 8)
        {
            LOG_ERROR("ECS_RENDER_SYNC: 変わった行だけの同期の結果が、全行から求め直した proxy と食い違う (件数=%u): %s",
                      violations, detail);
        }
        if (m_bInvalidationCheckFrame)
        {
            m_InvalidationFrameExamined += examined;
            m_InvalidationFrameViolations += violations;
            m_InvalidationStats.Violations += violations;
        }

        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_RENDER_SYNC"))
        {
            return;
        }
        ++m_RenderSyncCalls;
        const bool bPass = violations == 0;
        // 呼び出しが多いので、fail は毎回、pass は最初に行を照合できた回と 64 回ごとに出す
        const bool bReportPass = bPass && examined > 0 && (m_RenderSyncLastReport == 0 || m_RenderSyncCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_RENDER_SYNC", bPass, examined);
            m_RenderSyncLastReport = m_RenderSyncCalls;
        }
    }

    void World::CollectTransientBoardProxies(Container::VariableArray<Rendering::BoardProxy>& outProxies)
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        if (m_bLegacyRenderSync)
        {
            auto roots = GetRootEntities();
            for (Entity* entity : roots)
            {
                CollectTransientBoardProxiesRecursive(*entity, outProxies);
            }
            return;
        }

        // Text の行の一覧だけを見る。板の並びは旧の走査（木の深さ優先・Entity ごとに Component が子より先）と同じ
        if (m_TextBoardRows.empty())
        {
            return;
        }
        if (m_bTextBoardOrderDirty)
        {
            RebuildTextBoardOrder();
        }
        for (const uint64_t packed : m_TextBoardRows)
        {
            auto* text = CastTo<Component::TextComponent>(ComponentOfRow(UnpackEcsEntity(packed)));
            if (text == nullptr)
            {
                // 行が外れた。次の収集で一覧から除く
                m_bTextBoardOrderDirty = true;
                continue;
            }
            const Entity* owner = text->GetOwner();
            if (owner == nullptr || owner->GetWorld() != this || !IsEntityRenderScanned(*owner))
            {
                // 木の外・アクティブでない・破棄待ちの部分木の Text は出さない
                continue;
            }
            text->BuildGlyphBoardProxies(outProxies);
        }
    }

    void World::RegisterTextBoardRow(const Component::Component& component)
    {
        if (CastTo<Component::TextComponent>(&component) == nullptr)
        {
            return;
        }
        const Ecs::EcsEntity row = component.GetEcsEntity();
        if (!row.IsNull() && m_TextBoardSeen.insert(PackEcsEntity(row)).second)
        {
            m_TextBoardRows.push_back(PackEcsEntity(row));
            m_bTextBoardOrderDirty = true;
        }
    }

    void World::RebuildTextBoardOrder()
    {
        m_bTextBoardOrderDirty = false;

        // 生きている Text と、根からの道（各段の Outer の Inner の中の位置）を求める。
        // 道の 1 段は (種別 << 32 | 位置) で、Component(0) は同じ Entity の子(1)より先に並ぶ
        struct TextBoardEntry
        {
            uint64_t Packed = 0;
            bool bInWorld = false;
            Container::VariableArray<uint64_t> Path;
        };
        Container::VariableArray<TextBoardEntry> entries;
        entries.reserve(m_TextBoardRows.size());
        Container::UnorderedMap<const IUnknown*, uint32_t> positions;
        Container::VariableArray<const IUnknown*> outers;
        Container::UnorderedSet<const IUnknown*> outerSeen;
        for (const uint64_t packed : m_TextBoardRows)
        {
            const Component::Component* component = CastTo<Component::TextComponent>(ComponentOfRow(UnpackEcsEntity(packed)));
            if (component == nullptr)
            {
                continue;
            }
            TextBoardEntry entry;
            entry.Packed = packed;
            entry.bInWorld = false;
            for (const IUnknown* node = component; node != nullptr; node = node->GetOuter())
            {
                const IUnknown* outer = node->GetOuter();
                if (outer == nullptr)
                {
                    break;
                }
                positions.emplace(node, 0u);
                if (outerSeen.insert(outer).second)
                {
                    outers.push_back(outer);
                }
                if (outer == this)
                {
                    entry.bInWorld = true;
                    break;
                }
            }
            // 木から外れた Text も行が生きている間は一覧に残す（収集では出さない。付け直すと構造の変化で並べ直す）
            entries.push_back(std::move(entry));
        }

        // 道に現れる Outer ごとに Inner を 1 回だけたどり、要る節の位置を記す
        for (const IUnknown* outer : outers)
        {
            const auto& inners = outer->GetInners();
            for (size_t index = 0; index < inners.size(); ++index)
            {
                auto found = positions.find(inners[index]);
                if (found != positions.end())
                {
                    found->second = static_cast<uint32_t>(index);
                }
            }
        }

        for (TextBoardEntry& entry : entries)
        {
            if (!entry.bInWorld)
            {
                // 木の外の Text は末尾へ
                entry.Path.push_back(std::numeric_limits<uint64_t>::max());
                continue;
            }
            const IUnknown* component = ComponentOfRow(UnpackEcsEntity(entry.Packed));
            for (const IUnknown* node = component; node != this; node = node->GetOuter())
            {
                const uint64_t kind = node == component ? 0u : 1u;
                entry.Path.push_back((kind << 32) | positions[node]);
            }
            std::reverse(entry.Path.begin(), entry.Path.end());
        }
        std::stable_sort(entries.begin(), entries.end(), [](const TextBoardEntry& lhs, const TextBoardEntry& rhs)
                         { return std::lexicographical_compare(lhs.Path.begin(), lhs.Path.end(), rhs.Path.begin(), rhs.Path.end()); });

        m_TextBoardRows.clear();
        m_TextBoardSeen.clear();
        for (const TextBoardEntry& entry : entries)
        {
            m_TextBoardRows.push_back(entry.Packed);
            m_TextBoardSeen.insert(entry.Packed);
        }
    }

    void World::UpdateWorldTransforms()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }
        // 変換の伝播の区間（計測の方針の名前）
        NorvesLib::Debug::ScopedStat statScope("Ecs.Sync.TransformPropagate", "Ecs");

        // 同期点まで待たせた付け替えと作成予約（行がまだ無い Entity）がある間は、階層の列が Outer/Inner と食い違う。この間は Outer/Inner を正とする旧の再帰で求める
        const bool bLegacy = m_bLegacyTransformPropagation || !m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype ||
                             !m_PendingEntityRelinks.empty() || !m_PendingEntityCreates.empty();
        if (bLegacy)
        {
            auto roots = GetRootEntities();
            for (auto* entity : roots)
            {
                UpdateEntityTransformRecursive(*entity, Math::Transform::Identity);
            }
        }
        else
        {
            (void)PropagateEcsTransforms();
        }
        // 検査 ECS_TRANSFORM が有効なときだけ、全行を根から求め直した値と照合する
        RunEcsTransformCheck();
    }

    bool World::SetRenderInterpolationAlpha(float alpha)
    {
        if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
            return false;
        m_RenderInterpolationAlpha = alpha;
        return true;
    }
    namespace
    {
        /// 行の履歴を捨て、描画の変換を求め直させる（列が無ければ何もしない）
        void ResetRenderColumns(Ecs::EcsWorld& ecs, Ecs::EcsEntity row)
        {
            if (Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row))
            {
                history->History.Clear();
            }
            if (Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row))
            {
                render->bValid = 0;
            }
        }

        /// 描画の変換の列の値 ↔ Math::Transform
        Math::Transform LoadRenderTransform(const Ecs::EcsRenderTransform& column)
        {
            return Math::Transform(column.position, column.rotation, column.scale);
        }

        void StoreRenderTransform(Ecs::EcsRenderTransform& column, const Math::Transform& transform)
        {
            column.position = transform.position;
            column.rotation = transform.rotation;
            column.scale = transform.scale;
        }

        /// 検査 ECS_RENDER_TRANSFORM の比較（成分ごとに数値として等しいか。NaN は同じ bit のときだけ等しい）
        bool SameRenderTransform(const Math::Transform& lhs, const Math::Transform& rhs)
        {
            return Ecs::EcsEqualWorldTransform(Ecs::EcsWorldTransform{lhs.position, lhs.rotation, lhs.scale},
                                               Ecs::EcsWorldTransform{rhs.position, rhs.rotation, rhs.scale});
        }

        /// 根から数えた Entity の深さ（Outer/Inner の木で数える。階層の列が同期点まで古い間も正しい）
        uint32_t CountEntityDepth(const Entity& entity)
        {
            uint32_t depth = 0;
            for (const Entity* cursor = entity.GetParentEntity(); cursor; cursor = cursor->GetParentEntity())
            {
                ++depth;
            }
            return depth;
        }

        /// 補間の列（タグ・描画の変換・履歴）のどれかを持つか
        bool HasAnyRenderColumn(Ecs::EcsWorld& ecs, Ecs::EcsEntity row)
        {
            return ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr ||
                   ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr ||
                   ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;
        }
    } // namespace

    void World::SetRenderInterpolationAllowed(bool allowed)
    {
        if (m_bRenderInterpolationAllowed == allowed)
            return;
        m_bRenderInterpolationAllowed = allowed;
        // 履歴と描画の変換の列は許可に合わせて付け外しされる。次の更新で見直し、それまでも古い値を返さないよう値を下ろす
        if (!m_EcsWorld)
            return;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            m_RenderReconcile.push_back(row);
            ResetRenderColumns(*m_EcsWorld, row);
        }
    }

    Entity* World::ResolveRenderEntity(Ecs::EcsEntity row) const
    {
        if (!m_EcsWorld || row.IsNull() || !m_EcsWorld->IsAlive(row))
            return nullptr;
        const Ecs::EcsObjectRef* ref = m_EcsWorld->GetColumnValue<Ecs::EcsObjectRef>(row);
        const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(row);
        if (!ref || ref->Object == nullptr || ref->Generation != row.Generation || !hierarchy ||
            hierarchy->Kind != Ecs::kEcsHierarchyKindEntity)
            return nullptr;
        return static_cast<Entity*>(ref->Object);
    }

    bool World::IsRenderParticipating(const Entity& entity) const
    {
        if (!m_bRenderInterpolationAllowed)
            return false;
        for (const Entity* cursor = &entity; cursor; cursor = cursor->GetParentEntity())
            if (cursor->m_bRenderInterpolationEnabled)
                return true;
        return false;
    }

    void World::NoteRenderInterpolationChange(Entity& entity)
    {
        if (!m_EcsWorld || entity.m_EcsEntity.IsNull())
            return;
        m_RenderReconcile.push_back(entity.m_EcsEntity);
        // 付け替えや要求の変化で親子の前後が変わりうる。次の処理の前に並べ直す
        if (!m_RenderRows.empty())
            m_bRenderRowsUnsorted = true;
    }

    void World::NoteRenderRowCreated(Entity& entity)
    {
        const Ecs::EcsEntity row = entity.m_EcsEntity;
        if (!m_EcsWorld || row.IsNull())
            return;
        // 作られた行は、ワールドの変換を初めて求めたときに描画の更新へ知らせる。旧の再帰では行の作成が唯一の目印なので、ここでも積む
        QueueRenderChange(row);
        if (entity.m_bRenderInterpolationEnabled || (!m_RenderRows.empty() && IsRenderParticipating(entity)))
            m_RenderReconcile.push_back(row);
    }

    void World::ResetEntityRenderInterpolation(Entity& entity)
    {
        if (m_EcsWorld)
            ResetRenderColumns(*m_EcsWorld, entity.m_EcsEntity);
        for (auto* inner : entity.GetInners())
            if (auto* child = CastTo<Entity>(inner))
                ResetEntityRenderInterpolation(*child);
    }

    bool World::ReconcileRenderRow(Entity& entity, Ecs::EcsEntity row)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        // 要求と許可から決まる組: タグ = 要求した、描画の変換 = 要求した行かその子孫（許可されている間）、履歴 = 要求して許可されている
        const bool bTag = entity.m_bRenderInterpolationEnabled;
        const bool bRender = IsRenderParticipating(entity);
        const bool bHistory = m_bRenderInterpolationAllowed && bTag;
        const bool bHadTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
        const bool bHadRender = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr;
        const bool bHadHistory = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;

        if (bTag != bHadTag)
        {
            if (bTag)
                (void)ecs.AddColumnOf(row, Ecs::EcsRenderInterpolated{});
            else
                (void)ecs.RemoveColumnOf<Ecs::EcsRenderInterpolated>(row);
        }
        if (bRender != bHadRender)
        {
            if (bRender)
            {
                // 値は 0（bValid が 0）。次の更新が求める
                (void)ecs.AddColumnOf(row, Ecs::EcsRenderTransform{});
            }
            else
            {
                (void)ecs.RemoveColumnOf<Ecs::EcsRenderTransform>(row);
                // 描画の値が補間の結果からワールドの変換へ戻る。所有する描画状態を直させる
                QueueRenderChange(row);
            }
        }
        if (bHistory != bHadHistory)
        {
            if (bHistory)
                (void)ecs.AddColumnOf(row, Ecs::EcsTransformHistory{});
            else
                (void)ecs.RemoveColumnOf<Ecs::EcsTransformHistory>(row);
        }
        if (!bHadTag && !bHadRender && !bHadHistory && (bTag || bRender || bHistory))
        {
            m_RenderRows.push_back(row);
            m_bRenderRowsUnsorted = true;
        }
        return bRender != bHadRender;
    }

    void World::ReconcileRenderInterpolation()
    {
        if (m_RenderReconcile.empty() || !m_EcsWorld)
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        struct Item
        {
            uint32_t Depth;
            Ecs::EcsEntity Row;
        };
        Container::VariableArray<Item> work;
        for (const Ecs::EcsEntity row : m_RenderReconcile)
            if (const Entity* entity = ResolveRenderEntity(row))
                work.push_back(Item{CountEntityDepth(*entity), row});
        m_RenderReconcile.clear();

        bool bLostRows = false;
        Container::VariableArray<Item> current;
        while (!work.empty())
        {
            // 親を先に直す（子の要求は親の描画の変換の有無で決まる）。最も浅い深さの行をまとめて取り出す
            uint32_t depth = (std::numeric_limits<uint32_t>::max)();
            for (const Item& item : work)
                depth = (std::min)(depth, item.Depth);
            current.clear();
            size_t kept = 0;
            for (size_t i = 0; i < work.size(); ++i)
            {
                if (work[i].Depth == depth)
                    current.push_back(work[i]);
                else
                    work[kept++] = work[i];
            }
            work.resize(kept);
            std::sort(current.begin(), current.end(), [](const Item& lhs, const Item& rhs)
                      { return lhs.Row.Index != rhs.Row.Index ? lhs.Row.Index < rhs.Row.Index
                                                              : lhs.Row.Generation < rhs.Row.Generation; });
            current.erase(std::unique(current.begin(), current.end(),
                                      [](const Item& lhs, const Item& rhs) { return lhs.Row == rhs.Row; }),
                          current.end());
            for (const Item& item : current)
            {
                Entity* entity = ResolveRenderEntity(item.Row);
                if (!entity)
                    continue;
                if (ReconcileRenderRow(*entity, item.Row))
                {
                    // 描画の変換の有無が変わった行の子は、要求の組が変わりうる
                    for (Entity* child : entity->GetChildEntities())
                        if (!child->m_EcsEntity.IsNull())
                            work.push_back(Item{depth + 1, child->m_EcsEntity});
                }
                if (!HasAnyRenderColumn(ecs, item.Row))
                    bLostRows = true;
            }
        }
        if (bLostRows)
            CompactRenderRows();
    }

    void World::CompactRenderRows()
    {
        if (!m_EcsWorld)
        {
            m_RenderRows.clear();
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        size_t kept = 0;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            if (ResolveRenderEntity(row) != nullptr && HasAnyRenderColumn(ecs, row))
                m_RenderRows[kept++] = row;
        }
        m_RenderRows.resize(kept);
    }

    void World::SortRenderRows()
    {
        m_bRenderRowsUnsorted = false;
        struct Key
        {
            uint32_t Depth;
            Ecs::EcsEntity Row;
        };
        Container::VariableArray<Key> keys;
        for (const Ecs::EcsEntity row : m_RenderRows)
            if (const Entity* entity = ResolveRenderEntity(row))
                keys.push_back(Key{CountEntityDepth(*entity), row});
        std::stable_sort(keys.begin(), keys.end(), [](const Key& lhs, const Key& rhs)
                         { return lhs.Depth != rhs.Depth ? lhs.Depth < rhs.Depth : lhs.Row.Index < rhs.Row.Index; });
        m_RenderRows.clear();
        for (const Key& key : keys)
            m_RenderRows.push_back(key.Row);
    }

    void World::QueueRenderChange(Ecs::EcsEntity row)
    {
        if (!m_EcsWorld)
            return;
        Ecs::EcsPropagatedTransform* propagated = m_EcsWorld->GetColumnValue<Ecs::EcsPropagatedTransform>(row);
        if (!propagated || propagated->bRenderQueued != 0)
            return;
        propagated->bRenderQueued = 1;
        m_RenderChangedRows.push_back(row);
    }

    void World::NoteRenderTransformChanged(Entity& entity)
    {
        if (entity.m_RenderTransformVersion != UINT64_MAX)
            ++entity.m_RenderTransformVersion;
        // simulation版のversionは触らない。alphaだけ変わる0step frameでもproxyを更新する。
        for (auto* inner : entity.GetInners())
            if (auto* component = CastTo<Component::Component>(inner))
                if (CastTo<Component::MeshComponent>(component) || CastTo<Component::SkinnedMeshComponent>(component))
                    component->MarkRenderStateDirty();
    }

    void World::FlushRenderChangedRows()
    {
        if (m_RenderChangedRows.empty() || !m_EcsWorld)
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        for (const Ecs::EcsEntity row : m_RenderChangedRows)
        {
            if (Ecs::EcsPropagatedTransform* propagated = ecs.GetColumnValue<Ecs::EcsPropagatedTransform>(row))
                propagated->bRenderQueued = 0;
            Entity* entity = ResolveRenderEntity(row);
            // 補間する行とその子孫は UpdateRenderRows が見る。破棄予約の部分木は旧と同じく触らない
            if (!entity || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity) ||
                ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr)
                continue;
            NoteRenderTransformChanged(*entity);
        }
        m_RenderChangedRows.clear();
    }

    void World::StepRenderHistory(bool bCapture)
    {
        if (!HasFlag(OF_Initialized) || !m_bRenderInterpolationAllowed || !m_EcsWorld)
            return;
        ReconcileRenderInterpolation();
        if (m_RenderRows.empty())
            return;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            // 履歴を持つのは、補間を要求して許可されている行だけ（子孫は履歴を使わない）
            Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row);
            if (!history)
                continue;
            Entity* entity = ResolveRenderEntity(row);
            if (!entity || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity))
                continue;
            const Entity* parent = entity->GetParentEntity();
            const uint64_t parentId = parent ? parent->GetObjectId() : 0;
            if (Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row))
            {
                if (render->ParentId != parentId)
                {
                    history->History.Clear();
                    render->ParentId = parentId;
                }
            }
            if (bCapture)
                history->History.Capture(entity->GetWorldTransform());
            else
                history->History.Prepare(entity->GetWorldTransform());
        }
    }

    void World::PrepareRenderInterpolationStep()
    {
        StepRenderHistory(false);
    }

    void World::CaptureRenderInterpolationStep()
    {
        StepRenderHistory(true);
    }

    void World::UpdateRenderRows()
    {
        if (m_RenderRows.empty() || !m_EcsWorld)
            return;
        if (m_bRenderRowsUnsorted)
            SortRenderRows();
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        bool bLostRows = false;
#if NORVES_ENABLE_STATS
        // 計測: 補間の部分木の行の数（ECS_TRANSFORM_COUNTS で出す）
        m_TransformCounts.RenderRows += m_RenderRows.size();
#endif
        // 親が先（深さの順）。補間する行は履歴を評価し、その子孫は「親の描画の変換 × 局所の変換」で求める
        for (const Ecs::EcsEntity row : m_RenderRows)
        {
            Entity* entity = ResolveRenderEntity(row);
            if (!entity)
            {
                bLostRows = true;
                continue;
            }
            Ecs::EcsRenderTransform* render = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row);
            if (!render || entity->IsPendingDestroy() || HasPendingEntityAncestor(*entity))
                continue;
            Ecs::EcsTransformHistory* history = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row);
            const bool bTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
            const Entity* parent = entity->GetParentEntity();
            const uint64_t parentId = parent ? parent->GetObjectId() : 0;
            const Ecs::EcsRenderTransform* parentRender =
                parent ? ecs.GetColumnValue<Ecs::EcsRenderTransform>(parent->m_EcsEntity) : nullptr;
            const bool bInherited = m_bRenderInterpolationAllowed && parentRender != nullptr;
            if (render->ParentId != parentId || (render->bInherited != 0) != bInherited)
            {
                if (history)
                    history->History.Clear();
            }
            render->ParentId = parentId;
            render->bInherited = bInherited ? 1 : 0;
            const bool bInterpolated = m_bRenderInterpolationAllowed && (bInherited || bTag);
            const Math::Transform world = entity->GetWorldTransform();
            Math::Transform value = world;
            if (bInherited)
                value = LoadRenderTransform(*parentRender) * entity->GetLocalTransform();
            else if (bInterpolated && history && history->History.Prepare(world))
                (void)history->History.Evaluate(m_RenderInterpolationAlpha, value);
            const bool bChanged = render->bValid == 0 || LoadRenderTransform(*render) != value;
            StoreRenderTransform(*render, value);
            render->bValid = 1;
            if (bChanged)
                NoteRenderTransformChanged(*entity);
        }
        if (bLostRows)
            CompactRenderRows();
    }

    void World::UpdateRenderTransforms()
    {
        if (!HasFlag(OF_Initialized))
            return;
        UpdateWorldTransforms();
        // 描画の変換の系の区間（計測の方針の名前。前の変換の伝播は Ecs.Sync.TransformPropagate に入る）
        NorvesLib::Debug::ScopedStat statScope("Ecs.Sys.RenderTransform", "Ecs");
        ReconcileRenderInterpolation();
        UpdateRenderRows();
        FlushRenderChangedRows();
        // 検査 ECS_RENDER_TRANSFORM が有効なときだけ、全行を旧の式で求め直した値と照合する
        RunEcsRenderTransformCheck();
    }

    bool World::VerifyRenderEntity(Entity& entity, const Math::Transform& parentRender, bool bParentInterpolated,
                                   uint32_t& count, const char*& error)
    {
        // 旧の再帰と同じく、破棄予約の部分木は見ない
        if (entity.IsPendingDestroy())
            return true;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const bool bInherited = m_bRenderInterpolationAllowed && bParentInterpolated;
        const bool bInterpolated = m_bRenderInterpolationAllowed && (bInherited || entity.IsRenderInterpolationEnabled());
        const Math::Transform world = entity.GetWorldTransform();
        const Ecs::EcsEntity row = entity.GetEcsEntity();
        const bool bHasRow = !row.IsNull() && ecs.IsAlive(row);
        Math::Transform expected = world;
        if (bInherited)
        {
            expected = parentRender * entity.GetLocalTransform();
        }
        else if (bInterpolated)
        {
            const Ecs::EcsTransformHistory* history = bHasRow ? ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) : nullptr;
            if (history)
            {
                // 履歴の写しで求める（本体の履歴は変えない）
                Scene::RenderTransformHistory copy = history->History;
                if (copy.Prepare(world))
                    (void)copy.Evaluate(m_RenderInterpolationAlpha, expected);
            }
        }
        if (bHasRow)
        {
            ++count;
            const bool bTag = ecs.GetColumnValue<Ecs::EcsRenderInterpolated>(row) != nullptr;
            const bool bRender = ecs.GetColumnValue<Ecs::EcsRenderTransform>(row) != nullptr;
            const bool bHistory = ecs.GetColumnValue<Ecs::EcsTransformHistory>(row) != nullptr;
            if (bTag != entity.IsRenderInterpolationEnabled())
            {
                error = "補間のタグの有無が補間の要求と一致しない";
                return false;
            }
            if (bRender != bInterpolated)
            {
                error = "描画の変換の列の有無が補間の要求と許可から決まる組と一致しない";
                return false;
            }
            if (bHistory != (m_bRenderInterpolationAllowed && entity.IsRenderInterpolationEnabled()))
            {
                error = "履歴の列の有無が補間の要求と許可から決まる組と一致しない";
                return false;
            }
            if (!SameRenderTransform(entity.GetRenderWorldTransform(), expected))
            {
                error = "描画の変換が旧の式で求め直した値と一致しない";
                return false;
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            if (!VerifyRenderEntity(*child, expected, bInterpolated, count, error))
                return false;
        }
        return true;
    }

    bool World::VerifyEcsRenderTransform(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        // 同期点まで待たせた付け替え・作成予約・列の見直しがある間は、列が Outer/Inner と食い違うのが正当。この間は照合しない
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || !m_PendingEntityRelinks.empty() ||
            !m_PendingEntityCreates.empty() || !m_RenderReconcile.empty())
        {
            return true;
        }
        uint32_t count = 0;
        const char* error = nullptr;
        for (Entity* root : GetRootEntities())
        {
            if (!VerifyRenderEntity(*root, Math::Transform::Identity, false, count, error))
                break;
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (error)
        {
            LOG_ERROR("ECS_RENDER_TRANSFORM: %s", error);
            return false;
        }
        return true;
    }

    void World::RunEcsRenderTransformCheck()
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_RENDER_TRANSFORM"))
        {
            return;
        }
        ++m_RenderCheckCalls;
        uint32_t count = 0;
        const bool bPass = VerifyEcsRenderTransform(&count);
        // 呼び出しが多いので、fail は毎回、pass は最初に Entity が居た回と 64 回ごとに出す
        const bool bReportPass = bPass && count > 0 && (m_RenderCheckLastReport == 0 || m_RenderCheckCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_RENDER_TRANSFORM", bPass, count);
            m_RenderCheckLastReport = m_RenderCheckCalls;
        }
    }

    void World::UpdateEntityTransformRecursive(Entity& entity, const Math::Transform& parentWorld)
    {
        const uint64_t versionBefore = entity.m_TransformVersion;
        entity.RecomputeWorldTransform(parentWorld);
        if (entity.m_TransformVersion != versionBefore)
        {
            // 旧の再帰でも、ワールドの変換が変わった行を描画の更新へ知らせる
            QueueRenderChange(entity.m_EcsEntity);
            NoteRenderSyncTransform(entity.m_EcsEntity);
        }
        const Math::Transform& worldTransform = entity.GetWorldTransform();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            UpdateEntityTransformRecursive(*child, worldTransform);
        }
    }

    void World::SyncEntityRecursive(
        Entity& entity,
        const Rendering::MaterialResources* materials,
        const Rendering::MeshResources* meshes,
        Container::UnorderedSet<uint64_t>& liveMeshComponentIds,
        Container::UnorderedSet<uint64_t>& liveSkinnedMeshComponentIds,
        Container::UnorderedSet<uint64_t>& liveMegaGeometryObjectIds,
        Container::UnorderedSet<uint64_t>& liveLightIds,
        Container::UnorderedSet<uint64_t>& liveScreenBoardComponentIds,
        Container::UnorderedSet<uint64_t>& liveWorldBoardComponentIds,
        ComponentDataRegistry* componentDataRegistry)
    {
        ++m_RenderSyncVisitedRows;
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            // 走査しない部分木の proxy は RemoveStale* が消す。公開の状態の列もそれに合わせる。
            // 既定の同期では、Remove の一覧を外すときに公開の状態を戻すので、ここでは何もしない
            if (m_bLegacyRenderSync)
            {
                MarkEntitySubtreeRenderUnpublished(entity);
            }
            return;
        }

        if (componentDataRegistry)
        {
            componentDataRegistry->PublishTransform(entity);
        }

        const uint64_t ownerVersion = entity.GetTransformVersion();
        const bool bCheckInvalidation = m_bInvalidationCheckFrame;
        auto components = entity.GetComponents();
        for (auto* comp : components)
        {
            auto* skinnedMeshComp = CastTo<Component::SkinnedMeshComponent>(comp);
            if (m_bLegacyRenderSync && m_SceneView && skinnedMeshComp)
            {
                const RenderRowInfo rowInfo = ReadRenderRowInfo(*skinnedMeshComp, entity);
                const bool bNeedsSync = skinnedMeshComp->IsRenderStateDirty() ||
                                        skinnedMeshComp->GetLastSyncedTransformVersion() != ownerVersion ||
                                        rowInfo.bPoseChanged;
                if (bNeedsSync)
                {
                    NoteRenderScanChanged(*skinnedMeshComp);
                }
                if (!bNeedsSync)
                {
                    if (skinnedMeshComp->IsVisible())
                    {
                        liveSkinnedMeshComponentIds.insert(skinnedMeshComp->GetComponentId());
                    }
                    // 変更も通知も無い行は、作り直しても公開した内容と同じでなければならない。
                    // 姿勢が最新のときだけ作り直す（最新でないときの構築は姿勢を求め直して状態を変えてしまう）
                    if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified &&
                        skinnedMeshComp->IsVisible() && skinnedMeshComp->IsPoseCurrent())
                    {
                        Rendering::SkinnedMeshProxy probe;
                        const bool bBuilt = skinnedMeshComp->BuildSkinnedMeshProxy(probe);
                        if (bBuilt)
                        {
                            probe.ObjectId = entity.GetObjectId();
                            probe.ComponentId = skinnedMeshComp->GetComponentId();
                        }
                        CheckRenderRowUnchanged(*skinnedMeshComp, bBuilt, bBuilt ? Rendering::HashProxy(probe) : 0);
                    }
                }
                else if (!skinnedMeshComp->IsVisible())
                {
                    // 非表示は公開しない状態として受理済みで、再試行すべき失敗ではない
                    skinnedMeshComp->ClearRenderStateDirty();
                    skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                    RecordRenderPublish(*skinnedMeshComp, entity, false, 0);
                }
                else
                {
                    Rendering::SkinnedMeshProxy skinnedMeshProxy;
                    if (skinnedMeshComp->BuildSkinnedMeshProxy(skinnedMeshProxy))
                    {
                        skinnedMeshProxy.ObjectId = entity.GetObjectId();
                        skinnedMeshProxy.ComponentId = skinnedMeshComp->GetComponentId();
                        m_SceneView->UpdateSkinnedMeshProxy(skinnedMeshProxy);
                        liveSkinnedMeshComponentIds.insert(skinnedMeshProxy.ComponentId);
                        skinnedMeshComp->ClearRenderStateDirty();
                        skinnedMeshComp->SetLastSyncedTransformVersion(ownerVersion);
                        RecordRenderPublish(*skinnedMeshComp, entity, true,
                                            bCheckInvalidation ? Rendering::HashProxy(skinnedMeshProxy) : 0);
                    }
                    else
                    {
                        // 公開したいのに proxy を作れなかった（資産の読み込み待ちなど）。次の同期でもう一度試す
                        RecordRenderPublish(*skinnedMeshComp, entity, false, 0);
                        NoteRenderRetry(*skinnedMeshComp);
                    }
                    RegisterRenderResourceUsers(*skinnedMeshComp);
                }
            }

            auto* megaComp = CastTo<Component::MegaGeometryComponent>(comp);

            auto* meshComp = CastTo<Component::MeshComponent>(comp);
            if (m_bLegacyRenderSync && m_SceneView && meshComp && !megaComp)
            {
                const RenderRowInfo rowInfo = ReadRenderRowInfo(*meshComp, entity);
                const bool bNeedsSync = meshComp->IsRenderStateDirty() ||
                                        meshComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (bNeedsSync)
                {
                    NoteRenderScanChanged(*meshComp);
                }
                if (!bNeedsSync)
                {
                    liveMeshComponentIds.insert(meshComp->GetComponentId());
                    Rendering::MeshProxy meshProxy;
                    if (meshComp->BuildMeshProxy(meshProxy, materials, meshes))
                    {
                        meshProxy.ObjectId = entity.GetObjectId();
                        meshProxy.ComponentId = meshComp->GetComponentId();
                        m_SceneView->UpdateMeshProxy(meshProxy);
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMeshProxy(entity, meshProxy);
                        }
                        // 変更も通知も無い行を作り直した内容は、公開した内容と同じでなければならない
                        const uint64_t proxyHash = bCheckInvalidation ? Rendering::HashProxy(meshProxy) : 0;
                        if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified)
                        {
                            CheckRenderRowUnchanged(*meshComp, true, proxyHash);
                        }
                        RecordRenderPublish(*meshComp, entity, true, proxyHash);
                    }
                    else if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified)
                    {
                        CheckRenderRowUnchanged(*meshComp, false, 0);
                    }
                }
                else
                {
                    meshComp->RefreshRenderTransformCache();

                    Rendering::MeshProxy meshProxy;
                    if (meshComp->BuildMeshProxy(meshProxy, materials, meshes))
                    {
                        meshProxy.ObjectId = entity.GetObjectId();
                        meshProxy.ComponentId = meshComp->GetComponentId();
                        m_SceneView->UpdateMeshProxy(meshProxy);
                        liveMeshComponentIds.insert(meshProxy.ComponentId);
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMeshProxy(entity, meshProxy);
                        }
                        RecordRenderPublish(*meshComp, entity, true,
                                            bCheckInvalidation ? Rendering::HashProxy(meshProxy) : 0);
                        // 前フレームの変換が現在と違う間は、次のフレームの公開で履歴が現在に揃う。次の同期の通知にする
                        if (!SameMatrixBits(meshProxy.PreviousWorldTransform, meshProxy.WorldTransform))
                        {
                            m_RenderSettleNext.insert(meshComp->GetComponentId());
                        }
                    }
                    else
                    {
                        RecordRenderPublish(*meshComp, entity, false, 0);
                    }
                    RegisterRenderResourceUsers(*meshComp);

                    meshComp->ClearRenderStateDirty();
                    meshComp->SetLastSyncedTransformVersion(ownerVersion);
                }
                meshComp->CommitRenderTransformHistory();
            }

            if (m_SceneView && megaComp)
            {
                const RenderRowInfo rowInfo = ReadRenderRowInfo(*megaComp, entity);
                const bool bNeedsSync = megaComp->IsRenderStateDirty() ||
                                        megaComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (bNeedsSync)
                {
                    NoteRenderScanChanged(*megaComp);
                }
                if (!bNeedsSync)
                {
                    // 既定の同期は生存集合を使わない（O(全 Entity) のハッシュ挿入を避ける）
                    if (m_bLegacyRenderSync)
                    {
                        liveMegaGeometryObjectIds.insert(entity.GetObjectId());
                    }
                    if (componentDataRegistry)
                    {
                        Rendering::MegaGeometryProxy megaProxy;
                        if (megaComp->BuildMegaGeometryProxy(megaProxy))
                        {
                            megaProxy.ObjectId = entity.GetObjectId();
                            megaProxy.ComponentId = megaComp->GetComponentId();
                            componentDataRegistry->PublishMegaGeometryProxy(entity, megaProxy);
                        }
                    }
                    if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified)
                    {
                        Rendering::MegaGeometryProxy probe;
                        const bool bBuilt = megaComp->BuildMegaGeometryProxy(probe);
                        if (bBuilt)
                        {
                            probe.ObjectId = entity.GetObjectId();
                            probe.ComponentId = megaComp->GetComponentId();
                        }
                        CheckRenderRowUnchanged(*megaComp, bBuilt, bBuilt ? Rendering::HashProxy(probe) : 0);
                    }
                }
                else
                {
                    megaComp->RefreshRenderTransformCache();

                    Rendering::MegaGeometryProxy megaProxy;
                    if (megaComp->BuildMegaGeometryProxy(megaProxy))
                    {
                        megaProxy.ObjectId = entity.GetObjectId();
                        megaProxy.ComponentId = megaComp->GetComponentId();
                        const uint64_t proxyHash = bCheckInvalidation ? Rendering::HashProxy(megaProxy) : 0;
                        PublishMegaProxyToView(*m_SceneView, m_EcsWorld.get(), megaComp->GetEcsEntity(), megaProxy);
                        if (m_bLegacyRenderSync)
                        {
                            liveMegaGeometryObjectIds.insert(megaProxy.ObjectId);
                        }
                        if (componentDataRegistry)
                        {
                            componentDataRegistry->PublishMegaGeometryProxy(entity, megaProxy);
                        }
                        RecordRenderPublish(*megaComp, entity, true, proxyHash);
                    }
                    else
                    {
                        RecordRenderPublish(*megaComp, entity, false, 0);
                    }

                    megaComp->ClearRenderStateDirty();
                    megaComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }

            auto* lightComp = CastTo<Component::LightComponent>(comp);
            if (m_bLegacyRenderSync && m_SceneView && lightComp)
            {
                const RenderRowInfo rowInfo = ReadRenderRowInfo(*lightComp, entity);
                const bool bNeedsSync = lightComp->IsRenderStateDirty() ||
                                        lightComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (bNeedsSync)
                {
                    NoteRenderScanChanged(*lightComp);
                }
                if (!bNeedsSync)
                {
                    liveLightIds.insert(lightComp->GetComponentId());
                    if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified)
                    {
                        Rendering::LightProxy probe;
                        const bool bBuilt = lightComp->BuildLightProxy(probe);
                        if (bBuilt)
                        {
                            probe.LightId = lightComp->GetComponentId();
                        }
                        CheckRenderRowUnchanged(*lightComp, bBuilt, bBuilt ? Rendering::HashProxy(probe) : 0);
                    }
                }
                else
                {
                    Rendering::LightProxy lightProxy;
                    if (lightComp->BuildLightProxy(lightProxy))
                    {
                        lightProxy.LightId = lightComp->GetComponentId();
                        m_SceneView->UpdateLightProxy(lightProxy);
                        liveLightIds.insert(lightProxy.LightId);
                        RecordRenderPublish(*lightComp, entity, true,
                                            bCheckInvalidation ? Rendering::HashProxy(lightProxy) : 0);
                    }
                    else
                    {
                        RecordRenderPublish(*lightComp, entity, false, 0);
                    }

                    lightComp->ClearRenderStateDirty();
                    lightComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }

            auto* textComp = CastTo<Component::TextComponent>(comp);
            if (textComp)
            {
                textComp->RefreshRenderTransformCache();
                continue;
            }

            auto* boardComp = CastTo<Component::BoardComponent>(comp);
            if (m_bLegacyRenderSync && boardComp)
            {
                const RenderRowInfo rowInfo = ReadRenderRowInfo(*boardComp, entity);
                boardComp->PrepareFlipbookForRenderSync();
                const bool bForceWorldSpace = CastTo<Component::BillboardComponent>(boardComp) != nullptr;
                const Rendering::BoardSpace effectiveSpace = bForceWorldSpace
                                                                  ? Rendering::BoardSpace::WorldSpace
                                                                  : boardComp->GetBoardSpace();

                const bool bNeedsSync = boardComp->IsRenderStateDirty() ||
                                        boardComp->GetLastSyncedTransformVersion() != ownerVersion;
                if (bNeedsSync)
                {
                    NoteRenderScanChanged(*boardComp);
                }
                if (!bNeedsSync)
                {
                    if (effectiveSpace == Rendering::BoardSpace::WorldSpace)
                    {
                        liveWorldBoardComponentIds.insert(boardComp->GetComponentId());
                    }
                    else
                    {
                        liveScreenBoardComponentIds.insert(boardComp->GetComponentId());
                    }
                    if (bCheckInvalidation && !rowInfo.bRenderTransformChanged && !rowInfo.bNotified)
                    {
                        Rendering::BoardProxy probe;
                        const bool bBuilt = boardComp->BuildBoardProxy(probe);
                        if (bBuilt)
                        {
                            probe.ObjectId = entity.GetObjectId();
                            probe.ComponentId = boardComp->GetComponentId();
                        }
                        CheckRenderRowUnchanged(*boardComp, bBuilt, bBuilt ? Rendering::HashProxy(probe) : 0);
                    }
                }
                else
                {
                    boardComp->RefreshRenderTransformCache();

                    Rendering::BoardProxy boardProxy;
                    if (boardComp->BuildBoardProxy(boardProxy))
                    {
                        boardProxy.ObjectId = entity.GetObjectId();
                        boardProxy.ComponentId = boardComp->GetComponentId();
                        RecordRenderPublish(*boardComp, entity, true,
                                            bCheckInvalidation ? Rendering::HashProxy(boardProxy) : 0,
                                            boardProxy.Space == Rendering::BoardSpace::WorldSpace
                                                ? Ecs::EcsRenderSyncKind::WorldBoard
                                                : Ecs::EcsRenderSyncKind::ScreenBoard);
                        if (boardProxy.Space == Rendering::BoardSpace::WorldSpace)
                        {
                            liveWorldBoardComponentIds.insert(boardProxy.ComponentId);
                            if (m_SceneView)
                            {
                                m_SceneView->UpdateBoardProxy(boardProxy);
                            }
                            if (m_ScreenSpaceBoardSink)
                            {
                                m_ScreenSpaceBoardSink->RemoveBoardProxy(boardProxy.ComponentId);
                            }
                        }
                        else
                        {
                            liveScreenBoardComponentIds.insert(boardProxy.ComponentId);
                            if (m_ScreenSpaceBoardSink)
                            {
                                m_ScreenSpaceBoardSink->UpdateBoardProxy(boardProxy.ComponentId, boardProxy);
                            }
                            if (m_SceneView)
                            {
                                m_SceneView->RemoveBoardProxy(boardProxy.ComponentId);
                            }
                        }
                    }
                    else
                    {
                        RecordRenderPublish(*boardComp, entity, false, 0);
                        if (m_ScreenSpaceBoardSink)
                        {
                            m_ScreenSpaceBoardSink->RemoveBoardProxy(boardComp->GetComponentId());
                        }
                        if (m_SceneView)
                        {
                            m_SceneView->RemoveBoardProxy(boardComp->GetComponentId());
                        }
                    }

                    boardComp->ClearRenderStateDirty();
                    boardComp->SetLastSyncedTransformVersion(ownerVersion);
                }
            }
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            SyncEntityRecursive(
                *child,
                materials,
                meshes,
                liveMeshComponentIds,
                liveSkinnedMeshComponentIds,
                liveMegaGeometryObjectIds,
                liveLightIds,
                liveScreenBoardComponentIds,
                liveWorldBoardComponentIds,
                componentDataRegistry);
        }
    }

    void World::DispatchFixedTick(float fixedDeltaTime)
    {
        if (!HasFlag(OF_Initialized) || IsDeferringObjectRemoval())
        {
            return;
        }
        // Tick の外の単独の固定ステップは、それぞれを 1 回の Tick として数える（収集の中ではフレームの番号を共有する）
        if (!m_bTickCollectionOpen && ++m_TickSerial == 0)
        {
            ++m_TickSerial;
        }
        // 1 歩の時間は、呼び手が段の既定の delta（SetStageDefaultDelta）で段のシステムへ届ける
        (void)fixedDeltaTime;
        RunWorldStage(m_StageFixedTick);
    }

    void World::CleanupAfterFixedStep()
    {
        CleanupDestroyedObjects();
    }

    void World::CollectTransientBoardProxiesRecursive(
        Entity& entity,
        Container::VariableArray<Rendering::BoardProxy>& outProxies)
    {
        if (!entity.IsActive() || entity.IsPendingDestroy())
        {
            return;
        }

        for (Component::Component* component : entity.GetComponents())
        {
            if (Component::TextComponent* textComponent = CastTo<Component::TextComponent>(component))
            {
                textComponent->BuildGlyphBoardProxies(outProxies);
            }
        }

        for (Entity* child : entity.GetChildEntities())
        {
            CollectTransientBoardProxiesRecursive(*child, outProxies);
        }
    }

    void World::CleanupDestroyedObjects()
    {
        if (IsDeferringObjectRemoval())
        {
            return;
        }
        ScopedWorldFlag cleaning(m_bCleaningObjects);
        // 破棄の回収の区間（計測の方針の名前）
        NorvesLib::Debug::ScopedStat statScope("Ecs.Sync.Cleanup", "Ecs");

        // 検査 WORLD_CLEANUP: 全木を走査して見つけた予約と、一覧から求めた処理対象が一致するか照合する。
        // 有効なときだけ全木を走査する（通常は一覧だけを処理する）。
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        const bool bCheck = checks != nullptr && checks->IsEnabled("WORLD_CLEANUP");
        bool bCheckPass = true;
        uint32_t checkCount = 0;

        // 木から外す Component を先に処理する。所有する Entity か祖先が破棄予約なら、
        // Entity ごと破棄されるので飛ばす。
        auto& pendingComponents = m_PendingTickComponents;
        pendingComponents.clear();
        for (Component::Component* component : m_PendingDestroyComponents)
        {
            component->SetFlag(OF_InDestroyList, false);
            Entity* owner = component->GetOwner();
            if (owner && owner->GetWorld() == this && component->IsPendingDestroy() &&
                !owner->IsPendingDestroy() && !HasPendingEntityAncestor(*owner))
            {
                pendingComponents.push_back(component);
            }
        }
        m_PendingDestroyComponents.clear();

        if (bCheck)
        {
            Container::VariableArray<Component::Component*> scanned;
            for (Entity* root : GetRootEntities())
            {
                CollectPendingComponents(*root, scanned);
            }
            bCheckPass = SamePointerSet(scanned, pendingComponents) && bCheckPass;
            checkCount += static_cast<uint32_t>(scanned.size());
        }

        // 途中で別の Component が消えると、InvalidateTickComponent が待ち参照を nullptr にする
        for (Component::Component* component : pendingComponents)
        {
            if (!component)
            {
                continue;
            }
            if (Entity* owner = component->GetOwner())
            {
                owner->RemoveComponentImmediately(component);
            }
        }

        // 破棄予約の Entity。祖先が先に予約されているものは、その祖先の部分木ごと破棄される。
        // 破棄の前に判定を全部済ませる（先に破棄した部分木の中の Entity を後で参照しないため）。
        auto& toRemove = m_PendingTickEntities;
        toRemove.clear();
        for (Entity* entity : m_PendingDestroyEntities)
        {
            entity->SetFlag(OF_InDestroyList, false);
            if (entity->GetWorld() == this && entity->IsPendingDestroy() && !HasPendingEntityAncestor(*entity))
            {
                toRemove.push_back(entity);
            }
        }
        m_PendingDestroyEntities.clear();

        if (bCheck)
        {
            Container::VariableArray<Entity*> scanned;
            for (Entity* root : GetRootEntities())
            {
                CollectPendingDestroyRecursive(*root, scanned);
            }
            bCheckPass = SamePointerSet(scanned, toRemove) && bCheckPass;
            checkCount += static_cast<uint32_t>(scanned.size());
        }

        for (auto* entity : toRemove)
        {
            if (entity && entity->GetWorld() == this && entity->IsPendingDestroy())
            {
                DestroyEntitySubtree(*entity);
            }
        }

        if (!toRemove.empty())
        {
            NORVES_LOG_DEBUG("World", "Cleaned up %llu destroyed objects",
                             static_cast<uint64_t>(toRemove.size()));
        }

        // 破棄が無い回は、最初の 1 回だけ pass を出す（毎フレームのログを避けつつ、検査が走ったことを示す）
        if (bCheck && (!bCheckPass || checkCount > 0 || !m_bCleanupCheckReported))
        {
            checks->Report("WORLD_CLEANUP", bCheckPass, checkCount);
            m_bCleanupCheckReported = true;
        }
    }

    void World::RegisterPendingDestroy(IUnknown& object)
    {
        if (object.HasFlag(OF_InDestroyList))
        {
            return;
        }
        if (auto* entity = CastTo<Entity>(&object))
        {
            // Object のフラグだけ立てて直接積む経路でも、ECS の破棄の印と変更ログを更新する
            entity->getbPendingDestroy().Set(true);
            m_PendingDestroyEntities.push_back(entity);
            // 破棄予約の部分木は走査に入らなくなる。出していた proxy を Remove の候補にする
            NoteSubtreeRenderRemoval(*entity);
        }
        else if (auto* component = CastTo<Component::Component>(&object))
        {
            m_PendingDestroyComponents.push_back(component);
        }
        else
        {
            return;
        }
        object.SetFlag(OF_InDestroyList, true);
    }

    void World::ForgetPendingDestroy(IUnknown& object)
    {
        if (!object.HasFlag(OF_InDestroyList))
        {
            return;
        }
        object.SetFlag(OF_InDestroyList, false);
        if (auto* entity = CastTo<Entity>(&object))
        {
            auto it = std::find(m_PendingDestroyEntities.begin(), m_PendingDestroyEntities.end(), entity);
            if (it != m_PendingDestroyEntities.end())
            {
                m_PendingDestroyEntities.erase(it);
            }
        }
        else if (auto* component = CastTo<Component::Component>(&object))
        {
            auto it = std::find(m_PendingDestroyComponents.begin(), m_PendingDestroyComponents.end(), component);
            if (it != m_PendingDestroyComponents.end())
            {
                m_PendingDestroyComponents.erase(it);
            }
        }
    }

    void World::ForgetPendingDestroySubtree(Entity& entity)
    {
        if (m_PendingDestroyEntities.empty() && m_PendingDestroyComponents.empty())
        {
            return;
        }
        ForgetPendingDestroy(entity);
        for (IUnknown* inner : entity.GetInners())
        {
            if (m_PendingDestroyEntities.empty() && m_PendingDestroyComponents.empty())
            {
                return;
            }
            if (auto* child = CastTo<Entity>(inner))
            {
                ForgetPendingDestroySubtree(*child);
            }
            else if (inner->HasFlag(OF_InDestroyList))
            {
                ForgetPendingDestroy(*inner);
            }
        }
    }

    void World::CollectPendingComponents(Entity& entity, Container::VariableArray<Component::Component*>& output)
    {
        if (entity.IsPendingDestroy())
        {
            return;
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                if (component->IsPendingDestroy())
                {
                    output.push_back(component);
                }
            }
            else if (auto* child = CastTo<Entity>(inner))
            {
                CollectPendingComponents(*child, output);
            }
        }
    }

    void World::CollectPendingDestroyRecursive(Entity& entity, Container::VariableArray<Entity*>& toRemove)
    {
        if (entity.IsPendingDestroy())
        {
            toRemove.push_back(&entity);
            return;
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            CollectPendingDestroyRecursive(*child, toRemove);
        }
    }

    bool World::AttachRootEntity(Entity* entity)
    {
        if (!entity ||
            !HasFlag(OF_Initialized) ||
            entity->GetOuter() != nullptr ||
            entity->IsPendingDestroy() ||
            ContainsHeapOwnedEntity(*entity))
        {
            return false;
        }

        if (ContainsInner(*this, *entity))
        {
            return false;
        }

        AppendInnerReference(*this, *entity);
        SetOuterReference(*entity, this);

        BindEntitySubtreeRows(*entity);
        InitializeEntitySubtreeForWorld(*entity);
        AssignFreshObjectIdsRecursive(*entity);
        RegisterEntitySubtreeForComponentData(*this, *entity);
        MarkEntitySubtreeTransformDirty(*entity);
        MarkEntitySubtreeRenderStateDirty(*entity);
        NotifyEntitySubtreeAdded(*entity);
        return true;
    }

    bool World::AttachChildEntity(Entity* parent, Entity* child)
    {
        if (!parent || !child || !HasFlag(OF_Initialized))
        {
            return false;
        }

        if (parent->GetWorld() != this ||
            parent->IsPendingDestroy() ||
            child->GetOuter() != nullptr ||
            child->IsPendingDestroy() ||
            ContainsHeapOwnedEntity(*child) ||
            parent == child ||
            IsEntityInSubtree(*child, *parent))
        {
            return false;
        }

        if (ContainsInner(*parent, *child))
        {
            return false;
        }

        AppendInnerReference(*parent, *child);
        SetOuterReference(*child, parent);

        BindEntitySubtreeRows(*child);
        InitializeEntitySubtreeForWorld(*child);
        AssignFreshObjectIdsRecursive(*child);
        RegisterEntitySubtreeForComponentData(*this, *child);
        MarkEntitySubtreeTransformDirty(*child);
        MarkEntitySubtreeRenderStateDirty(*child);
        NotifyEntitySubtreeAdded(*child);
        return true;
    }

    bool World::MoveEntityInnerAtomic(Entity& entity, IUnknown& newOwner)
    {
        IUnknown* oldOwner = entity.GetOuter();
        if (!oldOwner || oldOwner == &newOwner)
        {
            return oldOwner == &newOwner;
        }

        if (ContainsInner(newOwner, entity))
        {
            return false;
        }

        AppendInnerReference(newOwner, entity);
        SetOuterReference(entity, &newOwner);

        if (!EraseInnerReference(*oldOwner, entity))
        {
            EraseInnerReference(newOwner, entity);
            SetOuterReference(entity, oldOwner);
            return false;
        }

        RelinkEntityRow(entity);
        return true;
    }

    bool World::DestroyEntitySubtree(Entity& entity)
    {
        if (entity.GetWorld() != this)
        {
            return false;
        }

        IUnknown* owner = entity.GetOuter();
        if (!owner)
        {
            return false;
        }

        ScopedWorldFlag removing(m_bCleaningObjects);
        UnregisterEntitySubtreeForComponentData(entity);

        RemoveEntitySubtreeProxies(entity);
        NotifyEntitySubtreeRemoved(entity);
        InvalidateTickEntitySubtree(entity);

        // 部分木の行を、木から切り離す前に孤児にして消す（Object の解放より前に行を切る）
        UnbindEntitySubtreeRows(entity);

        // 破棄する部分木の中の予約を一覧から外す。除去の通知（OnRemovedFromWorld）が再び予約しても
        // 取りこぼさないよう、通知が済んだ後・木を切り離す直前に行う（削除後に一覧が壊れたポインタを持たないため）
        ForgetPendingDestroySubtree(entity);

        EraseInnerReference(*owner, entity);
        SetOuterReference(entity, nullptr);

        if (entity.HasFlag(OF_HeapOwned))
        {
            entity.SetFlag(OF_PendingDestroy, true);
            return true;
        }

        entity.Finalize();
        delete &entity;
        return true;
    }

    void World::BindEntitySubtreeRows(Entity& entity)
    {
        // 木に部分木が加わった。Tick の呼び先と深さ優先の番号、Text の板の順を次の収集で付け直す
        m_bOrderedTicksDirty = true;
        m_bTextBoardOrderDirty = true;
        if (m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (entity.m_EcsCreateSlot == kNoEntityCreateSlot && !ecs.IsAlive(entity.m_EcsEntity) && m_Scheduler->IsRunning())
        {
            // 段の最中は作成を予約し、行は段の終わりの同期点で作る
            ReserveEntityRowCreate(entity);
        }
        else if (entity.m_EcsCreateSlot == kNoEntityCreateSlot && !ecs.IsAlive(entity.m_EcsEntity))
        {
            const Ecs::EcsEntity row = ecs.CreateEntity(m_EntityArchetype);
            if (row.IsNull())
            {
                LOG_ERROR("World: Entity の ECS の行を作れなかった");
            }
            else
            {
                entity.m_EcsEntity = row;
                if (Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row))
                {
                    // 作った直後の行なので、そのまま書く（作成が全列を「変わった」にする）
                    ref->Object = static_cast<void*>(&entity);
                    ref->Generation = row.Generation;
                }
                MoveEntityValuesToRow(entity);
                const Entity* parent = entity.GetParentEntity();
                LinkEntityRowToParent(row, parent ? parent->m_EcsEntity : Ecs::kNullEcsEntity);
                ++m_EcsBindingEpoch;
                NoteRenderRowCreated(entity);
            }
        }
        for (Component::Component* component : entity.GetComponents())
        {
            BindComponentRow(*component);
        }
        for (Entity* child : entity.GetChildEntities())
        {
            BindEntitySubtreeRows(*child);
        }
    }

    Ecs::EcsArchetypeId World::ComponentArchetypeFor(const Component::Component& component) const
    {
        const size_t kind = static_cast<size_t>(component.GetEcsRowKind());
        if (kind >= static_cast<size_t>(Ecs::EcsComponentRowKind::Count))
        {
            return Ecs::kInvalidEcsArchetype;
        }
        return m_ComponentArchetypes[kind];
    }

    void World::BindComponentRow(Component::Component& component)
    {
        // 木の Entity に Component が加わった。Tick の呼び先と深さ優先の番号を次の収集で付け直す
        m_bOrderedTicksDirty = true;
        if (ComponentArchetypeFor(component) == Ecs::kInvalidEcsArchetype || !m_EcsWorld)
        {
            return;
        }
        Entity* owner = component.GetOwner();
        if (!owner || owner->GetWorld() != this)
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (component.m_EcsCreateSlot != kNoEntityCreateSlot || ecs.IsAlive(component.m_EcsEntity))
        {
            // 行は付いている（または作成を予約済み）
            return;
        }
        const bool bOwnerReserved = owner->m_EcsCreateSlot != kNoEntityCreateSlot;
        if (!bOwnerReserved && !ecs.IsAlive(owner->m_EcsEntity))
        {
            // 所有者に行が無い（再生に失敗した）。値は Object に残し、行なしのまま
            return;
        }
        if (bOwnerReserved || m_Scheduler->IsRunning())
        {
            // 段の最中は作成を予約し、行は段の終わりの同期点で作る
            ReserveComponentRowCreate(component);
            return;
        }
        const Ecs::EcsEntity row = ecs.CreateEntity(ComponentArchetypeFor(component));
        if (row.IsNull())
        {
            LOG_ERROR("World: Component の ECS の行を作れなかった");
            return;
        }
        component.m_EcsEntity = row;
        if (Ecs::EcsObjectRef* ref = ecs.GetColumnValue<Ecs::EcsObjectRef>(row))
        {
            ref->Object = static_cast<void*>(&component);
            ref->Generation = row.Generation;
        }
        MoveComponentValuesToRow(component);
        LinkEntityRowToParent(row, owner->m_EcsEntity, Ecs::kEcsHierarchyKindComponent);
        ++m_EcsBindingEpoch;
        RegisterTextBoardRow(component);
    }

    void World::MoveEntityValuesToRow(Entity& entity)
    {
        // 時間倍率とワールドの変換のキャッシュは保存の対象でないので、予備の値を直接列へ写す。
        // flags と局所の変換は ECS_PROPERTY の移動に任せる
        m_EcsWorld->WriteColumnValueOf(entity.m_EcsEntity, Ecs::EcsTimeDilation{entity.m_CustomTimeDilation});
        const Math::Transform& cache = entity.m_WorldTransformFallback;
        m_EcsWorld->WriteColumnValueOf(entity.m_EcsEntity, Ecs::EcsWorldTransform{cache.position, cache.rotation, cache.scale});
        MoveEcsPropertyValuesToColumns(&entity);
    }

    void World::MoveComponentValuesToRow(Component::Component& component)
    {
        Ecs::EcsTickConfig config{};
        config.Group = static_cast<uint8_t>(component.m_TickConfiguration.GetGroup());
        config.Priority = component.m_TickConfiguration.GetPriority();
        config.Mask = component.m_TickConfiguration.GetMask();
        m_EcsWorld->WriteColumnValueOf(component.m_EcsEntity, config);
        MoveEcsPropertyValuesToColumns(&component);
        component.OnEcsRowAttached();
        // 描画の同期が必要になったら変更ログへ知らせる。いま必要な分は種として積む
        component.m_RenderLogWorld = this;
        if (component.IsRenderStateDirty())
        {
            NoteComponentRenderDirty(component.m_EcsEntity);
        }
    }

    void World::MoveEntityValuesToFallback(Entity& entity)
    {
        if (const Ecs::EcsTimeDilation* dilation = m_EcsWorld->GetColumnValue<Ecs::EcsTimeDilation>(entity.m_EcsEntity))
        {
            entity.m_CustomTimeDilation = dilation->CustomTimeDilation;
        }
        if (const Ecs::EcsWorldTransform* cache = m_EcsWorld->GetColumnValue<Ecs::EcsWorldTransform>(entity.m_EcsEntity))
        {
            entity.m_WorldTransformFallback = Math::Transform(cache->position, cache->rotation, cache->scale);
        }
        MoveEcsPropertyValuesToFallback(&entity);
    }

    void World::MoveComponentValuesToFallback(Component::Component& component)
    {
        component.OnEcsRowDetaching();
        component.m_RenderLogWorld = nullptr;
        if (const Ecs::EcsTickConfig* config = m_EcsWorld->GetColumnValue<Ecs::EcsTickConfig>(component.m_EcsEntity))
        {
            // 列の値は検証済みの設定から写したものなので、群 → 優先度 → mask の順に戻せる
            Component::TickGroupConfiguration restored;
            restored.SetGroup(static_cast<Component::ETickGroup>(config->Group));
            restored.SetPriority(config->Priority);
            restored.SetMask(config->Mask);
            component.m_TickConfiguration = restored;
        }
        MoveEcsPropertyValuesToFallback(&component);
    }

    void World::LinkEntityRowToParent(Ecs::EcsEntity row, Ecs::EcsEntity parentRow, uint32_t kind)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsHierarchy* parentHierarchy = parentRow.IsNull() ? nullptr : ecs.GetColumnValue<Ecs::EcsHierarchy>(parentRow);
        if (!parentHierarchy)
        {
            // 根（World 直下）。兄弟の連結は持たない
            EditHierarchy(ecs, row, [kind](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = Ecs::kNullEcsEntity;
                              self.PrevSibling = Ecs::kNullEcsEntity;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = 0;
                              self.Kind = kind;
                          });
            return;
        }
        // Component は所有者の Component の連結、Entity は子 Entity の連結の末尾へ付ける
        const bool bComponent = kind == Ecs::kEcsHierarchyKindComponent;
        const Ecs::EcsEntity tail = bComponent ? parentHierarchy->LastComponent : parentHierarchy->LastChild;
        const uint32_t depth = parentHierarchy->Depth + 1;
        EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                      {
                          self.Parent = parentRow;
                          self.PrevSibling = tail;
                          self.NextSibling = Ecs::kNullEcsEntity;
                          self.Depth = depth;
                          self.Kind = kind;
                      });
        if (!tail.IsNull())
        {
            EditHierarchy(ecs, tail, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
        }
        EditHierarchy(ecs, parentRow, [&](Ecs::EcsHierarchy& parent)
                      {
                          Ecs::EcsEntity& first = bComponent ? parent.FirstComponent : parent.FirstChild;
                          Ecs::EcsEntity& last = bComponent ? parent.LastComponent : parent.LastChild;
                          last = row;
                          if (first.IsNull())
                          {
                              first = row;
                          }
                      });
    }

    void World::UnlinkEntityRow(Ecs::EcsEntity row)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(row);
        if (!hierarchy)
        {
            return;
        }
        const Ecs::EcsHierarchy mine = *hierarchy;
        const bool bComponent = mine.Kind == Ecs::kEcsHierarchyKindComponent;
        if (!mine.Parent.IsNull())
        {
            if (mine.PrevSibling.IsNull())
            {
                EditHierarchy(ecs, mine.Parent, [&](Ecs::EcsHierarchy& parent)
                              { (bComponent ? parent.FirstComponent : parent.FirstChild) = mine.NextSibling; });
            }
            else
            {
                EditHierarchy(ecs, mine.PrevSibling, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = mine.NextSibling; });
            }
            if (mine.NextSibling.IsNull())
            {
                EditHierarchy(ecs, mine.Parent, [&](Ecs::EcsHierarchy& parent)
                              { (bComponent ? parent.LastComponent : parent.LastChild) = mine.PrevSibling; });
            }
            else
            {
                EditHierarchy(ecs, mine.NextSibling, [&](Ecs::EcsHierarchy& sibling) { sibling.PrevSibling = mine.PrevSibling; });
            }
        }
        EditHierarchy(ecs, row, [](Ecs::EcsHierarchy& self)
                      {
                          self.Parent = Ecs::kNullEcsEntity;
                          self.PrevSibling = Ecs::kNullEcsEntity;
                          self.NextSibling = Ecs::kNullEcsEntity;
                      });
    }

    void World::RelinkEntityRow(Entity& entity)
    {
        // 親が変わり、深さ優先の番号が変わる。Tick の呼び先と Text の板の順を次の収集で付け直す
        m_bOrderedTicksDirty = true;
        m_bTextBoardOrderDirty = true;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (!ecs.IsAlive(entity.m_EcsEntity))
        {
            // 作成を予約中の Entity は行が無い。同期点で今の親の下に結ばれる
            return;
        }
        UnlinkEntityRow(entity.m_EcsEntity);
        const Entity* parent = entity.GetParentEntity();
        if (parent && parent->m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 新しい親の行はまだ無い。同期点で親の行ができてから結び直す
            m_PendingEntityRelinks.push_back(entity.m_EcsEntity);
        }
        LinkEntityRowToParent(entity.m_EcsEntity, parent ? parent->m_EcsEntity : Ecs::kNullEcsEntity);
        uint32_t depth = 0;
        if (const Ecs::EcsHierarchy* hierarchy = ecs.GetColumnValue<Ecs::EcsHierarchy>(entity.m_EcsEntity))
        {
            depth = hierarchy->Depth;
        }
        UpdateEntitySubtreeDepth(entity, depth);
        ++m_EcsBindingEpoch;
        // 新しい親の下で補間する祖先が変わりうる。履歴は親の ID が変わったことで捨てられる
        NoteRenderInterpolationChange(entity);
    }

    void World::UpdateEntitySubtreeDepth(Entity& entity, uint32_t depth)
    {
        for (Component::Component* component : entity.GetComponents())
        {
            const Ecs::EcsEntity componentRow = component->m_EcsEntity;
            const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(componentRow);
            if (hierarchy && hierarchy->Depth != depth + 1)
            {
                EditHierarchy(*m_EcsWorld, componentRow, [&](Ecs::EcsHierarchy& self) { self.Depth = depth + 1; });
            }
        }
        for (Entity* child : entity.GetChildEntities())
        {
            const Ecs::EcsEntity childRow = child->m_EcsEntity;
            const Ecs::EcsHierarchy* hierarchy = m_EcsWorld->GetColumnValue<Ecs::EcsHierarchy>(childRow);
            if (hierarchy && hierarchy->Depth != depth + 1)
            {
                EditHierarchy(*m_EcsWorld, childRow, [&](Ecs::EcsHierarchy& self) { self.Depth = depth + 1; });
            }
            UpdateEntitySubtreeDepth(*child, depth + 1);
        }
    }

    void World::UnbindEntitySubtreeRows(Entity& entity)
    {
        if (!m_EcsWorld)
        {
            return;
        }
        // 部分木の最上位だけを親の連結から外す（子孫は行ごと消えるので連結は直さない）
        if (!entity.m_EcsEntity.IsNull())
        {
            UnlinkEntityRow(entity.m_EcsEntity);
        }
        ReleaseEntitySubtreeRows(entity);
    }

    void World::ReleaseEntitySubtreeRows(Entity& entity)
    {
        for (Entity* child : entity.GetChildEntities())
        {
            ReleaseEntitySubtreeRows(*child);
        }
        // Component は所有者の葉。所有者の行ごと消えるので、連結は直さない
        for (Component::Component* component : entity.GetComponents())
        {
            ReleaseComponentRow(*component, false);
        }
        ReleaseEntityRow(entity);
    }

    void World::ReleaseEntityRow(Entity& entity)
    {
        if (entity.m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 作成の予約がまだ再生されていない: 札を取り消す（行は無いので消すものも無い）
            const uint32_t slot = entity.m_EcsCreateSlot;
            entity.m_EcsCreateSlot = kNoEntityCreateSlot;
            if (slot < m_PendingEntityCreates.size() && m_PendingEntityCreates[slot].Target == &entity)
            {
                m_EntityCreateCommands.CancelCreate(m_PendingEntityCreates[slot].Ticket);
                m_PendingEntityCreates[slot].Target = nullptr;
            }
            ++m_EcsBindingEpoch;
            return;
        }
        const Ecs::EcsEntity row = entity.m_EcsEntity;
        if (row.IsNull())
        {
            return;
        }
        // 並列の段の中で Object を解放してはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        ++m_EcsBindingEpoch;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (ecs.IsAlive(row))
        {
            // 値を予備へ戻す（行を外す前でないと、行を引けない）
            MoveEntityValuesToFallback(entity);
        }
        entity.m_EcsEntity = Ecs::kNullEcsEntity;
        if (!ecs.IsAlive(row))
        {
            return;
        }
        // Object への参照を先に切り、以後どのクエリにも出ない孤児にする
        ecs.WriteColumnValueOf(row, Ecs::EcsObjectRef{});
        ecs.MarkOrphan(row);
        if (m_Scheduler->IsRunning())
        {
            m_DeferredEntityRows.push_back(row);
        }
        else
        {
            ecs.DestroyEntity(row);
        }
    }

    void World::ReleaseComponentRow(Component::Component& component, bool bUnlink)
    {
        if (component.m_EcsCreateSlot != kNoEntityCreateSlot)
        {
            // 作成の予約がまだ再生されていない: 札を取り消す（行は無いので消すものも無い）
            const uint32_t slot = component.m_EcsCreateSlot;
            component.m_EcsCreateSlot = kNoEntityCreateSlot;
            if (slot < m_PendingEntityCreates.size() && m_PendingEntityCreates[slot].ComponentTarget == &component)
            {
                m_EntityCreateCommands.CancelCreate(m_PendingEntityCreates[slot].Ticket);
                m_PendingEntityCreates[slot].ComponentTarget = nullptr;
            }
            ++m_EcsBindingEpoch;
            return;
        }
        const Ecs::EcsEntity row = component.m_EcsEntity;
        if (row.IsNull())
        {
            return;
        }
        // 並列の段の中で Object を解放してはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        ++m_EcsBindingEpoch;
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        if (ecs.IsAlive(row))
        {
            if (bUnlink)
            {
                UnlinkEntityRow(row);
            }
            // 値を予備へ戻す（行を外す前でないと、行を引けない）
            MoveComponentValuesToFallback(component);
        }
        component.m_EcsEntity = Ecs::kNullEcsEntity;
        if (!ecs.IsAlive(row))
        {
            return;
        }
        // Object への参照を先に切り、以後どのクエリにも出ない孤児にする
        ecs.WriteColumnValueOf(row, Ecs::EcsObjectRef{});
        ecs.MarkOrphan(row);
        if (m_Scheduler->IsRunning())
        {
            m_DeferredEntityRows.push_back(row);
        }
        else
        {
            ecs.DestroyEntity(row);
        }
    }

    void World::FlushDeferredEntityRows()
    {
        if (!m_DeferredEntityRows.empty())
        {
            for (const Ecs::EcsEntity row : m_DeferredEntityRows)
            {
                m_EcsWorld->DestroyEntity(row);
            }
            m_DeferredEntityRows.clear();
        }
        PlaybackEntityRowCreates();
    }

    void World::ReserveComponentRowCreate(Component::Component& component)
    {
        // 並列の段の中で Object を木に付けてはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        PendingEntityCreate pending;
        pending.ComponentTarget = &component;
        pending.Ticket = m_EntityCreateCommands.Create(ComponentArchetypeFor(component));
        component.m_EcsCreateSlot = static_cast<uint32_t>(m_PendingEntityCreates.size());
        m_PendingEntityCreates.push_back(pending);
        ++m_EcsBindingEpoch;
    }

    void World::ReserveEntityRowCreate(Entity& entity)
    {
        // 並列の段の中で Object を木に付けてはならない（ワーカーは Object に触れない）
        assert(!Ecs::Detail::EcsIsParallelActive());
        PendingEntityCreate pending;
        pending.Target = &entity;
        pending.Ticket = m_EntityCreateCommands.Create(m_EntityArchetype);
        entity.m_EcsCreateSlot = static_cast<uint32_t>(m_PendingEntityCreates.size());
        m_PendingEntityCreates.push_back(pending);
        ++m_EcsBindingEpoch;
    }

    void World::PlaybackEntityRowCreates()
    {
        if (m_PendingEntityCreates.empty() && m_PendingEntityRelinks.empty())
        {
            return;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        Ecs::EcsCommandBuffer* buffers[] = {&m_EntityCreateCommands};
        Ecs::EcsCommandBuffer::Replay(ecs, Container::Span<Ecs::EcsCommandBuffer* const>(buffers, 1));

        // 階層を直す親（行を持つ Entity）。Inner の並びから子と Component の連結を作り直す
        Container::VariableArray<Entity*> dirtyParents;
        Container::UnorderedSet<Entity*> dirtySet;
        const auto markEntity = [&](Entity* parent)
        {
            if (parent && dirtySet.insert(parent).second)
            {
                dirtyParents.push_back(parent);
            }
        };
        const auto markParent = [&](Entity& entity) { markEntity(entity.GetParentEntity()); };

        uint32_t failed = 0;
        uint32_t created = 0;
        for (const PendingEntityCreate& pending : m_PendingEntityCreates)
        {
            if (pending.ComponentTarget)
            {
                Component::Component& component = *pending.ComponentTarget;
                component.m_EcsCreateSlot = kNoEntityCreateSlot;
                const Ecs::EcsEntity componentRow = m_EntityCreateCommands.ResolveCreate(pending.Ticket);
                if (componentRow.IsNull())
                {
                    // 再生に失敗した Component は、値を Object に残したまま行なしで残る
                    ++failed;
                    continue;
                }
                ++created;
                component.m_EcsEntity = componentRow;
                Ecs::EcsObjectRef componentRef;
                componentRef.Object = static_cast<void*>(&component);
                componentRef.Generation = componentRow.Generation;
                ecs.WriteColumnValueOf(componentRow, componentRef);
                MoveComponentValuesToRow(component);
                RegisterTextBoardRow(component);
                markEntity(component.GetOwner());
                continue;
            }
            if (!pending.Target)
            {
                continue;
            }
            Entity& entity = *pending.Target;
            entity.m_EcsCreateSlot = kNoEntityCreateSlot;
            const Ecs::EcsEntity row = m_EntityCreateCommands.ResolveCreate(pending.Ticket);
            if (row.IsNull())
            {
                // 再生に失敗した Entity は、値を Object に残したまま行なしで残る
                ++failed;
                continue;
            }
            ++created;
            entity.m_EcsEntity = row;
            Ecs::EcsObjectRef ref;
            ref.Object = static_cast<void*>(&entity);
            ref.Generation = row.Generation;
            ecs.WriteColumnValueOf(row, ref);
            MoveEntityValuesToRow(entity);
            markParent(entity);
            NoteRenderRowCreated(entity);
        }
        for (const Ecs::EcsEntity row : m_PendingEntityRelinks)
        {
            const Ecs::EcsObjectRef* ref = ecs.IsAlive(row) ? ecs.GetColumnValue<Ecs::EcsObjectRef>(row) : nullptr;
            if (ref && ref->Object != nullptr && ref->Generation == row.Generation)
            {
                markParent(*static_cast<Entity*>(ref->Object));
                NoteRenderInterpolationChange(*static_cast<Entity*>(ref->Object));
            }
        }
        m_PendingEntityCreates.clear();
        m_PendingEntityRelinks.clear();
        m_EntityCreateCommands.Clear();

        // 祖先の少ない親から順に直す（子の深さは親の深さから決まる）
        Container::VariableArray<std::pair<uint32_t, Entity*>> ordered;
        for (Entity* parent : dirtyParents)
        {
            uint32_t ancestors = 0;
            for (const Entity* cursor = parent->GetParentEntity(); cursor; cursor = cursor->GetParentEntity())
            {
                ++ancestors;
            }
            ordered.push_back({ancestors, parent});
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const std::pair<uint32_t, Entity*>& lhs, const std::pair<uint32_t, Entity*>& rhs)
                         { return lhs.first < rhs.first; });
        for (const auto& item : ordered)
        {
            RebuildEntityRowChildren(*item.second);
        }
        ++m_EcsBindingEpoch;

        if (failed != 0)
        {
            LOG_ERROR("World: 予約した Entity の行の作成を再生できなかった（%u 件。値は Object に残り、行なしになる）", failed);
        }
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks != nullptr && checks->IsEnabled("ECS_PLAYBACK") && (created != 0 || failed != 0))
        {
            checks->Report("ECS_PLAYBACK", failed == 0, created);
        }
    }

    void World::RebuildEntityRowChildren(Entity& parent)
    {
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        const Ecs::EcsEntity parentRow = parent.m_EcsEntity;
        const Ecs::EcsHierarchy* parentHierarchy = ecs.IsAlive(parentRow) ? ecs.GetColumnValue<Ecs::EcsHierarchy>(parentRow) : nullptr;
        if (!parentHierarchy)
        {
            return;
        }
        const uint32_t parentDepth = parentHierarchy->Depth;
        Ecs::EcsEntity first = Ecs::kNullEcsEntity;
        Ecs::EcsEntity previous = Ecs::kNullEcsEntity;
        for (Entity* child : parent.GetChildEntities())
        {
            const Ecs::EcsEntity row = child->m_EcsEntity;
            if (!ecs.IsAlive(row))
            {
                continue;
            }
            const Ecs::EcsEntity before = previous;
            EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = parentRow;
                              self.PrevSibling = before;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = parentDepth + 1;
                          });
            if (!previous.IsNull())
            {
                EditHierarchy(ecs, previous, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
            }
            else
            {
                first = row;
            }
            previous = row;
        }
        // Component の連結も Inner の並び（Component だけ）から作り直す
        Ecs::EcsEntity firstComponent = Ecs::kNullEcsEntity;
        Ecs::EcsEntity previousComponent = Ecs::kNullEcsEntity;
        for (Component::Component* component : parent.GetComponents())
        {
            const Ecs::EcsEntity row = component->m_EcsEntity;
            if (!ecs.IsAlive(row))
            {
                continue;
            }
            const Ecs::EcsEntity before = previousComponent;
            EditHierarchy(ecs, row, [&](Ecs::EcsHierarchy& self)
                          {
                              self.Parent = parentRow;
                              self.PrevSibling = before;
                              self.NextSibling = Ecs::kNullEcsEntity;
                              self.Depth = parentDepth + 1;
                              self.Kind = Ecs::kEcsHierarchyKindComponent;
                          });
            if (!previousComponent.IsNull())
            {
                EditHierarchy(ecs, previousComponent, [&](Ecs::EcsHierarchy& sibling) { sibling.NextSibling = row; });
            }
            else
            {
                firstComponent = row;
            }
            previousComponent = row;
        }
        EditHierarchy(ecs, parentRow, [&](Ecs::EcsHierarchy& self)
                      {
                          self.FirstChild = first;
                          self.LastChild = previous;
                          self.FirstComponent = firstComponent;
                          self.LastComponent = previousComponent;
                      });
        UpdateEntitySubtreeDepth(parent, parentDepth);
    }

    bool World::VerifyEcsBinding(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || m_Scheduler->IsRunning())
        {
            return true;
        }
        Ecs::EcsWorld& ecs = *m_EcsWorld;
        uint32_t count = 0;
        uint32_t componentCount = 0;
        const char* error = nullptr;
        for (Entity* root : GetRootEntities())
        {
            if (!VerifyEntityRows(ecs, *root, Ecs::kNullEcsEntity, 0, count, componentCount, error))
            {
                break;
            }
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (!error)
        {
            // 行の側からも数える: EcsObjectRef を持つ行（孤児を含む）は、生きた Entity と同じ数だけある
            Ecs::EcsQuery query;
            query.All.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsObjectRef>().Type);
            query.bIncludeOrphans = true;
            Container::VariableArray<Ecs::EcsArchetypeId> archetypes;
            ecs.MatchArchetypes(query, archetypes);
            uint64_t rows = 0;
            for (const Ecs::EcsArchetypeId archetype : archetypes)
            {
                rows += ecs.GetArchetypeRowCount(archetype);
            }
            query.AnyTags = Ecs::kEcsTagOrphan;
            archetypes.clear();
            ecs.MatchArchetypes(query, archetypes);
            uint64_t orphans = 0;
            for (const Ecs::EcsArchetypeId archetype : archetypes)
            {
                orphans += ecs.GetArchetypeRowCount(archetype);
            }
            if (rows != static_cast<uint64_t>(count) + componentCount)
            {
                error = "生きた Entity・Component の数と行の数が一致しない";
            }
            else if (orphans != 0 || !m_DeferredEntityRows.empty())
            {
                error = "孤児の行が残っている";
            }
            else if (!m_PendingEntityRelinks.empty())
            {
                error = "同期点まで待たせた付け替えが残っている";
            }
            else
            {
                for (const PendingEntityCreate& pending : m_PendingEntityCreates)
                {
                    if (pending.Target || pending.ComponentTarget)
                    {
                        error = "再生されていない行の作成の予約が残っている";
                        break;
                    }
                }
            }
        }
        if (error)
        {
            LOG_ERROR("ECS_BIND: %s", error);
            return false;
        }
        return true;
    }

    Ecs::EcsTransformSystem& World::GetEcsTransformSystem()
    {
        if (!m_EcsTransformSystem)
        {
            m_EcsTransformSystem = Container::MakeUnique<Ecs::EcsTransformSystem>(*m_EcsWorld);
            // 行の少ないバケツは直列のまま（既定の閾値）。並列でも直列でも列の値と処理した順は同じ
            m_EcsTransformSystem->SetParallel(true);
        }
        return *m_EcsTransformSystem;
    }

    uint32_t World::PropagateEcsTransforms()
    {
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype)
        {
            return 0;
        }
        Ecs::EcsTransformSystem& system = GetEcsTransformSystem();
        const Ecs::EcsTransformSystem::RunResult run = system.Run();
#if NORVES_ENABLE_STATS
        // 計測: 変わった行（きっかけ）と、伝播で処理した行（きっかけ + 影響する子孫）を数える（ECS_TRANSFORM_COUNTS で出す）
        m_TransformCounts.Seeds += run.Seeds;
        m_TransformCounts.Processed += run.Processed;
        m_TransformCounts.Changed += run.Changed;
#endif
        for (const Ecs::EcsTransformSystem::AppliedRow& applied : system.GetApplied())
        {
            if (applied.bWorldChanged || applied.bFirst)
            {
                // 補間しない行の描画の変換はワールドの変換と同じ。変わった行だけ、描画の更新が版と描画状態を直す
                QueueRenderChange(applied.Entity);
            }
            const Ecs::EcsObjectRef* ref = m_EcsWorld->GetColumnValue<Ecs::EcsObjectRef>(applied.Entity);
            if (ref == nullptr || ref->Object == nullptr)
            {
                continue;
            }
            Entity* entity = static_cast<Entity*>(ref->Object);
            if (applied.bWorldChanged)
            {
                // 旧の再帰と同じく、ワールドの変換が変わった Entity だけ版を進める
                ++entity->m_TransformVersion;
                NoteRenderSyncTransform(applied.Entity);
            }
            entity->m_bWorldTransformDirty = false;
        }
        return run.Unresolved;
    }

    bool World::VerifyEcsTransform(uint32_t* outCount)
    {
        if (outCount)
        {
            *outCount = 0;
        }
        // 予約中の親の下へ付け替えた行は、同期点まで階層の列が古い（Outer/Inner と食い違うのは正当）。この間は伝播も照合もしない
        if (!m_EcsWorld || m_EntityArchetype == Ecs::kInvalidEcsArchetype || !m_PendingEntityRelinks.empty())
        {
            return true;
        }
        const uint32_t unresolved = PropagateEcsTransforms();
        uint32_t count = 0;
        const char* error = nullptr;
        if (unresolved != 0)
        {
            error = "階層のシステムが親の値を求められなかった行がある";
        }
        else
        {
            for (Entity* root : GetRootEntities())
            {
                const Math::Transform identity = Math::Transform::Identity;
                const Ecs::EcsWorldTransform identityColumn{identity.position, identity.rotation, identity.scale};
                if (!VerifyEntityTransforms(*m_EcsWorld, *root, identityColumn, true, count, error))
                {
                    break;
                }
            }
        }
        if (outCount)
        {
            *outCount = count;
        }
        if (error)
        {
            LOG_ERROR("ECS_TRANSFORM: %s", error);
            return false;
        }
        return true;
    }

    void World::RunEcsTransformCheck()
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_TRANSFORM"))
        {
            return;
        }
        ++m_EcsTransformCheckCalls;
        uint32_t count = 0;
        const bool bPass = VerifyEcsTransform(&count);
        // 呼び出しが多いので、fail は毎回、pass は最初に Entity が居た回と 64 回ごとに出す
        const bool bReportPass = bPass && count > 0 &&
                                 (m_EcsTransformLastReport == 0 || m_EcsTransformCheckCalls % 64 == 0);
        if (!bPass || bReportPass)
        {
            checks->Report("ECS_TRANSFORM", bPass, count);
            m_EcsTransformLastReport = m_EcsTransformCheckCalls;
        }
    }

    void World::RunEcsBindingCheck(bool bFinal)
    {
        NorvesLib::Debug::EngineChecks* checks = NorvesLib::Core::Engine::GEngine != nullptr
            ? &NorvesLib::Core::Engine::GEngine->GetEngineChecks()
            : nullptr;
        if (checks == nullptr || !checks->IsEnabled("ECS_BIND"))
        {
            return;
        }
        // 結び付けに変化が無い回は照合しない（World の終了時は必ず行う）
        if (!bFinal && m_EcsBindingEpoch == m_VerifiedBindingEpoch)
        {
            return;
        }
        uint32_t count = 0;
        const bool bPass = VerifyEcsBinding(&count);
        m_VerifiedBindingEpoch = m_EcsBindingEpoch;
        checks->Report("ECS_BIND", bPass, count);
    }

    void World::InitializeEntitySubtreeForWorld(Entity& entity)
    {
        if (!entity.HasFlag(OF_Initialized))
        {
            entity.Initialize();
        }

        for (auto* inner : entity.GetInners())
        {
            if (auto* child = CastTo<Entity>(inner))
            {
                InitializeEntitySubtreeForWorld(*child);
                continue;
            }

            if (auto* component = CastTo<Component::Component>(inner))
            {
                if (!component->HasFlag(OF_Initialized))
                {
                    component->Initialize();
                }
                component->BeginPlay();
            }
        }
    }

    void World::AssignFreshObjectIdsRecursive(Entity& entity)
    {
        entity.SetObjectId(NextObjectId++);

        // 木に入る前に破棄予約された Entity・Component を、一覧へ積む（World の外では積まれないため）
        if (entity.IsPendingDestroy())
        {
            RegisterPendingDestroy(entity);
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            AssignFreshObjectIdsRecursive(*child);
        }
        for (IUnknown* inner : entity.GetInners())
        {
            if (inner->HasFlag(OF_PendingDestroy) && !CastTo<Entity>(inner))
            {
                RegisterPendingDestroy(*inner);
            }
        }
    }

    void World::MarkEntitySubtreeRenderStateDirty(Entity& entity)
    {
        auto components = entity.GetComponents();
        for (auto* component : components)
        {
            component->MarkRenderStateDirty();
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            MarkEntitySubtreeRenderStateDirty(*child);
        }
    }

    void World::MarkEntitySubtreeRenderUnpublished(Entity& entity)
    {
        if (m_EcsWorld)
        {
            auto components = entity.GetComponents();
            for (auto* component : components)
            {
                const Ecs::EcsEntity row = component->GetEcsEntity();
                if (row.IsNull() || !m_EcsWorld->IsAlive(row))
                {
                    continue;
                }
                const Ecs::EcsPublishState* publish = m_EcsWorld->GetColumnValue<Ecs::EcsPublishState>(row);
                if (publish != nullptr && publish->bPublished)
                {
                    RecordRenderPublish(*component, entity, false, 0);
                }
            }
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            MarkEntitySubtreeRenderUnpublished(*child);
        }
    }

    void World::MarkEntitySubtreeTransformDirty(Entity& entity)
    {
        entity.MarkWorldTransformDirty();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            MarkEntitySubtreeTransformDirty(*child);
        }
    }

    void World::NotifyEntitySubtreeAdded(Entity& entity)
    {
        entity.OnAddedToWorld();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            NotifyEntitySubtreeAdded(*child);
        }
    }

    void World::NotifyEntitySubtreeRemoved(Entity& entity)
    {
        entity.OnRemovedFromWorld();

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            NotifyEntitySubtreeRemoved(*child);
        }
    }

    void World::RemoveEntitySubtreeProxies(Entity& entity)
    {
        auto components = entity.GetComponents();

        if (m_SceneView)
        {
            m_SceneView->RemoveMegaGeometryProxy(entity.GetObjectId());

            for (auto* comp : components)
            {
                if (auto* skinnedMeshComp = CastTo<Component::SkinnedMeshComponent>(comp))
                {
                    m_SceneView->RemoveSkinnedMeshProxy(skinnedMeshComp->GetComponentId());
                }
                if (auto* meshComp = CastTo<Component::MeshComponent>(comp))
                {
                    if (CastTo<Component::MegaGeometryComponent>(meshComp) == nullptr)
                    {
                        m_SceneView->RemoveMeshProxy(meshComp->GetComponentId());
                    }
                }
                if (auto* lightComp = CastTo<Component::LightComponent>(comp))
                {
                    m_SceneView->RemoveLightProxy(lightComp->GetComponentId());
                }
                if (auto* boardComp = CastTo<Component::BoardComponent>(comp))
                {
                    m_SceneView->RemoveBoardProxy(boardComp->GetComponentId());
                }
            }
        }

        if (m_ScreenSpaceBoardSink)
        {
            for (auto* comp : components)
            {
                if (auto* boardComp = CastTo<Component::BoardComponent>(comp))
                {
                    m_ScreenSpaceBoardSink->RemoveBoardProxy(boardComp->GetComponentId());
                }
            }
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            RemoveEntitySubtreeProxies(*child);
        }
    }

    bool World::IsEntityRoot(const Entity& entity) const
    {
        return entity.GetOuter() == this && entity.GetWorld() == this;
    }

    bool World::IsEntityInSubtree(const Entity& root, const Entity& candidate) const
    {
        if (&root == &candidate)
        {
            return true;
        }

        auto children = root.GetChildEntities();
        for (auto* child : children)
        {
            if (IsEntityInSubtree(*child, candidate))
            {
                return true;
            }
        }
        return false;
    }

    bool World::ContainsHeapOwnedEntity(const Entity& entity) const
    {
        if (entity.HasFlag(OF_HeapOwned))
        {
            return true;
        }

        auto children = entity.GetChildEntities();
        for (auto* child : children)
        {
            if (ContainsHeapOwnedEntity(*child))
            {
                return true;
            }
        }
        return false;
    }

    bool World::HasPendingEntityAncestor(const Entity& entity) const
    {
        for (Entity* parent = entity.GetParentEntity(); parent != nullptr; parent = parent->GetParentEntity())
        {
            if (parent->IsPendingDestroy())
            {
                return true;
            }
        }
        return false;
    }

    bool World::ContainsInner(const IUnknown& owner, const IUnknown& inner) const
    {
        for (auto* existing : owner.GetInners())
        {
            if (existing == &inner)
            {
                return true;
            }
        }
        return false;
    }

    bool World::EraseInnerReference(IUnknown& owner, IUnknown& inner)
    {
        auto* ownerImpl = dynamic_cast<UnknownImpl*>(&owner);
        if (!ownerImpl)
        {
            return false;
        }

        for (auto it = ownerImpl->m_Inners.begin(); it != ownerImpl->m_Inners.end(); ++it)
        {
            if (*it == &inner)
            {
                ownerImpl->m_Inners.erase(it);
                return true;
            }
        }
        return false;
    }

    void World::AppendInnerReference(IUnknown& owner, IUnknown& inner)
    {
        auto* ownerImpl = dynamic_cast<UnknownImpl*>(&owner);
        if (!ownerImpl)
        {
            return;
        }

        ownerImpl->m_Inners.push_back(&inner);
    }

    void World::SetOuterReference(IUnknown& inner, IUnknown* outer)
    {
        auto* innerImpl = dynamic_cast<UnknownImpl*>(&inner);
        if (!innerImpl)
        {
            return;
        }

        innerImpl->m_Outer = outer;
    }

} // namespace NorvesLib::Core
