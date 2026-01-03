#pragma once

#include "Object/Object.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{

    // 前方宣言
    class Component;

    // ========================================
    // コンポーネントオーナーインターフェース
    // ========================================

    /**
     * @brief コンポーネントオーナーインターフェース
     *
     * コンポーネントをアタッチ可能なオブジェクトが実装するインターフェース
     */
    class IComponentOwner
    {
    public:
        virtual ~IComponentOwner() = default;

        /**
         * @brief オーナーのユニークIDを取得
         */
        virtual uint64_t GetOwnerId() const = 0;

        /**
         * @brief ワールド位置を取得
         */
        virtual void GetWorldPosition(float &outX, float &outY, float &outZ) const = 0;

        /**
         * @brief ワールド回転を取得（クォータニオン）
         */
        virtual void GetWorldRotation(float &outX, float &outY, float &outZ, float &outW) const = 0;

        /**
         * @brief ワールドスケールを取得
         */
        virtual void GetWorldScale(float &outX, float &outY, float &outZ) const = 0;
    };

    // ========================================
    // コンポーネント基底クラス
    // ========================================

    /**
     * @brief コンポーネント基底クラス
     *
     * すべてのコンポーネントの基底クラス。
     * オブジェクトにアタッチされて機能を提供します。
     */
    class Component : public Object
    {
    public:
        using Super = Object;

        /**
         * @brief デフォルトコンストラクタ
         */
        Component();

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
        // オーナー管理
        // ========================================

        /**
         * @brief オーナーを設定
         * @param owner コンポーネントオーナー
         */
        void SetOwner(IComponentOwner *owner);

        /**
         * @brief オーナーを取得
         * @return コンポーネントオーナー
         */
        IComponentOwner *GetOwner() const { return m_Owner; }

        /**
         * @brief オーナーIDを取得
         */
        uint64_t GetOwnerId() const
        {
            return m_Owner ? m_Owner->GetOwnerId() : 0;
        }

        // ========================================
        // コンポーネントID
        // ========================================

        /**
         * @brief コンポーネントIDを取得
         */
        uint64_t GetComponentId() const { return m_ComponentId; }

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
        bool IsEnabled() const { return m_bEnabled; }

        /**
         * @brief アクティブかどうか（オーナーが有効かつ自身も有効）
         */
        virtual bool IsActive() const { return m_bEnabled && m_Owner != nullptr; }

        // ========================================
        // Tick設定
        // ========================================

        /**
         * @brief Tick有効設定
         */
        void SetTickEnabled(bool bEnabled) { m_bTickEnabled = bEnabled; }
        bool IsTickEnabled() const { return m_bTickEnabled; }

    protected:
        // オーナー
        IComponentOwner *m_Owner = nullptr;

        // コンポーネントID
        uint64_t m_ComponentId = 0;

        // 状態
        bool m_bEnabled = true;
        bool m_bTickEnabled = true;
        bool m_bBegunPlay = false;

    private:
        // ID生成用静的カウンター
        static uint64_t s_NextComponentId;
    };

    // コンポーネントへのスマートポインタ
    using ComponentPtr = Container::TSharedPtr<Component>;
    using ComponentWeakPtr = Container::TWeakPtr<Component>;

} // namespace NorvesLib::Core::Component
