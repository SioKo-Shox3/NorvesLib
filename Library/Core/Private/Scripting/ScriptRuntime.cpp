#include "Scripting/ScriptRuntime.h"

#include "Asset/AssetPath.h"
#include "Component/ScriptComponent.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Scripting/AngelScriptEngineOwner.h"
#include "Thread/Thread.h"
#include "Debug/Stats.h"

#include <angelscript.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

        bool HasRejectedSegment(const Container::String& path)
        {
            if (path.empty())
            {
                return true;
            }

            size_t segmentStart = 0;
            for (size_t index = 0; index <= path.size(); ++index)
            {
                const bool bAtEnd = index == path.size();
                const bool bSeparator = !bAtEnd && (path[index] == '/' || path[index] == '\\');
                if (!bAtEnd && !bSeparator)
                {
                    continue;
                }

                const size_t segmentLength = index - segmentStart;
                if (segmentLength == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
                {
                    return true;
                }

                segmentStart = index + 1;
            }
            return false;
        }

        bool IsDriveLetter(TCHAR character)
        {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        bool IsRootedOrDriveRelative(const Container::String& path)
        {
            if (path.empty())
            {
                return true;
            }

            if (path[0] == '/' || path[0] == '\\')
            {
                return true;
            }

            return path.size() >= 2 && IsDriveLetter(path[0]) && path[1] == ':';
        }

        bool IsPathContainedBy(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            std::filesystem::path::const_iterator rootIt = root.begin();
            std::filesystem::path::const_iterator candidateIt = candidate.begin();
            while (rootIt != root.end())
            {
                if (candidateIt == candidate.end() || *rootIt != *candidateIt)
                {
                    return false;
                }

                ++rootIt;
                ++candidateIt;
            }
            return true;
        }

        bool ResolveScriptPath(
            const Container::String& requestedPath,
            Container::String& outLogicalPath,
            std::filesystem::path& outResolvedPath)
        {
            outLogicalPath.clear();
            outResolvedPath.clear();
            if (HasRejectedSegment(requestedPath) || IsRootedOrDriveRelative(requestedPath))
            {
                return false;
            }

            const Asset::AssetPath normalized = Asset::AssetPath::Normalize(requestedPath.c_str(), NORVES_ASSET_DIR);
            if (!normalized.IsValid() || normalized.IsAbsolute() || !normalized.HasLogicalPath())
            {
                return false;
            }

            std::error_code error;
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(
                std::filesystem::path(NORVES_ASSET_DIR),
                error);
            if (error)
            {
                return false;
            }

            const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(
                canonicalRoot / normalized.GetLogicalPath().c_str(),
                error);
            if (error || !IsPathContainedBy(canonicalRoot, canonicalCandidate))
            {
                return false;
            }

            if (!std::filesystem::is_regular_file(canonicalCandidate, error) || error)
            {
                return false;
            }

            outLogicalPath = Container::String(normalized.GetLogicalPath().c_str());
            outResolvedPath = canonicalCandidate;
            return true;
        }

        bool ReadScriptFile(const std::filesystem::path& path, Container::AnsiString& outSource)
        {
            outSource.clear();
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return false;
            }

            char buffer[4096];
            while (stream)
            {
                stream.read(buffer, sizeof(buffer));
                const std::streamsize count = stream.gcount();
                if (count > 0)
                {
                    outSource.append(buffer, static_cast<size_t>(count));
                }
            }
            return !outSource.empty() && !stream.bad();
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
                for (uint32_t index = 0; index < m_BindingStorage->Slots.size(); ++index)
                {
                    if (m_BindingStorage->Slots[index].Component != nullptr)
                    {
                        ReleaseSlot(index, true, false);
                    }
                }
                m_BindingStorage.reset();
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

        void ReleaseSlot(uint32_t slotIndex, bool bInvokeEndPlay, bool bRecycleSlot = true)
        {
            BindingSlot& slot = m_BindingStorage->Slots[slotIndex];
            if (slot.Component == nullptr)
            {
                return;
            }

            if (bInvokeEndPlay && slot.EndPlay != nullptr && !slot.bFaulted && slot.Component->bBegunPlay)
            {
                Invoke(slot, slotIndex, slot.EndPlay, 0.0f, false);
            }

            if (slot.Object != nullptr)
            {
                slot.Object->Release();
            }

            if (!slot.ModuleName.empty() && m_EngineOwner.GetEngine() != nullptr)
            {
                m_EngineOwner.GetEngine()->DiscardModule(slot.ModuleName.c_str());
            }

            if (slot.Component->m_BindingHandle.SlotIndex == slotIndex &&
                slot.Component->m_BindingHandle.Generation == slot.Generation)
            {
                slot.Component->m_BindingHandle.Reset();
            }

            slot.Component = nullptr;
            slot.Object = nullptr;
            slot.Type = nullptr;
            slot.BeginPlay = nullptr;
            slot.Tick = nullptr;
            slot.EndPlay = nullptr;
            slot.ModuleName.clear();
            slot.bFaulted = false;
            if (bRecycleSlot)
            {
                ReturnAvailableSlot(slotIndex);
            }
            const uint32_t activeBindingCount = m_EngineOwner.GetDiagnostics().ActiveBindingCount;
            m_EngineOwner.SetActiveBindingCount(activeBindingCount > 0 ? activeBindingCount - 1 : 0);
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
                        if (m_Object != nullptr)
                        {
                            m_Object->Release();
                        }
                        if (!m_ModuleName.empty())
                        {
                            if (asIScriptEngine* engine = m_Runtime.m_EngineOwner.GetEngine())
                            {
                                engine->DiscardModule(m_ModuleName.c_str());
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
                    std::abort();
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
            (void)deltaSeconds;
            if (!m_Impl->m_bInitialized)
            {
                return EScriptRuntimeResult::NotInitialized;
            }
            m_Impl->SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
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

            Container::String logicalPath;
            std::filesystem::path resolvedPath;
            if (!ResolveScriptPath(component.ScriptPath.Get(), logicalPath, resolvedPath))
            {
                m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                return EScriptRuntimeResult::LoadFailed;
            }

            Container::AnsiString source;
            if (!ReadScriptFile(resolvedPath, source))
            {
                m_Impl->SetResult(EScriptRuntimeResult::LoadFailed);
                return EScriptRuntimeResult::LoadFailed;
            }

            if (m_Impl->m_LastIssuedBindingGeneration == ~uint32_t{0})
            {
                m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                return EScriptRuntimeResult::BindFailed;
            }
            const uint32_t nextGeneration = ++m_Impl->m_LastIssuedBindingGeneration;
            if (m_Impl->m_BindingStorage == nullptr)
            {
                m_Impl->m_BindingStorage = Container::MakeUnique<Impl::BindingStorage>();
            }

            Impl::BindingStorage& bindingStorage = *m_Impl->m_BindingStorage;
            uint32_t slotIndex = 0;
            if (!bindingStorage.FreeSlots.empty())
            {
                slotIndex = bindingStorage.FreeSlots.back();
                bindingStorage.FreeSlots.pop_back();
            }
            else
            {
                if (bindingStorage.Slots.size() == ~uint32_t{0})
                {
                    m_Impl->SetResult(EScriptRuntimeResult::BindFailed);
                    return EScriptRuntimeResult::BindFailed;
                }
                slotIndex = static_cast<uint32_t>(bindingStorage.Slots.size());
                bindingStorage.Slots.push_back(Impl::BindingSlot());
            }

            Impl::BindingCandidateTransaction candidate(*m_Impl, component, outHandle);
            candidate.ReserveSlot(slotIndex);
            Impl::BindingSlot& slot = bindingStorage.Slots[slotIndex];
            const Container::AnsiString moduleName = MakeModuleName(slotIndex, nextGeneration);
            candidate.SetModuleName(moduleName);
            asIScriptEngine* engine = m_Impl->m_EngineOwner.GetEngine();
            asIScriptModule* module = engine != nullptr
                ? engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE)
                : nullptr;
            if (module == nullptr || module->AddScriptSection(logicalPath.c_str(), source.c_str(), source.size()) < 0 ||
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

            component.ScriptPath = logicalPath;
            outHandle = component.m_BindingHandle;
            candidate.Commit();
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
            m_Impl->ReleaseSlot(slotIndex, true);
            handle.Reset();
            m_Impl->SetResult(EScriptRuntimeResult::Success);
            return EScriptRuntimeResult::Success;
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
