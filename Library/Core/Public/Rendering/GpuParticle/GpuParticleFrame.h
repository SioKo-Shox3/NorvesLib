// GPU の実行先のエミッタをフレームごとの値で描画スレッドへ渡す。
// Assets/Shaders/Particles/ParticleSimulation.glsl と同じ並び（std140/std430）。変えるときは両方を同時に変える。
#pragma once

#include "Rendering/GpuParticle/GpuParticleTypes.h"
#include "Math/Matrix4x4.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 全エミッタで共有する粒のプールの容量。
    inline constexpr uint32_t kGpuParticlePoolCapacity = 1u << 18;
    /// 1 フレームのエミッタの上限。割り当てのハンドルの Index もこの未満。
    inline constexpr uint32_t kGpuParticleMaxEmitters = 1024u;
    /// 1 フレームの単発の放出の上限。
    inline constexpr uint32_t kGpuParticleMaxBursts = 256u;
    /// 更新の 1 刻みの上限。
    inline constexpr float kGpuParticleMaxSubstepSeconds = 1.0f / 15.0f;
    /// このフレームで範囲を空にして書き込みの先頭を 0 に戻す印。
    inline constexpr uint32_t kGpuEmitterExtraFlagReset = 1u << 0u;

    /// スキンの源の行。RenderThread のパスが毎フレーム作る。std430 で 96 バイト。
    struct alignas(16) GpuSkinSource
    {
        uint32_t VertexAddress[2]; // SkinnedMeshVertex（64 B）の列の BDA（下位・上位 32 bit）。
        uint32_t SampleAddress[2]; // 表面の標本（16 B）の列の BDA（下位・上位 32 bit）。
        uint32_t SampleCount; // 表面の標本の数。
        uint32_t VertexCount; // 頂点の数。
        uint32_t BoneCount; // パレットに含まれる骨の数。
        uint32_t PaletteOffset; // mat4 の列で今の world・worldNormal・（位置・法線）×BoneCount が始まる番号。
        uint32_t PreviousPaletteOffset; // mat4 の列で前の world・位置×BoneCount が始まる番号。
        float FrameDeltaSeconds; // 前のパレットからの時間差（秒）。
        uint32_t DissolveEnabled; // ディゾルブの材質かどうか。
        float DissolveThreshold; // インスタンスの P0。
        float DissolveDirection[3]; // ディゾルブの方向。
        float DissolveDirectionBias; // ディゾルブの方向のバイアス。
        float DissolveNoiseScale; // ディゾルブのノイズの倍率。
        float DissolveEdgeWidth; // ディゾルブの境目の幅。
        uint32_t Reserved0, Reserved1; // 予約欄。
        uint32_t DissolveProtectBones[4]; // ディゾルブから保護する骨の組（128 bit）。
    };

    static_assert(sizeof(GpuSkinSource) == 96u);
    static_assert(alignof(GpuSkinSource) == 16u);
    static_assert(offsetof(GpuSkinSource, VertexAddress) == 0u);
    static_assert(offsetof(GpuSkinSource, SampleAddress) == 8u);
    static_assert(offsetof(GpuSkinSource, SampleCount) == 16u);
    static_assert(offsetof(GpuSkinSource, VertexCount) == 20u);
    static_assert(offsetof(GpuSkinSource, BoneCount) == 24u);
    static_assert(offsetof(GpuSkinSource, PaletteOffset) == 28u);
    static_assert(offsetof(GpuSkinSource, PreviousPaletteOffset) == 32u);
    static_assert(offsetof(GpuSkinSource, FrameDeltaSeconds) == 36u);
    static_assert(offsetof(GpuSkinSource, DissolveEnabled) == 40u);
    static_assert(offsetof(GpuSkinSource, DissolveThreshold) == 44u);
    static_assert(offsetof(GpuSkinSource, DissolveDirection) == 48u);
    static_assert(offsetof(GpuSkinSource, DissolveDirectionBias) == 60u);
    static_assert(offsetof(GpuSkinSource, DissolveNoiseScale) == 64u);
    static_assert(offsetof(GpuSkinSource, DissolveEdgeWidth) == 68u);
    static_assert(offsetof(GpuSkinSource, Reserved0) == 72u);
    static_assert(offsetof(GpuSkinSource, Reserved1) == 76u);
    static_assert(offsetof(GpuSkinSource, DissolveProtectBones) == 80u);

    /// 単発の放出（RaiseEvent の GPU 版）。std430 で 64 バイト。
    struct alignas(16) GpuParticleBurst
    {
        float Position[3]; uint32_t Count;        // 放出の位置（ワールド）と個数。
        float Normal[3]; uint32_t EmitterIndex;   // 法線（0 ベクトルは向きなし）と GpuParticleScene::Emitters の添字。
        float Velocity[3]; uint32_t FirstSerial;  // 生成した粒に足す速度と最初の通し番号。
        uint32_t OutputOffset;                   // 生成の通し位置（ジョブの合計の後ろから続く排他的な累積和）。
        uint32_t Reserved0, Reserved1, Reserved2;
    };

    /// エミッタの補助の行。RenderThread のパスが毎フレーム作る。std430 で 16 バイト。
    struct alignas(16) GpuEmitterFrameExtra
    {
        uint32_t PoolSlot;      // プールのスロット（割り当てのハンドルの Index。粒の Meta[1] に入る安定した番号）。
        uint32_t UpdateOffset;  // 更新の通し位置（このフレームの Emitters の順の Capacity の排他的な累積和）。
        uint32_t Flags;         // kGpuEmitterExtraFlagReset など。
        uint32_t Reserved;
    };

    /// 更新・生成・詰めの定数。std140 の一様バッファとして読む。256 バイト。
    struct alignas(16) GpuParticleSimulationConstants
    {
        float ViewProjection[16];         // 今のフレームの GBuffer と同じジッタつきの ViewProjection（GBuffer のシェーダーが受け取る並び）。
        float InverseViewProjection[16];  // 上の逆行列（命中点を視線上に復元する）。
        float CameraPosition[3]; float DeltaTime; // ビューのカメラの位置・この刻みの時間差（Particle のチャンネル）。
        float UnscaledDeltaTime; uint32_t SubstepIndex; uint32_t SubstepCount; uint32_t EmitterCount;
        uint32_t UpdateCount; uint32_t SpawnJobCount; uint32_t JobSpawnCount; uint32_t BurstCount;
        uint32_t TotalSpawnCount; uint32_t DepthCollisionEnabled; uint32_t HeightFieldBound; uint32_t CompactPhase; // 詰めは 0、仕上げは 1。
        float ScreenSize[2]; float InvScreenSize[2]; // GBuffer の解像度とその逆数。
        float WindA[4]; // 風の向き xz（WindDirectionXZ）・BaseSpeed・GustStrength（風が無効なら全部 0）。
        float WindB[4]; // GustFrequencyHz・GustScaleMeters・Turbulence・風の時刻（WindClockSeconds）。
        float HeightFieldWorldMin[2]; float HeightFieldWorldSize[2]; // 高さのテクスチャが覆う XZ の範囲。
    };

    static_assert(sizeof(GpuParticleBurst) == 64u);
    static_assert(alignof(GpuParticleBurst) == 16u);
    static_assert(offsetof(GpuParticleBurst, Position) == 0u);
    static_assert(offsetof(GpuParticleBurst, Count) == 12u);
    static_assert(offsetof(GpuParticleBurst, Normal) == 16u);
    static_assert(offsetof(GpuParticleBurst, EmitterIndex) == 28u);
    static_assert(offsetof(GpuParticleBurst, Velocity) == 32u);
    static_assert(offsetof(GpuParticleBurst, FirstSerial) == 44u);
    static_assert(offsetof(GpuParticleBurst, OutputOffset) == 48u);
    static_assert(offsetof(GpuParticleBurst, Reserved0) == 52u);
    static_assert(offsetof(GpuParticleBurst, Reserved1) == 56u);
    static_assert(offsetof(GpuParticleBurst, Reserved2) == 60u);

    static_assert(sizeof(GpuEmitterFrameExtra) == 16u);
    static_assert(alignof(GpuEmitterFrameExtra) == 16u);
    static_assert(offsetof(GpuEmitterFrameExtra, PoolSlot) == 0u);
    static_assert(offsetof(GpuEmitterFrameExtra, UpdateOffset) == 4u);
    static_assert(offsetof(GpuEmitterFrameExtra, Flags) == 8u);
    static_assert(offsetof(GpuEmitterFrameExtra, Reserved) == 12u);

    static_assert(sizeof(GpuParticleSimulationConstants) == 256u);
    static_assert(alignof(GpuParticleSimulationConstants) == 16u);
    static_assert(offsetof(GpuParticleSimulationConstants, ViewProjection) == 0u);
    static_assert(offsetof(GpuParticleSimulationConstants, InverseViewProjection) == 64u);
    static_assert(offsetof(GpuParticleSimulationConstants, CameraPosition) == 128u);
    static_assert(offsetof(GpuParticleSimulationConstants, DeltaTime) == 140u);
    static_assert(offsetof(GpuParticleSimulationConstants, UnscaledDeltaTime) == 144u);
    static_assert(offsetof(GpuParticleSimulationConstants, SubstepIndex) == 148u);
    static_assert(offsetof(GpuParticleSimulationConstants, SubstepCount) == 152u);
    static_assert(offsetof(GpuParticleSimulationConstants, EmitterCount) == 156u);
    static_assert(offsetof(GpuParticleSimulationConstants, UpdateCount) == 160u);
    static_assert(offsetof(GpuParticleSimulationConstants, SpawnJobCount) == 164u);
    static_assert(offsetof(GpuParticleSimulationConstants, JobSpawnCount) == 168u);
    static_assert(offsetof(GpuParticleSimulationConstants, BurstCount) == 172u);
    static_assert(offsetof(GpuParticleSimulationConstants, TotalSpawnCount) == 176u);
    static_assert(offsetof(GpuParticleSimulationConstants, DepthCollisionEnabled) == 180u);
    static_assert(offsetof(GpuParticleSimulationConstants, HeightFieldBound) == 184u);
    static_assert(offsetof(GpuParticleSimulationConstants, CompactPhase) == 188u);
    static_assert(offsetof(GpuParticleSimulationConstants, ScreenSize) == 192u);
    static_assert(offsetof(GpuParticleSimulationConstants, InvScreenSize) == 200u);
    static_assert(offsetof(GpuParticleSimulationConstants, WindA) == 208u);
    static_assert(offsetof(GpuParticleSimulationConstants, WindB) == 224u);
    static_assert(offsetof(GpuParticleSimulationConstants, HeightFieldWorldMin) == 240u);
    static_assert(offsetof(GpuParticleSimulationConstants, HeightFieldWorldSize) == 248u);

    /// エミッタ 1 つの 1 フレームの写し。
    struct GpuParticleEmitterFrame
    {
        uint64_t InstanceId = 0;           // 効果のインスタンスの識別子（VfxEffectHandle の Index と Generation を詰めた値）。
        uint32_t EmitterIndex = 0;         // 効果の中のエミッタの番号。
        uint32_t PoolSlot = 0xFFFFFFFFu;   // プールのスロット（割り当てのハンドルの Index）。
        uint32_t AllocationGeneration = 0; // 割り当ての世代。スロットと世代の組が変わったら範囲を空にする。
        Math::Matrix4x4 World;             // 今のワールドの変換。
        Math::Matrix4x4 PreviousWorld;     // 前の写しのワールドの変換（初回は World と同じ）。
        GpuEmitterParams Params{};        // 詰めた定数（PoolOffset・Capacity・SerialBase・SpawnCount を含む）。
        bool bEnabled = true;             // 有効の印（無効でも生きている粒の更新は続ける）。
        uint64_t SkinComponentId = 0;     // SkinSurface・Bone の源の部品。Draw.SourceMeshComponentId と同じ値で、0 は無し。
    };

    /// 単発の放出の列。上限 kGpuParticleMaxBursts、溢れは数える。
    struct GpuParticleBurstQueue
    {
        Container::VariableArray<GpuParticleBurst> Bursts;
        uint32_t OverflowCount = 0;

        /// 上限なら積まずに OverflowCount を 1 進めて false を返す。
        bool Push(const GpuParticleBurst& burst);
        /// 中身と OverflowCount を空にする（容量は残す）。
        void Clear();
    };

    /// 1 フレームの GPU の粒の写し。FramePacket::Scene.GpuParticles に入る。
    struct GpuParticleScene
    {
        Container::VariableArray<GpuParticleEmitterFrame> Emitters;
        Container::VariableArray<GpuCurveKey> CurveKeys; // GpuEmitterParams::CurveKeyOffset が指す。
        Container::VariableArray<GpuSpawnJob> SpawnJobs; // EmitterIndex は Emitters の添字、OutputOffset は排他的な累積和。
        GpuParticleBurstQueue Bursts;                   // OutputOffset はジョブの合計の後ろから続く。
        uint32_t JobSpawnCount = 0;                     // ジョブの生成数の合計。
        uint32_t TotalSpawnCount = 0;                   // ジョブと単発の放出の生成数の合計。
        float DeltaTime = 0.0f;                         // 前の写しからの Particle のチャンネルの時間差の合計。
        float UnscaledDeltaTime = 0.0f;                 // 同じく無倍率の時間差の合計。
        double ParticleTime = 0.0;                      // Particle のチャンネルの積算の時刻（ゲームの時間）。
        double UnscaledTime = 0.0;                      // 無倍率の積算の時刻。

        /// Emitters が空かどうかを返す。
        bool IsEmpty() const;
        /// 全部を既定に戻す（配列の容量は残す）。
        void Clear();
    };

    inline bool GpuParticleBurstQueue::Push(const GpuParticleBurst& burst)
    {
        if (Bursts.size() >= kGpuParticleMaxBursts)
        {
            ++OverflowCount;
            return false;
        }
        Bursts.push_back(burst);
        return true;
    }

    inline void GpuParticleBurstQueue::Clear()
    {
        Bursts.clear();
        OverflowCount = 0;
    }

    inline bool GpuParticleScene::IsEmpty() const
    {
        return Emitters.empty();
    }

    inline void GpuParticleScene::Clear()
    {
        Emitters.clear();
        CurveKeys.clear();
        SpawnJobs.clear();
        Bursts.Clear();
        JobSpawnCount = 0;
        TotalSpawnCount = 0;
        DeltaTime = 0.0f;
        UnscaledDeltaTime = 0.0f;
        ParticleTime = 0.0;
        UnscaledTime = 0.0;
    }
} // 名前空間 NorvesLib::Core::Rendering
