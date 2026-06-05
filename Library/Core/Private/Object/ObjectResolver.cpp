#include "Object/ObjectResolver.h"

namespace NorvesLib::Core
{
    ObjectResolveResult ObjectResolver::Resolve(
        const StableObjectRef &ref,
        const ObjectResolveContext &context,
        const IClass *expectedClass)
    {
        if (!context.Heap)
        {
            return {};
        }

        if (ref.HasSceneId())
        {
            auto it = context.SceneObjects.find(ref.SceneId);
            if (it != context.SceneObjects.end())
            {
                return ResolveHandle(it->second, context, expectedClass);
            }
        }

        if (ref.HasStableId())
        {
            auto it = context.StableObjects.find(ref.Id);
            if (it != context.StableObjects.end())
            {
                return ResolveHandle(it->second, context, expectedClass);
            }
        }

        if (ref.HasPath() && context.bAllowPathFallback)
        {
            auto it = context.PathObjects.find(Identity(ref.Path));
            if (it == context.PathObjects.end() || it->second.empty())
            {
                return {};
            }

            ObjectResolveResult resolved;
            for (ObjectHandle handle : it->second)
            {
                ObjectResolveResult candidate = ResolveHandle(handle, context, expectedClass);
                if (!candidate.IsResolved())
                {
                    if (candidate.Status == ObjectResolveStatus::ClassMismatch)
                    {
                        return candidate;
                    }
                    continue;
                }

                if (resolved.IsResolved())
                {
                    return ObjectResolveResult{.Status = ObjectResolveStatus::Ambiguous};
                }
                resolved = candidate;
            }

            return resolved.IsResolved() ? resolved : ObjectResolveResult{};
        }

        return {};
    }

    ObjectResolveResult ObjectResolver::ResolveHandle(
        ObjectHandle handle,
        const ObjectResolveContext &context,
        const IClass *expectedClass)
    {
        Object *object = context.Heap ? context.Heap->Resolve(handle) : nullptr;
        if (!object)
        {
            return {};
        }

        if (expectedClass && !object->GetClass()->IsChildOf(expectedClass))
        {
            return ObjectResolveResult{
                .Status = ObjectResolveStatus::ClassMismatch,
                .Handle = handle,
                .Instance = object};
        }

        return ObjectResolveResult{
            .Status = ObjectResolveStatus::Resolved,
            .Handle = handle,
            .Instance = object};
    }

} // namespace NorvesLib::Core
