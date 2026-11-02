// 当たりからエフェクトの放出要求を作る契約を CPU 上で反証する。
#include "Particle/HitEffectEmitter.h"

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
    return bSingleHits && bFallback && bRejection && bConsumption ? 0 : 1;
}
