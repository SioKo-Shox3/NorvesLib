#pragma once

#include <cstdint>

namespace NorvesLib::Core
{
    class JsonValue;
}
namespace NorvesLib::Core::Gltf
{
    enum class RequiredExtensionsStatus : uint8_t
    {
        Success, InvalidRoot, DuplicateDeclaration, InvalidDeclaration, Unsupported
    };
    // 現在対応する必須拡張は無い。optionalなextensionsUsedはこの判定に含めない。
    [[nodiscard]] RequiredExtensionsStatus CheckRequiredExtensions(const JsonValue& root);
    [[nodiscard]] constexpr const char* RequiredExtensionsError(RequiredExtensionsStatus status) noexcept
    {
        switch (status)
        {
        case RequiredExtensionsStatus::Success:
            return "";
        case RequiredExtensionsStatus::InvalidRoot:
            return "glTF root must be an object";
        case RequiredExtensionsStatus::DuplicateDeclaration:
            return "glTF extensionsRequired must not be duplicated";
        case RequiredExtensionsStatus::InvalidDeclaration:
            return "glTF extensionsRequired must be an array of strings";
        case RequiredExtensionsStatus::Unsupported:
            return "glTF required extensions are not supported";
        }
        return "invalid glTF required extension status";
    }
} // namespace NorvesLib::Core::Gltf
