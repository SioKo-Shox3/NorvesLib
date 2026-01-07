#pragma once

#include "Object.h"
#include "Reflection.h"
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    /**
     * @brief リソースの状態を表す列挙型
     */
    enum class ResourceState : uint8_t
    {
        Unloaded, // 未ロード
        Loading,  // ロード中
        Loaded,   // ロード完了
        Failed,   // ロード失敗
        Unloading // アンロード中
    };

    /**
     * @brief リソースの基底クラス
     *
     * Worldに依存しない、テクスチャ・メッシュ・マテリアル等のデータリソースの基底クラス。
     * Objectを継承して参照カウント管理機能を実装します。
     *
     * 責任者: GEngine（ResourceRegistry経由）
     * 寿命管理: IUnknownの参照カウント（AddRef/Release）で管理
     *
     * WorldObjectとの違い:
     * - WorldObjectはWorldのInnerとして管理され、Worldと寿命が一致
     * - ResourceはGEngineが管理し、参照カウントで寿命を制御
     * - WorldやWorldObjectが破棄されてもResourceは生存可能
     */
    class Resource : public Object
    {
        REFLECTION_CLASS(Resource, Object)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Resource();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit Resource(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit Resource(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~Resource();

        /**
         * @brief リソースを初期化します
         */
        virtual void Initialize() override;

        /**
         * @brief フィールド初期化子を使用してリソースを初期化します
         * @param initializer フィールド初期化子
         * @return 初期化に成功した場合はtrue
         */
        virtual bool Initialize(const FieldInitializer *initializer);

        /**
         * @brief リソースの破棄前処理を行います
         */
        virtual void Finalize() override;

        // ========================================
        // リソース固有のメソッド
        // ========================================

        /**
         * @brief リソースの識別子を取得します
         * @return リソースID
         */
        uint64_t GetResourceId() const { return m_ResourceId; }

        /**
         * @brief リソースパスを取得します
         * @return リソースパス
         */
        const Container::String &GetResourcePath() const { return m_ResourcePath; }

        /**
         * @brief リソース名を取得します
         * @return リソース名
         */
        const Identity &GetResourceName() const { return m_ResourceName; }

        /**
         * @brief リソースの状態を取得します
         * @return リソース状態
         */
        ResourceState GetResourceState() const { return m_State; }

        /**
         * @brief リソースがロード済みかどうか
         * @return ロード済みの場合true
         */
        bool IsLoaded() const { return m_State == ResourceState::Loaded; }

        /**
         * @brief リソースが有効かどうか
         * @return 有効な場合true
         */
        virtual bool IsValid() const { return m_State == ResourceState::Loaded; }

        /**
         * @brief リソースをロードします
         * @return ロードに成功した場合true
         */
        virtual bool Load();

        /**
         * @brief リソースをアンロードします
         */
        virtual void Unload();

        /**
         * @brief リソースのメモリサイズを取得します（バイト単位）
         * @return メモリサイズ
         */
        virtual size_t GetMemorySize() const { return 0; }

    protected:
        /**
         * @brief リソースIDを設定します（内部用）
         * @param id リソースID
         */
        void SetResourceId(uint64_t id) { m_ResourceId = id; }

        /**
         * @brief リソースパスを設定します（内部用）
         * @param path リソースパス
         */
        void SetResourcePath(const Container::String &path) { m_ResourcePath = path; }

        /**
         * @brief リソース名を設定します（内部用）
         * @param name リソース名
         */
        void SetResourceName(const Identity &name) { m_ResourceName = name; }

        /**
         * @brief リソース状態を設定します（内部用）
         * @param state リソース状態
         */
        void SetResourceState(ResourceState state) { m_State = state; }

        // ResourceRegistryからアクセス可能
        friend class ResourceRegistry;

    private:
        uint64_t m_ResourceId = 0;                       // リソースの一意識別子
        Container::String m_ResourcePath;                // リソースのファイルパス
        Identity m_ResourceName;                         // リソース名
        ResourceState m_State = ResourceState::Unloaded; // リソース状態
    };

} // namespace NorvesLib::Core
