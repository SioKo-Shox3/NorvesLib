// 当たりからエフェクトの放出要求を作る契約を CPU 上で反証する。
#include "Particle/HitEffectEmitter.h"
#include "Particle/VfxSchema.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;

namespace
{
    bool Check(bool bCondition, const char* item, const char* reason)
    {
        if (!bCondition)
        {
            std::cerr << item << ": " << reason << '\n';
        }
        return bCondition;
    }

#define REQUIRE(condition, reason) \
    do \
    { \
        if (!Check((condition), item, (reason))) \
        { \
            return false; \
        } \
    } while (false)

    bool SameVector(const Math::Vector3& left, const Math::Vector3& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    bool NearNormal(const Math::Vector3& left, const Math::Vector3& right)
    {
        return std::fabs(left.x - right.x) <= 1e-6f &&
            std::fabs(left.y - right.y) <= 1e-6f &&
            std::fabs(left.z - right.z) <= 1e-6f;
    }

    bool SameRequest(const HitEffectEmitRequest& left, const HitEffectEmitRequest& right)
    {
        return left.EffectIndex == right.EffectIndex &&
            SameVector(left.Position, right.Position) && SameVector(left.Normal, right.Normal);
    }

    bool TestSingleHits(HitEffectEmitter& emitter)
    {
        const char* item = "1. 当たりごとの要求";
        HitEffectHit hit;
        hit.SurfaceName = "Metal";
        hit.Position = Math::Vector3(1.25f, -2.5f, 3.75f);
        hit.Normal = Math::Vector3(0.0f, 0.6f, 0.8f);
        REQUIRE(emitter.SubmitHit(hit), "Metal の当たりを受け付けない");
        REQUIRE(emitter.GetPendingRequests().size() == 1u, "Metal の要求がちょうど 1 つではない");
        const HitEffectEmitRequest first = emitter.GetPendingRequests()[0];
        REQUIRE(first.EffectIndex == 1u, "Metal のエフェクトが違う");
        REQUIRE(SameVector(first.Position, hit.Position), "Metal の位置が一致しない");
        REQUIRE(NearNormal(first.Normal, hit.Normal), "Metal の法線が一致しない");

        hit.SurfaceName = "Wood";
        hit.Position = Math::Vector3(-4.0f, 5.0f, 6.0f);
        hit.Normal = Math::Vector3(0.0f, 1.0f, 0.0f);
        REQUIRE(emitter.SubmitHit(hit), "Wood の当たりを受け付けない");
        REQUIRE(emitter.GetPendingRequests().size() == 2u, "Wood の当たりで要求が 1 つ増えない");
        REQUIRE(emitter.GetPendingRequests()[1].EffectIndex == 2u, "Wood のエフェクトが違う");
        REQUIRE(SameRequest(emitter.GetPendingRequests()[0], first), "先の要求が変わった");
        return true;
    }

    bool TestFallback(HitEffectEmitter& emitter)
    {
        const char* item = "2. 表にない表面";
        constexpr const char* surfaces[] = {"Glass", "", "metal"};
        HitEffectHit hit;
        for (size_t index = 0u; index < 3u; ++index)
        {
            hit.SurfaceName = surfaces[index];
            hit.Position = Math::Vector3(static_cast<float>(index + 10u), 0.0f, 0.0f);
            const size_t previousSize = emitter.GetPendingRequests().size();
            REQUIRE(emitter.SubmitHit(hit), "表にない表面の当たりを受け付けない");
            REQUIRE(emitter.GetPendingRequests().size() == previousSize + 1u, "要求が 1 つ増えない");
            REQUIRE(emitter.GetPendingRequests()[previousSize].EffectIndex == 0u, "既定のエフェクトではない");
        }
        return true;
    }

    bool TestRejection(HitEffectEmitter& emitter)
    {
        const char* item = "3. 不正な当たりの拒否";
        HitEffectHit hit;
        hit.SurfaceName = "Metal";
        const size_t previousSize = emitter.GetPendingRequests().size();
        hit.Position = Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
        REQUIRE(!emitter.SubmitHit(hit), "位置が NaN の当たりを受け付けた");
        REQUIRE(emitter.GetPendingRequests().size() == previousSize, "位置が NaN の要求を積んだ");

        hit.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        hit.Normal = Math::Vector3(0.0f, 0.0f, 0.0f);
        REQUIRE(!emitter.SubmitHit(hit), "法線がゼロの当たりを受け付けた");
        REQUIRE(emitter.GetPendingRequests().size() == previousSize, "法線がゼロの要求を積んだ");

        hit.Normal = Math::Vector3(0.0f, std::numeric_limits<float>::infinity(), 0.0f);
        REQUIRE(!emitter.SubmitHit(hit), "法線が無限大の当たりを受け付けた");
        REQUIRE(emitter.GetPendingRequests().size() == previousSize, "法線が無限大の要求を積んだ");
        return true;
    }

    bool TestConsumption(HitEffectEmitter& emitter)
    {
        const char* item = "4. 要求の消費";
        const VariableArray<HitEffectEmitRequest> pending = emitter.GetPendingRequests();
        REQUIRE(pending.size() == 5u, "消費前の要求が 5 つではない");
        VariableArray<HitEffectEmitRequest> consumed;
        emitter.ConsumeRequests(consumed);
        REQUIRE(consumed.size() == pending.size(), "要求をすべて受け取れない");
        for (size_t index = 0u; index < pending.size(); ++index)
        {
            REQUIRE(SameRequest(consumed[index], pending[index]), "要求の内容または順序が違う");
        }
        REQUIRE(emitter.GetPendingRequests().empty(), "消費後も要求が残っている");
        return true;
    }

    bool TestPlayback()
    {
        const char* item = "5. 当たりの位置と法線に沿う再生";
        VfxEmitterAsset particleEmitter = MakeVfxDefaultEmitter("HitPlayback");
        VfxModule spawn = MakeVfxDefaultModule(VfxModuleType::SpawnOnEvent);
        VfxModule velocity = MakeVfxDefaultModule(VfxModuleType::InitVelocityBox);
        VfxModule acceleration = MakeVfxDefaultModule(VfxModuleType::Acceleration);
        VfxModule lifetime = MakeVfxDefaultModule(VfxModuleType::InitLifetime);
        Container::String error;
        REQUIRE(SetVfxEmitterProperty(particleEmitter, "Space",
            VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), &error),
            "ワールド空間を設定できない");
        REQUIRE(SetVfxModuleParameter(spawn, "EventName",
            VfxValue::MakeName(HitEffectEmitter::HitEventName), &error), "当たりイベントを設定できない");
        REQUIRE(SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(8u), &error),
            "放出数を設定できない");
        REQUIRE(SetVfxModuleParameter(velocity, "Max",
            VfxValue::MakeVector3(Math::Vector3(0.5f, 0.5f, 2.0f)), &error), "初速の上限を設定できない");
        REQUIRE(SetVfxModuleParameter(velocity, "Min",
            VfxValue::MakeVector3(Math::Vector3(-0.5f, -0.5f, 1.0f)), &error), "初速の下限を設定できない");
        REQUIRE(SetVfxModuleParameter(acceleration, "Acceleration",
            VfxValue::MakeVector3(Math::Vector3(0.0f, 0.0f, 0.0f)), &error), "加速度をゼロに設定できない");
        REQUIRE(SetVfxModuleParameter(lifetime, "Lifetime", VfxValue::MakeFloat(1.0f), &error),
            "寿命を設定できない");
        particleEmitter.Modules.push_back(spawn);
        particleEmitter.Modules.push_back(velocity);
        particleEmitter.Modules.push_back(lifetime);
        particleEmitter.Modules.push_back(acceleration);
        particleEmitter.Modules.push_back(MakeVfxDefaultModule(VfxModuleType::SpriteRenderer));
        VfxEffectAsset effect;
        effect.Name = "HitPlayback";
        effect.Emitters.push_back(particleEmitter);
        HitEffectTable table;
        table.Effects.push_back(effect);
        HitEffectEmitter emitter;
        REQUIRE(emitter.SetTable(table, &error), "再生用の表を設定できない");

