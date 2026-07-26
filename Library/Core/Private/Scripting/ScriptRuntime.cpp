#include "Scripting/ScriptRuntime.h"

#include "Component/ScriptComponent.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Scripting/AngelScriptEngineOwner.h"
#include "Scripting/ScriptSourceTracker.h"
#include "Thread/Thread.h"
#include "Debug/Stats.h"

#include <angelscript.h>

#include <cstdlib>
#include <new>

namespace NorvesLib::Core
{
    namespace
    {
        struct EntityRef
        {
            void* Runtime;
            uint32_t SlotIndex;
            uint32_t Generation;
        };

        struct ScriptVector3
        {
            float x;
            float y;
            float z;
        };

        ScriptVector3 MakeScriptVector3(const Math::Vector3& vector)
        {
            return ScriptVector3{vector.x, vector.y, vector.z};
        }

        Math::Vector3 MakeMathVector3(const ScriptVector3& vector)
        {
            return Math::Vector3(vector.x, vector.y, vector.z);
        }

        Container::AnsiString MakeModuleName(uint32_t slotIndex, uint32_t generation)
        {
            char text[96]{};
            sprintf_s(text, "NorvesScript_%u_%u", slotIndex, generation);
            return Container::AnsiString(text);
        }

        bool HasPublicDefaultConstructor(asITypeInfo* type)
        {
            if (type == nullptr)
            {
                return false;
            }

            for (asUINT index = 0; index < type->GetBehaviourCount(); ++index)
            {
                asEBehaviours behaviour = asBEHAVE_CONSTRUCT;
                asIScriptFunction* function = type->GetBehaviourByIndex(index, &behaviour);
                if (function != nullptr && behaviour == asBEHAVE_CONSTRUCT && !function->IsPrivate() &&
                    function->GetParamCount() == 0)
                {
                    return true;
                }
            }
            return false;
        }
    }

    class ScriptRuntime::Impl
    {
    public:
        struct BindingSlot
        {
            Component::ScriptComponent* Component = nullptr;
            asIScriptObject* Object = nullptr;
            asITypeInfo* Type = nullptr;
            asIScriptFunction* BeginPlay = nullptr;
            asIScriptFunction* Tick = nullptr;
            asIScriptFunction* EndPlay = nullptr;
            Container::AnsiString ModuleName;
            uint32_t Generation = 0;
            bool bFaulted = false;
        };

        struct BindingStorage
        {
            Container::VariableArray<BindingSlot> Slots;
            Container::VariableArray<uint32_t> FreeSlots;
        };

        struct CompiledBindingCandidate
        {
            Impl* Runtime = nullptr;
            uint32_t SlotIndex = ScriptBindingHandle::InvalidSlotIndex;
            uint32_t PreviousGeneration = 0;
            uint32_t FreshGeneration = 0;
            asIScriptObject* Object = nullptr;
            asITypeInfo* Type = nullptr;
            asIScriptFunction* BeginPlay = nullptr;
            asIScriptFunction* Tick = nullptr;
            asIScriptFunction* EndPlay = nullptr;
            Container::AnsiString ModuleName;
            Container::String NormalizedComponentPath;
            Scripting::ScriptSourceApproval TrackerApproval;
            bool bTransferred = false;
        };

        static void* operator new(size_t size)
        {
            void* memory = std::malloc(size);
            if (memory == nullptr)
            {
                throw std::bad_alloc();
            }
            return memory;
        }

        static void operator delete(void* memory) noexcept
        {
            std::free(memory);
        }

        static void operator delete(void* memory, size_t) noexcept
        {
            std::free(memory);
        }

        void SetResult(EScriptRuntimeResult result)
        {
            m_EngineOwner.SetLastResult(result);
        }

        bool IsOwnerThread() const
        {
            return !m_bOwnerThreadCaptured || m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
        }

        void CaptureOwnerThread()
        {
            m_OwnerThreadId = Thread::Thread::GetCurrentThreadId();
            m_bOwnerThreadCaptured = true;
        }

        void ClearLifecycleState()
        {
            m_bInitialized = false;
            m_bCleanupPending = false;
            m_bOwnerThreadCaptured = false;
            m_OwnerThreadId = {};
            m_World = nullptr;
        }

        EScriptRuntimeResult Cleanup()
        {
            if (m_BindingStorage != nullptr)
            {
                uint32_t liveCount = 0;
                for (const BindingSlot& slot : m_BindingStorage->Slots)
                {
                    if (slot.Component != nullptr)
                    {
                        ++liveCount;
                    }
                }

                m_BindingStorage->FreeSlots.reserve(m_BindingStorage->FreeSlots.size() + liveCount);
                bool bPhysicalCleanupFailed = false;
                for (uint32_t index = 0; index < m_BindingStorage->Slots.size(); ++index)
                {
                    if (m_BindingStorage->Slots[index].Component != nullptr)
                    {
                        const uint32_t generation = m_BindingStorage->Slots[index].Generation;
                        bPhysicalCleanupFailed = !ReleaseSlot(index, true, true) || bPhysicalCleanupFailed;
                        m_SourceTracker.UnregisterBinding(index, generation);
                    }
                }
                m_BindingStorage.reset();
                m_SourceTracker.Reset();

                if (m_bCleanupPending && !m_EngineOwner.Shutdown())
                {
                    SetResult(EScriptRuntimeResult::ExecutionFailed);
                    return EScriptRuntimeResult::ExecutionFailed;
                }

                ClearLifecycleState();
                if (bPhysicalCleanupFailed)
                {
                    SetResult(EScriptRuntimeResult::ExecutionFailed);
                    return EScriptRuntimeResult::ExecutionFailed;
                }
                SetResult(EScriptRuntimeResult::Success);
                return EScriptRuntimeResult::Success;
            }

            if (m_bCleanupPending && !m_EngineOwner.Shutdown())
            {
                SetResult(EScriptRuntimeResult::ExecutionFailed);
                return EScriptRuntimeResult::ExecutionFailed;
            }

            ClearLifecycleState();
            SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
        }

        void CleanupForDestruction()
        {
            if (m_bInitialized || m_bCleanupPending)
            {
                try
                {
                    Cleanup();
                }
                catch (...)
                {
                }
            }
        }

