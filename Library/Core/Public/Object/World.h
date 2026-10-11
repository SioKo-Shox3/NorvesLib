#pragma once
#include "Engine/TimeChannels.h"

#include "Object.h"
#include "Reflection.h"
#include "Entity.h"
#include "Component/Component.h"
#include "Container/Containers.h"
#include "Ecs/EcsCommandBuffer.h"
#include "Ecs/EcsRenderSync.h"
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    class IBoardProxySink;
    class MaterialResources;
    class MeshResources;
    class SceneView;
    struct BoardProxy;
}

namespace NorvesLib::Core::Ecs
{
    class EcsWorld;
    class SystemScheduler;
    class EcsTransformSystem;
    struct EcsSystemContext;
}

namespace NorvesLib::Core
{
    namespace Engine
    {
        class ApplicationProcessor;
    }
    class ComponentDataRegistry;
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
         * @brief 切り離すものを待ち参照と破棄予約の一覧から外してから Inner を外します
         */
        virtual bool RemoveInner(IUnknown* inner) override;

        /**
         * @brief 管理下のルートEntityを取得（World直下のInnersからフィルタリング）
         */
        Container::VariableArray<Entity *> GetRootEntities() const;

        /**
         * @brief World ツリー内の有効な Entity を ObjectId で検索します。
         *
         * root と child の両方を対象にし、破棄予約済み Entity とその subtree は
         * 返しません。ObjectId は同一 World ライフサイクル内だけで有効です。
         */
        Entity* FindEntityByObjectId(uint64_t id) const;

        /**
         * @brief World直下のルートEntity数を取得
         */
        size_t GetObjectCount() const;

        /**
         * @brief 順序付きの系に載った Component の Tick の本体を、群 Input〜PoseFinalize まで呼ぶ
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        void Tick(float deltaTime);
        void Tick(const Engine::FrameTimes& times);
        // 同じフレームの収集結果を用い、物理後の群を実行する。再収集はしない。
        void LateTick(float deltaTime);
        void LateTick(const Engine::FrameTimes& times);
        // 現在の可変dispatchだけを問い合わせる。dt0は成功、失敗時outは保持する。channel は型の登録の channel。
        // 時間解決は更新の許可ではなく、active/tick/pending判定は呼出側が行う。
        bool TryGetComponentTickDelta(const Component::Component& component, float& out) const;
        /// Component の型の channel（登録の TimeChannel。登録が無ければ World）
        Engine::TimeChannel GetComponentTimeChannel(const Component::Component& component) const;
        bool HasActiveTickTimeContext() const
        {
            return m_bHasActiveTickTime;
        }

        /**
         * @brief 型 cls の Tick の本体を順序付きの系へ登録する（設計 1.12。同じ型の登録は置き換え）
         *
         * 照合は型の完全一致。反映は次の Tick の収集から。Core の型は World の初期化で登録する
         */
        void RegisterComponentTick(const IClass* cls, const Component::ComponentTickRegistration& registration);
        /// 型 cls の登録（無ければ null。登録の無い型は毎フレームの処理を持たない）
        const Component::ComponentTickRegistration* FindComponentTick(const IClass* cls) const;
        /// Component の群・mask・優先度が変わった（次の収集の先頭で呼び先を付け直す）
        void NoteComponentTickConfigurationChanged() { m_bOrderedTicksDirty = true; }

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
         * @brief ScreenSpace BoardProxyの送信先を設定
         * @param sink BoardProxyの借用送信先。所有しない。
         */
        void SetScreenSpaceBoardSink(Rendering::IBoardProxySink *sink);

        /**
         * @brief ScreenSpace BoardProxyの送信先を取得
         */
        Rendering::IBoardProxySink *GetScreenSpaceBoardSink() const { return m_ScreenSpaceBoardSink; }

        /**
         * @brief 描画の資源（メッシュ・材質など）が変わったことを知らせる
         *
         * 「資源の handle → その資源を使う行」の逆引きで行を見つけ、その行の proxy を次の同期で作り直す。
         * 通常は SyncToSceneView が資源の記録（ResourceChangeLog）から取り出して呼ぶ
         */
        void NotifyRenderResourceChanged(Rendering::RenderResourceKind kind, uint64_t id);

        /**
         * @brief 全部の行の proxy を作り直して公開し直す（SceneView や画面空間の sink の接続先を替えたときなど）
         */
        void NotifyRenderRepublishAll();

        /// 検査 ECS_RENDER_INVALIDATION の累計
        struct RenderInvalidationStats
        {
            /// 公開した内容を覚えた回数
            uint32_t Published = 0;
            /// 変更も通知も無い行を作り直して、公開した内容と比べた回数
            uint32_t Probed = 0;
            /// 比べた結果が食い違った回数（変更も通知も無いのに proxy が変わった行）
            uint32_t Violations = 0;
            /// 再試行の一覧の行が、公開済みの状態になっていた回数
            uint32_t RetryMismatches = 0;
        };

        /// 検査の累計
        const RenderInvalidationStats& GetRenderInvalidationStats() const { return m_InvalidationStats; }

