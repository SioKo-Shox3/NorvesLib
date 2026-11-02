// 当たりの表を検証し、表面に応じたエフェクトの放出要求を CPU 上で蓄積する。
#include "Particle/HitEffectEmitter.h"

#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        /** @brief ベクトルの全成分が有限値か判定する。 */
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        /** @brief 検証失敗の理由を出力し、失敗を返す。 */
        bool Fail(Container::String* outError, const char* message)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }
            return false;
        }
    }

    bool HitEffectEmitter::SetTable(const HitEffectTable& table, Container::String* outError)
    {
        if (table.Effects.empty())
        {
            return Fail(outError, "エフェクトは一つ以上必要です。");
        }
        if (table.DefaultEffectIndex >= table.Effects.size())
        {
            return Fail(outError, "既定のエフェクトの添字が範囲外です。");
        }
        for (size_t index = 0u; index < table.Entries.size(); ++index)
        {
            const HitEffectTableEntry& entry = table.Entries[index];
            if (entry.EffectIndex >= table.Effects.size())
            {
                return Fail(outError, "表面に対応するエフェクトの添字が範囲外です。");
            }
            if (entry.SurfaceName.empty())
            {
                return Fail(outError, "表面の名前を空にはできません。");
            }
            for (size_t previousIndex = 0u; previousIndex < index; ++previousIndex)
            {
                if (entry.SurfaceName == table.Entries[previousIndex].SurfaceName)
                {
                    return Fail(outError, "表面の名前が重複しています。");
                }
            }
        }

        m_Table = table;
        m_bHasTable = true;
        return true;
    }

    const HitEffectTable& HitEffectEmitter::GetTable() const
    {
        return m_Table;
    }

    uint32_t HitEffectEmitter::SelectEffect(const char* surfaceName) const
    {
        if (!m_bHasTable)
        {
            return InvalidEffectIndex;
        }
        if (surfaceName != nullptr && surfaceName[0] != '\0')
        {
            for (const HitEffectTableEntry& entry : m_Table.Entries)
            {
                if (entry.SurfaceName == surfaceName)
                {
                    return entry.EffectIndex;
                }
            }
        }
        return m_Table.DefaultEffectIndex;
    }

    bool HitEffectEmitter::SubmitHit(const HitEffectHit& hit)
    {
        if (!m_bHasTable || !IsFinite(hit.Position) || !IsFinite(hit.Normal))
        {
            return false;
        }
        const float normalLengthSquared = hit.Normal.LengthSquared();
        if (normalLengthSquared <= 1e-12f)
        {
            return false;
        }
        if (m_PendingRequests.size() >= DefaultMaxPendingRequests)
        {
            ++m_DroppedRequestCount;
            return false;
        }

        Math::Vector3 normal = hit.Normal;
        if (!std::isfinite(normalLengthSquared))
        {
            // 有限の大きな法線でも、長さの計算がオーバーフローしない範囲へ縮小する。
            const float scale = std::fmax(std::fabs(normal.x), std::fmax(std::fabs(normal.y), std::fabs(normal.z)));
            normal /= scale;
        }
        HitEffectEmitRequest request;
        request.EffectIndex = SelectEffect(hit.SurfaceName.c_str());
        request.Position = hit.Position;
        request.Normal = normal.Normalized();
        m_PendingRequests.push_back(request);
        return true;
    }

    const VariableArray<HitEffectEmitRequest>& HitEffectEmitter::GetPendingRequests() const
    {
        return m_PendingRequests;
    }

    void HitEffectEmitter::ConsumeRequests(VariableArray<HitEffectEmitRequest>& outRequests)
    {
        outRequests.swap(m_PendingRequests);
        m_PendingRequests.clear();
    }

    void HitEffectEmitter::ClearRequests()
    {
        m_PendingRequests.clear();
    }

    uint32_t HitEffectEmitter::GetDroppedRequestCount() const
    {
        return m_DroppedRequestCount;
    }
} // namespace NorvesLib::Core::Particle
