#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "M6AngelScriptAcceptanceTest failed: " << message << "\n";
            return false;
        }
        return true;
    }

    Container::String ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Container::String result;
        char buffer[1024]{};
        while (stream)
        {
            stream.read(buffer, sizeof(buffer));
            result.append(buffer, static_cast<size_t>(stream.gcount()));
        }
        return result;
    }

    bool CheckRejectedPathPreservesState(
        World& world,
        ScriptRuntime& runtime,
        Entity& demoOwner,
        const char* path)
    {
        const uint64_t generation = runtime.GetDiagnostics().ReloadGeneration;
        const uint32_t bindingCount = runtime.GetDiagnostics().ActiveBindingCount;
        const NorvesLib::Math::Vector3 demoPosition = demoOwner.GetPosition();
        Entity* rejectedOwner = world.SpawnEntity<Entity>();
        if (!Check(rejectedOwner != nullptr, "path rejection owner spawn"))
        {
            return false;
        }
        rejectedOwner->SetPosition(NorvesLib::Math::Vector3(5.0f, 6.0f, 7.0f));
        const NorvesLib::Math::Vector3 rejectedPosition = rejectedOwner->GetPosition();
        auto* component = new Component::ScriptComponent();
        component->getScriptPath() = Container::String(path);
        component->getScriptClassName() = Container::String("M6Mover");
        if (!Check(rejectedOwner->AddComponent(component), "path rejection component attach"))
        {
            delete component;
            return false;
        }
        return Check(runtime.GetDiagnostics().ReloadGeneration == generation, "rejected path generation invariant") &&
               Check(runtime.GetDiagnostics().ActiveBindingCount == bindingCount, "rejected path binding invariant") &&
               Check(rejectedOwner->GetPosition() == rejectedPosition, "rejected owner position invariant") &&
               Check(demoOwner.GetPosition() == demoPosition, "demo owner survival invariant") &&
               Check(demoOwner.GetComponent<Component::ScriptComponent>() != nullptr, "demo binding survival invariant");
    }

    bool RunTest()
    {
        const std::filesystem::path originalCwd = std::filesystem::current_path();
        const std::filesystem::path externalCwd = std::filesystem::temp_directory_path() /
            ("NorvesLibM6Acceptance-" + std::to_string(std::rand()));
        std::filesystem::create_directories(externalCwd);
        std::filesystem::current_path(externalCwd);

        bool bPassed = true;
        World world;
        world.Initialize();
        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        bPassed &= Check(runtime.Initialize(world) == EScriptRuntimeResult::Success, "runtime initialization");
        (void)Entity::StaticClass();
        (void)Component::ScriptComponent::StaticClass();

        const std::filesystem::path scenePath = std::filesystem::path(NORVES_ASSET_DIR) /
            "Scenes/M6AngelScriptDemo.scene.json";
        const Container::String sceneText = ReadFile(scenePath);
        bPassed &= Check(!sceneText.empty(), "tracked scene is missing or empty");

        Scene::SceneDocument document;
        Container::String parseError;
        bPassed &= Check(Scene::SceneSerializer::TryParseJson(sceneText, document, &parseError), "scene JSON parse");
        Container::VariableArray<EntitySubtreeSnapshot> reconciledRoots;
        Scene::SceneLoadStats reconcileStats;
        bPassed &= Check(Scene::SceneSerializer::ReconcileWithSchema(document, reconciledRoots, reconcileStats), "scene schema reconcile");
        bPassed &= Check(reconciledRoots.size() == 1, "scene reconciles one root");

        const uint32_t baseBindings = runtime.GetDiagnostics().ActiveBindingCount;
        Scene::SceneLoadStats loadStats;
        bPassed &= Check(Scene::SceneSerializer::LoadIntoWorld(world, Container::String(scenePath.string()), &loadStats),
                         "scene load");
        bPassed &= Check(loadStats.LoadedRoots == 1, "scene loads one root");
        Container::VariableArray<Entity*> roots = world.GetRootEntities();
        bPassed &= Check(roots.size() == 1 && roots[0] != nullptr, "world has one demo root");
        Entity* owner = roots.empty() ? nullptr : roots[0];
        bPassed &= Check(owner != nullptr && owner->GetComponent<Component::ScriptComponent>() != nullptr,
                         "demo root has ScriptComponent");
        bPassed &= Check(runtime.GetDiagnostics().ActiveBindingCount == baseBindings + 1, "demo binding added");

        bPassed &= CheckRejectedPathPreservesState(world, runtime, *owner, "C:/outside/M6Mover.as");
        bPassed &= CheckRejectedPathPreservesState(world, runtime, *owner, "../Scripts/M6Mover.as");
        bPassed &= CheckRejectedPathPreservesState(world, runtime, *owner, "..\\Scripts\\M6Mover.as");
        bPassed &= CheckRejectedPathPreservesState(world, runtime, *owner, "Scripts/../../M6Mover.as");

        const NorvesLib::Math::Vector3 initialPosition = owner != nullptr ? owner->GetPosition() : NorvesLib::Math::Vector3();
        runtime.BeginFrameMaintenance(1.0f);
        world.Tick(1.0f);
        runtime.EndFrameMaintenance();
        const NorvesLib::Math::Vector3 finalPosition = owner != nullptr ? owner->GetPosition() : NorvesLib::Math::Vector3();
        bPassed &= Check(finalPosition.x > initialPosition.x, "v1 moves X");
        bPassed &= Check(finalPosition.y == initialPosition.y, "v1 keeps Y");
        bPassed &= Check(finalPosition.z == 1.0f, "v1 sets anchor z=1");

        world.Finalize();
        bPassed &= Check(runtime.GetDiagnostics().ActiveBindingCount == 0, "bindings removed on world finalization");
        bPassed &= Check(runtime.Shutdown() == EScriptRuntimeResult::Success, "runtime shutdown");
        std::filesystem::current_path(originalCwd);
        std::filesystem::remove_all(externalCwd);
        return bPassed;
    }
}

int main()
{
    std::cout << "M6AngelScriptAcceptanceTest start\n";
    const bool bPassed = RunTest();
    std::cout << (bPassed ? "M6AngelScriptAcceptanceTest passed\n" : "M6AngelScriptAcceptanceTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
