#include "FileStream/Package.h"
#include "FileStream/FileStream.h"
#include "Object/Reflection.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::FileStream
{
    // IMPLEMENT_CLASSマクロが使用するCore型をインポート
    using NorvesLib::Core::ObjectUtility;
    using NorvesLib::Core::VariableContainer;

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
        // 既にロード済みの場合は何もしない
        if (m_LoadState == PackageLoadState::Loaded && m_FilePath == path)
        {
            return true;
        }

        // 状態を更新
        m_LoadState = PackageLoadState::Loading;
        m_FilePath = path;
        m_PackageName = Core::Identity(path);

        // FileStreamを使用してファイルを読み込み
        auto fileStream = FileStream::Create(path, FileMode::Read, FileAccess::Read, FileShare::Read);
        if (!fileStream || !fileStream->IsOpen())
        {
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        // ファイルサイズを取得
        int64_t fileSize = fileStream->GetSize();
        if (fileSize <= 0)
        {
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        // バッファを確保してデータを読み込み
        m_RawData.resize(static_cast<size_t>(fileSize));
        size_t bytesRead = fileStream->Read(m_RawData.data(), m_RawData.size());

        if (bytesRead != static_cast<size_t>(fileSize))
        {
            m_RawData.clear();
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        m_LoadState = PackageLoadState::Loaded;
        return true;
    }

    bool Package::LoadAsync(const Core::Container::String &path)
    {
        // TODO: 非同期読み込み実装
        // 現時点では同期読み込みにフォールバック
        return Load(path);
    }

    void Package::Unload()
    {
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
