#pragma once

/**
 * @file DistortionTypes.h
 * @brief 画面の後処理で背景をずらす歪みの源の描画値と、変更しない共有の一覧
 */

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 歪みの源 1 件。4 バイトの値を欄順に詰めた 32 バイトの GPU 転送形式
     *
     * シェーダー側も各成分をこの順序で読み取る。ベクトル型固有の境界への詰め物は含まない。
     */
    struct DistortionInstance
    {
        /// 円盤の中心のワールド位置[m]
        float Position[3] = {0.0f, 0.0f, 0.0f};
        /// 円盤の半径[m]
        float Radius = 0.5f;
        /// 画面の高さに対する最大のずらしの割合（無次元）
        float Strength = 0.01f;
        /// 半径あたりのノイズの格子の数（無次元）
        float PatternScale = 4.0f;
        /// 円盤の半径を 1 とした局所座標で模様が上へ流れる速さ[1/s]
        float ScrollSpeed = 0.5f;
        /// 模様の種（無次元）
        uint32_t Seed = 0u;
    };
    static_assert(sizeof(DistortionInstance) == 32, "歪みの源の GPU 転送形式は 32 バイト");

    /// FramePacket へ渡す、作った後は変更しない歪みの源の一覧
    struct DistortionScene
    {
        Container::TSharedPtr<const Container::VariableArray<DistortionInstance>> Distortions;

        bool IsEmpty() const
        {
            return !Container::IsValid(Distortions) || Distortions->empty();
        }
    };
} // namespace NorvesLib::Core::Rendering
