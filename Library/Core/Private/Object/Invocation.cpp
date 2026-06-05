#include "Object/Invocation.h"

namespace NorvesLib::Core
{
    namespace
    {
        bool HasFlag(FunctionFlags value, FunctionFlags flag)
        {
            return (value & flag) != FunctionFlags::None;
        }

        bool HasFlag(InvokeFlags value, InvokeFlags flag)
        {
            return (value & flag) != InvokeFlags::None;
        }
    }

    void InvocationQueue::RegisterFunction(FunctionDesc desc)
    {
        if (desc.StableId != InvalidSchemaId)
        {
            m_Functions[desc.StableId] = std::move(desc);
        }
    }

    bool InvocationQueue::Enqueue(InvokeRequest request)
    {
        if (request.Function == InvalidSchemaId)
        {
            return false;
        }

        m_Requests.push_back(std::move(request));
        return true;
    }

    bool InvocationQueue::TryExecuteNext(const InvocationContext &context, InvokeResult &outResult)
    {
        if (m_Requests.empty())
        {
            return false;
        }

        InvokeRequest request = std::move(m_Requests.front());
        m_Requests.pop_front();
        outResult = Execute(context, request);
        return true;
    }

    size_t InvocationQueue::ExecuteAll(const InvocationContext &context, Container::VariableArray<InvokeResult> *outResults)
    {
        size_t count = 0;
        InvokeResult result;
        while (TryExecuteNext(context, result))
        {
            if (outResults)
            {
                outResults->push_back(std::move(result));
            }
            ++count;
        }
        return count;
    }

    void InvocationQueue::Clear()
    {
        m_Requests.clear();
        m_Functions.clear();
    }

    const FunctionDesc *InvocationQueue::FindFunction(StableFunctionId functionId) const
    {
        auto it = m_Functions.find(functionId);
        return it != m_Functions.end() ? &it->second : nullptr;
    }

    InvokeResult InvocationQueue::Execute(const InvocationContext &context, const InvokeRequest &request) const
    {
        InvokeResult result;
        result.RequestId = request.RequestId;

        const FunctionDesc *function = FindFunction(request.Function);
        if (!function)
        {
            result.Status = InvokeStatus::FunctionNotFound;
            result.ErrorMessage = "Function not found";
            return result;
        }

        const InvokeStatus validation = Validate(context, *function, request);
        if (validation != InvokeStatus::Queued)
        {
            result.Status = validation;
            result.ErrorMessage = "Invocation validation failed";
            return result;
        }

        IUnknown *instance = ResolveTargetInstance(context, request.Target);
        if (std::holds_alternative<ObjectInvokeTarget>(request.Target) && !instance)
        {
            result.Status = InvokeStatus::InvalidTarget;
            result.ErrorMessage = "Object target is invalid";
            return result;
        }

        if (!function->Invoke)
        {
            result.Status = InvokeStatus::Failed;
            result.ErrorMessage = "Function has no invoke callback";
            return result;
        }

        if (!function->Invoke(instance, request.Arguments, &result.ReturnValue))
        {
            result.Status = InvokeStatus::Failed;
            result.ErrorMessage = "Function invoke failed";
            return result;
        }

        result.Status = InvokeStatus::Succeeded;
        return result;
    }

    InvokeStatus InvocationQueue::Validate(const InvocationContext &context, const FunctionDesc &function, const InvokeRequest &request) const
    {
        if (function.Thread == ThreadPolicy::GameThreadOnly && context.CurrentThread != ThreadPolicy::GameThreadOnly)
        {
            return InvokeStatus::ThreadPolicyViolation;
        }

        if (function.Thread == ThreadPolicy::RenderThreadOnly && context.CurrentThread != ThreadPolicy::RenderThreadOnly)
        {
            return InvokeStatus::ThreadPolicyViolation;
        }

        if (HasFlag(function.Flags, FunctionFlags::RequiresAuthority) && !context.bHasAuthority)
        {
            return InvokeStatus::RequiresAuthority;
        }

        if (HasFlag(function.Flags, FunctionFlags::Unsafe) && !HasFlag(request.Flags, InvokeFlags::AllowUnsafe))
        {
            return InvokeStatus::UnsafeBlocked;
        }

        if (std::holds_alternative<ResourceInvokeTarget>(request.Target) && !context.Resources)
        {
            return InvokeStatus::InvalidTarget;
        }

        if (std::holds_alternative<ObjectInvokeTarget>(request.Target) && !context.ObjectHeap)
        {
            return InvokeStatus::InvalidTarget;
        }

        return InvokeStatus::Queued;
    }

    IUnknown *InvocationQueue::ResolveTargetInstance(const InvocationContext &context, const InvokeTarget &target) const
    {
        if (const auto *objectTarget = std::get_if<ObjectInvokeTarget>(&target))
        {
            return context.ObjectHeap ? context.ObjectHeap->Resolve(objectTarget->Object) : nullptr;
        }

        if (const auto *resourceTarget = std::get_if<ResourceInvokeTarget>(&target))
        {
            if (!context.Resources)
            {
                return nullptr;
            }
            return context.Resources->Resolve(resourceTarget->Resource).get();
        }

        return nullptr;
    }

} // namespace NorvesLib::Core
