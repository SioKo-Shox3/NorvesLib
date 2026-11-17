// Entity に付けて効果のインスタンスを 1 つ持ち、所有者の描画の位置へ追従させる Component
#include "Component/ParticleEmitterComponent.h"

#include "Engine/Engine.h"
#include "Engine/TimeChannels.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Particle/ParticleSystem.h"
#include "Particle/VfxEffectSerialization.h"
#include "Platform/PlatformFile.h"

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool LoadEffect(const Container::String& path, Particle::VfxEffectAsset& asset, Container::String& error)
        {
            Container::VariableArray<uint8_t> bytes;
            const auto read = Platform::ReadFileBytes(path, bytes);
            if (!read.bSuccess)
            {
                error = read.Message;
                return false;
            }

            uint32_t magic = 0u;
            if (bytes.size() >= 4u)
            {
                for (uint32_t i = 0u; i < 4u; ++i)
                {
                    magic |= static_cast<uint32_t>(bytes[i]) << (i * 8u);
                }
            }
            if (magic == Particle::kVfxCookedMagic)
            {
                return Particle::LoadCookedVfxEffect({bytes.data(), bytes.size()}, asset, error);
            }

            // AnimatorComponent と同じく相対パスは作業ディレクトリ基準で読み、UTF-8 の BOM を除く。
            const size_t skip = bytes.size() >= 3u && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3u : 0u;
            Container::String text;
            if constexpr (sizeof(TCHAR) == 1)
            {
                for (size_t i = skip; i < bytes.size(); ++i)
                {
                    text.push_back(TCHAR(bytes[i]));
                }
            }
            else if (bytes.size() > skip)
            {
                const size_t length = bytes.size() - skip;
                if (length > static_cast<size_t>(std::numeric_limits<int>::max()))
                {
                    error = "効果の JSON が大きすぎます";
                    return false;
                }
                const char* data = reinterpret_cast<const char*>(bytes.data() + skip);
                const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0);
                Container::VariableArray<wchar_t> wide;
                if (count <= 0)
                {
                    error = "効果の JSON が有効な UTF-8 ではありません";
                    return false;
                }
                wide.resize(static_cast<size_t>(count));
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), wide.data(), count) != count)
                {
                    error = "効果の JSON を文字列に変換できません";
                    return false;
                }
                for (const auto unit : wide)
                {
                    text.push_back(TCHAR(unit));
                }
            }
            return Particle::LoadVfxEffectFromJson(text, asset, error);
        }
    } // 読み込み用の補助関数

    IMPLEMENT_CLASS(ParticleEmitterComponent, Component)

    ParticleEmitterComponent::ParticleEmitterComponent()
        : Component()
    {
        SetDefaults();
    }

    ParticleEmitterComponent::ParticleEmitterComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    ParticleEmitterComponent::ParticleEmitterComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    ParticleEmitterComponent::~ParticleEmitterComponent()
    {
        ReleaseInstance();
    }

    void ParticleEmitterComponent::SetDefaults()
    {
        getEffectPath() = Container::String();
        getSeed() = Particle::VfxInstanceDesc{}.Seed;
        getEnabled() = true;
        SetTickEnabled(false);
    }

    void ParticleEmitterComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();
        RecreateInstance();
    }

    void ParticleEmitterComponent::RecreateInstance()
    {
        // 作成の入口を一つにし、古いインスタンスを返してから次を一つだけ作る。
        ReleaseInstance();
        Engine::Engine* engine = Engine::GEngine;
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!engine || !owner || !world)
        {
            LOG_WARNING("PARTICLE_EMITTER_COMPONENT_SKIPPED status=MissingEngineOrOwnerOrWorld エンジン・所有者・World のいずれかがありません");
            return;
        }
        if (GetEffectPath().empty())
        {
            LOG_WARNING("PARTICLE_EMITTER_COMPONENT_SKIPPED status=EmptyPath 効果のパスがありません");
            return;
        }

        Particle::VfxEffectAsset asset;
        Container::String error;
        if (!LoadEffect(GetEffectPath(), asset, error))
        {
            LOG_WARNING("PARTICLE_EMITTER_COMPONENT_SKIPPED status=LoadFailed path=%s reason=%s 効果を読み込めません",
                        GetEffectPath().c_str(), error.c_str());
            return;
        }

        Particle::VfxInstanceDesc desc;
        desc.Position = owner->GetRenderWorldTransform().position;
        desc.Seed = GetSeed();
        desc.bEnabled = IsEnabled();
        auto& system = engine->GetParticleSystem().GetVfxEffects();
        const auto handle = system.CreateInstance(asset, desc, &error);
        if (!system.IsLiveInstance(handle))
        {
            LOG_WARNING("PARTICLE_EMITTER_COMPONENT_SKIPPED status=CreateFailed path=%s reason=%s 効果のインスタンスを作れません",
                        GetEffectPath().c_str(), error.c_str());
            return;
        }
        m_RegisteredEngine = engine;
        m_RegisteredSystem = &system;
        m_EffectHandle = handle;
        for (const auto& entry : m_Overrides)
        {
            if (!ApplyOverride(entry, error))
            {
                LOG_WARNING("PARTICLE_EMITTER_COMPONENT_OVERRIDE status=ApplyFailed emitter=%u module=%u name=%s reason=%s 効果の上書きを適用できません",
                            entry.EmitterIndex, entry.ModuleIndex, entry.Name.c_str(), error.c_str());
            }
        }

        if (!world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "ParticleEmitter.Follow";
            tick.Run = [](Component& component, float)
            { static_cast<ParticleEmitterComponent&>(component).FollowOwner(); };
            tick.TimeChannel = Engine::TimeChannel::Particle;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
    }

    Particle::VfxEffectSystem* ParticleEmitterComponent::GetLiveSystem() const
    {
        // 終了後や別のエンジンへ切り替わった後は、保存した系を参照しない。
        if (!m_RegisteredEngine || Engine::GEngine != m_RegisteredEngine || !m_RegisteredSystem)
        {
            return nullptr;
        }
        return m_RegisteredSystem->IsLiveInstance(m_EffectHandle) ? m_RegisteredSystem : nullptr;
    }

    Particle::VfxEffectHandle ParticleEmitterComponent::GetEffectHandle() const
    {
        return GetLiveSystem() ? m_EffectHandle : Particle::VfxEffectHandle{};
    }

    void ParticleEmitterComponent::ReleaseInstance()
    {
        if (auto* system = GetLiveSystem())
        {
            system->DestroyInstance(m_EffectHandle);
        }
        // EndPlay の後にデストラクタが来ても、同じハンドルは二度返さない。
        m_EffectHandle = {};
        m_RegisteredSystem = nullptr;
        m_RegisteredEngine = nullptr;
        SetTickEnabled(false);
    }

    void ParticleEmitterComponent::EndPlay()
    {
        ReleaseInstance();
        Component::EndPlay();
    }

    void ParticleEmitterComponent::FollowOwner()
    {
        auto* system = GetLiveSystem();
        Entity* owner = GetOwner();
        if (system && owner)
        {
            system->SetInstancePosition(m_EffectHandle, owner->GetRenderWorldTransform().position);
        }
    }

    void ParticleEmitterComponent::SetEffectPath(const Container::String& path)
    {
        if (GetEffectPath() == path)
        {
            return;
        }
        getEffectPath() = path;
        if (bBegunPlay)
        {
            RecreateInstance();
        }
    }

    void ParticleEmitterComponent::SetSeed(uint32_t seed)
    {
        if (GetSeed() == seed)
        {
            return;
        }
        getSeed() = seed;
        if (bBegunPlay)
        {
            RecreateInstance();
        }
    }

    void ParticleEmitterComponent::SetEnabled(bool bEnabled)
    {
        getEnabled() = bEnabled;
        if (auto* system = GetLiveSystem())
        {
            system->SetInstanceEnabled(m_EffectHandle, bEnabled);
        }
    }

    bool ParticleEmitterComponent::ApplyOverride(const EffectOverride& entry, Container::String& error)
    {
        auto* system = GetLiveSystem();
        if (!system)
        {
            return false;
        }
        return entry.bModuleParameter
            ? system->SetModuleParameter(m_EffectHandle, entry.EmitterIndex, entry.ModuleIndex, entry.Name.c_str(), entry.Value, &error)
            : system->SetEmitterProperty(m_EffectHandle, entry.EmitterIndex, entry.Name.c_str(), entry.Value, &error);
    }

    bool ParticleEmitterComponent::StoreOverride(const EffectOverride& entry)
    {
        Container::String error;
        if (GetLiveSystem() && !ApplyOverride(entry, error))
        {
            LOG_WARNING("PARTICLE_EMITTER_COMPONENT_OVERRIDE status=ApplyFailed emitter=%u module=%u name=%s reason=%s 効果の上書きを適用できません",
                        entry.EmitterIndex, entry.ModuleIndex, entry.Name.c_str(), error.c_str());
            return false;
        }
        for (auto& stored : m_Overrides)
        {
            if (stored.bModuleParameter == entry.bModuleParameter && stored.EmitterIndex == entry.EmitterIndex &&
                stored.ModuleIndex == entry.ModuleIndex && stored.Name == entry.Name)
            {
                stored.Value = entry.Value;
                return true;
            }
        }
        m_Overrides.push_back(entry);
        return true;
    }

    bool ParticleEmitterComponent::SetEmitterPropertyOverride(uint32_t emitterIndex, const char* propertyName,
                                                              const Particle::VfxValue& value)
    {
        if (!propertyName || !propertyName[0])
        {
            return false;
        }
        EffectOverride entry;
        entry.EmitterIndex = emitterIndex;
        entry.Name = propertyName;
        entry.Value = value;
        return StoreOverride(entry);
    }

    bool ParticleEmitterComponent::SetModuleParameterOverride(uint32_t emitterIndex, uint32_t moduleIndex,
                                                              const char* parameterName, const Particle::VfxValue& value)
    {
        if (!parameterName || !parameterName[0])
        {
            return false;
        }
        EffectOverride entry;
        entry.EmitterIndex = emitterIndex;
        entry.ModuleIndex = moduleIndex;
        entry.Name = parameterName;
        entry.Value = value;
        entry.bModuleParameter = true;
        return StoreOverride(entry);
    }

    void ParticleEmitterComponent::ClearOverrides()
    {
        if (m_Overrides.empty())
        {
            return;
        }
        m_Overrides.clear();
        if (bBegunPlay)
        {
            RecreateInstance();
        }
    }
} // コンポーネントの名前空間
