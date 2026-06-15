#pragma once

#include "Object.h"
#include "Reflection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
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
     * WorldObjectはWorldのInnerとして管理され、Worldと寿命が一致します。
     * シーン内に存在するゲームオブジェクト（Actor、Componentなど）の基底クラスです。
     *
     * Outer/Inner関係:
     * - World（Outer）→ WorldObject（Inner）
     * - WorldObject（Outer）→ Component（Inner）
     *
     * GetOuter()をたどればWorldを取得できます。
     * m_InnersにはこのオブジェクトにアタッチされたComponentが含まれます。
     */
    class WorldObject : public Object
    {
        REFLECTION_CLASS(WorldObject, Object)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        WorldObject();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit WorldObject(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit WorldObject(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~WorldObject();

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
         * @brief オブジェクトIDを取得
         */
        uint64_t GetObjectId() const { return ObjectId; }

        /**
         * @brief オブジェクトIDを設定（World内部用）
         */
        void SetObjectId(uint64_t id) { ObjectId = id; }

        // ========================================
        // トランスフォーム
        // ========================================

        /**
         * @brief ワールド位置を設定
         */
        void SetPosition(const Math::Vector3 &pos)
        {
            Position = pos;
            ++m_TransformVersion;
        }
        void SetPosition(float x, float y, float z)
        {
            Position = Math::Vector3(x, y, z);
            ++m_TransformVersion;
        }

        /**
         * @brief ワールド位置を取得
         */
        const Math::Vector3 &GetPosition() const { return Position; }

        /**
         * @brief ワールド回転を設定（クォータニオン）
         */
        void SetRotation(const Math::Quaternion &rot)
        {
            Rotation = rot;
            ++m_TransformVersion;
        }
        void SetRotation(float x, float y, float z, float w)
        {
            Rotation = Math::Quaternion(x, y, z, w);
            ++m_TransformVersion;
        }

        /**
         * @brief ワールド回転を取得（クォータニオン）
         */
        const Math::Quaternion &GetRotation() const { return Rotation; }

        /**
         * @brief スケールを設定
         */
        void SetScale(const Math::Vector3 &scale)
        {
            Scale = scale;
            ++m_TransformVersion;
        }
        void SetScale(float x, float y, float z)
        {
            Scale = Math::Vector3(x, y, z);
            ++m_TransformVersion;
        }

        /**
         * @brief スケールを取得
         */
        const Math::Vector3 &GetScale() const { return Scale; }

        /**
         * @brief トランスフォーム更新バージョンを取得
         */
        uint64_t GetTransformVersion() const { return m_TransformVersion; }

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

        // トランスフォーム
        PROPERTY(Math::Vector3, Position)    // ワールド位置
        PROPERTY(Math::Quaternion, Rotation) // ワールド回転（クォータニオン）
        PROPERTY(Math::Vector3, Scale)       // スケール

        // オブジェクトID（World内でユニーク）
        PROPERTY(uint64_t, ObjectId)

        uint64_t m_TransformVersion = 1;
    };

} // namespace NorvesLib::Core

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::WorldObject, NorvesLib::Core::EClassCastFlags::WorldObject)