        /// 検査 ECS_RENDER_INVALIDATION を EngineChecks の設定によらず走らせる（テスト用）
        void SetRenderInvalidationCheckForced(bool bForced);

        /// 公開を試みたが proxy を作れず、次の同期でもう一度試す行
        const Container::VariableArray<Ecs::EcsEntity>& GetRenderRetryRows() const { return m_RenderRetryRows; }

        /// 検査 ECS_RENDER_SYNC の累計
        struct RenderSyncStats
        {
            /// 一覧を作って全走査と照合した同期の回数
            uint32_t Checked = 0;
            /// 照合した項目の数の合計
            uint32_t Examined = 0;
            /// 食い違った項目の数の合計
            uint32_t Violations = 0;
            /// 一覧に載せた変わった行・Remove の延べ数
            uint32_t ChangedListed = 0;
            uint32_t RemovedListed = 0;
        };

        /// 検査の累計
        const RenderSyncStats& GetRenderSyncStats() const { return m_RenderSyncStats; }

        /// 直近の同期で作った一覧
        const Ecs::EcsRenderSyncList& GetRenderSyncList() const { return m_RenderSyncList; }

        /// 検査 ECS_RENDER_SYNC を EngineChecks の設定によらず走らせる（テスト用）
        void SetRenderSyncCheckForced(bool bForced);

        /// 描画の同期を旧の全走査にするか（起動オプション --ecs-legacy-sync。比較と切り戻し用）。既定は false。
        /// 既定は、変わった行だけを公開し、Remove の一覧だけを外す（MegaGeometry は SCENE-DELTA まで旧の走査）。
        /// ComponentDataRegistry のフレームごとの捕捉は旧の全走査のときだけ行う
        void SetLegacyRenderSync(bool bLegacy) noexcept { m_bLegacyRenderSync = bLegacy; }
        [[nodiscard]] bool IsLegacyRenderSync() const noexcept { return m_bLegacyRenderSync; }

        /// 変更ログ: Component の描画の同期が不要から必要へ変わった。Component の MarkRenderStateDirty が呼ぶ
        void NoteComponentRenderDirty(Ecs::EcsEntity row);

        /**
         * @brief MeshComponent/LightComponentからSceneViewへProxy同期
         */
        void SyncToSceneView(const Rendering::MaterialResources* materials = nullptr,
                             const Rendering::MeshResources* meshes = nullptr);

        void CollectTransientBoardProxies(Container::VariableArray<Rendering::BoardProxy>& outProxies);

        /**
         * @brief Entity階層のワールドトランスフォームを更新
         *
         * 既定は階層のシステムの伝播（局所の変換・親・Active が変わった行とその子孫だけを求め直し、EcsWorldTransform に書く）。
         * SetLegacyTransformPropagation(true) の間は、全 Entity を根から再帰して求める旧の経路を使う。
         */
        void UpdateWorldTransforms();

        /// 旧の再帰でワールドの変換を更新するか（起動オプション --ecs-legacy-transform。比較と切り戻し用）。既定は false
        void SetLegacyTransformPropagation(bool bLegacy) noexcept { m_bLegacyTransformPropagation = bLegacy; }
        [[nodiscard]] bool IsLegacyTransformPropagation() const noexcept { return m_bLegacyTransformPropagation; }
        // 固定stepの前後で呼ぶ。描画alphaの適用はCamera群より前に行う。
        void PrepareRenderInterpolationStep();
        void CaptureRenderInterpolationStep();
        bool SetRenderInterpolationAlpha(float alpha);
        void SetRenderInterpolationAllowed(bool allowed);
        void UpdateRenderTransforms();

        uint64_t GetTickSerial() const noexcept { return m_TickSerial; }

        /// ECS の行の保管庫。更新は GetScheduler() の段を通して走る
        Ecs::EcsWorld& GetEcsWorld() { return *m_EcsWorld; }
        /// 更新の段（Tick / LateTick / FixedBegin / FixedTick / FixedEnd / Sync）を走らせるスケジューラ
        Ecs::SystemScheduler& GetScheduler() { return *m_Scheduler; }

        /// 固定ステップの前半（姿勢の確定と補間履歴の準備）。Module の PreFixedTick の前に呼ぶ
        void BeginFixedStep();

        /// 破棄予約の一覧へ積む（Object::Destroy が呼ぶ。GameThread だけ）。Entity・Component 以外は積まない。
        /// 回収（CleanupDestroyedObjects）はこの一覧だけを処理する。
        void RegisterPendingDestroy(IUnknown& object);
        /// 一覧から外す（木から外す・破棄する前に呼ぶ。載っていなければ何もしない）
        void ForgetPendingDestroy(IUnknown& object);
        /// entity とその部分木（Component を含む）を一覧から外す
        void ForgetPendingDestroySubtree(Entity& entity);

        /// 固定ステップの後半（姿勢の確定・補間履歴の記録・破棄の回収）。Module の FixedTick の後に呼ぶ
        void EndFixedStep();

