#pragma once

// 空が有効なときに太陽または月を表す方向光。ラスタとPTが同じ太陽を一度だけ数えるための約束を置く。

#include "Container/Containers.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SkyAtmosphere.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // 空の太陽の方向光に予約したLightId。Componentの通し番号（1から増える）とは重ならない。
    inline constexpr uint64_t SkySunLightId = 0xFFFFFFFFFFFFFFFFull;
    inline constexpr uint64_t SkyMoonLightId = SkySunLightId - 1u;

    inline bool IsSkySunLight(const LightProxy& light)
    {
        return light.LightId == SkySunLightId;
    }

    inline bool IsSkyMoonLight(const LightProxy& light)
    {
        return light.LightId == SkyMoonLightId;
    }

    /**
     * @brief 空が有効なとき、空の太陽を表す方向光を作る
     *
     * 方向は太陽方向の逆（光の進行方向）、色は地表の透過率、強度は地表照度
     * （ComputeSunGroundIlluminance）の輝度（lux）で、影を落とす。ラスタはこの灯を
     * 他の方向光と同じく照らし、CSMとRT影はこの灯を優先して影を掛ける。PTは同じ太陽を
     * 空の円盤の光源標本で数えるため、点・spot・方向光の評価からこの灯を除く。
     * 地表照度には雲の直達の係数（ComputeCloudDirectFactor）を掛ける。係数には下限
     * （CloudDirectFactorFloor）があるので、曇り度 c=1 でも光源表から消えず、
     * 光源の出入りによって LightRevision が跳ばない。
     * @return 空が無効、太陽高度が負、または地表照度が0のときfalse
     */
    bool MakeSkySunLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight);

    /**
     * @brief 空の月明かりを表す方向光を作る
     *
     * 方向は月の方向の逆、色は輝度1へ正規化した月の色、強度は月の照度（lux）。
     * @return 空が無効、照度が正の有限値でない、または色の輝度が0以下のときfalse
     */
    bool MakeSkyMoonLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight);

    /** @brief 空の太陽を優先し、作れなければ月明かりの方向光を作る */
    bool MakeSkyDirectionalLightProxy(const SkyAtmosphereParameters& parameters, LightProxy& outLight);

    /**
     * @brief 光源表の空の方向光を差し替える
     *
     * 既存の空の太陽と月をすべて除き、作れた方向光を末尾に1つ加える。再利用する
     * FramePacketへ毎フレーム呼んでも重複しない。
     */
    void ReplaceSkySunLight(const SkyAtmosphereParameters& parameters,
                            Container::VariableArray<LightProxy>& lights);

} // namespace NorvesLib::Core::Rendering
