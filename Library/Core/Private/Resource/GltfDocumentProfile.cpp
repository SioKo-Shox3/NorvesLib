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

    bool HasMorphData(const JsonValue& root)
    {
        const auto meshes = root.FindMember("meshes");
        for (size_t meshIndex = 0; meshIndex < meshes.GetArraySize(); ++meshIndex)
        {
            const auto mesh = meshes.GetArrayElement(meshIndex);
            if (mesh.HasMember("weights"))
            {
                return true;
            }
            const auto primitives = mesh.FindMember("primitives");
            for (size_t index = 0; index < primitives.GetArraySize(); ++index)
            {
                if (primitives.GetArrayElement(index).HasMember("targets"))
                {
                    return true;
                }
            }
        }
        const auto nodes = root.FindMember("nodes");
        for (size_t index = 0; index < nodes.GetArraySize(); ++index)
        {
            if (nodes.GetArrayElement(index).HasMember("weights"))
            {
                return true;
            }
        }
        const auto animations = root.FindMember("animations");
        for (size_t index = 0; index < animations.GetArraySize(); ++index)
        {
            const auto channels = animations.GetArrayElement(index).FindMember("channels");
            for (size_t channel = 0; channel < channels.GetArraySize(); ++channel)
            {
                if (channels.GetArrayElement(channel).FindMember("target").FindMember("path").AsString() == "weights")
                {
                    return true;
                }
            }
        }
        return false;
    }

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
