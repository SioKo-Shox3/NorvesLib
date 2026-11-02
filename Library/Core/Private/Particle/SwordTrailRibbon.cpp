/**
 * @file SwordTrailRibbon.cpp
 * @brief 剣の帯の標本の補間、寿命管理、描画用の頂点の生成。
 */

#include "Particle/SwordTrailRibbon.h"

#include "Math/QuaternionUtils.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Fail(Container::String* outError, const char* message)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }
            return false;
        }

        Math::Vector3 Lerp(const Math::Vector3& from, const Math::Vector3& to, float alpha)
        {
            return from * (1.0f - alpha) + to * alpha;
        }
    } // 無名名前空間

    bool SwordTrailRibbon::Configure(const SwordTrailRibbonSettings& settings, Container::String* outError)
    {
        if (!std::isfinite(settings.Lifetime) || settings.Lifetime <= 0.0f)
        {
            return Fail(outError, "帯の寿命には正の有限値を指定してください。");
        }
        if (settings.MaxPoints < 2)
        {
            return Fail(outError, "帯の点数の上限には 2 以上を指定してください。");
        }
        if (!std::isfinite(settings.MaxBendDegrees) || settings.MaxBendDegrees <= 0.0f || settings.MaxBendDegrees > 90.0f)
        {
            return Fail(outError, "折れ目の上限には 0 度より大きく 90 度以下の有限値を指定してください。");
        }
        if (!IsFinite(settings.BaseLocal) || !IsFinite(settings.TipLocal) || settings.BaseLocal == settings.TipLocal)
        {
            return Fail(outError, "刃の根元と切っ先には、異なる有限の座標を指定してください。");
        }
        if (!std::isfinite(settings.MinMoveDistance) || settings.MinMoveDistance < 0.0f ||
            !std::isfinite(settings.MinRotateDegrees) || settings.MinRotateDegrees < 0.0f)
        {
            return Fail(outError, "静止判定の距離と角度には、負でない有限値を指定してください。");
        }
        if (!std::isfinite(settings.Color.x) || !std::isfinite(settings.Color.y) ||
            !std::isfinite(settings.Color.z) || !std::isfinite(settings.Color.w))
        {
            return Fail(outError, "帯の色には有限値を指定してください。");
        }
        if (settings.Blend != Rendering::RibbonTrailBlend::Additive && settings.Blend != Rendering::RibbonTrailBlend::Alpha)
        {
            return Fail(outError, "帯の混合には加算またはアルファを指定してください。");
        }

        m_Settings = settings;
        Reset();
        if (outError != nullptr)
        {
            outError->clear();
        }
        return true;
    }

    void SwordTrailRibbon::Reset()
    {
        m_Points.clear();
        m_PreviousTransform = Math::Transform();
        m_LastSampleTime = 0.0;
        m_bHasSampleTime = false;
    }

    void SwordTrailRibbon::AddSample(const Math::Transform& bladeWorld, double time)
    {
        const Math::Quaternion& rotation = bladeWorld.rotation;
        if (!std::isfinite(time) || (m_bHasSampleTime && time < m_LastSampleTime) ||
            !IsFinite(bladeWorld.position) || !IsFinite(bladeWorld.scale) ||
            !std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
            !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
        {
            return;
        }
        const float rotationLength = Math::QuaternionUtils::Length(rotation);
        if (!std::isfinite(rotationLength) || rotationLength <= Math::Constants::EPSILON)
        {
            return;
        }

        Math::Transform current = bladeWorld;
        current.rotation = Math::QuaternionUtils::Normalize(rotation);
        const Math::Vector3 base = current.TransformPoint(m_Settings.BaseLocal);
        const Math::Vector3 tip = current.TransformPoint(m_Settings.TipLocal);
        if (!IsFinite(base) || !IsFinite(tip))
        {
            return;
        }
        if (m_Points.empty())
        {
            m_Points.push_back({base, tip, current.rotation, time, 0.0f});
            m_PreviousTransform = current;
            m_LastSampleTime = time;
            m_bHasSampleTime = true;
            return;
        }

        const float angleDegrees = Math::QuaternionUtils::Angle(m_PreviousTransform.rotation, current.rotation) *
                                   (180.0f / Math::Constants::PI);
        const Math::Vector3 movement = current.position - m_PreviousTransform.position;
        if (movement.Length() < m_Settings.MinMoveDistance && angleDegrees < m_Settings.MinRotateDegrees)
        {
            // 点の時刻と年齢は変えず、時刻の逆行を検出する履歴だけを進める。
            m_LastSampleTime = time;
            return;
        }

        // 非常に小さい折れ目の設定でも、直後に上限で消える補間点は作らない。
        const double divisions = std::max(1.0, std::ceil(static_cast<double>(angleDegrees) / m_Settings.MaxBendDegrees));
        const uint32_t addedCount = static_cast<uint32_t>(std::min(divisions, static_cast<double>(m_Settings.MaxPoints)));
        const SwordTrailRibbonPoint previous = m_Points.back();
        Container::VariableArray<SwordTrailRibbonPoint> addedPoints;
        addedPoints.reserve(addedCount);
        for (uint32_t i = 0; i < addedCount; ++i)
        {
            const double alpha = 1.0 - static_cast<double>(addedCount - 1 - i) / divisions;
            const float blend = static_cast<float>(alpha);
            Math::Transform interpolated = current;
            if (i + 1 < addedCount)
            {
                interpolated.position = Lerp(m_PreviousTransform.position, current.position, blend);
                interpolated.scale = Lerp(m_PreviousTransform.scale, current.scale, blend);
                interpolated.rotation = Math::QuaternionUtils::Slerp(m_PreviousTransform.rotation, current.rotation, blend);
            }
            SwordTrailRibbonPoint point;
            point.Base = interpolated.TransformPoint(m_Settings.BaseLocal);
            point.Tip = interpolated.TransformPoint(m_Settings.TipLocal);
            point.Rotation = interpolated.rotation;
            point.Time = i + 1 == addedCount ? time : previous.Time * (1.0 - alpha) + time * alpha;
            // 補間点も前の点から最新の点までの年齢に置き、古い補間点だけ若返らせない。
            point.Age = previous.Age * (1.0f - blend);
            if (!IsFinite(point.Base) || !IsFinite(point.Tip) || !std::isfinite(point.Time))
            {
                return;
            }
            addedPoints.push_back(point);
        }

        const size_t retainedCount = std::min(m_Points.size(), static_cast<size_t>(m_Settings.MaxPoints - addedCount));
        m_Points.erase(m_Points.begin(), m_Points.end() - retainedCount);
        m_Points.insert(m_Points.end(), addedPoints.begin(), addedPoints.end());
        m_PreviousTransform = current;
        m_LastSampleTime = time;
        m_bHasSampleTime = true;
    }

    void SwordTrailRibbon::Tick(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
        {
            return;
        }
        for (SwordTrailRibbonPoint& point : m_Points)
        {
            point.Age += deltaTime;
        }
        size_t expiredCount = 0;
        while (expiredCount < m_Points.size() && m_Points[expiredCount].Age >= m_Settings.Lifetime)
        {
            ++expiredCount;
        }
        m_Points.erase(m_Points.begin(), m_Points.begin() + expiredCount);
    }

    uint32_t SwordTrailRibbon::GetPointCount() const
    {
        return static_cast<uint32_t>(m_Points.size());
    }

    const SwordTrailRibbonPoint& SwordTrailRibbon::GetPoint(uint32_t i) const
    {
        return m_Points[i];
    }

    void SwordTrailRibbon::AppendDraw(Rendering::RibbonTrailScene& scene) const
    {
        if (m_Points.size() < 2)
        {
            return;
        }
        const uint32_t firstVertex = static_cast<uint32_t>(scene.Vertices.size());
        for (const SwordTrailRibbonPoint& point : m_Points)
        {
            const float u = std::clamp(point.Age / m_Settings.Lifetime, 0.0f, 1.0f);
            const float alpha = m_Settings.Color.w * (1.0f - u);
            scene.Vertices.push_back({{point.Base.x, point.Base.y, point.Base.z}, {u, 0.0f},
                                      {m_Settings.Color.x, m_Settings.Color.y, m_Settings.Color.z, alpha}});
            scene.Vertices.push_back({{point.Tip.x, point.Tip.y, point.Tip.z}, {u, 1.0f},
                                      {m_Settings.Color.x, m_Settings.Color.y, m_Settings.Color.z, alpha}});
        }
        scene.Draws.push_back({firstVertex, GetPointCount() * 2, m_Settings.Blend});
    }
} // 名前空間 NorvesLib::Core::Particle
