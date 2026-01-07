#pragma once

#include "Object/Object.h"
#include "Object/Reflection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Text/IdentityPool.h"
#include "Thread/Mutex.h"

// 前方宣言
namespace NorvesLib::FileStream
{
    class Package;
}

namespace NorvesLib::Core
{

    /**
     * @brief アセット解放ポリシー
     */
    enum class AssetUnloadPolicy : uint8_t
    {
        Manual,         ///< 手動解放のみ
        OnUnreferenced, ///< 参照がなくなったら解放
        LRU             ///< 最近使用されていないものから解放
    };

    /**
     * @brief アセット（Package）の寿命管理を行うサブシステム
     *
     * Engineのサブシステムとして、Packageを
     * Innerとして保持し、解放タイミングを管理します。
     *
     * 責務:
     * - Packageのライフサイクル管理
     * - ファイルパスからのPackage検索
     * - 解放ポリシーに基づくガベージコレクション
     * - メモリ使用量の管理
     *
     * 使用例:
     * ```cpp
     * auto* package = GEngine.GetAssetRegistry().LoadPackage("meshes/character.pak");
     * if (package->IsLoaded())
     * {
     *     auto meshResource = package->Deserialize<MeshResource>();
     * }
     * ```
     */
    class AssetRegistry : public Object
    {
        REFLECTION_CLASS(AssetRegistry, Object)

    public:
        /**
         * @brief コンストラクタ
         */
        AssetRegistry();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit AssetRegistry(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit AssetRegistry(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~AssetRegistry();

        /**
         * @brief オブジェクトを初期化します
         */
        void Initialize() override;

        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        void Finalize() override;

        // ========================================
        // サブシステム初期化
        // ========================================

        /**
         * @brief サブシステムを初期化します
         * @return 初期化成功時true
         */
        bool InitializeSubsystem();

        /**
         * @brief サブシステムを終了します
         */
        void ShutdownSubsystem();

        /**
         * @brief サブシステムが初期化済みかどうか
         * @return 初期化済みの場合true
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // Package管理
        // ========================================

        /**
         * @brief パッケージをロードします
         * @param path ファイルパス
         * @return ロードされたPackageへのポインタ（失敗時nullptr）
         */
        FileStream::Package *LoadPackage(const Container::String &path);

        /**
         * @brief パッケージを非同期でロードします
         * @param path ファイルパス
         * @return ロード開始されたPackageへのポインタ（失敗時nullptr）
         */
        FileStream::Package *LoadPackageAsync(const Container::String &path);

        /**
         * @brief パッケージを検索します（ロード済みのみ）
         * @param path ファイルパス
         * @return Packageへのポインタ（見つからない場合nullptr）
         */
        FileStream::Package *FindPackage(const Container::String &path);

        /**
         * @brief パッケージをアンロードします
         * @param package アンロードするパッケージ
         */
        void UnloadPackage(FileStream::Package *package);

        /**
         * @brief すべてのパッケージをアンロードします
         */
        void UnloadAll();

        // ========================================
        // 解放ポリシー
        // ========================================

        /**
         * @brief 解放ポリシーを設定します
         * @param policy 解放ポリシー
         */
        void SetUnloadPolicy(AssetUnloadPolicy policy) { m_UnloadPolicy = policy; }

        /**
         * @brief 解放ポリシーを取得します
         * @return 現在の解放ポリシー
         */
        AssetUnloadPolicy GetUnloadPolicy() const { return m_UnloadPolicy; }

        /**
         * @brief 最大キャッシュサイズを設定します
         * @param bytes 最大サイズ（バイト）
         */
        void SetMaxCacheSize(size_t bytes) { m_MaxCacheSize = bytes; }

        /**
         * @brief 最大キャッシュサイズを取得します
         * @return 最大サイズ（バイト）
         */
        size_t GetMaxCacheSize() const { return m_MaxCacheSize; }

        // ========================================
        // ガベージコレクション
        // ========================================

        /**
         * @brief ガベージコレクションを実行します
         * @return 解放されたパッケージ数
         */
        size_t CollectGarbage();

        /**
         * @brief フレーム更新（GC判定用）
         * @param frameIndex 現在のフレーム番号
         */
        void Update(uint64_t frameIndex);

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief 登録されているパッケージ数を取得します
         * @return パッケージ数
         */
        size_t GetPackageCount() const;

        /**
         * @brief 合計メモリ使用量を取得します
         * @return メモリ使用量（バイト）
         */
        size_t GetTotalMemoryUsage() const;

        /**
         * @brief ロード済みパッケージ数を取得します
         * @return ロード済みパッケージ数
         */
        size_t GetLoadedPackageCount() const;

    private:
        /**
         * @brief 新しいPackageを作成してInnerとして登録
         * @param path ファイルパス
         * @return 作成されたPackage
         */
        FileStream::Package *CreatePackage(const Container::String &path);

        /**
         * @brief LRUポリシーでの解放処理
         * @return 解放されたパッケージ数
         */
        size_t CollectLRU();

        // 初期化フラグ
        bool m_bInitialized = false;

        // 解放ポリシー
        AssetUnloadPolicy m_UnloadPolicy = AssetUnloadPolicy::OnUnreferenced;

        // 最大キャッシュサイズ（デフォルト512MB）
        size_t m_MaxCacheSize = 512 * 1024 * 1024;

        // LRU用フレーム閾値（この間アクセスがなければ解放対象）
        uint32_t m_LRUFrameThreshold = 300;

        // 現在のフレーム番号
        uint64_t m_CurrentFrame = 0;

        // パス検索用マップ（Identity → Package*）
        Container::UnorderedMap<Identity, FileStream::Package *, Identity::Hasher> m_PathToPackage;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core
