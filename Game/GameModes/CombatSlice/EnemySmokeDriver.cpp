#include "EnemySmokeDriver.h"

#include "CombatSliceCapture.h"
#include "CombatSliceSwing.h"
#include "Container/VariableArray.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/Shisha/ShishaComponent.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"
#include "Math/Quaternion.h"
#include "Object/Entity.h"
#include "Object/World.h"

#include <cmath>
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    namespace Enemy = NorvesLib::Modules::Gameplay::Enemy;
    namespace Gameplay = NorvesLib::Modules::Gameplay;
    namespace W = NorvesLib::Modules::Gameplay::World;
    namespace M = NorvesLib::Math;
    namespace L = CombatSliceLayout;

    namespace
    {
        enum class EMarker : uint8_t
        {
            ZoneEntered,
            Alerted,
            Attack,
            Collapsed,
            Defeated,
            ProgressionChanged,
            FinalUnlocked,
            Count
        };

        constexpr float MoveSpeed = 3.0f;
        constexpr double SwingPauseSeconds = 0.2;
        constexpr ULONGLONG CaptureDelayMilliseconds = 1000;
        constexpr double TimeoutSeconds = 30.0;

        const char* GetMarkerName(EMarker marker)
        {
            switch (marker)
            {
            case EMarker::ZoneEntered: return "ZoneEntered";
            case EMarker::Alerted: return "Alerted";
            case EMarker::Attack: return "Attack";
            case EMarker::Collapsed: return "Collapsed";
            case EMarker::Defeated: return "Defeated";
            case EMarker::ProgressionChanged: return "ProgressionChanged";
            case EMarker::FinalUnlocked: return "FinalUnlocked";
            case EMarker::Count: return "capture";
            }
            return "unknown";
        }

        bool MoveToStrikePosition(Entity& dog, float worldDt)
        {
            const auto position = dog.GetPosition();
            const auto offset = L::DogStrikePosition.ToVector3() - position;
            const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
            const float travel = MoveSpeed * worldDt;
            if (distance <= travel)
            {
                dog.SetPosition(L::DogStrikePosition.ToVector3());
                return true;
            }
            if (travel > 0.0f)
            {
                dog.SetPosition(position + offset * (travel / distance));
            }
            return false;
        }

        bool IsPathSeparator(wchar_t value)
        {
            return value == L'\\' || value == L'/';
        }

        bool EnsureCaptureParent(const Container::String& path)
        {
            if (path.empty() || path.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }
            for (const char value : path)
            {
                if (value == '\0')
                {
                    return false;
                }
            }
            const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
            if (wideLength <= 0)
            {
                return false;
            }
            Container::VariableArray<wchar_t> widePath(static_cast<size_t>(wideLength));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
                                    widePath.data(), wideLength) != wideLength)
            {
                return false;
            }
            const DWORD required = GetFullPathNameW(widePath.data(), 0, nullptr, nullptr);
            if (required == 0)
            {
                return false;
            }
            Container::VariableArray<wchar_t> fullPath(required);
            const DWORD length = GetFullPathNameW(widePath.data(), required, fullPath.data(), nullptr);
            if (length == 0 || length >= required || IsPathSeparator(fullPath[length - 1]))
            {
                return false;
            }

            size_t lastSeparator = length;
            while (lastSeparator > 0 && !IsPathSeparator(fullPath[lastSeparator - 1]))
            {
                --lastSeparator;
            }
            if (lastSeparator == 0)
            {
                return true;
            }
            --lastSeparator;

            // ドライブや UNC の共有名より後のディレクトリだけを作る。
            size_t start = 3;
            if (length >= 2 && IsPathSeparator(fullPath[0]) && IsPathSeparator(fullPath[1]))
            {
                const bool bExtended = length >= 8 && fullPath[2] == L'?' && IsPathSeparator(fullPath[3]);
                if (bExtended && (fullPath[4] == L'U' || fullPath[4] == L'u') &&
                    (fullPath[5] == L'N' || fullPath[5] == L'n') &&
                    (fullPath[6] == L'C' || fullPath[6] == L'c') && IsPathSeparator(fullPath[7]))
                {
                    start = 8;
                }
                else if (bExtended)
                {
                    start = 7;
                }
                else
                {
                    start = 2;
                }
                if (!bExtended || start == 8)
                {
                    int remaining = 2;
                    while (start < length && remaining > 0)
                    {
                        if (IsPathSeparator(fullPath[start++]))
                        {
                            --remaining;
                        }
                    }
                }
            }
            for (size_t index = start; index <= lastSeparator; ++index)
            {
                if (!IsPathSeparator(fullPath[index]))
                {
                    continue;
                }
                const wchar_t separator = fullPath[index];
                fullPath[index] = L'\0';
                const bool bCreated = CreateDirectoryW(fullPath.data(), nullptr) != 0;
                const DWORD error = bCreated ? ERROR_SUCCESS : GetLastError();
                const DWORD attributes = bCreated ? FILE_ATTRIBUTE_DIRECTORY : GetFileAttributesW(fullPath.data());
                fullPath[index] = separator;
                if (!bCreated && (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
                                  (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    struct EnemySmokeDriver::State
    {
        Engine::Engine* Engine = nullptr;
        Gameplay::IGameplayModule* Module = nullptr;
        CombatSliceSceneIds Ids;
        Container::String CapturePath;
        CombatSliceSwing Swing;
        CombatSliceCapture Capture;
        uint32_t NextMarker = 0;
        double WorldSeconds = 0.0;
        double SwingPause = 0.0;
        ULONGLONG DefeatStartedAt = 0;
        bool bListening = false;
        bool bAtStrikePosition = false;
        bool bSwingStarted = false;
        bool bWaitingForSwing = false;
        bool bDefeatObserved = false;
        bool bProgressionDefeated = false;
        bool bCaptureSaved = false;
        bool bCaptureLogged = false;
        bool bFinished = false;
        int32_t ExitCode = 0;
        Delegate<void, const W::EncounterZoneEvent&> EnterCallback;
        Delegate<void> FinalCallback;

        void Fail(const char* reason)
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;
            ExitCode = 1;
            NORVES_LOG_INFO("CombatSlice", "ENEMY_SMOKE stage=failure reason=%s", reason);
            Engine->RequestExit(1);
        }

        void Accept(EMarker marker)
        {
            if (bFinished)
            {
                return;
            }
            const auto index = static_cast<uint32_t>(marker);
            if (index < NextMarker)
            {
                return;
            }
            NORVES_LOG_INFO("CombatSlice", "ENEMY_SMOKE stage=%s", GetMarkerName(marker));
            if (index != NextMarker)
            {
                const Container::String reason = Container::String("order_") + GetMarkerName(marker);
                Fail(reason.c_str());
                return;
            }
            ++NextMarker;
        }

        void SampleProgression()
        {
            const bool bNowDefeated = Module->GetProgressionState().IsDefeated(L::EnemyId);
            if (bNowDefeated && !bProgressionDefeated)
            {
                Accept(EMarker::ProgressionChanged);
            }
            bProgressionDefeated = bNowDefeated;
        }

        void LogSavedCapture()
        {
            if (!bFinished && bCaptureSaved && !bCaptureLogged &&
                NextMarker == static_cast<uint32_t>(EMarker::Count))
            {
                bCaptureLogged = true;
                NORVES_LOG_INFO("CombatSlice", "ENEMY_SMOKE stage=capture path=%s", CapturePath.c_str());
            }
        }

        void OnZoneEntered(const W::EncounterZoneEvent& event)
        {
            if (bListening && !bFinished && event.ZoneId == L::ZoneId)
            {
                Accept(EMarker::ZoneEntered);
            }
        }

        void OnFinalUnlocked()
        {
            if (!bListening || bFinished)
            {
                return;
            }
            // 撃破 ID は解放通知の直前に追加されるため、同じ配送内で変化を拾う。
            SampleProgression();
            Accept(EMarker::FinalUnlocked);
            LogSavedCapture();
        }
    };

    EnemySmokeDriver::EnemySmokeDriver() = default;

    EnemySmokeDriver::~EnemySmokeDriver()
    {
        Unsubscribe();
    }

    bool EnemySmokeDriver::Begin(GameModeContext& ctx, const CombatSliceSceneIds& ids,
                                 const CombatSliceOptions& options)
    {
        End(ctx);
        m_bFinished = false;
        m_ExitCode = 0;
        m_State = Container::MakeShared<State>();
        auto& state = *m_State;
        state.Engine = &ctx.EngineRef;
        state.Module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        state.Ids = ids;
        state.CapturePath = options.CapturePath;
        if (!state.Module)
        {
            state.Fail("begin_gameplay");
            return false;
        }
        auto& progression = state.Module->GetProgressionState();
        if (progression.IsFinalUnlocked() || progression.IsDefeated(L::EnemyId))
        {
            state.Fail("progression_preloaded");
            return false;
        }
        auto* dog = ctx.WorldRef.FindEntityByObjectId(ids.Dog);
        auto* sensor = dog ? dog->GetComponent<W::ZoneSensorComponent>() : nullptr;
        auto* owner = ctx.WorldRef.FindEntityByObjectId(ids.Shisha);
        auto* shisha = owner ? owner->GetComponent<Game::Gameplay::ShishaComponent>() : nullptr;
        if (!dog || dog->IsPendingDestroy() || !sensor || sensor->IsPendingDestroy() ||
            !owner || owner->IsPendingDestroy() || !shisha || shisha->IsPendingDestroy())
        {
            state.Fail("begin_actors");
            return false;
        }
        if (!state.CapturePath.empty() && !EnsureCaptureParent(state.CapturePath))
        {
            state.Fail("capture");
            return false;
        }
        dog->SetPosition(L::DogStartPosition.ToVector3());
        dog->SetRotation(M::Quaternion::Identity);
        sensor->MarkTeleported();

        const Container::TWeakPtr<State> weak = m_State;
        state.EnterCallback = [weak](const W::EncounterZoneEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnZoneEntered(event);
            }
        };
        state.FinalCallback = [weak]()
        {
            if (const auto locked = weak.lock())
            {
                locked->OnFinalUnlocked();
            }
        };
        state.Module->GetEncounterService().OnZoneEntered.Add(state.EnterCallback);
        progression.OnFinalUnlocked.Add(state.FinalCallback);
        state.bListening = true;
        NORVES_LOG_INFO("CombatSlice", "ENEMY_SMOKE stage=ready");
        return true;
    }

    void EnemySmokeDriver::Tick(GameModeContext& ctx, float deltaTime)
    {
        (void)deltaTime;
        if (!m_State || m_State->bFinished)
        {
            return;
        }
        auto& state = *m_State;
        if (Gameplay::FindGameplayModule(Module::GetModuleRegistry()) != state.Module)
        {
            state.Fail("lost_gameplay");
            return;
        }
        auto* dog = ctx.WorldRef.FindEntityByObjectId(state.Ids.Dog);
        auto* sensor = dog ? dog->GetComponent<W::ZoneSensorComponent>() : nullptr;
        if (!dog || dog->IsPendingDestroy() || !sensor || sensor->IsPendingDestroy())
        {
            state.Fail("lost_dog");
            return;
        }
        const float worldDt = ctx.EngineRef.GetTimeSystem().GetFrameTimes().World;
        if (!std::isfinite(worldDt) || worldDt < 0.0f)
        {
            state.Fail("invalid_dt");
            return;
        }
        state.WorldSeconds += worldDt;

        auto* owner = ctx.WorldRef.FindEntityByObjectId(state.Ids.Shisha);
        auto* shisha = owner && !owner->IsPendingDestroy() ? owner->GetComponent<Game::Gameplay::ShishaComponent>() : nullptr;
        if (shisha && !shisha->IsPendingDestroy())
        {
            switch (shisha->GetState())
            {
            case Enemy::EEnemyState::Alerted: state.Accept(EMarker::Alerted); break;
            case Enemy::EEnemyState::Attack: state.Accept(EMarker::Attack); break;
            case Enemy::EEnemyState::Collapsed: state.Accept(EMarker::Collapsed); break;
            case Enemy::EEnemyState::Defeated:
                state.Accept(EMarker::Defeated);
                if (!state.bFinished && !state.bDefeatObserved)
                {
                    state.bDefeatObserved = true;
                    state.DefeatStartedAt = GetTickCount64();
                    state.Swing.Reset(ctx, state.Ids);
                }
                break;
            default: break;
            }
        }
        if (state.bFinished)
        {
            return;
        }
        state.SampleProgression();
        if (state.bFinished)
        {
            return;
        }
        if (state.WorldSeconds >= TimeoutSeconds)
        {
            const auto next = static_cast<EMarker>(state.NextMarker);
            const Container::String reason = Container::String("timeout_") + GetMarkerName(next);
            state.Fail(reason.c_str());
            return;
        }

        if (!state.bAtStrikePosition)
        {
            state.bAtStrikePosition = MoveToStrikePosition(*dog, worldDt);
        }
        if (state.bAtStrikePosition && state.NextMarker > static_cast<uint32_t>(EMarker::Attack) &&
            !state.bDefeatObserved)
        {
            if (!state.bSwingStarted)
            {
                state.bSwingStarted = true;
                if (!state.Swing.Start())
                {
                    state.Fail("swing");
                    return;
                }
            }
            const uint32_t previousCount = state.Swing.GetCompletedSwingCount();
            state.Swing.Tick(ctx, state.Ids, worldDt);
            if (state.Swing.IsFailed())
            {
                state.Fail("swing");
                return;
            }
            if (state.Swing.GetCompletedSwingCount() != previousCount)
            {
                state.bWaitingForSwing = true;
                state.SwingPause = 0.0;
            }
            else if (state.bWaitingForSwing)
            {
                state.SwingPause += worldDt;
                if (state.SwingPause >= SwingPauseSeconds)
                {
                    state.bWaitingForSwing = false;
                    if (!state.Swing.Start())
                    {
                        state.Fail("swing");
                        return;
                    }
                }
            }
        }

        if (state.bDefeatObserved && !state.CapturePath.empty())
        {
            // 固定刻みの deltaTime は実時間とは限らないため、撮影の待機だけ単調時計を使う。
            if (state.Capture.GetStatus() == CombatSliceCaptureStatus::Idle &&
                GetTickCount64() - state.DefeatStartedAt >= CaptureDelayMilliseconds &&
                !state.Capture.Request(ctx, state.CapturePath))
            {
                state.Fail("capture");
                return;
            }
            if (state.Capture.GetStatus() == CombatSliceCaptureStatus::Pending)
            {
                const auto status = state.Capture.Poll(ctx);
                if (status == CombatSliceCaptureStatus::Failed)
                {
                    state.Fail("capture");
                    return;
                }
                if (status == CombatSliceCaptureStatus::Saved)
                {
                    state.bCaptureSaved = true;
                }
            }
        }
        // 保存の通知は七つの状態の後へそろえ、画像の保存処理は毎フレーム進める。
        state.LogSavedCapture();
        if (state.NextMarker == static_cast<uint32_t>(EMarker::Count) &&
            (state.CapturePath.empty() || state.bCaptureLogged))
        {
            state.bFinished = true;
            NORVES_LOG_INFO("CombatSlice", "ENEMY_SMOKE stage=complete exit_code=0");
            ctx.EngineRef.RequestExit(0);
        }
    }

    bool EnemySmokeDriver::IsFinished() const
    {
        return m_State ? m_State->bFinished : m_bFinished;
    }

    int32_t EnemySmokeDriver::GetExitCode() const
    {
        return m_State ? m_State->ExitCode : m_ExitCode;
    }

    void EnemySmokeDriver::End(GameModeContext& ctx)
    {
        if (m_State)
        {
            m_State->Swing.Reset(ctx, m_State->Ids);
        }
        Unsubscribe();
    }

    void EnemySmokeDriver::Unsubscribe()
    {
        if (!m_State)
        {
            return;
        }
        auto& state = *m_State;
        state.bListening = false;
        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (module && module == state.Module)
        {
            module->GetEncounterService().OnZoneEntered.Remove(state.EnterCallback);
            module->GetProgressionState().OnFinalUnlocked.Remove(state.FinalCallback);
        }
        m_bFinished = state.bFinished;
        m_ExitCode = state.ExitCode;
        m_State.reset();
    }
} // namespace Game::GameModes
