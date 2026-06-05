#pragma once

#include "Object/ObjectHeap.h"
#include "Object/ResourceRegistry.h"
#include "Object/RuntimeSchema.h"
#include "Container/Containers.h"
#include <cstdint>
#include <variant>

namespace NorvesLib::Core
{
    using SubsystemId = uint64_t;

    struct ObjectInvokeTarget
    {
        ObjectHandle Object;
    };

    struct ResourceInvokeTarget
    {
        ResourceHandle<Resource> Resource;
    };

    struct SubsystemInvokeTarget
    {
        SubsystemId Subsystem = 0;
    };

    struct GlobalInvokeTarget
    {
    };

    using InvokeTarget = std::variant<
        ObjectInvokeTarget,
        ResourceInvokeTarget,
        SubsystemInvokeTarget,
        GlobalInvokeTarget>;

    enum class InvokeFlags : uint32_t
    {
        None = 0,
        FromExternalAdapter = 1 << 0,
        AllowUnsafe = 1 << 1
    };

    enum class InvokeStatus : uint8_t
    {
        Queued,
        Succeeded,
        FunctionNotFound,
        InvalidTarget,
        ThreadPolicyViolation,
        RequiresAuthority,
        UnsafeBlocked,
        Failed
    };

    struct InvokeRequest
    {
        InvokeTarget Target = GlobalInvokeTarget{};
        StableFunctionId Function = InvalidSchemaId;
        Container::VariableArray<PropertyValue> Arguments;
        InvokeFlags Flags = InvokeFlags::None;
        uint64_t RequestId = 0;
    };

    struct InvokeResult
    {
        InvokeStatus Status = InvokeStatus::Failed;
        PropertyValue ReturnValue;
        Container::String ErrorMessage;
        uint64_t RequestId = 0;
    };

    struct InvocationContext
    {
        ObjectHeap *ObjectHeap = nullptr;
        ResourceRegistry *Resources = nullptr;
        ThreadPolicy CurrentThread = ThreadPolicy::GameThreadOnly;
        bool bHasAuthority = true;
    };

    class InvocationQueue
    {
    public:
        void RegisterFunction(FunctionDesc desc);
        bool Enqueue(InvokeRequest request);
        bool TryExecuteNext(const InvocationContext &context, InvokeResult &outResult);
        size_t ExecuteAll(const InvocationContext &context, Container::VariableArray<InvokeResult> *outResults = nullptr);
        size_t GetPendingCount() const { return m_Requests.size(); }
        void Clear();

    private:
        const FunctionDesc *FindFunction(StableFunctionId functionId) const;
        InvokeResult Execute(const InvocationContext &context, const InvokeRequest &request) const;
        InvokeStatus Validate(const InvocationContext &context, const FunctionDesc &function, const InvokeRequest &request) const;
        IUnknown *ResolveTargetInstance(const InvocationContext &context, const InvokeTarget &target) const;

        Container::Deque<InvokeRequest> m_Requests;
        Container::UnorderedMap<StableFunctionId, FunctionDesc> m_Functions;
    };

    inline constexpr InvokeFlags operator|(InvokeFlags lhs, InvokeFlags rhs)
    {
        return static_cast<InvokeFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline constexpr InvokeFlags operator&(InvokeFlags lhs, InvokeFlags rhs)
    {
        return static_cast<InvokeFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

} // namespace NorvesLib::Core
