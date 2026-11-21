#pragma once

#include "Audio/IAudioModule.h"

namespace NorvesLib::Modules::Audio
{
    class AudioComponent;
    class AudioListenerComponent;

    namespace Private
    {
        /**
         * @brief 3D の定位に使うコンポーネントの登録を受け付ける。GameThread 専用。
         * 同じコンポーネントの再登録と、未登録のコンポーネントの解除は何もしない。
         */
        class IAudioSpatialHost
        {
        public:
            virtual ~IAudioSpatialHost() = default;
            virtual void RegisterListener(AudioListenerComponent& listener) = 0;
            virtual void UnregisterListener(AudioListenerComponent& listener) = 0;
            virtual void RegisterSpatialEmitter(AudioComponent& emitter) = 0;
            virtual void UnregisterSpatialEmitter(AudioComponent& emitter) = 0;
        };

        [[nodiscard]] inline IAudioSpatialHost* FindAudioSpatialHost(IAudioModule* module)
        {
            return dynamic_cast<IAudioSpatialHost*>(module);
        }
    }
}
