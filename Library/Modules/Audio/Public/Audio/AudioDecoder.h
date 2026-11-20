#pragma once

#include "Container/Span.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    /// 復号する音声の形式と長さ。
    struct AudioDecoderInfo
    {
        /// 1 秒あたりのフレーム数（Hz）。
        uint32_t SampleRate = 0;
        /// 1 フレームに含まれるチャンネル数。
        uint16_t ChannelCount = 0;
        /// 音声全体のフレーム数。
        uint64_t TotalFrames = 0;
    };

    /// 音声をフレーム単位で復号する。
    /// 同じインスタンスを複数のスレッドから同時に使わないこと。別のインスタンスは別のスレッドで同時に使ってよい。
    class IAudioDecoder
    {
    public:
        /// 復号用の資源を解放する。
        virtual ~IAudioDecoder() = default;

        /// 音声の形式と総フレーム数を返す。
        [[nodiscard]] virtual AudioDecoderInfo GetInfo() const noexcept = 0;

        /// 各チャンネルの int16 値を交互に並べ、最大 maxFrames フレームを書いて実際のフレーム数を返す。
        /// outInterleaved には maxFrames × ChannelCount 個の要素が必要。終端と失敗はともに 0 で、HasFailed で区別する。
        [[nodiscard]] virtual uint32_t DecodeFrames(int16_t* outInterleaved, uint32_t maxFrames) noexcept = 0;

        /// 次の DecodeFrames の開始位置をサンプル単位で正確に frame に合わせる。
        /// frame が TotalFrames を超える場合、またはシークに失敗した場合は false を返す。
        [[nodiscard]] virtual bool SeekToFrame(uint64_t frame) noexcept = 0;

        /// SeekToFrame(0) で音声の先頭に戻る。
        [[nodiscard]] virtual bool Rewind() noexcept = 0;

        /// 復号またはシークで失敗したかを返す。一度失敗したインスタンスは再利用できない。
        [[nodiscard]] virtual bool HasFailed() const noexcept = 0;
    };

    /// メモリ上の Ogg Vorbis を開く。開けない場合は空のポインタを返す。
    /// 開く前に全ページの CRC、単一の論理ストリームの連番、最後のページだけが EOS を持ち入力末尾で終わることを検査する。
    /// ページの破損、切り詰め、終端後の余分なバイトを検出すると空のポインタを返す。
    /// バイト列はコピーせずに借用するため、呼び出し側はデコーダを破棄するまで入力を有効に保つこと。
    [[nodiscard]] Core::Container::TUniquePtr<IAudioDecoder> CreateVorbisDecoder(Core::Container::Span<const uint8_t> oggBytes);
} // namespace NorvesLib::Modules::Audio
