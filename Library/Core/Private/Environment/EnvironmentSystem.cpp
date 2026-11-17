// 気象を固定刻みで進め、上書きの層を解決して描画へ渡す。
#include "Environment/EnvironmentSystem.h"

#include "Logging/LogMacros.h"
#include "Particle/VfxEffectSerialization.h"
#include "Particle/VfxSchema.h"
#include "Platform/PlatformFile.h"
#include "Rendering/GrassFieldRenderData.h"
#include "Rendering/RenderWorld.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        constexpr uint64_t NanosecondsPerSecond = 1'000'000'000;
        constexpr const char* RainNearEffectPath = "Assets/Data/Vfx/Rain_Near.vfx.json";
        constexpr uint32_t RainNearSeed = 0x5241494Eu;

        bool FindRainNearModule(const Particle::VfxEmitterAsset& emitter, Particle::VfxModuleType type,
            uint32_t& outIndex, Container::String& error)
        {
            const Particle::VfxObjectSchema* schema = Particle::FindVfxModuleSchema(type);
            if (schema == nullptr)
            {
                error = "必要なモジュールのスキーマがありません";
                return false;
            }
            bool bFound = false;
            for (uint32_t index = 0; index < emitter.Modules.size(); ++index)
            {
                if (emitter.Modules[index].Type != type)
                {
                    continue;
                }
                if (bFound)
                {
                    error = "必要なモジュールが複数あります: ";
                    error += schema->Name;
                    return false;
                }
                outIndex = index;
                bFound = true;
            }
            if (!bFound)
            {
                error = "必要なモジュールがありません: ";
                error += schema->Name;
            }
            return bFound;
        }

        const Particle::VfxValue* FindRainNearValue(const Particle::VfxModule& module, const char* name)
        {
            const Particle::VfxObjectSchema* schema = Particle::FindVfxModuleSchema(module.Type);
            if (schema == nullptr)
            {
                return nullptr;
            }
            const int32_t index = Particle::FindVfxParameterIndex(*schema, name);
            return index >= 0 && static_cast<uint32_t>(index) < module.Values.size()
                ? &module.Values[static_cast<uint32_t>(index)] : nullptr;
        }

        float SampleRainNearGrassDensity(const Rendering::GrassFieldRenderData& field, float x, float z)
        {
            if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(field.ChunkSizeMeters) ||
                field.ChunkSizeMeters <= 0.0f || field.LayerAResolution == 0)
            {
                return 0.0f;
            }
            const double chunkX = std::floor(static_cast<double>(x) / field.ChunkSizeMeters);
            const double chunkZ = std::floor(static_cast<double>(z) / field.ChunkSizeMeters);
            if (chunkX < INT32_MIN || chunkX > INT32_MAX || chunkZ < INT32_MIN || chunkZ > INT32_MAX)
            {
                return 0.0f;
            }
            const float pixelWidth = field.ChunkSizeMeters / static_cast<float>(field.LayerAResolution);
            if (pixelWidth <= 0.0f)
            {
                return 0.0f;
            }
            for (const Rendering::GrassFieldChunkRenderData& chunk : field.Chunks)
            {
                if (chunk.ChunkX != static_cast<int32_t>(chunkX) || chunk.ChunkZ != static_cast<int32_t>(chunkZ))
                {
                    continue;
                }
                // GrassField::Sample と同じく、局所座標の差は倍精度で取る。
                const float localX = static_cast<float>(static_cast<double>(x) - chunkX * field.ChunkSizeMeters);
                const float localZ = static_cast<float>(static_cast<double>(z) - chunkZ * field.ChunkSizeMeters);
                const auto pixelCoordinate = [pixelWidth, &field](float local)
                {
                    const double pixel = std::floor(local / pixelWidth);
                    return pixel <= 0.0 ? 0u : pixel >= field.LayerAResolution - 1u ?
                        field.LayerAResolution - 1u : static_cast<uint32_t>(pixel);
                };
                const uint32_t pixelX = pixelCoordinate(localX);
                const uint32_t pixelZ = pixelCoordinate(localZ);
                const uint64_t pixelIndex = static_cast<uint64_t>(pixelZ) * field.LayerAResolution + pixelX;
                const uint64_t byteCount = field.LayerAPixels.size();
                // RGBA の全体が入ることを先に確かめ、バイト位置の乗算も範囲内に収める。
                if (chunk.LayerAByteOffset > byteCount ||
                    pixelIndex >= (byteCount - chunk.LayerAByteOffset) / 4u)
                {
                    return 0.0f;
                }
                const uint64_t offset = chunk.LayerAByteOffset + pixelIndex * 4u;
                return static_cast<float>(field.LayerAPixels[offset]) / 255.0f;
            }
            return 0.0f;
        }

        uint64_t StepsForNanoseconds(uint64_t nanoseconds)
        {
            return (nanoseconds / NanosecondsPerSecond) * EnvironmentSystem::StepsPerSecond +
                   ((nanoseconds % NanosecondsPerSecond) * EnvironmentSystem::StepsPerSecond) / NanosecondsPerSecond;
        }

        uint64_t NanosecondsForGameTime(double seconds)
        {
            const double nanoseconds = seconds * static_cast<double>(NanosecondsPerSecond);
            constexpr double SignedLimit = 9223372036854775808.0;
            constexpr double UnsignedLimit = 18446744073709551616.0;
            if (nanoseconds >= UnsignedLimit)
            {
                LOG_WARNING("EnvironmentSystem の経過時刻が保持できる範囲を超えたため上限に丸めます");
                return std::numeric_limits<uint64_t>::max();
            }
            if (nanoseconds >= SignedLimit)
            {
                return (uint64_t{1} << 63) + static_cast<uint64_t>(std::llround(nanoseconds - SignedLimit));
            }
            return static_cast<uint64_t>(std::llround(nanoseconds));
        }

        void WarnDisabledCommand(const char* command)
        {
            LOG_WARNING("EnvironmentSystem が無効なため %s を無視します", command);
            (void)command;
        }

        double ClockHoursForGameTime(double seconds)
        {
            double hours = std::fmod(seconds / 3600.0, 24.0);
            if (hours < 0.0)
            {
                hours += 24.0;
            }
            return std::isfinite(hours) && hours < 24.0 ? hours : 0.0;
        }

        void WarnClockDataLoadFailure(const char* fileName, const Container::String& error)
        {
#ifdef UNICODE
            LOG_WARNING("EnvironmentSystem の時計の資料 %s を読み込めません: %ls", fileName, error.c_str());
#else
            LOG_WARNING("EnvironmentSystem の時計の資料 %s を読み込めません: %s", fileName, error.c_str());
#endif
            (void)fileName;
            (void)error;
        }

        double FieldPeriod(EEnvironmentField field)
        {
            switch (field)
            {
            case EEnvironmentField::WindDirectionYaw:
            case EEnvironmentField::SunAzimuth:
                return 360.0;
            case EEnvironmentField::TimeOfDayHours:
                return 24.0;
            default:
                return 0.0;
            }
        }
    } // namespace

    void EnvironmentSystem::Enable(const EnvironmentState& initial, const EnvironmentSceneBase& base)
    {
        m_RainTuning = Rendering::LoadRainTuningFile(Rendering::DefaultRainTuningPath);
        m_RainSplashBudget = LoadRainSplashBudgetFile(DefaultRainSplashBudgetPath);
        m_bRainNearRecreatePending = true;
        Container::String rainError;
        m_bRainNearAssetReady = LoadRainNearEffect(rainError);
        if (!m_bRainNearAssetReady)
        {
            NORVES_LOG_WARNING("General", "EnvironmentSystem の近距離の雨を読み込めません: %s: %s",
                RainNearEffectPath, rainError.c_str());
        }
        m_bRainTuningPublishPending = true;
        m_SceneBase = base;
        m_BaseState = SanitizeEnvironmentState(initial);
        LoadClockData();
        ClearTransitions();
        m_Overrides.clear();
        m_TotalNanoseconds = NanosecondsForGameTime(m_BaseState.GameTimeSeconds);
        if (m_TotalNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        }
        m_StepCount = StepsForNanoseconds(m_TotalNanoseconds);
        m_bEnabled = true;
        m_bRestorePending = false;
        ApplyClock();
        if (IsClockActive())
        {
            const double endSeconds = m_BaseState.GameTimeSeconds;
            m_BaseState.Wetness = RestoreClockWetness(endSeconds - WetnessRestoreWindowSeconds,
                                                      endSeconds, m_BaseState.Wetness);
        }
        RebuildEffective();
        Notify(EEnvironmentChange::Enabled);
    }

    void EnvironmentSystem::Disable()
    {
        if (!m_bEnabled)
        {
            return;
        }
        m_bEnabled = false;
        // 効果の所有者には触れず、次の UpdateRainNear でインスタンスを破棄する。
        m_bRestorePending = true;
        ClearTransitions();
        m_Overrides.clear();
        RebuildEffective();
        Notify(EEnvironmentChange::Disabled);
    }

    bool EnvironmentSystem::IsEnabled() const
    {
        return m_bEnabled;
    }

    bool EnvironmentSystem::IsClockActive() const
    {
        return m_bEnabled && m_BaseState.SunMode == EEnvironmentSunMode::Clock;
    }

    double EnvironmentSystem::GetClockHours() const
    {
        return ClockHoursForGameTime(m_BaseState.GameTimeSeconds);
    }

    void EnvironmentSystem::SetManualSunOverride(float altitudeDegrees, float azimuthDegrees)
    {
        EnvironmentTarget target;
        target.Mask = EnvironmentFieldBit(EEnvironmentField::SunAltitude) |
                      EnvironmentFieldBit(EEnvironmentField::SunAzimuth);
        target.Values = m_BaseState;
        target.Values.SunAltitudeDegrees = altitudeDegrees;
        target.Values.SunAzimuthDegrees = azimuthDegrees;
        PushOverride(ManualSunOverrideScope, target);
    }

    void EnvironmentSystem::ClearManualSunOverride()
    {
        PopOverride(ManualSunOverrideScope);
    }

    void EnvironmentSystem::SetTarget(const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SetTarget");
            return;
        }

        RefreshFields();
        const EnvironmentState values = SanitizeEnvironmentState(target.Values);
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const auto field = static_cast<EEnvironmentField>(index);
            if ((target.Mask & EnvironmentFieldBit(field)) == 0)
            {
                continue;
            }
            if (field == EEnvironmentField::Wetness)
            {
                LOG_WARNING("EnvironmentSystem の濡れは SetTarget では遷移できないため無視します");
                continue;
            }

            FieldTransition& transition = m_Transitions[index];
            const float to = GetEnvironmentField(values, field);
            const float duration = target.DurationSeconds[index];
            if (std::isfinite(duration) && duration > 0.0f)
            {
                transition.bActive = true;
                transition.From = GetEnvironmentField(m_BaseState, field);
                transition.To = to;
                transition.StartStep = m_StepCount;
                transition.DurationSeconds = duration;
                transition.Curve = target.Curves[index];
                if (field == EEnvironmentField::CloudLayerEnabled)
                {
                    SetEnvironmentField(m_BaseState, field, EvaluateField(field, m_StepCount));
                }
            }
            else
            {
                SetEnvironmentField(m_BaseState, field, to);
                transition.bActive = false;
            }
        }
        RebuildEffective();
        Notify(EEnvironmentChange::TargetSet);
    }

    void EnvironmentSystem::SetPresetRoot(const Container::String& rootDirectory)
    {
        m_PresetRoot = rootDirectory;
    }

    const Container::String& EnvironmentSystem::GetPresetRoot() const
    {
        return m_PresetRoot;
    }

    bool EnvironmentSystem::ApplyPreset(const Container::String& name)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("ApplyPreset");
            return false;
        }
        EnvironmentTarget target;
        Container::String error;
        if (!LoadEnvironmentPreset(m_PresetRoot, name, target, &error))
        {
#ifdef UNICODE
            LOG_WARNING("EnvironmentSystem のプリセットを読み込めません: %ls", error.c_str());
#else
            LOG_WARNING("EnvironmentSystem のプリセットを読み込めません: %s", error.c_str());
#endif
            return false;
        }
        SetTarget(target);
        return true;
    }

    void EnvironmentSystem::SetImmediate(const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SetImmediate");
            return;
        }

        const EnvironmentState values = SanitizeEnvironmentState(target.Values);
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const auto field = static_cast<EEnvironmentField>(index);
            if ((target.Mask & EnvironmentFieldBit(field)) != 0)
            {
                SetEnvironmentField(m_BaseState, field, GetEnvironmentField(values, field));
                m_Transitions[index].bActive = false;
            }
        }
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::Immediate);
    }

    void EnvironmentSystem::SnapToTarget()
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SnapToTarget");
            return;
        }

        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            FieldTransition& transition = m_Transitions[index];
            if (transition.bActive)
            {
                SetEnvironmentField(m_BaseState, static_cast<EEnvironmentField>(index), transition.To);
                transition.bActive = false;
            }
        }
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::Snapped);
    }

    void EnvironmentSystem::LoadState(const EnvironmentState& state)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("LoadState");
            return;
        }

        const uint32_t previousSeed = m_BaseState.Seed;
        const uint32_t nextDiscontinuityId = m_BaseState.DiscontinuityId + 1u;
        m_BaseState = SanitizeEnvironmentState(state);
        m_BaseState.DiscontinuityId = nextDiscontinuityId;
        if (m_BaseState.Seed != previousSeed)
        {
            RebuildWeatherTrack();
        }
        ClearTransitions();
        m_TotalNanoseconds = NanosecondsForGameTime(m_BaseState.GameTimeSeconds);
        if (m_TotalNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        }
        m_StepCount = StepsForNanoseconds(m_TotalNanoseconds);
        ApplyClock();
        if (IsClockActive())
        {
            const double endSeconds = m_BaseState.GameTimeSeconds;
            m_BaseState.Wetness = RestoreClockWetness(endSeconds - WetnessRestoreWindowSeconds,
                                                      endSeconds, m_BaseState.Wetness);
        }
        RebuildEffective();
        Notify(EEnvironmentChange::Loaded);
    }

    void EnvironmentSystem::SkipTime(double seconds)
    {
        SkipBy(seconds);
    }

    void EnvironmentSystem::SkipTo(double gameTimeSeconds)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SkipTo");
            return;
        }
        if (!std::isfinite(gameTimeSeconds) || gameTimeSeconds <= m_BaseState.GameTimeSeconds)
        {
            return;
        }
        SkipBy(gameTimeSeconds - m_BaseState.GameTimeSeconds);
    }

    void EnvironmentSystem::SkipBy(double seconds)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SkipTo");
            return;
        }
        if (!std::isfinite(seconds) || seconds <= 0.0)
        {
            return;
        }

        if (seconds > MaxSkipSeconds)
        {
            LOG_WARNING("EnvironmentSystem の SkipTo の移動時間が上限を超えたため丸めます");
            seconds = MaxSkipSeconds;
        }

        const double oldTimeSeconds = m_BaseState.GameTimeSeconds;
        const uint64_t nanoseconds = static_cast<uint64_t>(std::llround(seconds * static_cast<double>(NanosecondsPerSecond)));
        if (IsClockActive())
        {
            const uint64_t maximum = std::numeric_limits<uint64_t>::max();
            m_TotalNanoseconds = nanoseconds > maximum - m_TotalNanoseconds
                ? maximum : m_TotalNanoseconds + nanoseconds;
            m_StepCount = StepsForNanoseconds(m_TotalNanoseconds);
            RefreshFields();
            m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
            ApplyClock();
            const double windowStartSeconds = m_BaseState.GameTimeSeconds - WetnessRestoreWindowSeconds;
            m_BaseState.Wetness = RestoreClockWetness(oldTimeSeconds > windowStartSeconds
                                                          ? oldTimeSeconds : windowStartSeconds,
                                                      m_BaseState.GameTimeSeconds, m_BaseState.Wetness);
        }
        else
        {
            AdvanceNanoseconds(nanoseconds);
        }
        const double newTimeSeconds = m_BaseState.GameTimeSeconds;
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::TimeSkipped);
        m_OnTimeSkipped.Broadcast(oldTimeSeconds, newTimeSeconds);
    }

    void EnvironmentSystem::Tick(float worldDeltaSeconds)
    {
        if (!m_bEnabled || !std::isfinite(worldDeltaSeconds) || worldDeltaSeconds <= 0.0f)
        {
            return;
        }

        double delta = static_cast<double>(worldDeltaSeconds) < MaxTickDeltaSeconds
            ? static_cast<double>(worldDeltaSeconds) : MaxTickDeltaSeconds;
        if (IsClockActive())
        {
            delta *= m_BaseState.ClockTimeScale;
        }
        if (delta <= 0.0)
        {
            return;
        }
        AdvanceNanoseconds(static_cast<uint64_t>(std::llround(delta * static_cast<double>(NanosecondsPerSecond))));
    }

    const EnvironmentState& EnvironmentSystem::GetState() const
    {
        return m_EffectiveState;
    }

    const EnvironmentState& EnvironmentSystem::GetBaseState() const
    {
        return m_BaseState;
    }

    const EnvironmentSceneBase& EnvironmentSystem::GetSceneBase() const
    {
        return m_SceneBase;
    }

    const Rendering::RainTuning& EnvironmentSystem::GetRainTuning() const
    {
        return m_RainTuning;
    }

    EnvironmentResolved EnvironmentSystem::Resolve() const
    {
        EnvironmentResolved resolved = ResolveEnvironment(m_EffectiveState, m_SceneBase);
        resolved.Fog.DensityAtBaseHeight += Rendering::MakeRainFogBoost(m_EffectiveState.RainIntensity, m_RainTuning);
        bool bSunOverridden = false;
        constexpr EnvironmentFieldMask sunMask = EnvironmentFieldBit(EEnvironmentField::SunAltitude) |
                                                 EnvironmentFieldBit(EEnvironmentField::SunAzimuth);
        for (const OverrideEntry& entry : m_Overrides)
        {
            if ((entry.Target.Mask & sunMask) != 0)
            {
                bSunOverridden = true;
                break;
            }
        }
        resolved.Environment.SunRevisionQuantizationDegrees = IsClockActive() && !bSunOverridden
            ? ClockSunRevisionQuantizationDegrees : 0.0f;
        const NightLightState night = EvaluateNightLight(m_NightLight, resolved.Sky.SunAltitudeDegrees,
                                                         resolved.Sky.SunAzimuthDegrees);
        if (night.Weight > 0.0f)
        {
            resolved.Sky.MoonIlluminanceLux = night.MoonIlluminanceLux;
            resolved.Sky.MoonAltitudeDegrees = night.MoonAltitudeDegrees;
            resolved.Sky.MoonAzimuthDegrees = night.MoonAzimuthDegrees;
            resolved.Sky.MoonColor = Math::Vector3(night.MoonColor[0], night.MoonColor[1], night.MoonColor[2]);
            resolved.Sky.bMoonCastShadows = night.bMoonCastShadows;
            resolved.Sky.bOverrideAutoExposureMinEV100 = night.bOverrideMinEV100;
            resolved.Sky.AutoExposureMinEV100 = night.MinEV100;
        }
        return resolved;
    }

    bool EnvironmentSystem::PushOverride(EnvironmentOverrideScope scope, const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("PushOverride");
            return false;
        }

        EnvironmentTarget sanitizedTarget = target;
        sanitizedTarget.Mask &= EnvironmentFieldMaskAll;
        sanitizedTarget.Values = SanitizeEnvironmentState(target.Values);
        for (OverrideEntry& entry : m_Overrides)
        {
            if (entry.Scope == scope)
            {
                entry.Target = sanitizedTarget;
                RebuildEffective();
                Notify(EEnvironmentChange::OverridePushed);
                return true;
            }
        }
        m_Overrides.push_back(OverrideEntry{scope, sanitizedTarget});
        RebuildEffective();
        Notify(EEnvironmentChange::OverridePushed);
        return true;
    }

    bool EnvironmentSystem::PopOverride(EnvironmentOverrideScope scope)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("PopOverride");
            return false;
        }

        for (auto entry = m_Overrides.begin(); entry != m_Overrides.end(); ++entry)
        {
            if (entry->Scope == scope)
            {
                m_Overrides.erase(entry);
                RebuildEffective();
                Notify(EEnvironmentChange::OverridePopped);
                return true;
            }
        }
        return false;
    }

    void EnvironmentSystem::PublishTo(Rendering::RenderWorld& renderWorld)
    {
        const auto& coordinator = renderWorld.GetRenderingCoordinator();
        const auto& camera = coordinator.GetMainCamera();
        const auto& grassField = coordinator.GetGrassField();
        m_RainNearGrassDensity = grassField ?
            SampleRainNearGrassDensity(*grassField, camera.PositionX, camera.PositionZ) : 0.0f;

        if (m_bEnabled)
        {
            if (m_bRainTuningPublishPending)
            {
                renderWorld.SetRainTuning(m_RainTuning);
                m_bRainTuningPublishPending = false;
            }

            const EnvironmentResolved resolved = Resolve();
            renderWorld.SetSkyAtmosphere(resolved.Sky);
            renderWorld.SetVolumetricFogParameters(resolved.Fog);
            renderWorld.SetCloudLayer(resolved.CloudLayer);
            renderWorld.SetWindParameters(resolved.Wind);
            renderWorld.SetEnvironmentParameters(resolved.Environment);
        }
        else if (m_bRestorePending)
        {
            renderWorld.SetEnvironmentParameters(Rendering::MakeDefaultEnvironmentParameters());
            m_bRestorePending = false;
        }
    }

    bool EnvironmentSystem::LoadRainNearEffect(Container::String& error)
    {
        Container::VariableArray<uint8_t> bytes;
        const Platform::FileOperationResult result = Platform::ReadFileBytes(RainNearEffectPath, bytes);
        if (!result.bSuccess)
        {
            error = "ファイルを読めません: ";
            error += result.Message;
            return false;
        }
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            offset = 3;
        }
        Container::String text;
        if (bytes.size() > offset)
        {
            text.append(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
        }
        Particle::VfxEffectAsset asset;
        if (!Particle::LoadVfxEffectFromJson(text, asset, error))
        {
            return false;
        }
        if (asset.Emitters.empty() || !(asset.Emitters[0].Name == "RainNear"))
        {
            error = "先頭のエミッタが RainNear ではありません";
            return false;
        }
        const Particle::VfxEmitterAsset& emitter = asset.Emitters[0];
        const Particle::VfxEmitterAsset* splash = nullptr;
        const Particle::VfxEmitterAsset* droplet = nullptr;
        for (const Particle::VfxEmitterAsset& candidate : asset.Emitters)
        {
            if (candidate.Name == "Splash")
            {
                splash = &candidate;
            }
            else if (candidate.Name == "Droplet")
            {
                droplet = &candidate;
            }
        }
        if (splash == nullptr || droplet == nullptr)
        {
            error = "Splash または Droplet のエミッタがありません";
            return false;
        }
        const Particle::VfxObjectSchema& emitterSchema = Particle::GetVfxEmitterSchema();
        const int32_t executorIndex = Particle::FindVfxParameterIndex(emitterSchema, "Executor");
        const Particle::VfxEmitterAsset* requiredEmitters[] = {&emitter, splash, droplet};
        for (const Particle::VfxEmitterAsset* required : requiredEmitters)
        {
            if (executorIndex < 0 || static_cast<uint32_t>(executorIndex) >= required->Properties.size() ||
                required->Properties[static_cast<uint32_t>(executorIndex)].UInt != static_cast<uint32_t>(Particle::VfxExecutor::Gpu))
            {
                error = "エミッタの実行先が GPU ではありません: ";
                error += required->Name;
                return false;
            }
        }
        const int32_t capacityIndex = Particle::FindVfxParameterIndex(emitterSchema, "Capacity");
        const int32_t maxParticlesIndex = Particle::FindVfxParameterIndex(emitterSchema, "MaxParticles");
        if (capacityIndex < 0 || maxParticlesIndex < 0 ||
            static_cast<uint32_t>(capacityIndex) >= emitter.Properties.size() ||
            static_cast<uint32_t>(maxParticlesIndex) >= emitter.Properties.size())
        {
            error = "近距離の雨の粒の数がありません";
            return false;
        }
        uint32_t lifetimeIndex = 0;
        if (!FindRainNearModule(*splash, Particle::VfxModuleType::InitLifetime, lifetimeIndex, error))
        {
            return false;
        }
        const Particle::VfxValue* lifetime = FindRainNearValue(splash->Modules[lifetimeIndex], "Lifetime");
        if (lifetime == nullptr || !std::isfinite(lifetime->Vector.x) || lifetime->Vector.x <= 0.0f)
        {
            error = "Splash の寿命がないか正の有限値ではありません";
            return false;
        }

        RainNearModules modules;
        if (!FindRainNearModule(emitter, Particle::VfxModuleType::InitShape, modules.Box, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::InitVelocityBox, modules.Velocity, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::InitColor, modules.Color, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::InitSize, modules.Size, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::Acceleration, modules.Acceleration, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::WindInfluence, modules.Wind, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::ActivationFraction, modules.Activation, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::GpuSpriteRenderer, modules.Renderer, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::CollisionHeightField, modules.HeightField, error) ||
            !FindRainNearModule(emitter, Particle::VfxModuleType::CollisionDepth, modules.Depth, error))
        {
            return false;
        }
        const Particle::VfxModule& box = emitter.Modules[modules.Box];
        const Particle::VfxModule& velocity = emitter.Modules[modules.Velocity];
        const Particle::VfxModule& wind = emitter.Modules[modules.Wind];
        const Particle::VfxValue* coupling = FindRainNearValue(wind, "Coupling");
        const Particle::VfxValue* scale = FindRainNearValue(wind, "Scale");
        if (FindRainNearValue(box, "CameraBoxHalfWidth") == nullptr ||
            FindRainNearValue(box, "CameraBoxHeight") == nullptr ||
            FindRainNearValue(velocity, "Min") == nullptr || FindRainNearValue(velocity, "Max") == nullptr ||
            coupling == nullptr || scale == nullptr)
        {
            error = "必要なモジュールの引数がありません";
            return false;
        }
        const uint32_t capacity = emitter.Properties[static_cast<uint32_t>(capacityIndex)].UInt;
        m_RainNearParticleCount = capacity != 0 ? capacity : emitter.Properties[static_cast<uint32_t>(maxParticlesIndex)].UInt;
        m_RainSplashLifetimeSeconds = lifetime->Vector.x;
        m_RainNearWindCoupling = coupling->Vector.x;
        m_RainNearWindScale = scale->Vector.x;
        m_RainNearModules = modules;
        m_RainNearAsset = asset;
        return true;
    }

    void EnvironmentSystem::SetRainNearParameter(Particle::VfxEffectSystem& effects, RainNearParameter parameter,
        uint32_t moduleIndex, const char* name, const Particle::VfxValue& value)
    {
        RainNearParameterState& previous = m_RainNearParameters[static_cast<uint32_t>(parameter)];
        if (previous.bSent && previous.Value == value)
        {
            return;
        }
        Container::String error;
        if (effects.SetModuleParameter(m_RainNearHandle, 0, moduleIndex, name, value, &error))
        {
            previous.Value = value;
            previous.bSent = true;
        }
        else if (!m_bRainNearParameterWarning)
        {
            NORVES_LOG_WARNING("General", "EnvironmentSystem の近距離の雨の引数を更新できません: %s: %s: %s",
                RainNearEffectPath, name, error.c_str());
            m_bRainNearParameterWarning = true;
        }
    }

    void EnvironmentSystem::UpdateRainNear(Particle::VfxEffectSystem& effects)
    {
        const float intensity = m_EffectiveState.RainIntensity;
        const bool bWant = m_bEnabled && m_bRainNearAssetReady && std::isfinite(intensity) && intensity > 0.0f;
        if (!bWant || m_bRainNearRecreatePending)
        {
            if (effects.IsLiveInstance(m_RainNearHandle))
            {
                effects.DestroyInstance(m_RainNearHandle);
            }
            m_RainNearHandle = {};
            m_bRainNearRecreatePending = false;
            if (!bWant)
            {
                return;
            }
        }
        if (!effects.IsLiveInstance(m_RainNearHandle))
        {
            Particle::VfxInstanceDesc desc;
            desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
            desc.Seed = RainNearSeed;
            Container::String error;
            m_RainNearHandle = effects.CreateInstance(m_RainNearAsset, desc, &error);
            if (!effects.IsLiveInstance(m_RainNearHandle))
            {
                if (!m_bRainNearCreateWarning)
                {
                    NORVES_LOG_WARNING("General", "EnvironmentSystem の近距離の雨を作成できません: %s: %s",
                        RainNearEffectPath, error.c_str());
                    m_bRainNearCreateWarning = true;
                }
                return;
            }
            for (RainNearParameterState& state : m_RainNearParameters)
            {
                state = {};
            }
            const Particle::VfxModule& velocity = m_RainNearAsset.Emitters[0].Modules[m_RainNearModules.Velocity];
            m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMin)].Value = *FindRainNearValue(velocity, "Min");
            m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMax)].Value = *FindRainNearValue(velocity, "Max");
        }

        const Rendering::WindParameters wind = Resolve().Wind;
        const Rendering::RainNearEvaluation rain = Rendering::EvaluateRainNear(intensity, wind, m_RainTuning);
        float windX = 0.0f;
        float windZ = 0.0f;
        if (wind.bEnabled)
        {
            Rendering::WindDirectionXZ(wind, windX, windZ);
            windX *= wind.BaseSpeed * m_RainNearWindScale;
            windZ *= wind.BaseSpeed * m_RainNearWindScale;
        }
        const float fall = rain.FallSpeedMetersPerSecond;
        const float variation = m_RainTuning.NearFallSpeedVariation;
        const Math::Vector3 velocityMin(windX, -fall * (1.0f + variation), windZ);
        const Math::Vector3 velocityMax(windX, -fall * (1.0f - variation), windZ);

        SetRainNearParameter(effects, RainNearParameter::BoxHalfWidth, m_RainNearModules.Box,
            "CameraBoxHalfWidth", Particle::VfxValue::MakeFloat(m_RainTuning.NearBoxHalfWidthMeters));
        SetRainNearParameter(effects, RainNearParameter::BoxHeight, m_RainNearModules.Box,
            "CameraBoxHeight", Particle::VfxValue::MakeFloat(m_RainTuning.NearBoxHeightMeters));

        // 下限と上限は個別に検証されるため、一度範囲を広げてから目的の値へ縮める。
        const Math::Vector4 oldMin = m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMin)].Value.Vector;
        const Math::Vector4 oldMax = m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMax)].Value.Vector;
        const Math::Vector3 expandedMin(oldMin.x < velocityMin.x ? oldMin.x : velocityMin.x,
            oldMin.y < velocityMin.y ? oldMin.y : velocityMin.y, oldMin.z < velocityMin.z ? oldMin.z : velocityMin.z);
        const Math::Vector3 expandedMax(oldMax.x > velocityMax.x ? oldMax.x : velocityMax.x,
            oldMax.y > velocityMax.y ? oldMax.y : velocityMax.y, oldMax.z > velocityMax.z ? oldMax.z : velocityMax.z);
        const Particle::VfxValue expandedMinValue = Particle::VfxValue::MakeVector3(expandedMin);
        const Particle::VfxValue expandedMaxValue = Particle::VfxValue::MakeVector3(expandedMax);
        if (!(m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMin)].Value == expandedMinValue))
        {
            SetRainNearParameter(effects, RainNearParameter::VelocityMin, m_RainNearModules.Velocity, "Min", expandedMinValue);
        }
        if (!(m_RainNearParameters[static_cast<uint32_t>(RainNearParameter::VelocityMax)].Value == expandedMaxValue))
        {
            SetRainNearParameter(effects, RainNearParameter::VelocityMax, m_RainNearModules.Velocity, "Max", expandedMaxValue);
        }
        SetRainNearParameter(effects, RainNearParameter::VelocityMin, m_RainNearModules.Velocity,
            "Min", Particle::VfxValue::MakeVector3(velocityMin));
        SetRainNearParameter(effects, RainNearParameter::VelocityMax, m_RainNearModules.Velocity,
            "Max", Particle::VfxValue::MakeVector3(velocityMax));
        SetRainNearParameter(effects, RainNearParameter::Color, m_RainNearModules.Color, "Color",
            Particle::VfxValue::MakeColor(Math::Vector4(m_RainTuning.NearColorScale[0], m_RainTuning.NearColorScale[1],
                m_RainTuning.NearColorScale[2], rain.Opacity)));
        SetRainNearParameter(effects, RainNearParameter::Size, m_RainNearModules.Size, "Size",
            Particle::VfxValue::MakeVector2(Math::Vector2(rain.ThicknessMeters, rain.ThicknessMeters)));
        // WindInfluence は Y 速度も 0 へ寄せるため、落下速度に Coupling を掛けた加速度で補う。
        // これにより定常速度は（風、-fall）となり、雨筋の傾きは風速と落下速度で決まる。
        SetRainNearParameter(effects, RainNearParameter::Acceleration, m_RainNearModules.Acceleration, "Acceleration",
            Particle::VfxValue::MakeVector3(Math::Vector3(0.0f, -fall * m_RainNearWindCoupling, 0.0f)));
        SetRainNearParameter(effects, RainNearParameter::Fraction, m_RainNearModules.Activation, "Fraction",
            Particle::VfxValue::MakeFloat(rain.ActiveFraction));
        SetRainNearParameter(effects, RainNearParameter::Shutter, m_RainNearModules.Renderer, "StreakShutterSeconds",
            Particle::VfxValue::MakeFloat(m_RainTuning.StreakShutterSeconds));
        SetRainNearParameter(effects, RainNearParameter::FadeDistance, m_RainNearModules.Renderer, "NearFadeDistance",
            Particle::VfxValue::MakeFloat(m_RainTuning.NearFadeFullMeters));

        const RainSplashBudgetInput splashInput{
            intensity,
            static_cast<float>(m_RainNearParticleCount) * rain.ActiveFraction,
            rain.FallSpeedMetersPerSecond,
            m_RainTuning.NearBoxHeightMeters,
            m_RainSplashLifetimeSeconds,
            m_RainNearGrassDensity
        };
        const RainSplashBudgetResult splashBudget = ComputeRainSplashBudget(splashInput, m_RainSplashBudget);
        SetRainNearParameter(effects, RainNearParameter::HeightFieldChildProbability, m_RainNearModules.HeightField,
            "ChildProbability", Particle::VfxValue::MakeFloat(splashBudget.ChildProbability));
        SetRainNearParameter(effects, RainNearParameter::DepthChildProbability, m_RainNearModules.Depth,
            "ChildProbability", Particle::VfxValue::MakeFloat(splashBudget.ChildProbability));
        SetRainNearParameter(effects, RainNearParameter::ShelterChildProbability, m_RainNearModules.HeightField,
            "ShelterChildProbability", Particle::VfxValue::MakeFloat(splashBudget.DripProbability));
        SetRainNearParameter(effects, RainNearParameter::ShelterMinClearance, m_RainNearModules.HeightField,
            "ShelterMinClearance", Particle::VfxValue::MakeFloat(m_RainSplashBudget.CanopyMinClearanceMeters));
    }

    MulticastDelegate<EEnvironmentChange, const EnvironmentState&>& EnvironmentSystem::OnChanged()
    {
        return m_OnChanged;
    }

    MulticastDelegate<double, double>& EnvironmentSystem::OnTimeSkipped()
    {
        return m_OnTimeSkipped;
    }

    void EnvironmentSystem::LoadClockData()
    {
        Container::String error;
        m_SunPath = SunPathSettings{};
        if (!LoadSunPath(m_PresetRoot, m_SunPath, &error))
        {
            WarnClockDataLoadFailure("SunPath.json", error);
        }
        m_NightLight = NightLightSettings{};
        if (!LoadNightLight(m_PresetRoot, m_NightLight, &error))
        {
            WarnClockDataLoadFailure("NightLight.json", error);
        }
        m_WeatherSchedule = MakeDefaultWeatherScheduleSettings();
        if (!LoadWeatherSchedule(m_PresetRoot, m_WeatherSchedule, &error))
        {
            WarnClockDataLoadFailure("WeatherSchedule.json", error);
        }
        m_bWeatherReady = false;
        m_WeatherPresets.clear();
        if (!LoadWeatherPresets(m_PresetRoot, m_WeatherSchedule, m_WeatherPresets, &error))
        {
            WarnClockDataLoadFailure("*.env.json", error);
            LOG_WARNING("EnvironmentSystem の時計は天候の資料が揃わないため太陽だけを動かします");
            return;
        }
        RebuildWeatherTrack();
    }

    void EnvironmentSystem::RebuildWeatherTrack()
    {
        m_bWeatherReady = !m_WeatherPresets.empty() &&
                          m_WeatherPresets.size() == m_WeatherSchedule.PresetNames.size() &&
                          MakeWeatherTrack(m_WeatherSchedule, m_BaseState.Seed, m_WeatherTrack);
        if (!m_bWeatherReady)
        {
            LOG_WARNING("EnvironmentSystem の天候の並びを作れないため時計は太陽だけを動かします");
        }
    }

    bool EnvironmentSystem::EvaluateClockWeather(double gameTimeSeconds, EnvironmentTarget& outTarget) const
    {
        if (!m_bWeatherReady)
        {
            return false;
        }
        const WeatherSample sample = SampleWeatherTrack(m_WeatherSchedule, m_WeatherTrack,
                                                       ClockHoursForGameTime(gameTimeSeconds));
        BlendWeatherPresets(m_WeatherPresets[sample.FromPreset], m_WeatherPresets[sample.ToPreset],
                            sample.Blend, outTarget);
        return true;
    }

    float EnvironmentSystem::ScheduledRainAt(double gameTimeSeconds) const
    {
        EnvironmentTarget target;
        return EvaluateClockWeather(gameTimeSeconds, target) &&
               (target.Mask & EnvironmentFieldBit(EEnvironmentField::RainIntensity)) != 0
            ? target.Values.RainIntensity : m_BaseState.RainIntensity;
    }

    float EnvironmentSystem::RestoreClockWetness(double windowStartSeconds, double endSeconds, float startWetness) const
    {
        float wetness = startWetness;
        double timeSeconds = windowStartSeconds;
        while (timeSeconds < endSeconds)
        {
            const double nextStepSeconds = timeSeconds + WetnessRestoreStepSeconds;
            const double nextTimeSeconds = nextStepSeconds < endSeconds ? nextStepSeconds : endSeconds;
            const double deltaSeconds = nextTimeSeconds - timeSeconds;
            float rain = ScheduledRainAt(timeSeconds + deltaSeconds * 0.5);
            for (const OverrideEntry& entry : m_Overrides)
            {
                if ((entry.Target.Mask & EnvironmentFieldBit(EEnvironmentField::RainIntensity)) != 0)
                {
                    rain = entry.Target.Values.RainIntensity;
                }
            }
            wetness = AdvanceEnvironmentWetness(wetness, rain, deltaSeconds,
                                                m_BaseState.WetTimeConstantSeconds,
                                                m_BaseState.DryTimeConstantSeconds);
            timeSeconds = nextTimeSeconds;
        }
        return wetness;
    }

    void EnvironmentSystem::ApplyClock()
    {
        if (!IsClockActive())
        {
            return;
        }
        const float hours = static_cast<float>(GetClockHours());
        m_BaseState.TimeOfDayHours = hours < 24.0f ? hours : 0.0f;
        const SunAltAz sun = ComputeSunAltAz(m_BaseState.TimeOfDayHours, m_SunPath);
        m_BaseState.SunAltitudeDegrees = sun.AltitudeDegrees;
        m_BaseState.SunAzimuthDegrees = sun.AzimuthDegrees;

        EnvironmentTarget target;
        if (!EvaluateClockWeather(m_BaseState.GameTimeSeconds, target))
        {
            return;
        }
        constexpr EnvironmentFieldMask excludedMask = EnvironmentFieldBit(EEnvironmentField::SunAltitude) |
            EnvironmentFieldBit(EEnvironmentField::SunAzimuth) | EnvironmentFieldBit(EEnvironmentField::TimeOfDayHours) |
            EnvironmentFieldBit(EEnvironmentField::Wetness);
        const EnvironmentFieldMask mask = target.Mask & ~excludedMask;
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const auto field = static_cast<EEnvironmentField>(index);
            if ((mask & EnvironmentFieldBit(field)) != 0)
            {
                SetEnvironmentField(m_BaseState, field, GetEnvironmentField(target.Values, field));
                m_Transitions[index].bActive = false;
            }
        }
    }

    void EnvironmentSystem::ClearTransitions()
    {
        for (FieldTransition& transition : m_Transitions)
        {
            transition = {};
        }
    }

    float EnvironmentSystem::EvaluateField(EEnvironmentField field, uint64_t step) const
    {
        const FieldTransition& transition = m_Transitions[static_cast<uint32_t>(field)];
        if (!transition.bActive)
        {
            return GetEnvironmentField(m_BaseState, field);
        }

        const double progress = static_cast<double>(step - transition.StartStep) /
                                static_cast<double>(StepsPerSecond) / transition.DurationSeconds;
        if (progress >= 1.0)
        {
            return transition.To;
        }
        if (field == EEnvironmentField::CloudLayerEnabled)
        {
            return (transition.From >= 0.5f || transition.To >= 0.5f) ? 1.0f : 0.0f;
        }

        const double curve = EvaluateTransitionCurve(transition.Curve, static_cast<float>(progress));
        const double period = FieldPeriod(field);
        if (period > 0.0)
        {
            double difference = std::fmod(static_cast<double>(transition.To) - transition.From, period);
            if (difference > 0.5 * period)
            {
                difference -= period;
            }
            else if (difference < -0.5 * period)
            {
                difference += period;
            }
            double wrapped = std::fmod(static_cast<double>(transition.From) + difference * curve, period);
            if (wrapped < 0.0)
            {
                wrapped += period;
            }
            const float result = static_cast<float>(wrapped);
            return result >= period ? 0.0f : result;
        }
        return static_cast<float>(static_cast<double>(transition.From) +
                                  (static_cast<double>(transition.To) - transition.From) * curve);
    }

    void EnvironmentSystem::RefreshFields()
    {
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            FieldTransition& transition = m_Transitions[index];
            const auto field = static_cast<EEnvironmentField>(index);
            if (!transition.bActive || field == EEnvironmentField::Wetness)
            {
                continue;
            }
            const double elapsed = static_cast<double>(m_StepCount - transition.StartStep) /
                                   static_cast<double>(StepsPerSecond);
            if (elapsed >= transition.DurationSeconds)
            {
                SetEnvironmentField(m_BaseState, field, transition.To);
                transition.bActive = false;
            }
            else
            {
                SetEnvironmentField(m_BaseState, field, EvaluateField(field, m_StepCount));
            }
        }
    }

    void EnvironmentSystem::RebuildEffective()
    {
        m_EffectiveState = m_BaseState;
        for (const OverrideEntry& entry : m_Overrides)
        {
            for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
            {
                const auto field = static_cast<EEnvironmentField>(index);
                if ((entry.Target.Mask & EnvironmentFieldBit(field)) != 0)
                {
                    SetEnvironmentField(m_EffectiveState, field, GetEnvironmentField(entry.Target.Values, field));
                }
            }
        }
    }

    void EnvironmentSystem::StepOnce()
    {
        float rain = IsClockActive() && m_bWeatherReady
            ? ScheduledRainAt(static_cast<double>(m_StepCount) / static_cast<double>(StepsPerSecond))
            : EvaluateField(EEnvironmentField::RainIntensity, m_StepCount);
        for (const OverrideEntry& entry : m_Overrides)
        {
            if ((entry.Target.Mask & EnvironmentFieldBit(EEnvironmentField::RainIntensity)) != 0)
            {
                rain = entry.Target.Values.RainIntensity;
            }
        }
        m_BaseState.Wetness = AdvanceEnvironmentWetness(m_BaseState.Wetness, rain,
                                                        1.0 / static_cast<double>(StepsPerSecond),
                                                        m_BaseState.WetTimeConstantSeconds,
                                                        m_BaseState.DryTimeConstantSeconds);
        ++m_StepCount;
    }

    void EnvironmentSystem::AdvanceNanoseconds(uint64_t nanoseconds)
    {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        m_TotalNanoseconds = nanoseconds > maximum - m_TotalNanoseconds
            ? maximum : m_TotalNanoseconds + nanoseconds;
        const uint64_t target = StepsForNanoseconds(m_TotalNanoseconds);
        while (m_StepCount < target)
        {
            StepOnce();
        }
        RefreshFields();
        m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        ApplyClock();
        RebuildEffective();
    }

    void EnvironmentSystem::Notify(EEnvironmentChange change)
    {
        m_OnChanged.Broadcast(change, m_EffectiveState);
    }
} // namespace NorvesLib::Core::Environment
