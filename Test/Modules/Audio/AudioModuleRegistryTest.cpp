#include "FakeAudioBackend.h"

#include "Audio/AudioClipResource.h"
#include "Audio/AudioDomain.h"
#include "Audio/IAudioModule.h"
#include "Audio/IAudioService.h"
#include "Asset/AssetBlob.h"
#include "Container/PointerTypes.h"
#include "CoreTypes.h"
#include "Engine/Engine.h"
#include "Module/IRenderModule.h"
#include "Module/ModuleRegistry.h"

#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>

static_assert(std::is_base_of_v<NorvesLib::Core::Module::IModule,
                               NorvesLib::Modules::Audio::IAudioModule>);
static_assert(!std::is_base_of_v<NorvesLib::Core::Module::IRenderModule,
                                NorvesLib::Modules::Audio::IAudioModule>);

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Module;
    using namespace NorvesLib::Modules::Audio;
    using NorvesLib::Test::Audio::FakeAudioBackend;
    using NorvesLib::Test::Audio::FakeAudioOperation;

    Core::Engine::Engine& LeakedEngineRef()
    {
        static Core::Engine::Engine* engine = new Core::Engine::Engine();
        return *engine;
    }

    TSharedPtr<AudioClipResource> MakeClip()
    {
        const Core::Asset::AssetBlob::ByteArray pcm{0, 0, 1, 0};
        const auto blob = Core::Asset::AssetBlob::CopyBytes(
            Core::Container::Span<const uint8_t>(pcm.data(), pcm.size()),
            Core::Container::AnsiString("memory/registry"));
        return MakeShared<AudioClipResource>(blob, AudioPcmFormat{44100, 1, 16, 2}, 2);
    }
}

int main()
{
    {
        auto failingBackend = MakeUnique<FakeAudioBackend>();
        FakeAudioBackend* failingFake = failingBackend.get();
        failingFake->bFailInitialize = true;
        auto failingOwnedModule = Private::CreateAudioModuleForBackend(std::move(failingBackend));
        assert(failingOwnedModule != nullptr);

        ModuleRegistry failingRegistry;
        IAudioModule* failingModule = dynamic_cast<IAudioModule*>(
            failingRegistry.Register(std::move(failingOwnedModule)));
        assert(failingModule != nullptr);
        assert(!failingRegistry.InstallAll(LeakedEngineRef()));
        if (failingModule->GetPhase() != EModulePhase::Uninstalled ||
            failingFake->Operations.size() != 2 ||
            failingFake->Operations[0] != FakeAudioOperation::Initialize ||
            failingFake->Operations[1] != FakeAudioOperation::Shutdown)
        {
            std::cerr << "ModuleRegistry initialize failure did not roll back a clean audio backend\n";
            return 1;
        }

        failingFake->bFailInitialize = false;
        assert(failingRegistry.InstallAll(LeakedEngineRef()));
        failingRegistry.ShutdownAll(LeakedEngineRef());
    }

    auto backend = MakeUnique<FakeAudioBackend>();
    FakeAudioBackend* fake = backend.get();
    auto ownedModule = Private::CreateAudioModuleForBackend(std::move(backend));
    assert(ownedModule != nullptr);

    ModuleRegistry registry;
    IAudioModule* audioModule = dynamic_cast<IAudioModule*>(registry.Register(std::move(ownedModule)));
    assert(audioModule != nullptr);
    assert(registry.GetRenderModules().empty());
    assert(registry.InstallAll(LeakedEngineRef()));
    assert(audioModule->GetPhase() == EModulePhase::Initialized);
    assert(fake->Operations.size() == 1);
    assert(fake->Operations[0] == FakeAudioOperation::Initialize);

    IAudioService& service = audioModule->GetAudioService();
    VoiceHandle handle;
    assert(service.CreateVoice(MakeClip(), handle) == AudioResult::Success);
    assert(service.StartVoice(handle) == AudioResult::Success);
    assert(service.StopVoice(handle) == AudioResult::Success);
    fake->Emit(handle, fake->ShutdownEpoch, Private::AudioBackendEventKind::Drained);
    registry.TickAll(0.016f);
    AudioVoiceState state = AudioVoiceState::Destroyed;
    assert(service.GetVoiceState(handle, state) == AudioResult::Success);
    assert(state == AudioVoiceState::Drained);

    registry.ShutdownAll(LeakedEngineRef());
    assert(audioModule->GetPhase() == EModulePhase::Uninstalled);
    assert(fake->Operations[fake->Operations.size() - 1] == FakeAudioOperation::Shutdown);
    std::cout << "AudioModuleRegistryTest passed\n";
    return 0;
}
