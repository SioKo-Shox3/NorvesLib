// 資産プレビューの起動引数と、床に並べる位置をエンジンの状態に依存せずに求める。
#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Math/GeometryTypes.h"
#include "Core/Public/Math/Vector3.h"

#include <cstddef>

namespace Game::GameModes
{
    /// 資産プレビューの起動引数。パスは種類ごとに指定順を保つ。
    struct AssetPreviewArguments
    {
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> ModelPaths;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> SkeletalPaths;
        NorvesLib::Core::Container::String ClipName; // 空なら bind の姿勢。
        float TimeSeconds = -1.0f; // 負なら時刻指定なし。

        /// いずれかの引数が指定されているかを返す。
        bool HasAny() const noexcept;
    };

    inline constexpr float kAssetPreviewMaxTimeSeconds = 100000.0f;

    /// 資産プレビュー用の接頭辞を持つ引数かを返す。
    bool IsAssetPreviewOption(const NorvesLib::Core::Container::String& argument);

    /// 起動引数を読み、失敗時は結果を初期化して該当する引数と理由を返す。
    bool ParseAssetPreviewArguments(
        const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& args,
        AssetPreviewArguments& out, NorvesLib::Core::Container::String& outError);

    inline constexpr float kAssetPreviewGapMeters = 0.5f; // 資産間の空き[m]。
    inline constexpr float kAssetPreviewMinFootprintMeters = 0.1f; // X 方向の最小幅[m]。

    /// 全成分が有限で、各軸の最小値が最大値以下かを返す。
    bool IsValidAssetPreviewBounds(const NorvesLib::Math::AABB& bounds);

    /// ローカル境界から、床上で X 軸に並べる各資産の原点を求める。
    /// 引数が不正、または位置を float で表せない場合は結果を空にして false を返す。
    bool ComputeAssetPreviewLayout(
        const NorvesLib::Math::AABB* bounds, size_t count, float gapMeters,
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3>& outPositions);
} // namespace Game::GameModes
