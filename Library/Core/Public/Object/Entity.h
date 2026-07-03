#pragma once

#include "Object.h"
#include "Object/EntityHandle.h"
#include "Reflection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Math/GeometryTypes.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
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
         * @brief ローカルトランスフォームを取得
         */
        const Math::Transform& GetLocalTransform() const { return m_LocalTransform; }

        /**
         * @brief ワールドトランスフォームを設定
         */
        void SetWorldTransform(const Math::Transform& worldTransform);

        /**
         * @brief ワールドトランスフォームを取得
         */
        const Math::Transform& GetWorldTransform() const;

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
        const Math::Vector3& GetPosition() const;

        /**
         * @brief ワールド回転を設定（クォータニオン）
         */
        void SetRotation(const Math::Quaternion& rot);
        void SetRotation(float x, float y, float z, float w);

        /**
         * @brief ワールド回転を取得（クォータニオン）
         */
        const Math::Quaternion& GetRotation() const;

        /**
         * @brief ワールドスケールを設定
         */
        void SetScale(const Math::Vector3& scale);
        void SetScale(float x, float y, float z);

        /**
         * @brief ワールドスケールを取得
         */
        const Math::Vector3& GetScale() const;

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

        /**
         * @brief 更新が有効かどうか
         * @return 有効な場合true
         */
        bool IsTickEnabled() const { return bTickEnabled; }

        /**
         * @brief 更新の有効/無効を設定
         * @param bEnabled 有効にする場合true
         */
        void SetTickEnabled(bool bEnabled) { bTickEnabled = bEnabled; }

        // ========================================
        // アクティブ状態
        // ========================================

        /**
         * @brief オブジェクトがアクティブかどうか
         * @return アクティブな場合true
         */
        bool IsActive() const { return bActive; }

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
            bPendingDestroy = true;
            Object::Destroy();
        }

        /**
         * @brief 破棄予約されているかどうか
         * @return 破棄予約されている場合true
         */
        bool IsPendingDestroy() const { return bPendingDestroy || Object::IsPendingDestroy(); }

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================
        PROPERTY(bool, bTickEnabled)    // Tick更新が有効か
        PROPERTY(bool, bActive)         // アクティブ状態
        PROPERTY(bool, bPendingDestroy) // 破棄予約フラグ

        // 表示名（エディタでのリネーム用。空文字は未設定を表す）
        PROPERTY(Container::String, Name)

        // トランスフォーム
        PROPERTY(Math::Vector3, Position)    // ローカル位置
        PROPERTY(Math::Quaternion, Rotation) // ローカル回転（クォータニオン）
        PROPERTY(Math::Vector3, Scale)       // ローカルスケール

        // オブジェクトID（World内でユニーク）
        PROPERTY(uint64_t, ObjectId)

        Math::Transform m_LocalTransform;
        Math::Transform m_CachedWorldTransform;
        bool m_bWorldTransformDirty = true;
        uint64_t m_TransformVersion = 1;
        EntityHandle m_EntityHandle;

    private:
        void MarkRenderStateDirtyRecursive();
        void SetLocalTransform(const Math::Transform& transform);
        void SyncLocalTransformFromProperties();
        Math::Transform EvaluateWorldTransformNonMutating() const;
    };

} // namespace NorvesLib::Core

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Entity, NorvesLib::Core::EClassCastFlags::Entity)
