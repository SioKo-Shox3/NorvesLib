// ペイロードの準備、コミット、解放を行うための口を定義する。
#pragma once

#include "Streaming/StreamingTypes.h"
#include "Thread/Atomic.h"

namespace NorvesLib::Core::Streaming
{
    // 取り消しの印。ワーカーが読み、GameThread が立てる。
    class StreamingCancelToken
    {
    public:
        bool IsCancelled() const
        {
            return m_bCancelled.Load(std::memory_order_acquire);
        }

        void Cancel()
        {
            m_bCancelled.Store(true, std::memory_order_release);
        }

    private:
        Thread::Atomic<bool> m_bCancelled{false};
    };

    // ハンドラは所有されず、StreamingSystem より長く生きる。
    class IStreamingPayloadHandler
    {
    public:
        virtual ~IStreamingPayloadHandler() = default;
        virtual const char* GetKind() const = 0;

        // ワーカーでファイルの読み・解析・デコードを行う。GPU と World には触れない。
        virtual EStreamingPrepareResult Prepare(const StreamingPayloadContext& context, const StreamingCancelToken& cancel) = 0;

        // GameThread で 1 歩だけ進め、この歩でコミットしたバイト数を返す。
        virtual EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes) = 0;

        // GameThread で Active と Dormant を切り替える。
        virtual void SetDormant(const StreamingPayloadContext& context, bool bDormant) = 0;

        // GameThread で 1 歩だけ戻す。途中までのコミットも戻す。
        virtual EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) = 0;

        // GameThread で、取り消し・失敗・アンロード後の準備済みデータを捨てる。
        virtual void DiscardPrepared(const StreamingPayloadContext& context) = 0;

        // GameThread で、現在保持している CPU と GPU のバイト数を申告する。
        virtual StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext& context) const = 0;
    };
} // namespace NorvesLib::Core::Streaming
