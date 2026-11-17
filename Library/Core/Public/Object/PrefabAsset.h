#pragma once

#include "Object/Resource.h"
#include "Object/SchemaProjection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core
{
    template <typename T>
    class ResourceRef;

    class Entity;
    class PrefabAsset;
    struct PrefabNestedPrefabInstance;
    struct PrefabNestedPrefabStorage;

    using PrefabNestedInstanceId = uint64_t;
    inline constexpr PrefabNestedInstanceId InvalidPrefabNestedInstanceId = 0;

    struct PrefabTargetPath
    {
        Container::VariableArray<PrefabNestedInstanceId> NestedInstances;
        SubtreeSnapshotAliasId Alias = InvalidSubtreeSnapshotAliasId;
    };

    struct PrefabPropertyOverride
    {
        PrefabTargetPath Target;
        ProjectedPropertyValue Value;
    };

    struct PrefabOverrideSet
    {
        Container::VariableArray<PrefabPropertyOverride> Properties;
    };

    class PrefabAsset : public Resource
    {
        REFLECTION_CLASS(PrefabAsset, Resource)

    public:
        PrefabAsset();
        explicit PrefabAsset(const FieldInitializer* initializer);
        explicit PrefabAsset(const IUnknown* sourceObject);
        virtual ~PrefabAsset();

        const EntitySubtreeSnapshot& GetTree() const { return m_Tree; }
        void SetTree(const EntitySubtreeSnapshot& tree);
        void SetTree(EntitySubtreeSnapshot&& tree);
        bool HasTree() const { return m_bHasTree; }

        const Container::VariableArray<PrefabNestedPrefabInstance>& GetNestedPrefabs() const;
        /// 次に入れ子へ割り当てるインスタンス番号を返す。
        PrefabNestedInstanceId GetNextNestedInstanceId() const { return m_NextNestedInstanceId; }

        /// 既存の全インスタンス番号より大きい有効な値を、次の番号として復元する。
        bool SetNextNestedInstanceId(PrefabNestedInstanceId nextInstanceId);

        /// 保存されたインスタンス番号を維持して入れ子を復元し、次の番号を必要に応じて進める。
        bool RestoreNestedPrefab(
            PrefabNestedInstanceId instanceId,
            SubtreeSnapshotAliasId parentAlias,
            const ResourceRef<PrefabAsset>& prefab,
            const PrefabOverrideSet& overrides = PrefabOverrideSet());

        PrefabNestedInstanceId AddNestedPrefab(
            SubtreeSnapshotAliasId parentAlias,
            const ResourceRef<PrefabAsset>& prefab,
            const PrefabOverrideSet& overrides = PrefabOverrideSet());

        PrefabOverrideSet BuildOverridesFrom(const Entity& editedRoot) const;

    private:
        EntitySubtreeSnapshot m_Tree;
        Container::TUniquePtr<PrefabNestedPrefabStorage> m_NestedStorage;
        PrefabNestedInstanceId m_NextNestedInstanceId = 1;
        bool m_bHasTree = false;
    };

} // namespace NorvesLib::Core

#include "Object/ResourceRef.h"

namespace NorvesLib::Core
{
    struct PrefabNestedPrefabInstance
    {
        PrefabNestedInstanceId InstanceId = InvalidPrefabNestedInstanceId;
        SubtreeSnapshotAliasId ParentAlias = InvalidSubtreeSnapshotAliasId;
        ResourceRef<PrefabAsset> Prefab;
        PrefabOverrideSet Overrides;
    };

    bool ShouldSkipPrefabRestoreProperty(const IClass& concreteClass, const ClassProperty& property);

} // namespace NorvesLib::Core
