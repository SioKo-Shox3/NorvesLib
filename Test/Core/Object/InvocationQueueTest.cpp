#include "Animal.h"
#include "Object/Invocation.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

namespace
{
    FunctionDesc MakeAddFunction(StableFunctionId id)
    {
        FunctionDesc desc;
        desc.StableId = id;
        desc.Name = "Add";
        desc.ReturnType = TypeRegistry::Get().GetTypeId<int>();
        desc.Flags = FunctionFlags::RuntimeCallable | FunctionFlags::GameThreadOnly;
        desc.Thread = ThreadPolicy::GameThreadOnly;
        desc.Params.push_back(ParamDesc{.Name = "A", .Type = TypeRegistry::Get().GetTypeId<int>()});
        desc.Params.push_back(ParamDesc{.Name = "B", .Type = TypeRegistry::Get().GetTypeId<int>()});
        desc.Invoke = [](NorvesLib::Core::IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)instance;
            if (arguments.size() != 2 || !outReturnValue)
            {
                return false;
            }

            const int *a = arguments[0].Get<int>();
            const int *b = arguments[1].Get<int>();
            if (!a || !b)
            {
                return false;
            }

            outReturnValue->Set<int>(*a + *b);
            return true;
        };
        return desc;
    }

    FunctionDesc MakeDogAgeFunction(StableFunctionId id)
    {
        FunctionDesc desc;
        desc.StableId = id;
        desc.Name = "GetDogAge";
        desc.ReturnType = TypeRegistry::Get().GetTypeId<int>();
        desc.Flags = FunctionFlags::RuntimeCallable | FunctionFlags::ReadOnly | FunctionFlags::GameThreadOnly;
        desc.Thread = ThreadPolicy::GameThreadOnly;
        desc.Invoke = [](NorvesLib::Core::IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)arguments;
            Dog *dog = CastTo<Dog>(instance);
            if (!dog || !outReturnValue)
            {
                return false;
            }

            const int *age = static_cast<const int *>(dog->GetPropertyValue(Identity("Age")));
            if (!age)
            {
                return false;
            }

            outReturnValue->Set<int>(*age);
            return true;
        };
        return desc;
    }
}

int main()
{
    std::cout << "InvocationQueueTest start\n";

    const StableFunctionId addId = MakeStableSchemaId("NorvesLib.Test", "Function", Container::StringView("Global"), Container::StringView("Add"));
    const StableFunctionId dogAgeId = MakeStableSchemaId("NorvesLib.Test", "Function", Container::StringView("Dog"), Container::StringView("GetDogAge"));
    const StableFunctionId renderOnlyId = MakeStableSchemaId("NorvesLib.Test", "Function", Container::StringView("Global"), Container::StringView("RenderOnly"));
    const StableFunctionId authorityId = MakeStableSchemaId("NorvesLib.Test", "Function", Container::StringView("Global"), Container::StringView("Authority"));
    const StableFunctionId unsafeId = MakeStableSchemaId("NorvesLib.Test", "Function", Container::StringView("Global"), Container::StringView("Unsafe"));

    ObjectHeap heap;
    ObjectHandle dogHandle = heap.Create<Dog>();
    Dog *dog = heap.Resolve<Dog>(dogHandle);
    assert(dog != nullptr);

    InvocationQueue queue;
    queue.RegisterFunction(MakeAddFunction(addId));
    queue.RegisterFunction(MakeDogAgeFunction(dogAgeId));

    FunctionDesc renderOnly = MakeAddFunction(renderOnlyId);
    renderOnly.Thread = ThreadPolicy::RenderThreadOnly;
    queue.RegisterFunction(renderOnly);

    FunctionDesc authority = MakeAddFunction(authorityId);
    authority.Flags = authority.Flags | FunctionFlags::RequiresAuthority;
    queue.RegisterFunction(authority);

    FunctionDesc unsafe = MakeAddFunction(unsafeId);
    unsafe.Flags = unsafe.Flags | FunctionFlags::Unsafe;
    queue.RegisterFunction(unsafe);

    InvokeRequest addRequest;
    addRequest.Target = GlobalInvokeTarget{};
    addRequest.Function = addId;
    addRequest.RequestId = 10;
    addRequest.Arguments.push_back(PropertyValue::Create<int>(2));
    addRequest.Arguments.push_back(PropertyValue::Create<int>(3));
    assert(queue.Enqueue(std::move(addRequest)));

    InvocationContext context;
    context.ObjectHeap = &heap;
    context.CurrentThread = ThreadPolicy::GameThreadOnly;

    InvokeResult result;
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::Succeeded);
    assert(result.RequestId == 10);
    assert(result.ReturnValue.Get<int>() != nullptr);
    assert(*result.ReturnValue.Get<int>() == 5);

    InvokeRequest objectRequest;
    objectRequest.Target = ObjectInvokeTarget{.Object = dogHandle};
    objectRequest.Function = dogAgeId;
    objectRequest.RequestId = 11;
    assert(queue.Enqueue(std::move(objectRequest)));

    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::Succeeded);
    assert(result.ReturnValue.Get<int>() != nullptr);
    assert(*result.ReturnValue.Get<int>() == 3);

    InvokeRequest renderThreadViolation;
    renderThreadViolation.Target = GlobalInvokeTarget{};
    renderThreadViolation.Function = renderOnlyId;
    renderThreadViolation.RequestId = 12;
    renderThreadViolation.Arguments.push_back(PropertyValue::Create<int>(1));
    renderThreadViolation.Arguments.push_back(PropertyValue::Create<int>(1));
    assert(queue.Enqueue(std::move(renderThreadViolation)));
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::ThreadPolicyViolation);

    context.bHasAuthority = false;
    InvokeRequest authorityRequest;
    authorityRequest.Target = GlobalInvokeTarget{};
    authorityRequest.Function = authorityId;
    authorityRequest.RequestId = 13;
    authorityRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    authorityRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    assert(queue.Enqueue(std::move(authorityRequest)));
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::RequiresAuthority);
    context.bHasAuthority = true;

    InvokeRequest unsafeRequest;
    unsafeRequest.Target = GlobalInvokeTarget{};
    unsafeRequest.Function = unsafeId;
    unsafeRequest.RequestId = 14;
    unsafeRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    unsafeRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    assert(queue.Enqueue(std::move(unsafeRequest)));
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::UnsafeBlocked);

    unsafeRequest.Flags = InvokeFlags::AllowUnsafe;
    unsafeRequest.RequestId = 15;
    unsafeRequest.Arguments.clear();
    unsafeRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    unsafeRequest.Arguments.push_back(PropertyValue::Create<int>(1));
    assert(queue.Enqueue(std::move(unsafeRequest)));
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::Succeeded);

    assert(heap.EnqueueDestroy(dogHandle));
    assert(heap.ProcessDestroyQueue() == 1);

    std::cout << "InvocationQueueTest passed\n";
    return 0;
}
