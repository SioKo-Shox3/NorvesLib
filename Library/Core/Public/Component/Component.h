#pragma once

#include "Object/Object.h"
#include "Component/TickGroup.h"
#include "Ecs/EcsTypes.h"
#include "Engine/TimeChannels.h"
#include "Object/Entity.h"
#include "Object/Reflection.h"
#include "Container/Containers.h"
#include "Rendering/ResourceChangeLog.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{

    // ========================================
    // コンポーネント基底クラス
    // ========================================

    /**
     * @brief コンポーネント基底クラス
     *
     * すべてのコンポーネントの基底クラス。
     * EntityのInnerとして管理され、GetOuter()でオーナーのEntityを取得できます。
     *
     * Outer/Inner関係:
     * - Entity（Outer）→ Component（Inner）
     * - Entityが破棄されると、Inner全てが連鎖破棄されます。
     */
    class Component : public Object
    {
        REFLECTION_CLASS(Component, Object)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Component();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit Component(const FieldInitializer *initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit Component(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~Component();

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief コンポーネントの初期化
         */
        virtual void Initialize() override;

        /**
         * @brief コンポーネントの終了処理
         */
        virtual void Finalize() override;

        /**
         * @brief 開始時に呼ばれる
         */
        virtual void BeginPlay();

        /**
         * @brief 終了時に呼ばれる
         */
        virtual void EndPlay();

        /**
         * @brief 毎フレーム更新
         * @param deltaTime 前フレームからの経過時間
         */
        virtual void Tick(float deltaTime);
        virtual Engine::TimeChannel GetTimeChannel() const noexcept
        {
            return Engine::TimeChannel::World;
        }

        // 既定では主群の呼び出しだけを旧Tickへ渡す。複数群を使う型は上書きする。
        virtual void OnTickGroup(ETickGroup group, float deltaTime);
        // 実破棄はWorldの安全なcleanup位置で行う。
        void MarkForDestroy()
        {
            Object::Destroy();
        }

        /**
         * @brief 固定ステップ更新(GameThread限定)
         *
         * Module PreFixedTick(Transform読み・準備専用)の後、登録順・Entity深さ優先で
         * 呼ばれる。RenderThreadへはこの更新後のFramePacket snapshotだけを渡す。
         */
        virtual void FixedTick(float fixedDeltaTime);

        // ========================================
        // オーナー管理（Outer経由）
        // ========================================

        /**
         * @brief オーナーのEntityを取得（Outer経由）
         * @return Entityへのポインタ（所属していない場合はnullptr）
         */
        Entity *GetOwner();
        const Entity *GetOwner() const;

        /**
         * @brief オーナーIDを取得
         */
        uint64_t GetOwnerId() const;

        // ========================================
        // コンポーネントID
        // ========================================

        /**
         * @brief コンポーネントIDを取得
         */
        uint64_t GetComponentId() const { return ComponentId; }

        // ========================================
        // 有効/無効
        // ========================================

        /**
         * @brief コンポーネントを有効化
         */
        virtual void Enable();

        /**
         * @brief コンポーネントを無効化
         */
        virtual void Disable();

        /**
         * @brief 有効かどうか
         */
        bool IsEnabled() const { return getbEnabled().Get(); }

        /**
         * @brief アクティブかどうか（オーナーが存在かつ自身も有効）
         */
        virtual bool IsActive() const { return getbEnabled().Get() && GetOuter() != nullptr; }

        // ========================================
        // Render state dirty tracking
        // ========================================

        /**
         * @brief RenderThread同期が必要な状態にする
         *
         * 同期が不要から必要へ変わったときだけ、World の段 RenderSync の一覧（変更ログ）へ知らせる
         */
        void MarkRenderStateDirty()
        {
            if (!m_bRenderStateDirty)
            {
                m_bRenderStateDirty = true;
                NotifyRenderStateDirty();
            }
        }

        /**
         * @brief RenderThread同期が必要かどうか
         */
        bool IsRenderStateDirty() const { return m_bRenderStateDirty; }

        /**
         * @brief RenderThread同期済みとしてdirty状態を解除する
         */
        void ClearRenderStateDirty() { m_bRenderStateDirty = false; }

        /**
         * @brief 最後にSceneViewへ同期したオーナートランスフォームバージョンを取得
         */
        uint64_t GetLastSyncedTransformVersion() const { return m_LastSyncedTransformVersion; }

        /**
         * @brief 最後にSceneViewへ同期したオーナートランスフォームバージョンを設定
         */
        void SetLastSyncedTransformVersion(uint64_t version) { m_LastSyncedTransformVersion = version; }

        // ========================================
        // Tick設定
        // ========================================

        /**
         * @brief Tick有効設定
         */
        void SetTickEnabled(bool bEnabled) { getbTickEnabled().Set(bEnabled); }
        bool IsTickEnabled() const { return getbTickEnabled().Get(); }
        ETickGroup GetTickGroup() const
        {
            return LoadTickConfiguration().GetGroup();
        }
        int16_t GetTickPriority() const
        {
            return LoadTickConfiguration().GetPriority();
        }
        TickGroupMask GetTickGroupMask() const
        {
            return LoadTickConfiguration().GetMask();
        }
        bool SetTickGroup(ETickGroup group)
        {
            TickGroupConfiguration configuration = LoadTickConfiguration();
            if (!configuration.SetGroup(group))
            {
                return false;
            }
            StoreTickConfiguration(configuration);
            return true;
        }
        void SetTickPriority(int16_t priority)
        {
            TickGroupConfiguration configuration = LoadTickConfiguration();
            configuration.SetPriority(priority);
            StoreTickConfiguration(configuration);
        }
        bool SetTickGroupMask(TickGroupMask mask)
        {
            TickGroupConfiguration configuration = LoadTickConfiguration();
            if (!configuration.SetMask(mask))
            {
                return false;
            }
            StoreTickConfiguration(configuration);
            return true;
        }

        // ========================================
        // ECS の行
        // ========================================

        /**
         * @brief この Component の ECS の行（World の木にいる間だけ有効。木から外れる前に World が切る）
         */
        Ecs::EcsEntity GetEcsEntity() const { return m_EcsEntity; }

        /**
         * @brief 行を引く口（プロパティ用）。行が無い間は IsBound() が false
         */
        Ecs::EcsRowRef GetEcsRow() const;

        /**
         * @brief 行に足す描画の値の列の種類（World が行を作るときのアーキタイプを選ぶ）
         */
        virtual Ecs::EcsComponentRowKind GetEcsRowKind() const { return Ecs::EcsComponentRowKind::Basic; }

        /**
         * @brief proxy の内容が依存している資源（メッシュ・材質など）を out へ足す
         *
         * World が「資源の handle → 使っている行」の逆引きに使う。資源の登録・更新・解放が通知されたとき、
         * この行の proxy を作り直す。handle を変える setter は描画の更新を要求するので、そのたびに呼ばれて申告し直される
         */
        virtual void CollectRenderResourceRefs(Container::VariableArray<Rendering::RenderResourceRef>& out) const
        {
            (void)out;
        }

    protected:
        /**
         * @brief 行が付いた直後に呼ばれる。ECS_PROPERTY にしていない値を、予備の値から列へ写す
         */
        virtual void OnEcsRowAttached() {}

        /**
         * @brief 行を外す直前に呼ばれる。ECS_PROPERTY にしていない値を、列から予備の値へ戻す
         */
        virtual void OnEcsRowDetaching() {}

        // ========================================
        // リフレクションプロパティ
        // ========================================
        PROPERTY(uint64_t, ComponentId) // コンポーネントID
        // flags は World の木にいる間 ECS の行（EcsComponentFlags）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsComponentFlags, bEnabled, bool, bEnabled)         // 有効状態
        ECS_PROPERTY(Ecs::EcsComponentFlags, bTickEnabled, bool, bTickEnabled) // Tick有効
        PROPERTY(bool, bBegunPlay)      // BeginPlay済みフラグ

        bool m_bRenderStateDirty = true;
        /// 変更ログ（段 RenderSync の一覧）へ知らせる World。行が付いている間だけ持つ（World が付け外しする）
        ::NorvesLib::Core::World* m_RenderLogWorld = nullptr;
        uint64_t m_LastSyncedTransformVersion = 0;
        /// ECS の行（World が付け外しする）
        Ecs::EcsEntity m_EcsEntity;
        /// 段の最中に予約した行の作成の位置（World の予約の並び。予約が無ければ最大値。同期点までの間だけ持つ）
        uint32_t m_EcsCreateSlot = 0xFFFFFFFFu;

    private:
        friend class ::NorvesLib::Core::World;
        /// 描画の同期が必要になったことを World の変更ログへ知らせる（World が無ければ何もしない）
        void NotifyRenderStateDirty();
        ETickGroup m_DispatchPrimaryGroup = ETickGroup::Default;
        bool m_bHasDispatchPrimaryGroup = false;

        // ID生成用静的カウンター
        static uint64_t s_NextComponentId;
        // 行が付いている間は ECS の列（EcsTickConfig）が正で、無い間はこの予備が正（行を外すときに列から戻す）
        TickGroupConfiguration m_TickConfiguration;
        TickGroupConfiguration LoadTickConfiguration() const;
        void StoreTickConfiguration(const TickGroupConfiguration &configuration);
    };

    /// Tick を持っていた型の本体（非仮想）。World の順序付きの系が（群, 優先度, 深さ優先の順）で呼ぶ
    using ComponentTickFn = void (*)(Component &component, float deltaTime);

    /**
     * @brief 型ごとの Tick の登録（設計 1.12）
     *
     * World::RegisterComponentTick で型（IClass）に結び付ける。照合は型の完全一致で、派生の型は自分の型で登録する
     * （リフレクションを宣言しない派生は基底の型として扱われる）。登録の無い型は旧の仮想の OnTickGroup で呼ばれる。
     * channel は型の GetTimeChannel、群・mask・優先度は Component の Tick の設定をそのまま使う。
     */
    struct ComponentTickRegistration
    {
        /// 登録の名前（ログ・検査用。文字列は呼び手が持ち続ける）
        const char *Name = nullptr;
        /// 本体。null は「毎フレームの処理を持たない型」で、順の一覧に載せない
        ComponentTickFn Run = nullptr;
        /// true: 主群（GetTickGroup）の呼び出しだけで呼ぶ（旧の Tick と同じ）。false: Groups に含まれる群で呼ぶ
        bool bPrimaryGroupOnly = true;
        TickGroupMask Groups = AllTickGroups;
    };

    // コンポーネントへのスマートポインタ
    using ComponentPtr = Container::TSharedPtr<Component>;
    using ComponentWeakPtr = Container::TWeakPtr<Component>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::Component, NorvesLib::Core::EClassCastFlags::Component)
