#pragma once

#include "Object/IClass.h"

namespace NorvesLib::Core
{
    class Object;

    int ApplyInitialValues(IUnknown *object, const FieldInitializer *initializer);
    int ApplyDefaultValues(IUnknown *object);
    int CopyEditableProperties(Object &dst, const Object &src);

} // namespace NorvesLib::Core
