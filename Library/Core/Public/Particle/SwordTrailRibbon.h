#pragma once

/**
 * @file SwordTrailRibbon.h
 * @brief 描画用の剣の変換から、寿命付きの根元・切っ先の点列と帯の頂点を作る。
 */

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Math/Transform.h"
#include "Math/Vector4.h"
#include "Rendering/RibbonTrailTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Particle
{
    struct SwordTrailRibbonSettings
    {
        Math::Vector3 BaseLocal = Math::Vector3(0.0f, 0.0f, 0.0f);
        Math::Vector3 TipLocal = Math::Vector3(0.0f, 1.0f, 0.0f);
        float Lifetime = 0.25f;
        uint32_t MaxPoints = 128;
        float MaxBendDegrees = 10.0f;
        float MinMoveDistance = 1.0e-4f;
        float MinRotateDegrees = 0.05f;
        /// 線形の色。不透明度だけを点の年齢に応じて下げる。
        Math::Vector4 Color = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Rendering::RibbonTrailBlend Blend = Rendering::RibbonTrailBlend::Additive;
    };

    struct SwordTrailRibbonPoint
    {
        Math::Vector3 Base;
        Math::Vector3 Tip;
        Math::Quaternion Rotation;
        double Time;
        float Age;
    };

    class SwordTrailRibbon
    {
    public:
        /// 不正な設定なら設定と点列を保つ。受理した設定では点列を消す。
        bool Configure(const SwordTrailRibbonSettings& settings, Container::String* outError = nullptr);
        /// 点と標本の履歴を消し、次の標本から新しい帯を始める。
        void Reset();
        /// 描画用に補間した剣のワールド変換を、描画のフレームごとに 1 回渡す。
        void AddSample(const Math::Transform& bladeWorld, double time);
        /// 年齢を秒で進め、寿命に達した点を古い順に取り除く。
        void Tick(float deltaTime);

        uint32_t GetPointCount() const;
        /// 範囲内の点を古い順に読む。
        const SwordTrailRibbonPoint& GetPoint(uint32_t i) const;
        void AppendDraw(Rendering::RibbonTrailScene& scene) const;

    private:
        SwordTrailRibbonSettings m_Settings;
        Container::VariableArray<SwordTrailRibbonPoint> m_Points;
        /// 最後に点を追加した変換。微小な移動はここからの差として積み重ねる。
        Math::Transform m_PreviousTransform;
        /// 静止中の標本も含めて、時刻の逆行を拒否するための履歴。
        double m_LastSampleTime = 0.0;
        bool m_bHasSampleTime = false;
    };
} // 名前空間 NorvesLib::Core::Particle