        VfxEffectSystem effects;
        HitEffectHit hit;
        hit.Position = Math::Vector3(2.0f, 3.0f, -1.0f);
        hit.Normal = Math::Vector3(1.0f, 0.0f, 0.0f);
        REQUIRE(emitter.SubmitHit(hit), "再生用の当たりを受け付けない");
        emitter.Dispatch(effects);
        effects.Tick(0.1f);
        VariableArray<VfxParticleState> particles;
        REQUIRE(effects.CopyParticles(emitter.GetInstanceHandle(0u), 0u, particles), "放出した粒を取得できない");
        REQUIRE(particles.size() == 8u, "当たり 1 回の粒が 8 個ではない");
        for (const VfxParticleState& particle : particles)
        {
            REQUIRE(Math::Vector3::Dot(particle.Velocity, hit.Normal) > 0.9f, "粒が当たりの法線の側へ進まない");
            const Math::Vector3 expectedPosition = hit.Position + particle.Velocity * particle.Age;
            REQUIRE(std::fabs(particle.Position.x - expectedPosition.x) <= 1e-4f &&
                std::fabs(particle.Position.y - expectedPosition.y) <= 1e-4f &&
                std::fabs(particle.Position.z - expectedPosition.z) <= 1e-4f,
                "粒の位置が当たりの位置と速度・経過時間から求めた位置と一致しない");
        }

        emitter.Dispatch(effects);
        effects.Tick(0.1f);
        REQUIRE(effects.CopyParticles(emitter.GetInstanceHandle(0u), 0u, particles), "次の更新の粒を取得できない");
        REQUIRE(particles.size() <= 8u, "当たりを追加していないのに粒が増えた");
        return true;
    }

#undef REQUIRE
} // namespace

int main()
{
    HitEffectTable table;
    VfxEffectAsset effect;
    effect.Name = "Default";
    table.Effects.push_back(effect);
    effect.Name = "Metal";
    table.Effects.push_back(effect);
    effect.Name = "Wood";
    table.Effects.push_back(effect);
    table.DefaultEffectIndex = 0u;
    HitEffectTableEntry entry;
    entry.SurfaceName = "Metal";
    entry.EffectIndex = 1u;
    table.Entries.push_back(entry);
    entry.SurfaceName = "Wood";
    entry.EffectIndex = 2u;
    table.Entries.push_back(entry);

    HitEffectEmitter emitter;
    if (!Check(emitter.SetTable(table), "準備", "表を設定できない"))
    {
        return 1;
    }
    const bool bSingleHits = TestSingleHits(emitter);
    const bool bFallback = TestFallback(emitter);
    const bool bRejection = TestRejection(emitter);
    const bool bConsumption = TestConsumption(emitter);
    const bool bPlayback = TestPlayback();
    return bSingleHits && bFallback && bRejection && bConsumption && bPlayback ? 0 : 1;
}
