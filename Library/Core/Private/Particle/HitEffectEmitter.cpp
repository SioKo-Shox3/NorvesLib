// 当たりの表を検証し、表面に応じたエフェクトの放出要求を CPU 上で蓄積する。
#include "Particle/HitEffectEmitter.h"

#include "Logging/LogMacros.h"
#include "Particle/VfxSchema.h"
#include "Rendering/MaterialTypes.h"

#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        /** @brief スキーマの既定値を基に、当たりでだけ放出するエフェクトを組む。 */
        VfxEffectAsset MakeHitEffect(const char* name, uint32_t maxParticles, uint32_t count,
            const Math::Vector3& velocityMin, const Math::Vector3& velocityMax,
            const Math::Vector3& accelerationValue, float lifetimeValue,
            const Math::Vector4& colorValue, const Math::Vector2& sizeValue, Rendering::BlendMode blend)
        {
            VfxEffectAsset effect;
            effect.Name = name;
            VfxEmitterAsset emitter = MakeVfxDefaultEmitter(name);
            VfxModule spawn = MakeVfxDefaultModule(VfxModuleType::SpawnOnEvent);
            VfxModule velocity = MakeVfxDefaultModule(VfxModuleType::InitVelocityBox);
            VfxModule lifetime = MakeVfxDefaultModule(VfxModuleType::InitLifetime);
            VfxModule color = MakeVfxDefaultModule(VfxModuleType::InitColor);
            VfxModule size = MakeVfxDefaultModule(VfxModuleType::InitSize);
            VfxModule acceleration = MakeVfxDefaultModule(VfxModuleType::Acceleration);
            VfxModule renderer = MakeVfxDefaultModule(VfxModuleType::SpriteRenderer);
            Container::String error;
            // Transparent はスキーマの列挙値 1。ワールドの半透明の板として扱う。
            const bool bConfigured =
                SetVfxEmitterProperty(emitter, "MaxParticles", VfxValue::MakeUInt(maxParticles), &error) &&
                SetVfxEmitterProperty(emitter, "Space", VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), &error) &&
                SetVfxModuleParameter(spawn, "EventName", VfxValue::MakeName(HitEffectEmitter::HitEventName), &error) &&
                SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(count), &error) &&
                SetVfxModuleParameter(velocity, "Max", VfxValue::MakeVector3(velocityMax), &error) &&
                SetVfxModuleParameter(velocity, "Min", VfxValue::MakeVector3(velocityMin), &error) &&
                SetVfxModuleParameter(lifetime, "Lifetime", VfxValue::MakeFloat(lifetimeValue), &error) &&
                SetVfxModuleParameter(color, "Color", VfxValue::MakeColor(colorValue), &error) &&
                SetVfxModuleParameter(size, "Size", VfxValue::MakeVector2(sizeValue), &error) &&
                SetVfxModuleParameter(acceleration, "Acceleration", VfxValue::MakeVector3(accelerationValue), &error) &&
                SetVfxModuleParameter(renderer, "Layer", VfxValue::MakeEnum(1u), &error) &&
                SetVfxModuleParameter(renderer, "Blend", VfxValue::MakeEnum(static_cast<uint32_t>(blend)), &error);
            if (!bConfigured)
            {
                NORVES_LOG_ERROR("HitEffectEmitter", "当たりの既定エフェクトを設定できませんでした: %s", error.c_str());
                return {};
            }
            emitter.Modules.push_back(spawn);
            emitter.Modules.push_back(velocity);
            emitter.Modules.push_back(lifetime);
            emitter.Modules.push_back(color);
            emitter.Modules.push_back(size);
            emitter.Modules.push_back(acceleration);
            emitter.Modules.push_back(renderer);
            effect.Emitters.push_back(emitter);
            if (!ValidateVfxEffect(effect, &error))
            {
                NORVES_LOG_ERROR("HitEffectEmitter", "当たりの既定エフェクトが無効です: %s", error.c_str());
                return {};
            }
            return effect;
        }

        VfxEffectAsset MakeHitImpact()
        {
            // 局所 +Z の初速で法線の側へ小さく広がる衝撃にする。
            return MakeHitEffect("HitImpact", 128u, 8u,
                Math::Vector3(-0.4f, -0.4f, 0.3f), Math::Vector3(0.4f, 0.4f, 1.2f),
                Math::Vector3(0.0f, -0.5f, 0.0f), 0.6f,
                Math::Vector4(0.45f, 0.42f, 0.38f, 0.6f), Math::Vector2(0.12f, 0.12f),
                Rendering::BlendMode::Translucent);
        }

        VfxEffectAsset MakeHitSparks()
        {
            // 局所 +Z の初速で法線の側へ飛ばし、Y 上向きのワールドの重力で落とす。
            return MakeHitEffect("HitSparks", 256u, 16u,
                Math::Vector3(-1.5f, -1.5f, 2.0f), Math::Vector3(1.5f, 1.5f, 6.0f),
                Math::Vector3(0.0f, -9.81f, 0.0f), 0.25f,
                Math::Vector4(1.0f, 0.55f, 0.15f, 1.0f), Math::Vector2(0.03f, 0.03f),
                Rendering::BlendMode::Additive);
        }

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

    HitEffectEmitter::HitEffectEmitter()
    {
        Container::String error;
        if (!SetTable(MakeDefaultTable(), &error))
        {
            NORVES_LOG_ERROR("HitEffectEmitter", "当たりの既定表を設定できませんでした: %s", error.c_str());
        }
    }

    HitEffectTable HitEffectEmitter::MakeDefaultTable()
    {
        HitEffectTable table;
        table.Effects.push_back(MakeHitImpact());
        table.Effects.push_back(MakeHitSparks());
        table.Entries.push_back({"Metal", 1u});
        table.DefaultEffectIndex = 0u;
        return table;
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
        m_bInstancesNeedReset = true;
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

    void HitEffectEmitter::Dispatch(VfxEffectSystem& effects)
    {
        if (m_bInstancesNeedReset)
        {
            for (VfxEffectHandle handle : m_InstanceHandles)
            {
                effects.DestroyInstance(handle);
            }
            m_InstanceHandles.clear();
            m_bInstancesNeedReset = false;
        }
        if (m_PendingRequests.empty())
        {
            return;
        }

        m_InstanceHandles.resize(m_Table.Effects.size());
        for (const HitEffectEmitRequest& request : m_PendingRequests)
        {
            // 表の差し替え前に積んだ要求の添字が、新しい表では存在しない場合がある。
            if (request.EffectIndex >= m_Table.Effects.size())
            {
                continue;
            }
            VfxEffectHandle& handle = m_InstanceHandles[request.EffectIndex];
            if (!effects.IsLiveInstance(handle))
            {
                VfxInstanceDesc desc;
                desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
                desc.Seed = 0xC001D00Du ^ (request.EffectIndex * 0x9E3779B9u);
                handle = effects.CreateInstance(m_Table.Effects[request.EffectIndex], desc);
                if (!effects.IsLiveInstance(handle))
                {
                    continue;
                }
            }
            effects.RaiseOrientedEvent(handle, HitEventName, request.Position, request.Normal,
                effects.GetInstanceTime(handle));
        }
        m_PendingRequests.clear();
    }

    void HitEffectEmitter::ResetInstances()
    {
        m_InstanceHandles.clear();
        m_PendingRequests.clear();
        m_bInstancesNeedReset = false;
    }

    VfxEffectHandle HitEffectEmitter::GetInstanceHandle(uint32_t effectIndex) const
    {
        return effectIndex < m_InstanceHandles.size() ? m_InstanceHandles[effectIndex] : VfxEffectHandle{};
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
