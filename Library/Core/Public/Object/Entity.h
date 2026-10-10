#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Ecs/EcsTypes.h"
#include "Math/GeometryTypes.h"
#include "Math/Quaternion.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include "Object.h"
#include "Object/EntityHandle.h"
#include "Reflection.h"
#include "Scene/RenderTransformHistory.h"
#include <cstdint>

namespace NorvesLib::Core
{
    // 前方宣言
    class World;

    namespace Component
    {
        class Component;
    }

    /**
     * @brief Worldに属するオブジェクトの基底クラス
     *
     * EntityはWorldのInnerとして管理され、Worldと寿命が一致します。
     * シーン内に存在するゲームオブジェクト（Actor、Componentなど）の基底クラスです。
     *
     * Outer/Inner関係:
     * - World（Outer）→ Entity（Inner）
     * - Entity（Outer）→ Component（Inner）
     *
     * GetOuter()をたどればWorldを取得できます。
     * m_InnersにはこのオブジェクトにアタッチされたComponentが含まれます。
     */
    class Entity : public Object
    {
        REFLECTION_CLASS(Entity, Object)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Entity();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit Entity(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit Entity(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~Entity();

        /**
         * @brief オブジェクトを初期化します
         */
        virtual void Initialize() override;

        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        virtual void Finalize() override;

        // ========================================
        // コンポーネント管理（Outer/Inner経由）
        // ========================================

        /**
         * @brief コンポーネントを追加
         * @param component 追加するコンポーネント
         */
        bool AddComponent(Component::Component *component);

        /**
         * @brief コンポーネントを削除
         * @param component 削除するコンポーネント
         */
        void RemoveComponent(Component::Component *component);

        /**
         * @brief 指定型のコンポーネントを取得（Innersから検索）
         * @tparam T コンポーネントの型
         * @return 見つかったコンポーネント、見つからない場合nullptr
         */
        template <typename T>
        T *GetComponent() const
        {
            for (auto *inner : GetInners())
            {
                if (T *cast = CastTo<T>(inner))
                {
                    return cast;
                }
            }
            return nullptr;
        }

        /**
         * @brief 全コンポーネントを取得（Innersからフィルタリング）
         */
        Container::VariableArray<Component::Component *> GetComponents() const;

        /**
         * @brief 子Entityを取得（Innersからフィルタリング）
         */
        Container::VariableArray<Entity*> GetChildEntities() const;

        /**
         * @brief 全コンポーネントのTickを呼び出す
         * @param deltaTime 前フレームからの経過時間
         */
        void TickComponents(float deltaTime);

        // ========================================
        // World関連（Outer経由）
        // ========================================

        /**
         * @brief このオブジェクトが属するWorldを取得（Outerをたどる）
         * @return Worldへのポインタ（Worldに属していない場合はnullptr）
         */
        World *GetWorld() const;

        /**
         * @brief Worldに属しているかどうか
         * @return Worldに属している場合true
         */
        bool IsInWorld() const;

        /**
         * @brief public AddInnerからのin-world子Entity追加を拒否します
         */
        virtual bool AddInner(IUnknown* inner) override;
        // ObjectHeapも通るdetach入口で、Worldの非所有更新参照を無効化する。
        virtual bool RemoveInner(IUnknown* inner) override;

        /**
         * @brief この Entity の ECS の行（World の木にいる間だけ有効。木から外れる前に World が切る）
         * @return 行が無ければ null
         */
        Ecs::EcsEntity GetEcsEntity() const { return m_EcsEntity; }

        /**
         * @brief 行を引く口（プロパティ用）。行が無い間は IsBound() が false
         */
        Ecs::EcsRowRef GetEcsRow() const;

        /**
         * @brief オブジェクトIDを取得
         */
        uint64_t GetObjectId() const { return ObjectId; }

        /**
         * @brief オブジェクトIDを設定（World内部用）
         */
        void SetObjectId(uint64_t id) { ObjectId = id; }

        /**
         * @brief ComponentDataRegistry用Entityハンドルを取得
         */
        EntityHandle GetEntityHandle() const { return m_EntityHandle; }

        /**
         * @brief ComponentDataRegistry用Entityハンドルを設定
         */
        void SetEntityHandle(EntityHandle handle) { m_EntityHandle = handle; }

        // ========================================
        // トランスフォーム
        // ========================================

        /**
         * @brief ローカル位置を設定
         */
        void SetLocalPosition(const Math::Vector3& pos);
        void SetLocalPosition(float x, float y, float z);

        /**
         * @brief ローカル回転を設定（クォータニオン）
         */
        void SetLocalRotation(const Math::Quaternion& rot);
        void SetLocalRotation(float x, float y, float z, float w);

        /**
         * @brief ローカルスケールを設定
         */
        void SetLocalScale(const Math::Vector3& scale);
        void SetLocalScale(float x, float y, float z);

        /**
         * @brief ローカルトランスフォームを取得（値で返す。実体は ECS の列 EcsLocalTransform にある）
         */
        Math::Transform GetLocalTransform() const;

        /**
         * @brief ワールドトランスフォームを設定
         */
        void SetWorldTransform(const Math::Transform& worldTransform);

        /**
         * @brief ワールドトランスフォームを取得（値で返す。キャッシュは ECS の列 EcsWorldTransform にある）
         */
        Math::Transform GetWorldTransform() const;
        // 描画専用。opt-in root以下だけ補間し、物理/joint/AABBのworld姿勢は変更しない。
        // 明示ON/OFFはsubsystem由来の既定値より優先する。falseの明示も記録する。
        void SetRenderInterpolationEnabled(bool enabled);
        void ClearRenderInterpolationOverride();
        void SetDefaultRenderInterpolationEnabled(bool enabled);
        bool IsRenderInterpolationEnabled() const
        {
            return m_bRenderInterpolationEnabled;
        }
        Math::Transform GetRenderWorldTransform() const;
        uint64_t GetRenderTransformVersion() const
        {
            return m_RenderTransformVersion;
        }
        // Teleport・駆動切替で子孫の履歴も切る。次の捕捉で新姿勢に揃える。
        void ResetRenderInterpolation();

        /**
         * @brief このEntityに属するMeshComponentのワールドAABBを取得
         */
        bool GetWorldAABB(Math::AABB& outAABB) const;

        /**
         * @brief ワールド位置を設定
         */
        void SetPosition(const Math::Vector3& pos);
        void SetPosition(float x, float y, float z);

        /**
         * @brief ワールド位置を取得
         */
        Math::Vector3 GetPosition() const;

        /**
         * @brief ワールド回転を設定（クォータニオン）
         */
        void SetRotation(const Math::Quaternion& rot);
        void SetRotation(float x, float y, float z, float w);

        /**
         * @brief ワールド回転を取得（クォータニオン）
         */
        Math::Quaternion GetRotation() const;

        /**
         * @brief ワールドスケールを設定
         */
        void SetScale(const Math::Vector3& scale);
        void SetScale(float x, float y, float z);

        /**
         * @brief ワールドスケールを取得
         */
        Math::Vector3 GetScale() const;

        /**
         * @brief トランスフォーム更新バージョンを取得
         */
        uint64_t GetTransformVersion() const { return m_TransformVersion; }

        /**
         * @brief Outerから親Entityを取得
         */
        Entity* GetParentEntity() const;

        /**
         * @brief ワールドトランスフォームをdirty化
         */
        void MarkWorldTransformDirty();

        /**
         * @brief 親ワールドトランスフォームからワールドキャッシュを再計算
         */
        void RecomputeWorldTransform(const Math::Transform& parentWorld);

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief オブジェクトがWorldに追加された時に呼ばれる
         */
        virtual void OnAddedToWorld() {}

        /**
         * @brief オブジェクトがWorldから削除される時に呼ばれる
         */
        virtual void OnRemovedFromWorld() {}

        /**
         * @brief フレーム更新（GameThread）
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        virtual void Tick(float deltaTime) {}
        // 実行時の局所倍率。親子へは継承せず、World/Animationだけに適用する。
        bool SetCustomTimeDilation(float value);
        // 行が付いている間は ECS の列（EcsTimeDilation）が正で、無い間は m_CustomTimeDilation が正
        float GetCustomTimeDilation() const;

        /**
         * @brief 更新が有効かどうか
         * @return 有効な場合true
         */
        bool IsTickEnabled() const { return getbTickEnabled().Get(); }

        /**
         * @brief 更新の有効/無効を設定
         * @param bEnabled 有効にする場合true
         */
        void SetTickEnabled(bool bEnabled) { getbTickEnabled().Set(bEnabled); }

        // ========================================
        // アクティブ状態
        // ========================================

        /**
         * @brief オブジェクトがアクティブかどうか
         * @return アクティブな場合true
         */
        bool IsActive() const { return getbActive().Get(); }

        /**
         * @brief アクティブ状態を設定
         * @param bActive アクティブにする場合true
         */
        virtual void SetActive(bool bActive);

        /**
         * @brief オブジェクトを破棄予約
         *
         * 次のフレーム終了時にWorldから削除され、破棄されます。
         */
        void MarkForDestroy()
        {
            getbPendingDestroy().Set(true);
            Object::Destroy();
        }

        /**
         * @brief 破棄予約されているかどうか
         * @return 破棄予約されている場合true
         */
        bool IsPendingDestroy() const { return getbPendingDestroy().Get() || Object::IsPendingDestroy(); }

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================
        // flags は World の木にいる間 ECS の行（EcsEntityFlags）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsEntityFlags, bTickEnabled, bool, bTickEnabled)       // Tick更新が有効か
        ECS_PROPERTY(Ecs::EcsEntityFlags, bActive, bool, bActive)                 // アクティブ状態
        ECS_PROPERTY(Ecs::EcsEntityFlags, bPendingDestroy, bool, bPendingDestroy) // 破棄予約フラグ

        // 表示名（エディタでのリネーム用。空文字は未設定を表す）
        PROPERTY(Container::String, Name)

        // トランスフォーム（World の木にいる間は ECS の行（EcsLocalTransform）の列にあり、無い間は予備の値にある）
        ECS_PROPERTY(Ecs::EcsLocalTransform, position, Math::Vector3, Position)    // ローカル位置
        ECS_PROPERTY(Ecs::EcsLocalTransform, rotation, Math::Quaternion, Rotation) // ローカル回転（クォータニオン）
        ECS_PROPERTY(Ecs::EcsLocalTransform, scale, Math::Vector3, Scale)          // ローカルスケール

        // オブジェクトID（World内でユニーク）
        PROPERTY(uint64_t, ObjectId)

        float m_CustomTimeDilation = 1;
        /// ワールドの変換のキャッシュの予備（行が付いている間は列 EcsWorldTransform が正で、ここは行を外すときに列から戻す）
        Math::Transform m_WorldTransformFallback;
        bool m_bWorldTransformDirty = true;
        uint64_t m_TransformVersion = 1;
        EntityHandle m_EntityHandle;
        /// ECS の行（World が付け外しする）
        Ecs::EcsEntity m_EcsEntity;
        /// 段の最中に予約した行の作成の位置（World の予約の並び。予約が無ければ最大値。同期点までの間だけ持つ）
        uint32_t m_EcsCreateSlot = 0xFFFFFFFFu;
        // 補間の要求の状態。履歴と描画の変換は ECS の列（EcsRenderInterpolated・EcsTransformHistory・EcsRenderTransform）にある
        bool m_bRenderInterpolationEnabled = false;
        bool m_bRenderInterpolationDefault = false;
        bool m_bRenderInterpolationOverride = false;
        uint64_t m_RenderTransformVersion = 1;

      private:
        friend class World;
        void RemoveComponentImmediately(Component::Component* component);
        void MarkRenderStateDirtyRecursive();
        void SetLocalTransform(const Math::Transform& transform);
        Math::Transform EvaluateWorldTransformNonMutating() const;
        /// ローカルの変換の読み書き（行が付いていて列を持つ間は列、無い間は予備のプロパティ）
        Math::Transform LoadLocalTransform() const;
        bool StoreLocalTransform(const Math::Transform& transform);
        /// ワールドの変換のキャッシュの読み書き（同上。列は EcsWorldTransform、予備は m_WorldTransformFallback）
        Math::Transform LoadWorldTransformCache() const;
        bool StoreWorldTransformCache(const Math::Transform& transform);
    };

} // namespace NorvesLib::Core

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Entity, NorvesLib::Core::EClassCastFlags::Entity)
