#pragma once

#include "Object.h"
#include "Reflection.h"
#include "Entity.h"
#include "Component/Component.h"
#include "Container/Containers.h"
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    class MaterialResources;
    class SceneView;
}

namespace NorvesLib::Core
{
    class PrefabAsset;
    struct PrefabOverrideSet;

    /**
     * @brief ゲームワールド
     *
     * Objectを継承し、EntityをInner（子オブジェクト）として管理します。
     * EntityはOuter（親）をたどることでWorldを取得できます。
     *
     * 責務:
     * - Entityのライフサイクル管理（Inner/Outerで親子付け）
     * - 毎フレームのTick更新
     * - MeshComponentからSceneViewへのMeshProxy同期
     * - LightComponentからSceneViewへのLightProxy同期
     *
     * Worldが破棄されると、Inner全てが連鎖破棄されます。
     */
    class World : public Object
    {
        REFLECTION_CLASS(World, Object)

    public:
        World();
        explicit World(const FieldInitializer *initializer);
        explicit World(const IUnknown *sourceObject);
        virtual ~World();

        virtual void Initialize() override;
        virtual void Finalize() override;

        /**
         * @brief World管理下のEntityを新規生成します
         * @tparam T Entity派生型
         * @return 生成されたEntity。所有権はWorldが持ちます。
         */
        template <typename T = Entity>
        T *SpawnEntity(Entity* parent = nullptr)
        {
            static_assert(std::is_base_of_v<Entity, T>, "T must derive from Entity");

            if (!HasFlag(OF_Initialized))
            {
                return nullptr;
            }

            T *object = nullptr;
            try
            {
                object = new T();
            }
            catch (...)
            {
                return nullptr;
            }

            if (!object)
            {
                return nullptr;
            }

            const bool bAttached = parent
                ? AttachChildEntity(parent, object)
                : AttachRootEntity(object);
            if (!bAttached)
            {
                if (object->HasFlag(OF_Initialized))
                {
                    object->Finalize();
                }
                delete object;
                return nullptr;
            }

            return object;
        }

        /**
         * @brief World管理下のルートEntityを新規生成します（互換API）
         * @tparam T Entity派生型
         * @return 生成されたEntity。所有権はWorldが持ちます。
         */
        template <typename T = Entity>
        T *SpawnObject()
        {
            return SpawnEntity<T>(nullptr);
        }

        Entity* SpawnPrefab(
            const PrefabAsset& prefab,
            Entity* parent = nullptr,
            const PrefabOverrideSet* overrides = nullptr);

        /**
         * @brief Entityに紐づくComponentを新規生成します
         * @tparam T Component派生型
         * @param owner コンポーネントを所有するEntity
         * @return 生成されたComponent。所有権はownerが持ちます。
         */
        template <typename T>
        T *CreateComponent(Entity *owner)
        {
            static_assert(std::is_base_of_v<Component::Component, T>, "T must derive from Component");

            if (!owner || owner->GetWorld() != this)
            {
                return nullptr;
            }

            T *component = nullptr;
            try
            {
                component = new T();
            }
            catch (...)
            {
                return nullptr;
            }

            if (!component)
            {
                return nullptr;
            }

            if (!owner->AddComponent(component))
            {
                delete component;
                return nullptr;
            }

            return component;
        }

        /**
         * @brief EntityをInnerとして追加
         * @param object 追加するEntity（OuterがこのWorldに設定されます）
         */
        bool AddObject(Entity *object);

        /**
         * @brief EntityをInnerから除去
         * @param object 除去するEntity
         */
        void RemoveObject(Entity *object);

        /**
         * @brief EntityをWorldツリーから除去
         * @param entity 除去するEntity（root/childどちらも可）
         * @return 除去できた場合true
         */
        bool RemoveEntity(Entity* entity);

        /**
         * @brief Entityを同一World内の別親へ移動
         * @param entity 移動するEntity
         * @param newParent 新しい親。nullptrならWorld直下rootへ移動
         * @return 移動できた場合true
         */
        bool ReparentEntity(Entity* entity, Entity* newParent);

        /**
         * @brief public AddInnerからのEntity直追加を拒否します
         */
        virtual bool AddInner(IUnknown* inner) override;

        /**
         * @brief 管理下のルートEntityを取得（World直下のInnersからフィルタリング）
         */
        Container::VariableArray<Entity *> GetRootEntities() const;

        /**
         * @brief World直下のルートEntity数を取得
         */
        size_t GetObjectCount() const;

        /**
         * @brief 全オブジェクトのTick更新
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        void Tick(float deltaTime);

        /**
         * @brief 描画先SceneViewを設定
         * @param sceneView MeshProxyの送信先
         */
        void SetSceneView(Rendering::SceneView *sceneView);

        /**
         * @brief SceneViewを取得
         */
        Rendering::SceneView *GetSceneView() const { return m_SceneView; }

        /**
         * @brief MeshComponent/LightComponentからSceneViewへProxy同期
         */
        void SyncToSceneView(const Rendering::MaterialResources *materials = nullptr);

        /**
         * @brief Entity階層のワールドトランスフォームを更新
         */
        void UpdateWorldTransforms();

    private:
        void CleanupDestroyedObjects();
        void UpdateEntityTransformRecursive(Entity& entity, const Math::Transform& parentWorld);
        void TickEntityRecursive(Entity& entity, float deltaTime);
        void SyncEntityRecursive(Entity& entity,
                                 const Rendering::MaterialResources* materials,
                                 Container::UnorderedSet<uint64_t>& liveMeshObjectIds,
                                 Container::UnorderedSet<uint64_t>& liveMegaGeometryObjectIds,
                                 Container::UnorderedSet<uint64_t>& liveLightIds);
        void CollectPendingDestroyRecursive(Entity& entity, Container::VariableArray<Entity*>& toRemove);
        bool AttachRootEntity(Entity* entity);
        bool AttachChildEntity(Entity* parent, Entity* child);
        bool MoveEntityInnerAtomic(Entity& entity, IUnknown& newOwner);
        bool DestroyEntitySubtree(Entity& entity);
        void InitializeEntitySubtreeForWorld(Entity& entity);
        void AssignFreshObjectIdsRecursive(Entity& entity);
        void MarkEntitySubtreeRenderStateDirty(Entity& entity);
        void MarkEntitySubtreeTransformDirty(Entity& entity);
        void NotifyEntitySubtreeAdded(Entity& entity);
        void NotifyEntitySubtreeRemoved(Entity& entity);
        void RemoveEntitySubtreeProxies(Entity& entity);
        bool IsEntityRoot(const Entity& entity) const;
        bool IsEntityInSubtree(const Entity& root, const Entity& candidate) const;
        bool ContainsHeapOwnedEntity(const Entity& entity) const;
        bool HasPendingEntityAncestor(const Entity& entity) const;
        bool ContainsInner(const IUnknown& owner, const IUnknown& inner) const;
        bool EraseInnerReference(IUnknown& owner, IUnknown& inner);
        void AppendInnerReference(IUnknown& owner, IUnknown& inner);
        void SetOuterReference(IUnknown& inner, IUnknown* outer);

        // システムポインタ（リフレクション対象外）
        Rendering::SceneView *m_SceneView = nullptr;

        // リフレクションプロパティ
        PROPERTY(uint64_t, NextObjectId)
    };

} // namespace NorvesLib::Core

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::World, NorvesLib::Core::EClassCastFlags::World)
