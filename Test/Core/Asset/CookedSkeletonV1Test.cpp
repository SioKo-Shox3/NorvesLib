// role1を独立current restとして所有し、geometryやIBMを持ち越さない。
#include "Asset/CookedSkeletonV1.h"
#include "Animation/SkeletonResource.h"
#include "Asset/RigSplitWire.h"
#include "ClipBankV1Fixture.h"
#include "Object/ResourceRegistry.h"
#include "RigSplitWireTestFixture.h"
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace S = NorvesLib::Core::Skeletal;
namespace C = NorvesLib::Core::Container;
namespace
{

    void Sockets(F::Fixture& f)
    {
        namespace A = NorvesLib::Core::Animation;
        using NorvesLib::Core::Identity;
        auto source = f.Import(f.Json);
        S::SkeletonV1 base, withSockets;
        S::RigV1Report report;
        RIG_CHECK(S::BuildSkeletonV1(source, base, report));
        F::Bytes original, bytes, again;
        RIG_CHECK(S::WriteSkeletonV1(base, original, report));
        A::SocketDefinition socket;
        socket.Name = Identity("Mouth");
        socket.ParentJoint = 1;
        socket.Offset.position = {1, 2, 3};
        RIG_CHECK(S::WithSkeletonSockets(base, {&socket, 1}, withSockets, report));
        RIG_CHECK(withSockets.GetData()->ContentHash != base.GetData()->ContentHash &&
                  withSockets.GetData()->Topology.SkeletonId == base.GetData()->Topology.SkeletonId &&
                  withSockets.GetData()->CurrentRest.RestHash == base.GetData()->CurrentRest.RestHash);
        RIG_CHECK(S::WriteSkeletonV1(withSockets, bytes, report) && F::U32(bytes, 28) == 6);
        RIG_CHECK(F::U32(bytes, 256 + 5 * 32) == S::SplitWire::Four('S', 'O', 'C', 'K') &&
                  F::U32(bytes, 256 + 5 * 32 + 4) == 0 && F::U32(bytes, 256 + 5 * 32 + 24) == 64);
        RIG_CHECK(S::ParseSkeletonV1(F::View(bytes), withSockets, report) &&
                  S::WriteSkeletonV1(withSockets, again, report) && bytes == again);
        RIG_CHECK(withSockets.GetData()->Sockets.size() == 1 && withSockets.GetData()->Sockets[0].Name == socket.Name &&
                  withSockets.GetData()->Sockets[0].ParentJoint == 1 &&
                  withSockets.GetData()->Sockets[0].Offset.position.z == 3);
        NorvesLib::Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        auto resource = registry.CreateTransient<NorvesLib::Core::SkeletonResource>("SocketSkeleton");
        RIG_CHECK(resource && resource->SetSplitSkeleton(withSockets) && resource->Load());
        const auto pose = resource->GetPoseRevision(), hash = resource->GetSplitSkeleton()->ContentHash;
        RIG_CHECK(resource->FindSocket(Identity("Mouth")) && resource->FindSocket(Identity("Mouth"))->ParentJoint == 1);
        socket.Offset.position.x = 9;
        A::SocketReport socketReport;
        RIG_CHECK(resource->SetSockets({&socket, 1}, socketReport));
        RIG_CHECK(resource->GetPoseRevision() == pose && resource->GetSplitSkeleton()->ContentHash == hash &&
                  resource->GetSplitSkeleton()->Sockets[0].Offset.position.x == 1 &&
                  resource->FindSocket(Identity("Mouth"))->Offset.position.x == 9);
        S::RigV1Limits small;
        small.MaxJoints = 1;
        const auto stable = withSockets.GetData();
        RIG_CHECK(!S::WithSkeletonSockets(base, {&socket, 1}, withSockets, report, small) &&
                  withSockets.GetData() == stable);
        RIG_CHECK(S::WithSkeletonSockets(withSockets, {}, withSockets, report) &&
                  S::WriteSkeletonV1(withSockets, again, report) && again == original);
        resource->Unload();
        RIG_CHECK(resource->GetSockets().empty() && !resource->FindSocket(Identity("Mouth")));
        std::puts("SKELETON_SOCKET_WIRE result=pass");
    }
    void Codec(F::Fixture& f)
    {
        auto source = f.Import(f.Json);
        S::SkeletonV1 skeleton;
        S::RigV1Report report;
        RIG_CHECK(S::BuildSkeletonV1(source, skeleton, report));
        F::Bytes bytes, again;
        RIG_CHECK(S::WriteSkeletonV1(skeleton, bytes, report));
        RIG_CHECK(bytes.size() == 704 && F::U32(bytes, 20) == 1 && F::U32(bytes, 28) == 5);
        source = {};
        S::SkeletonV1 parsed;
        RIG_CHECK(S::ParseSkeletonV1(F::View(bytes), parsed, report));
        RIG_CHECK(S::WriteSkeletonV1(parsed, again, report) && bytes == again);
        RIG_CHECK(parsed.GetData()->Topology.CanonicalToSource[0] == 0 &&
                  parsed.GetData()->Topology.Joints[0].Name == "Child");
        RIG_CHECK(parsed.GetData()->CurrentRest.Rest[0].Translation.Y == 1 &&
                  parsed.GetData()->CurrentRest.Rest[1].Translation.Y == 0);
        RIG_CHECK(parsed.GetData()->RootHash == S::SplitWire::RootIdentityHash() &&
                  parsed.GetData()->ContentHash == F::Hash(F::View(bytes)));
        auto optional = NorvesLib::Tests::RigSplitWireFixture::Optional(bytes);
        S::SkeletonV1 withOptional;
        RIG_CHECK(S::ParseSkeletonV1(F::View(optional), withOptional, report) &&
                  withOptional.GetData()->ContentHash != parsed.GetData()->ContentHash);
        F::Put32(optional, 256 + 5 * 32 + 4, 1);
        F::Reseal(optional);
        RIG_CHECK(!S::ParseSkeletonV1(F::View(optional), withOptional, report) &&
                  report.Status == S::RigV1Status::UnsupportedSection);
        const auto* stable = parsed.GetData();
        for (size_t n = 0; n < bytes.size(); ++n)
        {
            RIG_CHECK(!S::ParseSkeletonV1({bytes.data(), n}, parsed, report) && parsed.GetData() == stable);
        }
        for (int n = 0; n < 14; ++n)
        {
            auto broken = bytes;
            const size_t set = F::SectionOffset(bytes, 2), rest = F::SectionOffset(bytes, 3),
                         root = F::SectionOffset(bytes, 4);
            switch (n)
            {
            case 0:
                F::Put32(broken, 20, 3);
                break;
            case 1:
                broken[72] = 1;
                break;
            case 2:
                F::Put32(broken, 256 + 4 * 32, S::SplitWire::Four('I', 'B', 'M', 'S'));
                break;
            case 3:
                F::Put32(broken, 256 + 4 * 32 + 4, 0);
                break;
            case 4:
                F::Put64(broken, 256 + 8, 256);
                break;
            case 5:
                F::Put32(broken, set + 4, 1);
                break;
            case 6:
                F::Put64(broken, set + 24, 1);
                break;
            case 7:
                F::Put64(broken, set + 32, 0);
                break;
            case 8:
                F::Float(broken, rest + 28, 0);
                break;
            case 9:
                F::Float(broken, rest + 24, 1e30f);
                break;
            case 10:
                F::Float(broken, root + 48, 1);
                break;
            case 11:
                broken[rest + 40] = 1;
                break;
            case 12:
                F::Put64(broken, 56, 0);
                break;
            case 13:
                F::Put32(broken, F::SectionOffset(bytes, 1) + 12, 0);
                break;
            }
            F::Reseal(broken);
            RIG_CHECK(!S::ParseSkeletonV1(F::View(broken), parsed, report) && parsed.GetData() == stable);
        }
        S::RigV1Limits low;
        low.MaxJoints = 1;
        RIG_CHECK(!S::ParseSkeletonV1(F::View(bytes), parsed, report, low) && parsed.GetData() == stable);
        namespace P = NorvesLib::Tests::RigSplitWireFixture;
        auto alias = bytes;
        const auto set = F::SectionOffset(alias, 2);
        F::Put64(alias, set + 8, 5);
        F::Put32(alias, set + 16, 4);
        F::Bytes shortStrings{'C', 'h', 'i', 'l', 'd', 'R', 'o', 'o', 't'};
        alias = P::Strings(alias, shortStrings);
        low = {};
        low.MaxStringBytes = 9;
        P::AllocationCounts counts;
        {
            P::ObserveAllocations observe(counts);
            RIG_CHECK(!S::ParseSkeletonV1(F::View(alias), parsed, report, low) &&
                      report.Status == S::RigV1Status::LimitExceeded);
        }
        RIG_CHECK(counts.Owned == 0 && counts.Topology == 0 && parsed.GetData() == stable);
        low.MaxStringBytes = 13;
        RIG_CHECK(S::ParseSkeletonV1(F::View(alias), parsed, report, low) &&
                  S::WriteSkeletonV1(parsed, again, report, low));
        low.MaxStringBytes = 12;
        counts = {};
        {
            P::ObserveAllocations observe(counts);
            RIG_CHECK(!S::ParseSkeletonV1(F::View(alias), parsed, report, low) && counts.Owned == 0);
        }
        const char* output = std::getenv("NORVES_RIG_SPLIT_V1_OUTPUT");
        if (output && *output)
        {
            std::filesystem::create_directories(output);
            F::WriteBytes(std::filesystem::path(output) / "skeleton-v1.nvskel", bytes);
        }
        std::printf("RIG_SPLIT_CASE skeleton_wire result=pass\n");
    }
} // namespace
int main()
{
    F::Fixture fixture;
    Codec(fixture);
    Sockets(fixture);
    std::printf("SKELETON_V1_WIRE result=pass current_rest_no_mesh_ibm_canonical_topology_content_pin_strict_parse\n");
    return 0;
}