        /**
         * @brief ECS の行と Object の結び付けを照合する（段の外、GameThread から呼ぶ）
         *
         * - World の木にいる Entity の数と、行（EcsObjectRef を持つ行。孤児を含む）の数が一致する（1 対 1）。
         * - 行の EcsObjectRef が Entity を指し、Entity が行の EcsEntity を覚えている。
         * - EcsHierarchy の Parent・Depth・子の並び（FirstChild・LastChild・兄弟）が Outer/Inner と一致する。
         * - Component にも 1 対 1 で行があり（所有者の子の葉）、所有者の Component の連結（FirstComponent・LastComponent・兄弟）が
         *   Inner の並び（Component だけ）と一致する。
         * - 孤児の行と、同期点を待つ破棄の予約が残っていない。
         * 食い違いは LOG_ERROR で理由を出す。
         * @param outCount 照合した Entity の数
         * @return 全部一致すれば true。段の最中なら（孤児が正当に残りうるので）照合せず true
         */
        bool VerifyEcsBinding(uint32_t* outCount = nullptr);

        /// 結び付け（行の作成・消去・付け替え）の回数。変化があったときだけ検査を走らせるのに使う
        uint64_t GetEcsBindingEpoch() const noexcept { return m_EcsBindingEpoch; }

        /**
         * @brief 階層のシステムの結果を、根から全部たどって求め直した値と全行照合する（検査 ECS_TRANSFORM）
         *
         * 階層のシステムを 1 回走らせ（変更の無い回は何もしない）、World の木にいる全 Entity について、EcsWorldTransform が
         * 局所の変換から根をたどって求め直した値と bit まで一致するか、EffectiveActive が祖先をたどる従来の判定
         * （自分と全祖先が Active かつ PendingDestroy でない）と一致するかを確かめる。
         * UpdateWorldTransforms の直後に呼ぶ。食い違いは LOG_ERROR で理由を出す。
         * @param outCount 照合した Entity の数（行の無い Entity は含まない）
         * @return 全部一致すれば true
         */
        bool VerifyEcsTransform(uint32_t* outCount = nullptr);

        /**
         * @brief 描画の変換（補間の結果）を、旧の式で全行を求め直した値と照合する（検査 ECS_RENDER_TRANSFORM）
         *
         * 木を根からたどり、補間する行は履歴の写しに Prepare・Evaluate をかけた値、その子孫は「親の描画の変換 × 局所の変換」、
         * それ以外はワールドの変換を期待値とし、各 Entity の GetRenderWorldTransform と全成分で照合する。
         * 描画の変換の列・タグ・履歴の列が、補間の要求と許可から決まる組と一致するかも確かめる。UpdateRenderTransforms の直後に呼ぶ。
         * @param outCount 照合した Entity の数（行の無い Entity は含まない）
         */
        bool VerifyEcsRenderTransform(uint32_t* outCount = nullptr);

        /// 階層のシステム。無ければ作る（作ったときは最初の Run が全行をきっかけにする）
        Ecs::EcsTransformSystem& GetEcsTransformSystem();

    private:
        friend class Engine::ApplicationProcessor;
        friend class Entity;
        friend struct WorldTickGroupTestAccess;
        bool IsDeferringObjectRemoval() const
        {
            return m_bDispatchingTicks || m_bCleaningObjects;
        }
        uint64_t m_TickSerial = 0;
        float m_RenderInterpolationAlpha = 1;
        bool m_bRenderInterpolationAllowed = true;

        // ---- 描画の変換の補間（設計 1.9）。処理するのは、補間する行とその子孫（m_RenderRows）と、ワールドの変換が変わった行だけ ----
        /// 補間の要求・親・許可・行の作成が変わったことを知らせる（次の更新で列を付け外しする）
        void NoteRenderInterpolationChange(Entity& entity);
        /// 行を作った直後に呼ぶ。補間する Entity か、補間する祖先がいる行なら、列の付け外しの対象にし、描画の更新へ知らせる
        void NoteRenderRowCreated(Entity& entity);
        /// 部分木の履歴を切り、描画の変換を求め直させる（Teleport・駆動切替）
        void ResetEntityRenderInterpolation(Entity& entity);
        /// 補間を要求した Entity か、補間する祖先がいる Entity か（許可されている間だけ）
        bool IsRenderParticipating(const Entity& entity) const;
        /// 行の描画の変換の列・タグ・履歴の列を、要求と許可から決まる組に合わせる。描画の変換の列の有無が変わったら true
        bool ReconcileRenderRow(Entity& entity, Ecs::EcsEntity row);
        /// 溜まった通知の行を浅い順に処理し、描画の変換の列の有無が変わった行の子へ広げる
        void ReconcileRenderInterpolation();
        /// 補間する行とその子孫を浅い順に並べ直す
        void SortRenderRows();
        /// 列を持たなくなった行・消えた行を一覧から除く
        void CompactRenderRows();
        /// 補間する行とその子孫の描画の変換を求める（親から順）
        void UpdateRenderRows();
        /// 固定ステップの前（bCapture が false。履歴の準備）か後（true。捕捉）に、補間を要求した行の履歴を進める
        void StepRenderHistory(bool bCapture);
        /// 通知: ワールドの変換が変わった（初めて求めた）行。次の描画の更新が、補間しない行の版と描画状態を直す
        void QueueRenderChange(Ecs::EcsEntity row);
        /// 溜まった行のうち、描画の変換の列を持たない行の版を進め、描画状態を dirty にする
        void FlushRenderChangedRows();
        /// 描画の変換が変わった Entity の版を進め、所有する Mesh・SkinnedMesh の描画状態を dirty にする
        void NoteRenderTransformChanged(Entity& entity);
        /// 行から Entity を引く（Entity の行でない・Object を切られた行は null）
        Entity* ResolveRenderEntity(Ecs::EcsEntity row) const;
        bool VerifyRenderEntity(Entity& entity, const Math::Transform& parentRender, bool bParentInterpolated,
                                uint32_t& count, const char*& error);
        void RunEcsRenderTransformCheck();