        bool RegisterTypes()
        {
            asIScriptEngine* engine = m_EngineOwner.GetEngine();
            if (engine == nullptr)
            {
                return false;
            }

            const int vectorType = engine->RegisterObjectType(
                "Vector3", sizeof(ScriptVector3),
                asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<ScriptVector3>());
            const int vectorX = vectorType >= 0
                ? engine->RegisterObjectProperty("Vector3", "float x", asOFFSET(ScriptVector3, x))
                : vectorType;
            const int vectorY = vectorX >= 0
                ? engine->RegisterObjectProperty("Vector3", "float y", asOFFSET(ScriptVector3, y))
                : vectorX;
            const int vectorZ = vectorY >= 0
                ? engine->RegisterObjectProperty("Vector3", "float z", asOFFSET(ScriptVector3, z))
                : vectorY;
            const int entityType = vectorZ >= 0
                ? engine->RegisterObjectType("EntityRef", sizeof(EntityRef),
                                             asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<EntityRef>())
                : vectorZ;
            const int isValid = entityType >= 0
                ? engine->RegisterObjectMethod("EntityRef", "bool IsValid() const", asFUNCTION(EntityRefIsValid), asCALL_CDECL_OBJLAST)
                : entityType;
            const int getPosition = isValid >= 0
                ? engine->RegisterObjectMethod("EntityRef", "Vector3 GetPosition() const", asFUNCTION(EntityRefGetPosition), asCALL_CDECL_OBJLAST)
                : isValid;
            const int setPosition = getPosition >= 0
                ? engine->RegisterObjectMethod("EntityRef", "bool SetPosition(const Vector3 &in)",
                                               asFUNCTION(EntityRefSetPosition), asCALL_CDECL_OBJLAST)
                : getPosition;
            if (setPosition < 0)
            {
                NORVES_LOG_ERROR("Scripting", "AngelScript type registration failed: Vector3=%d x=%d y=%d z=%d EntityRef=%d IsValid=%d GetPosition=%d SetPosition=%d",
                                 vectorType, vectorX, vectorY, vectorZ, entityType, isValid, getPosition, setPosition);
                return false;
            }
            return true;
        }

        bool ResolveEntityRef(const EntityRef& reference, Entity*& outOwner)
        {
            outOwner = nullptr;
            if (reference.Runtime != this || m_BindingStorage == nullptr ||
                reference.SlotIndex >= m_BindingStorage->Slots.size())
            {
                return false;
            }

            BindingSlot& slot = m_BindingStorage->Slots[reference.SlotIndex];
            Component::ScriptComponent* component = slot.Component;
            if (slot.Generation != reference.Generation || component == nullptr ||
                !component->HasFlag(OF_Initialized) || !component->bBegunPlay || m_World == nullptr)
            {
                return false;
            }
            if (!component->m_BindingHandle.IsValid() ||
                component->m_BindingHandle.SlotIndex != reference.SlotIndex ||
                component->m_BindingHandle.Generation != reference.Generation)
            {
                return false;
            }

            Entity* owner = component->GetOwner();
            if (owner == nullptr || owner->IsPendingDestroy() || owner->GetWorld() != m_World)
            {
                return false;
            }

            for (IUnknown* inner : owner->GetInners())
            {
                if (inner == component)
                {
                    outOwner = owner;
                    return true;
                }
            }
            return false;
        }

        static bool EntityRefIsValid(const EntityRef* reference)
        {
            if (reference == nullptr || reference->Runtime == nullptr)
            {
                return false;
            }

            Entity* owner = nullptr;
            return static_cast<Impl*>(reference->Runtime)->ResolveEntityRef(*reference, owner);
        }

        static ScriptVector3 EntityRefGetPosition(const EntityRef* reference)
        {
            Entity* owner = nullptr;
            if (reference != nullptr && reference->Runtime != nullptr &&
                static_cast<Impl*>(reference->Runtime)->ResolveEntityRef(*reference, owner))
            {
                return MakeScriptVector3(owner->GetPosition());
            }

            if (asIScriptContext* context = asGetActiveContext())
            {
                context->SetException("EntityRef is no longer valid");
            }
            return ScriptVector3{};
        }

        static bool EntityRefSetPosition(const ScriptVector3& position, EntityRef* reference)
        {
            Entity* owner = nullptr;
            if (reference != nullptr && reference->Runtime != nullptr &&
                static_cast<Impl*>(reference->Runtime)->ResolveEntityRef(*reference, owner))
            {
                owner->SetPosition(MakeMathVector3(position));
                return true;
            }

            if (asIScriptContext* context = asGetActiveContext())
            {
                context->SetException("EntityRef is no longer valid");
            }
            return false;
        }

        bool IsValidSlot(const ScriptBindingHandle& handle, BindingSlot*& outSlot)
        {
            outSlot = nullptr;
            if (!handle.IsValid() || m_BindingStorage == nullptr ||
                handle.SlotIndex >= m_BindingStorage->Slots.size())
            {
                return false;
            }

            BindingSlot& slot = m_BindingStorage->Slots[handle.SlotIndex];
            if (slot.Component == nullptr || slot.Object == nullptr || slot.Generation != handle.Generation)
            {
                return false;
            }

            outSlot = &slot;
            return true;
        }

        class ContextReleaseGuard final
        {
        public:
            explicit ContextReleaseGuard(asIScriptContext* context)
                : m_Context(context)
            {
            }

            ~ContextReleaseGuard()
            {
                if (m_Context != nullptr)
                {
                    m_Context->Release();
                }
            }

        private:
            asIScriptContext* m_Context = nullptr;
        };

