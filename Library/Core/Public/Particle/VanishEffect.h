#pragma once

/**
 * @file VanishEffect.h
 * @brief 消滅の時間割をディゾルブと VFX に反映する。
 */

#include "Particle/VanishEffectDefinition.h"
#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Component
{
    class SkinnedMeshComponent;
}

namespace NorvesLib::Core::Particle
{
    struct VanishEffectStartDesc
    {
        Math::Vector3 Position; // VFX と単発放出の World 位置。
        uint32_t Seed = 0xC001D00Du;
        // null の場合、閾値は保持するだけで反映しない。
        Component::SkinnedMeshComponent* DissolveTarget = nullptr;
        // 0 の場合は DissolveTarget の部品 ID を使う。
        uint64_t SkinComponentId = 0u;
        // null でなければ開始時に表をコピーする。
        const Rendering::GpuSkinHumanoidJointTable* SkinJoints = nullptr;
        uint32_t SkinJointCount = 0u;
    };

    /// 単一のゲームスレッドから使う。VFX の系と閾値の反映先は、この型より長く生存させる。
    class VanishEffect
    {
    public:
        explicit VanishEffect(VfxEffectSystem& vfx);
        ~VanishEffect();
        VanishEffect(const VanishEffect&) = delete;
        VanishEffect& operator=(const VanishEffect&) = delete;
        VanishEffect(VanishEffect&&) = delete;
        VanishEffect& operator=(VanishEffect&&) = delete;

        /// 動作中なら停止してから定義を保持し、時刻 0 の状態と単発の放出を適用する。失敗時は理由を返す。
        bool Start(const VanishEffectDefinition& definition, const VfxEffectAsset& effect,
            const VanishEffectStartDesc& desc, Container::String* outError = nullptr);
        /// World の時間の刻みで進める。0 以下・非有限・未開始・終了後は何もしない。
        void Tick(float worldDeltaSeconds);
        /// 作成したインスタンスを破棄して未開始に戻す。閾値の反映先は変更しない。
        void Stop();

        /// 閾値を 0〜1 に丸めて反映先に適用する。
        void SetDissolveThreshold(float threshold);
        /// 負の生成率を指定すると、VFX の定義の率に戻す。
        bool SetEmitterEnabled(uint32_t emitterIndex, bool bEnabled, float spawnRatePerSecond = -1.0f);
        /// 開始位置と上向きの法線で GPU へ単発の放出を要求する。
        bool EmitBurst(uint32_t emitterIndex, uint32_t count, const Math::Vector3& velocity);

        /// 開始済みで VFX のインスタンスが生存しているか。時間割の終了後も生存する。
        bool IsRunning() const;
        /// 時間割が長さに達したか。
        bool IsFinished() const;
        double GetTimeSeconds() const;
        float GetDissolveThreshold() const;
        /// 定義の発光輝度。負の値は未指定を表す。
        float GetEdgeEmissiveNits() const;
        /// 要求した放出の回数。GPU 側で受け付けられなかった要求も含む。
        uint32_t GetBurstCount() const;
        /// 放出要求数と、この型が扱う生成率の期待値の合計。実際の粒の数ではない。
        uint64_t GetRequestedParticleCount() const;
        VfxEffectHandle GetInstance() const;

    private:
        struct EmitterState
        {
            double DefaultRate = 0.0;
            double Rate = 0.0;
            double PreviousRate = 0.0;
            bool bEnabled = true;
        };

        VfxEffectSystem& m_Vfx;
        VanishEffectDefinition m_Definition;
        VariableArray<EmitterState> m_Emitters;
        VfxEffectHandle m_Instance;
        Component::SkinnedMeshComponent* m_DissolveTarget = nullptr;
        Math::Vector3 m_Position;
        double m_TimeSeconds = 0.0;
        double m_RequestedContinuousParticles = 0.0;
        uint64_t m_RequestedBurstParticles = 0u;
        size_t m_NextBurstIndex = 0u;
        uint32_t m_BurstCount = 0u;
        float m_DissolveThreshold = 0.0f;
        bool m_bFinished = false;
        bool m_bWarnedBurst = false;
    };
} // namespace NorvesLib::Core::Particle