        /// 登録のある型の呼び先（順序付きの系の項目）を、木の深さ優先の順で足し・付け替え・外す。
        /// 木の構造・Tick の設定・型の登録が変わったとき（m_bOrderedTicksDirty）だけ、収集の先頭で呼ぶ
        void SyncOrderedTicks();
        void CollectOrderedTicks(Entity& entity, size_t& ordinal);
        bool CanDispatchEntity(const Entity& entity) const;
        const Engine::FrameTimes* m_ActiveTickTimes = nullptr;
        float m_ActiveTickDelta = 0;
        bool m_bHasActiveTickTime = false;
        bool TryResolveChannelDelta(const Entity& owner, Engine::TimeChannel channel, float& out) const;

        /// 順序付きの系の呼び先 1 つ（登録のある型の Component の、群 1 つぶん）。項目の User はこれを指す
        struct OrderedTickRecord;
        /// Component ごとの呼び先。Component が木から外れる（InvalidateTickComponent）か、付け直しで見つからなければ外す
        Container::UnorderedMap<const Component::Component*,
                                Container::VariableArray<Container::TUniquePtr<OrderedTickRecord>>> m_OrderedTicks;
        /// 順序付きの系: [0] Input〜PoseFinalize（Tick）・[1] PostPhysics・[2] Camera〜PreRender（LateTick）
        uint32_t m_OrderedTickSystems[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        uint64_t m_OrderedTickWalk = 0;
        /// 呼び先の付け直しが要る（木への追加・付け替え、Tick の設定・型の登録の変更で立つ）
        bool m_bOrderedTicksDirty = true;
        uint32_t OrderedTickSystemOf(Component::ETickGroup group) const;
        void SyncOrderedTick(Entity& entity, Component::Component& component, Component::ETickGroup group,
                             int16_t priority, size_t ordinal, const Component::ComponentTickRegistration& registration);
        bool AddOrderedTickEntry(OrderedTickRecord& record);
        void SweepOrderedTicks();
        void RemoveOrderedTicks(const Component::Component& component);
        void ClearOrderedTicks();
        static void RunOrderedTick(void* user, const Ecs::EcsSystemContext& context);
        void ExecuteOrderedTick(OrderedTickRecord& record);
        void TickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime);
        void LateTickWithFrameTimes(const Engine::FrameTimes* times, float deltaTime);
        void DispatchTickGroups(Component::ETickGroup first, Component::ETickGroup last, float deltaTime,
                                const Engine::FrameTimes* times = nullptr);
        void InvalidateTickComponent(Component::Component& component);
        void InvalidateTickEntitySubtree(Entity& entity);
        void CollectPendingComponents(Entity& entity, Container::VariableArray<Component::Component*>& output);
        /// 型ごとの Tick の登録（設計 1.12）
        Container::UnorderedMap<const IClass*, Component::ComponentTickRegistration> m_ComponentTicks;
        void RegisterCoreComponentTicks();
        /// 検査 ECS_TICK_DUP: この収集で呼んだ（呼び先, 群）。検査が有効な収集だけ積む
        Container::UnorderedSet<uint64_t> m_TickDupSeen;
        uint32_t m_TickDupCalls = 0;
        uint32_t m_TickDupFailures = 0;
        uint32_t m_TickDupCollections = 0;
        bool m_bTickDupCheck = false;
        void NoteTickDispatchForCheck(const Component::Component& component, Component::ETickGroup group);
        void ReportTickDupCheck();
        Container::VariableArray<Component::Component*> m_PendingTickComponents;
        Container::VariableArray<Entity*> m_PendingTickEntities;
        /// 破棄予約の一覧（積んだ順。OF_InDestroyList が立っているものだけが載る）
        Container::VariableArray<Component::Component*> m_PendingDestroyComponents;
        Container::VariableArray<Entity*> m_PendingDestroyEntities;
        /// 検査 WORLD_CLEANUP の pass を 1 回以上出したか（破棄が無い間も pass を 1 回は出す）
        bool m_bCleanupCheckReported = false;
        bool m_bDispatchingTicks = false;
        bool m_bCleaningObjects = false;

