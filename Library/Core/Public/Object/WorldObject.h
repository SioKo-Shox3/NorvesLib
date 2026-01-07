#pragma once

#include "Object.h"
#include "Reflection.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    // 前方宣言
    class World;

    /**
     * @brief Worldに属するオブジェクトの基底クラス
     *
     * WorldObjectはWorldのInnerとして管理され、Worldと寿命が一致します。
     * シーン内に存在するゲームオブジェクト（Actor、Componentなど）の基底クラスです。
     *
     * 責任者: World
     * 寿命管理: Inner/Outer親子関係（WorldのInnerとして登録）
     *
     * Resourceとの違い:
     * - ResourceはGEngineが管理し、参照カウントで寿命を制御
     * - WorldObjectはWorldが管理し、World破棄時に連鎖破棄される
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
        // World関連
        // ========================================

        /**
         * @brief このオブジェクトが属するWorldを取得
         * @return Worldへのポインタ（Worldに属していない場合はnullptr）
         */
        World *GetWorld() const { return m_World; }

        /**
         * @brief このオブジェクトが属するWorldを設定（内部用）
         * @param world 設定するWorld
         */
        void SetWorld(World *world) { m_World = world; }

        /**
         * @brief Worldに属しているかどうか
         * @return Worldに属している場合true
         */
        bool IsInWorld() const { return m_World != nullptr; }

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
        bool IsTickEnabled() const { return m_bTickEnabled; }

        /**
         * @brief 更新の有効/無効を設定
         * @param bEnabled 有効にする場合true
         */
        void SetTickEnabled(bool bEnabled) { m_bTickEnabled = bEnabled; }

        // ========================================
        // アクティブ状態
        // ========================================

        /**
         * @brief オブジェクトがアクティブかどうか
         * @return アクティブな場合true
         */
        bool IsActive() const { return m_bActive; }

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
        void MarkForDestroy() { m_bPendingDestroy = true; }

        /**
         * @brief 破棄予約されているかどうか
         * @return 破棄予約されている場合true
         */
        bool IsPendingDestroy() const { return m_bPendingDestroy; }

    protected:
        World *m_World = nullptr;       // 所属するWorld
        bool m_bTickEnabled = true;     // Tick更新が有効か
        bool m_bActive = true;          // アクティブ状態
        bool m_bPendingDestroy = false; // 破棄予約フラグ
    };

} // namespace NorvesLib::Core
