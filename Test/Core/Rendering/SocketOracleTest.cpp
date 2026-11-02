// ソケットの姿勢を CPU スキニングと比較し、不正な定義の拒否を検証する。
#include "Animation/AnimGraphResource.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletalPoseBounds.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SocketAttachmentComponent.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace
{
    using namespace NorvesLib::Core;
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace M = NorvesLib::Math;

#define SOCKET_CHECK(condition)                                                                                       \
    do                                                                                                                \
    {                                                                                                                 \
        if (!(condition))                                                                                             \
        {                                                                                                             \
            std::fprintf(stderr, "ソケット試験 %s:%d 失敗: %s\n", __FILE__, __LINE__, #condition);                    \
            std::abort();                                                                                             \
        }                                                                                                             \
    } while (false)

    void Near(float actual, float expected, const std::source_location& where = std::source_location::current())
    {
        if (!(std::fabs(actual - expected) <= 1e-4f))
        {
            std::fprintf(stderr, "ソケット試験 %s:%u 実際=%.9g 期待=%.9g\n", where.file_name(), where.line(),
                         double(actual), double(expected));
            std::abort();
        }
    }

    void NearPoint(const M::Vector3& actual, const M::Vector3& expected)
    {
        Near(actual.x, expected.x);
        Near(actual.y, expected.y);
        Near(actual.z, expected.z);
    }

    M::Matrix4x4 Matrix(const M::Transform& transform)
    {
        return M::MatrixUtils::CreateWorldRowVector(transform.position, transform.rotation, transform.scale);
    }

    void AddChannel(S::SkeletalAnimationClip& clip, uint32_t joint, S::SkeletalAnimationPath path,
                    const S::SkeletalValue& value)
    {
        S::SkeletalAnimationChannel channel;
        channel.JointIndex = joint;
        channel.Path = path;
        channel.Samples.push_back({0, value});
        clip.Channels.push_back(std::move(channel));
    }

    struct SocketFixture
    {
        ResourceRegistry Registry;
        C::TSharedPtr<SkeletonResource> Skeleton;
        C::TSharedPtr<SkinnedMeshResource> Mesh;
        C::TSharedPtr<AnimationClipResource> Clip;
        C::TSharedPtr<SkeletalAssetResource> Asset;
        M::Matrix4x4 MeshNode;
        const M::Vector3 Points[4] = {{0, 0, 0}, {0.5f, 0, 0}, {0, 0.5f, 0}, {0, 0, 0.5f}};

        SocketFixture(const M::Quaternion& childRotation, float jointScale)
        {
            SOCKET_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("SocketOracleSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("SocketOracleMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("SocketOracleClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("SocketOracleAsset");
            SOCKET_CHECK(Skeleton && Mesh && Clip && Asset);

            MeshNode = M::MatrixUtils::CreateWorldRowVector(
                {1.3f, -2.0f, 0.7f}, M::QuaternionUtils::Normalize({0.12f, 0.27f, -0.19f, 0.92f}),
                M::Vector3::One);
            const auto inverseMesh = M::MatrixUtils::Inverse(MeshNode);
            const auto parentBind = M::MatrixUtils::CreateWorldRowVector(
                {0.2f, -0.4f, 0.9f}, M::QuaternionUtils::Normalize({0.1f, 0.2f, 0.15f, 0.95f}),
                M::Vector3::One);
            const auto childBind = M::MatrixUtils::CreateWorldRowVector(
                                       {0.8f, 0.3f, -0.6f},
                                       M::QuaternionUtils::Normalize({-0.22f, 0.12f, 0.17f, 0.9f}),
                                       M::Vector3::One) *
                                   parentBind;

            C::VariableArray<S::SkeletalJoint> joints(2);
            joints[0].Name = "Root";
            joints[1].Name = "Child";
            joints[1].ParentIndex = 0;
            const auto parentInverseBind = MeshNode * M::MatrixUtils::Inverse(parentBind);
            const auto childInverseBind = MeshNode * M::MatrixUtils::Inverse(childBind);
            for (size_t i = 0; i < 16; ++i)
            {
                joints[0].InverseBindMatrix[i] = parentInverseBind.values[i];
                joints[1].InverseBindMatrix[i] = childInverseBind.values[i];
            }
            Skeleton->SetJoints(std::move(joints));
            SOCKET_CHECK(Skeleton->Load());

            A::SocketReport report;
            // scale の検査では原点を関節の位置に置き、拡縮されたオフセットと区別する。
            const char* socketJson = jointScale == 1.0f
                ? R"({"version":1,"sockets":[{"name":"Hand","parent":"Child","position":[0.4,-0.25,0.3],"rotation":[0.2,0.3,0.1,0.92736185],"scale":[1,1,1]}]})"
                : R"({"version":1,"sockets":[{"name":"Hand","parent":"Child","position":[0,0,0],"rotation":[0.2,0.3,0.1,0.92736185],"scale":[1,1,1]}]})";
            SOCKET_CHECK(Skeleton->ApplySocketsJson(C::String(socketJson), report));
            const auto* socket = Skeleton->FindSocket(Identity("Hand"));
            SOCKET_CHECK(socket && socket->ParentJoint == 1);

            // バインド姿勢の関節大域座標から逆メッシュ行列で頂点をメッシュ空間へ戻す。
            const auto bindPoint = Matrix(socket->Offset) * childBind * inverseMesh;
            C::VariableArray<S::SkeletalVertex> vertices(4);
            for (size_t i = 0; i < 4; ++i)
            {
                const auto position = M::MatrixUtils::TransformPointRowVector(bindPoint, Points[i]);
                vertices[i].Position = {position.x, position.y, position.z};
                vertices[i].Normal = {0, 1, 0};
                vertices[i].JointIndices[0] = 1;
                vertices[i].JointWeights[0] = 1;
            }
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(C::VariableArray<uint32_t>{0, 1, 2});
            SOCKET_CHECK(Mesh->Load());

            S::SkeletalAnimationClip clip;
            clip.Name = "SocketOracle";
            clip.DurationSeconds = 1;
            const auto parentRotation = M::QuaternionUtils::Normalize({0.14f, -0.21f, 0.08f, 0.95f});
            AddChannel(clip, 0, S::SkeletalAnimationPath::Translation, {0.7f, -0.3f, 0.6f, 0});
            AddChannel(clip, 0, S::SkeletalAnimationPath::Rotation,
                       {parentRotation.x, parentRotation.y, parentRotation.z, parentRotation.w});
            AddChannel(clip, 1, S::SkeletalAnimationPath::Translation, {0.35f, -0.7f, 1.2f, 0});
            AddChannel(clip, 1, S::SkeletalAnimationPath::Rotation,
                       {childRotation.x, childRotation.y, childRotation.z, childRotation.w});
            AddChannel(clip, 1, S::SkeletalAnimationPath::Scale, {jointScale, jointScale, jointScale, 0});
            Clip->SetClip(std::move(clip));
            SOCKET_CHECK(Clip->Load());
            Asset->SetResources(Mesh, Skeleton, Clip);
            SOCKET_CHECK(Asset->Load());
        }
    };

    void TestJsonRejection(SocketFixture& fixture)
    {
        A::SocketReport report;
        SOCKET_CHECK(fixture.Skeleton->ApplySocketsJson(
            C::String(R"({"version":1,"sockets":[{"name":"Hand","parent":"Child","position":[0.4,-0.25,0.3],"rotation":[0.2,0.3,0.1,0.92736185],"scale":[1,1,1]}]})"), report));
        const auto* original = fixture.Skeleton->FindSocket(Identity("Hand"));
        SOCKET_CHECK(original);
        const auto saved = *original;
        const char* invalid[] = {
            R"({"sockets":[{"name":"Candidate","parent":"Root"},{"name":"Bad","parent":"Child","position":[1e400,0,0]}]})",
            R"({"sockets":[{"name":"Candidate","parent":"Root"},{"name":"Bad","parent":"Child","rotation":[0,0,0,2]}]})",
            R"({"sockets":[{"name":"Candidate","parent":"Root"},{"name":"Bad","parent":"Child","scale":[2,2,2]}]})",
            R"({"sockets":[{"name":"Candidate","parent":"Root"},{"name":"","parent":"Child"}]})"};
        for (const char* json : invalid)
        {
            SOCKET_CHECK(!fixture.Skeleton->ApplySocketsJson(C::String(json), report));
            const auto* retained = fixture.Skeleton->FindSocket(Identity("Hand"));
            SOCKET_CHECK(retained && retained->ParentJoint == saved.ParentJoint);
            SOCKET_CHECK(retained->Offset.position.x == saved.Offset.position.x &&
                         retained->Offset.position.y == saved.Offset.position.y &&
                         retained->Offset.position.z == saved.Offset.position.z);
            SOCKET_CHECK(retained->Offset.rotation.x == saved.Offset.rotation.x &&
                         retained->Offset.rotation.y == saved.Offset.rotation.y &&
                         retained->Offset.rotation.z == saved.Offset.rotation.z &&
                         retained->Offset.rotation.w == saved.Offset.rotation.w);
            SOCKET_CHECK(retained->Offset.scale.x == saved.Offset.scale.x &&
                         retained->Offset.scale.y == saved.Offset.scale.y &&
                         retained->Offset.scale.z == saved.Offset.scale.z);
            SOCKET_CHECK(!fixture.Skeleton->FindSocket(Identity("Candidate")) &&
                         !fixture.Skeleton->FindSocket(Identity("Bad")) &&
                         !fixture.Skeleton->FindSocket(Identity("")));
        }
    }

    void TestPose(const M::Quaternion& childRotation, float jointScale, bool checkJson)
    {
        SocketFixture fixture(childRotation, jointScale);
        World world;
        world.Initialize();
        auto* owner = world.SpawnEntity<Entity>();
        SOCKET_CHECK(owner);
        auto* component = world.CreateComponent<Component::SkinnedMeshComponent>(owner);
        SOCKET_CHECK(component);
        owner->SetLocalPosition(-3.0f, 4.0f, 2.0f);
        owner->SetLocalRotation(M::QuaternionUtils::Normalize({-0.16f, 0.18f, 0.24f, 0.93f}));
        owner->SetLocalScale(1.7f, 1.7f, 1.7f);
        component->SetSkeletalAsset(fixture.Asset);
        component->SetMeshNodeGlobalTransform(fixture.MeshNode);
        component->SetPlaying(false);
        world.Tick(0);
        world.LateTick(0);

        M::Transform socket;
        SOCKET_CHECK(component->GetSocketWorldTransform(Identity("Hand"), socket));
        A::SkeletalPoseSnapshot pose;
        SOCKET_CHECK(A::SkeletalAnimationSampler::Sample(*fixture.Skeleton, *fixture.Clip, *fixture.Mesh, 0,
                                                         fixture.MeshNode, pose));
        SOCKET_CHECK(pose.BonePalette.size() == 2);
        const auto ownerMatrix = Matrix(owner->GetWorldTransform());
        const auto socketMatrix = Matrix(socket);
        const size_t pointCount = jointScale == 1.0f ? 4 : 1;
        for (size_t i = 0; i < pointCount; ++i)
        {
            const auto skinned = A::SkinPosition(fixture.Mesh->GetVertices()[i], pose.BonePalette);
            const auto skinnedWorld = M::MatrixUtils::TransformPointRowVector(ownerMatrix, skinned);
            const auto socketWorld = M::MatrixUtils::TransformPointRowVector(socketMatrix, fixture.Points[i]);
            NearPoint(socketWorld, skinnedWorld);
        }
        if (jointScale != 1.0f)
        {
            NearPoint(socket.scale, owner->GetWorldTransform().scale);
        }
        if (checkJson)
        {
            TestJsonRejection(fixture);
        }
        world.Finalize();
    }
    struct SocketClipResolver : A::IClipResolver
    {
        C::TSharedPtr<AnimationClipResource> Clip;

        explicit SocketClipResolver(const C::TSharedPtr<AnimationClipResource>& clip) : Clip(clip)
        {
        }

        C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
        {
            return name == C::StringView("moving") ? Clip : C::TSharedPtr<AnimationClipResource>{};
        }
    };

    void NearRotation(const M::Quaternion& actual, const M::Quaternion& expected)
    {
        const float sign = M::QuaternionUtils::Dot(actual, expected) < 0 ? -1.0f : 1.0f;
        Near(actual.x, sign * expected.x);
        Near(actual.y, sign * expected.y);
        Near(actual.z, sign * expected.z);
        Near(actual.w, sign * expected.w);
    }

    void TestAttachmentTiming(bool bAnimatorDriven, bool bFollowerFirst)
    {
        std::fprintf(stderr, "ソケット追従: 駆動=%s, 生成順=%s\n",
                     bAnimatorDriven ? "Animator" : "Clip", bFollowerFirst ? "追従側が先" : "骨格側が先");
        const auto childRotation = M::QuaternionUtils::Normalize({0.25f, -0.38f, 0.17f, 0.85f});
        SocketFixture fixture(childRotation, 1.0f);
        auto clip = fixture.Registry.CreateTransient<AnimationClipResource>("SocketMovingClip");
        SOCKET_CHECK(clip);

        // 1 秒かけて大きく移動・回転し、2 秒で元の姿勢へ戻るループ。
        S::SkeletalAnimationClip data;
        data.Name = "moving";
        data.DurationSeconds = 2.0f;
        S::SkeletalAnimationChannel translation;
        translation.JointIndex = 0;
        translation.Path = S::SkeletalAnimationPath::Translation;
        translation.Samples.push_back({0, {0, 0, 0, 0}});
        translation.Samples.push_back({1, {12, 5, -3, 0}});
        translation.Samples.push_back({2, {0, 0, 0, 0}});
        data.Channels.push_back(std::move(translation));
        constexpr float halfSqrt = 0.7071067811865475f;
        S::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = S::SkeletalAnimationPath::Rotation;
        rotation.Samples.push_back({0, {0, 0, 0, 1}});
        rotation.Samples.push_back({1, {0, 0, halfSqrt, halfSqrt}});
        rotation.Samples.push_back({2, {0, 0, 0, 1}});
        data.Channels.push_back(std::move(rotation));
        AddChannel(data, 1, S::SkeletalAnimationPath::Translation, {0.35f, -0.7f, 1.2f, 0});
        AddChannel(data, 1, S::SkeletalAnimationPath::Rotation,
                   {childRotation.x, childRotation.y, childRotation.z, childRotation.w});
        clip->SetClip(std::move(data));
        SOCKET_CHECK(clip->Load());
        fixture.Asset->SetResources(fixture.Mesh, fixture.Skeleton, clip);
        SOCKET_CHECK(fixture.Asset->Load());

        World world;
        world.Initialize();
        auto* first = world.SpawnEntity<Entity>();
        auto* second = world.SpawnEntity<Entity>();
        SOCKET_CHECK(first && second);
        auto* follower = bFollowerFirst ? first : second;
        auto* target = bFollowerFirst ? second : first;
        auto* attachment = world.CreateComponent<Component::SocketAttachmentComponent>(follower);
        auto* mesh = world.CreateComponent<Component::SkinnedMeshComponent>(target);
        SOCKET_CHECK(attachment && mesh);
        target->SetLocalPosition(-3.0f, 4.0f, 2.0f);
        target->SetLocalRotation(M::QuaternionUtils::Normalize({-0.16f, 0.18f, 0.24f, 0.93f}));
        target->SetLocalScale(1.7f, 1.7f, 1.7f);
        mesh->SetSkeletalAsset(fixture.Asset);
        mesh->SetMeshNodeGlobalTransform(fixture.MeshNode);
        mesh->SetLooping(true);
        mesh->SetPlaying(!bAnimatorDriven);
        if (bAnimatorDriven)
        {
            SocketClipResolver resolver(clip);
            auto graph = fixture.Registry.CreateTransient<AnimGraphResource>("SocketMovingGraph");
            A::AnimGraphReport report;
            SOCKET_CHECK(graph && graph->Compile(
                C::String(R"({"version":1,"nodes":[{"id":"move","type":"clip","clip":"moving","loop":true}],"root":"move"})"),
                *fixture.Skeleton, resolver, report) && graph->Load());
            auto* animator = world.CreateComponent<Component::AnimatorComponent>(target);
            SOCKET_CHECK(animator && animator->SetGraph(graph) && mesh->IsExternalAnimationDriven());
        }
        else
        {
            SOCKET_CHECK(mesh->IsPlaying() && !mesh->IsExternalAnimationDriven());
        }
        SOCKET_CHECK(attachment->Attach(target->GetObjectId(), Identity("Hand"), {}, 0));

        M::Vector3 previous;
        constexpr float dt = 1.0f / 30.0f;
        for (unsigned frame = 0; frame < 6; ++frame)
        {
            world.Tick(dt);
            world.LateTick(dt);
            M::Transform socket;
            SOCKET_CHECK(mesh->GetSocketWorldTransform(Identity("Hand"), socket));
            const auto& followerTransform = follower->GetWorldTransform();
            NearPoint(followerTransform.position, socket.position);
            NearRotation(followerTransform.rotation, socket.rotation);
            if (frame > 0)
            {
                SOCKET_CHECK((socket.position - previous).LengthSquared() > 1e-6f);
            }
            previous = socket.position;
        }
        world.Finalize();
    }
} // namespace

void TestSocketOracle()
{
    constexpr float halfSqrt = 0.7071067811865475f;
    TestPose(M::Quaternion(0, 0, halfSqrt, halfSqrt), 1.0f, false);
    const auto arbitrary = M::QuaternionUtils::Normalize({0.25f, -0.38f, 0.17f, 0.85f});
    TestPose(arbitrary, 1.0f, true);
    TestPose(arbitrary, 2.0f, false);
    TestAttachmentTiming(false, true);
    TestAttachmentTiming(false, false);
    TestAttachmentTiming(true, true);
    TestAttachmentTiming(true, false);
}
