#pragma once

#include "Object/ObjectHandle.h"
#include "Object/ObjectHeap.h"
#include "Object/IClass.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    enum class ObjectResolveStatus : uint8_t
    {
        Resolved,
        Failed,
        Ambiguous,
        ClassMismatch,
        NamespaceMismatch
    };

    struct ObjectResolveResult
    {
        ObjectResolveStatus Status = ObjectResolveStatus::Failed;
        ObjectHandle Handle;
        Object *Instance = nullptr;

        bool IsResolved() const
        {
            return Status == ObjectResolveStatus::Resolved && Instance != nullptr;
        }
    };

    struct ObjectResolveContext
    {
        ObjectHeap *Heap = nullptr;
        bool bAllowPathFallback = false;
        Container::UnorderedMap<SceneObjectId, ObjectHandle> SceneObjects;
        Container::UnorderedMap<StableObjectId, ObjectHandle> StableObjects;
        Container::UnorderedMap<Identity, Container::VariableArray<ObjectHandle>, Identity::Hasher> PathObjects;

        void RegisterSceneObject(SceneObjectId id, ObjectHandle handle)
        {
            if (id != 0 && handle)
            {
                SceneObjects[id] = handle;
            }
        }

        void RegisterStableObject(StableObjectId id, ObjectHandle handle)
        {
            if (id != 0 && handle)
            {
                StableObjects[id] = handle;
            }
        }

        void RegisterPathObject(const ObjectPath &path, ObjectHandle handle)
        {
            if (!path.empty() && handle)
            {
                PathObjects[Identity(path)].push_back(handle);
            }
        }
    };

    class ObjectResolver
    {
    public:
        static ObjectResolveResult Resolve(
            const StableObjectRef &ref,
            const ObjectResolveContext &context,
            const IClass *expectedClass = nullptr);

    private:
        static ObjectResolveResult ResolveHandle(
            ObjectHandle handle,
            const ObjectResolveContext &context,
            const IClass *expectedClass);
    };

} // namespace NorvesLib::Core
