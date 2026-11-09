#pragma once

/**
 * @file RenderExtensionData.h
 * @brief GameThread で作った拡張データを不変のまま RenderThread に渡す。
 *
 * GPU 資源をブロブに入れない。GameThread は GPU に触れない。
 * MB 級の配列を毎フレーム Set しない。同じ値は memcmp で再利用するが、比較の費用は掛かる。
 * パスは Revision を比較し、必要なときだけ GPU へアップロードする。
 * 1 つの View に複数の Viewport があるとパスは Viewport ごとに Declare・Execute される。
 * パス内の履歴は Viewport の id で分ける。
 * 拡張データの変化は SceneRevision に含めず、RTGI・DDGI の履歴をリセットしない。
 */

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Text/IdentityPool.h"
#include "Thread/Atomic.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 拡張データの名前と値の型を対応付ける。
     *
     * キーは inline constexpr RenderExtensionDataKey<MyParams> kMyKey{
     * Identity::Literal("MyKey", sizeof("MyKey") - 1)} の形で定数にする。
     */
    template <typename T>
    struct RenderExtensionDataKey
    {
        static_assert(std::is_trivially_copyable_v<T>, "拡張データにはトリビアルにコピー可能な型を使ってください。");

        Identity Name;
    };

    /** @brief 型名、サイズ、アラインメントからレイアウトの FNV-1a ハッシュを求める。 */
    template <typename T>
    constexpr uint64_t ComputeRenderExtensionLayoutHash()
    {
        constexpr uint64_t FnvOffset = 14695981039346656037ULL;
        constexpr uint64_t FnvPrime = 1099511628211ULL;
        uint64_t hash = FnvOffset;

        const uint64_t layout[] = {static_cast<uint64_t>(sizeof(T)), static_cast<uint64_t>(alignof(T))};
        for (uint64_t value : layout)
        {
            for (size_t i = 0; i < sizeof(uint64_t); ++i)
            {
                hash ^= (value >> (i * 8)) & 0xffULL;
                hash *= FnvPrime;
            }
        }

#ifdef _MSC_VER
        const char* typeName = __FUNCSIG__;
#else
        const char* typeName = __PRETTY_FUNCTION__;
#endif
        for (const char* c = typeName; *c != '\0'; ++c)
        {
            hash ^= static_cast<unsigned char>(*c);
            hash *= FnvPrime;
        }
        return hash;
    }

    /**
     * @brief 作成後は内容が変わらない拡張データ。
     *
     * データの先頭は 16 バイト以上に整列する。16 バイトを超えるアラインメントを
     * 要求する型は、取得時にアドレスがその要件を満たす場合だけ参照できる。
     */
    struct RenderExtensionBlob
    {
        /** @brief data はサイズが 0 でない場合、全バイトを読み取れる有効なアドレスとする。 */
        RenderExtensionBlob(Identity name, uint64_t layoutHash, uint32_t elementSize, uint32_t count,
                            uint64_t revision, const void* data);

        const Identity Name;
        const uint64_t LayoutHash;
        const uint32_t ElementSize;
        const uint32_t Count;
        const uint64_t Revision;

        /** @brief 保持するバイト列の先頭。空の場合は nullptr。 */
        const void* GetData() const;

        /** @brief 保持する有効なバイト数。 */
        size_t GetSizeBytes() const;

    private:
        struct alignas(16) AlignedBlock
        {
            uint8_t Bytes[16];
        };

        Container::VariableArray<AlignedBlock> m_Data;
        mutable Thread::Atomic<bool> m_bLayoutMismatchLogged{false};

        friend const RenderExtensionBlob* FindRenderExtensionBlob(
            const Container::VariableArray<Container::TSharedPtr<const RenderExtensionBlob>>& set,
            Identity name, uint64_t layoutHash);
    };

    /** @brief パケットのシーンが共有する不変ブロブの表。 */
    using RenderExtensionDataSet = Container::VariableArray<Container::TSharedPtr<const RenderExtensionBlob>>;

    /**
     * @brief GameThread 専用の拡張データ保持。ロックは行わない。
     *
     * RenderWorld::SetFoliageSource と同じく GameThread から操作し、
     * SnapshotTo で共有したブロブを RenderThread が読み取る。
     */
    class RenderExtensionDataStore
    {
    public:
        /**
         * @brief 名前に対応する値を更新する。同じレイアウトとバイト列ならブロブを再利用する。
         * @param data バイト数が 0 でない場合は読み取り可能な先頭アドレス。空配列なら nullptr でもよい。
         */
        void SetBytes(Identity name, uint64_t layoutHash, uint32_t elementSize, uint32_t count, const void* data);

        /** @brief 名前に対応する値を取り除く。 */
        void Remove(Identity name);

        /** @brief 診断用の借用参照。更新・削除後の寿命は呼び出し側で保証する。 */
        const RenderExtensionBlob* Find(Identity name) const;

        /** @brief 保持中の名前の数を返す。 */
        uint32_t GetCount() const;

        /** @brief 出力を空にし、ブロブをコピーせず共有ポインタだけを写す。 */
        void SnapshotTo(RenderExtensionDataSet& out) const;

    private:
        RenderExtensionDataSet m_Data;
        uint64_t m_NextRevision = 1;
    };

    /** @brief 名前とレイアウトが一致するブロブを検索する。レイアウト不一致はブロブごとに一度だけ警告する。 */
    const RenderExtensionBlob* FindRenderExtensionBlob(
        const RenderExtensionDataSet& set, Identity name, uint64_t layoutHash);

    /**
     * @brief 型と名前が一致し、要素数が 1 のときだけ値を借用する。
     * @note 返された参照は、そのブロブの共有参照を保持している間だけ有効。
     */
    template <typename T>
    const T* FindRenderExtensionValue(const RenderExtensionDataSet& set, const RenderExtensionDataKey<T>& key)
    {
        const RenderExtensionBlob* blob = FindRenderExtensionBlob(set, key.Name, ComputeRenderExtensionLayoutHash<T>());
        if (blob == nullptr || blob->Count != 1 || blob->ElementSize != sizeof(T) ||
            blob->GetData() == nullptr || reinterpret_cast<uintptr_t>(blob->GetData()) % alignof(T) != 0)
        {
            return nullptr;
        }
        return static_cast<const T*>(blob->GetData());
    }

    /**
     * @brief 型と名前が一致する配列を借用する。該当しない場合は空の Span を返す。
     * @note 返された参照は、そのブロブの共有参照を保持している間だけ有効。
     */
    template <typename T>
    Container::Span<const T> FindRenderExtensionArray(
        const RenderExtensionDataSet& set, const RenderExtensionDataKey<T>& key)
    {
        const RenderExtensionBlob* blob = FindRenderExtensionBlob(set, key.Name, ComputeRenderExtensionLayoutHash<T>());
        if (blob == nullptr || blob->Count == 0 || blob->ElementSize != sizeof(T) ||
            blob->GetData() == nullptr || reinterpret_cast<uintptr_t>(blob->GetData()) % alignof(T) != 0)
        {
            return {};
        }
        return Container::Span<const T>(static_cast<const T*>(blob->GetData()), blob->Count);
    }

    /** @brief 拡張パスが参照するゲームと描画済みパケットの時間。 */
    struct RenderExtensionFrameInfo
    {
        /** @brief パケットのゲームのフレーム番号。 */
        uint64_t FrameNumber = 0;

        /** @brief パケットに記録したゲームの累積時間。 */
        double GameTotalTime = 0.0;

        /** @brief ゲームのフレーム時間。RenderThread のフレーム時間である ViewRenderContext::DeltaTime とは別。 */
        float GameDeltaTime = 0.0f;

        /**
         * @brief RenderThread が最後に描いたパケットの GameTotalTime。
         *
         * ゲームのフレームを飛ばして描画した場合も物体の前の位置の付け替えと同じ基準。
         * 初回は GameTotalTime - GameDeltaTime。
         */
        double PreviousRenderedTotalTime = 0.0;
    };
} // namespace NorvesLib::Core::Rendering
