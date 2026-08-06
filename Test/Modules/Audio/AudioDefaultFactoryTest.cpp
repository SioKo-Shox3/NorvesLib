#include "Audio/IAudioModule.h"

#include <cassert>
#include <iostream>

int main()
{
    auto module = NorvesLib::Modules::Audio::CreateAudioModule();
    if (module != nullptr)
    {
        auto& service = module->GetAudioService();
        const auto initializeResult = service.Initialize();
        assert(initializeResult == NorvesLib::Modules::Audio::AudioResult::Success ||
               initializeResult == NorvesLib::Modules::Audio::AudioResult::BackendFailure);
        if (initializeResult == NorvesLib::Modules::Audio::AudioResult::Success)
        {
            assert(service.Shutdown() == NorvesLib::Modules::Audio::AudioResult::Success);
        }
    }
    std::cout << "Audio default factory exposes no fake fallback\n";
    return 0;
}
