#pragma once

#include "Resource/SkeletalGltfData.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    enum class ClipChannelEncoding : uint32_t
    {
        F32 = 0,
        RotSmallest3_32 = 1,
        RotSmallest3_48 = 2,
        TransQuant16 = 3,
        Const = 4
    };

    // 回転の入力は有限で、ノルムが EPSILON を超えることを呼び出し側で検証する。
    [[nodiscard]] uint32_t EncodeRotationSmallest3_32(const SkeletalValue& rotation) noexcept;
    [[nodiscard]] SkeletalValue DecodeRotationSmallest3_32(uint32_t bits) noexcept;
    [[nodiscard]] uint64_t EncodeRotationSmallest3_48(const SkeletalValue& rotation) noexcept;
    // 予約された bit45 と bit48 以上がすべて 0 かを調べる。
    [[nodiscard]] bool IsValidRotationSmallest3_48(uint64_t bits) noexcept;
    [[nodiscard]] SkeletalValue DecodeRotationSmallest3_48(uint64_t bits) noexcept;
    // 両方を正規化し、同じ回転を表す符号反転を除いた回転角の差を度で返す。
    [[nodiscard]] double RotationAngleDifferenceDegrees(const SkeletalValue& a, const SkeletalValue& b) noexcept;

    struct TranslationQuantRange
    {
        float Min[3]{};
        float Extent[3]{};
    };

    // 空の範囲では Min と Extent をすべて 0 にする。
    [[nodiscard]] TranslationQuantRange ComputeTranslationQuantRange(
        Container::Span<const SkeletalAnimationSample> samples) noexcept;
    void EncodeTranslation16(const SkeletalValue& value, const TranslationQuantRange& range, uint16_t out[3]) noexcept;
    [[nodiscard]] SkeletalValue DecodeTranslation16(const uint16_t codes[3], const TranslationQuantRange& range) noexcept;

    // 空のチャンネルは F32、それ以外は定数判定とパスごとの誤差条件で選ぶ。
    [[nodiscard]] ClipChannelEncoding ChooseClipChannelEncoding(const SkeletalAnimationChannel& channel) noexcept;
} // 名前空間 NorvesLib::Core::Skeletal
