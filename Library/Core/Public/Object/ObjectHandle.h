#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core
{
    using ObjectId = uint64_t;
    using StableObjectId = uint64_t;
    using SceneObjectId = uint64_t;
    using ObjectPath = Container::String;

    struct ObjectHandle
    {
        ObjectId Id = 0;
        uint32_t Generation = 0;

        bool IsValid() const
        {
            return Id != 0 && Generation != 0;
        }

        explicit operator bool() const
        {
            return IsValid();
        }

        bool operator==(const ObjectHandle &other) const
        {
            return Id == other.Id && Generation == other.Generation;
        }

        bool operator!=(const ObjectHandle &other) const
        {
            return !(*this == other);
        }
    };

    struct StableObjectRef
    {
        StableObjectId Id = 0;
        ObjectPath Path;
        SceneObjectId SceneId = 0;

        bool HasStableId() const { return Id != 0; }
        bool HasSceneId() const { return SceneId != 0; }
        bool HasPath() const { return !Path.empty(); }
    };

} // namespace NorvesLib::Core