        EScriptRuntimeResult Invoke(BindingSlot& slot, uint32_t slotIndex, asIScriptFunction* function, float deltaSeconds, bool bTick)
        {
            if (function == nullptr)
            {
                return EScriptRuntimeResult::Success;
            }

            asIScriptEngine* engine = m_EngineOwner.GetEngine();
            asIScriptContext* context = engine != nullptr ? engine->CreateContext() : nullptr;
            if (context == nullptr)
            {
                slot.bFaulted = true;
                return EScriptRuntimeResult::ExecutionFailed;
            }
            ContextReleaseGuard contextReleaseGuard(context);

            try
            {
                EntityRef ownerReference;
                ownerReference.Runtime = this;
                ownerReference.SlotIndex = slotIndex;
                ownerReference.Generation = slot.Generation;
                const bool bPrepared = context->Prepare(function) >= 0;
                const bool bBoundObject = bPrepared && context->SetObject(slot.Object) >= 0;
                const bool bBoundOwner = bBoundObject && context->SetArgObject(0, &ownerReference) >= 0;
                const bool bBoundDelta = !bTick || (bBoundOwner && context->SetArgFloat(1, deltaSeconds) >= 0);
                const int executionResult = bBoundDelta ? context->Execute() : asEXECUTION_ERROR;

                if (executionResult != asEXECUTION_FINISHED)
                {
                    slot.bFaulted = true;
                    return EScriptRuntimeResult::ExecutionFailed;
                }
                return EScriptRuntimeResult::Success;
            }
            catch (...)
            {
                slot.bFaulted = true;
                return EScriptRuntimeResult::ExecutionFailed;
            }
        }

        void ReturnAvailableSlot(uint32_t slotIndex)
        {
            if (m_BindingStorage != nullptr)
            {
                m_BindingStorage->FreeSlots.push_back(slotIndex);
            }
        }

        bool ReleaseSlot(uint32_t slotIndex, bool bInvokeEndPlay, bool bRecycleSlot = true)
        {
            BindingSlot& slot = m_BindingStorage->Slots[slotIndex];
            if (slot.Component == nullptr)
            {
                return true;
            }

            if (bInvokeEndPlay && slot.Object != nullptr && slot.EndPlay != nullptr &&
                !slot.bFaulted && slot.Component->bBegunPlay)
            {
                Invoke(slot, slotIndex, slot.EndPlay, 0.0f, false);
            }

            Component::ScriptComponent* component = slot.Component;
            asIScriptObject* object = slot.Object;
            Container::AnsiString moduleName = std::move(slot.ModuleName);
            slot.Object = nullptr;
            slot.Type = nullptr;
            slot.BeginPlay = nullptr;
            slot.Tick = nullptr;
            slot.EndPlay = nullptr;

            if (component->m_BindingHandle.SlotIndex == slotIndex &&
                component->m_BindingHandle.Generation == slot.Generation)
            {
                component->m_BindingHandle.Reset();
            }
            slot.Component = nullptr;
            slot.bFaulted = false;
            if (bRecycleSlot)
            {
                ReturnAvailableSlot(slotIndex);
            }
            const uint32_t activeBindingCount = m_EngineOwner.GetDiagnostics().ActiveBindingCount;
            m_EngineOwner.SetActiveBindingCount(activeBindingCount > 0 ? activeBindingCount - 1 : 0);

            bool bPhysicalCleanupSucceeded = true;
            if (object != nullptr)
            {
                try
                {
                    object->Release();
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime binding object release failed");
                    bPhysicalCleanupSucceeded = false;
                }
            }
            if (!moduleName.empty())
            {
                try
                {
                    asIScriptEngine* engine = m_EngineOwner.GetEngine();
                    if (engine == nullptr || engine->DiscardModule(moduleName.c_str()) < 0)
                    {
                        NORVES_LOG_ERROR("Scripting", "ScriptRuntime binding module discard failed");
                        bPhysicalCleanupSucceeded = false;
                    }
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime binding module discard failed");
                    bPhysicalCleanupSucceeded = false;
                }
            }
            return bPhysicalCleanupSucceeded;
        }

        EScriptRuntimeResult InvokeReloadLifecycle(
            BindingSlot& slot,
            uint32_t slotIndex,
            asIScriptFunction* function) noexcept
        {
            if (function == nullptr)
            {
                return EScriptRuntimeResult::Success;
            }

            asIScriptContext* context = nullptr;
            try
            {
                asIScriptEngine* engine = m_EngineOwner.GetEngine();
                context = engine != nullptr ? engine->CreateContext() : nullptr;
                if (context == nullptr)
                {
                    return EScriptRuntimeResult::ExecutionFailed;
                }

                EntityRef ownerReference;
                ownerReference.Runtime = this;
                ownerReference.SlotIndex = slotIndex;
                ownerReference.Generation = slot.Generation;
                const bool bPrepared = context->Prepare(function) >= 0;
                const bool bBoundObject = bPrepared && context->SetObject(slot.Object) >= 0;
                const bool bBoundOwner = bBoundObject && context->SetArgObject(0, &ownerReference) >= 0;
                const int executionResult = bBoundOwner ? context->Execute() : asEXECUTION_ERROR;
                asIScriptContext* contextToRelease = context;
                context = nullptr;
                contextToRelease->Release();
                return executionResult == asEXECUTION_FINISHED
                    ? EScriptRuntimeResult::Success
                    : EScriptRuntimeResult::ExecutionFailed;
            }
            catch (...)
            {
                if (context != nullptr)
                {
                    asIScriptContext* contextToRelease = context;
                    context = nullptr;
                    try
                    {
                        contextToRelease->Release();
                    }
                    catch (...)
                    {
                    }
                }
                return EScriptRuntimeResult::ExecutionFailed;
            }
        }

        bool ReleaseReloadObject(BindingSlot& slot) noexcept
        {
            if (slot.Object == nullptr)
            {
                return true;
            }

            try
            {
                slot.Object->Release();
                slot.Object = nullptr;
                return true;
            }
            catch (...)
            {
                NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload object release failed");
                slot.Object = nullptr;
                return false;
            }
        }

        bool DiscardReloadModule(BindingSlot& slot) noexcept
        {
            if (slot.ModuleName.empty())
            {
                return true;
            }

            bool bDiscarded = true;
            try
            {
                asIScriptEngine* engine = m_EngineOwner.GetEngine();
                if (engine == nullptr || engine->DiscardModule(slot.ModuleName.c_str()) < 0)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload module discard failed");
                    bDiscarded = false;
                }
            }
            catch (...)
            {
                NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload module discard failed");
                bDiscarded = false;
            }

            slot.Type = nullptr;
            slot.BeginPlay = nullptr;
            slot.Tick = nullptr;
            slot.EndPlay = nullptr;
            slot.ModuleName.clear();
            return bDiscarded;
        }

