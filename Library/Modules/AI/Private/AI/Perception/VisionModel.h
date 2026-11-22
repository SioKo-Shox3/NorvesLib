// 点の可視度は「草の透過率 × 地形が通れば 1、遮れば 0 の係数 × 視野の重み × 距離の重み」。
// 草の透過率は目が線分上の草の先端の最大高さ以上なら 1、それ以外は exp(−光学的厚さ × 倍率)。
// 草の枯れ度はポートの実装（GrassField）が描画と同じ式で含めるため、ここで二重に掛けない。
// 固体の遮蔽は知覚処理側で物理のレイを撃って判定する。
#pragma once

#include "AI/Ports/IGrassFieldSource.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainSystem;
}

namespace NorvesLib::Modules::AI::Perception
{
    /// 視野・距離・遮蔽の調整値。既定値はすべて仮の値。
    struct VisionTuning
    {
        float FieldOfViewDegrees = 120.0f;    ///< 視野の全角（度）。
        float FieldOfViewEdgeDegrees = 15.0f; ///< 視野の縁から内側にかけて減衰する幅（度）。
        float FadeStartMeters = 40.0f;        ///< 距離による減衰の開始位置（m）。
        float MaxDistanceMeters = 60.0f;      ///< 可視度が零になる距離（m）。
        float GrassStepMeters = 0.5f;         ///< 草の透過率を積分する刻み（m）。
        float GrassOpacityScale = 1.0f;       ///< 草の光学的厚さの倍率。
        float TerrainClearanceMeters = 0.05f; ///< 地形のレイを対象の手前で止める余裕（m）。
    };

    struct VisionQuery
    {
        Math::Vector3 Eye;     ///< 目のワールド座標（m）。
        Math::Vector3 Forward; ///< 視線の方向。長さは任意。
        Core::Container::Span<const Math::Vector3> TargetPoints; ///< 可視点のワールド座標（m）。
        const IGrassFieldSource* Grass = nullptr;
        const Core::Rendering::Terrain::TerrainSystem* Terrain = nullptr;
    };

    struct VisionPointResult
    {
        float Visibility = 0.0f;
        float GrassTransmittance = 1.0f; ///< 倍率を光学的厚さに掛けた透過率。目が草の先端の最大高さ以上なら 1。
        bool bTerrainBlocked = false;
        float AngleWeight = 0.0f;
        float DistanceWeight = 0.0f;
    };

    struct VisionResult
    {
        float Visibility = 0.0f;
        int32_t BestPointIndex = -1; ///< 正の可視度が最大の点。同点なら先頭、該当なしなら -1。
        VisionPointResult Best;
    };

    /// 目から一点への可視度を求める。地形を渡す場合は GameThread で呼ぶ。
    VisionPointResult EvaluateVisionPoint(const Math::Vector3& eye, const Math::Vector3& forward,
                                          const Math::Vector3& target, const IGrassFieldSource* grass,
                                          const Core::Rendering::Terrain::TerrainSystem* terrain, const VisionTuning& tuning);
    /// 対象の可視点を評価し、最大の可視度とその内訳を返す。
    VisionResult EvaluateVision(const VisionQuery& query, const VisionTuning& tuning);
}
