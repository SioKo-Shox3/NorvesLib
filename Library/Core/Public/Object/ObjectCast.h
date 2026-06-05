#pragma once

#include "Object/IClass.h"
#include "Object/IUnknown.h"
#include <type_traits>

namespace NorvesLib::Core
{
    template <typename T>
    T *CastTo(IUnknown *object)
    {
        static_assert(std::is_base_of_v<IUnknown, T>, "T must derive from IUnknown");
        const IClass *objectClass = object ? object->GetClass() : nullptr;
        return objectClass && objectClass->IsChildOf(T::StaticClass()) ? static_cast<T *>(object) : nullptr;
    }

    template <typename T>
    const T *CastTo(const IUnknown *object)
    {
        static_assert(std::is_base_of_v<IUnknown, T>, "T must derive from IUnknown");
        const IClass *objectClass = object ? object->GetClass() : nullptr;
        return objectClass && objectClass->IsChildOf(T::StaticClass()) ? static_cast<const T *>(object) : nullptr;
    }

    template <typename T>
    T *TryCast(IUnknown *object)
    {
        return CastTo<T>(object);
    }

    template <typename T>
    const T *TryCast(const IUnknown *object)
    {
        return CastTo<T>(object);
    }

    template <typename T>
    bool IsA(const IUnknown *object)
    {
        return CastTo<T>(object) != nullptr;
    }

} // namespace NorvesLib::Core