        bool ReleaseCompiledCandidate(CompiledBindingCandidate& candidate) noexcept
        {
            if (candidate.bTransferred)
            {
                return true;
            }

            asIScriptObject* object = candidate.Object;
            Container::AnsiString moduleName = std::move(candidate.ModuleName);
            candidate.Object = nullptr;
            candidate.Type = nullptr;
            candidate.BeginPlay = nullptr;
            candidate.Tick = nullptr;
            candidate.EndPlay = nullptr;

            bool bCleanupSucceeded = true;
            if (object != nullptr)
            {
                try
                {
                    object->Release();
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload candidate object release failed");
                    bCleanupSucceeded = false;
                }
            }

            if (!moduleName.empty())
            {
                try
                {
                    asIScriptEngine* engine = m_EngineOwner.GetEngine();
                    if (engine == nullptr || engine->DiscardModule(moduleName.c_str()) < 0)
                    {
                        NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload candidate module discard failed");
                        bCleanupSucceeded = false;
                    }
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload candidate module discard failed");
                    bCleanupSucceeded = false;
                }
            }
            return bCleanupSucceeded;
        }

        class ReloadCandidateTransaction final
        {
        public:
            ReloadCandidateTransaction(
                Impl& runtime,
                Container::VariableArray<Container::TUniquePtr<CompiledBindingCandidate>>& candidates)
                : m_Runtime(runtime)
                , m_Candidates(candidates)
            {
            }

            ~ReloadCandidateTransaction()
            {
                if (!m_bCommitted)
                {
                    Rollback();
                }
            }

            bool Rollback() noexcept
            {
                if (m_bRollbackCompleted)
                {
                    return m_bRollbackSucceeded;
                }

                m_bRollbackCompleted = true;
                for (const Container::TUniquePtr<CompiledBindingCandidate>& candidate : m_Candidates)
                {
                    if (candidate != nullptr && !m_Runtime.ReleaseCompiledCandidate(*candidate))
                    {
                        NORVES_LOG_ERROR("Scripting", "ScriptRuntime reload candidate cleanup failed");
                        m_bRollbackSucceeded = false;
                    }
                }
                return m_bRollbackSucceeded;
            }

            void Commit()
            {
                m_bCommitted = true;
            }

        private:
            Impl& m_Runtime;
            Container::VariableArray<Container::TUniquePtr<CompiledBindingCandidate>>& m_Candidates;
            bool m_bCommitted = false;
            bool m_bRollbackCompleted = false;
            bool m_bRollbackSucceeded = true;
        };

        EScriptRuntimeResult StageReloadCandidate(
            const Scripting::ScriptSourceChange& change,
            const Scripting::ScriptSourceSnapshot& source,
            uint32_t freshGeneration,
            CompiledBindingCandidate& outCandidate)
        {
            if (m_BindingStorage == nullptr || change.SlotIndex >= m_BindingStorage->Slots.size())
            {
                return EScriptRuntimeResult::BindFailed;
            }

            BindingSlot& liveSlot = m_BindingStorage->Slots[change.SlotIndex];
            if (liveSlot.Component == nullptr || liveSlot.Object == nullptr || liveSlot.Generation != change.Generation)
            {
                return EScriptRuntimeResult::BindFailed;
            }

            asIScriptEngine* engine = m_EngineOwner.GetEngine();
            if (engine == nullptr)
            {
                return EScriptRuntimeResult::ExecutionFailed;
            }

            outCandidate.Runtime = this;
            outCandidate.SlotIndex = change.SlotIndex;
            outCandidate.PreviousGeneration = liveSlot.Generation;
            outCandidate.FreshGeneration = freshGeneration;
            const Container::AnsiString moduleName = MakeModuleName(change.SlotIndex, freshGeneration);
            asIScriptModule* module = engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE);
            if (module == nullptr)
            {
                return EScriptRuntimeResult::ExecutionFailed;
            }
            try
            {
                outCandidate.ModuleName = moduleName;
            }
            catch (...)
            {
                try
                {
                    engine->DiscardModule(moduleName.c_str());
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime candidate module arm cleanup failed");
                }
                throw;
            }

            const int sectionResult = module->AddScriptSection(
                source.LogicalPath.c_str(), source.Bytes.c_str(), source.Bytes.size());
            if (sectionResult < 0)
            {
                return sectionResult == asOUT_OF_MEMORY
                    ? EScriptRuntimeResult::ExecutionFailed
                    : EScriptRuntimeResult::CompileFailed;
            }

            const int buildResult = module->Build();
            if (buildResult < 0)
            {
                return buildResult == asOUT_OF_MEMORY
                    ? EScriptRuntimeResult::ExecutionFailed
                    : EScriptRuntimeResult::CompileFailed;
            }

            outCandidate.Type = module->GetTypeInfoByName(change.ScriptClassName.c_str());
            outCandidate.Tick = outCandidate.Type != nullptr
                ? outCandidate.Type->GetMethodByDecl("void Tick(EntityRef, float)")
                : nullptr;
            outCandidate.BeginPlay = outCandidate.Type != nullptr
                ? outCandidate.Type->GetMethodByDecl("void BeginPlay(EntityRef)")
                : nullptr;
            outCandidate.EndPlay = outCandidate.Type != nullptr
                ? outCandidate.Type->GetMethodByDecl("void EndPlay(EntityRef)")
                : nullptr;
            if (outCandidate.Type == nullptr || outCandidate.Tick == nullptr ||
                !HasPublicDefaultConstructor(outCandidate.Type))
            {
                return EScriptRuntimeResult::BindFailed;
            }

            outCandidate.Object = static_cast<asIScriptObject*>(engine->CreateScriptObject(outCandidate.Type));
            if (outCandidate.Object == nullptr)
            {
                return EScriptRuntimeResult::ExecutionFailed;
            }

