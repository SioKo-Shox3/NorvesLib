#include "GameMode/GameModeScope.h"
#include "Object/World.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Rendering/RenderResources.h"

namespace NorvesLib::Core::GameMode
{
    namespace
    {
        bool ContainsEntity(
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*>& entities,
            const NorvesLib::Core::Entity* entity)
        {
            for (const NorvesLib::Core::Entity* existing : entities)
            {
                if (existing == entity)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasTrackedAncestor(
            NorvesLib::Core::Entity* entity,
            NorvesLib::Core::World* world,
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*>& trackedObjects)
        {
            for (NorvesLib::Core::Entity* parent = entity->GetParentEntity(); parent != nullptr; parent = parent->GetParentEntity())
            {
                if (parent->GetWorld() != world)
                {
                    continue;
                }

                if (ContainsEntity(trackedObjects, parent))
                {
                    return true;
                }
            }
            return false;
        }
    }

    GameModeScope::GameModeScope()
        : GameModeScope(nullptr, nullptr)
    {
    }

    GameModeScope::GameModeScope(
        NorvesLib::Core::World* world,
        NorvesLib::Core::Rendering::RenderResources* renderResources)
        : m_AsyncTextureLifetime(Container::MakeShared<AsyncTextureLifetime>())
        , m_pWorld(world)
        , m_pRenderResources(renderResources)
    {
        m_AsyncTextureLifetime->Scope = this;
        m_AsyncTextureLifetime->RenderResources = renderResources;
#if NORVES_ENABLE_STATS
        m_BaselineCounts = CaptureCounts();
#endif
    }

    GameModeScope::~GameModeScope()
    {
        ++m_AsyncTextureLifetime->Generation;
        m_AsyncTextureLifetime->Scope = nullptr;
#if NORVES_ENABLE_STATS
        if (m_pWorld != nullptr && m_pRenderResources != nullptr)
        {
            const auto leaks = CollectLeaks();
            if (!leaks.empty())
            {
                Container::StringBuilder message("GAMEMODE_LEAK");
                for (const GameModeLeakEntry& entry : leaks)
                {
                    message.AppendFormat(" %s=%llu->%llu", entry.Name.c_str(),
                        static_cast<unsigned long long>(entry.Baseline),
                        static_cast<unsigned long long>(entry.Current));
                }
                NORVES_LOG_WARNING("GameMode", "%s ゲームモード終了時の資源数が基準と異なります", message.ToString().c_str());
            }
        }
#endif
    }

    void GameModeScope::TrackObject(NorvesLib::Core::Entity* object)
    {
        if (object == nullptr || ContainsEntity(m_TrackedObjects, object))
        {
            return;
        }

        m_TrackedObjects.push_back(object);
    }

    void GameModeScope::TrackMesh(NorvesLib::Core::Rendering::MeshDataHandle handle)
    {
        m_TrackedMeshes.push_back(handle);
    }

    void GameModeScope::TrackModel(NorvesLib::Core::Rendering::ModelHandle handle)
    {
        m_TrackedModels.push_back(handle);
    }

    void GameModeScope::TrackMaterial(NorvesLib::Core::Rendering::MaterialHandle handle)
    {
        if (handle.IsValid())
        {
            m_TrackedMaterials.push_back(handle);
        }
    }

    void GameModeScope::TrackTexture(NorvesLib::Core::Rendering::TextureHandle handle)
    {
        if (handle.IsValid())
        {
            m_TrackedTextures.push_back(handle);
        }
    }

    void GameModeScope::Untrack(NorvesLib::Core::Entity* object)
    {
        if (object == nullptr)
        {
            return;
        }

        for (auto it = m_TrackedObjects.begin(); it != m_TrackedObjects.end(); ++it)
        {
            if (*it == object)
            {
                m_TrackedObjects.erase(it);
                return;
            }
        }
    }

    void GameModeScope::AddCleanup(GameModeCleanupPhase phase, Delegate<void> callback)
    {
        if (!callback.IsBound())
        {
            return;
        }

        auto& callbacks = phase == GameModeCleanupPhase::BeforeEntities ? m_BeforeEntities : m_AfterEntities;
        callbacks.push_back(std::move(callback));
    }

    void GameModeScope::TrackAsyncModelLoad(uint32_t requestId)
    {
        if (requestId == 0 || m_pRenderResources == nullptr)
        {
            return;
        }

        AddCleanup(GameModeCleanupPhase::BeforeEntities, [resources = m_pRenderResources, requestId]()
        {
            resources->MegaGeometry().CancelModelLoad(requestId);
        });
    }

    Delegate<void, Rendering::TextureHandle> GameModeScope::WrapAsyncTextureCallback(
        Delegate<void, Rendering::TextureHandle> onLoaded,
        GameModeAsyncTextureOwnership ownership)
    {
        return [lifetime = m_AsyncTextureLifetime, generation = m_AsyncTextureLifetime->Generation,
                onLoaded = std::move(onLoaded), ownership](Rendering::TextureHandle handle)
        {
            if (lifetime->Generation == generation && lifetime->Scope != nullptr)
            {
                if (ownership == GameModeAsyncTextureOwnership::Scope)
                {
                    lifetime->Scope->TrackTexture(handle);
                }
                onLoaded.InvokeIfBound(handle);
            }
            else if (ownership == GameModeAsyncTextureOwnership::Scope && lifetime->RenderResources != nullptr)
            {
                lifetime->RenderResources->Textures().ReleaseTexture(handle);
            }
        };
    }

    GameModeScopeCounts GameModeScope::CaptureCounts() const
    {
        GameModeScopeCounts counts;
        if (m_pWorld != nullptr)
        {
            counts.RootEntities = m_pWorld->GetObjectCount();
        }
        if (m_pRenderResources != nullptr)
        {
            // TextureCount は登録中の数であり、GPU の遅延破棄待ちは含まない。
            counts.Textures = m_pRenderResources->GetResourceStats().TextureCount;
            counts.Meshes = m_pRenderResources->Meshes().GetCount();
            counts.Models = m_pRenderResources->MegaGeometry().GetModelCount();
            counts.PendingAsyncLoads = static_cast<uint64_t>(m_pRenderResources->Textures().GetPendingAsyncLoadCount())
                + m_pRenderResources->MegaGeometry().GetPendingAsyncModelLoadCount();
        }
        return counts;
    }

    void GameModeScope::AddLeakCounter(const char* name, Delegate<uint64_t> counter)
    {
#if NORVES_ENABLE_STATS
        if (name == nullptr || !counter.IsBound())
        {
            return;
        }

        const uint64_t baseline = counter.Invoke();
        m_LeakCounters.push_back({Container::String(name), std::move(counter), baseline});
#else
        (void)name;
        (void)counter;
#endif
    }

    Container::VariableArray<GameModeLeakEntry> GameModeScope::CollectLeaks() const
    {
        Container::VariableArray<GameModeLeakEntry> leaks;
#if NORVES_ENABLE_STATS
        const GameModeScopeCounts current = CaptureCounts();
        auto collect = [&leaks](const Container::String& name, uint64_t baseline, uint64_t value)
        {
            if (baseline != value)
            {
                leaks.push_back({name, baseline, value});
            }
        };
        collect("RootEntities", m_BaselineCounts.RootEntities, current.RootEntities);
        collect("Textures", m_BaselineCounts.Textures, current.Textures);
        collect("Meshes", m_BaselineCounts.Meshes, current.Meshes);
        collect("Models", m_BaselineCounts.Models, current.Models);
        collect("PendingAsyncLoads", m_BaselineCounts.PendingAsyncLoads, current.PendingAsyncLoads);
        for (const LeakCounter& counter : m_LeakCounters)
        {
            collect(counter.Name, counter.Baseline, counter.Counter.Invoke());
        }
#endif
        return leaks;
    }

    void GameModeScope::Cleanup()
    {
        if (m_bCleaningUp)
        {
            return;
        }

        m_bCleaningUp = true;
        struct CleanupGuard
        {
            bool& Flag;
            ~CleanupGuard()
            {
                Flag = false;
            }
        } guard{m_bCleaningUp};
        ++m_AsyncTextureLifetime->Generation;

        auto drain = [](Container::VariableArray<Delegate<void>>& callbacks)
        {
            while (!callbacks.empty())
            {
                Delegate<void> callback = std::move(callbacks.back());
                callbacks.pop_back();
                callback.Invoke();
            }
        };

        do
        {
            drain(m_BeforeEntities);

            // 1) 親子の追跡の重複を除き、Entity を World から除去する
            if (m_pWorld)
            {
                NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*> toDestroy;
                for (NorvesLib::Core::Entity* obj : m_TrackedObjects)
                {
                    if (obj == nullptr || obj->GetWorld() != m_pWorld)
                    {
                        continue;
                    }

                    if (HasTrackedAncestor(obj, m_pWorld, m_TrackedObjects))
                    {
                        continue;
                    }

                    if (!ContainsEntity(toDestroy, obj))
                    {
                        toDestroy.push_back(obj);
                    }
                }

                for (NorvesLib::Core::Entity* obj : toDestroy)
                {
                    if (obj == nullptr || obj->GetWorld() != m_pWorld)
                    {
                        continue;
                    }

                    if (obj->GetParentEntity() == nullptr)
                    {
                        m_pWorld->RemoveObject(obj);
                    }
                    else
                    {
                        m_pWorld->RemoveEntity(obj);
                    }
                }
            }
            m_TrackedObjects.clear();

            drain(m_AfterEntities);

            // 2) MeshDataHandle を MeshResources::Unregister で解放
            // 3) ModelHandle を MegaGeometryResources::ReleaseModel で解放
            if (m_pRenderResources)
            {
                for (NorvesLib::Core::Rendering::MeshDataHandle handle : m_TrackedMeshes)
                {
                    m_pRenderResources->Meshes().Unregister(handle);
                }
                m_TrackedMeshes.clear();

                for (NorvesLib::Core::Rendering::ModelHandle handle : m_TrackedModels)
                {
                    m_pRenderResources->MegaGeometry().ReleaseModel(handle);
                }

                // 4) 材質を解放する。材質が参照する VT は材質ごとに作られるので、材質のデータが消える前に集めて返す。
                //    複数の材質が同じ VT を参照することがあるので、重複は除く。ReleaseTexture は GPU が使い終わるまで
                //    実体の破棄を待つ。
                NorvesLib::Core::Rendering::TextureResources& textures = m_pRenderResources->Textures();
                NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::TextureHandle> ownedTextures;
                auto collectOwnedTexture =
                    [&](NorvesLib::Core::Rendering::TextureHandle texture, bool bOnlyVirtual)
                {
                    uint32_t virtualIndex = 0;
                    if (!texture.IsValid() || (bOnlyVirtual && !textures.TryGetVirtualTextureIndex(texture, virtualIndex)))
                    {
                        return;
                    }
                    for (const NorvesLib::Core::Rendering::TextureHandle existing : ownedTextures)
                    {
                        if (existing == texture)
                        {
                            return;
                        }
                    }
                    ownedTextures.push_back(texture);
                };
                for (NorvesLib::Core::Rendering::MaterialHandle handle : m_TrackedMaterials)
                {
                    const NorvesLib::Core::Rendering::MaterialResourceData* data = m_pRenderResources->Materials().GetData(handle);
                    if (data != nullptr)
                    {
                        collectOwnedTexture(data->AlbedoTexture, true);
                        collectOwnedTexture(data->NormalTexture, true);
                        collectOwnedTexture(data->MetallicTexture, true);
                        collectOwnedTexture(data->RoughnessTexture, true);
                        collectOwnedTexture(data->AOTexture, true);
                        collectOwnedTexture(data->ORMTexture, true);
                        collectOwnedTexture(data->HeightTexture, true);
                    }
                    m_pRenderResources->Materials().Release(handle);
                }

                // 5) 追跡したテクスチャ（匿名）を解放する
                for (NorvesLib::Core::Rendering::TextureHandle handle : m_TrackedTextures)
                {
                    collectOwnedTexture(handle, false);
                }
                for (NorvesLib::Core::Rendering::TextureHandle handle : ownedTextures)
                {
                    textures.ReleaseTexture(handle);
                }
            }
            m_TrackedMeshes.clear();
            m_TrackedModels.clear();
            m_TrackedMaterials.clear();
            m_TrackedTextures.clear();
            // 終了済みの段へ追加された後始末も、同じ Cleanup の中で実行する。
        } while (!m_BeforeEntities.empty() || !m_AfterEntities.empty());
    }

    bool GameModeScope::IsEmpty() const
    {
        return m_TrackedObjects.empty()
            && m_TrackedMeshes.empty()
            && m_TrackedModels.empty()
            && m_TrackedMaterials.empty()
            && m_TrackedTextures.empty()
            && m_BeforeEntities.empty()
            && m_AfterEntities.empty();
    }

} // namespace NorvesLib::Core::GameMode
