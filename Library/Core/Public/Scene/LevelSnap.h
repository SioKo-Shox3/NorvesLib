// レベル配置対象の地面への吸着と離れの検査を提供する。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
}

namespace NorvesLib::Core::Scene
{
    class SceneQuery;
}

namespace NorvesLib::Core::Scene::LevelSnap
{
    /// 吸着し直す対象。Target は非所有の借用。
    struct LevelSnapTarget
    {
        Entity* Target = nullptr;
        float GroundOffset = 0.0f;
    };

    /// 吸着し直しの結果。
    struct LevelSnapStats
    {
        uint32_t Snapped = 0;
        uint32_t Missed = 0;
    };

    /// 離れの検査で見つかった対象。
    struct LevelSnapDeviation
    {
        Entity* Target = nullptr;
        bool bNoGround = false;       // 地面が見つからなかった。
        float ExpectedY = 0.0f;       // 地面の高さ + GroundOffset。非有限の GroundOffset は加算せず、地面が無いときは 0。
        float ActualY = 0.0f;         // 現在のワールド位置の y。
        float Distance = 0.0f;        // ActualY と ExpectedY の差の絶対値。非有限の GroundOffset は無限大。
    };

    inline constexpr float kDefaultProbeLift = 2.0f;

    /// 真上から真下へ光線を撃ち、最初に当たったトリガー以外の点と法線を返す。
    /// 光線の長さは probeLift + maxDrop。問い合わせに失敗した場合は出力を変更しない。
    /// 始点が球・箱・カプセルのコライダー内にあると、そのコライダーに距離 0 で当たり、始点を返す。
    /// Raycast は PhysicsQueryDesc::bReportStartOverlap に関係なく、この始点の重なりを報告する。
    /// maxDrop と probeLift は有限かつ非負でなければならない。
    bool SnapToGround(const SceneQuery& query, const Math::Vector3& position,
        Math::Vector3& outPosition, Math::Vector3& outNormal, float maxDrop,
        float probeLift = kDefaultProbeLift);

    /// 各対象を地面の高さ + GroundOffset へ動かし、回転と拡大を保つ。
    /// 地面が無い対象、空の Target、無効な距離、非有限の GroundOffset は動かさず Missed に数える。
    /// 吸着の後の位置から同じ呼び出しをしても位置は変わらない。ただし GroundOffset が probeLift 未満で、
    /// 地面から probeLift 以内の上にトリガー以外のほかの面が無いこと（あれば 2 回目はその面に当たる）。
    LevelSnapStats ReSnapAll(const SceneQuery& query, Container::Span<const LevelSnapTarget> targets,
        float maxDrop, float probeLift = kDefaultProbeLift);

    /// 許容範囲を超えた対象と地面の無い対象を出力する。出力は最初に空にする。
    /// maxDrop・probeLift・tolerance が非有限または負なら、出力を空にして false を返す。
    /// 非有限の GroundOffset も離れとして出力する。空の Target は飛ばし、離れた対象が無ければ true を返す。
    bool FindSnapDeviations(const SceneQuery& query, Container::Span<const LevelSnapTarget> targets,
        float maxDrop, float tolerance, Container::VariableArray<LevelSnapDeviation>& outDeviations,
        float probeLift = kDefaultProbeLift);
} // namespace NorvesLib::Core::Scene::LevelSnap
