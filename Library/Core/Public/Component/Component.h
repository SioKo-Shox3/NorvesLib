#pragma once

#include "Object/Object.h"
#include "Object/Entity.h"
#include "Object/Reflection.h"
#include "Container/Containers.h"
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
        bool IsEnabled() const { return bEnabled; }

        /**
         * @brief アクティブかどうか（オーナーが存在かつ自身も有効）
         */
        virtual bool IsActive() const { return bEnabled && GetOuter() != nullptr; }

        // ========================================
        // Render state dirty tracking
        // ========================================

        /**
         * @brief RenderThread同期が必要な状態にする
         */
        void MarkRenderStateDirty() { m_bRenderStateDirty = true; }

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
        void SetTickEnabled(bool bEnabled) { bTickEnabled = bEnabled; }
        bool IsTickEnabled() const { return bTickEnabled; }

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================
        PROPERTY(uint64_t, ComponentId) // コンポーネントID
        PROPERTY(bool, bEnabled)        // 有効状態
        PROPERTY(bool, bTickEnabled)    // Tick有効
        PROPERTY(bool, bBegunPlay)      // BeginPlay済みフラグ

        bool m_bRenderStateDirty = true;
        uint64_t m_LastSyncedTransformVersion = 0;

    private:
        // ID生成用静的カウンター
        static uint64_t s_NextComponentId;
    };

    // コンポーネントへのスマートポインタ
    using ComponentPtr = Container::TSharedPtr<Component>;
    using ComponentWeakPtr = Container::TWeakPtr<Component>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::Component, NorvesLib::Core::EClassCastFlags::Component)
