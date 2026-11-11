#include "Rendering/GBufferProgramRegistry.h"

#include "Logging/LogMacros.h"
#include "Text/IdentityPool.h"
#include "Thread/Mutex.h"

#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool Reject(const char* name, const char* reason)
        {
            NORVES_LOG_WARNING("GBufferProgram", "プログラム '%s' の登録を拒否しました: %s", name, reason);
            return false;
        }
    } // namespace

    GBufferProgramId MakeGBufferProgramId(const char* name)
    {
        if (!name || !*name)
        {
            return kStandardGBufferProgramId;
        }

        // エンジン共通の FNV-1a を 32 ビットに畳み、標準の ID を予約する。
        const uint64_t hash = Fnv1aRaw(name, std::strlen(name));
        const GBufferProgramId id = static_cast<GBufferProgramId>(hash ^ (hash >> 32));
        return id == kStandardGBufferProgramId ? 1u : id;
    }

    GBufferPipelineKey MakeGBufferPipelineKey(const MaterialResourceData* material, bool bSkinned, bool bWireframe)
    {
        GBufferPipelineKey key;
        key.Program = material ? material->ProgramId : kStandardGBufferProgramId;
        // 既存の見た目を保つため、両面と切り抜きは後続の G5-GR27-MASKED-GBUFFER で接続する。
        // 現時点では bTwoSided と Blend を読まず、背面カリング・切り抜きなしに固定する。
        key.Cull = RHI::CullMode::Back;
        key.bAlphaClip = false;
        key.Polygon = bWireframe ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill;
        key.bSkinned = bSkinned;
        return key;
    }

    struct GBufferProgramRegistry::State
    {
        struct Entry
        {
            GBufferProgramDesc Desc;
            GBufferProgramId Id = kStandardGBufferProgramId;
            bool bRemovalPending = false;
        };

        enum class Operation : uint8_t { Register, Unregister };

        struct Pending
        {
            Operation Type;
            GBufferProgramId Id;
        };

        Thread::Mutex Mutex;
        // ゲームスレッドが更新する索引と描画スレッドが参照する表を分ける。
        Container::VariableArray<Entry> Entries;
        Container::VariableArray<Pending> PendingOperations;
        Container::UnorderedMap<GBufferProgramId, GBufferProgramDesc> Applied;
        uint64_t Revision = 0;
        bool bInitializationWindow = true;

        Entry* FindByName(const char* name)
        {
            if (name)
            {
                for (Entry& entry : Entries)
                {
                    if (entry.Desc.Name == name)
                    {
                        return &entry;
                    }
                }
            }
            return nullptr;
        }

        void Apply()
        {
            for (const Pending& pending : PendingOperations)
            {
                for (auto it = Entries.begin(); it != Entries.end(); ++it)
                {
                    if (it->Id != pending.Id)
                    {
                        continue;
                    }
                    if (pending.Type == Operation::Register)
                    {
                        Applied.emplace(it->Id, it->Desc);
                        ++Revision;
                    }
                    else
                    {
                        Applied.erase(it->Id);
                        Entries.erase(it);
                        ++Revision;
                    }
                    break;
                }
            }
            PendingOperations.clear();
        }
    };

    GBufferProgramRegistry::GBufferProgramRegistry()
        : m_State(Container::MakeUnique<State>())
    {
    }

    GBufferProgramRegistry::~GBufferProgramRegistry() = default;

    bool GBufferProgramRegistry::Register(const GBufferProgramDesc& desc)
    {
        const char* name = desc.Name.c_str();
        if (desc.Name.empty() || std::strlen(name) != desc.Name.size())
        {
            return Reject(name, "名前を指定してください");
        }
        if (desc.FragmentShader.empty() || std::strlen(desc.FragmentShader.c_str()) != desc.FragmentShader.size())
        {
            return Reject(name, "フラグメントシェーダーを指定してください");
        }

        const GBufferProgramId id = MakeGBufferProgramId(name);
        Thread::ScopedLock lock(m_State->Mutex);
        for (const State::Entry& entry : m_State->Entries)
        {
            if (entry.Desc.Name == name)
            {
                return Reject(name, "同じ名前が登録済みか保留中です");
            }
            if (entry.Id == id)
            {
                // 異なる名前が同じ ID に衝突すると誤ったシェーダーを返すため拒否する。
                return Reject(name, "別の名前とプログラム ID が衝突しています");
            }
        }

        m_State->Entries.push_back({desc, id, false});
        m_State->PendingOperations.push_back({State::Operation::Register, id});
        if (m_State->bInitializationWindow)
        {
            m_State->Apply();
        }
        return true;
    }

    bool GBufferProgramRegistry::Unregister(const char* name)
    {
        Thread::ScopedLock lock(m_State->Mutex);
        State::Entry* entry = m_State->FindByName(name);
        if (!entry || entry->bRemovalPending)
        {
            return false;
        }
        entry->bRemovalPending = true;
        m_State->PendingOperations.push_back({State::Operation::Unregister, entry->Id});
        if (m_State->bInitializationWindow)
        {
            m_State->Apply();
        }
        return true;
    }

    void GBufferProgramRegistry::CloseInitializationWindow()
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->bInitializationWindow = false;
    }

    void GBufferProgramRegistry::ApplyPending()
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->bInitializationWindow = false;
        m_State->Apply();
    }

    const GBufferProgramDesc* GBufferProgramRegistry::Find(GBufferProgramId id) const
    {
        const auto it = m_State->Applied.find(id);
        return it == m_State->Applied.end() ? nullptr : &it->second;
    }

    uint64_t GBufferProgramRegistry::GetRevision() const
    {
        return m_State->Revision;
    }
} // namespace NorvesLib::Core::Rendering
