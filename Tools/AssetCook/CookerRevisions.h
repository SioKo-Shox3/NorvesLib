#pragma once
#include "Container/String.h"
#include <cstdint>

// cooker の出力のバイトが変わる変更をしたら、その種別の版を 1 上げ、
// Test/Core/Asset/CookerRevisionGolden.json の行（revision と cooked_hash）を直す。
// 1 と 2 は管理 store の記録に spec の版を入れていたころの値なので使わない
// （既存の記録を全件再 cook させるため）。
namespace NorvesLib::Tools::AssetCook::CookerRevisions
{
    inline constexpr uint64_t Texture = 3;
    inline constexpr uint64_t Model = 3;
    inline constexpr uint64_t Skeletal = 3;
    inline constexpr uint64_t Animation = 3;
    inline constexpr uint64_t Audio = 3;
    inline constexpr uint64_t Raw = 3;

    static_assert(Texture > 2 && Model > 2 && Skeletal > 2 && Animation > 2 && Audio > 2 && Raw > 2,
                  "Cooker revisions 1 and 2 are reserved for legacy spec revisions");
}

namespace NorvesLib::Tools::AssetCook
{
    // kind は asset-set の spec の種別名（skeletal は model へ写す前の名前）。
    [[nodiscard]] inline uint64_t CookerRevisionForKind(const Core::Container::AnsiString& kind)
    {
        if (kind == "texture")
        {
            return CookerRevisions::Texture;
        }
        if (kind == "model")
        {
            return CookerRevisions::Model;
        }
        if (kind == "skeletal")
        {
            return CookerRevisions::Skeletal;
        }
        if (kind == "animation")
        {
            return CookerRevisions::Animation;
        }
        if (kind == "audio")
        {
            return CookerRevisions::Audio;
        }
        if (kind == "raw")
        {
            return CookerRevisions::Raw;
        }
        return 0;
    }
}
