#pragma once

#include "Object/SchemaProjection.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    /**
     * @brief シーンJSON上のプロパティ1件。StableIdに加えて名前文字列を併記する（改名耐性・可読性）。
     */
    struct ScenePropertyRecord
    {
        Container::String Name;
        StablePropertyId PropertyId = InvalidSchemaId;
        Container::String TypeName;
        StableTypeId TypeId = InvalidSchemaId;
        Container::String Value;
    };

    struct SceneObjectRecord
    {
        Container::String ClassName;
        StableClassId ClassId = InvalidSchemaId;
        Container::String Path;
        Container::VariableArray<ScenePropertyRecord> Properties;
    };

    struct SceneComponentRecord
    {
        SubtreeSnapshotAliasId Alias = InvalidSubtreeSnapshotAliasId;
        SubtreeSnapshotAliasId OwnerAlias = InvalidSubtreeSnapshotAliasId;
        SceneObjectRecord Object;
    };

    struct SceneEntityRecord
    {
        SubtreeSnapshotAliasId Alias = InvalidSubtreeSnapshotAliasId;
        SubtreeSnapshotAliasId ParentAlias = InvalidSubtreeSnapshotAliasId;
        SceneObjectRecord Object;
        Container::VariableArray<SceneComponentRecord> Components;
        Container::VariableArray<SceneEntityRecord> Children;
    };

    struct SceneRootRecord
    {
        uint32_t FormatVersion = 1;
        SubtreeSnapshotAliasId RootAlias = InvalidSubtreeSnapshotAliasId;
        Container::String RootPath;
        SceneEntityRecord Root;
    };

    /**
     * @brief 複数ルートEntityを束ねるシーンドキュメント。
     */
    struct SceneDocument
    {
        uint32_t FormatVersion = 1;
        Container::VariableArray<SceneRootRecord> Roots;
    };

    /**
     * @brief 寛容ロードのフィルタ統計。dropは全てNORVES_LOG_WARNINGにも計上される。
     */
    struct SceneLoadStats
    {
        size_t LoadedRoots = 0;
        size_t DroppedEntities = 0;
        size_t DroppedComponents = 0;
        size_t DroppedProperties = 0;
    };

    /**
     * @brief EntitySubtreeSnapshot群とシーンJSONの相互変換（GameThread専用）。
     *
     * RuntimeSchemaProjectorと同型のstaticユーティリティ。インスタンス化しない。
     * ReconcileWithSchemaは現行ClassRegistry/TypeRegistryと突き合わせ、未知クラス・
     * 未知プロパティ・型不一致・値破損・formatVersion不一致ルートを事前フィルタ＋
     * 警告ログし、既知の残存経路（IClass::NewInstance失敗・World未初期化）を除き
     * World::SpawnPrefabが失敗しないスナップショットを出力する（World.cpp非改変）。
     * outStatsは呼び出し時にゼロ化される（累積しない）。
     * aliasはロード時に再採番されるため、JSON中のalias値は診断用途のみ。
     */
    class SceneSerializer
    {
    public:
        static constexpr uint32_t SceneFileFormatVersion = 1;

        static SceneDocument BuildDocument(const Container::VariableArray<EntitySubtreeSnapshot>& roots);
        static Container::String ToJson(const SceneDocument& document);
        static bool TryParseJson(
            const Container::String& jsonText,
            SceneDocument& outDocument,
            Container::String* pOutError = nullptr);
        static bool ReconcileWithSchema(
            const SceneDocument& document,
            Container::VariableArray<EntitySubtreeSnapshot>& outRoots,
            SceneLoadStats& outStats);
    };

} // namespace NorvesLib::Core::Scene
