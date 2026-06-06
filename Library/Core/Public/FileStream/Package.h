#pragma once

#include "Object/Object.h"
#include "Object/Reflection.h"
#include "Asset/AssetBlob.h"
#include "Asset/AssetPackageFormat.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Text/IdentityPool.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

namespace NorvesLib::FileStream
{
    // REFLECTION_CLASSマクロが使用するCore型をインポート
    using NorvesLib::Core::FieldInitializer;
    using NorvesLib::Core::FunctionRegistry;
    using NorvesLib::Core::IClass;
    using NorvesLib::Core::IUnknown;
    using NorvesLib::Core::PropertyRegistry;
    using NorvesLib::Core::TClass;

    /**
     * @brief パッケージの読み込み状態
     */
    enum class PackageLoadState : uint8_t
    {
        Unloaded, ///< 未読み込み
        Loading,  ///< 読み込み中
        Loaded,   ///< 読み込み完了
        Failed    ///< 読み込み失敗
    };

    /**
     * @brief パッケージの解析形式
     */
    enum class PackageFormat : uint8_t
    {
        None,
        Raw,
        V1
    };

    /**
     * @brief Package内のエントリメタデータ
     *
     * PackageEntryはデータ所有権を持ちません。エントリ内容を非同期境界へ渡すときは
     * Package::OpenEntry() が返す AssetBlob を使い、Package-owned storage の寿命を保持します。
     */
    struct PackageEntry
    {
        Core::Container::AnsiString Name;
        Core::Asset::AssetPackageFourCC Type = 0;
        Core::Asset::AssetPackageCompression Compression = Core::Asset::AssetPackageCompression::None;
        size_t DataOffset = 0;
        size_t StoredSize = 0;
        size_t UncompressedSize = 0;
        uint64_t PayloadHash = Core::Asset::AssetPackageFormatV1::ZeroSizePayloadHash;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !Name.empty();
        }
    };

    /**
     * @brief ファイルバッファを抽象化するクラス
     *
     * AssetRegistryのInnerとして管理され、
     * ファイルの読み込み状態とバッファを保持します。
     *
     * 責務:
     * - ファイルI/O（読み込み）
     * - 生バイナリデータの保持
     * - 読み込み状態の管理
     *
     * 使用例:
     * ```cpp
     * auto package = assetRegistry.LoadPackage("meshes/character.pak");
     * if (package->IsLoaded())
     * {
     *     auto meshResource = package->Deserialize<MeshResource>();
     * }
     * ```
     */
    class Package : public Core::Object
    {
        REFLECTION_CLASS(Package, Core::Object)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Package();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit Package(const Core::FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit Package(const Core::IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~Package();

        /**
         * @brief オブジェクトを初期化します
         */
        void Initialize() override;

        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        void Finalize() override;

        // ========================================
        // ファイル読み込み
        // ========================================

        /**
         * @brief ファイルを読み込みます
         * @param path ファイルパス
         * @return 読み込み成功時true
         */
        bool Load(const Core::Container::String &path);

        /**
         * @brief 非同期でファイルを読み込みます
         * @param path ファイルパス
         * @return 読み込み開始成功時true
         */
        bool LoadAsync(const Core::Container::String &path);

        /**
         * @brief データをアンロードします
         */
        void Unload();

        /**
         * @brief メモリ上のバイト列からPackageを読み込みます
         * @param bytes 読み込み元バイト列
         * @param sourcePath 診断用ソースパス
         * @return 読み込み成功時true
         */
        bool LoadFromMemory(Core::Container::Span<const uint8_t> bytes, const Core::Container::String &sourcePath = {});

        // ========================================
        // 状態取得
        // ========================================

        /**
         * @brief 読み込み状態を取得します
         * @return 読み込み状態
         */
        PackageLoadState GetLoadState() const;

        /**
         * @brief ロード済みかどうか
         * @return ロード済みの場合true
         */
        bool IsLoaded() const;

        /**
         * @brief 読み込み中かどうか
         * @return 読み込み中の場合true
         */
        bool IsLoading() const;

        /**
         * @brief ファイルパスを取得します
         * @return ファイルパス
         */
        const Core::Container::String &GetFilePath() const { return m_FilePath; }

        /**
         * @brief パッケージ名（Identity）を取得します
         * @return パッケージ名
         */
        const Core::Identity &GetPackageName() const { return m_PackageName; }

        /**
         * @brief パッケージ形式を取得します
         * @return パッケージ形式
         */
        PackageFormat GetFormat() const;

        /**
         * @brief エントリ数を取得します
         * @return エントリ数
         */
        size_t GetEntryCount() const;

        /**
         * @brief index指定でエントリメタデータを取得します
         * @param index エントリindex
         * @param outEntry エントリ出力
         * @return 成功時true
         */
        bool GetEntry(size_t index, PackageEntry &outEntry) const;

        /**
         * @brief name/type指定でエントリメタデータを検索します
         * @param name エントリ名
         * @param type 32-bit FourCC
         * @param outEntry エントリ出力
         * @return 成功時true
         */
        bool FindEntry(const Core::Container::AnsiString &name,
                       Core::Asset::AssetPackageFourCC type,
                       PackageEntry &outEntry) const;

        /**
         * @brief エントリ内容をAssetBlobとして開きます
         * @param entry エントリメタデータ
         * @return storage lifetimeを保持するAssetBlob
         */
        Core::Asset::AssetBlob OpenEntry(const PackageEntry &entry) const;

        /**
         * @brief name/type指定でエントリ内容をAssetBlobとして開きます
         * @param name エントリ名
         * @param type 32-bit FourCC
         * @return storage lifetimeを保持するAssetBlob
         */
        Core::Asset::AssetBlob OpenEntry(const Core::Container::AnsiString &name,
                                         Core::Asset::AssetPackageFourCC type) const;

        // ========================================
        // バッファアクセス
        // ========================================

        /**
         * @brief 生データへのポインタを取得します
         * @return データポインタ（未ロード時はnullptr）
         */
        const uint8_t *GetData() const;

        /**
         * @brief データサイズを取得します
         * @return データサイズ（バイト）
         */
        size_t GetDataSize() const;

        /**
         * @brief データのSpanを取得します
         * @return データのSpan
         */
        Core::Container::Span<const uint8_t> GetDataSpan() const;

        // ========================================
        // メモリ情報
        // ========================================

        /**
         * @brief このパッケージが使用しているメモリサイズを取得します
         * @return メモリサイズ（バイト）
         */
        size_t GetMemorySize() const;

        /**
         * @brief 最後にアクセスされたフレーム番号を取得します
         * @return フレーム番号
         */
        uint64_t GetLastAccessFrame() const { return m_LastAccessFrame; }

        /**
         * @brief 最後にアクセスされたフレーム番号を更新します
         * @param frame フレーム番号
         */
        void SetLastAccessFrame(uint64_t frame) { m_LastAccessFrame = frame; }

    private:
        Core::Container::String m_FilePath;                        ///< ファイルパス
        Core::Container::AnsiString m_SourcePathAnsi;              ///< AssetBlob診断用パス
        Core::Identity m_PackageName;                              ///< パッケージ名
        Core::Container::TSharedPtr<const Core::Asset::AssetBlob::ByteArray> m_Storage; ///< immutable package storage
        Core::Container::VariableArray<PackageEntry> m_Entries;    ///< エントリメタデータ
        PackageFormat m_Format = PackageFormat::None;              ///< 解析形式
        PackageLoadState m_LoadState = PackageLoadState::Unloaded; ///< 読み込み状態
        uint64_t m_LastAccessFrame = 0;                            ///< 最終アクセスフレーム
        mutable Thread::Mutex m_LoadMutex;                         ///< 非同期ロード保護
        Thread::TaskPtr m_LoadTask;                                ///< 進行中のロードタスク
    };

    // スマートポインタエイリアス
    using PackagePtr = Core::Container::TSharedPtr<Package>;
    using PackageWeakPtr = Core::Container::TWeakPtr<Package>;

} // namespace NorvesLib::FileStream
