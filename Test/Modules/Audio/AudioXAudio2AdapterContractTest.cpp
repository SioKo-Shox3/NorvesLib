#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cassert>
#include <fstream>
#include <iostream>

namespace
{
    NorvesLib::Core::Container::AnsiString ReadSource()
    {
        std::ifstream input(AUDIO_XAUDIO2_SOURCE_PATH, std::ios::binary | std::ios::ate);
        assert(input.is_open());
        const std::streamsize size = input.tellg();
        assert(size >= 0);
        input.seekg(0, std::ios::beg);
        NorvesLib::Core::Container::VariableArray<char> bytes(static_cast<size_t>(size));
        if (size > 0)
        {
            assert(input.read(bytes.data(), size));
        }
        NorvesLib::Core::Container::AnsiString source;
        source.append(bytes.data(), bytes.size());
        return source;
    }
}

int main()
{
    const auto source = ReadSource();
    const auto stopFunction = source.find("AudioBackendStopResult StopVoice(");
    const auto stopCall = source.find("->Stop(0)", stopFunction);
    const auto flushCall = source.find("->FlushSourceBuffers()", stopCall);
    const auto quiesceFunction = source.find("void QuiesceCallbacks()", stopFunction);
    assert(stopFunction != decltype(source)::npos);
    assert(stopCall != decltype(source)::npos);
    assert(flushCall != decltype(source)::npos);
    assert(quiesceFunction != decltype(source)::npos);
    assert(stopCall < flushCall && flushCall < quiesceFunction);

    assert(source.find("Thread::Atomic<IAudioBackendEventSink*>") != decltype(source)::npos);
    assert(source.find("Thread::Atomic<uint64_t> m_ShutdownEpoch") != decltype(source)::npos);
    const auto emitFunction = source.find("void Emit(");
    const auto inFlightEntry = source.find("++m_InFlightCallbacks", emitFunction);
    const auto acceptingCheck = source.find("m_bAcceptingCallbacks.Load()", emitFunction);
    const auto sinkLoad = source.find("m_EventSink.Load()", emitFunction);
    assert(emitFunction != decltype(source)::npos);
    assert(inFlightEntry < acceptingCheck && acceptingCheck < sinkLoad);
    assert(source.find("m_bAcceptingCallbacks.Store(false)") != decltype(source)::npos);
    assert(source.find("m_EventSink.Exchange(nullptr)") != decltype(source)::npos);
    std::cout << "AudioXAudio2AdapterContractTest passed\n";
    return 0;
}
