#include "AnimationDebugScene.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Component/AnimatorComponent.h"
#include "Core/Public/Component/HoldSlotComponent.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Component/SocketAttachmentComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"
#include "Core/Public/Animation/FootIKModifier.h"
#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Math/MatrixUtils.h"
#include "Physics/ColliderComponent.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace A = NorvesLib::Core::Animation;
    namespace M = NorvesLib::Math;
    namespace
    {
        bool Panel(SkinnedMeshResource& mesh, float halfWidth, float low, float high, bool skinTop)
        {
            C::VariableArray<S::SkeletalVertex> vertices;
            C::VariableArray<uint32_t> indices;
            for (uint32_t face = 0; face < 2; ++face)
            {
                const float xy[4][2] = {{-halfWidth, low}, {halfWidth, low}, {halfWidth, high}, {-halfWidth, high}};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    S::SkeletalVertex v;
                    v.Position = {xy[i][0], xy[i][1], 0};
                    v.Normal = {0, 0, face ? -1.f : 1.f};
                    v.TexCoord = {i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f};
                    v.JointIndices = {skinTop && i >= 2 ? 1u : 0u, 0, 0, 0};
                    v.JointWeights = {1, 0, 0, 0};
                    vertices.push_back(v);
                }
                const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
                for (uint32_t i = 0; i < 6; i += 3)
                {
                    indices.push_back(face * 4 + order[i]);
                    indices.push_back(face * 4 + order[i + (face ? 2 : 1)]);
                    indices.push_back(face * 4 + order[i + (face ? 1 : 2)]);
                }
            }
            mesh.SetVertices(std::move(vertices));
            mesh.SetIndices(std::move(indices));
            return mesh.Load();
        }
        S::SkeletalAnimationClip Motion(const char* name, float duration, bool yaw)
        {
            S::SkeletalAnimationClip clip;
            clip.Name = name;
            clip.DurationSeconds = duration;
            clip.Metadata.Root.NominalSpeed = 1;
            clip.Metadata.Markers = {{Identity("Left"), yaw ? duration * .1f : 0},
                                     {Identity("Right"), duration * (yaw ? .7f : .4f)}};
            S::SkeletalAnimationChannel channel;
            channel.JointIndex = 1;
            channel.Path = S::SkeletalAnimationPath::Rotation;
            for (unsigned i = 0; i <= 32; ++i)
            {
                const float time = duration * float(i) / 32, angle = .6f * std::sin(6.283185307f * float(i) / 32);
                const float q = std::sin(angle * .5f), w = std::cos(angle * .5f);
                channel.Samples.push_back({time, yaw ? S::SkeletalValue{0, q, 0, w} : S::SkeletalValue{0, 0, q, w}});
            }
            clip.Channels.push_back(std::move(channel));
            return clip;
        }
        constexpr float kQuadrupedFloorY = -0.98f;
        constexpr float kQuadrupedSpeed = 1.21f;
        constexpr float kQuadrupedTurnRadius = 1.2f;
        constexpr float kQuadrupedStraightLength = 2.6f;
        constexpr float kPi = 3.141592654f;
        constexpr float kRampAngle = 18.0f * kPi / 180.0f;
        constexpr float kRampRun = 1.1f;
        constexpr float kRampHalfThickness = .08f;
        const float kRampHeight = kRampRun * std::tan(kRampAngle);
        const M::Vector3 kQuadrupedCenter(-3.5f, kQuadrupedFloorY, 2.0f);

        struct GroundBox
        {
            M::Vector3 Center;
            M::Vector3 HalfSize;
            float Pitch = 0;
        };
        // 同じ寸法を描画・物理・胴体の高さに使う。斜面の中心は天面の投影位置から求める。
        const GroundBox kQuadrupedGround[] = {
            {{-3.5f, kQuadrupedFloorY - .12f, 2.0f}, {3.2f, .12f, 3.2f}},
            {{-4.7f, kQuadrupedFloorY + kRampHeight * .5f - kRampHalfThickness * std::cos(kRampAngle),
              1.25f + kRampHalfThickness * std::sin(kRampAngle)},
             {.5f, kRampHalfThickness, kRampRun / (2 * std::cos(kRampAngle))}, -kRampAngle},
            {{-4.7f, kQuadrupedFloorY + kRampHeight * .5f, 2.0f}, {.5f, kRampHeight * .5f, .2f}},
            {{-4.7f, kQuadrupedFloorY + kRampHeight * .5f - kRampHalfThickness * std::cos(kRampAngle),
              2.75f - kRampHalfThickness * std::sin(kRampAngle)},
             {.5f, kRampHalfThickness, kRampRun / (2 * std::cos(kRampAngle))}, kRampAngle},
            // 両端から三段で上り下りする。中央の天面は長く取り、足を置く余地を残す。
            {{-2.3f, kQuadrupedFloorY + .06f, .85f}, {.5f, .06f, .15f}},
            {{-2.3f, kQuadrupedFloorY + .12f, 1.15f}, {.5f, .12f, .15f}},
            {{-2.3f, kQuadrupedFloorY + .18f, 2.0f}, {.5f, .18f, .7f}},
            {{-2.3f, kQuadrupedFloorY + .12f, 2.85f}, {.5f, .12f, .15f}},
            {{-2.3f, kQuadrupedFloorY + .06f, 3.15f}, {.5f, .06f, .15f}}};

        float QuadrupedGroundHeight(const M::Vector3& position)
        {
            float height = kQuadrupedFloorY;
            for (const auto& box : kQuadrupedGround)
            {
                const float cosine = std::cos(box.Pitch);
                const float sine = std::sin(box.Pitch);
                const float offsetZ = position.z - box.Center.z;
                const float offsetY = box.HalfSize.y / cosine - offsetZ * sine / cosine;
                const float localZ = offsetZ * cosine - offsetY * sine;
                // 経路は天面上を通る。斜面の端の側面は床または中央の台に埋まる。
                if (std::abs(position.x - box.Center.x) <= box.HalfSize.x &&
                    std::abs(localZ) <= box.HalfSize.z + .0001f)
                {
                    height = std::max(height, box.Center.y + offsetY);
                }
            }
            return height;
        }

        struct QuadrupedPathSample
        {
            M::Vector3 Position;
            float Yaw = 0;
            float TurnRate = 0;
        };

        QuadrupedPathSample SampleQuadrupedPath(double time)
        {
            const float arcLength = kPi * kQuadrupedTurnRadius;
            const float length = 2 * (kQuadrupedStraightLength + arcLength);
            // 床だけの下側半円の中央から始め、斜面、上側半円、階段、床へ戻る。
            float distance = float(std::fmod(time * kQuadrupedSpeed +
                2 * kQuadrupedStraightLength + 1.5f * arcLength, length));
            const float lowZ = kQuadrupedCenter.z - kQuadrupedStraightLength * .5f;
            const float highZ = kQuadrupedCenter.z + kQuadrupedStraightLength * .5f;
            QuadrupedPathSample sample;
            if (distance < kQuadrupedStraightLength)
            {
                sample.Position = M::Vector3(kQuadrupedCenter.x - kQuadrupedTurnRadius, 0, lowZ + distance);
            }
            else if ((distance -= kQuadrupedStraightLength) < arcLength)
            {
                const float angle = distance / kQuadrupedTurnRadius;
                sample.Position = M::Vector3(kQuadrupedCenter.x - kQuadrupedTurnRadius * std::cos(angle),
                                            0, highZ + kQuadrupedTurnRadius * std::sin(angle));
                sample.Yaw = angle;
                sample.TurnRate = kQuadrupedSpeed / kQuadrupedTurnRadius;
            }
            else if ((distance -= arcLength) < kQuadrupedStraightLength)
            {
                sample.Position = M::Vector3(kQuadrupedCenter.x + kQuadrupedTurnRadius, 0, highZ - distance);
                sample.Yaw = kPi;
            }
            else
            {
                distance -= kQuadrupedStraightLength;
                const float angle = distance / kQuadrupedTurnRadius;
                sample.Position = M::Vector3(kQuadrupedCenter.x + kQuadrupedTurnRadius * std::cos(angle),
                                            0, lowZ - kQuadrupedTurnRadius * std::sin(angle));
                sample.Yaw = kPi + angle;
                sample.TurnRate = kQuadrupedSpeed / kQuadrupedTurnRadius;
            }
            return sample;
        }

        struct JointSpec
        {
            const char* Name;
            int32_t Parent;
            M::Vector3 Position;
            M::Vector3 HalfSize;
        };
        // 座標はモデル空間。前方は +Z、脚の根は骨盤または胸の子にする。
        const JointSpec kQuadrupedJoints[] = {
            {"Pelvis", -1, {0, .68f, -.32f}, {.16f, .12f, .17f}},
            {"Spine1", 0, {0, .70f, -.13f}, {.14f, .11f, .12f}},
            {"Spine2", 1, {0, .72f, .07f}, {.15f, .12f, .12f}},
            {"Chest", 2, {0, .73f, .30f}, {.18f, .14f, .15f}},
            {"Neck", 3, {0, .88f, .43f}, {.10f, .13f, .10f}},
            {"Head", 4, {0, 1.02f, .57f}, {.13f, .12f, .20f}},
            {"EarLeft", 5, {-.11f, 1.11f, .53f}, {.045f, .08f, .045f}},
            {"EarLeftTip", 6, {-.15f, 1.22f, .51f}, {.035f, .035f, .03f}},
            {"EarRight", 5, {.11f, 1.11f, .53f}, {.045f, .08f, .045f}},
            {"EarRightTip", 8, {.15f, 1.22f, .51f}, {.035f, .035f, .03f}},
            {"TailRoot", 0, {0, .75f, -.46f}, {.045f, .045f, .10f}},
            {"TailMid", 10, {0, .82f, -.65f}, {.04f, .04f, .10f}},
            {"TailTip", 11, {0, .86f, -.85f}, {.03f, .03f, .10f}},
            {"FrontLeftUpper", 3, {-.18f, .67f, .30f}, {.045f, .045f, .045f}},
            {"FrontLeftLower", 13, {-.18f, .35f, .18f}, {.04f, .04f, .04f}},
            {"FrontLeftAnkle", 14, {-.18f, .055f, .30f}, {.065f, .055f, .10f}},
            {"FrontRightUpper", 3, {.18f, .67f, .30f}, {.045f, .045f, .045f}},
            {"FrontRightLower", 16, {.18f, .35f, .18f}, {.04f, .04f, .04f}},
            {"FrontRightAnkle", 17, {.18f, .055f, .30f}, {.065f, .055f, .10f}},
            {"RearLeftUpper", 0, {-.17f, .62f, -.32f}, {.055f, .055f, .055f}},
            {"RearLeftLower", 19, {-.17f, .34f, -.15f}, {.045f, .045f, .045f}},
            {"RearLeftAnkle", 20, {-.17f, .055f, -.32f}, {.065f, .055f, .10f}},
            {"RearRightUpper", 0, {.17f, .62f, -.32f}, {.055f, .055f, .055f}},
            {"RearRightLower", 22, {.17f, .34f, -.15f}, {.045f, .045f, .045f}},
            {"RearRightAnkle", 23, {.17f, .055f, -.32f}, {.065f, .055f, .10f}},
        };

        void AppendBox(C::VariableArray<S::SkeletalVertex>& vertices, C::VariableArray<uint32_t>& indices,
                       uint32_t joint, const M::Vector3& center, const M::Vector3& halfSize,
                       const M::Quaternion& rotation = M::Quaternion())
        {
            const M::Vector3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            // 各面を外側から見て反時計回りに並べる。
            const M::Vector3 corners[6][4] = {
                {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},
                {{-1,-1,1},{-1,1,1},{-1,1,-1},{-1,-1,-1}},
                {{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},
                {{-1,-1,1},{-1,-1,-1},{1,-1,-1},{1,-1,1}},
                {{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,1}},
                {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}};
            for (uint32_t face = 0; face < 6; ++face)
            {
                const auto base = static_cast<uint32_t>(vertices.size());
                for (uint32_t corner = 0; corner < 4; ++corner)
                {
                    const auto& c = corners[face][corner];
                    const auto position = center + rotation * M::Vector3(c.x * halfSize.x, c.y * halfSize.y, c.z * halfSize.z);
                    const auto normal = rotation * normals[face];
                    S::SkeletalVertex vertex;
                    vertex.Position = {position.x, position.y, position.z};
                    vertex.Normal = {normal.x, normal.y, normal.z};
                    vertex.TexCoord = {corner == 1 || corner == 2 ? 1.f : 0.f, corner >= 2 ? 1.f : 0.f};
                    vertex.JointIndices = {joint, 0, 0, 0};
                    vertex.JointWeights = {1, 0, 0, 0};
                    vertices.push_back(vertex);
                }
                for (const uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
                {
                    indices.push_back(base + index);
                }
            }
        }

        bool QuadrupedGeometry(SkeletonResource& skeleton, SkinnedMeshResource& mesh)
        {
            C::VariableArray<S::SkeletalJoint> joints;
            C::VariableArray<S::SkeletalRestTransform> rest;
            C::VariableArray<S::SkeletalVertex> vertices;
            C::VariableArray<uint32_t> indices;
            for (uint32_t i = 0; i < sizeof(kQuadrupedJoints) / sizeof(kQuadrupedJoints[0]); ++i)
            {
                const auto& spec = kQuadrupedJoints[i];
                A::JointTransform model;
                model.Translation = spec.Position;
                const auto inverse = M::MatrixUtils::Inverse(A::ToRowMatrix(model));
                S::SkeletalJoint joint;
                joint.Name = spec.Name;
                joint.ParentIndex = spec.Parent;
                // 行列は演算 API で求め、資産の連続した保存領域へ丸ごと転送する。
                static_assert(sizeof(inverse) == sizeof(float) * 16);
                std::memcpy(joint.InverseBindMatrix.data(), &inverse, sizeof(inverse));
                joints.push_back(std::move(joint));
                const auto local = spec.Parent < 0 ? spec.Position : spec.Position - kQuadrupedJoints[spec.Parent].Position;
                S::SkeletalRestTransform transform;
                transform.Translation = {local.x, local.y, local.z};
                rest.push_back(transform);
                AppendBox(vertices, indices, i, spec.Position, spec.HalfSize);
                // 脚の上・下節は親の回転で振れる長い箱にする。
                if (i >= 13 && (i - 13) % 3 != 2)
                {
                    const auto offset = kQuadrupedJoints[i + 1].Position - spec.Position;
                    const auto axis = M::Vector3::Cross(M::Vector3::UnitY, offset.Normalized());
                    const float dot = M::Vector3::Dot(M::Vector3::UnitY, offset.Normalized());
                    const auto rotation = M::Quaternion(axis, std::acos(dot));
                    AppendBox(vertices, indices, i, spec.Position + offset * .5f,
                              M::Vector3(.04f, offset.Length() * .5f, .04f), rotation);
                }
            }
            skeleton.SetJoints(std::move(joints));
            mesh.SetVertices(std::move(vertices));
            mesh.SetIndices(std::move(indices));
            return skeleton.SetAuthorRestPose(rest) && skeleton.Load() && mesh.Load();
        }

        S::SkeletalAnimationClip QuadrupedWalk()
        {
            S::SkeletalAnimationClip clip;
            clip.Name = "walk";
            clip.DurationSeconds = 1.2f;
            clip.Metadata.Root.NominalSpeed = kQuadrupedSpeed;
            clip.Metadata.Markers = {{Identity("Left"), 0}, {Identity("Right"), .6f}};
            const char* const down[] = {"Foot.FrontLeft.Down", "Foot.FrontRight.Down",
                                       "Foot.RearLeft.Down", "Foot.RearRight.Down"};
            const char* const up[] = {"Foot.FrontLeft.Up", "Foot.FrontRight.Up",
                                     "Foot.RearLeft.Up", "Foot.RearRight.Up"};
            for (uint32_t leg = 0; leg < 4; ++leg)
            {
                const bool bOpposite = leg == 1 || leg == 2;
                // 上節の振りを対角で反転する。接地窓は後方へ蹴る半周期に合わせる。
                A::AnimEvent plant;
                plant.Name = Identity(down[leg]);
                plant.Time = bOpposite ? .3f : .9f;
                clip.Metadata.Events.push_back(plant);
                plant.Name = Identity(up[leg]);
                plant.Time = bOpposite ? .9f : .3f;
                clip.Metadata.Events.push_back(plant);
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = 13 + leg * 3;
                channel.Path = S::SkeletalAnimationPath::Rotation;
                for (uint32_t sample = 0; sample <= 48; ++sample)
                {
                    const float phase = float(sample) / 48;
                    const float angle = .4f * std::sin(6.283185307f * phase + (bOpposite ? 3.141592654f : 0));
                    channel.Samples.push_back({phase * clip.DurationSeconds,
                                               {std::sin(angle * .5f), 0, 0, std::cos(angle * .5f)}});
                }
                clip.Channels.push_back(std::move(channel));
            }
            std::stable_sort(clip.Metadata.Events.begin(), clip.Metadata.Events.end(),
                             [](const auto& a, const auto& b) { return a.Time < b.Time; });
            return clip;
        }

        bool DecodeUtf8(const char* bytes, size_t size, C::String& out)
        {
            out.clear();
            if (!bytes || size == 0 || size > 1024 * 1024)
            {
                return false;
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, int(size), nullptr, 0);
            if (count <= 0)
            {
                return false;
            }
            C::VariableArray<wchar_t> wide(size_t(count), wchar_t(0));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, int(size), wide.data(), count) != count)
            {
                return false;
            }
            if constexpr (sizeof(TCHAR) == 1)
            {
                for (size_t i = 0; i < size; ++i)
                {
                    out.push_back(TCHAR(bytes[i]));
                }
            }
            else
            {
                for (const auto unit : wide)
                {
                    out.push_back(TCHAR(unit));
                }
            }
            return true;
        }

        struct Resolver final : A::IClipResolver
        {
            C::TSharedPtr<AnimationClipResource> Slow, Fast;
            C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
            {
                return name == C::StringView("slow")   ? Slow
                       : name == C::StringView("fast") ? Fast
                                                       : C::TSharedPtr<AnimationClipResource>{};
            }
        };
    } // namespace
    AnimationDebugScene::~AnimationDebugScene()
    {
        Stop();
    }
    bool AnimationDebugScene::Prepare(GameMode::GameModeContext& ctx, Rendering::MaterialHandle material,
                                      bool bPoseModifiersEnabled)
    {
        Stop();
        if (!material.IsValid())
            return false;
        Particle::SwordTrailRibbonSettings trailSettings;
        // 剣の板は局所 Y 軸の 0〜0.8 に作る。
        trailSettings.BaseLocal = M::Vector3(0.0f, 0.0f, 0.0f);
        trailSettings.TipLocal = M::Vector3(0.0f, 0.8f, 0.0f);
        trailSettings.Lifetime = 0.25f;
        trailSettings.MaxPoints = 128;
        trailSettings.MaxBendDegrees = 10.0f;
        trailSettings.Color = M::Vector4(0.6f, 0.8f, 1.0f, 0.8f);
        trailSettings.Blend = Rendering::RibbonTrailBlend::Additive;
        if (!m_SwordTrail.Configure(trailSettings))
            return false;
        m_TrailScene.Vertices.reserve(trailSettings.MaxPoints * 2);
        m_TrailScene.Draws.reserve(1);
        auto& registry = GEngine.GetResourceRegistry();
        auto skeleton = registry.CreateTransient<SkeletonResource>("AnimationDebugSkeleton");
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugBody");
        auto itemMesh = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugSword");
        Resolver resolver;
        resolver.Slow = registry.CreateTransient<AnimationClipResource>("AnimationDebugSlow");
        resolver.Fast = registry.CreateTransient<AnimationClipResource>("AnimationDebugFast");
        auto still = registry.CreateTransient<AnimationClipResource>("AnimationDebugStill");
        m_CharacterAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugCharacter");
        m_ItemAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugItem");
        m_Graph = registry.CreateTransient<AnimGraphResource>("AnimationDebugGraph");
        if (!skeleton || !mesh || !itemMesh || !resolver.Slow || !resolver.Fast || !still || !m_CharacterAsset ||
            !m_ItemAsset || !m_Graph)
            return false;
        C::VariableArray<S::SkeletalJoint> joints(2);
        joints[0].Name = "Root";
        joints[1].Name = "Head";
        joints[1].ParentIndex = 0;
        for (auto& joint : joints)
        {
            joint.InverseBindMatrix.fill(0);
            for (unsigned i = 0; i < 4; ++i)
                joint.InverseBindMatrix[i * 5] = 1;
        }
        skeleton->SetJoints(std::move(joints));
        if (!skeleton->Load() || !Panel(*mesh, .45f, -.7f, .7f, true) || !Panel(*itemMesh, .04f, 0, .8f, false))
            return false;
        resolver.Slow->SetClip(Motion("slow", 2, false));
        resolver.Fast->SetClip(Motion("fast", 1.4f, true));
        S::SkeletalAnimationClip itemClip;
        itemClip.Name = "still";
        itemClip.DurationSeconds = 1;
        S::SkeletalAnimationChannel root;
        root.JointIndex = 0;
        root.Path = S::SkeletalAnimationPath::Translation;
        root.Samples = {{0, {0, 0, 0, 0}}};
        itemClip.Channels.push_back(root);
        still->SetClip(std::move(itemClip));
        if (!resolver.Slow->Load() || !resolver.Fast->Load() || !still->Load())
            return false;
        m_CharacterAsset->SetClipResources(mesh, skeleton, {resolver.Slow, resolver.Fast});
        m_ItemAsset->SetResources(itemMesh, skeleton, still);
        if (!m_CharacterAsset->Load() || !m_ItemAsset->Load())
            return false;
        A::SocketReport socketReport;
        if (!skeleton->ApplySocketsJson(
                C::String(R"({"sockets":[{"name":"Mouth","parent":"Head","position":[0,0.55,0]}]})"), socketReport))
            return false;
        A::AnimGraphReport report;
        const C::String json(
            R"({"version":1,"params":[{"name":"Blend","type":"float","value":0.5},{"name":"Speed","type":"float","value":1},{"name":"Profile","type":"int"}],"syncGroups":[{"name":"Gait","speed":"Speed","minRate":0,"maxRate":2}],"nodes":[{"id":"slow","type":"clip","clip":"slow","syncGroup":"Gait"},{"id":"fast","type":"clip","clip":"fast","syncGroup":"Gait"},{"id":"mix","type":"blend2","children":["slow","fast"],"weight":"Blend"},{"id":"locomotion","type":"stateMachine","initial":"Blend","states":[{"name":"Slow","node":"slow"},{"name":"Blend","node":"mix"}]}],"root":"locomotion"})");
        if (!m_Graph->Compile(json, *skeleton, resolver, report) || !m_Graph->Load())
            return false;
        auto* character = ctx.ScopeRef.SpawnObject<Entity>();
        auto* item = ctx.ScopeRef.SpawnObject<Entity>();
        if (!character || !item)
            return false;
        character->SetPosition(2.8f, 0, 1.5f);
        auto* skinned = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(character);
        auto* animator = ctx.WorldRef.CreateComponent<Component::AnimatorComponent>(character);
        auto* slots = ctx.WorldRef.CreateComponent<Component::HoldSlotComponent>(character);
        auto* sword = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(item);
        auto* attachment = ctx.WorldRef.CreateComponent<Component::SocketAttachmentComponent>(item);
        if (!skinned || !animator || !slots || !sword || !attachment)
            return false;
        skinned->SetSkeletalAsset(m_CharacterAsset);
        skinned->SetMaterial(material);
        sword->SetSkeletalAsset(m_ItemAsset);
        sword->SetMaterial(material);
        sword->SetPlaying(false);
        if (!animator->SetGraph(m_Graph) ||
            !slots->ApplySlotsJson(C::String(R"({"holdSlots":[{"name":"Mouth","capacity":1,"acceptTags":["Sword"]}]})"),
                                   socketReport))
            return false;
        if (!attachment->ApplySettingsJson(
                C::String(
                    R"({"attachProfiles":[{"name":"Along"},{"name":"Side","rotation":[0,0,0.7071067811865475,0.7071067811865475]}],"grip":{"position":[0,0.05,0]}})"),
                socketReport))
            return false;
        attachment->BindProfileParameter(Identity("Profile"));
        const Identity tag("Sword");
        if (!attachment->Attach(character->GetObjectId(), Identity("Mouth"), Identity("Mouth"), .1f, {&tag, 1}))
            return false;
        // 剣はソケット先とは別のルートなので、自身に描画補間を要求する。
        item->SetRenderInterpolationEnabled(true);
        m_Character = character->GetObjectId();
        m_Item = item->GetObjectId();
        if (!PrepareQuadruped(ctx, material, bPoseModifiersEnabled))
        {
            Stop();
            return false;
        }
        LOG_INFO("ANIMATION_DEBUG scene=ready synthetic=1");
        return true;
    }
    bool AnimationDebugScene::PrepareQuadruped(GameMode::GameModeContext& ctx, Rendering::MaterialHandle material,
                                                bool bPoseModifiersEnabled)
    {
        m_bPoseModifiersEnabled = bPoseModifiersEnabled;
        auto& registry = GEngine.GetResourceRegistry();
        auto skeleton = registry.CreateTransient<SkeletonResource>("AnimationDebugQuadrupedSkeleton");
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugQuadrupedMesh");
        Resolver resolver;
        resolver.Slow = registry.CreateTransient<AnimationClipResource>("AnimationDebugQuadrupedWalk");
        m_QuadrupedAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugQuadruped");
        m_QuadrupedGraph = registry.CreateTransient<AnimGraphResource>("AnimationDebugQuadrupedGraph");
        if (!skeleton || !mesh || !resolver.Slow || !m_QuadrupedAsset || !m_QuadrupedGraph ||
            !QuadrupedGeometry(*skeleton, *mesh))
        {
            return false;
        }
        resolver.Slow->SetClip(QuadrupedWalk());
        if (!resolver.Slow->Load())
        {
            return false;
        }
        m_QuadrupedAsset->SetResources(mesh, skeleton, resolver.Slow);
        A::AnimGraphReport graphReport;
        const C::String graphJson(
            R"({"version":1,"params":[{"name":"Speed","type":"float","value":1.21},{"name":"BankAngle","type":"float"},{"name":"PitchAngle","type":"float"},{"name":"LeanWeight","type":"float","value":1},{"name":"SpineWeight","type":"float","value":1},{"name":"LookWeight","type":"float","value":1},{"name":"FootIKWeight","type":"float","value":1},{"name":"SpringBoneWeight","type":"float","value":1},{"name":"FollowThroughWeight","type":"float","value":1}],"syncGroups":[{"name":"Gait","speed":"Speed","minRate":0.5,"maxRate":2}],"nodes":[{"id":"walk","type":"clip","clip":"slow","syncGroup":"Gait"}],"root":"walk"})");
        if (!m_QuadrupedAsset->Load() || !m_QuadrupedGraph->Compile(graphJson, *skeleton, resolver, graphReport) ||
            !m_QuadrupedGraph->Load())
        {
            return false;
        }
        auto* quadruped = ctx.ScopeRef.SpawnObject<Entity>();
        auto* floor = ctx.ScopeRef.SpawnObject<Entity>();
        if (!quadruped || !floor)
        {
            return false;
        }
        auto* skinned = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(quadruped);
        auto* animator = ctx.WorldRef.CreateComponent<Component::AnimatorComponent>(quadruped);
        auto* floorMesh = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(floor);
        if (!skinned || !animator || !floorMesh)
        {
            return false;
        }
        skinned->SetSkeletalAsset(m_QuadrupedAsset);
        skinned->SetMaterial(material);
        if (!animator->SetGraph(m_QuadrupedGraph))
        {
            return false;
        }
        m_Quadruped = quadruped->GetObjectId();
        // 床と障害物の描画は一つにまとめ、物理の箱にはそれぞれ同じ位置と回転を与える。
        const auto floorPosition = kQuadrupedGround[0].Center;
        floor->SetPosition(floorPosition);
        for (const auto& box : kQuadrupedGround)
        {
            auto* ground = ctx.ScopeRef.SpawnObject<Entity>();
            if (!ground)
            {
                return false;
            }
            ground->SetPosition(box.Center);
            ground->SetRotation(M::Quaternion(M::Vector3::UnitX, box.Pitch));
            auto* collider = ctx.WorldRef.CreateComponent<NorvesLib::Modules::Physics::ColliderComponent>(ground);
            if (!collider || collider->SetBox(box.HalfSize) != NorvesLib::Modules::Physics::EPhysicsResult::Success)
            {
                return false;
            }
        }
        auto floorSkeleton = registry.CreateTransient<SkeletonResource>("AnimationDebugFloorSkeleton");
        auto floorGeometry = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugFloorMesh");
        auto floorClip = registry.CreateTransient<AnimationClipResource>("AnimationDebugFloorStill");
        m_FloorAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugFloor");
        if (!floorSkeleton || !floorGeometry || !floorClip || !m_FloorAsset)
        {
            return false;
        }
        C::VariableArray<S::SkeletalJoint> floorJoints(1);
        floorJoints[0].Name = "Floor";
        const auto identity = M::Matrix4x4::Identity;
        std::memcpy(floorJoints[0].InverseBindMatrix.data(), &identity, sizeof(identity));
        floorSkeleton->SetJoints(std::move(floorJoints));
        C::VariableArray<S::SkeletalVertex> floorVertices;
        C::VariableArray<uint32_t> floorIndices;
        for (const auto& box : kQuadrupedGround)
        {
            AppendBox(floorVertices, floorIndices, 0, box.Center - floorPosition, box.HalfSize,
                      M::Quaternion(M::Vector3::UnitX, box.Pitch));
        }
        floorGeometry->SetVertices(std::move(floorVertices));
        floorGeometry->SetIndices(std::move(floorIndices));
        S::SkeletalAnimationClip still;
        still.Name = "still";
        still.DurationSeconds = 1;
        S::SkeletalAnimationChannel root;
        root.JointIndex = 0;
        root.Path = S::SkeletalAnimationPath::Translation;
        root.Samples = {{0, {0, 0, 0, 0}}};
        still.Channels.push_back(std::move(root));
        floorClip->SetClip(std::move(still));
        if (!floorSkeleton->Load() || !floorGeometry->Load() || !floorClip->Load())
        {
            return false;
        }
        m_FloorAsset->SetResources(floorGeometry, floorSkeleton, floorClip);
        if (!m_FloorAsset->Load())
        {
            return false;
        }
        floorMesh->SetSkeletalAsset(m_FloorAsset);
        floorMesh->SetMaterial(material);
        floorMesh->SetPlaying(false);

        m_WeightParameterNames = {"LeanWeight", "SpineWeight", "LookWeight", "FootIKWeight",
                                  "SpringBoneWeight", "FollowThroughWeight"};
        Asset::AssetFileReader reader;
        const auto asset = reader.Read("Gameplay/AnimationDebugQuadruped.posemod.json");
        if (!asset.Succeeded())
        {
            LOG_ERROR("ANIMATION_DEBUG 四足の姿勢補正ファイルを読めません read_status=%u", unsigned(asset.Status));
            return false;
        }
        const auto& path = asset.Path.GetResolvedPath();
        const auto bytes = asset.Blob.GetSpan();
        const size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        if (!DecodeUtf8(path.data(), path.size(), m_PoseModifiersPath) ||
            !DecodeUtf8(reinterpret_cast<const char*>(bytes.data() + skip), bytes.size() - skip, m_PoseModifiersJson))
        {
            return false;
        }
        A::Procedural::PoseModifierConfigReport poseReport;
        if (bPoseModifiersEnabled)
        {
            if (!animator->ApplyPoseModifiersFile(m_PoseModifiersPath, &poseReport))
            {
                LOG_ERROR("ANIMATION_DEBUG 四足の姿勢補正設定が不正です error=%u", unsigned(poseReport.Error));
                return false;
            }
            // Animator は補正の列挙口を持たないため、同じ設定から借用を結べる群を作って差し替える。
            A::AnimParamSet params;
            if (!params.Initialize(m_QuadrupedGraph->GetData()->Parameters) ||
                !A::Procedural::ParsePoseModifierConfig(m_PoseModifiersJson, *skeleton, &params,
                    A::Procedural::GetBuiltinPoseModifierTypes(), m_QuadrupedModifiers, poseReport))
            {
                return false;
            }
            animator->ClearModifiers();
            for (const auto& modifier : m_QuadrupedModifiers)
            {
                if (auto* footIk = A::FindFootIKModifier(modifier.get()))
                {
                    m_GroundQuery = C::MakeUnique<A::PhysicsGroundQuery>(
                        ctx.EngineRef.GetSceneQuery(), A::MakePhysicsGroundQuerySettings(footIk->GetSettings().Ground));
                    footIk->SetGroundQuery(m_GroundQuery.get());
                }
                if (!animator->AddModifier(modifier))
                {
                    return false;
                }
            }
        }
        else
        {
            m_GroundQuery = C::MakeUnique<A::PhysicsGroundQuery>(
                ctx.EngineRef.GetSceneQuery(), A::MakePhysicsGroundQuerySettings(A::FootIKGroundSettings{}));
        }
        TickQuadruped(ctx.WorldRef, 0);
        LOG_INFO("ANIMATION_DEBUG quadruped=ready pose_modifiers=%d", bPoseModifiersEnabled ? 1 : 0);
        return true;
    }

    void AnimationDebugScene::TickQuadruped(World& world, float deltaTime)
    {
        auto* quadruped = m_Quadruped ? world.FindEntityByObjectId(m_Quadruped) : nullptr;
        if (!quadruped)
        {
            return;
        }
        if (std::isfinite(deltaTime) && deltaTime > 0)
        {
            m_QuadrupedTime += double(deltaTime);
        }
        const auto sample = SampleQuadrupedPath(m_QuadrupedTime);
        const float groundHeight = QuadrupedGroundHeight(sample.Position);
        const float bank = std::atan(kQuadrupedSpeed * sample.TurnRate / 9.81f);
        if (std::isfinite(deltaTime) && deltaTime > 0)
        {
            // 胴体だけを滑らかに追従させる。各足の段差と法線への補正は FootIK が担う。
            m_QuadrupedHeight += (groundHeight - m_QuadrupedHeight) * (1 - std::exp(-deltaTime / .08f));
            m_QuadrupedBank += (bank - m_QuadrupedBank) * (1 - std::exp(-deltaTime / .12f));
        }
        else if (m_QuadrupedTime == 0)
        {
            m_QuadrupedHeight = groundHeight;
            m_QuadrupedBank = bank;
        }
        quadruped->SetPosition(sample.Position.x, m_QuadrupedHeight, sample.Position.z);
        quadruped->SetRotation(M::Quaternion(M::Vector3::UnitY, sample.Yaw));
        if (auto* animator = quadruped->GetComponent<Component::AnimatorComponent>())
        {
            // 向心加速度と重力の比から旋回の傾きを求め、直線との境目を平滑化する。
            (void)animator->SetFloat(animator->FindParam(Identity("BankAngle")), m_QuadrupedBank);
            const float phase = float(std::fmod(m_QuadrupedTime * .55, 6.283185307179586));
            (void)animator->SetFloat(animator->FindParam(Identity("PitchAngle")), .08f * std::sin(phase * 2));
            animator->SetLookTarget(0, Component::AnimatorLookTarget::AtWorldPosition(
                kQuadrupedCenter + M::Vector3(.5f, 1.6f, .5f)));
        }
    }

    void AnimationDebugScene::ActivateViews(World& world)
    {
#if defined(NORVES_ENABLE_IMGUI)
        if (m_Character && m_Item)
        {
            m_AnimatorView.Attach(world, m_Character);
            m_SocketView.Attach(world, m_Character, m_Item);
        }
        if (m_Quadruped)
        {
            m_PoseModifierView.Attach(world, m_Quadruped, m_PoseModifiersJson, m_GroundQuery.get(),
                                      m_bPoseModifiersEnabled);
        }
#else
        (void)world;
#endif
    }
    void AnimationDebugScene::TickTrail(World& world, float deltaTime)
    {
        TickQuadruped(world, deltaTime);
#if defined(NORVES_ENABLE_IMGUI)
        m_PoseModifierView.SubmitDebugDraw();
#endif
        if (!m_Item || !Engine::GEngine)
            return;

        m_SwordTrail.Tick(deltaTime);
        if (std::isfinite(deltaTime) && deltaTime > 0.0f)
            m_TrailTime += static_cast<double>(deltaTime);
        if (auto* item = world.FindEntityByObjectId(m_Item))
        {
            if (m_bFirstTrailSample)
            {
                // 場面の開始位置へ古い軌跡をつながない。
                m_SwordTrail.Reset();
                m_bFirstTrailSample = false;
            }
            m_SwordTrail.AddSample(item->GetRenderWorldTransform(), m_TrailTime);
        }
        else
        {
            m_SwordTrail.Reset();
            m_bFirstTrailSample = true;
        }
        m_TrailScene.Clear();
        m_SwordTrail.AppendDraw(m_TrailScene);
        Engine::GEngine->GetRenderWorld().SetRibbonTrails(m_TrailScene);
    }
    void AnimationDebugScene::Stop()
    {
#if defined(NORVES_ENABLE_IMGUI)
        // 問い合わせを持つ view の借用を、場面の問い合わせより先に解く。
        m_PoseModifierView.Detach();
#endif
        // World は保持しない。場面が先に終了しても、Entity が先に消えても借用を残さない。
        for (const auto& modifier : m_QuadrupedModifiers)
        {
            if (auto* footIk = A::FindFootIKModifier(modifier.get()))
            {
                footIk->SetGroundQuery(nullptr);
            }
        }
        m_QuadrupedModifiers.clear();
        m_GroundQuery.reset();
        m_Quadruped = 0;
        m_QuadrupedTime = 0.0;
        m_QuadrupedHeight = 0;
        m_QuadrupedBank = 0;
        m_QuadrupedGraph.reset();
        m_QuadrupedAsset.reset();
        m_FloorAsset.reset();
        m_PoseModifiersJson.clear();
        m_PoseModifiersPath.clear();
        m_WeightParameterNames.clear();
        m_SwordTrail.Reset();
        m_TrailScene.Clear();
        if (m_Item && Engine::GEngine)
            Engine::GEngine->GetRenderWorld().SetRibbonTrails(m_TrailScene);
        m_TrailTime = 0.0;
        m_bFirstTrailSample = true;
#if defined(NORVES_ENABLE_IMGUI)
        m_AnimatorView.Detach();
        m_SocketView.Detach();
#endif
        m_Character = 0;
        m_Item = 0;
        m_Graph.reset();
        m_ItemAsset.reset();
        m_CharacterAsset.reset();
    }
} // namespace Game::GameModes
