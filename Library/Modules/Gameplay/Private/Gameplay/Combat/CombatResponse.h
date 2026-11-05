// 当たりのエフェクトとヒットストップの要求を扱う。
#pragma once

#include "Gameplay/Combat/CombatResponseSettings.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Modules::Gameplay::Combat
{
    class CombatRuntime;
    struct CombatHitEvent;

    class CombatResponse
    {
    public:
        explicit CombatResponse(CombatRuntime& runtime);
        ~CombatResponse();
        CombatResponse(const CombatResponse&) = delete;
        CombatResponse& operator=(const CombatResponse&) = delete;

        void SetEngine(Core::Engine::Engine* engine) { m_Engine = engine; }
        void SetSettings(const CombatResponseSettings& settings) { m_Settings = settings; }
        const CombatResponseSettings& GetSettings() const { return m_Settings; }
        void Flush();
        void Reset();

    private:
        void HandleHit(const CombatHitEvent& event);

        CombatRuntime& m_Runtime;
        Core::Engine::Engine* m_Engine = nullptr;
        CombatResponseSettings m_Settings;
        float m_MaxHitStopHint = 0.0f;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
