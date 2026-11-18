#pragma once

/**
 * @file VanishEffectDefinition.h
 * @brief 消滅の閾値と粒の時間割を表す。
 */

#include "Particle/VfxEffectTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Particle
{
    struct VanishThresholdKey
    {
        float TimeSeconds = 0.0f;
        float Value = 0.0f;
    };

    struct VanishEmitterKey
    {
        float TimeSeconds = 0.0f;
        bool bEnabled = false;
        float SpawnRate = 0.0f;
    };

    struct VanishEmitterTrack
    {
        Container::String EmitterName;
        uint32_t EmitterIndex = UINT32_MAX;
        VariableArray<VanishEmitterKey> Keys;
    };

    struct VanishBurst
    {
        float TimeSeconds = 0.0f;
        Container::String EmitterName;
        uint32_t EmitterIndex = UINT32_MAX;
        uint32_t Count = 0u;
        Math::Vector3 Velocity;
        // 並べ替え後も読み込み元の配列位置をエラーに示す。
        size_t SourceIndex = static_cast<size_t>(-1);
    };

    struct VanishEffectDefinition
    {
        Container::String Name;
        Container::String EffectPath;
        float DurationSeconds = 0.0f;
        // 負の値は未指定を表す。
        float EdgeEmissiveNits = -1.0f;
        VariableArray<VanishThresholdKey> Threshold;
        VariableArray<VanishEmitterTrack> Emitters;
        VariableArray<VanishBurst> Bursts;
    };

    /// 閾値を区分線形で評価し、両端の外側では端の値を返す。キーが無ければ 0 を返す。
    float EvaluateVanishThreshold(const VanishEffectDefinition& definition, double timeSeconds);
    /// 有効状態は直前のキーから求め、両端が有効な区間だけ生成率を線形補間する。
    void EvaluateVanishEmitter(const VanishEmitterTrack& track, double timeSeconds, bool& outEnabled, float& outSpawnRate);
    /// JSON を解析し、失敗時はファイル名とキーの道を含むエラーを返す。
    bool LoadVanishEffectFromJson(const Container::String& text, const Container::String& sourceName,
        VanishEffectDefinition& outDefinition, Container::String& outError);
    /// エミッタ名を参照先の添字に解決する。
    bool ResolveVanishEffectEmitters(VanishEffectDefinition& definition, const VfxEffectAsset& asset,
        const Container::String& sourceName, Container::String& outError);
    /// ファイルと参照先 VFX を作業ディレクトリ基準で読み、エミッタ名を解決する。
    bool LoadVanishEffectFile(const Container::String& path, VanishEffectDefinition& outDefinition,
        VfxEffectAsset& outEffect, Container::String& outError);
} // namespace NorvesLib::Core::Particle
