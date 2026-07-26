#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Scripting/AngelScriptEngineOwner.h"
#include "Scripting/ScriptSourceTracker.h"

#include <angelscript.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <process.h>

using namespace NorvesLib::Core;

namespace
{
    constexpr float RequiredPollIntervalSeconds = 250.0f / 1000.0f;

    bool Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cout << "ScriptHotReloadTest behavior mismatch: " << message << "\n";
        }
        return bCondition;
    }

    float GetPollIntervalSeconds()
    {
        return Scripting::ScriptSourceTracker::PollIntervalSeconds;
    }

    uint32_t GetLiveModuleCount()
    {
        asIScriptEngine* engine = Scripting::GetActiveAngelScriptEngine();
        Check(engine != nullptr, "active AngelScript engine was unavailable");
        return engine != nullptr ? static_cast<uint32_t>(engine->GetModuleCount()) : ~uint32_t{0};
    }

    class HotReloadFixture final
    {
    public:
        bool Initialize(bool bInitializeRuntime = true)
        {
            static uint32_t directoryCounter = 0;
            char directoryName[96]{};
            std::error_code error;
            do
            {
                const uint32_t counter = ++directoryCounter;
                sprintf_s(directoryName, ".m6-hotreload-%u-%u", static_cast<uint32_t>(_getpid()), counter);
                PhysicalDirectory = std::filesystem::path(NORVES_ASSET_DIR) / directoryName;
                if (std::filesystem::exists(PhysicalDirectory, error) || error)
                {
                    if (error)
                    {
                        return false;
                    }
                    continue;
                }
                if (!std::filesystem::create_directories(PhysicalDirectory, error) || error)
                {
                    return false;
                }
                m_bDirectoryOwned = true;
                break;
            }
            while (!m_bDirectoryOwned);

            LogicalDirectory = Container::String(directoryName);
            WorldInstance.Initialize();
            m_bWorldInitialized = true;
            if (!bInitializeRuntime)
            {
                return true;
            }
            m_bRuntimeCleanupRequired = true;
            if (GEngine.GetScriptRuntime().Initialize(WorldInstance) != EScriptRuntimeResult::Success)
            {
                return false;
            }
            return true;
        }

        bool WriteScript(Container::StringView fileName, Container::AnsiStringView sourceBody)
        {
            if (!m_bDirectoryOwned)
            {
                return false;
            }
            const std::filesystem::path path = PhysicalDirectory /
                std::filesystem::path(fileName.data(), fileName.data() + fileName.size());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return false;
            }

            const unsigned char bom[] = {0xef, 0xbb, 0xbf};
            stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            stream.write(sourceBody.data(), static_cast<std::streamsize>(sourceBody.size()));
            return stream.good();
        }

        Component::ScriptComponent* AddComponent(
            Entity& owner,
            Container::StringView logicalPath,
            Container::StringView className)
        {
            auto* component = new Component::ScriptComponent();
            Container::String path;
            path.append(logicalPath.data(), logicalPath.size());
            Container::String scriptClassName;
            scriptClassName.append(className.data(), className.size());
            component->getScriptPath() = std::move(path);
            component->getScriptClassName() = std::move(scriptClassName);
            if (!owner.AddComponent(component))
            {
                delete component;
                return nullptr;
            }
            return component;
        }

        Container::String MakeLogicalPath(const char* fileName) const
        {
            Container::String path = LogicalDirectory;
            path += "/";
            path += fileName;
            return path;
        }

        bool Cleanup()
        {
            if (m_bCleaned)
            {
                return true;
            }

            bool bSuccess = true;
            if (m_bWorldInitialized)
            {
                WorldInstance.Finalize();
                m_bWorldInitialized = false;
            }
            if (m_bRuntimeCleanupRequired)
            {
                const EScriptRuntimeResult shutdownResult = GEngine.GetScriptRuntime().Shutdown();
                if (shutdownResult != EScriptRuntimeResult::Success && shutdownResult != EScriptRuntimeResult::NotInitialized)
                {
                    return false;
                }
                m_bRuntimeCleanupRequired = false;
            }

            if (m_bDirectoryOwned)
            {
                std::error_code error;
                std::filesystem::remove_all(PhysicalDirectory, error);
                if (error)
                {
                    return false;
                }
                m_bDirectoryOwned = false;
            }
            m_bCleaned = true;
            return bSuccess;
        }

        ~HotReloadFixture()
        {
            if (!m_bCleaned)
            {
                Cleanup();
            }
        }

        World WorldInstance;
        std::filesystem::path PhysicalDirectory;
        Container::String LogicalDirectory;

    private:
        bool m_bWorldInitialized = false;
        bool m_bRuntimeCleanupRequired = false;
        bool m_bDirectoryOwned = false;
        bool m_bCleaned = false;
    };

    bool RunCadencePauseAndSameBytesContract()
    {
        constexpr Container::AnsiStringView cadenceV1(
            "class Cadence\r\n"
            "{\r\n"
            "    void BeginPlay(EntityRef owner) {}\r\n"
            "    void Tick(EntityRef owner, float deltaSeconds)\r\n"
            "    {\r\n"
            "        Vector3 position = owner.GetPosition();\r\n"
            "        position.x += 1.0f;\r\n"
            "        owner.SetPosition(position);\r\n"
            "    }\r\n"
            "}\r\n");
        constexpr Container::AnsiStringView cadenceV2(
            "class Cadence\r\n"
            "{\r\n"
            "    void BeginPlay(EntityRef owner)\r\n"
            "    {\r\n"
            "        Vector3 position = owner.GetPosition();\r\n"
            "        position.z = 42.0f;\r\n"
            "        owner.SetPosition(position);\r\n"
            "    }\r\n"
            "    void Tick(EntityRef owner, float deltaSeconds)\r\n"
            "    {\r\n"
            "        Vector3 position = owner.GetPosition();\r\n"
            "        position.x += 10.0f;\r\n"
            "        owner.SetPosition(position);\r\n"
            "    }\r\n"
            "}\r\n");

        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(), "hot reload fixture initialization failed");
        bPassed = Check(fixture.WriteScript("Cadence.as", cadenceV1), "Cadence v1 write failed") && bPassed;
        Entity* owner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        bPassed = Check(owner != nullptr, "Cadence owner creation failed") && bPassed;
        Component::ScriptComponent* component = bPassed
            ? fixture.AddComponent(*owner, fixture.MakeLogicalPath("Cadence.as"), "Cadence")
            : nullptr;
        bPassed = Check(component != nullptr, "Cadence initial binding failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 1,
            "Cadence initial binding count was not one") && bPassed;
        bPassed = Check(GetLiveModuleCount() == 1, "Cadence initial module count was not one") && bPassed;
        bPassed = Check(GetPollIntervalSeconds() == RequiredPollIntervalSeconds,
            "poll interval is not 250 milliseconds") && bPassed;

        const uint64_t reloadBefore = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        bPassed = Check(fixture.WriteScript("Cadence.as", cadenceV2), "Cadence v2 write failed") && bPassed;
        bPassed = Check(
            GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds() - 1.0f / 1000.0f) ==
                EScriptRuntimeResult::Success,
            "cadence pre-interval maintenance failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == reloadBefore,
            "cadence polled before 250 milliseconds") && bPassed;
        bPassed = Check(owner != nullptr && owner->GetPosition().z == 0.0f,
            "cadence changed during pre-interval maintenance") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(1.0f / 1000.0f) ==
            EScriptRuntimeResult::Success, "cadence due maintenance failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == reloadBefore + 1,
            "pause maintenance did not reload at accumulated 250 milliseconds") && bPassed;
        bPassed = Check(owner != nullptr && owner->GetPosition().z == 42.0f,
            "pause maintenance did not run the reloaded BeginPlay") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(owner->GetPosition().x == 10.0f, "Cadence reload did not update Tick") && bPassed;
        }

        const uint64_t reloadAfter = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        const uint32_t modulesAfter = GetLiveModuleCount();
        bPassed = Check(fixture.WriteScript("Cadence.as", cadenceV2), "Cadence identical v2 write failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) ==
            EScriptRuntimeResult::Success, "Cadence identical-bytes maintenance failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == reloadAfter &&
            GetLiveModuleCount() == modulesAfter, "identical source bytes triggered another reload") && bPassed;

        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "Cadence fixture cleanup failed") && bPassed;
        bPassed = Check(!std::filesystem::exists(directory), "Cadence fixture cleanup left an asset directory") && bPassed;
        return bPassed;
    }

    bool RunCompileRecoveryContract()
    {
        constexpr Container::AnsiStringView v1("class CompileRecovery { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 1.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView bad("class CompileRecovery { void Tick(EntityRef owner, float deltaSeconds) { this is invalid; } }\r\n");
        constexpr Container::AnsiStringView v2("class CompileRecovery { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 5.0f; owner.SetPosition(p); } }\r\n");
        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(), "compile recovery fixture initialization failed");
        bPassed = Check(fixture.WriteScript("CompileRecovery.as", v1), "CompileRecovery v1 write failed") && bPassed;
        Entity* owner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Component::ScriptComponent* component = owner != nullptr
            ? fixture.AddComponent(*owner, fixture.MakeLogicalPath("CompileRecovery.as"), "CompileRecovery") : nullptr;
        bPassed = Check(component != nullptr, "CompileRecovery initial bind failed") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
        }
        const uint64_t generation = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        const uint32_t bindings = GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount;
        const uint32_t modules = GetLiveModuleCount();
        bPassed = Check(fixture.WriteScript("CompileRecovery.as", bad), "CompileRecovery bad write failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::CompileFailed,
            "bad edit did not report CompileFailed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().LastResult == EScriptRuntimeResult::CompileFailed,
            "bad edit did not preserve CompileFailed diagnostics") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation &&
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == bindings && GetLiveModuleCount() == modules,
            "bad edit changed live binding state") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(owner->GetPosition().x == 2.0f, "bad edit did not preserve the old Tick behavior") && bPassed;
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().LastResult == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation && GetLiveModuleCount() == modules,
            "identical bad source bytes were not suppressed") && bPassed;
        bPassed = Check(fixture.WriteScript("CompileRecovery.as", v2), "CompileRecovery v2 write failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation + 1,
            "good edit after bad bytes did not recover") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(owner->GetPosition().x == 7.0f, "good recovery did not install Tick +5") && bPassed;
        }
        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "CompileRecovery fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "CompileRecovery fixture cleanup left an asset directory") && bPassed;
    }

    bool RunPropertyRecoveryContract()
    {
        constexpr Container::AnsiStringView propertyA("class PropertyA { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 2.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView propertyB("class PropertyB { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 7.0f; owner.SetPosition(p); } }\r\n");
        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(), "PROPERTY recovery fixture initialization failed");
        bPassed = Check(fixture.WriteScript("PropertyA.as", propertyA) && fixture.WriteScript("PropertyB.as", propertyB),
            "PROPERTY scripts write failed") && bPassed;
        Entity* owner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Component::ScriptComponent* component = owner != nullptr
            ? fixture.AddComponent(*owner, fixture.MakeLogicalPath("PropertyA.as"), "PropertyA") : nullptr;
        bPassed = Check(component != nullptr, "PropertyA initial bind failed") && bPassed;
        const uint64_t generation = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        const uint32_t modules = GetLiveModuleCount();
        if (component != nullptr)
        {
            component->getScriptPath() = Container::String("../escape.as");
            component->getScriptClassName() = Container::String("MissingClass");
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::LoadFailed &&
            GEngine.GetScriptRuntime().GetDiagnostics().LastResult == EScriptRuntimeResult::LoadFailed,
            "invalid PROPERTY edit did not return LoadFailed") && bPassed;
        bPassed = Check(component != nullptr && static_cast<Container::String>(component->getScriptPath()) == "../escape.as" &&
            static_cast<Container::String>(component->getScriptClassName()) == "MissingClass",
            "invalid PROPERTY edit was not displayed unchanged") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation && GetLiveModuleCount() == modules,
            "invalid PROPERTY edit changed approved state") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 1 && GetLiveModuleCount() == modules,
            "invalid PROPERTY edit corrupted active binding or module count") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(owner->GetPosition().x == 2.0f, "invalid PROPERTY edit did not preserve old Tick") && bPassed;
        }
        if (component != nullptr)
        {
            Container::String nonNormalized = fixture.LogicalDirectory;
            nonNormalized += "\\.\\PropertyB.as";
            component->getScriptPath() = std::move(nonNormalized);
            component->getScriptClassName() = Container::String("PropertyB");
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation + 1,
            "valid PROPERTY correction did not rebind") && bPassed;
        bPassed = Check(component != nullptr && static_cast<Container::String>(component->getScriptPath()) == fixture.MakeLogicalPath("PropertyB.as") &&
            static_cast<Container::String>(component->getScriptClassName()) == "PropertyB",
            "valid PROPERTY correction did not normalize display path") && bPassed;
        if (owner != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(owner->GetPosition().x == 9.0f, "valid PROPERTY correction did not install Tick +7") && bPassed;
        }
        const uint64_t approvedGeneration = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        const uint32_t approvedModules = GetLiveModuleCount();
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == approvedGeneration && GetLiveModuleCount() == approvedModules,
            "approved PROPERTY path triggered another reload") && bPassed;
        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "PROPERTY fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "PROPERTY fixture cleanup left an asset directory") && bPassed;
    }

    bool RunMixedTransactionAndBeginPlayFaultContract()
    {
        constexpr Container::AnsiStringView sharedOld("class SharedA { void EndPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.y = 1.0f; owner.SetPosition(p); } void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 1.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView sharedNew("class SharedA { void BeginPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); if (p.y == 1.0f) { p.y = 2.0f; owner.SetPosition(p); } } void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 10.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView configOld("class ConfigOld { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 2.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView configMissingTick("class ConfigNew { void TickRenamed(EntityRef owner, float deltaSeconds) {} }\r\n");
        constexpr Container::AnsiStringView configNew("class ConfigNew { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 20.0f; owner.SetPosition(p); } }\r\nclass ConfigBeginThrow { void BeginPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.z = 99.0f; owner.SetPosition(p); ConfigBeginThrow@ nullObject = null; nullObject.Throw(); } void Throw() {} void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 100.0f; owner.SetPosition(p); } }\r\n");
        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(), "mixed transaction fixture initialization failed");
        bPassed = Check(fixture.WriteScript("Shared.as", sharedOld) && fixture.WriteScript("ConfigOld.as", configOld) &&
            fixture.WriteScript("ConfigNew.as", configMissingTick), "mixed transaction initial writes failed") && bPassed;
        Entity* a1 = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Entity* a2 = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Entity* b = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Component::ScriptComponent* a1Component = a1 != nullptr ? fixture.AddComponent(*a1, fixture.MakeLogicalPath("Shared.as"), "SharedA") : nullptr;
        Component::ScriptComponent* a2Component = a2 != nullptr ? fixture.AddComponent(*a2, fixture.MakeLogicalPath("Shared.as"), "SharedA") : nullptr;
        Component::ScriptComponent* bComponent = b != nullptr ? fixture.AddComponent(*b, fixture.MakeLogicalPath("ConfigOld.as"), "ConfigOld") : nullptr;
        bPassed = Check(a1Component != nullptr && a2Component != nullptr && bComponent != nullptr, "mixed transaction initial bind failed") && bPassed;
        const uint64_t generation = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        bPassed = Check(fixture.WriteScript("Shared.as", sharedNew), "mixed shared edit write failed") && bPassed;
        if (bComponent != nullptr)
        {
            bComponent->getScriptPath() = fixture.MakeLogicalPath("ConfigNew.as");
            bComponent->getScriptClassName() = Container::String("ConfigNew");
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::BindFailed,
            "mixed transaction named Tick mismatch was not rejected") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation &&
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 3 && GetLiveModuleCount() == 3,
            "shared-source slots were partially committed") && bPassed;
        if (a1 != nullptr && a2 != nullptr && b != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(a1->GetPosition().x == 1.0f && a2->GetPosition().x == 1.0f && b->GetPosition().x == 2.0f &&
                a1->GetPosition().y == 0.0f && a2->GetPosition().y == 0.0f && b->GetPosition().y == 0.0f,
                "mixed failure called old EndPlay or changed a live Tick") && bPassed;
        }
        bPassed = Check(fixture.WriteScript("ConfigNew.as", configNew), "mixed recovery ConfigNew write failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation + 1 &&
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 3 && GetLiveModuleCount() == 3,
            "mixed transaction recovery did not include every dirty binding") && bPassed;
        if (a1 != nullptr && a2 != nullptr && b != nullptr)
        {
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(a1->GetPosition().x == 11.0f && a2->GetPosition().x == 11.0f && b->GetPosition().x == 22.0f &&
                a1->GetPosition().y == 2.0f && a2->GetPosition().y == 2.0f,
                "mixed transaction recovery did not preserve lifecycle order") && bPassed;
        }
        const uint64_t faultGeneration = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        if (bComponent != nullptr)
        {
            bComponent->getScriptClassName() = Container::String("ConfigBeginThrow");
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::ExecutionFailed &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == faultGeneration + 1 &&
            GEngine.GetScriptRuntime().GetDiagnostics().LastResult == EScriptRuntimeResult::ExecutionFailed &&
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 3 && GetLiveModuleCount() == 3,
            "BeginPlay exception rolled back a committed reload") && bPassed;
        if (a1 != nullptr && a2 != nullptr && b != nullptr)
        {
            bPassed = Check(b->GetPosition().z == 99.0f, "BeginPlay exception rolled back its side effect") && bPassed;
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(a1->GetPosition().x == 21.0f && a2->GetPosition().x == 21.0f && b->GetPosition().x == 22.0f,
                "BeginPlay fault stopped a healthy peer") && bPassed;
        }
        if (bComponent != nullptr)
        {
            bComponent->getScriptClassName() = Container::String("ConfigNew");
        }
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success,
            "successful rebind did not clear the faulted slot") && bPassed;
        if (b != nullptr)
        {
            const float xBeforeRebindTick = b->GetPosition().x;
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(b->GetPosition().x == xBeforeRebindTick + 20.0f,
                "successful rebind did not restore ConfigNew Tick") && bPassed;
        }
        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "mixed transaction fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "mixed transaction fixture cleanup left an asset directory") && bPassed;
    }

    bool RunDirectHandleContract()
    {
        constexpr Container::AnsiStringView v1("class DirectReload { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 1.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView v2("class DirectReload { void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 2.0f; owner.SetPosition(p); } }\r\n");
        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(false), "direct handle fixture initialization failed");
        bPassed = Check(fixture.WriteScript("DirectReload.as", v1), "direct handle v1 write failed") && bPassed;
        Entity* owner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Component::ScriptComponent* component = owner != nullptr
            ? fixture.AddComponent(*owner, fixture.MakeLogicalPath("DirectReload.as"), "DirectReload") : nullptr;
        bPassed = Check(component != nullptr, "direct handle component creation failed") && bPassed;
        {
            ScriptRuntime runtime;
            bPassed = Check(runtime.Initialize(fixture.WorldInstance) == EScriptRuntimeResult::Success,
                "direct runtime initialization failed") && bPassed;
            ScriptBindingHandle originalHandle;
            bPassed = Check(component != nullptr && runtime.BindComponent(*component, originalHandle) == EScriptRuntimeResult::Success,
                "direct initial bind failed") && bPassed;
            const ScriptBindingHandle staleHandle = originalHandle;
            const uint64_t reloadBefore = runtime.GetDiagnostics().ReloadGeneration;
            bPassed = Check(fixture.WriteScript("DirectReload.as", v2), "direct handle v2 write failed") && bPassed;
            bPassed = Check(runtime.BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::Success &&
                runtime.GetDiagnostics().ReloadGeneration == reloadBefore + 1,
                "direct handle reload did not advance generation") && bPassed;
            bPassed = Check(runtime.TickComponent(staleHandle, 1.0f) == EScriptRuntimeResult::InvalidHandle,
                "stale pre-reload handle remained valid after generation swap") && bPassed;
            bPassed = Check(runtime.Shutdown() == EScriptRuntimeResult::Success,
                "direct runtime shutdown before World::Finalize failed") && bPassed;
        }
        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "direct handle fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "direct handle fixture cleanup left an asset directory") && bPassed;
    }

    bool RunEndPlayPeerContinuationContract()
    {
        constexpr Container::AnsiStringView throwingOld("class ThrowingEnd { void EndPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.y = 11.0f; owner.SetPosition(p); ThrowingEnd@ nullObject = null; nullObject.Throw(); } void Throw() {} void Tick(EntityRef owner, float deltaSeconds) { } }\r\n");
        constexpr Container::AnsiStringView healthyOld("class HealthyEnd { void EndPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.y = 21.0f; owner.SetPosition(p); } void Tick(EntityRef owner, float deltaSeconds) { } }\r\n");
        constexpr Container::AnsiStringView throwingNew("class ThrowingEnd { void BeginPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.z = 31.0f; owner.SetPosition(p); } void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 10.0f; owner.SetPosition(p); } }\r\n");
        constexpr Container::AnsiStringView healthyNew("class HealthyEnd { void BeginPlay(EntityRef owner) { Vector3 p = owner.GetPosition(); p.z = 41.0f; owner.SetPosition(p); } void Tick(EntityRef owner, float deltaSeconds) { Vector3 p = owner.GetPosition(); p.x += 20.0f; owner.SetPosition(p); } }\r\n");
        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(), "EndPlay peer fixture initialization failed");
        bPassed = Check(fixture.WriteScript("ThrowingEnd.as", throwingOld) && fixture.WriteScript("HealthyEnd.as", healthyOld),
            "EndPlay peer initial writes failed") && bPassed;
        Entity* throwingOwner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Entity* healthyOwner = bPassed ? fixture.WorldInstance.SpawnEntity<Entity>() : nullptr;
        Component::ScriptComponent* throwingComponent = throwingOwner != nullptr
            ? fixture.AddComponent(*throwingOwner, fixture.MakeLogicalPath("ThrowingEnd.as"), "ThrowingEnd") : nullptr;
        Component::ScriptComponent* healthyComponent = healthyOwner != nullptr
            ? fixture.AddComponent(*healthyOwner, fixture.MakeLogicalPath("HealthyEnd.as"), "HealthyEnd") : nullptr;
        bPassed = Check(throwingComponent != nullptr && healthyComponent != nullptr, "EndPlay peer initial bind failed") && bPassed;
        const uint64_t generation = GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration;
        bPassed = Check(fixture.WriteScript("ThrowingEnd.as", throwingNew) && fixture.WriteScript("HealthyEnd.as", healthyNew),
            "EndPlay peer reload writes failed") && bPassed;
        bPassed = Check(GEngine.GetScriptRuntime().BeginFrameMaintenance(GetPollIntervalSeconds()) == EScriptRuntimeResult::ExecutionFailed &&
            GEngine.GetScriptRuntime().GetDiagnostics().LastResult == EScriptRuntimeResult::ExecutionFailed &&
            GEngine.GetScriptRuntime().GetDiagnostics().ReloadGeneration == generation + 1 &&
            GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount == 2 && GetLiveModuleCount() == 2,
            "old EndPlay exception stopped peer retirement") && bPassed;
        if (throwingOwner != nullptr && healthyOwner != nullptr)
        {
            bPassed = Check(throwingOwner->GetPosition().y == 11.0f && healthyOwner->GetPosition().y == 21.0f &&
                throwingOwner->GetPosition().z == 31.0f && healthyOwner->GetPosition().z == 41.0f,
                "old EndPlay exception stopped new BeginPlay peers") && bPassed;
            fixture.WorldInstance.Tick(1.0f);
            bPassed = Check(throwingOwner->GetPosition().x == 10.0f && healthyOwner->GetPosition().x == 20.0f,
                "old EndPlay exception disabled a committed new Tick") && bPassed;
        }
        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "EndPlay peer fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "EndPlay peer fixture cleanup left an asset directory") && bPassed;
    }

    bool RunScriptSourceTrackerContract()
    {
        constexpr Container::AnsiStringView initialSource("class Shared {}\r\n");
        constexpr Container::AnsiStringView changedSource("class Shared { int value = 1; }\r\n");

        HotReloadFixture fixture;
        bool bPassed = Check(fixture.Initialize(false), "tracker fixture initialization failed");
        const Container::String logicalPath = fixture.MakeLogicalPath("Shared.as");
        bPassed = Check(fixture.WriteScript("Shared.as", initialSource), "tracker initial source write failed") && bPassed;

        Scripting::ScriptSourceTracker tracker;
        Scripting::ScriptSourceSnapshot initialSnapshot;
        bPassed = Check(tracker.ReadSource(logicalPath, initialSnapshot) == Scripting::EScriptSourceReadResult::Success,
            "tracker initial source read failed") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }

        tracker.ReserveBindingCapacity(2);
        Scripting::ScriptSourceApproval initialFirst;
        initialFirst.SlotIndex = 3;
        initialFirst.PreviousGeneration = 7;
        initialFirst.Generation = 7;
        initialFirst.ApprovedPropertyPath = initialSnapshot.LogicalPath;
        initialFirst.ApprovedClassName = Container::String("SharedFirst");
        initialFirst.ApprovedLogicalPath = initialSnapshot.LogicalPath;
        initialFirst.ApprovedContentHash = initialSnapshot.ContentHash;
        tracker.RegisterBinding(std::move(initialFirst));

        Scripting::ScriptSourceApproval initialSecond;
        initialSecond.SlotIndex = 9;
        initialSecond.PreviousGeneration = 11;
        initialSecond.Generation = 11;
        initialSecond.ApprovedPropertyPath = initialSnapshot.LogicalPath;
        initialSecond.ApprovedClassName = Container::String("SharedSecond");
        initialSecond.ApprovedLogicalPath = initialSnapshot.LogicalPath;
        initialSecond.ApprovedContentHash = initialSnapshot.ContentHash;
        tracker.RegisterBinding(std::move(initialSecond));

        bPassed = Check(fixture.WriteScript("Shared.as", changedSource), "tracker changed source write failed") && bPassed;
        Container::String firstClass("SharedFirst");
        Container::String secondClass("SharedSecond");
        Scripting::ScriptSourceBindingView bindings[] =
        {
            {3, 7, Container::StringView(logicalPath.data(), logicalPath.size()), Container::StringView(firstClass.data(), firstClass.size())},
            {9, 11, Container::StringView(logicalPath.data(), logicalPath.size()), Container::StringView(secondClass.data(), secondClass.size())}
        };

        Scripting::ScriptSourcePollBatch batch;
        bPassed = Check(tracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(bindings), batch) ==
                Scripting::EScriptSourcePollResult::Changes,
            "tracker changed source was not dirty") && bPassed;
        bPassed = Check(batch.Changes.size() == 2 && batch.Sources.size() == 1 &&
            batch.Changes[0].SourceIndex == batch.Changes[1].SourceIndex,
            "tracker did not deduplicate the shared requested path") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }

        tracker.RejectBatch(batch.Fingerprint);
        bPassed = Check(tracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(bindings), batch) ==
                Scripting::EScriptSourcePollResult::NoChanges && batch.Changes.empty(),
            "tracker did not suppress the rejected fingerprint") && bPassed;

        Container::String recoveredClass("RecoveredClass");
        bindings[1].ScriptClassName = Container::StringView(recoveredClass.data(), recoveredClass.size());
        bPassed = Check(tracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(bindings), batch) ==
                Scripting::EScriptSourcePollResult::Changes && batch.Changes.size() == 2,
            "tracker did not resubmit every dirty binding after configuration recovery") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }

        Scripting::ScriptSourceApproval approvals[2];
        approvals[0].SlotIndex = 3;
        approvals[0].PreviousGeneration = 7;
        approvals[0].Generation = 8;
        approvals[0].ApprovedPropertyPath = batch.Sources[0].LogicalPath;
        approvals[0].ApprovedClassName = firstClass;
        approvals[0].ApprovedLogicalPath = batch.Sources[0].LogicalPath;
        approvals[0].ApprovedContentHash = batch.Sources[0].ContentHash;
        approvals[1].SlotIndex = 9;
        approvals[1].PreviousGeneration = 11;
        approvals[1].Generation = 12;
        approvals[1].ApprovedPropertyPath = batch.Sources[0].LogicalPath;
        approvals[1].ApprovedClassName = recoveredClass;
        approvals[1].ApprovedLogicalPath = batch.Sources[0].LogicalPath;
        approvals[1].ApprovedContentHash = batch.Sources[0].ContentHash;
        bPassed = Check(tracker.CanApproveBatch(Container::Span<const Scripting::ScriptSourceApproval>(approvals)),
            "tracker rejected valid fresh generations") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }
        tracker.ApproveBatch(Container::Span<Scripting::ScriptSourceApproval>(approvals));

        Scripting::ScriptSourceBindingView freshBindings[] =
        {
            {3, 8, Container::StringView(logicalPath.data(), logicalPath.size()), Container::StringView(firstClass.data(), firstClass.size())},
            {9, 12, Container::StringView(logicalPath.data(), logicalPath.size()), Container::StringView(recoveredClass.data(), recoveredClass.size())}
        };
        bPassed = Check(tracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(freshBindings), batch) ==
                Scripting::EScriptSourcePollResult::NoChanges && batch.Changes.empty(),
            "tracker reported approved source and configuration as dirty") && bPassed;
        Container::String firstClassSuffix("SharedFirstSuffix");
        freshBindings[0].ScriptClassName = Container::StringView(firstClassSuffix.data(), firstClass.size());
        bPassed = Check(tracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(freshBindings), batch) ==
                Scripting::EScriptSourcePollResult::NoChanges && batch.Changes.empty(),
            "tracker treated a bounded class-name view as a C string") && bPassed;

        const bool bPartialViewPassed = bPassed;
        bPassed = true;
        constexpr Container::AnsiStringView collisionInitialSource("initial");
        constexpr Container::AnsiStringView collisionShortSource("Y");
        const Container::String collisionFirstPath = fixture.MakeLogicalPath("CollisionFirst.as");
        const Container::String collisionSecondPath = fixture.MakeLogicalPath("CollisionSecond.as");
        Container::String collisionInvalidPath("../CollisionEscape.as");
        Container::String emptyLogicalPath;
        Container::String collisionFirstClass("CollisionFirst");
        Container::String collisionSecondClass("CollisionSecond");
        Container::String collisionRecoveredClass("CollisionRecovered");
        bPassed = Check(fixture.WriteScript("CollisionFirst.as", collisionInitialSource) &&
            fixture.WriteScript("CollisionSecond.as", Container::AnsiStringView("")),
            "fingerprint collision fixture write failed") && bPassed;

        Scripting::ScriptSourceTracker collisionTracker;
        Scripting::ScriptSourceSnapshot collisionFirstInitial;
        Scripting::ScriptSourceSnapshot collisionSecondInitial;
        bPassed = Check(collisionTracker.ReadSource(collisionFirstPath, collisionFirstInitial) ==
            Scripting::EScriptSourceReadResult::Success &&
            collisionTracker.ReadSource(collisionSecondPath, collisionSecondInitial) ==
                Scripting::EScriptSourceReadResult::Success,
            "fingerprint collision initial source read failed") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }

        collisionTracker.ReserveBindingCapacity(2);
        Scripting::ScriptSourceApproval collisionFirstApproval;
        collisionFirstApproval.SlotIndex = 41;
        collisionFirstApproval.PreviousGeneration = 5;
        collisionFirstApproval.Generation = 5;
        collisionFirstApproval.ApprovedPropertyPath = collisionFirstInitial.LogicalPath;
        collisionFirstApproval.ApprovedClassName = collisionFirstClass;
        collisionFirstApproval.ApprovedLogicalPath = collisionFirstInitial.LogicalPath;
        collisionFirstApproval.ApprovedContentHash = collisionFirstInitial.ContentHash;
        collisionTracker.RegisterBinding(std::move(collisionFirstApproval));
        Scripting::ScriptSourceApproval collisionSecondApproval;
        collisionSecondApproval.SlotIndex = 0xffffffffu;
        collisionSecondApproval.PreviousGeneration = 0xffffffffu;
        collisionSecondApproval.Generation = 0xffffffffu;
        collisionSecondApproval.ApprovedPropertyPath = collisionSecondInitial.LogicalPath;
        collisionSecondApproval.ApprovedClassName = collisionSecondClass;
        collisionSecondApproval.ApprovedLogicalPath = collisionSecondInitial.LogicalPath;
        collisionSecondApproval.ApprovedContentHash = collisionSecondInitial.ContentHash;
        collisionTracker.RegisterBinding(std::move(collisionSecondApproval));

        char collisionPayload[512]{};
        size_t collisionPayloadSize = 0;
        const auto appendUint32 = [&collisionPayload, &collisionPayloadSize](uint32_t value)
        {
            for (size_t index = 0; index < sizeof(value); ++index)
            {
                collisionPayload[collisionPayloadSize++] = static_cast<char>(value >> (index * 8));
            }
        };
        const auto appendString = [&collisionPayload, &collisionPayloadSize](const Container::String& value)
        {
            std::memcpy(collisionPayload + collisionPayloadSize, value.data(), value.size());
            collisionPayloadSize += value.size();
            collisionPayload[collisionPayloadSize++] = static_cast<char>(0xff);
        };
        collisionPayload[collisionPayloadSize++] = 'Y';
        appendUint32(0xffffffffu);
        appendUint32(0xffffffffu);
        appendString(collisionInvalidPath);
        appendString(collisionRecoveredClass);
        collisionPayload[collisionPayloadSize++] = static_cast<char>(Scripting::EScriptSourceReadResult::InvalidPath);
        appendString(emptyLogicalPath);
        bPassed = Check(fixture.WriteScript("CollisionFirst.as",
            Container::AnsiStringView(collisionPayload, collisionPayloadSize)),
            "fingerprint collision first payload write failed") && bPassed;

        Scripting::ScriptSourceBindingView collisionBindings[] =
        {
            {41, 5, Container::StringView(collisionFirstPath.data(), collisionFirstPath.size()),
                Container::StringView(collisionFirstClass.data(), collisionFirstClass.size())},
            {0xffffffffu, 0xffffffffu, Container::StringView(collisionSecondPath.data(), collisionSecondPath.size()),
                Container::StringView(collisionSecondClass.data(), collisionSecondClass.size())}
        };
        Scripting::ScriptSourcePollBatch collisionBatch;
        bPassed = Check(collisionTracker.Poll(Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(collisionBindings), collisionBatch) ==
                Scripting::EScriptSourcePollResult::Changes && collisionBatch.Changes.size() == 1,
            "fingerprint collision first dirty set was unexpected") && bPassed;
        if (!bPassed)
        {
            return Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        }
        collisionTracker.RejectBatch(collisionBatch.Fingerprint);
        bPassed = Check(fixture.WriteScript("CollisionFirst.as", collisionShortSource),
            "fingerprint collision short payload write failed") && bPassed;
        collisionBindings[1].ScriptClassName = Container::StringView(
            collisionRecoveredClass.data(), collisionRecoveredClass.size());
        collisionBindings[1].ScriptPath = Container::StringView(
            collisionInvalidPath.data(), collisionInvalidPath.size());
        const Scripting::EScriptSourcePollResult collisionResult = collisionTracker.Poll(
            Scripting::ScriptSourceTracker::PollIntervalSeconds,
            Container::Span<const Scripting::ScriptSourceBindingView>(collisionBindings), collisionBatch);
        if (collisionBatch.Sources.size() != 2)
        {
            return Check(false, "fingerprint collision second scan did not read both sources") && bPassed;
        }
        bPassed = Check(collisionResult == Scripting::EScriptSourcePollResult::Changes && collisionBatch.Changes.size() == 2,
            "tracker suppressed a distinct dirty set with an ambiguous fingerprint") && bPassed;
        bPassed = bPartialViewPassed && bPassed;

        const std::filesystem::path directory = fixture.PhysicalDirectory;
        bPassed = Check(fixture.Cleanup(), "tracker fixture cleanup failed") && bPassed;
        return Check(!std::filesystem::exists(directory), "tracker fixture cleanup left an asset directory") && bPassed;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 2 && std::strcmp(arguments[1], "--tracker-contract") == 0)
    {
        const bool bPassed = Check(GetPollIntervalSeconds() == RequiredPollIntervalSeconds,
            "tracker poll interval is not 250 milliseconds") && RunScriptSourceTrackerContract();
        std::cout << (bPassed ? "ScriptSourceTrackerContractTest passed\n" : "ScriptSourceTrackerContractTest failed\n");
        return bPassed ? 0 : 1;
    }

    std::cout << "ScriptHotReloadTest start\n";
    bool bPassed = RunCadencePauseAndSameBytesContract();
    bPassed = RunCompileRecoveryContract() && bPassed;
    bPassed = RunPropertyRecoveryContract() && bPassed;
    bPassed = RunMixedTransactionAndBeginPlayFaultContract() && bPassed;
    bPassed = RunDirectHandleContract() && bPassed;
    bPassed = RunEndPlayPeerContinuationContract() && bPassed;
    std::cout << (bPassed ? "ScriptHotReloadTest passed\n" : "ScriptHotReloadTest failed\n");
    return bPassed ? 0 : 1;
}
