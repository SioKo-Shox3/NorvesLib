#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/View.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Logging/LogMacros.h"
#include "Thread/Mutex.h"
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        class RenderExtensionGuardPass final : public IViewPass, public IRenderGraphPass
        {
        public:
            RenderExtensionGuardPass(const Container::String& name, Container::TUniquePtr<IViewPass> pass)
                : m_Name(name), m_Pass(std::move(pass)), m_GraphPass(dynamic_cast<IRenderGraphPass*>(m_Pass.get()))
            {
            }

            ~RenderExtensionGuardPass() override
            {
                Shutdown();
            }

            const char* GetName() const override { return m_Name.c_str(); }

            bool Initialize(ViewRenderContext& context) override
            {
                if (m_bInitialized)
                {
                    return true;
                }
                m_bInitialized = true;
                if (!m_bFailed)
                {
                    m_bInitializationAttempted = true;
                    try
                    {
                        if (!m_Pass->Initialize(context))
                        {
                            Fail("Initialize");
                        }
                    }
                    catch (...)
                    {
                        Fail("Initialize");
                    }
                }
                // 拡張の失敗だけで View 全体の描画を止めない。
                return true;
            }

            void Shutdown() override
            {
                if (m_bInitializationAttempted)
                {
                    m_bInitializationAttempted = false;
                    // 無効化後も、初期化途中で確保した資源は回収する。
                    try
                    {
                        m_Pass->Shutdown();
                    }
                    catch (...)
                    {
                        Fail("Shutdown");
                    }
                }
                m_bInitialized = false;
            }

            void Setup(ViewRenderContext& context) override
            {
                Forward("Setup", [&] { m_Pass->Setup(context); });
            }

            void Execute(ViewRenderContext& context) override
            {
                Forward("Execute", [&] { m_Pass->Execute(context); });
            }

            void OnAssignedToPacket(uint32_t slotIndex) override
            {
                Forward("OnAssignedToPacket", [&] { m_Pass->OnAssignedToPacket(slotIndex); });
            }

            void Declare(RenderGraphBuilder& builder) override
            {
                // 宣言のない無効パスでも資源を作らず、錨からの順序だけを保つ。
                builder.PreserveInsertionOrder();
                Forward("Declare", [&] { m_GraphPass->Declare(builder); });
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                Forward("Execute", [&] { m_GraphPass->Execute(resources, context); });
            }

            bool DeclaresAllLayoutChanges() const override
            {
                return Query("DeclaresAllLayoutChanges", false, [&] { return m_GraphPass->DeclaresAllLayoutChanges(); });
            }

            RGRecordPolicy GetRecordPolicy() const override
            {
                return Query("GetRecordPolicy", RGRecordPolicy::Serial, [&] { return m_GraphPass->GetRecordPolicy(); });
            }

            bool ReadsRecordingOf(const IRenderGraphPass& earlierPass) const override
            {
                const auto* guard = dynamic_cast<const RenderExtensionGuardPass*>(&earlierPass);
                const IRenderGraphPass& inner = guard ? *guard->m_GraphPass : earlierPass;
                return Query("ReadsRecordingOf", false, [&] { return m_GraphPass->ReadsRecordingOf(inner); });
            }

            void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                Forward("PrepareParallelRecord", [&] { m_GraphPass->PrepareParallelRecord(resources, context); });
            }

            void FinishParallelRecord(const ViewRenderContext& recordedContext, ViewRenderContext& context) override
            {
                Forward("FinishParallelRecord", [&] { m_GraphPass->FinishParallelRecord(recordedContext, context); });
            }

            void SetExtensionEnabled(bool bEnabled)
            {
                SetEnabled(bEnabled && !m_bFailed);
                m_Pass->SetEnabled(IsEnabled());
            }

        private:
            void Fail(const char* operation)
            {
                if (!m_bFailed)
                {
                    NORVES_LOG_ERROR("RenderExtension", "拡張 '%s' の処理に失敗したため無効にしました: %s", GetName(), operation);
                }
                m_bFailed = true;
                SetEnabled(false);
            }

            template <typename Fn>
            void Forward(const char* operation, Fn&& function)
            {
                if (!IsEnabled() || m_bFailed)
                {
                    return;
                }
                try
                {
                    function();
                }
                catch (...)
                {
                    Fail(operation);
                }
            }

            template <typename Result, typename Fn>
            Result Query(const char* operation, Result fallback, Fn&& function) const
            {
                if (!IsEnabled() || m_bFailed)
                {
                    return fallback;
                }
                try
                {
                    return function();
                }
                catch (...)
                {
                    const_cast<RenderExtensionGuardPass*>(this)->Fail(operation);
                    return fallback;
                }
            }

            Container::String m_Name;
            Container::TUniquePtr<IViewPass> m_Pass;
            IRenderGraphPass* m_GraphPass = nullptr;
            bool m_bFailed = false;
            bool m_bInitializationAttempted = false;
        };

        const char* GetAnchor(RenderExtensionStage stage)
        {
            switch (stage)
            {
            case RenderExtensionStage::GBuffer: return "FoliagePass";
            case RenderExtensionStage::AfterLighting: return "LightingPass";
            case RenderExtensionStage::Transparent: return "ForwardPass";
            case RenderExtensionStage::PostTemporalAA: return "TemporalAAPass";
            case RenderExtensionStage::PostTonemap: return "VignettePass";
            default: return nullptr;
            }
        }

        bool IsPostProcessStage(RenderExtensionStage stage)
        {
            return stage == RenderExtensionStage::PostTemporalAA || stage == RenderExtensionStage::PostTonemap;
        }

        bool Reject(const char* name, const char* reason)
        {
            NORVES_LOG_WARNING("RenderExtension", "拡張 '%s' の登録を拒否しました: %s", name, reason);
            return false;
        }
    } // namespace

    struct RenderExtensionRegistry::State
    {
        struct Entry
        {
            RenderExtensionDesc Desc;
            RenderExtensionFactory Factory;
            uint64_t Sequence = 0;
            bool bEnabled = true;
            bool bApplied = false;
            bool bRemovalPending = false;
            bool bCreationFailed = false;
        };

        enum class Operation : uint8_t { Register, Unregister, SetEnabled };

        struct Pending
        {
            Operation Type;
            uint64_t Sequence;
            bool bEnabled = true;
        };

        mutable Thread::Mutex Mutex;
        View* Owner = nullptr;
        Container::VariableArray<Entry> Entries;
        Container::VariableArray<Pending> PendingOperations;
        uint64_t NextSequence = 0;
        bool bInitializationWindow = true;
        bool bPathTracingPipeline = false;
        bool bPathTracingNoted = false;
        bool bMissingAnchorNoted[5] = {};

        Entry* Find(const char* name)
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

        IViewPass* FindPass(const Entry& entry) const
        {
            if (!Owner)
            {
                return nullptr;
            }
            if (IsPostProcessStage(entry.Desc.Stage))
            {
                PostProcessStack* stack = Owner->GetPostProcessStack();
                return stack ? stack->GetPass(entry.Desc.Name.c_str()) : nullptr;
            }
            return Owner->FindPass(entry.Desc.Name.c_str());
        }

        void Insert(Entry& entry)
        {
            if (!Owner || !entry.bApplied || entry.bCreationFailed || bPathTracingPipeline || FindPass(entry))
            {
                return;
            }

            const bool bPost = IsPostProcessStage(entry.Desc.Stage);
            PostProcessStack* stack = bPost ? Owner->GetPostProcessStack() : nullptr;
            const uint32_t count = bPost ? (stack ? stack->GetPassCount() : 0u) : Owner->GetPassCount();
            auto getPass = [&](uint32_t index) -> IViewPass*
            {
                return bPost ? stack->GetPasses()[index].get() : Owner->GetPassAt(index);
            };

            uint32_t position = count;
            bool bFoundAnchor = false;
            const char* anchor = GetAnchor(entry.Desc.Stage);
            for (uint32_t index = 0; index < count; ++index)
            {
                IViewPass* pass = getPass(index);
                if (pass && std::strcmp(pass->GetName(), anchor) == 0)
                {
                    position = index + 1;
                    bFoundAnchor = true;
                    break;
                }
            }
            if (!bFoundAnchor)
            {
                const uint32_t stageIndex = static_cast<uint32_t>(entry.Desc.Stage);
                if (!bMissingAnchorNoted[stageIndex])
                {
                    NORVES_LOG_WARNING("RenderExtension", "拡張の差し込み先がありません: stage=%u anchor=%s", stageIndex, anchor);
                    bMissingAnchorNoted[stageIndex] = true;
                }
                return;
            }

            // 小さい Order、同じ Order なら早い登録の後ろに置き、既存の実体には触れない。
            for (uint32_t index = position; index < count; ++index)
            {
                IViewPass* pass = getPass(index);
                Entry* other = pass ? Find(pass->GetName()) : nullptr;
                if (!other || other->Desc.Stage != entry.Desc.Stage || other->Desc.Order > entry.Desc.Order ||
                    (other->Desc.Order == entry.Desc.Order && other->Sequence > entry.Sequence))
                {
                    break;
                }
                position = index + 1;
            }

            Container::TUniquePtr<IViewPass> pass;
            try
            {
                pass = entry.Factory.Create(entry.Factory.Context);
            }
            catch (...)
            {
                entry.bCreationFailed = true;
                Reject(entry.Desc.Name.c_str(), "ファクトリが例外を送出しました");
                return;
            }
            if (!pass || !dynamic_cast<IRenderGraphPass*>(pass.get()))
            {
                entry.bCreationFailed = true;
                Reject(entry.Desc.Name.c_str(), "ファクトリがネイティブの描画パスを返しませんでした");
                return;
            }

            auto guard = Container::MakeUnique<RenderExtensionGuardPass>(entry.Desc.Name, std::move(pass));
            guard->SetExtensionEnabled(entry.bEnabled);
            if (bPost)
            {
                stack->InsertPass(position, std::move(guard));
            }
            else
            {
                Owner->InsertPass(position, std::move(guard));
            }
        }

        void Apply()
        {
            for (const Pending& pending : PendingOperations)
            {
                for (auto it = Entries.begin(); it != Entries.end(); ++it)
                {
                    if (it->Sequence != pending.Sequence)
                    {
                        continue;
                    }
                    if (pending.Type == Operation::Unregister)
                    {
                        if (Owner)
                        {
                            if (IsPostProcessStage(it->Desc.Stage))
                            {
                                if (PostProcessStack* stack = Owner->GetPostProcessStack())
                                {
                                    stack->RemovePass(it->Desc.Name.c_str());
                                }
                            }
                            else
                            {
                                Owner->RemovePass(it->Desc.Name.c_str());
                            }
                        }
                        Entries.erase(it);
                    }
                    else if (pending.Type == Operation::Register)
                    {
                        it->bApplied = true;
                    }
                    else
                    {
                        it->bEnabled = pending.bEnabled;
                        if (auto* guard = dynamic_cast<RenderExtensionGuardPass*>(FindPass(*it)))
                        {
                            guard->SetExtensionEnabled(pending.bEnabled);
                        }
                    }
                    break;
                }
            }
            PendingOperations.clear();
            for (Entry& entry : Entries)
            {
                Insert(entry);
            }
        }
    };

    RenderExtensionRegistry::RenderExtensionRegistry(View* owner)
        : m_State(Container::MakeUnique<State>())
    {
        m_State->Owner = owner;
    }

    RenderExtensionRegistry::~RenderExtensionRegistry() = default;

    void RenderExtensionRegistry::SetOwner(View* owner)
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->Owner = owner;
    }

    bool RenderExtensionRegistry::Register(const RenderExtensionDesc& desc, const RenderExtensionFactory& factory)
    {
        const char* name = desc.Name.c_str();
        if (desc.Name.empty() || std::strncmp(name, "Ext.", 4) != 0)
        {
            return Reject(name, "名前は Ext.<Game>.<Name> の形式で指定してください");
        }
        if (!factory.Create)
        {
            return Reject(name, "ファクトリが指定されていません");
        }
        if (desc.Stage == RenderExtensionStage::ShadowCaster)
        {
            return Reject(name, "ShadowCaster にはまだ差し込み先がありません");
        }
        if (!GetAnchor(desc.Stage))
        {
            return Reject(name, "描画の段が不正です");
        }

        Thread::ScopedLock lock(m_State->Mutex);
        if (m_State->Find(name))
        {
            return Reject(name, "同じ名前が登録済みか保留中です");
        }
        // 型の検査だけを行い、試しの実体は初期化せず破棄する。
        try
        {
            auto probe = factory.Create(factory.Context);
            if (!probe || !dynamic_cast<IRenderGraphPass*>(probe.get()))
            {
                return Reject(name, "IViewPass と IRenderGraphPass の両方を実装してください");
            }
        }
        catch (...)
        {
            return Reject(name, "ファクトリが例外を送出しました");
        }

        const uint64_t sequence = m_State->NextSequence++;
        m_State->Entries.push_back({desc, factory, sequence});
        m_State->PendingOperations.push_back({State::Operation::Register, sequence});
        if (m_State->bInitializationWindow)
        {
            m_State->Apply();
        }
        return true;
    }

    bool RenderExtensionRegistry::Unregister(const char* name)
    {
        Thread::ScopedLock lock(m_State->Mutex);
        State::Entry* entry = m_State->Find(name);
        if (!entry || entry->bRemovalPending)
        {
            return false;
        }
        entry->bRemovalPending = true;
        m_State->PendingOperations.push_back({State::Operation::Unregister, entry->Sequence});
        if (m_State->bInitializationWindow)
        {
            m_State->Apply();
        }
        return true;
    }

    bool RenderExtensionRegistry::SetEnabled(const char* name, bool bEnabled)
    {
        Thread::ScopedLock lock(m_State->Mutex);
        State::Entry* entry = m_State->Find(name);
        if (!entry || entry->bRemovalPending)
        {
            return false;
        }
        m_State->PendingOperations.push_back({State::Operation::SetEnabled, entry->Sequence, bEnabled});
        if (m_State->bInitializationWindow)
        {
            m_State->Apply();
        }
        return true;
    }

    void RenderExtensionRegistry::ApplyPending()
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->bInitializationWindow = false;
        m_State->Apply();
    }

    void RenderExtensionRegistry::ReinsertAll()
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->bPathTracingPipeline = false;
        for (State::Entry& entry : m_State->Entries)
        {
            entry.bCreationFailed = false;
            m_State->Insert(entry);
        }
    }

    void RenderExtensionRegistry::NotePathTracingPipeline()
    {
        Thread::ScopedLock lock(m_State->Mutex);
        m_State->bPathTracingPipeline = true;
        if (!m_State->Entries.empty() && !m_State->bPathTracingNoted)
        {
            NORVES_LOG_WARNING("RenderExtension", "パストレーシングでは登録された描画拡張を実行しません");
            m_State->bPathTracingNoted = true;
        }
    }

    uint32_t RenderExtensionRegistry::GetRegisteredCount() const
    {
        Thread::ScopedLock lock(m_State->Mutex);
        return static_cast<uint32_t>(m_State->Entries.size());
    }
} // namespace NorvesLib::Core::Rendering
