/**
 * @file DefeatBloodPresenter.cpp
 * @brief 撃破の合図を共有 VFX のイベントと寿命なしのデカールへ写す
 */

#include "Particle/DefeatBloodPresenter.h"

#include "Logging/LogMacros.h"
#include "Particle/VfxSchema.h"
#include "Rendering/MaterialTypes.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        void AlignDecalToNormal(Rendering::DecalInstance& instance, const Math::Vector3& normal)
        {
            const double x = normal.x;
            const double y = normal.y;
            const double z = normal.z;
            const double transverse = std::sqrt(x * x + y * y);
            if (transverse == 0.0)
            {
                // 真逆の -Z は X 軸まわりの半回転。それ以外は既定の単位四元数を保つ。
                if (z < 0.0)
                {
                    instance.Rotation[0] = 1.0f;
                    instance.Rotation[3] = 0.0f;
                }
                return;
            }

            // +Z と法線の外積を回転軸にする。atan2 なら -Z の近くでも 1 + z の桁落ちを避けられる。
            const double halfAngle = 0.5 * std::atan2(transverse, z);
            const double scale = std::sin(halfAngle) / transverse;
            instance.Rotation[0] = static_cast<float>(-y * scale);
            instance.Rotation[1] = static_cast<float>(x * scale);
            instance.Rotation[2] = 0.0f;
            instance.Rotation[3] = static_cast<float>(std::cos(halfAngle));
        }
    } // namespace

    DefeatBloodPresenter::DefeatBloodPresenter(VfxEffectSystem& vfx, DecalPool& decals,
        const DefeatBloodPresenterDesc& desc)
        : m_Vfx(vfx), m_Decals(decals), m_Desc(desc)
    {
        m_bValidDesc = desc.Timeline.IsValid() && std::isfinite(desc.DecalDepthMeters) &&
            desc.DecalDepthMeters > 0.0f && std::isfinite(desc.MinDecalRadiusMeters) &&
            desc.MinDecalRadiusMeters > 0.0f;
        for (const float component : desc.DecalColor)
        {
            m_bValidDesc = m_bValidDesc && std::isfinite(component);
        }
        m_Slots.resize(desc.MaxActive);
        m_Cues.reserve(4u);
    }

    DefeatBloodPresenter::~DefeatBloodPresenter()
    {
        for (const ActiveSlot& slot : m_Slots)
        {
            if (slot.bActive && m_Decals.IsAlive(slot.Decal))
            {
                m_Decals.Remove(slot.Decal);
            }
        }
        if (m_Vfx.IsLiveInstance(m_SprayInstance))
        {
            m_Vfx.DestroyInstance(m_SprayInstance);
        }
    }

    bool DefeatBloodPresenter::Trigger(const Math::Vector3& position, const Math::Vector3& normal)
    {
        DefeatBloodSequence sequence;
        if (!m_bValidDesc || !sequence.Start(m_Desc.Timeline, position, normal))
        {
            WarnOnce("撃破の血の時間割・位置・法線・描画値が不正です");
            return false;
        }
        if (m_ActiveCount >= m_Desc.MaxActive)
        {
            WarnOnce("撃破の血の同時進行数が上限に達しています");
            return false;
        }
        if (!m_SprayInstance.IsValid())
        {
            VfxInstanceDesc instanceDesc;
            instanceDesc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
            m_SprayInstance = m_Vfx.CreateInstance(MakeDefaultSprayEffect(), instanceDesc);
        }
        if (!m_Vfx.IsLiveInstance(m_SprayInstance))
        {
            WarnOnce("撃破の血しぶきのインスタンスを利用できません");
            return false;
        }
        for (ActiveSlot& slot : m_Slots)
        {
            if (!slot.bActive)
            {
                slot.Sequence = sequence;
                slot.Decal = {};
                slot.StartVfxTime = m_Vfx.GetInstanceTime(m_SprayInstance);
                slot.bActive = true;
                ++m_ActiveCount;
                return true;
            }
        }
        return false;
    }

    void DefeatBloodPresenter::Tick(float deltaSeconds)
    {
        for (ActiveSlot& slot : m_Slots)
        {
            if (!slot.bActive)
            {
                continue;
            }
            m_Cues.clear();
            slot.Sequence.Tick(deltaSeconds, m_Cues);
            for (const DefeatBloodCue& cue : m_Cues)
            {
                switch (cue.Type)
                {
                case DefeatBloodCueType::Spray:
                    // イベント位置は生成位置へ直接渡されるワールド位置で、インスタンス位置は加算されない。
                    if (!m_Vfx.RaiseEvent(m_SprayInstance, SprayEventName, slot.Sequence.GetPosition(),
                        slot.StartVfxTime + cue.TimeSeconds))
                    {
                        WarnOnce("撃破の血しぶきのイベントを送れませんでした。刻みの呼び出し順を確認してください");
                    }
                    break;
                case DefeatBloodCueType::Bleed:
                {
                    DecalDesc desc;
                    const Math::Vector3& position = slot.Sequence.GetPosition();
                    desc.Instance.Position[0] = position.x;
                    desc.Instance.Position[1] = position.y;
                    desc.Instance.Position[2] = position.z;
                    AlignDecalToNormal(desc.Instance, slot.Sequence.GetNormal());
                    desc.LifetimeSeconds = 0.0f;
                    desc.Instance.Flags = 0u;
                    slot.Decal = m_Decals.Add(desc);
                    if (!slot.Decal.IsValid())
                    {
                        WarnOnce("撃破の血の滲みのデカールを追加できませんでした");
                    }
                    break;
                }
                case DefeatBloodCueType::Vanish:
                    // 不透明度は時間割の評価値を毎刻み反映する。
                    break;
                case DefeatBloodCueType::Finished:
                    if (m_Decals.IsAlive(slot.Decal))
                    {
                        m_Decals.Remove(slot.Decal);
                    }
                    slot.Decal = {};
                    slot.bActive = false;
                    --m_ActiveCount;
                    break;
                }
            }
            if (slot.bActive && m_Decals.IsAlive(slot.Decal))
            {
                const float radius = std::max(m_Desc.MinDecalRadiusMeters, slot.Sequence.GetBleedRadiusMeters());
                const float halfExtents[3] = {radius, radius, m_Desc.DecalDepthMeters};
                const float color[4] = {m_Desc.DecalColor[0], m_Desc.DecalColor[1], m_Desc.DecalColor[2],
                    slot.Sequence.GetOpacity()};
                if (!m_Decals.SetAppearance(slot.Decal, halfExtents, color))
                {
                    WarnOnce("撃破の血の滲みの大きさと不透明度を更新できませんでした");
                }
            }
        }
    }

    uint32_t DefeatBloodPresenter::GetActiveCount() const
    {
        return m_ActiveCount;
    }

    void DefeatBloodPresenter::WarnOnce(const char* message)
    {
        if (!m_bWarned)
        {
            LOG_WARNING("%s", message);
            m_bWarned = true;
        }
    }

    VfxEffectAsset DefeatBloodPresenter::MakeDefaultSprayEffect()
    {
        VfxEffectAsset effect;
        effect.Name = "DefeatBloodSpray";
        VfxEmitterAsset emitter = MakeVfxDefaultEmitter("Blood");
        VfxModule spawn = MakeVfxDefaultModule(VfxModuleType::SpawnOnEvent);
        VfxModule velocity = MakeVfxDefaultModule(VfxModuleType::InitVelocityBox);
        VfxModule lifetime = MakeVfxDefaultModule(VfxModuleType::InitLifetime);
        VfxModule color = MakeVfxDefaultModule(VfxModuleType::InitColor);
        VfxModule size = MakeVfxDefaultModule(VfxModuleType::InitSize);
        VfxModule acceleration = MakeVfxDefaultModule(VfxModuleType::Acceleration);
        VfxModule renderer = MakeVfxDefaultModule(VfxModuleType::SpriteRenderer);

        // Vector3::Up は (0, 1, 0)。ワールドの +Y を上向きとして、初速と重力を設定する。
        const bool bConfigured =
            SetVfxEmitterProperty(emitter, "MaxParticles", VfxValue::MakeUInt(256u), nullptr) &&
            SetVfxEmitterProperty(emitter, "Space", VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), nullptr) &&
            SetVfxModuleParameter(spawn, "EventName", VfxValue::MakeName(SprayEventName), nullptr) &&
            SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(24u), nullptr) &&
            SetVfxModuleParameter(velocity, "Min", VfxValue::MakeVector3(Math::Vector3(-1.5f, 1.0f, -1.5f)), nullptr) &&
            SetVfxModuleParameter(velocity, "Max", VfxValue::MakeVector3(Math::Vector3(1.5f, 3.5f, 1.5f)), nullptr) &&
            SetVfxModuleParameter(lifetime, "Lifetime", VfxValue::MakeFloat(0.6f), nullptr) &&
            SetVfxModuleParameter(color, "Color", VfxValue::MakeColor(Math::Vector4(0.35f, 0.0f, 0.0f, 0.9f)), nullptr) &&
            SetVfxModuleParameter(size, "Size", VfxValue::MakeVector2(Math::Vector2(0.04f, 0.04f)), nullptr) &&
            SetVfxModuleParameter(acceleration, "Acceleration", VfxValue::MakeVector3(Math::Vector3(0.0f, -9.8f, 0.0f)), nullptr) &&
            SetVfxModuleParameter(renderer, "Layer", VfxValue::MakeEnum(1u), nullptr) &&
            SetVfxModuleParameter(renderer, "Blend", VfxValue::MakeEnum(static_cast<uint32_t>(Rendering::BlendMode::Translucent)), nullptr);
        if (!bConfigured)
        {
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
        return effect;
    }
} // namespace NorvesLib::Core::Particle
