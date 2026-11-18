#pragma once

#include "Object/SchemaProjection.h"
#include "Asset/AssetManifest.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class PrefabAsset;
    class ResourceRegistry;
    template <typename T>
    class ResourceRef;
    class World;

    namespace Asset
    {
        class AssetSystem;
    }
}

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
        uint32_t FormatVersion = 3;
        SubtreeSnapshotAliasId RootAlias = InvalidSubtreeSnapshotAliasId;
        Container::String RootPath;
        SceneEntityRecord Root;
    };

    /// シーンから参照する資産。Variant の空文字は参照側で指定しない既定の形式を表す。
    struct SceneAssetDependency
    {
        Container::String Path;
        Asset::AssetKind Kind = Asset::AssetKind::Unknown;
        Container::String Variant;
    };

    struct SceneSaveStats
    {
        size_t SavedRoots = 0;
        size_t DroppedProperties = 0;
    };

    struct SceneSaveOptions
    {
        /// 一時ファイルに一部を書いた後で保存を失敗させる試験用の指定。
        bool bInjectWriteFailureForTesting = false;
    };

    /**
     * @brief 複数ルートEntityを束ねるシーンドキュメント。
     */
    struct SceneDocument
    {
        uint32_t FormatVersion = 3;
        Container::VariableArray<SceneAssetDependency> Assets;
        Container::VariableArray<SceneRootRecord> Roots;
    };

    /// Prefab の木または入れ子の対象に適用するプロパティの上書き。
    struct PrefabOverrideRecord
    {
        /// PrefabNestedInstanceId と同じ値を、外側から内側の順に並べる。
        Container::VariableArray<uint64_t> TargetNested;
        SubtreeSnapshotAliasId TargetAlias = InvalidSubtreeSnapshotAliasId;
        /// Value に上書きする値の文字列表現を保持する。
        ScenePropertyRecord Property;
    };

    /// Prefab の木に配置された入れ子の Prefab と、その上書き。
    struct PrefabNestedRecord
    {
        /// PrefabNestedInstanceId と同じ値。
        uint64_t InstanceId = 0;
        SubtreeSnapshotAliasId ParentAlias = InvalidSubtreeSnapshotAliasId;
        Container::String PrefabPath;
        Container::VariableArray<PrefabOverrideRecord> Overrides;
    };

    /// 単一のルートを持つ Prefab の木と入れ子の配置。
    struct PrefabDocument
    {
        SceneDocument Tree;
        uint64_t NextNestedInstanceId = 1;
        Container::VariableArray<PrefabNestedRecord> Nested;
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
        size_t DroppedNestedPrefabs = 0;
        size_t DroppedOverrides = 0;
    };

    enum class ESceneSpawnStepResult : uint8_t
    {
        InProgress,
        Done
    };

    /**
     * @brief GameThread で進めるシーン生成の状態。
     * 完了前に破棄すると、一時資産だけが解放され、生成済みのルートは World に残る。
     * 生成済みのルートを取り消すには CancelSpawn を使う。
     */
    class SceneSpawnJob
    {
    public:
        SceneSpawnJob();
        ~SceneSpawnJob();
        SceneSpawnJob(SceneSpawnJob&&) noexcept;
        SceneSpawnJob& operator=(SceneSpawnJob&&) noexcept;
        SceneSpawnJob(const SceneSpawnJob&) = delete;
        SceneSpawnJob& operator=(const SceneSpawnJob&) = delete;

        bool IsValid() const;
        bool IsFinished() const;
        size_t GetRootCount() const;
        size_t GetNextRootIndex() const;
        const SceneLoadStats& GetStats() const;
        bool HasFailures() const;
        const Container::VariableArray<uint64_t>& GetSpawnedRootIds() const;

    private:
        friend class SceneSerializer;
        struct State;
        Container::TUniquePtr<State> m_State;
    };

    /**
     * @brief EntitySubtreeSnapshot群とシーンJSONの相互変換（GameThread専用）。
     * ReadDocumentFromFile・ReadDocumentFromAsset はワーカーでも呼べる。
     *
     * RuntimeSchemaProjectorと同型のstaticユーティリティ。インスタンス化しない。
     * ReconcileWithSchemaは現行ClassRegistry/TypeRegistryと突き合わせ、未知クラス・
     * 未知プロパティ・型不一致・値破損・formatVersion不一致ルートを事前フィルタ＋
     * 警告ログし、既知の残存経路（IClass::NewInstance失敗・World未初期化）を除き
     * World::SpawnPrefabが失敗しないスナップショットを出力する（World.cpp非改変）。
     * outStatsは呼び出し時にゼロ化される（累積しない）。
     * EntityRefIdは文書中のルートの0始まりの添字とEntityのaliasを「添字:alias」で保存し、
     * 参照なしは「0」で保存する。保存対象外への参照は警告して参照なしにする。
     * aliasはロード時に再採番されるため、文書中のaliasと生成用aliasの対応を保持する。
     * ReconcileWithSchemaはEntityRefIdをスナップショットから外す。参照の張り替えは
     * LoadIntoWorld内で全ルートの生成後（各ComponentのBeginPlayより後）に行う。
     */
    class SceneSerializer
    {
    public:
        static constexpr uint32_t SceneFileFormatVersion = 3;
        static constexpr const char* PrefabFileSuffix = ".prefab.json";

        static SceneDocument BuildDocument(const Container::VariableArray<EntitySubtreeSnapshot>& roots);
        static Container::String ToJson(const SceneDocument& document);
        static bool TryParseJson(
            const Container::String& jsonText,
            SceneDocument& outDocument,
            Container::String* pOutError = nullptr);
        static Container::String PrefabToJson(const PrefabDocument& document);
        static bool TryParsePrefabJson(
            const Container::String& jsonText,
            PrefabDocument& outDocument,
            Container::String* pOutError = nullptr);
        /// alias を維持して Prefab の木・入れ子・上書きを文書にする。
        static bool BuildPrefabDocument(const PrefabAsset& prefab, PrefabDocument& outDocument,
                                        Container::String* pOutError = nullptr);
        /// Prefab 文書を一時ファイルに書き、保存先を置換する。
        static bool SavePrefabToFile(const PrefabAsset& prefab, const Container::String& filePath,
                                     Container::String* pOutError = nullptr, const SceneSaveOptions& options = {});
        static bool ReconcileWithSchema(
            const SceneDocument& document,
            Container::VariableArray<EntitySubtreeSnapshot>& outRoots,
            SceneLoadStats& outStats);
        /**
         * @brief シーンファイルの読み込みと構造解析を行う。ワーカーで呼べる。
         * 文書は版の移行と物理量の検証の前であり、ReconcileWithSchema には直接渡せない。
         * BeginSpawn に渡すと移行と検証を行う。
         */
        static bool ReadDocumentFromFile(const Container::String& filePath, SceneDocument& outDocument,
                                         Container::String* pOutError = nullptr);

        /**
         * @brief シーン資産の解決と構造解析を行う。ワーカーで呼べる。
         * 文書の扱いは ReadDocumentFromFile と同じ。
         * AssetSystem の SetManifest・ResetManifest・LoadManifestFromJsonText と同時に呼ばない。
         */
        static bool ReadDocumentFromAsset(const Asset::AssetSystem& assetSystem,
                                          Container::AnsiStringView logicalPath, SceneDocument& outDocument,
                                          Container::String* pOutError = nullptr);

        /**
         * @brief スキーマ照合を行い、シーン生成を準備する。GameThread 専用。
         * 既存の未完了ジョブを上書きすると、その生成済みルートは World に残る。
         * 失敗時は pOutStats にその時点の統計を返す。
         * TryParseJson で移行済みの文書も渡せ、再検証しても文書は変化しない。
         */
        static bool BeginSpawn(SceneDocument document, const Container::String& sourceName,
                               SceneSpawnJob& outJob, SceneLoadStats* pOutStats = nullptr);

        /**
         * @brief ルートを最大 1 つ生成し、最後のルートの後で参照を張り替える。GameThread 専用。
         * pOutRoots は空にせず、成功したルートを末尾に追加する。
         * 同じジョブの全呼び出しで同じ生存中の World を渡す。
         */
        static ESceneSpawnStepResult SpawnNextRoot(World& world, SceneSpawnJob& job,
                                                   Container::VariableArray<Entity*>* pOutRoots = nullptr);

        /// 未完了ジョブの生成済みルートを同じ World から取り除く。GameThread 専用。
        static void CancelSpawn(World& world, SceneSpawnJob& job);

        // ---- 入口API（GameThread限定。Worldのライブ状態を読む/書くため） ----

        /**
         * @brief Worldのルート群をスナップショット化する。
         * bPendingDestroyのEntity（部分木ごと）とComponentを除外し、
         * シリアライズ不能で落ちたプロパティ数をNORVES_LOGに計上する。
         * @return outRootsへ書き出したルート数
         */
        static size_t CaptureWorld(const World& world, Container::VariableArray<EntitySubtreeSnapshot>& outRoots,
                                   SceneSaveStats* pOutStats = nullptr);

        /**
         * @brief WorldをシーンJSONファイルへ保存する。
         */
        static bool SaveToFile(const World& world, const Container::String& filePath,
                               SceneSaveStats* pOutStats = nullptr, const SceneSaveOptions& options = {});

        /**
         * @brief シーンJSONファイルをWorldへ加算ロードする（既存Entityはクリアしない）。
         * 復元Entity/ComponentのIDはAssignFreshObjectIdsRecursiveで再採番される（仕様）。
         * EntityRefIdは全ルートのSpawnPrefab完了後、BeginPlayより後に新しいObjectIdへ張り替える。
         * 事前フィルタ済みルートのSpawnPrefabが1つでも失敗した場合false（成功分は残る）。
         * pOutRootsは呼び出し時に空にされ、生成に成功したルートが文書の順に入る。
         * 返したルートはWorldが所有する。GameModeScope::TrackObjectに渡すとモードの終わりに消える。
         */
        static bool LoadIntoWorld(World& world, const Container::String& filePath,
                                  SceneLoadStats* pOutStats = nullptr,
                                  Container::VariableArray<Entity*>* pOutRoots = nullptr);

        /**
         * @brief 論理パスからシーンを解決し、Worldへ加算ロードする。
         * 出力と所有権はLoadIntoWorldと同じ。失敗時も生成に成功したルートは残る。
         */
        static bool LoadIntoWorldFromAsset(World& world, const Asset::AssetSystem& assetSystem,
                                           Container::AnsiStringView logicalPath,
                                           SceneLoadStats* pOutStats = nullptr,
                                           Container::VariableArray<Entity*>* pOutRoots = nullptr);

        /// 論理パスの Prefab を読み、registry に登録済みなら再利用する。
        static ResourceRef<PrefabAsset> LoadPrefabFromAsset(ResourceRegistry& registry,
                                                           const Asset::AssetSystem& assetSystem,
                                                           Container::AnsiStringView logicalPath,
                                                           SceneLoadStats* pOutStats = nullptr);

        /// Prefab またはシーンの資産を World に加算する。
        static bool LoadIntoWorldFromAsset(World& world, ResourceRegistry& registry,
                                           const Asset::AssetSystem& assetSystem,
                                           Container::AnsiStringView logicalPath,
                                           SceneLoadStats* pOutStats = nullptr,
                                           Container::VariableArray<Entity*>* pOutRoots = nullptr);
    };

} // namespace NorvesLib::Core::Scene
