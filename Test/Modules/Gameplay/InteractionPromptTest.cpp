// 操作候補の選択、表示の寿命、入力と戦闘の制限、全設定の保存復元を反証する。
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/InteractableComponent.h"
#include "Gameplay/World/InteractionService.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/PrefabAsset.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace G = NorvesLib::Modules::Gameplay;
    using namespace G::World;
    using String = C::Container::String;
    template <typename T>
    using Array = C::Container::VariableArray<T>;
    constexpr const char* kSceneFilePath = "InteractionPromptTest.scene.json";

#define INTERACTION_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "InteractionPrompt %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    struct PromptChange
    {
        InteractionPrompt Old;
        InteractionPrompt New;
    };

    struct Trigger
    {
        String ActionId;
        uint64_t ObjectId = 0;
    };

    struct Fixture
    {
        C::World World;
        InteractionService& Service;
        EncounterService& Encounter;
        Array<PromptChange> Changes;
        Array<Trigger> Triggers;
        C::Entity* SensorEntity = nullptr;
        bool bPressed = false;
        bool bEngaged = false;

        explicit Fixture(G::IGameplayModule& module)
            : Service(module.GetInteractionService()), Encounter(module.GetEncounterService())
        {
            World.Initialize();
            Service.Reset();
            Encounter.Reset();
            Service.SetInteractPressedSource([this]() { return bPressed; });
            Service.SetEngagedQuery([this](C::EntityHandle) { return bEngaged; });
            Service.SetMouthFreeQuery({});
            Service.OnPromptChanged.Add([this](const InteractionPrompt& oldPrompt, const InteractionPrompt& newPrompt)
            {
                Changes.push_back({oldPrompt, newPrompt});
            });
            Service.OnInteractionTriggered.Add([this](const String& actionId, uint64_t objectId)
            {
                Triggers.push_back({actionId, objectId});
            });
        }

        ~Fixture()
        {
            Service.OnPromptChanged.Clear();
            Service.OnInteractionTriggered.Clear();
            Service.SetInteractPressedSource({});
            Service.SetEngagedQuery({});
            World.Finalize();
            Service.Reset();
            Encounter.Reset();
        }

        void AddSensor(float z = 0.0f)
        {
            SensorEntity = World.SpawnEntity<C::Entity>();
            INTERACTION_CHECK(SensorEntity != nullptr);
            SensorEntity->SetLocalPosition(0.0f, 0.0f, z);
            INTERACTION_CHECK(World.CreateComponent<ZoneSensorComponent>(SensorEntity) != nullptr);
            Service.RequestRegisterSensor(World.GetEntityHandle(*SensorEntity));
        }

        InteractableComponent* AddInteractable(const char* actionId, const M::Vector3& position)
        {
            auto* entity = World.SpawnEntity<C::Entity>();
            INTERACTION_CHECK(entity != nullptr);
            entity->SetLocalPosition(position);
            auto* component = World.CreateComponent<InteractableComponent>(entity);
            INTERACTION_CHECK(component != nullptr);
            component->getActionId() = String(actionId);
            component->getPromptTextKey() = String("prompt.interact");
            component->getPromptOffset() = M::Vector3::Zero;
            component->getEnterRadius() = 10.0f;
            component->getExitRadius() = 12.0f;
            Service.RequestRegisterInteractable(World.GetEntityHandle(*entity));
            return component;
        }

        void Evaluate()
        {
            Service.Evaluate(World);
        }

        void ExpectSelected(InteractableComponent& component) const
        {
            INTERACTION_CHECK(Service.GetCurrentPrompt().IsValid());
            INTERACTION_CHECK(Service.GetCurrentPrompt().Interactable == World.GetEntityHandle(*component.GetOwner()));
        }

        void ExpectCleared(size_t index, const InteractionPrompt& previous) const
        {
            INTERACTION_CHECK(Changes.size() == index + 1);
            const auto& change = Changes[index];
            INTERACTION_CHECK(change.Old.Interactable == previous.Interactable);
            INTERACTION_CHECK(change.Old.ObjectId == previous.ObjectId && change.Old.ActionId == previous.ActionId);
            INTERACTION_CHECK(!change.New.IsValid() && change.New.ObjectId == 0 && change.New.ActionId.empty());
            INTERACTION_CHECK(!Service.GetCurrentPrompt().IsValid());
        }
    };

    // 優先度、距離、視線の角の順で選び、視野外の候補を除外する。
    void BestCandidate(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        fixture.AddSensor();
        auto* nearTarget = fixture.AddInteractable("nearTarget", M::Vector3(0, 0, 2));
        auto* farTarget = fixture.AddInteractable("farTarget", M::Vector3(0, 0, 6));
        farTarget->getPriority() = 10;
        fixture.Evaluate();
        fixture.ExpectSelected(*farTarget);

        farTarget->getPriority() = 0;
        fixture.Evaluate();
        fixture.ExpectSelected(*nearTarget);

        // x と z を交換した対称な配置で距離を厳密に等しくする。
        nearTarget->GetOwner()->SetLocalPosition(4.0f, 0.0f, 3.0f);
        farTarget->GetOwner()->SetLocalPosition(3.0f, 0.0f, 4.0f);
        fixture.Evaluate();
        fixture.ExpectSelected(*farTarget);

        nearTarget->getPriority() = 100;
        nearTarget->getFacingConeDegrees() = 60.0f;
        farTarget->getFacingConeDegrees() = 60.0f;
        fixture.Evaluate();
        INTERACTION_CHECK(!fixture.Service.GetCurrentPrompt().IsValid());
        farTarget->getFacingConeDegrees() = 90.0f;
        fixture.Evaluate();
        fixture.ExpectSelected(*farTarget);
    }

    // 進入前は帯で表示せず、進入後の帯の往復では表示を保つ。
    void HysteresisDoesNotFlicker(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        fixture.AddSensor(6.0f);
        auto* target = fixture.AddInteractable("band", M::Vector3::Zero);
        target->getEnterRadius() = 2.0f;
        target->getExitRadius() = 4.0f;
        fixture.Evaluate();
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 3.0f);
        fixture.Evaluate();
        INTERACTION_CHECK(fixture.Changes.empty() && !fixture.Service.GetCurrentPrompt().IsValid());
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 1.5f);
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
        INTERACTION_CHECK(fixture.Changes.size() == 1 && !fixture.Changes[0].Old.IsValid());
        INTERACTION_CHECK(fixture.Changes[0].New.Interactable == fixture.World.GetEntityHandle(*target->GetOwner()));
        const InteractionPrompt previous = fixture.Service.GetCurrentPrompt();
        for (unsigned frame = 0; frame < 10; ++frame)
        {
            fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, frame % 2 == 0 ? 2.25f : 3.75f);
            fixture.Evaluate();
            fixture.ExpectSelected(*target);
            INTERACTION_CHECK(fixture.Changes.size() == 1);
        }
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 4.5f);
        fixture.Evaluate();
        fixture.ExpectCleared(1, previous);
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 3.0f);
        fixture.Evaluate();
        INTERACTION_CHECK(fixture.Changes.size() == 2 && !fixture.Service.GetCurrentPrompt().IsValid());
    }

    // 無効化と Entity の破棄は評価を待たず一度だけ解除を通知する。
    void UnavailableClearsImmediately(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        fixture.AddSensor();
        auto* target = fixture.AddInteractable("unavailable", M::Vector3(0, 0, 1));
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
        const InteractionPrompt beforeDisable = fixture.Service.GetCurrentPrompt();
        target->Disable();
        fixture.ExpectCleared(1, beforeDisable);
        fixture.Evaluate();
        INTERACTION_CHECK(fixture.Changes.size() == 2 && !fixture.Service.GetCurrentPrompt().IsValid());

        target->Enable();
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
        INTERACTION_CHECK(fixture.Changes.size() == 3);
        const InteractionPrompt beforeDestroy = fixture.Service.GetCurrentPrompt();
        INTERACTION_CHECK(fixture.World.RemoveEntity(target->GetOwner()));
        fixture.ExpectCleared(3, beforeDestroy);
        INTERACTION_CHECK(fixture.World.ResolveEntityHandle(beforeDestroy.Interactable) == nullptr);
        fixture.Evaluate();
        INTERACTION_CHECK(fixture.Changes.size() == 4 && !fixture.Service.GetCurrentPrompt().IsValid());
    }

    // 一フレームだけの入力を対象 ID とともに一度だけ通知し、候補なしでは通知しない。
    void OnePressTriggersOnce(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        fixture.AddSensor();
        auto* target = fixture.AddInteractable("action.open", M::Vector3(0, 0, 1));
        const uint64_t targetId = target->GetOwner()->GetObjectId();
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
        INTERACTION_CHECK(fixture.Triggers.empty());
        fixture.bPressed = true;
        fixture.Evaluate();
        fixture.bPressed = false;
        for (unsigned frame = 0; frame < 3; ++frame)
        {
            fixture.Evaluate();
        }
        INTERACTION_CHECK(fixture.Triggers.size() == 1);
        INTERACTION_CHECK(fixture.Triggers[0].ActionId == "action.open" && fixture.Triggers[0].ObjectId == targetId);
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 20.0f);
        fixture.Evaluate();
        INTERACTION_CHECK(!fixture.Service.GetCurrentPrompt().IsValid());
        fixture.bPressed = true;
        fixture.Evaluate();
        fixture.bPressed = false;
        fixture.Evaluate();
        INTERACTION_CHECK(fixture.Triggers.size() == 1);
    }

    // 偽の照会と実物の遭遇サービスの両方で、戦闘中だけ候補を抑える。
    void EngagedPreventsPrompt(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        fixture.AddSensor();
        auto* target = fixture.AddInteractable("peace-only", M::Vector3(0, 0, 1));
        target->getbDisabledWhileEngaged() = true;
        fixture.bEngaged = true;
        fixture.Evaluate();
        INTERACTION_CHECK(!fixture.Service.GetCurrentPrompt().IsValid() && fixture.Changes.empty());
        fixture.bEngaged = false;
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
        fixture.bEngaged = true;
        fixture.Evaluate();
        INTERACTION_CHECK(!fixture.Service.GetCurrentPrompt().IsValid());

        auto& encounter = fixture.Encounter;
        auto* zoneEntity = fixture.World.SpawnEntity<C::Entity>();
        INTERACTION_CHECK(zoneEntity != nullptr);
        zoneEntity->SetLocalPosition(0.0f, 0.0f, 20.0f);
        auto* zone = fixture.World.CreateComponent<EncounterZoneComponent>(zoneEntity);
        INTERACTION_CHECK(zone != nullptr);
        zone->getZoneId() = String("interaction.encounter");
        zone->getRadius() = 3.0f;
        encounter.RequestRegisterZone(fixture.World.GetEntityHandle(*zoneEntity));
        const auto sensorHandle = fixture.World.GetEntityHandle(*fixture.SensorEntity);
        encounter.RequestRegisterSensor(sensorHandle);
        fixture.Service.SetEngagedQuery(C::MakeDelegate(&encounter, &EncounterService::IsSensorEngaged));
        encounter.Evaluate(fixture.World, 0.0f);
        INTERACTION_CHECK(!encounter.IsSensorEngaged(sensorHandle));
        fixture.Evaluate();
        fixture.ExpectSelected(*target);

        // センサーと対象を一緒に移し、距離ではなく戦闘状態だけで表示が消える条件にする。
        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 20.0f);
        target->GetOwner()->SetLocalPosition(0.0f, 0.0f, 21.0f);
        encounter.Evaluate(fixture.World, 0.0f);
        INTERACTION_CHECK(zone->GetState() == EEncounterZoneState::Engaged && encounter.IsSensorEngaged(sensorHandle));
        fixture.Evaluate();
        INTERACTION_CHECK(!fixture.Service.GetCurrentPrompt().IsValid());

        fixture.SensorEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        target->GetOwner()->SetLocalPosition(0.0f, 0.0f, 1.0f);
        encounter.Evaluate(fixture.World, 0.0f);
        INTERACTION_CHECK(!encounter.IsSensorEngaged(sensorHandle));
        fixture.Evaluate();
        fixture.ExpectSelected(*target);
    }

    // 九つの PROPERTY を全て既定と違う値にし、新しい World への保存復元で保持する。
    void SceneRoundTrip(G::IGameplayModule& module)
    {
        Fixture fixture(module);
        auto* source = fixture.AddInteractable("action.round_trip", M::Vector3(1, 2, 3));
        source->getPromptTextKey() = String("prompt.round_trip");
        source->getEnterRadius() = 3.25f;
        source->getExitRadius() = 4.75f;
        source->getPromptOffset() = M::Vector3(0.5f, 1.25f, -0.75f);
        source->getFacingConeDegrees() = 125.0f;
        source->getPriority() = 17;
        source->getbDisabledWhileEngaged() = true;
        source->getbRequiresFreeMouth() = true;
        INTERACTION_CHECK(C::Scene::SceneSerializer::SaveToFile(fixture.World, String(kSceneFilePath)));

        C::World loadedWorld;
        loadedWorld.Initialize();
        C::Scene::SceneLoadStats stats;
        INTERACTION_CHECK(C::Scene::SceneSerializer::LoadIntoWorld(loadedWorld, String(kSceneFilePath), &stats));
        INTERACTION_CHECK(stats.LoadedRoots == 1 && stats.DroppedEntities == 0 &&
                          stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
        const auto roots = loadedWorld.GetRootEntities();
        INTERACTION_CHECK(roots.size() == 1);
        auto* loaded = roots[0]->GetComponent<InteractableComponent>();
        INTERACTION_CHECK(loaded != nullptr);
        INTERACTION_CHECK(String(loaded->getActionId()) == String(source->getActionId()));
        INTERACTION_CHECK(String(loaded->getPromptTextKey()) == String(source->getPromptTextKey()));
        INTERACTION_CHECK(loaded->getEnterRadius() == source->getEnterRadius());
        INTERACTION_CHECK(loaded->getExitRadius() == source->getExitRadius());
        const M::Vector3 expectedOffset = source->getPromptOffset();
        const M::Vector3 actualOffset = loaded->getPromptOffset();
        INTERACTION_CHECK(actualOffset.x == expectedOffset.x && actualOffset.y == expectedOffset.y && actualOffset.z == expectedOffset.z);
        INTERACTION_CHECK(loaded->getFacingConeDegrees() == source->getFacingConeDegrees());
        INTERACTION_CHECK(loaded->getPriority() == source->getPriority());
        INTERACTION_CHECK(loaded->getbDisabledWhileEngaged() == source->getbDisabledWhileEngaged());
        INTERACTION_CHECK(loaded->getbRequiresFreeMouth() == source->getbRequiresFreeMouth());
        loadedWorld.Finalize();
        INTERACTION_CHECK(std::remove(kSceneFilePath) == 0);
    }
} // 無名名前空間

int main()
{
    (void)C::Entity::StaticClass();
    (void)C::Component::Component::StaticClass();
    (void)C::PrefabAsset::StaticClass();
    auto engine = C::Container::MakeUnique<C::Engine::Engine>();
    auto& registry = C::Module::GetModuleRegistry();
    auto* module = G::RegisterGameplayModule(registry);
    INTERACTION_CHECK(module != nullptr && registry.InstallAll(*engine));
    BestCandidate(*module);
    HysteresisDoesNotFlicker(*module);
    UnavailableClearsImmediately(*module);
    OnePressTriggersOnce(*module);
    EngagedPreventsPrompt(*module);
    SceneRoundTrip(*module);
    registry.ShutdownAll(*engine);
    std::puts("InteractionPromptTest: PASS");
    return 0;
}