        // ---- 更新の段（中身は従来の処理。段の GameThreadOnly のシステムが同じ順で呼ぶ） ----
        void InitializeSchedule();
        void EndTickCollection();
        void RunWorldStage(uint32_t stage);
        void SyncToSceneViewBody();

        // ---- 描画の無効化の通知（設計 1.10） ----
        /// 同期の対象になる行を読む（公開の状態の列・ポーズの版・通知）
        struct RenderRowInfo
        {
            bool bRenderTransformChanged = false;
            bool bPoseChanged = false;
            bool bNotified = false;
            uint64_t PoseSerial = 0;
        };
        RenderRowInfo ReadRenderRowInfo(const Component::Component& component, const Entity& owner) const;
        /// 公開の状態の列を書く。proxy を出したときは bPublished を true にして版を進め、出さなかった・外したときは false にする
        void RecordRenderPublish(const Component::Component& component, const Entity& owner, bool bPublished,
                                 uint64_t proxyHash, Ecs::EcsRenderSyncKind sink = Ecs::EcsRenderSyncKind::None);
        /// 公開したかった行が proxy を作れなかった。次の同期でもう一度試す一覧に入れる
        void NoteRenderRetry(const Component::Component& component);
        /// 資源の逆引きへ、この Component の行が使っている資源を登録する（proxy を作り直すたびに呼ぶ）
        void RegisterRenderResourceUsers(const Component::Component& component);
        /// 資源の変化の記録から取り出して、使っている行へ通知する
        void DrainRenderResourceChanges(const Rendering::MaterialResources* materials,
                                        const Rendering::MeshResources* meshes);
        /// 変更も通知も無い行を作り直した proxy が、公開した内容と同じか確かめる
        void CheckRenderRowUnchanged(const Component::Component& component, bool bBuilt, uint64_t proxyHash);
        bool IsRenderInvalidationCheckActive() const;
        /// 1 回の同期の終わり: 再試行の一覧と通知の入れ替え、検査の報告
        void FinishRenderInvalidationFrame();
        Component::Component* ComponentOfRow(Ecs::EcsEntity row) const;

        // ---- 段 RenderSync の一覧（設計 1.10）。既定は一覧の行だけを公開する。旧の全走査では一覧と全走査を照合する ----
        bool IsRenderSyncCheckActive() const;
        /// 変更ログを取るかを同期の頭で決める。取り始めるときは、いまの状態を種として積む。戻りは、この同期で検査を走らせるか
        bool BeginRenderSyncFrame();
        void SeedRenderSyncLog(Entity& entity);
        /// 変更ログ: ワールドの変換の版が進んだ Entity の行を積む
        void NoteRenderSyncTransform(Ecs::EcsEntity row);
        /// 変更ログ・通知・公開の状態から、変わった行と Remove の一覧を作る
        void BuildRenderSyncList();
        /// bFromSettle: 履歴が落ち着くまでの公開し直し（変わった行の一覧には載せず、公開する行にだけ入れる）
        void AddRenderSyncChanged(Component::Component& component, bool bFromSettle = false);
        /// 走査に入らない部分木の行が proxy を出していたら、Remove の一覧と「公開していない」へ戻す行の一覧に載せる
        void AddRenderSyncRemoval(Component::Component& component);
        /// 破棄予約された Entity の部分木の行を、Remove の候補として変更ログに積む
        void NoteSubtreeRenderRemoval(Entity& entity);
        /// 変更ログの整理: 変換と削除は使い切り、描画の値は、まだ同期が必要な行だけ残す
        void TrimRenderSyncLogs();
        // ---- 既定の同期（変わった行だけ） ----
        void SyncRenderRowsDelta(const Rendering::MaterialResources* materials, const Rendering::MeshResources* meshes);
        /// 一覧の Remove を SceneView・画面空間の sink から外し、走査に入らない行の公開の状態を戻す
        void ApplyRenderSyncRemovals();
        /// 変わった行 1 つの proxy を作り直して公開する（MegaGeometry は旧の走査が行う）
        void PublishRenderRow(Component::Component& component, const Rendering::MaterialResources* materials,
                              const Rendering::MeshResources* meshes);
        /// 既定の同期の結果を、全行から求め直した proxy と照合する（検査が有効なときだけ）
        void VerifyRenderSyncDelta(const Rendering::MaterialResources* materials, const Rendering::MeshResources* meshes);
        /// 描画の同期の走査が入る Entity か（自分と祖先がすべてアクティブで破棄待ちでない）
        bool IsEntityRenderScanned(const Entity& entity) const;
        /// 描画の同期の対象の種類。対象でなければ None
        Ecs::EcsRenderSyncKind RenderSyncKindOf(const Component::Component& component) const;
        /// proxy の鍵（Mega は Entity の ObjectId、他は ComponentId）
        uint64_t RenderSyncProxyKey(const Component::Component& component) const;
        /// 全走査が proxy を作り直した行を覚える（一覧との照合用）
        void NoteRenderScanChanged(const Component::Component& component);
        /// 走査の終わり: 外された proxy を求め、一覧と照合し、変更ログを整理する
        void FinishRenderSyncFrame(const Container::UnorderedSet<uint64_t>& liveMesh,
                                   const Container::UnorderedSet<uint64_t>& liveSkinned,
                                   const Container::UnorderedSet<uint64_t>& liveMega,
                                   const Container::UnorderedSet<uint64_t>& liveLight,
                                   const Container::UnorderedSet<uint64_t>& liveScreenBoard,
                                   const Container::UnorderedSet<uint64_t>& liveWorldBoard);
        /// 行の削除（公開の状態の列が消える）のフック。出していた proxy の Remove を積む
        static void OnPublishStateDestroyed(void* user, Ecs::EcsEntity entity, const void* value,
                                            Ecs::EcsHookReason reason);

