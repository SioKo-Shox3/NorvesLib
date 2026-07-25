#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/World.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    Component::ScriptComponent* AddConfiguredComponent(Entity& owner, const char* path)
    {
        auto* component = new Component::ScriptComponent();
        component->getScriptPath() = Container::String(path);
        component->getScriptClassName() = Container::String("ScriptComponentMover");
        assert(owner.AddComponent(component));
        return component;
    }

    void AssertRejectedBindPreservesState(
        ScriptRuntime& runtime,
        World& world,
        const char* path)
    {
        const uint64_t generation = runtime.GetDiagnostics().ReloadGeneration;
        const uint32_t activeBindings = runtime.GetDiagnostics().ActiveBindingCount;
        Entity* owner = world.SpawnEntity<Entity>();
        assert(owner != nullptr);
        owner->SetPosition(NorvesLib::Math::Vector3(5.0f, 6.0f, 7.0f));
        const NorvesLib::Math::Vector3 position = owner->GetPosition();
        AddConfiguredComponent(*owner, path);
        assert(runtime.GetDiagnostics().ReloadGeneration == generation);
        assert(runtime.GetDiagnostics().ActiveBindingCount == activeBindings);
        assert(owner->GetPosition() == position);
    }
}

int main()
{
    std::cout << "ScriptPathResolutionTest start\n";
    const std::filesystem::path originalCwd = std::filesystem::current_path();
    const std::filesystem::path externalCwd = std::filesystem::temp_directory_path() / "NorvesLibM6ExternalCwd";
    std::filesystem::create_directories(externalCwd);
    std::filesystem::current_path(externalCwd);

    World world;
    world.Initialize();
    ScriptRuntime& runtime = GEngine.GetScriptRuntime();
    assert(runtime.Initialize(world) == EScriptRuntimeResult::Success);
    Entity* owner = world.SpawnEntity<Entity>();
    assert(owner != nullptr);
    Component::ScriptComponent* component = AddConfiguredComponent(*owner, "Scripts\\Test\\.\\ScriptComponentMover.as");
    assert(static_cast<Container::String>(component->getScriptPath()) == "Scripts/Test/ScriptComponentMover.as");

    AssertRejectedBindPreservesState(runtime, world, "");
    AssertRejectedBindPreservesState(runtime, world, "C:ScriptComponentMover.as");
    AssertRejectedBindPreservesState(runtime, world, "C:\\outside\\ScriptComponentMover.as");
    AssertRejectedBindPreservesState(runtime, world, "\\\\server\\share\\ScriptComponentMover.as");
    AssertRejectedBindPreservesState(runtime, world, "../Scripts/Test/ScriptComponentMover.as");
    AssertRejectedBindPreservesState(runtime, world, "..\\Scripts\\Test\\ScriptComponentMover.as");
    AssertRejectedBindPreservesState(runtime, world, "Scripts/../Test/ScriptComponentMover.as");

    const std::filesystem::path assetRoot(NORVES_ASSET_DIR);
    const std::filesystem::path outsideScript = externalCwd / "M6OutsideScript.as";
    {
        std::ofstream stream(outsideScript, std::ios::binary);
        stream << "class ScriptComponentMover { void Tick(EntityRef owner, float deltaTime) {} }";
    }
    const std::filesystem::path reparsePath = assetRoot / "Scripts/Test/M6OutsideScript.as";
    std::error_code symlinkError;
    std::filesystem::create_symlink(outsideScript, reparsePath, symlinkError);
    if (symlinkError)
    {
        std::cout << "reparse creation unavailable: " << symlinkError.message() << "\n";
    }
    else
    {
        AssertRejectedBindPreservesState(runtime, world, "Scripts/Test/M6OutsideScript.as");
        std::filesystem::remove(reparsePath);
    }
    std::filesystem::remove(outsideScript);

    world.Finalize();
    assert(runtime.Shutdown() == EScriptRuntimeResult::Success);
    std::filesystem::current_path(originalCwd);
    std::filesystem::remove(externalCwd);
    std::cout << "ScriptPathResolutionTest passed\n";
    return 0;
}
