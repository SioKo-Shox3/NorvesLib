#include "FileStream/Package.h"
#include "FileStream/FileStream.h"
#include "Object/Reflection.h"
#include "Thread/JobSystem.h"

namespace NorvesLib::FileStream
{
    namespace
    {
        bool ReadPackageFile(const Core::Container::String &path,
                             Core::Container::VariableArray<uint8_t> &outRawData)
        {
            auto fileStream = FileStream::Create(path, FileMode::Read, FileAccess::Read, FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                return false;
            }

            int64_t fileSize = fileStream->GetSize();
            if (fileSize <= 0)
            {
                return false;
            }

            outRawData.resize(static_cast<size_t>(fileSize));
            size_t bytesRead = fileStream->Read(outRawData.data(), outRawData.size());
            if (bytesRead != static_cast<size_t>(fileSize))
            {
                outRawData.clear();
                return false;
            }

            return true;
        }
    }

    // ========================================
    // リフレクション実装
    // ========================================

    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(Package, Core::Object)

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    Package::Package()
        : Object(), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
    }

    Package::Package(const Core::FieldInitializer *initializer)
        : Object(initializer), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
    }

    Package::Package(const Core::IUnknown *sourceObject)
        : Object(sourceObject), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
        // ソースがPackageの場合、データをコピー
        if (sourceObject)
        {
            const Package *sourcePackage = dynamic_cast<const Package *>(sourceObject);
            if (sourcePackage)
            {
                m_FilePath = sourcePackage->m_FilePath;
                m_PackageName = sourcePackage->m_PackageName;
                m_RawData = sourcePackage->m_RawData;
                m_LoadState = sourcePackage->m_LoadState;
                m_LastAccessFrame = sourcePackage->m_LastAccessFrame;
            }
        }
    }

    Package::~Package()
    {
        Unload();
    }

    // ========================================
    // 初期化・終了処理
    // ========================================

    void Package::Initialize()
    {
        Object::Initialize();
    }

    void Package::Finalize()
    {
        Unload();
        Object::Finalize();
    }

    // ========================================
    // ファイル読み込み
    // ========================================

    bool Package::Load(const Core::Container::String &path)
    {
        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted() && m_LoadState == PackageLoadState::Loading)
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }
        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
        }

        // 既にロード済みの場合は何もしない
        if (m_LoadState == PackageLoadState::Loaded && m_FilePath == path)
        {
            return true;
        }

        // 状態を更新
        m_LoadState = PackageLoadState::Loading;
        m_FilePath = path;
        m_PackageName = Core::Identity(path);

        Core::Container::VariableArray<uint8_t> rawData;
        if (!ReadPackageFile(path, rawData))
        {
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        m_RawData = std::move(rawData);
        m_LoadState = PackageLoadState::Loaded;
        return true;
    }

    bool Package::LoadAsync(const Core::Container::String &path)
    {
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted() && m_FilePath == path)
            {
                return true;
            }
        }

        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted())
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_FilePath = path;
            m_PackageName = Core::Identity(path);
            m_RawData.clear();
            m_LoadState = PackageLoadState::Loading;
        }

        auto task = Thread::Task::Create([this, path]()
        {
            Core::Container::VariableArray<uint8_t> rawData;
            bool bSuccess = ReadPackageFile(path, rawData);

            Thread::ScopedLock lock(m_LoadMutex);
            if (m_FilePath != path)
            {
                return;
            }

            if (!bSuccess)
            {
                m_RawData.clear();
                m_LoadState = PackageLoadState::Failed;
                return;
            }

            m_RawData = std::move(rawData);
            m_LoadState = PackageLoadState::Loaded;
        }, Thread::TaskPriority::NORMAL);

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask = task;
        }

        Thread::JobSystem::Get().SubmitTask(task);
        return true;
    }

    void Package::Unload()
    {
        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted())
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }
        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
        }

        m_RawData.clear();
        m_RawData.shrink_to_fit();
        m_LoadState = PackageLoadState::Unloaded;
    }

    // ========================================
    // バッファアクセス
    // ========================================

    const uint8_t *Package::GetData() const
    {
        if (m_LoadState != PackageLoadState::Loaded || m_RawData.empty())
        {
            return nullptr;
        }
        return m_RawData.data();
    }

    Core::Container::Span<const uint8_t> Package::GetDataSpan() const
    {
        if (m_LoadState != PackageLoadState::Loaded || m_RawData.empty())
        {
            return Core::Container::Span<const uint8_t>();
        }
        return Core::Container::Span<const uint8_t>(m_RawData.data(), m_RawData.size());
    }

} // namespace NorvesLib::FileStream