        Container::TUniquePtr<Ecs::EcsWorld> m_EcsWorld;
        Container::TUniquePtr<Ecs::SystemScheduler> m_Scheduler;
        /// 階層のシステム（ワールドの変換と EffectiveActive を伝播で求める）。EcsWorld より先に壊す
        Container::TUniquePtr<Ecs::EcsTransformSystem> m_EcsTransformSystem;
        /// 旧の再帰でワールドの変換を更新する（--ecs-legacy-transform）
        bool m_bLegacyTransformPropagation = false;
        /// 描画の同期を旧の全走査にする（--ecs-legacy-sync）
        bool m_bLegacyRenderSync = false;
        /// 検査 ECS_TRANSFORM の照合の回数と、最後に pass を出した回
        uint64_t m_EcsTransformCheckCalls = 0;
        uint64_t m_EcsTransformLastReport = 0;
        /// 補間を要求した行と、描画の変換の列を持つ行（補間する行とその子孫）。m_bRenderRowsUnsorted の間は深さの順でない
        Container::VariableArray<Ecs::EcsEntity> m_RenderRows;
        bool m_bRenderRowsUnsorted = false;
        /// 列の付け外しを見直す行（補間の要求・親・許可・行の作成が変わった Entity）
        Container::VariableArray<Ecs::EcsEntity> m_RenderReconcile;
        /// 前回の描画の更新より後にワールドの変換が変わった（初めて求めた）行
        Container::VariableArray<Ecs::EcsEntity> m_RenderChangedRows;
        /// 検査 ECS_RENDER_TRANSFORM の照合の回数と、最後に pass を出した回
        uint64_t m_RenderCheckCalls = 0;
        uint64_t m_RenderCheckLastReport = 0;
        uint32_t m_StageTick = 0;
        uint32_t m_StageLateTick = 0;
        uint32_t m_StageFixedBegin = 0;
        uint32_t m_StageFixedTick = 0;
        uint32_t m_StageFixedEnd = 0;
        uint32_t m_StageSync = 0;
        /// 段の中のシステムが読む、呼び出しごとの引数
        const Engine::FrameTimes* m_StageTimes = nullptr;
        float m_StageDelta = 0;
        const Rendering::MaterialResources* m_StageMaterials = nullptr;
        const Rendering::MeshResources* m_StageMeshes = nullptr;
        /// Tick から LateTick までを 1 つの収集として開いている（参加の番号をフレームで共有する）
        bool m_bTickCollectionOpen = false;

