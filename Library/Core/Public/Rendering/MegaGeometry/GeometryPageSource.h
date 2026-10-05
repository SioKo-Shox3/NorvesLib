#pragma once

// ジオメトリのページの読み込み元の窓口。メッシュ 1 つにつき 1 つ持ち、ページの中身（頂点とインデックス）を範囲読みする。
// 読み込みは呼び出し側のスレッドを待たせない。本番はジョブシステムの範囲読み（CookedMeshPageSource）、テストでは偽物。

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /** @brief ページ 1 つの読み込みの結果 */
    struct GeometryPageReadResult
    {
        uint32_t PageId = 0;
        bool bSucceeded = false;
        /**
         * @brief ページの中身（失敗のときは空）
         *
         * 頂点（Mesh3DVertex と同じ並びの 32 バイト × ページの頂点数）に、クラスタのインデックス（uint32 ×
         * ページのインデックス数。クラスタの頂点の範囲の先頭からの相対）が続く。隙間は無い。
         */
        VariableArray<uint8_t> Data;
    };

    /**
     * @brief ページの中身の読み込みの窓口（メッシュ 1 つに 1 つ）
     *
     * BeginRead はジョブを積むだけで待たない。ジョブは窓口より長く生きてもよい（結果の置き場は共有）。
     */
    class IGeometryPageSource
    {
    public:
        virtual ~IGeometryPageSource() = default;

        /** @brief ページの読み込みを始める。始められなければ false（ジョブを積めない・ページの番号が範囲外など） */
        virtual bool BeginRead(uint32_t pageId) = 0;

        /** @brief 完了した読み込み（失敗も含む）を out へ足す。完了したものは二度返さない */
        virtual void CollectCompleted(VariableArray<GeometryPageReadResult> &out) = 0;
    };
} // namespace NorvesLib::Core::Rendering::MegaGeometry
