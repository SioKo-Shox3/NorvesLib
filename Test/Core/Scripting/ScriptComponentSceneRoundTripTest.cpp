#include "Component/ScriptComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/Entity.h"
#include "Object/Reflection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    constexpr const char* ScenePath = "ScriptComponentSceneRoundTripTest.scene.json";

    Container::String ReadFile(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        Container::String text;
        char buffer[1024];
        while (stream)
        {
            stream.read(buffer, sizeof(buffer));
            text.append(buffer, static_cast<size_t>(stream.gcount()));
        }
        return text;
    }
}

int main()
{
    std::cout << "ScriptComponentSceneRoundTripTest start\n";
    const IClass* coldRegisteredClass = ClassRegistry::Get().FindClass(Identity("ScriptComponent"));
    assert(coldRegisteredClass != nullptr);

    World sourceWorld;
    sourceWorld.Initialize();
    ScriptRuntime& runtime = GEngine.GetScriptRuntime();
    assert(runtime.Initialize(sourceWorld) == EScriptRuntimeResult::Success);
    Entity* sourceOwner = sourceWorld.SpawnEntity<Entity>();
    assert(sourceOwner != nullptr);
    auto* sourceComponent = new Component::ScriptComponent();
    sourceComponent->getScriptPath() = Container::String("Scripts/Test/ScriptComponentMover.as");
    sourceComponent->getScriptClassName() = Container::String("ScriptComponentMover");
    assert(sourceOwner->AddComponent(sourceComponent));
    assert(runtime.GetDiagnostics().ActiveBindingCount == 1);
    assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(ScenePath)));

    const Container::String sceneText = ReadFile(ScenePath);
    assert(sceneText.find("ScriptPath") != Container::String::npos);
    assert(sceneText.find("ScriptClassName") != Container::String::npos);
    assert(sceneText.find("ScriptBindingHandle") == Container::String::npos);

    sourceWorld.Finalize();
    assert(runtime.GetDiagnostics().ActiveBindingCount == 0);
    assert(runtime.Shutdown() == EScriptRuntimeResult::Success);

    World loadedWorld;
    loadedWorld.Initialize();
    Scene::SceneLoadStats stats;
    assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(ScenePath), &stats));
    assert(stats.LoadedRoots == 1);
    Entity* loadedOwner = loadedWorld.GetRootEntities()[0];
    auto* loadedComponent = loadedOwner->GetComponent<Component::ScriptComponent>();
    assert(loadedComponent != nullptr);
    assert(static_cast<Container::String>(loadedComponent->getScriptPath()) == "Scripts/Test/ScriptComponentMover.as");
    assert(static_cast<Container::String>(loadedComponent->getScriptClassName()) == "ScriptComponentMover");

    assert(runtime.Initialize(loadedWorld) == EScriptRuntimeResult::Success);
    ScriptBindingHandle loadedHandle;
    assert(runtime.BindComponent(*loadedComponent, loadedHandle) == EScriptRuntimeResult::Success);
    assert(loadedHandle.IsValid());
    assert(runtime.UnbindComponent(loadedHandle) == EScriptRuntimeResult::Success);
    assert(!loadedHandle.IsValid());

    loadedWorld.Finalize();
    assert(runtime.Shutdown() == EScriptRuntimeResult::Success);
    std::remove(ScenePath);
    std::cout << "ScriptComponentSceneRoundTripTest passed\n";
    return 0;
}
