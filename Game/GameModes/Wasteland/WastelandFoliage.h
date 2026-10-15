#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の草・低木の一角
//
// 谷床の平らな一角（視点 grass の前）に、枯れ草の株とブラックブラッシュ・モルモンティーの低木を数千株置き、
// 株の種類ごとの束（FoliageBatch）にする。株の形は種と関数だけで決まり、どの実行でも同じになる。
// 色は手本の足元の写真（Assets/Reference/ValleyOfTheGods/ground_01.jpg）の植生に合わせた線形のアルベド。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/String.h"
#include "Core/Public/Rendering/FoliageTypes.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

namespace Game::GameModes::WastelandFoliage
{

    /// 高さ場の上に、草・低木の一角の束と風を作る
    NorvesLib::Core::Rendering::FoliageScene BuildFoliage(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field);

    /// 視点の名前に応じた風の時計のずらし[s]（grass_gust は grass と同じ構図で時刻だけを変えた 2 枚目）
    float WindTimeOffsetForView(const NorvesLib::Core::Container::String &viewName);

} // namespace Game::GameModes::WastelandFoliage
