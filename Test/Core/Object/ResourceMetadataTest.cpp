#include "Object/Invocation.h"
#include "Object/Resource.h"
#include "Object/ResourceRef.h"
#include "Object/ResourceRegistry.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    const FunctionDesc *FindFunction(const Container::VariableArray<FunctionDesc> &functions, const char *name)
    {
        const Identity id(name);
        for (const FunctionDesc &function : functions)
        {
            if (Identity(function.Name) == id)
            {
                return &function;
            }
        }
        return nullptr;
    }
}

int main()
{
    std::cout << "ResourceMetadataTest start\n";

    ResourceRegistry registry;
    assert(registry.Initialize());

    const Container::String path = "Assets/Data/Metadata.resource";
    auto resource = registry.Load<Resource>(path);
    assert(resource != nullptr);
    assert(resource->IsLoaded());
    assert(resource->GetId() == resource->GetResourceId());
    assert(resource->GetType() == Identity("Resource"));
    assert(resource->GetMetadata().URI == path);
    assert(resource->GetLoadState() == ResourceState::Loaded);

    ResourceHandle<Resource> handle = registry.FindHandle<Resource>(path);
    assert(handle.IsValid());

    auto records = registry.GetRecords();
    assert(records.size() == 1);
    assert(records[0].Id == resource->GetResourceId());
    assert(records[0].Generation == handle.Generation);
    assert(records[0].URI == path);
    assert(records[0].Type == Identity("Resource"));
    assert(records[0].LoadState == ResourceState::Loaded);

    ResourceRef<Resource> strongRef(registry, handle);
    assert(strongRef.IsValid());
    resource.reset();
    assert(registry.CollectGarbage() == 0);

    Container::VariableArray<FunctionDesc> functions = Resource::BuildResourceFunctionDescs();
    assert(FindFunction(functions, "Reload") != nullptr);
    assert(FindFunction(functions, "Reimport") != nullptr);
    assert(FindFunction(functions, "Unload") != nullptr);
    assert(FindFunction(functions, "Pin") != nullptr);

    InvocationQueue queue;
    for (FunctionDesc &function : functions)
    {
        queue.RegisterFunction(std::move(function));
    }

    InvocationContext context;
    context.Resources = &registry;
    context.CurrentThread = ThreadPolicy::GameThreadOnly;

    const StableFunctionId unloadId = MakeStableSchemaId("NorvesLib", "Function", Container::StringView("Resource"), Container::StringView("Unload"));
    InvokeRequest unloadRequest;
    unloadRequest.Target = ResourceInvokeTarget{.Resource = handle};
    unloadRequest.Function = unloadId;
    unloadRequest.RequestId = 100;
    assert(queue.Enqueue(std::move(unloadRequest)));

    InvokeResult result;
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::Succeeded);
    assert(result.ReturnValue.Get<bool>() != nullptr);
    assert(*result.ReturnValue.Get<bool>());
    assert(strongRef.Get()->GetLoadState() == ResourceState::Unloaded);

    const StableFunctionId reloadId = MakeStableSchemaId("NorvesLib", "Function", Container::StringView("Resource"), Container::StringView("Reload"));
    InvokeRequest reloadRequest;
    reloadRequest.Target = ResourceInvokeTarget{.Resource = handle};
    reloadRequest.Function = reloadId;
    reloadRequest.RequestId = 101;
    assert(queue.Enqueue(std::move(reloadRequest)));
    assert(queue.TryExecuteNext(context, result));
    assert(result.Status == InvokeStatus::Succeeded);
    assert(result.ReturnValue.Get<bool>() != nullptr);
    assert(*result.ReturnValue.Get<bool>());
    assert(strongRef.Get()->GetLoadState() == ResourceState::Loaded);

    strongRef.Reset();
    assert(registry.CollectGarbage() == 1);
    registry.Shutdown();

    std::cout << "ResourceMetadataTest passed\n";
    return 0;
}
