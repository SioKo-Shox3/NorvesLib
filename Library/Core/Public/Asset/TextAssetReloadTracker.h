#pragma once
#include "Asset/AssetFileReader.h"
namespace NorvesLib::Core::Asset
{
    enum class TextAssetPollResult : uint8_t
    {
        NotDue,
        Unchanged,
        Changed,
        ReadFailed,
        InvalidArgument
    };
    // ゲーム/カメラ設定で共有する主スレッド用の内容検出。解析と採用は呼出側が行う。
    // Changedは内容を観測した意味で、設定値が有効という意味ではない。
    class TextAssetReloadTracker final
    {
      public:
        TextAssetReloadTracker() = default;
        explicit TextAssetReloadTracker(const Container::AnsiString& assetRoot) : m_Reader(assetRoot) {}
        bool SetPath(Container::AnsiStringView path);
        const Container::AnsiString& GetPath() const
        {
            return m_Path;
        }
        void Reset();
        // 最初は即時、以後0.25秒ごと。実時間を渡す。Changed以外ではoutを保持する。
        // 解析失敗でも同じ内容を再通知しない。内容変更かResetで再通知できる。
        TextAssetPollResult Poll(double elapsedSeconds, AssetBlob& out);
        AssetReadStatus GetLastReadStatus() const
        {
            return m_LastReadStatus;
        }
        static constexpr uint64_t MaximumBytes = 64 * 1024;

      private:
        AssetFileReader m_Reader;
        Container::AnsiString m_Path;
        uint64_t m_LastHash = 0;
        size_t m_LastSize = 0;
        double m_Elapsed = 0;
        bool m_bFirst = true, m_bObserved = false;
        AssetReadStatus m_LastReadStatus = AssetReadStatus::InvalidRequest;
    };
} // namespace NorvesLib::Core::Asset
