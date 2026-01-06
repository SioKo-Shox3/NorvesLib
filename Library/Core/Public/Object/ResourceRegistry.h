#pragma once

#include "Object/Resource.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core
{
    /**
     * @brief リソースレジストリ
     * 
     * GEngineが保持するサブシステムとして、すべてのResourceの管理を行います。
     * 
     * 責務:
     * - Resourceのライフサイクル管理（参照カウント方式）
     * - 重複ロード防止のためのキャッシュ機能
     * - リソースパスからのロード/取得
     * - 未使用リソースのガベージコレクション
     * 
     * 使用例:
     * ```cpp
     * // リソースのロード（キャッシュがあればキャッシュから返す）
     * auto texture = GEngine.GetResourceRegistry().Load<TextureResource>("path/to/texture.png");
     * 
     * // リソースの取得（ロード済みリソースのみ）
     * auto mesh = GEngine.GetResourceRegistry().Get<MeshResource>(resourceId);
     * ```
     */
    class ResourceRegistry
    {
    public:
        /**
         * @brief コンストラクタ
         */
        ResourceRegistry() = default;

        /**
         * @brief デストラクタ
         */
        ~ResourceRegistry();

        /**
         * @brief 初期化
         * @return 初期化成功時true
         */
        bool Initialize();

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // リソースのロード/取得
        // ========================================

        /**
         * @brief リソースをロードまたはキャッシュから取得
         * @tparam T リソース型（Resourceを継承している必要あり）
         * @param path リソースパス
         * @return リソースへの共有ポインタ
         */
        template <typename T>
        Container::TSharedPtr<T> Load(const Container::String& path)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            // キャッシュをチェック
            {
                Thread::ScopedLock lock(m_Mutex);
                auto it = m_PathToResource.find(path);
                if (it != m_PathToResource.end())
                {
                    // キャッシュヒット - 弱参照から共有ポインタを取得
                    if (auto resource = it->second.lock())
                    {
                        return Container::DynamicPointerCast<T>(resource);
                    }
                    // 弱参照が無効なら削除
                    m_PathToResource.erase(it);
                }
            }

            // 新規作成
            auto resource = CreateResource<T>(path);
            if (resource && resource->Load())
            {
                RegisterResource(resource, path);
                return resource;
            }

            return nullptr;
        }

        /**
         * @brief リソースIDからリソースを取得
         * @tparam T リソース型
         * @param resourceId リソースID
         * @return リソースへの共有ポインタ（見つからない場合はnullptr）
         */
        template <typename T>
        Container::TSharedPtr<T> Get(uint64_t resourceId)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            Thread::ScopedLock lock(m_Mutex);
            auto it = m_IdToResource.find(resourceId);
            if (it != m_IdToResource.end())
            {
                if (auto resource = it->second.lock())
                {
                    return Container::DynamicPointerCast<T>(resource);
                }
                // 弱参照が無効なら削除
                m_IdToResource.erase(it);
            }
            return nullptr;
        }

        /**
         * @brief リソースパスからリソースを取得（ロード済みのみ）
         * @tparam T リソース型
         * @param path リソースパス
         * @return リソースへの共有ポインタ（見つからない場合はnullptr）
         */
        template <typename T>
        Container::TSharedPtr<T> Find(const Container::String& path)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            Thread::ScopedLock lock(m_Mutex);
            auto it = m_PathToResource.find(path);
            if (it != m_PathToResource.end())
            {
                if (auto resource = it->second.lock())
                {
                    return Container::DynamicPointerCast<T>(resource);
                }
                m_PathToResource.erase(it);
            }
            return nullptr;
        }

        /**
         * @brief 新しいリソースを作成（ロードはしない）
         * @tparam T リソース型
         * @param path リソースパス
         * @return リソースへの共有ポインタ
         */
        template <typename T>
        Container::TSharedPtr<T> CreateResource(const Container::String& path)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            auto resource = Container::MakeShared<T>();
            if (resource)
            {
                resource->SetResourceId(GenerateResourceId());
                resource->SetResourcePath(path);
                resource->SetResourceName(Identity(path));
                resource->Initialize();
            }
            return resource;
        }

        /**
         * @brief リソースを作成して登録（ファイルパスなし）
         * @tparam T リソース型
         * @param name リソース名
         * @return リソースへの共有ポインタ
         */
        template <typename T>
        Container::TSharedPtr<T> CreateTransient(const Container::String& name)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            auto resource = Container::MakeShared<T>();
            if (resource)
            {
                uint64_t id = GenerateResourceId();
                resource->SetResourceId(id);
                resource->SetResourceName(Identity(name));
                resource->Initialize();

                // 一時リソースとしてIDのみ登録
                Thread::ScopedLock lock(m_Mutex);
                m_IdToResource[id] = resource;
            }
            return resource;
        }

        // ========================================
        // ガベージコレクション
        // ========================================

        /**
         * @brief 未使用リソースをクリーンアップ
         * 弱参照が無効になったエントリを削除
         * @return 削除されたエントリ数
         */
        size_t CollectGarbage();

        /**
         * @brief すべてのリソースをアンロード
         */
        void UnloadAll();

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief 登録されているリソース数を取得
         */
        size_t GetResourceCount() const;

        /**
         * @brief キャッシュされているパス数を取得
         */
        size_t GetCachedPathCount() const;

        /**
         * @brief 合計メモリ使用量を取得（概算）
         */
        size_t GetTotalMemoryUsage() const;

    private:
        /**
         * @brief リソースを登録
         * @param resource リソース
         * @param path リソースパス
         */
        void RegisterResource(Container::TSharedPtr<Resource> resource, const Container::String& path);

        /**
         * @brief リソースIDを生成
         * @return 新しいリソースID
         */
        uint64_t GenerateResourceId();

        // 初期化フラグ
        bool m_bInitialized = false;

        // リソースID生成カウンター
        Thread::Atomic<uint64_t> m_NextResourceId{1};

        // リソースキャッシュ（弱参照で保持）
        // パス -> リソース
        Container::UnorderedMap<Container::String, Container::TWeakPtr<Resource>> m_PathToResource;
        
        // ID -> リソース
        Container::UnorderedMap<uint64_t, Container::TWeakPtr<Resource>> m_IdToResource;

        // スレッドセーフ用ミューテックス
        mutable Thread::Mutex m_Mutex;

        // コピー禁止
        ResourceRegistry(const ResourceRegistry&) = delete;
        ResourceRegistry& operator=(const ResourceRegistry&) = delete;
    };

} // namespace NorvesLib::Core
