#pragma once

#include "Audio/AudioListener.h"
#include "Component/Component.h"

namespace NorvesLib::Modules::Audio
{
    class IAudioModule;

    /**
     * @brief 3D 音声を聞く位置と向きの取得元を指定する。GameThread 専用。
     * 有効なリスナーのうち Priority が最大のものを使い、同値なら持ち主の ObjectId が小さい方を選ぶ。
     * Hybrid は持ち主の Entity から位置を、主のカメラから向きを取る。
     * 頭の位置で聞く場合は、頭のソケットに付けた子の Entity にこのコンポーネントを付ける。
     */
    class AudioListenerComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(AudioListenerComponent, Core::Component::Component)

    public:
        AudioListenerComponent();
        AudioListenerComponent(const AudioListenerComponent&) = delete;
        AudioListenerComponent& operator=(const AudioListenerComponent&) = delete;

        void Initialize() override;
        void Finalize() override;

        PROPERTY(AudioListenerMode, Mode)
        PROPERTY(int32_t, Priority)

    private:
        [[nodiscard]] IAudioModule* FindRegisteredModule() const;
        IAudioModule* m_AudioModule = nullptr;
    };
}
