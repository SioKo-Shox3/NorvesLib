// AudioComponent の持ち主の寿命と設定の保存復元を fake バックエンドで検証する。
#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioComponent.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioModule.h"
#include "Audio/SoundSystem.h"
#include "Asset/AssetBlob.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/PrefabAsset.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <Windows.h>

namespace
{
    namespace C = NorvesLib::Core;
    namespace A = NorvesLib::Modules::Audio;
    using NorvesLib::Test::Audio::FakeAudioBackend;
    using C::Container::String;

    int g_Failures = 0;

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

#define CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "AudioComponentTest %d%s: %s %s\n", __LINE__, Utf8(u8" 行"), #expr, Utf8(u8"が成立しません")); \
            ++g_Failures; \
        } \
    } while (false)

#define REQUIRE(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "AudioComponentTest %d%s: %s %s %s\n", __LINE__, Utf8(u8" 行"), Utf8(u8"前提"), #expr, Utf8(u8"が成立しません")); \
            std::abort(); \
        } \
    } while (false)

    struct Fixture
    {
        A::IAudioModule& Module;
        A::Private::SoundSystem& Sound;
        FakeAudioBackend& Fake;
    };

    A::AudioComponent* AddComponent(C::World& world, C::Entity*& entity)
    {
        entity = world.SpawnEntity<C::Entity>();
        REQUIRE(entity != nullptr);
        auto* component = world.CreateComponent<A::AudioComponent>(entity);
        REQUIRE(component != nullptr);
        return component;
    }

    void LoadDefinitions(A::Private::SoundSystem& sound)
    {
        const C::Asset::AssetBlob::ByteArray pcm(16 * 2, 0);
        const auto blob = C::Asset::AssetBlob::CopyBytes(
            C::Container::Span<const uint8_t>(pcm.data(), pcm.size()), C::Container::AnsiString("memory/component"));
        const auto clip = C::Container::MakeShared<A::AudioClipResource>(blob, A::AudioPcmFormat{48000, 1, 16, 2}, 16);
        const A::Private::SoundBankClipResolver resolveClip(
            [&clip](const String& path) -> C::Container::TSharedPtr<A::AudioClipResource>
            {
                return path == "Audio/Test/component.wav" ? clip : C::Container::TSharedPtr<A::AudioClipResource>{};
            });
        constexpr char json[] = R"({"version":1,"sounds":[
            {"id":"Component.Loop","clips":["Audio/Test/component.wav"],"loop":true,"fade_out_ms":400},
            {"id":"Component.OneShot","clips":["Audio/Test/component.wav"]}
        ]})";
        A::SoundBankLoadReport report;
        REQUIRE(A::Private::SoundSystemTestAccess::LoadBankFromText(
            sound, "Audio/Test/component.soundbank.json",
            C::Container::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(json), sizeof(json) - 1),
            resolveClip, report) == A::AudioResult::Success);
        REQUIRE(report.LoadedDefinitionCount == 2 && report.RejectedDefinitionCount == 0);
    }

    void TestWithoutModule()
    {
        REQUIRE(A::FindAudioModule(C::Module::GetModuleRegistry()) == nullptr);
        C::World world;
        world.Initialize();
        C::Entity* entity = nullptr;
        A::AudioComponent* component = AddComponent(world, entity);
        A::SoundInstanceHandle handle{0, 1};
        CHECK(component->Play(A::SoundId("Component.OneShot"), {}, handle) == A::AudioResult::InvalidState);
        CHECK(!handle.IsValid());
        CHECK(world.RemoveEntity(entity));
        world.Finalize();
    }

    void TestPlayAndStop(Fixture& fixture)
    {
        C::World world;
        world.Initialize();
        C::Entity* firstEntity = nullptr;
        C::Entity* secondEntity = nullptr;
        A::AudioComponent* first = AddComponent(world, firstEntity);
        A::AudioComponent* second = AddComponent(world, secondEntity);
        A::SoundInstanceHandle firstHandle;
        A::SoundInstanceHandle secondHandle;
        REQUIRE(first->Play(A::SoundId("Component.OneShot"), {}, firstHandle) == A::AudioResult::Success);
        REQUIRE(second->Play(A::SoundId("Component.OneShot"), {}, secondHandle) == A::AudioResult::Success);
        CHECK(firstHandle.IsValid() && secondHandle.IsValid());
        CHECK(fixture.Sound.IsPlaying(firstHandle) && fixture.Sound.IsPlaying(secondHandle));
        const uint32_t stopsBefore = fixture.Fake.StopCount;
        CHECK(first->Stop(secondHandle, A::SoundFade{0.1f}) == A::AudioResult::StaleHandle);
        CHECK(fixture.Fake.StopCount == stopsBefore);
        CHECK(fixture.Sound.IsPlaying(secondHandle));
        const A::VoiceHandle firstVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, firstHandle);
        const A::VoiceHandle secondVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, secondHandle);
        REQUIRE(firstVoice.IsValid() && secondVoice.IsValid());
        CHECK(first->Stop(firstHandle, A::SoundFade{0.1f}) == A::AudioResult::Success);
        fixture.Module.Tick(0.11f);
        CHECK(fixture.Fake.StopCount == stopsBefore + 1);
        CHECK(fixture.Sound.IsPlaying(secondHandle));
        fixture.Fake.Emit(firstVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Fake.Emit(secondVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Module.Tick(0.0f);
        CHECK(!fixture.Sound.IsPlaying(firstHandle) && !fixture.Sound.IsPlaying(secondHandle));
        world.Finalize();
    }

    void TestOwnerDestruction(Fixture& fixture)
    {
        C::World world;
        world.Initialize();
        C::Entity* entity = nullptr;
        A::AudioComponent* component = AddComponent(world, entity);
        A::SoundInstanceHandle loop;
        A::SoundInstanceHandle oneShot;
        REQUIRE(component->Play(A::SoundId("Component.Loop"), {}, loop) == A::AudioResult::Success);
        const float initialLoopVolume = fixture.Fake.LastVolume;
        CHECK(initialLoopVolume > 0.0f);
        REQUIRE(component->Play(A::SoundId("Component.OneShot"), {}, oneShot) == A::AudioResult::Success);
        const A::VoiceHandle loopVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, loop);
        const A::VoiceHandle oneShotVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, oneShot);
        REQUIRE(loop.IsValid() && oneShot.IsValid() && loopVoice.IsValid() && oneShotVoice.IsValid());
        CHECK(fixture.Sound.IsPlaying(loop) && fixture.Sound.IsPlaying(oneShot));
        const uint32_t stopsBefore = fixture.Fake.StopCount;
        const uint32_t destroysBefore = fixture.Fake.DestroyCount;
        REQUIRE(world.RemoveEntity(entity));
        CHECK(!fixture.Sound.IsPlaying(loop));
        CHECK(fixture.Fake.StopCount == stopsBefore);
        CHECK(fixture.Fake.DestroyCount == destroysBefore);
        A::AudioVoiceState state = A::AudioVoiceState::Destroyed;
        CHECK(fixture.Module.GetAudioService().GetVoiceState(loopVoice, state) == A::AudioResult::Success);
        CHECK(state == A::AudioVoiceState::Playing);
        CHECK(fixture.Sound.IsPlaying(oneShot));
        float previousVolume = initialLoopVolume;
        for (uint32_t sample = 0; sample < 2; ++sample)
        {
            fixture.Module.Tick(0.1f);
            CHECK(fixture.Module.GetAudioService().GetVoiceState(loopVoice, state) == A::AudioResult::Success);
            CHECK(state == A::AudioVoiceState::Playing);
            CHECK(fixture.Fake.LastVolume < previousVolume);
            CHECK(fixture.Fake.LastVolume >= 0.0f);
            CHECK(fixture.Fake.StopCount == stopsBefore);
            CHECK(fixture.Fake.DestroyCount == destroysBefore);
            CHECK(fixture.Sound.IsPlaying(oneShot));
            previousVolume = fixture.Fake.LastVolume;
        }
        CHECK(std::fabs(fixture.Fake.LastVolume - 0.5f) < 0.001f);
        fixture.Module.Tick(0.21f);
        CHECK(fixture.Fake.LastVolume == 0.0f);
        CHECK(fixture.Fake.StopCount == stopsBefore + 1);
        CHECK(fixture.Fake.DestroyCount == destroysBefore);
        CHECK(fixture.Module.GetAudioService().GetVoiceState(loopVoice, state) == A::AudioResult::Success);
        CHECK(state == A::AudioVoiceState::Stopping);
        CHECK(!fixture.Sound.IsPlaying(loop));
        CHECK(fixture.Sound.IsPlaying(oneShot));
        CHECK(fixture.Module.GetAudioService().GetVoiceState(oneShotVoice, state) == A::AudioResult::Success);
        CHECK(state == A::AudioVoiceState::Playing);
        fixture.Fake.Emit(loopVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Module.Tick(0.0f);
        CHECK(fixture.Fake.DestroyCount == destroysBefore + 1);
        CHECK(!fixture.Sound.IsPlaying(loop) && fixture.Sound.IsPlaying(oneShot));
        fixture.Fake.Emit(oneShotVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Module.Tick(0.0f);
        CHECK(fixture.Fake.StopCount == stopsBefore + 1);
        CHECK(fixture.Fake.DestroyCount == destroysBefore + 2);
        CHECK(!fixture.Sound.IsPlaying(oneShot));
        world.Finalize();
    }

    void TestStopAll(Fixture& fixture)
    {
        C::World world;
        world.Initialize();
        C::Entity* firstEntity = nullptr;
        C::Entity* secondEntity = nullptr;
        A::AudioComponent* first = AddComponent(world, firstEntity);
        A::AudioComponent* second = AddComponent(world, secondEntity);
        A::SoundInstanceHandle owned[2];
        A::SoundInstanceHandle other;
        REQUIRE(first->Play(A::SoundId("Component.OneShot"), {}, owned[0]) == A::AudioResult::Success);
        REQUIRE(first->Play(A::SoundId("Component.OneShot"), {}, owned[1]) == A::AudioResult::Success);
        REQUIRE(second->Play(A::SoundId("Component.OneShot"), {}, other) == A::AudioResult::Success);
        const A::VoiceHandle firstVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, owned[0]);
        const A::VoiceHandle secondVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, owned[1]);
        const A::VoiceHandle otherVoice = A::Private::SoundSystemTestAccess::GetVoiceHandle(fixture.Sound, other);
        REQUIRE(firstVoice.IsValid() && secondVoice.IsValid() && otherVoice.IsValid());
        const uint32_t stopsBefore = fixture.Fake.StopCount;
        CHECK(first->StopAll(A::SoundFade{0.1f}) == A::AudioResult::Success);
        fixture.Module.Tick(0.11f);
        CHECK(fixture.Fake.StopCount == stopsBefore + 2);
        CHECK(fixture.Sound.IsPlaying(other));
        A::AudioVoiceState otherState = A::AudioVoiceState::Destroyed;
        CHECK(fixture.Module.GetAudioService().GetVoiceState(otherVoice, otherState) == A::AudioResult::Success);
        CHECK(otherState == A::AudioVoiceState::Playing);
        fixture.Fake.Emit(firstVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Fake.Emit(secondVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Module.Tick(0.0f);
        CHECK(!fixture.Sound.IsPlaying(owned[0]) && !fixture.Sound.IsPlaying(owned[1]));
        CHECK(fixture.Sound.IsPlaying(other));
        fixture.Fake.Emit(otherVoice, fixture.Fake.ShutdownEpoch, A::Private::AudioBackendEventKind::Drained);
        fixture.Module.Tick(0.0f);
        CHECK(!fixture.Sound.IsPlaying(other));
        world.Finalize();
    }

    void TestSceneRoundTrip()
    {
        REQUIRE(C::ClassRegistry::Get().FindClass(C::Identity("AudioComponent")) == A::AudioComponent::StaticClass());
        (void)C::Entity::StaticClass();
        (void)C::Component::Component::StaticClass();
        (void)C::PrefabAsset::StaticClass();
        char path[96]{};
        std::snprintf(path, sizeof(path), "AudioComponentTest-%lu.scene.json", GetCurrentProcessId());
        C::World source;
        source.Initialize();
        C::Entity* entity = nullptr;
        A::AudioComponent* original = AddComponent(source, entity);
        original->getSoundName() = String("Component.SavedName");
        original->getbAutoPlay() = true;
        original->getVolume() = 0.375f;
        original->getPitchSemitones() = -3.5f;
        REQUIRE(C::Scene::SceneSerializer::SaveToFile(source, String(path)));

        C::World loaded;
        loaded.Initialize();
        C::Scene::SceneLoadStats stats;
        REQUIRE(C::Scene::SceneSerializer::LoadIntoWorld(loaded, String(path), &stats));
        CHECK(stats.LoadedRoots == 1 && stats.DroppedEntities == 0 &&
              stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
        const auto roots = loaded.GetRootEntities();
        REQUIRE(roots.size() == 1);
        A::AudioComponent* restored = roots[0]->GetComponent<A::AudioComponent>();
        REQUIRE(restored != nullptr);
        CHECK(String(restored->getSoundName()) == String(original->getSoundName()));
        CHECK(static_cast<bool>(restored->getbAutoPlay()) == static_cast<bool>(original->getbAutoPlay()));
        CHECK(std::fabs(static_cast<float>(restored->getVolume()) - static_cast<float>(original->getVolume())) < 0.0001f);
        CHECK(std::fabs(static_cast<float>(restored->getPitchSemitones()) - static_cast<float>(original->getPitchSemitones())) < 0.0001f);
        loaded.Finalize();
        source.Finalize();
        CHECK(std::remove(path) == 0);
    }

    void ReportCase(const char* name, int before)
    {
        std::printf("%s: %s\n", name, g_Failures == before ? Utf8(u8"合格") : Utf8(u8"不合格"));
    }
} // 無名名前空間

int main()
{
    auto backend = C::Container::MakeUnique<FakeAudioBackend>();
    FakeAudioBackend* fake = backend.get();
    auto ownedModule = A::Private::CreateAudioModuleForBackend(std::move(backend));
    REQUIRE(ownedModule != nullptr);
    // Component の生成より前に、モジュールの登録経路を通して型を公開する。
    REQUIRE(C::ClassRegistry::Get().FindClass(C::Identity("AudioComponent")) == nullptr);
    ownedModule->RegisterReflectedTypes();
    REQUIRE(C::ClassRegistry::Get().FindClass(C::Identity("AudioComponent")) != nullptr);

    int before = g_Failures;
    TestWithoutModule();
    ReportCase(Utf8(u8"モジュールなし"), before);

    C::Module::ModuleRegistry& registry = C::Module::GetModuleRegistry();
    auto* module = dynamic_cast<A::IAudioModule*>(registry.Register(std::move(ownedModule)));
    REQUIRE(module != nullptr);
    auto engine = C::Container::MakeUnique<C::Engine::Engine>();
    REQUIRE(registry.InstallAll(*engine));
    auto* sound = dynamic_cast<A::Private::SoundSystem*>(&module->GetSoundSystem());
    REQUIRE(sound != nullptr);
    LoadDefinitions(*sound);
    Fixture fixture{*module, *sound, *fake};

    before = g_Failures;
    TestPlayAndStop(fixture);
    ReportCase("Play / Stop", before);
    before = g_Failures;
    TestOwnerDestruction(fixture);
    ReportCase(Utf8(u8"持ち主の破棄"), before);
    before = g_Failures;
    TestStopAll(fixture);
    ReportCase("StopAll", before);
    before = g_Failures;
    TestSceneRoundTrip();
    ReportCase(Utf8(u8"シーンの保存と読み込み"), before);

    registry.ShutdownAll(*engine);
    return g_Failures == 0 ? 0 : 1;
}