        // ---- ECS の行と Entity の結び付け（World の木にいる全 Entity に 1 行。値はまだ Object にある） ----
        /// 部分木の行を親から順に作り、階層（親・子の並び・深さ）を結ぶ。Outer/Inner を結んだ後・BeginPlay の前に呼ぶ
        void BindEntitySubtreeRows(Entity& entity);
        /// Component の行を作り、所有者の Component の連結の末尾へ付ける。所有者が World の木にいるときだけ（Entity::AddInner と
        /// BindEntitySubtreeRows が呼ぶ）。段の最中、または所有者の行が予約中なら作成を予約する
        void BindComponentRow(Component::Component& component);
        /// Component の行を、解放や付け替えの前に切る（値を予備へ戻して孤児にして消す）。bUnlink は所有者の連結から外すか
        void ReleaseComponentRow(Component::Component& component, bool bUnlink = true);
        /// 部分木の行を、解放の前に切る（親の連結から外し、子から順に孤児にして消す）。木から外す前に呼ぶ
        void UnbindEntitySubtreeRows(Entity& entity);
        void ReleaseEntitySubtreeRows(Entity& entity);
        /// 行 1 つを切る: Object への参照を null にして孤児の印を付け、並列の段の外なら消す。段の最中は同期点まで待つ
        void ReleaseEntityRow(Entity& entity);
        /// 親を付け替えた後の階層を直す（古い親の連結から外し、新しい親の末尾へ付け、部分木の深さを直す）
        void RelinkEntityRow(Entity& entity);
        /// row を parentRow の連結の末尾へ付ける。kind が Component なら所有者の Component の連結、Entity なら子 Entity の連結
        void LinkEntityRowToParent(Ecs::EcsEntity row, Ecs::EcsEntity parentRow, uint32_t kind = Ecs::kEcsHierarchyKindEntity);
        void UnlinkEntityRow(Ecs::EcsEntity row);
        void UpdateEntitySubtreeDepth(Entity& entity, uint32_t depth);
        /// 段の最中の Attach*: 行の作成を予約して Entity に札を持たせる（行は同期点まで無い）
        void ReserveEntityRowCreate(Entity& entity);
        void ReserveComponentRowCreate(Component::Component& component);
        /// 行を付けた直後に、Object の予備の値（flags・時間倍率・Tick の設定）を列へ移す
        void MoveEntityValuesToRow(Entity& entity);
        void MoveComponentValuesToRow(Component::Component& component);
        /// 行を消す前に、列の値を Object の予備へ戻す
        void MoveEntityValuesToFallback(Entity& entity);
        void MoveComponentValuesToFallback(Component::Component& component);
        /// 同期点: 予約した作成を再生して行を結び、階層（子の並び・深さ）を Inner の並びから直す
        void PlaybackEntityRowCreates();
        /// 親の行の子の連結と、子（とその子孫）の深さを Inner の並びに合わせて作り直す
        void RebuildEntityRowChildren(Entity& parent);
        /// 同期点まで待たせた行を消す
        void FlushDeferredEntityRows();
        /// 検査 ECS_BIND: 結び付けに変化があったときだけ照合して結果を出す。最後の引数は World の終了時（行が残らない）
        void RunEcsBindingCheck(bool bFinal);
        /// 検査 ECS_TRANSFORM: 更新の直後に階層のシステムの結果を全行照合し、結果を出す
        void RunEcsTransformCheck();
        /// 階層のシステムを 1 回走らせ、値が変わった Entity の版を進めて、処理した Entity の dirty を戻す。求められなかった行の数を返す
        uint32_t PropagateEcsTransforms();

        Ecs::EcsArchetypeId m_EntityArchetype = Ecs::kInvalidEcsArchetype;
        /// Component の行のアーキタイプ（基本の列 + 種類ごとの描画の値の列。添字は EcsComponentRowKind）
        Ecs::EcsArchetypeId m_ComponentArchetypes[static_cast<size_t>(Ecs::EcsComponentRowKind::Count)] = {
            Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype,
            Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype, Ecs::kInvalidEcsArchetype};
        Ecs::EcsArchetypeId ComponentArchetypeFor(const Component::Component& component) const;
        /// 段の最中に孤児にした行（同期点で消す）
        Container::VariableArray<Ecs::EcsEntity> m_DeferredEntityRows;
        /// 段の最中に予約した行の作成。取り消した項目は Target が null（添字は Entity の札が指すので詰めない）
        struct PendingEntityCreate
        {
            /// Entity の作成なら Target、Component の作成なら ComponentTarget（どちらも null なら取り消し済み）
            Entity* Target = nullptr;
            Component::Component* ComponentTarget = nullptr;
            Ecs::EcsCreateTicket Ticket;
        };
        Ecs::EcsCommandBuffer m_EntityCreateCommands;
        Container::VariableArray<PendingEntityCreate> m_PendingEntityCreates;
        /// 予約中の親の下へ付け替えた行（同期点で親の行ができてから結び直す）
        Container::VariableArray<Ecs::EcsEntity> m_PendingEntityRelinks;
        uint64_t m_EcsBindingEpoch = 0;
        /// 検査 ECS_BIND で最後に照合したときの m_EcsBindingEpoch（まだなら最大値）
        uint64_t m_VerifiedBindingEpoch = ~0ull;

        void CleanupDestroyedObjects();
        void DispatchFixedTick(float fixedDeltaTime);
        void CleanupAfterFixedStep();
        void UpdateEntityTransformRecursive(Entity& entity, const Math::Transform& parentWorld);
        void SyncEntityRecursive(Entity& entity,
                                 const Rendering::MaterialResources* materials,
                                 const Rendering::MeshResources* meshes,
                                 Container::UnorderedSet<uint64_t>& liveMeshComponentIds,
                                 Container::UnorderedSet<uint64_t>& liveSkinnedMeshComponentIds,
                                 Container::UnorderedSet<uint64_t>& liveMegaGeometryObjectIds,
                                 Container::UnorderedSet<uint64_t>& liveLightIds,
                                 Container::UnorderedSet<uint64_t>& liveScreenBoardComponentIds,
                                 Container::UnorderedSet<uint64_t>& liveWorldBoardComponentIds,
                                 ComponentDataRegistry* componentDataRegistry);
        void CollectTransientBoardProxiesRecursive(Entity& entity,
                                                   Container::VariableArray<Rendering::BoardProxy>& outProxies);
        void CollectPendingDestroyRecursive(Entity& entity, Container::VariableArray<Entity*>& toRemove);
        bool AttachRootEntity(Entity* entity);
        bool AttachChildEntity(Entity* parent, Entity* child);
        bool MoveEntityInnerAtomic(Entity& entity, IUnknown& newOwner);
        bool DestroyEntitySubtree(Entity& entity);
        void InitializeEntitySubtreeForWorld(Entity& entity);
        void AssignFreshObjectIdsRecursive(Entity& entity);
        void MarkEntitySubtreeRenderStateDirty(Entity& entity);
        /// 走査されない（非アクティブ・破棄待ち）部分木の行について、公開の状態の列を「公開していない」へ合わせる
        void MarkEntitySubtreeRenderUnpublished(Entity& entity);
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
        Rendering::IBoardProxySink *m_ScreenSpaceBoardSink = nullptr;