            outCandidate.NormalizedComponentPath = source.LogicalPath;
            outCandidate.TrackerApproval.SlotIndex = change.SlotIndex;
            outCandidate.TrackerApproval.PreviousGeneration = liveSlot.Generation;
            outCandidate.TrackerApproval.Generation = freshGeneration;
            outCandidate.TrackerApproval.ApprovedPropertyPath = source.LogicalPath;
            outCandidate.TrackerApproval.ApprovedClassName = change.ScriptClassName;
            outCandidate.TrackerApproval.ApprovedLogicalPath = source.LogicalPath;
            outCandidate.TrackerApproval.ApprovedContentHash = source.ContentHash;
            return EScriptRuntimeResult::Success;
        }

        class BindingCandidateTransaction final
        {
        public:
            BindingCandidateTransaction(
                Impl& runtime,
                Component::ScriptComponent& component,
                ScriptBindingHandle& outputHandle)
                : m_Runtime(runtime)
                , m_Component(component)
                , m_OutputHandle(outputHandle)
                , m_OutputHandleBefore(outputHandle)
                , m_ComponentHandleBefore(component.m_BindingHandle)
            {
            }

            ~BindingCandidateTransaction()
            {
                if (!m_bCommitted)
                {
                    Rollback();
                }
            }

            void ReserveSlot(uint32_t slotIndex)
            {
                m_SlotIndex = slotIndex;
                m_bSlotReserved = true;
            }

            void SetModuleName(const Container::AnsiString& moduleName)
            {
                m_ModuleName = moduleName;
            }

            void SetObject(asIScriptObject* object)
            {
                m_Object = object;
            }

            void MarkSlotInitialized()
            {
                m_bSlotInitialized = true;
                m_Object = nullptr;
            }

            void Commit()
            {
                m_bCommitted = true;
            }

        private:
            void Rollback() noexcept
            {
                try
                {
                    if (m_bSlotInitialized)
                    {
                        m_Runtime.ReleaseSlot(m_SlotIndex, false);
                    }
                    else
                    {
                        asIScriptObject* object = m_Object;
                        Container::AnsiString moduleName = std::move(m_ModuleName);
                        m_Object = nullptr;
                        if (object != nullptr)
                        {
                            try
                            {
                                object->Release();
                            }
                            catch (...)
                            {
                                NORVES_LOG_ERROR("Scripting", "ScriptRuntime initial candidate object rollback failed");
                            }
                        }
                        if (!moduleName.empty())
                        {
                            try
                            {
                                if (asIScriptEngine* engine = m_Runtime.m_EngineOwner.GetEngine())
                                {
                                    engine->DiscardModule(moduleName.c_str());
                                }
                            }
                            catch (...)
                            {
                                NORVES_LOG_ERROR("Scripting", "ScriptRuntime initial candidate module rollback failed");
                            }
                        }
                        if (m_bSlotReserved)
                        {
                            m_Runtime.ReturnAvailableSlot(m_SlotIndex);
                        }
                    }

                    m_Component.m_BindingHandle = m_ComponentHandleBefore;
                    m_OutputHandle = m_OutputHandleBefore;
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime binding candidate rollback failed");
                }
            }

            Impl& m_Runtime;
            Component::ScriptComponent& m_Component;
            ScriptBindingHandle& m_OutputHandle;
            ScriptBindingHandle m_OutputHandleBefore;
            ScriptBindingHandle m_ComponentHandleBefore;
            Container::AnsiString m_ModuleName;
            asIScriptObject* m_Object = nullptr;
            uint32_t m_SlotIndex = ScriptBindingHandle::InvalidSlotIndex;
            bool m_bSlotReserved = false;
            bool m_bSlotInitialized = false;
            bool m_bCommitted = false;
        };

        Scripting::AngelScriptEngineOwner m_EngineOwner;
        World* m_World = nullptr;
        Container::TUniquePtr<BindingStorage> m_BindingStorage;
        Scripting::ScriptSourceTracker m_SourceTracker;
        Scripting::ScriptSourcePollBatch m_PollBatch;
        Container::VariableArray<Scripting::ScriptSourceBindingView> m_BindingViews;
        uint32_t m_LastIssuedBindingGeneration = 0;
        Thread::Thread::ThreadId m_OwnerThreadId;
        bool m_bOwnerThreadCaptured = false;
        bool m_bInitialized = false;
        bool m_bCleanupPending = false;
    };

    bool ScriptBindingHandle::IsValid() const
    {
        return SlotIndex != InvalidSlotIndex;
    }

    void ScriptBindingHandle::Reset()
    {
        SlotIndex = InvalidSlotIndex;
        Generation = 0;
    }

    ScriptRuntime::ScriptRuntime()
        : m_Impl(MakeUnique<Impl>())
    {
    }

    ScriptRuntime::~ScriptRuntime()
    {
        m_Impl->CleanupForDestruction();
    }

    EScriptRuntimeResult ScriptRuntime::Initialize(World& world)
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (m_Impl->m_bInitialized || m_Impl->m_bCleanupPending)
            {
                return EScriptRuntimeResult::AlreadyInitialized;
            }
            m_Impl->CaptureOwnerThread();
            m_Impl->m_bCleanupPending = true;

            if (!m_Impl->m_EngineOwner.Initialize())
            {
                const EScriptRuntimeResult ownerResult = m_Impl->m_EngineOwner.GetDiagnostics().LastResult;
                if (!m_Impl->m_EngineOwner.OwnsGlobalAllocator())
                {
                    m_Impl->ClearLifecycleState();
                }
                if (ownerResult == EScriptRuntimeResult::ExecutionFailed)
                {
                    return EScriptRuntimeResult::ExecutionFailed;
                }
                m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                return EScriptRuntimeResult::LoadFailed;
            }

            if (!m_Impl->RegisterTypes())
            {
                if (m_Impl->m_EngineOwner.Shutdown())
                {
                    m_Impl->ClearLifecycleState();
                    m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                    return EScriptRuntimeResult::LoadFailed;
                }
                return EScriptRuntimeResult::ExecutionFailed;
            }

            m_Impl->m_SourceTracker.Reset();
            m_Impl->m_PollBatch.Reset();
            m_Impl->m_BindingViews.clear();
            m_Impl->m_World = &world;
            m_Impl->m_bInitialized = true;
            m_Impl->SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
        }
        catch (...)
        {
            if (m_Impl->m_EngineOwner.OwnsGlobalAllocator())
            {
                try
                {
                    m_Impl->Cleanup();
                }
                catch (...)
                {
                }
            }
            else
            {
                m_Impl->ClearLifecycleState();
            }
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::Shutdown()
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized && !m_Impl->m_bCleanupPending)
            {
                return EScriptRuntimeResult::NotInitialized;
            }
            return m_Impl->Cleanup();
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::BeginFrameMaintenance(float deltaSeconds)
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized)
            {
                return EScriptRuntimeResult::NotInitialized;
            }

            if (m_Impl->m_BindingStorage == nullptr)
            {
                m_Impl->SetResult(EScriptRuntimeResult::Success);
                return EScriptRuntimeResult::Success;
            }

            m_Impl->m_BindingViews.clear();
            uint32_t liveCount = 0;
            for (const Impl::BindingSlot& slot : m_Impl->m_BindingStorage->Slots)
            {
                if (slot.Component != nullptr && slot.Object != nullptr)
                {
                    ++liveCount;
                }
            }
            if (liveCount == 0)
            {
                m_Impl->SetResult(EScriptRuntimeResult::Success);
                return EScriptRuntimeResult::Success;
            }

            m_Impl->m_BindingViews.reserve(liveCount);
            for (uint32_t slotIndex = 0; slotIndex < m_Impl->m_BindingStorage->Slots.size(); ++slotIndex)
            {
                const Impl::BindingSlot& slot = m_Impl->m_BindingStorage->Slots[slotIndex];
                if (slot.Component == nullptr || slot.Object == nullptr)
                {
                    continue;
                }

                Scripting::ScriptSourceBindingView view;
                view.SlotIndex = slotIndex;
                view.Generation = slot.Generation;
                view.ScriptPath = Container::StringView(slot.Component->ScriptPath.Get());
                view.ScriptClassName = Container::StringView(slot.Component->ScriptClassName.Get());
                m_Impl->m_BindingViews.push_back(view);
            }

            const Scripting::EScriptSourcePollResult pollResult = m_Impl->m_SourceTracker.Poll(
                deltaSeconds,
                Container::Span<const Scripting::ScriptSourceBindingView>(
                    m_Impl->m_BindingViews.data(), m_Impl->m_BindingViews.size()),
                m_Impl->m_PollBatch);
            if (pollResult == Scripting::EScriptSourcePollResult::NotDue ||
                pollResult == Scripting::EScriptSourcePollResult::NoChanges)
            {
                m_Impl->SetResult(EScriptRuntimeResult::Success);
                return EScriptRuntimeResult::Success;
            }

            const uint32_t dirtyCount = static_cast<uint32_t>(m_Impl->m_PollBatch.Changes.size());
            for (const Scripting::ScriptSourceChange& change : m_Impl->m_PollBatch.Changes)
            {
                if (change.SourceIndex >= m_Impl->m_PollBatch.Sources.size() ||
                    m_Impl->m_PollBatch.Sources[change.SourceIndex].Result !=
                        Scripting::EScriptSourceReadResult::Success)
                {
                    m_Impl->m_SourceTracker.RejectBatch(m_Impl->m_PollBatch.Fingerprint);
                    m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                    return EScriptRuntimeResult::LoadFailed;
                }
            }

            if (dirtyCount > ~uint32_t{0} - m_Impl->m_LastIssuedBindingGeneration ||
                m_Impl->m_EngineOwner.GetDiagnostics().ReloadGeneration == ~uint64_t{0})
            {
                m_Impl->m_SourceTracker.RejectBatch(m_Impl->m_PollBatch.Fingerprint);
                m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                return EScriptRuntimeResult::BindFailed;
            }

            Container::VariableArray<Container::TUniquePtr<Impl::CompiledBindingCandidate>> candidates;
            Container::VariableArray<Scripting::ScriptSourceApproval> approvals;
            Container::VariableArray<EScriptRuntimeResult> lifecycleResults;
            candidates.reserve(dirtyCount);
            approvals.reserve(dirtyCount);
            lifecycleResults.reserve(dirtyCount);
            Impl::ReloadCandidateTransaction transaction(*m_Impl, candidates);

            for (uint32_t candidateIndex = 0; candidateIndex < dirtyCount; ++candidateIndex)
            {
                const Scripting::ScriptSourceChange& change = m_Impl->m_PollBatch.Changes[candidateIndex];
                const Scripting::ScriptSourceSnapshot& source = m_Impl->m_PollBatch.Sources[change.SourceIndex];
                Container::TUniquePtr<Impl::CompiledBindingCandidate> candidate =
                    Container::MakeUnique<Impl::CompiledBindingCandidate>();
                candidates.push_back(std::move(candidate));
                const EScriptRuntimeResult stageResult = m_Impl->StageReloadCandidate(
                    change,
                    source,
                    m_Impl->m_LastIssuedBindingGeneration + candidateIndex + 1,
                    *candidates.back());
                if (stageResult != EScriptRuntimeResult::Success)
                {
                    if (!transaction.Rollback())
                    {
                        m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
                        return EScriptRuntimeResult::ExecutionFailed;
                    }
                    if (stageResult == EScriptRuntimeResult::LoadFailed ||
                        stageResult == EScriptRuntimeResult::CompileFailed ||
                        stageResult == EScriptRuntimeResult::BindFailed)
                    {
                        m_Impl->m_SourceTracker.RejectBatch(m_Impl->m_PollBatch.Fingerprint);
                    }
                    m_Impl->SetResult(stageResult);
                    return stageResult;
                }

                approvals.push_back(candidates.back()->TrackerApproval);
            }

            if (!m_Impl->m_SourceTracker.CanApproveBatch(
                    Container::Span<const Scripting::ScriptSourceApproval>(approvals.data(), approvals.size())))
            {
                m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
                return EScriptRuntimeResult::ExecutionFailed;
            }

            bool bLifecycleFailed = false;
            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                Impl::BindingSlot& slot = m_Impl->m_BindingStorage->Slots[candidate->SlotIndex];
                const EScriptRuntimeResult lifecycleResult =
                    slot.EndPlay != nullptr && !slot.bFaulted && slot.Component->bBegunPlay
                    ? m_Impl->InvokeReloadLifecycle(slot, candidate->SlotIndex, slot.EndPlay)
                    : EScriptRuntimeResult::Success;
                lifecycleResults.push_back(lifecycleResult);
                bLifecycleFailed = bLifecycleFailed || lifecycleResult != EScriptRuntimeResult::Success;
            }

            bool bRetireFailed = false;
            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                bRetireFailed = !m_Impl->ReleaseReloadObject(
                    m_Impl->m_BindingStorage->Slots[candidate->SlotIndex]) || bRetireFailed;
            }
            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                bRetireFailed = !m_Impl->DiscardReloadModule(
                    m_Impl->m_BindingStorage->Slots[candidate->SlotIndex]) || bRetireFailed;
            }

            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                Impl::BindingSlot& slot = m_Impl->m_BindingStorage->Slots[candidate->SlotIndex];
                slot.Object = candidate->Object;
                slot.Type = candidate->Type;
                slot.BeginPlay = candidate->BeginPlay;
                slot.Tick = candidate->Tick;
                slot.EndPlay = candidate->EndPlay;
                slot.ModuleName = std::move(candidate->ModuleName);
                slot.Generation = candidate->FreshGeneration;
                slot.bFaulted = false;
                slot.Component->m_BindingHandle.SlotIndex = candidate->SlotIndex;
                slot.Component->m_BindingHandle.Generation = candidate->FreshGeneration;
                candidate->Object = nullptr;
                candidate->Type = nullptr;
                candidate->BeginPlay = nullptr;
                candidate->Tick = nullptr;
                candidate->EndPlay = nullptr;
                candidate->ModuleName.clear();
                candidate->bTransferred = true;
            }

            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                m_Impl->m_BindingStorage->Slots[candidate->SlotIndex].Component->ScriptPath =
                    std::move(candidate->NormalizedComponentPath);
            }
            m_Impl->m_LastIssuedBindingGeneration += dirtyCount;
            m_Impl->m_SourceTracker.ApproveBatch(
                Container::Span<Scripting::ScriptSourceApproval>(approvals.data(), approvals.size()));
            m_Impl->m_EngineOwner.SetReloadGeneration(
                m_Impl->m_EngineOwner.GetDiagnostics().ReloadGeneration + 1);

            for (const Container::TUniquePtr<Impl::CompiledBindingCandidate>& candidate : candidates)
            {
                Impl::BindingSlot& slot = m_Impl->m_BindingStorage->Slots[candidate->SlotIndex];
                const EScriptRuntimeResult lifecycleResult =
                    m_Impl->InvokeReloadLifecycle(slot, candidate->SlotIndex, slot.BeginPlay);
                if (lifecycleResult != EScriptRuntimeResult::Success)
                {
                    slot.bFaulted = true;
                    bLifecycleFailed = true;
                }
            }

            transaction.Commit();
            const EScriptRuntimeResult result = bLifecycleFailed || bRetireFailed
                ? EScriptRuntimeResult::ExecutionFailed
                : EScriptRuntimeResult::Success;
            m_Impl->SetResult(result);
            return result;
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::EndFrameMaintenance()
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized)
            {
                return EScriptRuntimeResult::NotInitialized;
            }
            NORVES_STAT_SCOPE_CATEGORY("ScriptRuntime::GarbageCollect", "Scripting");
            m_Impl->m_EngineOwner.GetEngine()->GarbageCollect(asGC_ONE_STEP);
            m_Impl->m_EngineOwner.IncrementGcStepCount();
            m_Impl->SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::BindComponent(
        Component::ScriptComponent& component,
        ScriptBindingHandle& outHandle)
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized)
            {
                return EScriptRuntimeResult::NotInitialized;
            }
            if (outHandle.IsValid() || component.m_BindingHandle.IsValid())
            {
                return EScriptRuntimeResult::InvalidArgument;
            }
            if (!component.HasFlag(OF_Initialized) || !component.bBegunPlay || component.GetOwner() == nullptr ||
                component.GetOwner()->GetWorld() != m_Impl->m_World || component.GetOwner()->IsPendingDestroy())
            {
                return EScriptRuntimeResult::InvalidArgument;
            }

            Scripting::ScriptSourceSnapshot sourceSnapshot;
            if (m_Impl->m_SourceTracker.ReadSource(
                    Container::StringView(component.ScriptPath.Get()), sourceSnapshot) !=
                Scripting::EScriptSourceReadResult::Success)
            {
                m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                return EScriptRuntimeResult::LoadFailed;
            }

            if (m_Impl->m_LastIssuedBindingGeneration == ~uint32_t{0})
            {
                m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                return EScriptRuntimeResult::BindFailed;
            }
            const uint32_t nextGeneration = m_Impl->m_LastIssuedBindingGeneration + 1;
            if (m_Impl->m_BindingStorage == nullptr)
            {
                m_Impl->m_BindingStorage = Container::MakeUnique<Impl::BindingStorage>();
            }

            Impl::BindingStorage& bindingStorage = *m_Impl->m_BindingStorage;
            uint32_t slotIndex = 0;
            if (!bindingStorage.FreeSlots.empty())
            {
                slotIndex = bindingStorage.FreeSlots.back();
            }
            else
            {
                if (bindingStorage.Slots.size() == ~uint32_t{0})
                {
                    m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                    return EScriptRuntimeResult::BindFailed;
                }
                slotIndex = static_cast<uint32_t>(bindingStorage.Slots.size());
                bindingStorage.Slots.reserve(bindingStorage.Slots.size() + 1);
            }

            bindingStorage.FreeSlots.reserve(bindingStorage.FreeSlots.size() + 1);
            m_Impl->m_SourceTracker.ReserveBindingCapacity(
                m_Impl->m_EngineOwner.GetDiagnostics().ActiveBindingCount + 1);
            if (!bindingStorage.FreeSlots.empty())
            {
                bindingStorage.FreeSlots.pop_back();
            }
            else
            {
                bindingStorage.Slots.push_back(Impl::BindingSlot());
            }

            Impl::BindingCandidateTransaction candidate(*m_Impl, component, outHandle);
            candidate.ReserveSlot(slotIndex);
            Impl::BindingSlot& slot = bindingStorage.Slots[slotIndex];
            const Container::AnsiString moduleName = MakeModuleName(slotIndex, nextGeneration);
            asIScriptEngine* engine = m_Impl->m_EngineOwner.GetEngine();
            asIScriptModule* module = engine != nullptr
                ? engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE)
                : nullptr;
            if (module == nullptr)
            {
                m_Impl->SetResult(EScriptRuntimeResult::CompileFailed);
                return EScriptRuntimeResult::CompileFailed;
            }
            try
            {
                candidate.SetModuleName(moduleName);
            }
            catch (...)
            {
                try
                {
                    engine->DiscardModule(moduleName.c_str());
                }
                catch (...)
                {
                    NORVES_LOG_ERROR("Scripting", "ScriptRuntime initial candidate module arm cleanup failed");
                }
                throw;
            }
            if (module->AddScriptSection(
                    sourceSnapshot.LogicalPath.c_str(), sourceSnapshot.Bytes.c_str(), sourceSnapshot.Bytes.size()) < 0 ||
                module->Build() < 0)
            {
                m_Impl->SetResult(EScriptRuntimeResult::CompileFailed);
                return EScriptRuntimeResult::CompileFailed;
            }

            asITypeInfo* type = module->GetTypeInfoByName(component.ScriptClassName.Get().c_str());
            asIScriptFunction* tick = type != nullptr ? type->GetMethodByDecl("void Tick(EntityRef, float)") : nullptr;
            asIScriptFunction* beginPlay = type != nullptr ? type->GetMethodByDecl("void BeginPlay(EntityRef)") : nullptr;
            asIScriptFunction* endPlay = type != nullptr ? type->GetMethodByDecl("void EndPlay(EntityRef)") : nullptr;
            asIScriptObject* object = HasPublicDefaultConstructor(type)
                ? static_cast<asIScriptObject*>(engine->CreateScriptObject(type))
                : nullptr;
            candidate.SetObject(object);
            if (type == nullptr || tick == nullptr || object == nullptr)
            {
                m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                return EScriptRuntimeResult::BindFailed;
            }

            const Math::Vector3 ownerPosition = component.GetOwner()->GetPosition();
            slot.ModuleName = moduleName;
            slot.Component = &component;
            slot.Object = object;
            slot.Type = type;
            slot.BeginPlay = beginPlay;
            slot.Tick = tick;
            slot.EndPlay = endPlay;
            slot.Generation = nextGeneration;
            slot.bFaulted = false;
            candidate.MarkSlotInitialized();
            component.m_BindingHandle.SlotIndex = slotIndex;
            component.m_BindingHandle.Generation = nextGeneration;
            m_Impl->m_EngineOwner.SetActiveBindingCount(m_Impl->m_EngineOwner.GetDiagnostics().ActiveBindingCount + 1);

            const EScriptRuntimeResult beginResult = m_Impl->Invoke(slot, slotIndex, slot.BeginPlay, 0.0f, false);
            if (beginResult != EScriptRuntimeResult::Success)
            {
                component.GetOwner()->SetPosition(ownerPosition);
                m_Impl->SetResult(beginResult);
                return beginResult;
            }

            Scripting::ScriptSourceApproval approval;
            approval.SlotIndex = slotIndex;
            approval.PreviousGeneration = nextGeneration;
            approval.Generation = nextGeneration;
            approval.ApprovedPropertyPath = sourceSnapshot.LogicalPath;
            approval.ApprovedClassName = component.ScriptClassName.Get();
            approval.ApprovedLogicalPath = sourceSnapshot.LogicalPath;
            approval.ApprovedContentHash = sourceSnapshot.ContentHash;
            m_Impl->m_SourceTracker.RegisterBinding(std::move(approval));
            component.ScriptPath = std::move(sourceSnapshot.LogicalPath);
            outHandle = component.m_BindingHandle;
            candidate.Commit();
            m_Impl->m_LastIssuedBindingGeneration = nextGeneration;
            m_Impl->SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::UnbindComponent(ScriptBindingHandle& handle)
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized)
            {
                handle.Reset();
                return EScriptRuntimeResult::NotInitialized;
            }
            Impl::BindingSlot* slot = nullptr;
            if (!m_Impl->IsValidSlot(handle, slot))
            {
                handle.Reset();
                m_Impl->SetResult(EScriptRuntimeResult::Success);
                return EScriptRuntimeResult::Success;
            }

            const uint32_t slotIndex = handle.SlotIndex;
            const uint32_t generation = slot->Generation;
            m_Impl->m_BindingStorage->FreeSlots.reserve(m_Impl->m_BindingStorage->FreeSlots.size() + 1);
            const bool bPhysicalCleanupSucceeded = m_Impl->ReleaseSlot(slotIndex, true);
            m_Impl->m_SourceTracker.UnregisterBinding(slotIndex, generation);
            handle.Reset();
            const EScriptRuntimeResult result = bPhysicalCleanupSucceeded
                ? EScriptRuntimeResult::Success
                : EScriptRuntimeResult::ExecutionFailed;
            m_Impl->SetResult(result);
            return result;
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    EScriptRuntimeResult ScriptRuntime::TickComponent(const ScriptBindingHandle& handle, float deltaSeconds)
    {
        if (m_Impl->m_bOwnerThreadCaptured && !m_Impl->IsOwnerThread())
        {
            m_Impl->SetResult(EScriptRuntimeResult::WrongThread);
            return EScriptRuntimeResult::WrongThread;
        }
        try
        {
            if (!m_Impl->m_bInitialized)
            {
                return EScriptRuntimeResult::NotInitialized;
            }
            Impl::BindingSlot* slot = nullptr;
            if (!m_Impl->IsValidSlot(handle, slot) || slot->bFaulted)
            {
                return EScriptRuntimeResult::InvalidHandle;
            }

            const EScriptRuntimeResult result = m_Impl->Invoke(*slot, handle.SlotIndex, slot->Tick, deltaSeconds, true);
            m_Impl->SetResult(result);
            return result;
        }
        catch (...)
        {
            m_Impl->SetResult(EScriptRuntimeResult::ExecutionFailed);
            return EScriptRuntimeResult::ExecutionFailed;
        }
    }

    bool ScriptRuntime::IsInitialized() const
    {
        return m_Impl->m_bInitialized;
    }

    const ScriptRuntimeDiagnostics& ScriptRuntime::GetDiagnostics() const
    {
        return m_Impl->m_EngineOwner.GetDiagnostics();
    }
} // namespace NorvesLib::Core
