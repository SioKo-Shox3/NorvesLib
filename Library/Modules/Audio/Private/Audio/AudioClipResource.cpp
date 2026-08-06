#include "Audio/AudioClipResource.h"

#include <utility>

namespace NorvesLib::Modules::Audio
{
    IMPLEMENT_CLASS(AudioClipResource, Core::Resource)

    AudioClipResource::AudioClipResource() = default;

    AudioClipResource::AudioClipResource(const Core::FieldInitializer* initializer)
        : Core::Resource(initializer)
    {
    }

    AudioClipResource::AudioClipResource(const Core::IUnknown* sourceObject)
        : Core::Resource(sourceObject)
    {
    }

    AudioClipResource::AudioClipResource(Core::Asset::AssetBlob pcmBlob,
                                         AudioPcmFormat format,
                                         uint64_t frameCount)
        : m_PcmBlob(std::move(pcmBlob)),
          m_Format(format),
          m_FrameCount(frameCount)
    {
        Load();
    }

    AudioClipResource::~AudioClipResource()
    {
        Finalize();
    }

    void AudioClipResource::Initialize()
    {
        Core::Resource::Initialize();
    }

    void AudioClipResource::Finalize()
    {
        Unload();
        Core::Resource::Finalize();
    }

    bool AudioClipResource::Load()
    {
        const bool bValidPayload = m_PcmBlob.IsValid() && !m_PcmBlob.IsEmpty() &&
                                   m_Format.IsSupported() && m_FrameCount != 0 &&
                                   m_PcmBlob.GetSize() % m_Format.BlockAlignment == 0 &&
                                   m_PcmBlob.GetSize() / m_Format.BlockAlignment == m_FrameCount;
        SetResourceState(bValidPayload ? Core::ResourceState::Loaded : Core::ResourceState::Failed);
        return bValidPayload;
    }

    void AudioClipResource::Unload()
    {
        m_PcmBlob = Core::Asset::AssetBlob::Invalid();
        m_Format = {};
        m_FrameCount = 0;
        SetResourceState(Core::ResourceState::Unloaded);
    }

    bool AudioClipResource::IsValid() const
    {
        if (!Core::Resource::IsValid() || !m_PcmBlob.IsValid() || m_PcmBlob.IsEmpty() ||
            !m_Format.IsSupported() || m_FrameCount == 0)
        {
            return false;
        }
        const size_t size = m_PcmBlob.GetSize();
        return size % m_Format.BlockAlignment == 0 && size / m_Format.BlockAlignment == m_FrameCount;
    }

    size_t AudioClipResource::GetMemorySize() const
    {
        return sizeof(AudioClipResource) + m_PcmBlob.GetSize();
    }

    const AudioPcmFormat& AudioClipResource::GetFormat() const noexcept
    {
        return m_Format;
    }

    uint64_t AudioClipResource::GetFrameCount() const noexcept
    {
        return m_FrameCount;
    }

    Core::Container::Span<const uint8_t> AudioClipResource::GetPcmBytes() const noexcept
    {
        return m_PcmBlob.GetSpan();
    }
} // namespace NorvesLib::Modules::Audio
