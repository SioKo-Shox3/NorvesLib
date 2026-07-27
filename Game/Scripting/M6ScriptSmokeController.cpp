#include "Scripting/M6ScriptSmokeController.h"

#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Asset/AssetPath.h"
#include "Core/Public/Component/ScriptComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Scene/SceneSerializer.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;

namespace Game::Scripting
{
    namespace
    {
        constexpr const char* kOptionPrefix = "--m6-script-smoke=";
        constexpr uint64_t kScriptHashPollIntervalMilliseconds = 50;
        constexpr uint64_t kScriptHashFailureTimeoutMilliseconds = 2000;

        bool StartsWith(const String& value, const char* prefix)
        {
            size_t index = 0;
            while (prefix[index] != '\0')
            {
                if (index >= value.size() || value[index] != prefix[index])
                {
                    return false;
                }
                ++index;
            }
            return true;
        }

        uint64_t Fnv1a(const uint8_t* bytes, size_t size)
        {
            uint64_t hash = 14695981039346656037ull;
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        void EmitSmokeMarker(const char* format, ...)
        {
#if !NORVES_ENABLE_LOGGING
            char message[512]{};
            va_list arguments;
            va_start(arguments, format);
            const int messageLength = vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
            va_end(arguments);
            if (messageLength <= 0)
            {
                return;
            }

            HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            if (output == nullptr || output == INVALID_HANDLE_VALUE)
            {
                return;
            }
            DWORD written = 0;
            (void)WriteFile(output, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
            (void)WriteFile(output, "\r\n", 2, &written, nullptr);
#else
            (void)format;
#endif
        }
    }

    void M6ScriptSmokeController::Configure(const VariableArray<String>& arguments)
    {
        m_State = EState::Disabled;
        m_ResolvedScenePath = {};
        bool bSeen = false;
        for (const String& argument : arguments)
        {
            if (!StartsWith(argument, kOptionPrefix))
            {
                continue;
            }
            if (bSeen)
            {
                Fail("duplicate_option");
                return;
            }
            bSeen = true;

            const size_t prefixLength = 18;
            if (argument.size() <= prefixLength)
            {
                Fail("empty_option");
                return;
            }

            const String logicalPath = argument.substr(prefixLength);
            const AnsiString assetRoot = Asset::AssetFileReader::GetCompiledDefaultAssetRoot();
            const Asset::AssetPath path = Asset::AssetPath::Normalize(
                AnsiStringView(logicalPath.data(), logicalPath.size()),
                AnsiStringView(assetRoot.data(), assetRoot.size()));
            if (!path.IsValid() || path.GetKind() != Asset::AssetPath::PathKind::Logical || !path.HasResolvedPath())
            {
                Fail("invalid_scene_path");
                return;
            }
            m_ResolvedScenePath = String(path.GetResolvedPath().c_str());
            m_State = EState::AwaitInitialMovement;
        }
    }

    void M6ScriptSmokeController::Initialize()
    {
        if (m_State == EState::Failed)
        {
            if (Engine::GEngine != nullptr)
            {
                Engine::GEngine->RequestExit(1);
            }
            return;
        }
        if (!IsEnabled())
        {
            return;
        }
        if (Engine::GEngine == nullptr || !GEngine.GetScriptRuntime().IsInitialized())
        {
            Fail("runtime_unavailable");
            return;
        }

        (void)Entity::StaticClass();
        (void)Component::ScriptComponent::StaticClass();
        World& world = Engine::GEngine->GetWorld();
        const size_t baseRootCount = world.GetRootEntities().size();
        m_BaseRuntimeGeneration = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        m_BaseBindingCount = GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount;
        if (m_BaseRuntimeGeneration == ~uint64_t{0} || m_BaseBindingCount == ~uint32_t{0})
        {
            Fail("generation_or_binding_overflow");
            return;
        }

        Scene::SceneLoadStats loadStats;
        if (!Scene::SceneSerializer::LoadIntoWorld(world, m_ResolvedScenePath, &loadStats) || loadStats.LoadedRoots != 1)
        {
            Fail("scene_load");
            return;
        }

        const VariableArray<Entity*> roots = world.GetRootEntities();
        if (roots.size() != baseRootCount + 1 || roots.empty())
        {
            Fail("scene_root_count");
            return;
        }
        Entity* owner = roots[roots.size() - 1];
        Component::ScriptComponent* component = owner != nullptr ? owner->GetComponent<Component::ScriptComponent>() : nullptr;
        if (owner == nullptr || component == nullptr ||
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount != m_BaseBindingCount + 1)
        {
            Fail("scene_binding");
            return;
        }

        const AnsiString assetRoot = Asset::AssetFileReader::GetCompiledDefaultAssetRoot();
        const String scriptPath = component->getScriptPath();
        const Asset::AssetPath normalizedScript = Asset::AssetPath::Normalize(
            AnsiStringView(scriptPath.data(), scriptPath.size()),
            AnsiStringView(assetRoot.data(), assetRoot.size()));
        if (!normalizedScript.IsValid() || normalizedScript.GetKind() != Asset::AssetPath::PathKind::Logical ||
            !normalizedScript.HasResolvedPath())
        {
            Fail("script_path");
            return;
        }
        m_ResolvedScriptPath = String(normalizedScript.GetResolvedPath().c_str());
        m_OwnerObjectId = owner->GetObjectId();
        const NorvesLib::Math::Vector3 initialPosition = owner->GetPosition();
        m_InitialX = initialPosition.x;
        m_InitialY = initialPosition.y;
    }

    void M6ScriptSmokeController::Update()
    {
        if (!IsEnabled() || m_State == EState::Completed || m_State == EState::Failed)
        {
            return;
        }

        NorvesLib::Math::Vector3 position;
        if (!ResolveLoadedOwner(position))
        {
            Fail("owner_missing");
            return;
        }

        const ScriptRuntimeDiagnostics& diagnostics = GEngine.GetScriptRuntime().GetDiagnostics();
        if (m_State == EState::AwaitInitialMovement)
        {
            if (position.y != m_InitialY)
            {
                Fail("v1_changed_y");
                return;
            }
            if (position.x > m_InitialX)
            {
                if (position.z != 1.0f)
                {
                    Fail("v1_anchor");
                    return;
                }
                NORVES_LOG_INFO("M6", "M6_SCRIPT_SMOKE stage=ready_good pid=%lu generation=1 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f",
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                                position.x, position.y, position.z);
                EmitSmokeMarker("M6_SCRIPT_SMOKE stage=ready_good pid=%lu generation=1 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f",
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                                position.x, position.y, position.z);
                m_PreReloadX = position.x;
                m_State = EState::AwaitGoodReload;
            }
            return;
        }

        if (m_State == EState::AwaitGoodReload)
        {
            const uint64_t expectedGeneration = m_BaseRuntimeGeneration + 1;
            const uint32_t expectedBindingCount = m_BaseBindingCount + 1;
            if (diagnostics.ReloadGeneration == m_BaseRuntimeGeneration)
            {
                m_PreReloadX = position.x;
                return;
            }
            if (diagnostics.ReloadGeneration != expectedGeneration)
            {
                Fail("good_reload_generation");
                return;
            }
            if (diagnostics.ActiveBindingCount != expectedBindingCount)
            {
                Fail("good_reload_binding_count");
                return;
            }
            if (position.x != m_PreReloadX)
            {
                Fail("v2_changed_x");
                return;
            }
            m_GoodRuntimeGeneration = diagnostics.ReloadGeneration;
            m_GoodBindingCount = diagnostics.ActiveBindingCount;
            m_GoodX = m_PreReloadX;
            m_State = EState::AwaitV2StableMovement;
            return;
        }

        if (m_State == EState::AwaitV2StableMovement)
        {
            if (diagnostics.ReloadGeneration != m_GoodRuntimeGeneration ||
                diagnostics.ActiveBindingCount != m_GoodBindingCount)
            {
                Fail("v2_reload_changed_state");
                return;
            }
            if (position.x != m_GoodX)
            {
                Fail("v2_changed_x");
                return;
            }
            if (position.y > m_InitialY)
            {
                if (position.z != 2.0f)
                {
                    Fail("v2_anchor");
                    return;
                }
                uint64_t scriptHash = 0;
                const EScriptHashPollResult pollResult = PollScriptHash(scriptHash);
                if (pollResult == EScriptHashPollResult::Pending)
                {
                    return;
                }
                if (pollResult == EScriptHashPollResult::Failed)
                {
                    Fail("script_read");
                    return;
                }
                m_ReadyBadScriptHash = scriptHash;
                m_ReadyBadX = position.x;
                m_ReadyBadY = position.y;
                NORVES_LOG_INFO("M6", "M6_SCRIPT_SMOKE stage=ready_bad pid=%lu generation=2 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f v2_initial_x=%f",
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                                position.x, position.y, position.z, m_GoodX);
                EmitSmokeMarker("M6_SCRIPT_SMOKE stage=ready_bad pid=%lu generation=2 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f v2_initial_x=%f",
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                                position.x, position.y, position.z, m_GoodX);
                m_State = EState::AwaitBadCompile;
            }
            return;
        }

        if (m_State == EState::AwaitBadCompile)
        {
            if (!m_bBadSourceObserved)
            {
                uint64_t scriptHash = 0;
                const EScriptHashPollResult pollResult = PollScriptHash(scriptHash);
                if (pollResult == EScriptHashPollResult::Pending)
                {
                    return;
                }
                if (pollResult == EScriptHashPollResult::Failed)
                {
                    Fail("script_read");
                    return;
                }
                if (scriptHash != m_ReadyBadScriptHash)
                {
                    m_bBadSourceObserved = true;
                }
                return;
            }

            m_BadObservationSeconds += Engine::GEngine->GetDeltaTime();
            if (m_BadObservationSeconds < 1.0f)
            {
                return;
            }
            if (diagnostics.ReloadGeneration != m_GoodRuntimeGeneration ||
                diagnostics.ActiveBindingCount != m_GoodBindingCount || position.x != m_ReadyBadX ||
                position.y <= m_ReadyBadY || position.z != 2.0f)
            {
                Fail("bad_reload_changed_state");
                return;
            }
            Complete(position);
        }
    }

    void M6ScriptSmokeController::Shutdown()
    {
        m_OwnerObjectId = 0;
        m_ResolvedScenePath = {};
        m_ResolvedScriptPath = {};
        if (m_State != EState::Disabled)
        {
            m_State = EState::Disabled;
        }
    }

    void M6ScriptSmokeController::Fail(const char* reason)
    {
        m_State = EState::Failed;
        NORVES_LOG_ERROR("M6", "M6_SCRIPT_SMOKE stage=failure pid=%lu reason=%s exit_code=1",
                         static_cast<unsigned long>(GetCurrentProcessId()), reason);
        EmitSmokeMarker("M6_SCRIPT_SMOKE stage=failure pid=%lu reason=%s exit_code=1",
                        static_cast<unsigned long>(GetCurrentProcessId()), reason);
        if (Engine::GEngine != nullptr)
        {
            Engine::GEngine->RequestExit(1);
        }
    }

    void M6ScriptSmokeController::Complete(const NorvesLib::Math::Vector3& position)
    {
        const ScriptRuntimeDiagnostics& diagnostics = GEngine.GetScriptRuntime().GetDiagnostics();
        NORVES_LOG_INFO("M6", "M6_SCRIPT_SMOKE stage=complete pid=%lu generation=2 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f v2_initial_x=%f bad_source_observed=1 old_generation_continues=1 exit_code=0",
                        static_cast<unsigned long>(GetCurrentProcessId()),
                        static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                        position.x, position.y, position.z, m_GoodX);
        EmitSmokeMarker("M6_SCRIPT_SMOKE stage=complete pid=%lu generation=2 runtime_generation=%llu active_bindings=%u position_x=%f position_y=%f anchor_z=%f v2_initial_x=%f bad_source_observed=1 old_generation_continues=1 exit_code=0",
                        static_cast<unsigned long>(GetCurrentProcessId()),
                        static_cast<unsigned long long>(diagnostics.ReloadGeneration), diagnostics.ActiveBindingCount,
                        position.x, position.y, position.z, m_GoodX);
        m_State = EState::Completed;
        Engine::GEngine->RequestExit(0);
    }

    bool M6ScriptSmokeController::ReadScriptHash(uint64_t& outHash) const
    {
        outHash = 0;
        Asset::AssetFileReader reader;
        const Asset::AssetReadResult result = reader.Read(
            AnsiStringView(m_ResolvedScriptPath.data(), m_ResolvedScriptPath.size()));
        if (!result.Succeeded())
        {
            return false;
        }
        outHash = Fnv1a(result.Blob.GetData(), result.Blob.GetSize());
        return true;
    }

    M6ScriptSmokeController::EScriptHashPollResult M6ScriptSmokeController::PollScriptHash(uint64_t& outHash)
    {
        const uint64_t now = GetTickCount64();
        if (m_ScriptHashFailureDeadlineTick != 0 && now >= m_ScriptHashFailureDeadlineTick)
        {
            return EScriptHashPollResult::Failed;
        }
        if (m_NextScriptHashPollTick != 0 && now < m_NextScriptHashPollTick)
        {
            return EScriptHashPollResult::Pending;
        }

        if (ReadScriptHash(outHash))
        {
            m_NextScriptHashPollTick = 0;
            m_ScriptHashFailureDeadlineTick = 0;
            return EScriptHashPollResult::Ready;
        }

        // ReplaceFile can briefly leave the target unavailable or shared; retry that window, but bound permanent failures.
        if (m_ScriptHashFailureDeadlineTick == 0)
        {
            m_ScriptHashFailureDeadlineTick = now + kScriptHashFailureTimeoutMilliseconds;
        }
        m_NextScriptHashPollTick = now + kScriptHashPollIntervalMilliseconds;
        return EScriptHashPollResult::Pending;
    }

    bool M6ScriptSmokeController::ResolveLoadedOwner(NorvesLib::Math::Vector3& outPosition) const
    {
        if (Engine::GEngine == nullptr || m_OwnerObjectId == 0)
        {
            return false;
        }
        const VariableArray<Entity*> roots = Engine::GEngine->GetWorld().GetRootEntities();
        for (Entity* root : roots)
        {
            if (root != nullptr && root->GetObjectId() == m_OwnerObjectId)
            {
                outPosition = root->GetPosition();
                return true;
            }
        }
        return false;
    }
} // namespace Game::Scripting
