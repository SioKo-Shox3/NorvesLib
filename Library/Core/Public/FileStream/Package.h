#pragma once

#include "Object/Object.h"
#include "Object/Reflection.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Text/IdentityPool.h"

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

        // ========================================
        // 状態取得
        // ========================================

        /**
         * @brief 読み込み状態を取得します
         * @return 読み込み状態
         */
        PackageLoadState GetLoadState() const { return m_LoadState; }

        /**
         * @brief ロード済みかどうか
         * @return ロード済みの場合true
         */
        bool IsLoaded() const { return m_LoadState == PackageLoadState::Loaded; }

        /**
         * @brief 読み込み中かどうか
         * @return 読み込み中の場合true
         */
        bool IsLoading() const { return m_LoadState == PackageLoadState::Loading; }

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
        size_t GetDataSize() const { return m_RawData.size(); }

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
        size_t GetMemorySize() const { return m_RawData.size(); }

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
        Core::Identity m_PackageName;                              ///< パッケージ名
        Core::Container::VariableArray<uint8_t> m_RawData;         ///< 生バイナリデータ
        PackageLoadState m_LoadState = PackageLoadState::Unloaded; ///< 読み込み状態
        uint64_t m_LastAccessFrame = 0;                            ///< 最終アクセスフレーム
    };

    // スマートポインタエイリアス
    using PackagePtr = Core::Container::TSharedPtr<Package>;
    using PackageWeakPtr = Core::Container::TWeakPtr<Package>;

} // namespace NorvesLib::FileStream
