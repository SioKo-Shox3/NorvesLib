#pragma once

#include "Audio/IAudioOcclusion.h"
#include "Audio/ISoundSystem.h"
#include "Audio/SoundBank.h"
#include "Asset/AssetBlob.h"
#include "Container/UnorderedMap.h"
#include "Debug/DebugConfig.h"
#include "Random/Random.h"

#if NORVES_BUILD_DEBUG
#include "Thread/Thread.h"
#endif

namespace NorvesLib::Modules::Audio::Private
{
    class AudioMixerDomain;

    struct SoundSystemConfig
    {
        uint32_t MaxConcurrentVoices = 32;
    };

    class SoundSystem final : public ISoundSystem
    {
        friend class SoundSystemTestAccess;

    public:
        SoundSystem(IAudioService& service, Random::Generator generator, const SoundSystemConfig& config = {});
        SoundSystem(const SoundSystem&) = delete;
        SoundSystem& operator=(const SoundSystem&) = delete;

        void BindMixer(AudioMixerDomain* mixer);
        void Update(float deltaSeconds);
        /** モジュールが Update の前に渡すリスナー状態。非有限の成分があれば前の状態を保つ。 */
        void SetListener(const AudioListenerState& listener);
        /** 3D の声の遮蔽の追跡先を借用する。nullptr で外す。寿命はモジュールが持つ。 */
        void SetAudioOcclusion(IAudioOcclusion* occlusion);
        AudioResult Shutdown();
        AudioResult RegisterDefinition(SoundDefinition definition);
        AudioResult UnregisterDefinition(SoundId id);
        AudioResult LoadBank(const Core::Asset::AssetSystem& assets,
                             Core::Container::AnsiStringView bankLogicalPath, SoundBankLoadReport& outReport) override;
        AudioResult UnloadBank(Core::Container::AnsiStringView bankLogicalPath) override;
        AudioResult PlayGlobal(SoundId id, const SoundPlayParams& params, SoundInstanceHandle& outHandle) override;
        AudioResult PlayAt(SoundId id, const Math::Vector3& position, const SoundPlayParams& params, SoundInstanceHandle& outHandle) override;
        AudioResult Stop(SoundInstanceHandle handle, const SoundFade& fade) override;
        AudioResult StopOwner(uint64_t ownerKey, const SoundFade& fade) override;
        AudioResult ReleaseOwner(uint64_t ownerKey, const SoundFade& fade) override;
        AudioResult SetVolume(SoundInstanceHandle handle, float volume) override;
        AudioResult SetPitch(SoundInstanceHandle handle, float semitones) override;
        AudioResult SetEmitterPosition(SoundInstanceHandle handle, const Math::Vector3& position) override;
        AudioResult SetInstanceAudible(SoundInstanceHandle handle, bool bAudible) override;
        [[nodiscard]] bool IsPlaying(SoundInstanceHandle handle) const override;
        [[nodiscard]] bool IsAudible(SoundInstanceHandle handle) const override;
        void SetMasterVolume(float volume) override;
        [[nodiscard]] float GetMasterVolume() const override;
        void SetCategoryVolume(SoundCategory category, float volume) override;
        [[nodiscard]] float GetCategoryVolume(SoundCategory category) const override;
        [[nodiscard]] SoundSystemDiagnostics GetDiagnostics() const override;

