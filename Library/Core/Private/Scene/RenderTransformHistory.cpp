#include "Scene/RenderTransformHistory.h"
#include "Math/QuaternionUtils.h"
#include <cmath>
namespace NorvesLib::Core::Scene
{
    namespace
    {
        bool Valid(const Math::Transform& t)
        {
            const auto finite = [](const Math::Vector3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            const auto& q = t.rotation;
            const double norm = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
                                static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
            // Transformは単位回転が前提。壊れた姿勢を補間で別の姿勢へ正規化しない。
            return finite(t.position) && finite(t.scale) && std::isfinite(norm) && std::fabs(norm - 1) < 1e-3;
        }
        float Blend(float a, float b, float alpha)
        {
            return static_cast<float>(static_cast<double>(a) * (1 - static_cast<double>(alpha)) +
                                      static_cast<double>(b) * alpha);
        }
    } // namespace
    bool RenderTransformHistory::Reset(const Math::Transform& transform)
    {
        if (!Valid(transform))
            return false;
        m_Previous = m_Current = transform;
        m_bValid = true;
        return true;
    }
    bool RenderTransformHistory::Prepare(const Math::Transform& simulation)
    {
        return m_bValid && m_Current == simulation ? true : Reset(simulation);
    }
    bool RenderTransformHistory::Capture(const Math::Transform& simulation)
    {
        if (!Valid(simulation))
            return false;
        if (!m_bValid)
            return Reset(simulation);
        m_Previous = m_Current;
        m_Current = simulation;
        return true;
    }
    bool RenderTransformHistory::Evaluate(float alpha, Math::Transform& out) const
    {
        if (!m_bValid || !std::isfinite(alpha) || alpha < 0 || alpha > 1)
            return false;
        if (alpha == 0)
        {
            out = m_Previous;
            return true;
        }
        if (alpha == 1)
        {
            out = m_Current;
            return true;
        }
        Math::Transform value;
        value.position = {Blend(m_Previous.position.x, m_Current.position.x, alpha),
                          Blend(m_Previous.position.y, m_Current.position.y, alpha),
                          Blend(m_Previous.position.z, m_Current.position.z, alpha)};
        value.scale = {Blend(m_Previous.scale.x, m_Current.scale.x, alpha),
                       Blend(m_Previous.scale.y, m_Current.scale.y, alpha),
                       Blend(m_Previous.scale.z, m_Current.scale.z, alpha)};
        value.rotation = Math::QuaternionUtils::Slerp(Math::QuaternionUtils::Normalize(m_Previous.rotation),
                                                      Math::QuaternionUtils::Normalize(m_Current.rotation), alpha);
        if (!Valid(value))
            return false;
        out = value;
        return true;
    }
} // namespace NorvesLib::Core::Scene
