#include "Input/CameraPath.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Input
{

    namespace
    {
        constexpr float kRadiansToDegrees = 180.0f / 3.14159265358979323846f;

        /// 区間の両端 p0・p1 と接線 m0・m1（時刻あたりの変化量）から、区間の幅 dt と割合 u のエルミート補間をする
        Math::Vector3 Hermite(const Math::Vector3 &p0, const Math::Vector3 &m0, const Math::Vector3 &p1,
                              const Math::Vector3 &m1, float dt, float u)
        {
            const float u2 = u * u;
            const float u3 = u2 * u;
            const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            const float h10 = u3 - 2.0f * u2 + u;
            const float h01 = -2.0f * u3 + 3.0f * u2;
            const float h11 = u3 - u2;
            return p0 * h00 + m0 * (h10 * dt) + p1 * h01 + m1 * (h11 * dt);
        }

        /// 両隣の差から求めた接線。時刻の差が 0 のとき（キーが 1 つしか無い端など）は 0
        Math::Vector3 Tangent(const Math::Vector3 &prev, float prevTime, const Math::Vector3 &next, float nextTime)
        {
            const float span = nextTime - prevTime;
            if (span <= 0.0f)
            {
                return Math::Vector3(0.0f, 0.0f, 0.0f);
            }
            return (next - prev) * (1.0f / span);
        }
    } // namespace

    bool CameraPath::Set(const CameraPathKey *keys, std::size_t count, bool bLoop, float loopSeconds)
    {
        if (keys == nullptr || count < 2)
        {
            return false;
        }
        for (std::size_t index = 1; index < count; ++index)
        {
            if (!(keys[index].Time > keys[index - 1].Time))
            {
                return false;
            }
        }
        if (bLoop && (keys[0].Time != 0.0f || !(loopSeconds > keys[count - 1].Time)))
        {
            return false;
        }
        m_Keys = keys;
        m_Count = count;
        m_bLoop = bLoop;
        m_Duration = bLoop ? loopSeconds : keys[count - 1].Time;
        return true;
    }

    void CameraPath::Clear()
    {
        m_Keys = nullptr;
        m_Count = 0;
        m_bLoop = false;
        m_Duration = 0.0f;
    }

    float CameraPath::WrapTime(float time) const
    {
        if (!IsValid())
        {
            return 0.0f;
        }
        if (m_bLoop)
        {
            float wrapped = std::fmod(time, m_Duration);
            if (wrapped < 0.0f)
            {
                wrapped += m_Duration;
            }
            return wrapped;
        }
        return std::clamp(time, 0.0f, m_Duration);
    }

    void CameraPath::KeyAt(long index, float &outTime, Math::Vector3 &outPosition, Math::Vector3 &outTarget) const
    {
        const long count = static_cast<long>(m_Count);
        long wrapped = index;
        float timeOffset = 0.0f;
        if (m_bLoop)
        {
            // 添字を 0..count-1 へ巻き戻し、巻き戻した周回数ぶん時刻をずらす
            long laps = index / count;
            wrapped = index % count;
            if (wrapped < 0)
            {
                wrapped += count;
                --laps;
            }
            timeOffset = static_cast<float>(laps) * m_Duration;
        }
        else
        {
            wrapped = std::clamp(index, 0L, count - 1);
        }
        const CameraPathKey &key = m_Keys[wrapped];
        outTime = key.Time + timeOffset;
        outPosition = Math::Vector3(key.Position[0], key.Position[1], key.Position[2]);
        outTarget = Math::Vector3(key.Target[0], key.Target[1], key.Target[2]);
    }

    bool CameraPath::Evaluate(float time, Math::Vector3 &outPosition, Math::Vector3 &outTarget) const
    {
        if (!IsValid())
        {
            return false;
        }
        const float t = WrapTime(time);

        // t を含む区間 [segment, segment+1]（一周するパスの最後の区間は最初のキーの 1 周後へつなぐ）
        long segment = 0;
        const long lastIndex = static_cast<long>(m_Count) - 1;
        while (segment < lastIndex && m_Keys[segment + 1].Time <= t)
        {
            ++segment;
        }
        if (!m_bLoop && segment >= lastIndex)
        {
            // 最後のキー以降は最後のキーに留まる
            segment = lastIndex - 1;
        }

        float timePrev = 0.0f;
        float time0 = 0.0f;
        float time1 = 0.0f;
        float timeNext = 0.0f;
        Math::Vector3 posPrev, pos0, pos1, posNext;
        Math::Vector3 tgtPrev, tgt0, tgt1, tgtNext;
        KeyAt(segment - 1, timePrev, posPrev, tgtPrev);
        KeyAt(segment, time0, pos0, tgt0);
        KeyAt(segment + 1, time1, pos1, tgt1);
        KeyAt(segment + 2, timeNext, posNext, tgtNext);

        const float dt = time1 - time0;
        const float u = std::clamp((t - time0) / dt, 0.0f, 1.0f);
        outPosition = Hermite(pos0, Tangent(posPrev, timePrev, pos1, time1), pos1, Tangent(pos0, time0, posNext, timeNext),
                              dt, u);
        outTarget = Hermite(tgt0, Tangent(tgtPrev, timePrev, tgt1, time1), tgt1, Tangent(tgt0, time0, tgtNext, timeNext),
                            dt, u);
        return true;
    }

    bool CameraPath::LookAngles(const Math::Vector3 &position, const Math::Vector3 &target, float &outYawDegrees,
                                float &outPitchDegrees)
    {
        const Math::Vector3 delta = target - position;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (length < 1e-4f)
        {
            return false;
        }
        const float invLength = 1.0f / length;
        // 前方向 = (-sinYaw cosPitch, sinPitch, cosYaw cosPitch)
        outYawDegrees = std::atan2(-delta.x * invLength, delta.z * invLength) * kRadiansToDegrees;
        outPitchDegrees = std::asin(std::clamp(delta.y * invLength, -1.0f, 1.0f)) * kRadiansToDegrees;
        return true;
    }

} // namespace NorvesLib::Core::Input
