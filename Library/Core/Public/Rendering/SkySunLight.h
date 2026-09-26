#pragma once

// 空が有効なときに空の太陽を表す方向光。ラスタとPTが同じ太陽を一度だけ数えるための約束を置く。

#include "Container/Containers.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SkyAtmosphere.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // 空の太陽の方向光に予約したLightId。Componentの通し番号（1から増える）とは重ならない。
    inline constexpr uint64_t SkySunLightId = 0xFFFFFFFFFFFFFFFFull;

    inline bool IsSkySunLight(const LightProxy& light)
    {
        return light.LightId == SkySunLightId;
    }

    /**
     * @brief 空が有効なとき、空の太陽を表す方向光を作る
     *
     * 方向は太陽方向の逆（光の進行方向）、色は地表の透過率、強度は地表照度
     * （ComputeSunGroundIlluminance）の輝度（lux）で、影を落とす。ラスタはこの灯を
     * 他の方向光と同じく照らし、CSMとRT影はこの灯を優先して影を掛ける。PTは同じ太陽を
     * 空の円盤の光源標本で数えるため、点・spot・方向光の評価からこの灯を除く。
     * @return 空が無効、または地表照度が0のときfalse
     */
    bool MakeSkySunLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight);

    /**
     * @brief 光源表の空の太陽を差し替える
     *
     * 既存の空の太陽をすべて除き、空が有効なら末尾に1つ加える。再利用する
     * FramePacketへ毎フレーム呼んでも重複しない。
     */
    void ReplaceSkySunLight(const SkyAtmosphereParameters& parameters,
                            Container::VariableArray<LightProxy>& lights);

} // namespace NorvesLib::Core::Rendering
