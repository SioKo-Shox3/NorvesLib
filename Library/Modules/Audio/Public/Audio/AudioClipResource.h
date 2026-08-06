#pragma once

#include "Audio/AudioTypes.h"
#include "Asset/AssetBlob.h"
#include "Container/Span.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    class AudioClipResource final : public Core::Resource
    {
        REFLECTION_CLASS(AudioClipResource, Core::Resource)

    public:
        AudioClipResource();
        explicit AudioClipResource(const Core::FieldInitializer* initializer);
        explicit AudioClipResource(const Core::IUnknown* sourceObject);
        AudioClipResource(Core::Asset::AssetBlob pcmBlob, AudioPcmFormat format, uint64_t frameCount);
        ~AudioClipResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        [[nodiscard]] bool IsValid() const override;
        [[nodiscard]] size_t GetMemorySize() const override;
        [[nodiscard]] const AudioPcmFormat& GetFormat() const noexcept;
        [[nodiscard]] uint64_t GetFrameCount() const noexcept;
        [[nodiscard]] Core::Asset::AssetBlob GetPcmBlob() const;
        [[nodiscard]] Core::Container::Span<const uint8_t> GetPcmBytes() const noexcept;

    private:
        Core::Asset::AssetBlob m_PcmBlob;
        AudioPcmFormat m_Format;
        uint64_t m_FrameCount = 0;
    };
} // namespace NorvesLib::Modules::Audio