        /// 資源の逆引き: 鍵（RenderResourceRef::Key）→ その資源を使う Component の行（EcsEntity を 1 つの値に詰めたもの）。
        /// 死んだ行は通知のときに取り除く
        Container::UnorderedMap<uint64_t, Container::UnorderedSet<uint64_t>> m_RenderResourceUsers;
        /// 行ごとの旧参照: 行 → 逆引きに登録した資源の鍵。登録し直すとき、交換前の資源の集合から行を外すのに使う
        Container::UnorderedMap<uint64_t, Container::VariableArray<uint64_t>> m_RenderRowRefKeys;
        /// 今回の同期で「通知を受けた」ことにする Component の ID（資源の変化・前回の公開で履歴が落ち着いていない行）
        Container::UnorderedSet<uint64_t> m_RenderNotified;
        /// 次の同期で通知を受けたことにする Component の ID（今回の公開で前フレームの変換が現在と違う行）
        Container::UnorderedSet<uint64_t> m_RenderSettleNext;
        /// 全部の行を公開し直す要求（次に走った同期の終わりまで）
        bool m_bRenderRepublishAll = false;
        /// 再試行の一覧（今の分と、今回の同期で作っている次の分）
        Container::VariableArray<Ecs::EcsEntity> m_RenderRetryRows;
        Container::VariableArray<Ecs::EcsEntity> m_RenderRetryNext;
        /// 取り出した資源の変化の置き場
        Container::VariableArray<Rendering::RenderResourceRef> m_RenderRefScratch;
        /// 検査 ECS_RENDER_INVALIDATION: Component の ID → 公開した proxy の内容の hash
        Container::UnorderedMap<uint64_t, uint64_t> m_InvalidationHashes;
        RenderInvalidationStats m_InvalidationStats;
        bool m_bInvalidationCheckForced = false;
        /// 今回の同期で検査を走らせる（同期の頭で決める）
        bool m_bInvalidationCheckFrame = false;
        uint64_t m_InvalidationCalls = 0;
        uint64_t m_InvalidationLastReport = 0;
        uint32_t m_InvalidationFrameExamined = 0;
        uint32_t m_InvalidationFrameViolations = 0;

        // ---- 段 RenderSync の一覧 ----
        /// 変更ログを取っている（検査 ECS_RENDER_SYNC が有効）
        bool m_bRenderSyncLogEnabled = false;
        bool m_bRenderSyncCheckForced = false;
        /// この同期で一覧を作って照合する
        bool m_bRenderSyncCheckFrame = false;
        /// 変更ログ: 描画の同期が必要になった Component の行（EcsEntity を詰めたもの。同期の終わりに、まだ必要な行だけ残す）
        Container::VariableArray<uint64_t> m_RenderSyncDirtyLog;
        /// 変更ログ: ワールドの変換の版が進んだ Entity の行（同期のたびに空にする）
        Container::VariableArray<uint64_t> m_RenderSyncTransformLog;
        /// 行の削除のフックが積んだ Remove（同期のたびに空にする）
        Container::UnorderedSet<uint64_t> m_RenderSyncDestroyed;
        Ecs::EcsRenderSyncList m_RenderSyncList;
        /// 一覧の変わった行（積んだ順。公開の順を決定論にする）と、その重複除け
        Container::VariableArray<uint64_t> m_RenderSyncChangedRows;
        Container::UnorderedSet<uint64_t> m_RenderSyncChangedSeen;
        /// 走査に入らない部分木で proxy を出していた行（公開の状態を「公開していない」へ戻す）
        Container::VariableArray<uint64_t> m_RenderSyncUnpublishRows;
        /// 今回の同期で公開し直す行と、次の同期で公開し直す行（Mesh の履歴が落ち着くまでの公開し直し）
        Container::VariableArray<uint64_t> m_RenderSettleRows;
        Container::VariableArray<uint64_t> m_RenderSettleNextRows;
        /// 全走査が proxy を作り直した行
        Container::UnorderedSet<uint64_t> m_RenderSyncScanChanged;
        RenderSyncStats m_RenderSyncStats;
        uint64_t m_RenderSyncCalls = 0;
        uint64_t m_RenderSyncLastReport = 0;

        // リフレクションプロパティ
        PROPERTY(uint64_t, NextObjectId)
    };

} // namespace NorvesLib::Core

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::World, NorvesLib::Core::EClassCastFlags::World)
