#pragma once

#include "Object/Object.h"
#include "Object/Reflection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class WorldObject;
}

namespace NorvesLib::Core::Component
{

    // ========================================
    // コンポーネント基底クラス
    // ========================================

    /**
     * @brief コンポーネント基底クラス
     *
     * すべてのコンポーネントの基底クラス。
     * WorldObjectのInnerとして管理され、GetOuter()でオーナーのWorldObjectを取得できます。
     *
     * Outer/Inner関係:
     * - WorldObject（Outer）→ Component（Inner）
     * - WorldObjectが破棄されると、Inner全てが連鎖破棄されます。
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
         * @brief オーナーのWorldObjectを取得（Outer経由）
         * @return WorldObjectへのポインタ（所属していない場合はnullptr）
         */
        WorldObject *GetOwner();
        const WorldObject *GetOwner() const;

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

    private:
        // ID生成用静的カウンター
        static uint64_t s_NextComponentId;
    };

    // コンポーネントへのスマートポインタ
    using ComponentPtr = Container::TSharedPtr<Component>;
    using ComponentWeakPtr = Container::TWeakPtr<Component>;

} // namespace NorvesLib::Core::Component
