#include "Engine/AssetRegistry.h"
#include "FileStream/Package.h"
#include "Object/Reflection.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core
{

    // ========================================
    // リフレクション実装
    // ========================================

    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(AssetRegistry, Object)

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    AssetRegistry::AssetRegistry()
        : Object(), m_bInitialized(false), m_UnloadPolicy(AssetUnloadPolicy::OnUnreferenced), m_MaxCacheSize(512 * 1024 * 1024), m_LRUFrameThreshold(300), m_CurrentFrame(0)
    {
    }

    AssetRegistry::AssetRegistry(const FieldInitializer *initializer)
        : Object(initializer), m_bInitialized(false), m_UnloadPolicy(AssetUnloadPolicy::OnUnreferenced), m_MaxCacheSize(512 * 1024 * 1024), m_LRUFrameThreshold(300), m_CurrentFrame(0)
    {
    }

    AssetRegistry::AssetRegistry(const IUnknown *sourceObject)
        : Object(sourceObject), m_bInitialized(false), m_UnloadPolicy(AssetUnloadPolicy::OnUnreferenced), m_MaxCacheSize(512 * 1024 * 1024), m_LRUFrameThreshold(300), m_CurrentFrame(0)
    {
        // AssetRegistryはコピーによるインスタンス化を想定しない
        // 必要に応じて状態コピーを実装
    }

    AssetRegistry::~AssetRegistry()
    {
        ShutdownSubsystem();
    }

    // ========================================
    // 初期化・終了処理
    // ========================================

    void AssetRegistry::Initialize()
    {
        Object::Initialize();
    }

    void AssetRegistry::Finalize()
    {
        ShutdownSubsystem();
        Object::Finalize();
    }

    bool AssetRegistry::InitializeSubsystem()
    {
        if (m_bInitialized)
        {
            return true;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("AssetRegistry", "AssetRegistry initialized");
        return true;
    }

    void AssetRegistry::ShutdownSubsystem()
    {
        if (!m_bInitialized)
        {
            return;
        }

        UnloadAll();
        m_bInitialized = false;
        NORVES_LOG_INFO("AssetRegistry", "AssetRegistry shutdown");
    }

    // ========================================
    // Package管理
    // ========================================

    FileStream::Package *AssetRegistry::LoadPackage(const Container::String &path)
    {
        Thread::ScopedLock lock(m_Mutex);

        Identity pathId(path);

        // 既存パッケージを検索
        auto it = m_PathToPackage.find(pathId);
        if (it != m_PathToPackage.end())
        {
            FileStream::Package *package = it->second;
            if (package)
            {
                package->SetLastAccessFrame(m_CurrentFrame);
                return package;
            }
        }

        // 新規作成
        FileStream::Package *package = CreatePackage(path);
        if (!package)
        {
            NORVES_LOG_ERROR("AssetRegistry", "Failed to create package: " + path);
            return nullptr;
        }

        // ファイル読み込み
        if (!package->Load(path))
        {
            NORVES_LOG_ERROR("AssetRegistry", "Failed to load package: " + path);
            // パッケージは保持するが、状態がFailedになる
        }

        package->SetLastAccessFrame(m_CurrentFrame);
        NORVES_LOG_DEBUG("AssetRegistry", "Loaded package: " + path);
        return package;
    }

    FileStream::Package *AssetRegistry::LoadPackageAsync(const Container::String &path)
    {
        Thread::ScopedLock lock(m_Mutex);

        Identity pathId(path);

        // 既存パッケージを検索
        auto it = m_PathToPackage.find(pathId);
        if (it != m_PathToPackage.end())
        {
            FileStream::Package *package = it->second;
            if (package)
            {
                package->SetLastAccessFrame(m_CurrentFrame);
                return package;
            }
        }

        // 新規作成
        FileStream::Package *package = CreatePackage(path);
        if (!package)
        {
            return nullptr;
        }

        // 非同期読み込み開始
        package->LoadAsync(path);
        package->SetLastAccessFrame(m_CurrentFrame);
        return package;
    }

    FileStream::Package *AssetRegistry::FindPackage(const Container::String &path)
    {
        Thread::ScopedLock lock(m_Mutex);

        Identity pathId(path);
        auto it = m_PathToPackage.find(pathId);
        if (it != m_PathToPackage.end())
        {
            FileStream::Package *package = it->second;
            if (package)
            {
                package->SetLastAccessFrame(m_CurrentFrame);
                return package;
            }
        }
        return nullptr;
    }

    void AssetRegistry::UnloadPackage(FileStream::Package *package)
    {
        if (!package)
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);

        // マップから削除
        Identity pathId(package->GetFilePath());
        m_PathToPackage.erase(pathId);

        // PackageはこのAssetRegistryのInnerなので、RemoveInnerで解放
        RemoveInner(package);

        NORVES_LOG_DEBUG("AssetRegistry", "Unloaded package: " + package->GetFilePath());
    }

    void AssetRegistry::UnloadAll()
    {
        Thread::ScopedLock lock(m_Mutex);

        // マップをクリア（Packageへの参照も解放される）
        m_PathToPackage.clear();

        NORVES_LOG_INFO("AssetRegistry", "All packages unloaded");
    }

    FileStream::Package *AssetRegistry::CreatePackage(const Container::String &path)
    {
        // Packageを作成してInnerとして登録
        auto package = Container::MakeShared<FileStream::Package>();
        if (!package)
        {
            return nullptr;
        }

        package->Initialize();

        // このAssetRegistryのInnerとして登録
        AddInner(package.get());

        // マップに登録
        Identity pathId(path);
        m_PathToPackage[pathId] = package.get();

        return package.get();
    }

    // ========================================
    // ガベージコレクション
    // ========================================

    size_t AssetRegistry::CollectGarbage()
    {
        switch (m_UnloadPolicy)
        {
        case AssetUnloadPolicy::Manual:
            // 手動モードでは何もしない
            return 0;

        case AssetUnloadPolicy::OnUnreferenced:
            // TODO: 参照カウントチェック実装
            return 0;

        case AssetUnloadPolicy::LRU:
            return CollectLRU();

        default:
            return 0;
        }
    }

    size_t AssetRegistry::CollectLRU()
    {
        Thread::ScopedLock lock(m_Mutex);

        size_t currentMemory = GetTotalMemoryUsage();
        if (currentMemory <= m_MaxCacheSize)
        {
            return 0;
        }

        // LRUで古いパッケージを収集
        Container::VariableArray<FileStream::Package *> toRemove;

        for (auto &[pathId, package] : m_PathToPackage)
        {
            if (!package)
            {
                continue;
            }

            uint64_t framesSinceAccess = m_CurrentFrame - package->GetLastAccessFrame();
            if (framesSinceAccess > m_LRUFrameThreshold)
            {
                toRemove.push_back(package);
            }
        }

        // 解放実行
        size_t removedCount = 0;
        for (auto *package : toRemove)
        {
            Identity pathId(package->GetFilePath());
            m_PathToPackage.erase(pathId);
            RemoveInner(package);
            ++removedCount;

            // メモリ使用量をチェック
            if (GetTotalMemoryUsage() <= m_MaxCacheSize)
            {
                break;
            }
        }

        if (removedCount > 0)
        {
            NORVES_LOG_DEBUG("AssetRegistry",
                             "LRU collected " + Container::String(std::to_string(removedCount)) + " packages");
        }

        return removedCount;
    }

    void AssetRegistry::Update(uint64_t frameIndex)
    {
        m_CurrentFrame = frameIndex;

        // 定期的にGCを実行（例: 60フレームごと）
        if (frameIndex % 60 == 0)
        {
            CollectGarbage();
        }
    }

    // ========================================
    // 統計情報
    // ========================================

    size_t AssetRegistry::GetPackageCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        return m_PathToPackage.size();
    }

    size_t AssetRegistry::GetTotalMemoryUsage() const
    {
        Thread::ScopedLock lock(m_Mutex);

        size_t total = 0;
        for (const auto &[pathId, package] : m_PathToPackage)
        {
            if (package && package->IsLoaded())
            {
                total += package->GetMemorySize();
            }
        }
        return total;
    }

    size_t AssetRegistry::GetLoadedPackageCount() const
    {
        Thread::ScopedLock lock(m_Mutex);

        size_t count = 0;
        for (const auto &[pathId, package] : m_PathToPackage)
        {
            if (package && package->IsLoaded())
            {
                ++count;
            }
        }
        return count;
    }

} // namespace NorvesLib::Core
