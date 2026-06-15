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
        if (!object)
        {
            return nullptr;
        }
        const IClass *objectClass = object->GetClass();
        if (!objectClass)
        {
            return nullptr;
        }
        constexpr EClassCastFlags flag = ClassCastFlagTraits<T>::Value;
        if constexpr (flag != EClassCastFlags::None)
        {
            // 第1層：単一ビットAND（フラグ持ちのホット型）
            return HasAnyFlags(objectClass->GetCastFlags(), flag) ? static_cast<T *>(object) : nullptr;
        }
        else
        {
            // 第2層：祖先テーブル（O(1)）へフォールバック
            return objectClass->IsChildOf(T::StaticClass()) ? static_cast<T *>(object) : nullptr;
        }
    }

    template <typename T>
    const T *CastTo(const IUnknown *object)
    {
        static_assert(std::is_base_of_v<IUnknown, T>, "T must derive from IUnknown");
        if (!object)
        {
            return nullptr;
        }
        const IClass *objectClass = object->GetClass();
        if (!objectClass)
        {
            return nullptr;
        }
        constexpr EClassCastFlags flag = ClassCastFlagTraits<T>::Value;
        if constexpr (flag != EClassCastFlags::None)
        {
            // 第1層：単一ビットAND（フラグ持ちのホット型）
            return HasAnyFlags(objectClass->GetCastFlags(), flag) ? static_cast<const T *>(object) : nullptr;
        }
        else
        {
            // 第2層：祖先テーブル（O(1)）へフォールバック
            return objectClass->IsChildOf(T::StaticClass()) ? static_cast<const T *>(object) : nullptr;
        }
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
