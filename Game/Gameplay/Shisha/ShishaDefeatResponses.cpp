// シ者の撃破応答を ID から解決し、枯れ度の源に回復を設定する。
#include "Gameplay/Shisha/ShishaDefeatResponses.h"

#include "Component/WitherSourceComponent.h"
#include "Engine/Engine.h"
#include "Gameplay/Shisha/ShishaComponent.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Vegetation/Vegetation.h"

#include <cmath>

namespace Game::Gameplay
{
    namespace
    {
        static const ShishaDefeatResponse DefeatResponses[] = {
            {WitherRecoverResponseId, &RunWitherRecover},
        };
    }

    const ShishaDefeatResponse* FindShishaDefeatResponse(const NorvesLib::Core::Container::String& id)
    {
        for (const auto& response : DefeatResponses)
        {
            if (id == response.Id)
            {
                return &response;
            }
        }
        return nullptr;
    }

    bool RunWitherRecover(ShishaComponent& shisha)
    {
        const float seconds = shisha.getWitherRecoverSeconds();
        const float target = shisha.getWitherRecoverTarget();
        if (!std::isfinite(seconds) || seconds < 0.0f ||
            !std::isfinite(target) || target < 0.0f || target > 1.0f)
        {
            LOG_WARNING("SHISHA_WITHER_RECOVER status=InvalidArgs enemy=%s seconds=%.3f target=%.3f 回復の引数が不正です",
                        shisha.GetEnemyId().c_str(), seconds, target);
            return false;
        }

        auto* owner = shisha.GetOwner();
        auto* source = owner ? owner->GetComponent<NorvesLib::Core::Component::WitherSourceComponent>() : nullptr;
        bool bOwnSourceApplied = false;
        if (source)
        {
            // 撃破後の EndPlay で源を手放しても、遷移中は目標に届くまでマップに残る。
            bOwnSourceApplied = source->SetLevel(target, seconds);
        }

        const NorvesLib::Core::Container::String key = shisha.getWitherSourceKey();
        bool bKeySourceApplied = false;
        auto* engine = NorvesLib::Core::Engine::GEngine;
        if (!key.empty() && (!source || key != source->GetKey()) && engine)
        {
            bKeySourceApplied = engine->GetVegetation().GetDrynessMap().SetTargetByKey(key, target, seconds);
        }

        if (bOwnSourceApplied || bKeySourceApplied)
        {
            LOG_INFO("SHISHA_WITHER_RECOVER status=Applied enemy=%s seconds=%.3f target=%.3f own=%d keyed=%d 回復を設定しました",
                     shisha.GetEnemyId().c_str(), seconds, target, bOwnSourceApplied, bKeySourceApplied);
            return true;
        }

        LOG_INFO("SHISHA_WITHER_RECOVER status=NoSource enemy=%s 枯れ度の源が無いため何もしません",
                 shisha.GetEnemyId().c_str());
        return false;
    }
} // namespace Game::Gameplay
