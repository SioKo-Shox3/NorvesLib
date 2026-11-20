#pragma once

#include "Asset/AssetBlob.h"
#include "Container/Span.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedAudioFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'A', 'U', 'D', 'v', '0', '\0'};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr uint32_t EntryType =
            static_cast<uint32_t>('A') |
            (static_cast<uint32_t>('u') << 8) |
            (static_cast<uint32_t>('d') << 16) |
            (static_cast<uint32_t>('0') << 24);
        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr size_t HeaderSize = 68;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t VersionMajor = 12;
            inline constexpr size_t VersionMinor = 14;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t Reserved0 = 20;
            inline constexpr size_t FileSize = 24;
            inline constexpr size_t PayloadOffset = 32;
            inline constexpr size_t PayloadSize = 40;
            inline constexpr size_t PayloadHash = 48;
            inline constexpr size_t SampleRate = 56;
            inline constexpr size_t ChannelCount = 60;
            inline constexpr size_t BitsPerSample = 62;
            inline constexpr size_t BlockAlignment = 64;
            inline constexpr size_t Reserved1 = 66;
        }
    } // namespace CookedAudioFormatV0

    /// NVAUD v1 のヘッダに使用する定数。
    namespace CookedAudioFormatV1
    {
        /// ファイル形式を識別するマジック。
        inline constexpr uint8_t Magic[] = {'N', 'V', 'A', 'U', 'D', 'v', '1', '\0'};
        /// マジックのバイト数。
        inline constexpr size_t MagicSize = sizeof(Magic);
        /// 形式のメジャーバージョン。
        inline constexpr uint16_t VersionMajor = 1;
        /// 形式のマイナーバージョン。
        inline constexpr uint16_t VersionMinor = 0;
        /// エンディアンを識別するマーカー。
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        /// パッケージ内のエントリ種別。
        inline constexpr uint32_t EntryType =
            static_cast<uint32_t>('A') |
            (static_cast<uint32_t>('u') << 8) |
            (static_cast<uint32_t>('d') << 16) |
            (static_cast<uint32_t>('1') << 24);
        /// ヘッダのバイト数。
        inline constexpr size_t HeaderSize = 104;
        /// ループ範囲の有無を表すフラグ。
        inline constexpr uint16_t FlagHasLoop = 0x0001u;
        /// 読み込み可能なフラグのマスク。
        inline constexpr uint16_t KnownFlagsMask = FlagHasLoop;
        /// PCM16 のマニフェスト形式名。
        inline constexpr char ManifestFormatPcm16[] = "nvaud.v1.pcm16";
        /// Vorbis のマニフェスト形式名。
        inline constexpr char ManifestFormatVorbis[] = "nvaud.v1.vorbis";

        /// リトルエンディアンのヘッダ各欄のバイト位置。
        namespace HeaderOffset
        {
            /// マジックの位置。
            inline constexpr size_t Magic = 0;
            /// ヘッダサイズの位置。
            inline constexpr size_t HeaderSize = 8;
            /// メジャーバージョンの位置。
            inline constexpr size_t VersionMajor = 12;
            /// マイナーバージョンの位置。
            inline constexpr size_t VersionMinor = 14;
            /// エンディアンマーカーの位置。
            inline constexpr size_t EndianMarker = 16;
            /// コーデックの位置。
            inline constexpr size_t Codec = 20;
            /// フラグの位置。
            inline constexpr size_t Flags = 22;
            /// ファイルサイズの位置。
            inline constexpr size_t FileSize = 24;
            /// ペイロード開始位置の欄の位置。
            inline constexpr size_t PayloadOffset = 32;
            /// ペイロードサイズの位置。
            inline constexpr size_t PayloadSize = 40;
            /// ペイロードハッシュの位置。
            inline constexpr size_t PayloadHash = 48;
            /// 総フレーム数の位置。
            inline constexpr size_t TotalFrames = 56;
            /// ループ開始フレームの位置。
            inline constexpr size_t LoopStartFrame = 64;
            /// ループ終了フレームの位置。
            inline constexpr size_t LoopEndFrame = 72;
            /// サンプルレートの位置。
            inline constexpr size_t SampleRate = 80;
            /// チャンネル数の位置。
            inline constexpr size_t ChannelCount = 84;
            /// 量子化ビット数の位置。
            inline constexpr size_t BitsPerSample = 86;
            /// ブロック単位のバイト数の位置。
            inline constexpr size_t BlockAlignment = 88;
            /// カテゴリの位置。
            inline constexpr size_t Category = 90;
            /// エンコーダ遅延の位置。
            inline constexpr size_t EncoderDelay = 92;
            /// ゲインの位置。
            inline constexpr size_t GainDb = 96;
            /// 予約欄の位置。
            inline constexpr size_t Reserved = 100;
        }
        static_assert(HeaderOffset::Reserved + sizeof(uint32_t) == HeaderSize);
    } // namespace CookedAudioFormatV1

    /// 音声ペイロードの符号化方式。
    enum class CookedAudioCodec : uint16_t
    {
        /// 16 ビット PCM。
        Pcm16 = 0,
        /// Vorbis 圧縮音声。
        Vorbis = 1,
        /// 将来用の Opus 予約値。
        Opus = 2,
    };

    /// 音声の用途を表すカテゴリ。
    enum class CookedAudioCategory : uint16_t
    {
        /// カテゴリ未指定。
        Unspecified = 0,
        /// 効果音。
        Sfx = 1,
        /// 環境音。
        Ambience = 2,
        /// 足音。
        Footstep = 3,
        /// 音楽。
        Music = 4,
        /// UI 音。
        Ui = 5,
    };

    enum class CookedAudioParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        EmptyBlob,
        HeaderTooSmall,
        BadMagic,
        UnsupportedVersion,
        EndianMismatch,
        HeaderSizeMismatch,
        FileSizeMismatch,
        ReservedFieldNonZero,
        UnsupportedSampleRate,
        UnsupportedChannelCount,
        UnsupportedBitsPerSample,
        BlockAlignmentMismatch,
        InvalidPayloadSize,
        PayloadBlockMisaligned,
        PayloadOutOfRange,
        PayloadHashMismatch,
        /// 未対応のコーデック。
        UnsupportedCodec,
        /// 未対応のカテゴリ。
        UnsupportedCategory,
        /// 有限値でないゲイン。
        InvalidGain,
        /// 不正なループ範囲。
        LoopRangeInvalid,
        /// 不正な総フレーム数または PCM のエンコーダ遅延。
        FrameCountMismatch,
    };

    struct CookedAudioData
    {
        AssetBlob SourceBlob;
        uint32_t SampleRate = 0;
        uint16_t ChannelCount = 0;
        uint16_t BitsPerSample = 0;
        uint16_t BlockAlignment = 0;
        uint64_t FrameCount = 0;
        uint64_t PayloadHash = 0;
        size_t PayloadOffset = 0;
        size_t PayloadSize = 0;
        /// 読み取った形式のメジャーバージョン。
        uint16_t FormatVersion = 0;
        /// ペイロードの符号化方式。
        CookedAudioCodec Codec = CookedAudioCodec::Pcm16;
        /// 音声の総フレーム数。
        uint64_t TotalFrames = 0;
        /// ループ範囲を持つかどうか。
        bool HasLoop = false;
        /// ループ開始位置のフレーム番号。
        uint64_t LoopStartFrame = 0;
        /// ループ終端位置のフレーム番号（終端は含まない）。
        uint64_t LoopEndFrame = 0;
        /// エンコーダ遅延のフレーム数。
        uint32_t EncoderDelay = 0;
        /// 適用するゲイン（dB）。
        float GainDb = 0.0f;
        /// 音声の用途を表すカテゴリ。
        CookedAudioCategory Category = CookedAudioCategory::Unspecified;

        /// PCM16 のペイロードを返し、それ以外の方式では空を返す。
        [[nodiscard]] Container::Span<const uint8_t> GetPcmBytes() const noexcept;
        /// 符号化方式にかかわらずペイロードを返す。
        [[nodiscard]] Container::Span<const uint8_t> GetEncodedBytes() const noexcept;
    };

    struct CookedAudioParseResult
    {
        CookedAudioParseStatus Status = CookedAudioParseStatus::InvalidBlob;
        CookedAudioData Audio;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CookedAudioParseStatus::Success;
        }
    };

    [[nodiscard]] constexpr uint64_t ComputeCookedAudioPayloadHash(const uint8_t* data, size_t size) noexcept
    {
        uint64_t hash = CookedAudioFormatV0::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= CookedAudioFormatV0::Fnv1a64Prime;
        }
        return hash;
    }

    /// NVAUD v0 だけを検査して読み取る。
    [[nodiscard]] CookedAudioParseResult ParseCookedAudioV0(AssetBlob sourceBlob);
    /// NVAUD v1 だけを検査して読み取る。
    [[nodiscard]] CookedAudioParseResult ParseCookedAudioV1(AssetBlob sourceBlob);
    /// マジックに応じて NVAUD v0 または v1 を読み取る。
    [[nodiscard]] CookedAudioParseResult ParseCookedAudio(AssetBlob sourceBlob);
} // namespace NorvesLib::Core::Asset
