#include "Container/String.h"
#include "Asset/AssetFileReader.h"

#include <cassert>
#include <iostream>

namespace
{
    NorvesLib::Core::Container::AnsiString ReadSource(const char* path)
    {
        const NorvesLib::Core::Asset::AssetFileReader reader;
        const NorvesLib::Core::Asset::AssetReadResult result = reader.Read(
            NorvesLib::Core::Container::AnsiStringView(path));
        assert(result.Succeeded());

        const auto bytes = result.Blob.GetSpan();
        NorvesLib::Core::Container::AnsiString source;
        if (!bytes.empty())
        {
            source.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        return source;
    }
}

int main()
{
    const auto source = ReadSource(AUDIO_XAUDIO2_SOURCE_PATH);
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

    const auto domainSource = ReadSource(AUDIO_DOMAIN_SOURCE_PATH);
    const auto enqueueFunction = domainSource.find("void EnqueueBackendEvent(");
    const auto acquireSlotFunction = domainSource.find("uint32_t AcquireSlot()", enqueueFunction);
    assert(enqueueFunction != decltype(domainSource)::npos);
    assert(acquireSlotFunction != decltype(domainSource)::npos);
    const auto enqueueSource = domainSource.substr(
        enqueueFunction,
        acquireSlotFunction - enqueueFunction);
    assert(enqueueSource.find("ScopedLock") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("push_back") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("MakeUnique") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("MakeShared") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("resize") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("emplace") == decltype(enqueueSource)::npos);
    assert(enqueueSource.find("TryPublish") != decltype(enqueueSource)::npos);
    std::cout << "AudioXAudio2AdapterContractTest passed\n";
    return 0;
}
