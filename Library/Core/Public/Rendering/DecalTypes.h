#pragma once

/**
 * @file DecalTypes.h
 * @brief 箱で投影するデカールの描画値と、変更しない共有の一覧
 */

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace DecalFlags
    {
        /// 草の画素にも描く。0 なら草（Material.A が 0.125〜0.375）には描かない
        inline constexpr uint32_t ReceiveFoliage = 1u;
    } // namespace DecalFlags

    /**
     * @brief デカール 1 件。4 バイトの値を欄順に詰めた 80 バイトの GPU 転送形式
     *
     * シェーダー側も各成分をこの順序で読み取る。ベクトル型固有の境界への詰め物は含まない。
     */
    struct DecalInstance
    {
        /// 箱の中心のワールド位置[m]
        float Position[3] = {0.0f, 0.0f, 0.0f};
        /// 局所からワールドへの単位四元数（x, y, z, w。無次元）
        float Rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        /// 局所 X・Y・Z の箱の半分の大きさ[m]。投影方向は局所 -Z
        float HalfExtents[3] = {0.5f, 0.5f, 0.5f};
        /// 線形の色 RGB（無次元）と不透明度 A（0〜1）
        float Color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        /// 局所法線を混ぜる不透明度（0〜1、無次元）
        float NormalOpacity = 0.0f;
        /// 箱の局所空間の法線（無次元）
        float LocalNormal[3] = {0.0f, 0.0f, 1.0f};
        /// 箱の半分の大きさに対する縁のぼかし幅の割合（0〜1、無次元）
        float EdgeFade = 0.1f;
        /// DecalFlags のビット列（無次元）
        uint32_t Flags = 0u;
    };
    static_assert(sizeof(DecalInstance) == 80, "デカールの GPU 転送形式は 80 バイト");

    /// FramePacket へ渡す、作った後は変更しないデカールの一覧
    struct DecalScene
    {
        Container::TSharedPtr<const Container::VariableArray<DecalInstance>> Decals;

        bool IsEmpty() const
        {
            return !Container::IsValid(Decals) || Decals->empty();
        }
    };
} // namespace NorvesLib::Core::Rendering
