#pragma once

/**
 * @file RibbonTrailTypes.h
 * @brief 剣の軌跡の帯を描画へ渡す頂点と描画範囲。
 *
 * 各点の根元・切っ先を交互に並べ、帯ごとに独立した三角形ストリップとして描く。
 */

#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// ワールド座標の位置、年齢と刃に沿う座標、線形の色を持つ頂点。
    struct RibbonTrailVertex
    {
        float Position[3];
        float UV[2];
        float Color[4];
    };

    static_assert(sizeof(RibbonTrailVertex) == 36);
    static_assert(offsetof(RibbonTrailVertex, Position) == 0);
    static_assert(offsetof(RibbonTrailVertex, UV) == sizeof(float) * 3);
    static_assert(offsetof(RibbonTrailVertex, Color) == sizeof(float) * 5);

    enum class RibbonTrailBlend : uint8_t
    {
        Additive,
        Alpha
    };

    /// 1 点につき根元・切っ先の 2 頂点を並べた三角形ストリップの範囲。
    struct RibbonTrailDraw
    {
        uint32_t FirstVertex;
        uint32_t VertexCount;
        RibbonTrailBlend Blend;
    };

    /// フレームへ渡す帯の頂点と、帯ごとの描画範囲。
    struct RibbonTrailScene
    {
        Container::VariableArray<RibbonTrailVertex> Vertices;
        Container::VariableArray<RibbonTrailDraw> Draws;

        void Clear()
        {
            Vertices.clear();
            Draws.clear();
        }

        bool IsEmpty() const
        {
            return Draws.empty();
        }
    };
} // 名前空間 NorvesLib::Core::Rendering
