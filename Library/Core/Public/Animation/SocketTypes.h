#pragma once
#include "Container/Span.h"
#include "Math/Transform.h"
#include "Text/IdentityPool.h"
namespace NorvesLib::Core
{
    class SkeletonResource;
    class JsonDocument;
    namespace Animation
    {
        struct SocketDefinition
        {
            Identity Name;
            uint32_t ParentJoint = UINT32_MAX;
            // オフセットは剛体。scaleは1だけを受理する。
            Math::Transform Offset;
        };
        struct AttachProfile
        {
            Identity Name;
            Math::Transform Offset;
        };
        enum class SocketError : uint8_t
        {
            None,
            InvalidJson,
            InvalidName,
            DuplicateName,
            UnknownJoint,
            InvalidTransform,
            LimitExceeded,
            FileReadFailed
        };
        struct SocketReport
        {
            SocketError Error = SocketError::None;
            Container::String Detail;
        };
        [[nodiscard]] bool ParseSocketSettingsDocument(const Container::String&, JsonDocument&, SocketReport&);
        [[nodiscard]] bool ReadSocketSettingsFile(const Container::String& path, Container::String&, SocketReport&);
        [[nodiscard]] bool ValidateSockets(Container::Span<const SocketDefinition>, size_t jointCount, SocketReport&);
        [[nodiscard]] bool ParseSockets(const Container::String&, const SkeletonResource&,
                                        Container::VariableArray<SocketDefinition>&, SocketReport&);
        [[nodiscard]] bool ParseAttachProfiles(const Container::String&, Container::VariableArray<AttachProfile>&,
                                               Math::Transform& grip, SocketReport&);
        [[nodiscard]] bool ComposeSocketAttachment(const Math::Transform& socket, const Math::Transform& profile,
                                                   const Math::Transform& grip, Math::Transform&);
        // 骨のscale/shearを除去する。反転・退化したbasisは拒否し、平行移動は保持する。
        [[nodiscard]] bool ExtractSocketRigidMatrix(const Math::Matrix4x4&, Math::Matrix4x4&);
        [[nodiscard]] bool BuildSocketWorldTransform(const Math::Matrix4x4& jointModel, const Math::Transform& offset,
                                                     const Math::Transform& ownerWorld, Math::Transform&);
    } // namespace Animation
} // namespace NorvesLib::Core