    private:
        struct Instance
        {
            uint32_t Generation = 1;
            bool bActive = false;
            bool bStopRequested = false;
            bool bStopVoiceIssued = false;
            bool bVirtual = false; ///< 物理ボイスを手放して休眠している。
            bool bVirtualByDistance = false; ///< 距離による休眠条件が成立している。
            bool bVirtualRequested = false; ///< 呼び出し側が休眠を求めている。
            bool bGlobal = true;
            bool bLoop = false;
            VoiceHandle Voice;
            Core::Container::TSharedPtr<AudioClipResource> Clip; ///< ボイスの再作成に使う、選択済みのクリップ。
            Core::Asset::AssetBlob PcmBlob; ///< 休眠からボイスを作り直せるよう、開始時の PCM を共有する。
            AudioPcmFormat Format; ///< 再生位置の更新とボイスの再作成に使う開始時の形式。
            uint64_t FrameCount = 0; ///< 再生区間の終端を求めるために控えた開始時のフレーム数。
            AudioVoiceDesc VoiceDesc; ///< 作成時のループ区間とピッチ上限を保持し、開始フレームは作成のたびに指定する。
            double PlayFrame = 0.0; ///< 仮想時計で進めるクリップ内の再生位置（フレーム）。
            SoundId DefinitionId;
            Core::Identity DuckTag;
            SoundPlayParams Params;
            uint64_t OwnerKey = 0;
            double AgeSeconds = 0.0;
            uint64_t PlaySequence = 0;
            uint8_t Priority = 128;
            Core::Scene::ESoundClass SoundClass = Core::Scene::ESoundClass::Ambient; ///< 遮蔽の評価で使う分類。
            Math::Vector3 Position;
            bool bSpatial = false; ///< 3D 音源として再生するか。
            AudioEmitterSpatialSettings SpatialSettings; ///< この音源の距離減衰とドップラーの設定。
            Math::Vector3 PendingPosition; ///< 次の更新に使う最新の位置。
            bool bHasPendingPosition = false; ///< 未反映の位置があるか。
            Math::Vector3 Velocity; ///< 直近の更新で求めた音源速度。
            float SpatialVolumeScale = 1.0f; ///< 音量に掛ける背後減衰。
            float RearHighFrequencyFactor = 1.0f; ///< 背後から届く音の高域係数。
            float DopplerRatio = 1.0f; ///< ピッチに掛けるドップラー比。
            float LastLeftGain = 0.0f; ///< 最後に送信に成功した左の利得。
            float LastRightGain = 0.0f; ///< 最後に送信に成功した右の利得。
            float MaxFrequencyRatio = 2.0f; ///< 3D ボイスの作成時に設定した周波数比の上限。
            float LastFrequencyRatio = 0.0f; ///< 最後に送信に成功した周波数比。
            SoundCategory Category = SoundCategory::Effect;
            uint32_t FadeOutMs = 0;
            float BaseGain = 1.0f;
            float Gain = 1.0f;
            float FadeStartGain = 1.0f;
            float FadeSeconds = 0.0f;
            double FadeElapsedSeconds = 0.0;
            SoundFadeCurve FadeCurve = SoundFadeCurve::Linear;
            float LastVoiceVolume = 0.0f;

            /** 再生区間の終端（含まない）を返す。クリップが空なら 0 を返す。 */
            [[nodiscard]] uint64_t GetPlaybackEndFrame() const;
            /** 現在位置での係数を求め、音量とピッチの変更にも使う値を保持する。 */
            AudioSpatialResult ComputeSpatial(const AudioListenerState& listener);
        };

        struct RecentPlay
        {
            double TimeSeconds = 0.0;
            uint64_t Frame = 0;
            Math::Vector3 Position;
            bool bGlobal = true;
        };

        struct DefinitionState
        {
            static constexpr uint32_t RecentPlayCapacity = 8;
            RecentPlay RecentPlays[RecentPlayCapacity];
            uint32_t RecentPlayCount = 0;
            uint32_t NextRecentPlay = 0;
            uint32_t LastClipIndex = UINT32_MAX;
            double LastPlayedSeconds = 0.0;
            bool bHasPlayed = false;
        };

