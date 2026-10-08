#pragma once
// Mesh固有のIBMを含まない、検証済みのcurrent Skeleton。
#include "Animation/SocketTypes.h"
#include "Asset/CookedClipBankV1.h"
namespace NorvesLib::Core
{
    class SkeletonResource;
}
namespace NorvesLib::Core::Skeletal
{
    struct SkeletonV1Data
    {
        RigImportProfile Profile = RigImportProfile::DirectTrs128;
        RigTopology Topology;
        RigClipSnapshot CurrentRest;
        Container::FixedArray<float, 16> RootTransform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        Container::VariableArray<Animation::SocketDefinition> Sockets;
        uint64_t PayloadHash = 0, ContentHash = 0, RootHash = 0;
    };
    class SkeletonV1
    {
      public:
        [[nodiscard]] const SkeletonV1Data* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        friend class NorvesLib::Core::SkeletonResource;
        Container::TSharedPtr<const SkeletonV1Data> m_Data;
        friend bool BuildSkeletonV1(const RigAuthoringCpu&, SkeletonV1&, RigV1Report&, const RigV1Limits&,
                                    RigImportProfile);
        friend bool ParseSkeletonV1(Container::Span<const uint8_t>, SkeletonV1&, RigV1Report&, const RigV1Limits&,
                                    RigImportProfile);
    };
    [[nodiscard]] bool BuildSkeletonV1(const RigAuthoringCpu&, SkeletonV1& out, RigV1Report&, const RigV1Limits& = {},
                                       RigImportProfile = RigImportProfile::DirectTrs128);
    // ParentJointはこのSkeletonの正準添字。新しいContentHashを持つ所有値を返す。
    [[nodiscard]] bool WithSkeletonSockets(const SkeletonV1&, Container::Span<const Animation::SocketDefinition>,
                                           SkeletonV1&, RigV1Report&, const RigV1Limits& = {},
                                           RigImportProfile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool WriteSkeletonV1(const SkeletonV1&, Container::VariableArray<uint8_t>& out, RigV1Report&,
                                       const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool ParseSkeletonV1(Container::Span<const uint8_t>, SkeletonV1& out, RigV1Report&,
                                       const RigV1Limits& = {}, RigImportProfile = RigImportProfile::DirectTrs128);
} // namespace NorvesLib::Core::Skeletal
