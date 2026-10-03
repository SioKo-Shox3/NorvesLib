#include "Resource/GltfDocumentProfile.h"
#include "Text/JsonDocument.h"

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        bool IsRequiredExtensionsName(const Container::String& name)
        {
            constexpr char expected[] = "extensionsRequired";
            if (name.size() != sizeof(expected) - 1)
            {
                return false;
            }
            for (size_t index = 0; index < name.size(); ++index)
            {
                if (static_cast<uint32_t>(name[index]) != static_cast<uint8_t>(expected[index]))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    RequiredExtensionsStatus CheckRequiredExtensions(const JsonValue& root)
    {
        if (!root.IsObject())
        {
            return RequiredExtensionsStatus::InvalidRoot;
        }
        bool bFound = false;
        for (size_t index = 0; index < root.GetObjectSize(); ++index)
        {
            if (IsRequiredExtensionsName(root.GetMemberName(index)))
            {
                if (bFound)
                {
                    return RequiredExtensionsStatus::DuplicateDeclaration;
                }
                bFound = true;
            }
        }
        if (!bFound)
        {
            return RequiredExtensionsStatus::Success;
        }
        const auto required = root.FindMember("extensionsRequired");
        if (!required.IsArray())
        {
            return RequiredExtensionsStatus::InvalidDeclaration;
        }
        for (size_t index = 0; index < required.GetArraySize(); ++index)
        {
            if (!required.GetArrayElement(index).IsString())
            {
                return RequiredExtensionsStatus::InvalidDeclaration;
            }
        }
        return required.GetArraySize() == 0 ? RequiredExtensionsStatus::Success : RequiredExtensionsStatus::Unsupported;
    }
} // namespace NorvesLib::Core::Gltf