        [[nodiscard]] bool IsOwnerThread() const;
        AudioResult LoadBankFromText(Core::Container::AnsiStringView bankKey,
                                     Core::Container::Span<const uint8_t> jsonBytes,
                                     const SoundBankClipResolver& resolveClip, SoundBankLoadReport& outReport);
        AudioResult Play(SoundId id, const Math::Vector3& position, bool bGlobal,
                         const SoundPlayParams& params, SoundInstanceHandle& outHandle);
        /**
         * 指定フレームからボイスを開始する。休眠からの復帰時も再開フレームを渡して作り直す。
         * ストリームのボイス（G16-GR74-STREAM-VOICE）では、この位置をデコーダの Seek に渡す。
         */
        AudioResult StartInstanceVoice(Instance& instance, uint32_t startFrame);
        void TrackVoiceIfSpatial(const Instance& instance);
        void UntrackVoice(const Instance& instance);
        void VirtualizeInstance(Instance& instance);
        void TryDevirtualizeInstance(Instance& instance);
        [[nodiscard]] uint32_t GetPhysicalVoiceCount() const;
        [[nodiscard]] float GetInstanceFrequencyRatio(const Instance& instance) const;
        bool AdvancePlayFrame(Instance& instance, float deltaSeconds) const;
        [[nodiscard]] uint32_t GetResumeFrame(const Instance& instance) const;
        // 上限と抑制の方針は、この入口に集約する。
        AudioResult CheckPlayAdmission(const SoundDefinition& definition, Instance& candidate, uint32_t& outStealIndex);
        [[nodiscard]] static bool IsLowerVoiceRank(const Instance& left, const Instance& right);
        AudioResult StealInstance(uint32_t index);
        void RecordPlay(SoundId id, const Math::Vector3& position, bool bGlobal);
        Core::Container::TSharedPtr<AudioClipResource> SelectClip(const SoundDefinition& definition);
        uint32_t AcquireSlot();
        void ReleaseSlot(uint32_t index);
        Instance* Resolve(SoundInstanceHandle handle);
        const Instance* Resolve(SoundInstanceHandle handle) const;
        float ResolveFadeSeconds(const SoundFade& fade, uint32_t defaultMs, bool bStopping);
        void BeginStop(Instance& instance, const SoundFade& fade);
        [[nodiscard]] float GetVoiceVolume(const Instance& instance) const;
        AudioResult ApplyVolume(Instance& instance);
        /** 音源の移動を反映し、左右の利得と周波数比の変化をボイスに送る。 */
        void UpdateSpatial(Instance& instance, float deltaSeconds);

        IAudioService& m_Service;
        AudioMixerDomain* m_Mixer = nullptr;
        IAudioOcclusion* m_Occlusion = nullptr;
        Random::Generator m_Generator;
        SoundSystemConfig m_Config;
        Core::Container::UnorderedMap<SoundId, SoundDefinition, SoundId::Hasher> m_Definitions;
        Core::Container::UnorderedMap<SoundId, DefinitionState, SoundId::Hasher> m_DefinitionStates;
        Core::Container::VariableArray<Instance> m_Instances;
        Core::Container::UnorderedMap<Core::Identity, Core::Container::VariableArray<SoundId>, Core::Identity::Hasher> m_Banks;
        Core::Container::VariableArray<uint32_t> m_FreeIndices;
        SoundSystemDiagnostics m_Diagnostics;
        AudioListenerState m_Listener; ///< 空間計算に使うリスナーの状態。
        float m_MasterVolume = 1.0f;
        float m_CategoryVolumes[kSoundCategoryCount] = {1.0f, 1.0f};
        double m_TimeSeconds = 0.0;
        uint64_t m_Frame = 0;
        uint64_t m_PlaySequence = 0;
        bool m_bShutdown = false;
#if NORVES_BUILD_DEBUG
        Thread::Thread::ThreadId m_OwnerThreadId;
#endif
    };

    class SoundSystemTestAccess
    {
    public:
        // UTF-8 の JSON バイト列から、実際の読み込みと同じ登録経路を通す。
        static AudioResult LoadBankFromText(SoundSystem& system, Core::Container::AnsiStringView bankKey,
                                            Core::Container::Span<const uint8_t> jsonBytes,
                                            const SoundBankClipResolver& resolveClip, SoundBankLoadReport& outReport);
        static bool PrepareInstanceGenerationWrap(SoundSystem& system, SoundInstanceHandle& handle);
        [[nodiscard]] static VoiceHandle GetVoiceHandle(const SoundSystem& system, SoundInstanceHandle handle);
    };
} // namespace NorvesLib::Modules::Audio::Private
