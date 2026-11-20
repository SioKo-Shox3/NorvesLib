// 組版に必要な字形と行の寸法をピクセル単位で提供する。
#pragma once

#include "Math/Vector2.h"
#include <cstdint>

namespace NorvesLib::Core
{
    struct TextGlyphMetrics
    {
        float AdvancePx = 0.0f;
        Math::Vector2 SizePx;
        /// 基準線上のペンからビットマップ左上への距離。右と上が正。
        Math::Vector2 BearingPx;
        bool bValid = false;
    };

    class ITextMetrics
    {
    public:
        virtual ~ITextMetrics() = default;

        /// 指定の文字が無いときは実装が選んだ代替字形の寸法を返す。
        virtual TextGlyphMetrics GetGlyphMetrics(uint32_t codepoint) const = 0;
        /// 基準線から上端までの距離。フォールバックを含む全フェイスの最大値を返す。
        virtual float GetAscenderPx() const = 0;
        /// 基準線から下端までの正の距離。フォールバックを含む全フェイスの最大値を返す。
        virtual float GetDescenderPx() const = 0;
        /// 隣り合う行の基準線間の距離。
        virtual float GetLineAdvancePx() const = 0;
        /// 字形の集合が変わるたびに更新する、プロセス全体で一意な世代番号。キャッシュの鍵に使う。
        virtual uint64_t GetGeneration() const = 0;
    };
}
