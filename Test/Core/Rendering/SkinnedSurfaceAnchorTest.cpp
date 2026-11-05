// 表面アンカーの位置・速度・探索・無効入力・加速度平滑を実際の姿勢評価経路で検証する。
#include "Animation/SkinnedSurfaceAnchor.h"
#include "Animation/SkeletalPoseBounds.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/GeometryIntersection.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace
{
    namespace Animation = NorvesLib::Core::Animation;
    namespace Container = NorvesLib::Core::Container;
    namespace Skeletal = NorvesLib::Core::Skeletal;
    namespace Component = NorvesLib::Core::Component;
    namespace Math = NorvesLib::Math;
    using NorvesLib::Core::AnimationClipResource;
    using NorvesLib::Core::Entity;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletalAssetResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    using NorvesLib::Core::World;

    constexpr double PositionTolerance = 1.0e-5;
    int g_Failures = 0;

    void Check(bool bOk, const char* contract, int sample, double actual, double limit)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "SkinnedSurfaceAnchorTest 検査失敗: %s 標本=%d 実測=%.17g 限界=%.17g\n",
                         contract, sample, actual, limit);
            ++g_Failures;
        }
    }

    void Require(bool bOk, const char* contract)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "SkinnedSurfaceAnchorTest 準備失敗: %s\n", contract);
            std::abort();
        }
    }

    double Dot(const Math::Vector3& a, const Math::Vector3& b)
    {
        return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
    }

    double Length(const Math::Vector3& v)
    {
        return std::sqrt(Dot(v, v));
    }

    double Distance(const Math::Vector3& a, const Math::Vector3& b)
    {
        return std::sqrt((double(a.x) - b.x) * (double(a.x) - b.x)
            + (double(a.y) - b.y) * (double(a.y) - b.y)
            + (double(a.z) - b.z) * (double(a.z) - b.z));
    }

    Math::Vector3 Unit(const Math::Vector3& v)
    {
        return v * float(1.0 / Length(v));
    }

    void CheckWithin(double value, double limit, const char* contract, int sample)
    {
        Check(std::isfinite(value) && value <= limit, contract, sample, value, limit);
    }

    enum class FixtureKind { Mixed, Rotation, Search, Collapsed, Translation };

    struct Fixture
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        World TestWorld;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Container::VariableArray<Math::Matrix4x4> JointModels;
        uint32_t JointCount;

        explicit Fixture(FixtureKind kind)
            : JointCount(kind == FixtureKind::Search ? 4u
                : (kind == FixtureKind::Rotation || kind == FixtureKind::Translation ? 1u : 3u))
        {
            Require(Registry.Initialize(), "Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("SurfaceAnchorSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("SurfaceAnchorMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("SurfaceAnchorClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("SurfaceAnchorAsset");
            Require(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            const char* names[] = {"Root", "Middle", "Tip", "End"};
            for (uint32_t joint = 0; joint < JointCount; ++joint)
            {
                joints[joint].Name = names[joint];
                joints[joint].ParentIndex = int32_t(joint) - 1;
                rest[joint].Translation = joint == 0 ? Skeletal::SkeletalPosition{}
                    : Skeletal::SkeletalPosition{0.1f, 0.15f, 0.05f};
                const auto bind = Math::MatrixUtils::CreateWorldRowVector(
                    {0.1f * joint, 0.15f * joint, 0.05f * joint},
                    Math::Quaternion::Identity, Math::Vector3::One);
                const auto inverse = Math::MatrixUtils::Inverse(bind);
                for (size_t k = 0; k < 16; ++k)
                {
                    joints[joint].InverseBindMatrix[k] = inverse.values[k];
                }
            }
            Skeleton->SetJoints(std::move(joints));
            Require(Skeleton->SetAuthorRestPose(rest) && Skeleton->Load(), "試験用骨格の読み込み");

            Container::VariableArray<Skeletal::SkeletalVertex> vertices;
            Container::VariableArray<uint32_t> indices;
            const auto addTriangle = [&](Math::Vector3 a, Math::Vector3 b, Math::Vector3 c, uint32_t dominant)
            {
                const Math::Vector3 positions[] = {a, b, c};
                for (uint32_t corner = 0; corner < 3; ++corner)
                {
                    Skeletal::SkeletalVertex vertex;
                    const auto& p = positions[corner];
                    vertex.Position = {p.x, p.y, p.z};
                    vertex.Normal = {0, 0, 1};
                    vertex.JointIndices[0] = dominant;
                    vertex.JointWeights[0] = 1.0f;
                    if (JointCount > 1)
                    {
                        vertex.JointIndices[1] = (dominant + 1) % JointCount;
                        vertex.JointWeights[1] = kind == FixtureKind::Mixed ? 0.2f + corner * 0.1f : 0.2f;
                        vertex.JointWeights[0] = 1.0f - vertex.JointWeights[1];
                    }
                    indices.push_back(uint32_t(vertices.size()));
                    vertices.push_back(vertex);
                }
            };
            if (kind == FixtureKind::Rotation || kind == FixtureKind::Translation)
            {
                addTriangle({1, 0, 0}, {2, 0, 0}, {1, 1, 0}, 0);
            }
            else if (kind == FixtureKind::Collapsed)
            {
                addTriangle({1, 0.2f, 0}, {1, 0.2f, 0}, {1, 0.2f, 0}, 0);
            }
            else
            {
                const int columns = kind == FixtureKind::Search ? 16 : 4;
                const int rows = kind == FixtureKind::Search ? 8 : 4;
                const int layers = kind == FixtureKind::Search ? 2 : 1;
                for (int layer = 0; layer < layers; ++layer)
                {
                    for (int y = 0; y < rows; ++y)
                    {
                        for (int x = 0; x < columns; ++x)
                        {
                            const float x0 = -2.0f + 4.0f * x / columns;
                            const float x1 = -2.0f + 4.0f * (x + 1) / columns;
                            const float y0 = -2.0f + 4.0f * y / rows;
                            const float y1 = -2.0f + 4.0f * (y + 1) / rows;
                            const float z = -0.5f * layer;
                            const uint32_t joint = uint32_t(x) % JointCount;
                            addTriangle({x0, y0, z}, {x1, y0, z}, {x0, y1, z}, joint);
                            addTriangle({x1, y0, z}, {x1, y1, z}, {x0, y1, z}, joint);
                        }
                    }
                }
            }
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(std::move(indices));
            Require(Mesh->Load(), "試験用メッシュの読み込み");

            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "SurfaceAnchorMotion";
            clip.DurationSeconds = kind == FixtureKind::Translation ? 2.0f : 1.0f;
            for (uint32_t joint = 0; joint < JointCount; ++joint)
            {
                Skeletal::SkeletalAnimationChannel channel;
                channel.JointIndex = joint;
                channel.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
                channel.Path = kind == FixtureKind::Rotation || kind == FixtureKind::Mixed
                    ? Skeletal::SkeletalAnimationPath::Rotation : Skeletal::SkeletalAnimationPath::Translation;
                const int keyCount = kind == FixtureKind::Translation ? 33 : 2;
                for (int key = 0; key < keyCount; ++key)
                {
                    const float time = kind == FixtureKind::Translation ? key * 0.0625f : float(key);
                    Skeletal::SkeletalValue value;
                    if (channel.Path == Skeletal::SkeletalAnimationPath::Rotation)
                    {
                        const float angle = kind == FixtureKind::Rotation ? 2.0f * time
                            : (0.2f + 0.15f * joint) * time;
                        const auto q = Math::QuaternionUtils::FromAxisAngle(Math::Vector3::UnitZ, angle);
                        value = {q.x, q.y, q.z, q.w};
                    }
                    else if (kind == FixtureKind::Translation)
                    {
                        // 62.5 ms ごとに速度を +1 / -1 m/s に切り替える線形キー。
                        value = {key % 2 == 0 ? 0.0f : 0.0625f, 0, 0, 0};
                    }
                    else
                    {
                        const auto& t = rest[joint].Translation;
                        value = {t.X, t.Y, t.Z + (0.05f + 0.025f * joint) * time, 0};
                    }
                    channel.Samples.push_back({time, value});
                }
                clip.Channels.push_back(std::move(channel));
            }
            Clip->SetClip(std::move(clip));
            Require(Clip->Load(), "試験用クリップの読み込み");
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            Require(Asset->Load(), "試験用アセットの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            Require(Owner != nullptr, "試験用 Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Require(MeshComponent != nullptr, "試験用コンポーネントの作成");
            Owner->SetLocalPosition(0.7f, -0.4f, 1.1f);
            Owner->SetLocalRotation(Math::QuaternionUtils::FromAxisAngle(Unit({1, 2, 3}), 0.63f));
            TestWorld.UpdateWorldTransforms();
            MeshComponent->SetSkeletalAsset(Asset);
            MeshComponent->SetPlaying(false);
            MeshComponent->SetLooping(false);
            JointModels.resize(JointCount);
        }

        ~Fixture()
        {
            TestWorld.Finalize();
        }

        Animation::SurfaceAnchorDesc Desc(uint32_t triangle = 0) const
        {
            Animation::SurfaceAnchorDesc desc;
            desc.TriangleIndex = triangle;
            desc.U = 0.2f;
            desc.V = 0.3f;
            desc.MeshRevision = Mesh->GetPoseRevision();
            return desc;
        }

        Animation::SurfaceAnchorPoseView Pose(float time)
        {
            MeshComponent->SetAnimationTimeSeconds(time);
            Require(MeshComponent->EvaluatePose(), "姿勢の評価");
            Animation::SurfaceAnchorPoseView pose;
            Require(MeshComponent->TryGetBonePalette(pose.Palette), "パレットの取得");
            Require(pose.Palette.size() == JointCount, "パレットの関節数");
            for (uint32_t joint = 0; joint < JointCount; ++joint)
            {
                Require(MeshComponent->TryGetJointModelMatrix(joint, JointModels[joint]), "関節モデル行列の取得");
            }
            pose.JointModelMatrices = {JointModels.data(), JointModels.size()};
            const auto world = Owner->GetWorldTransform();
            pose.OwnerWorld = Math::MatrixUtils::CreateWorldRowVector(world.position, world.rotation, world.scale);
            return pose;
        }

        Math::Vector3 WorldVertex(uint32_t index, const Animation::SurfaceAnchorPoseView& pose) const
        {
            return Math::MatrixUtils::TransformPointRowVector(pose.OwnerWorld,
                Animation::SkinPosition(Mesh->GetVertices()[index], pose.Palette));
        }

        void Triangle(uint32_t triangle, const Animation::SurfaceAnchorPoseView& pose, Math::Vector3 (&p)[3]) const
        {
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                p[corner] = WorldVertex(Mesh->GetIndices()[triangle * 3 + corner], pose);
            }
        }
    };

    void TestPosition()
    {
        Fixture fixture(FixtureKind::Mixed);
        Math::Matrix4x4 sentinel = Math::Matrix4x4::Identity;
        sentinel.SetTranslationRow({123.0f, 0.0f, 0.0f});
        Container::Span<const Math::Matrix4x4> palette(&sentinel, 1);
        const bool bReadBeforeEvaluation = fixture.MeshComponent->TryGetBonePalette(palette);
        Check(!bReadBeforeEvaluation && palette.data() == &sentinel && palette.size() == 1
            && sentinel.GetTranslationRow().x == 123.0f, "未評価では false で出力を保持", 0, bReadBeforeEvaluation, 0);
        double maxPosition = 0, maxUnit = 0, maxOrthogonality = 0;
        const float times[] = {0.0f, 0.5f, 1.0f};
        for (int sample = 0; sample < 3; ++sample)
        {
            const auto pose = fixture.Pose(times[sample]);
            for (uint32_t triangle = 0; triangle < fixture.Mesh->GetIndices().size() / 3; ++triangle)
            {
                const auto desc = fixture.Desc(triangle);
                const auto frame = Animation::EvaluateSurfaceAnchor(desc, *fixture.Mesh, pose);
                Math::Vector3 p[3];
                fixture.Triangle(triangle, pose, p);
                const auto expected = p[0] + (p[1] - p[0]) * desc.U + (p[2] - p[0]) * desc.V;
                const double error = Distance(frame.Position, expected);
                const double unitError = std::fmax(std::abs(Length(frame.Normal) - 1.0),
                    std::abs(Length(frame.Tangent) - 1.0));
                const double orthogonal = std::fmax(std::abs(Dot(frame.Normal, frame.Tangent)),
                    std::fmax(std::abs(Dot(frame.Normal, Unit(p[1] - p[0]))),
                              std::abs(Dot(frame.Normal, Unit(p[2] - p[0])))));
                Check(frame.bValid && !frame.bFromFallbackJoint, "通常の表面フレーム", sample, frame.bValid, 1);
                CheckWithin(error, PositionTolerance, "スキニングした三頂点の重心位置", sample);
                CheckWithin(unitError, PositionTolerance, "法線と接線の単位長", sample);
                CheckWithin(orthogonal, PositionTolerance, "法線と辺・接線の直交", sample);
                maxPosition = std::fmax(maxPosition, error);
                maxUnit = std::fmax(maxUnit, unitError);
                maxOrthogonality = std::fmax(maxOrthogonality, orthogonal);
            }
        }
        std::printf("SkinnedSurfaceAnchorTest 位置: 最大位置誤差=%.9g 最大単位長誤差=%.9g 最大直交誤差=%.9g 許容=1e-5\n",
                    maxPosition, maxUnit, maxOrthogonality);
    }

    void TestRotationVelocity()
    {
        Fixture fixture(FixtureKind::Rotation);
        Animation::SurfaceAnchorMotion motion;
        Animation::SurfaceAnchorMotionSettings settings;
        settings.SmoothingTimeConstantSeconds = 0.0f;
        constexpr float dt = 1.0f / 240.0f;
        // 中点の解析値に対する弦差分の打切り誤差は 1-sinc(ωdt/2) ≈ 2.9e-6。
        // float の姿勢・位置差分の丸めを含めても 1 % 以内を要求する。
        constexpr double relativeTolerance = 0.01;
        double maxRelativeError = 0;
        for (int sample = 0; sample <= 192; ++sample)
        {
            const float time = 0.1f + sample * dt;
            const auto pose = fixture.Pose(time);
            const auto frame = Animation::EvaluateSurfaceAnchor(fixture.Desc(), *fixture.Mesh, pose);
            Require(frame.bValid, "回転するアンカーの評価");
            const auto result = motion.Update(frame, dt, settings);
            Check(!result.bDiscontinuity, "一定角速度は連続", sample, result.bDiscontinuity, 0);
            if (sample == 0)
            {
                Check(!result.bHasVelocity, "初回に速度を捏造しない", sample, result.bHasVelocity, 0);
                continue;
            }
            const double middleTime = 0.5 * (double(0.1f + (sample - 1) * dt) + time);
            const double angle = 2.0 * middleTime;
            // 重心の静止位置 (1.2, 0.3, 0) を解析的に回す。回転中心は原点。
            const Math::Vector3 r(float(1.2 * std::cos(angle) - 0.3 * std::sin(angle)),
                                  float(1.2 * std::sin(angle) + 0.3 * std::cos(angle)), 0);
            const auto expected = Math::MatrixUtils::TransformVectorRowVector(pose.OwnerWorld, {-2.0f * r.y, 2.0f * r.x, 0});
            const double error = Distance(result.Velocity, expected) / Length(expected);
            Check(result.bHasVelocity, "二フレーム目から速度がある", sample, result.bHasVelocity, 1);
            CheckWithin(error, relativeTolerance, "ω×r と生の差分速度", sample);
            maxRelativeError = std::fmax(maxRelativeError, error);
        }
        std::printf("SkinnedSurfaceAnchorTest 回転速度: 最大相対誤差=%.9g 許容=0.01 dt=%.9g omega=2\n",
                    maxRelativeError, double(dt));
    }

    struct Random
    {
        uint32_t State = 0x51a7c3d9u;
        float Signed()
        {
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            return float(State >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }
    };

    void TestSearch()
    {
        Fixture fixture(FixtureKind::Search);
        const auto pose = fixture.Pose(0.5f);
        Animation::JointTriangleBuckets buckets;
        Require(buckets.Build(*fixture.Mesh, fixture.JointCount), "関節バケットの構築");
        const uint32_t joints[] = {0, 1, 2, 3};
        const uint32_t triangleCount = uint32_t(fixture.Mesh->GetIndices().size() / 3);
        Random random;
        int hits = 0, misses = 0;
        double maxDistanceError = 0, maxBarycentricError = 0;
        Animation::StabQuery query;
        query.MaxDistance = 12.0f;
        query.CandidateJoints = joints;
        for (int sample = 0; sample < 1000; ++sample)
        {
            const Math::Vector3 localOrigin(random.Signed() * 4, random.Signed() * 4, 4);
            const Math::Vector3 localTarget(random.Signed() * 2.8f, random.Signed() * 2.8f, 0);
            const auto origin = Math::MatrixUtils::TransformPointRowVector(pose.OwnerWorld, localOrigin);
            const auto target = Math::MatrixUtils::TransformPointRowVector(pose.OwnerWorld, localTarget);
            query.Ray = Math::Ray(origin, Unit(target - origin));
            const auto result = Animation::FindSurfacePoint(query, buckets, *fixture.Mesh, pose);
            bool bBruteHit = false;
            uint32_t bestTriangle = 0;
            float bestDistance = query.MaxDistance, bestU = 0, bestV = 0;
            for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
            {
                Math::Vector3 p[3];
                fixture.Triangle(triangle, pose, p);
                float t, u, v;
                if (Math::RayIntersectsTriangleBarycentric(query.Ray, p[0], p[1], p[2], t, u, v)
                    && t <= query.MaxDistance && (!bBruteHit || t < bestDistance
                        || (t == bestDistance && triangle < bestTriangle)))
                {
                    bBruteHit = true;
                    bestTriangle = triangle;
                    bestDistance = t;
                    bestU = u;
                    bestV = v;
                }
            }
            Check(result.TrianglesTested == triangleCount, "全関節は全三角形を試す", sample, result.TrianglesTested, triangleCount);
            if (bBruteHit)
            {
                ++hits;
                const double distanceError = std::abs(double(result.Distance) - bestDistance);
                const double barycentricError = std::fmax(std::abs(double(result.U) - bestU), std::abs(double(result.V) - bestV));
                Check(result.Kind == Animation::StabResultKind::Hit, "総当たりの交差と Hit が一致", sample, int(result.Kind), int(Animation::StabResultKind::Hit));
                Check(result.TriangleIndex == bestTriangle, "最近交差の三角形が一致", sample, result.TriangleIndex, bestTriangle);
                CheckWithin(distanceError, PositionTolerance, "最近交差の距離が一致", sample);
                CheckWithin(barycentricError, PositionTolerance, "最近交差の重心座標が一致", sample);
                maxDistanceError = std::fmax(maxDistanceError, distanceError);
                maxBarycentricError = std::fmax(maxBarycentricError, barycentricError);
            }
            else
            {
                ++misses;
                Check(result.Kind == Animation::StabResultKind::Miss, "総当たりが外れたレイは Miss", sample, int(result.Kind), int(Animation::StabResultKind::Miss));
            }
        }
        Check(hits >= 300, "レイ試験の十分な交差数", 0, hits, 300);
        Check(misses > 0, "外れたレイも実行する", 0, misses, 1);
        query.CandidateJoints = {joints, 1};
        const auto restricted = Animation::FindSurfacePoint(query, buckets, *fixture.Mesh, pose);
        const auto bucketSize = buckets.GetTriangles(0).size();
        Check(restricted.TrianglesTested == bucketSize && bucketSize > 0 && bucketSize < triangleCount,
            "一関節はそのバケットだけを試す", 0, restricted.TrianglesTested, double(bucketSize));
        std::printf("SkinnedSurfaceAnchorTest 探索: Hit=%d/1000 Miss=%d 最大距離誤差=%.9g 最大重心誤差=%.9g TrianglesTested=%u/%u bucket=%zu\n",
                    hits, misses, maxDistanceError, maxBarycentricError, restricted.TrianglesTested, triangleCount, bucketSize);

        query.CandidateJoints = joints;
        double maxSnapReturnError = 0, maxSnapDistanceError = 0;
        int snaps = 0;
        // x=-2 の境界の少し外を通す。全関節の変位は Z だけなので横の隙間は既知。
        for (int sample = 0; sample < 3; ++sample)
        {
            const float gap = 0.01f + sample * 0.01f;
            const auto localOrigin = Math::Vector3(-2.0f - gap, -1.2f + sample * 0.9f, 2.0f);
            query.Ray = Math::Ray(Math::MatrixUtils::TransformPointRowVector(pose.OwnerWorld, localOrigin),
                Unit(Math::MatrixUtils::TransformVectorRowVector(pose.OwnerWorld, {0, 0, -1})));
            query.MaxDistance = 4.0f;
            query.MaxSnapDistance = gap * 1.5f;
            const auto snapped = Animation::FindSurfacePoint(query, buckets, *fixture.Mesh, pose);
            Check(snapped.Kind == Animation::StabResultKind::Snapped, "隙間が上限内なら Snapped", sample, int(snapped.Kind), int(Animation::StabResultKind::Snapped));
            auto desc = fixture.Desc(snapped.TriangleIndex);
            desc.MeshRevision = snapped.MeshRevision;
            desc.U = snapped.U;
            desc.V = snapped.V;
            const auto frame = Animation::EvaluateSurfaceAnchor(desc, *fixture.Mesh, pose);
            const double returnError = Distance(frame.Position, snapped.WorldPosition);
            const double distanceError = std::abs(double(snapped.Distance) - gap);
            Check(frame.bValid && !frame.bFromFallbackJoint, "スナップを通常アンカーに戻せる", sample, frame.bValid, 1);
            CheckWithin(returnError, PositionTolerance, "スナップ位置の再評価", sample);
            CheckWithin(distanceError, PositionTolerance, "スナップの隙間は幾何学的な距離", sample);
            query.MaxSnapDistance = gap * 0.5f;
            const auto outside = Animation::FindSurfacePoint(query, buckets, *fixture.Mesh, pose);
            Check(outside.Kind == Animation::StabResultKind::Miss && outside.Reason == Animation::StabMissReason::BeyondSnapDistance,
                "隙間が上限外なら BeyondSnapDistance", sample, int(outside.Reason), int(Animation::StabMissReason::BeyondSnapDistance));
            snaps += snapped.Kind == Animation::StabResultKind::Snapped ? 1 : 0;
            maxSnapReturnError = std::fmax(maxSnapReturnError, returnError);
            maxSnapDistanceError = std::fmax(maxSnapDistanceError, distanceError);
        }
        std::printf("SkinnedSurfaceAnchorTest スナップ: Snapped=%d/3 最大再評価誤差=%.9g 最大隙間誤差=%.9g 許容=1e-5\n",
                    snaps, maxSnapReturnError, maxSnapDistanceError);
    }

    void TestInvalid()
    {
        Fixture fixture(FixtureKind::Mixed);
        auto pose = fixture.Pose(0.5f);
        auto desc = fixture.Desc();
        ++desc.MeshRevision;
        const bool bRevisionInvalid = !Animation::EvaluateSurfaceAnchor(desc, *fixture.Mesh, pose).bValid;
        Check(bRevisionInvalid, "異なるリビジョンのアンカーは無効", 0, bRevisionInvalid, 1);
        desc = fixture.Desc();
        desc.TriangleIndex = uint32_t(fixture.Mesh->GetIndices().size() / 3);
        const bool bIndexInvalid = !Animation::EvaluateSurfaceAnchor(desc, *fixture.Mesh, pose).bValid;
        Check(bIndexInvalid, "範囲外の三角形は無効", 0, bIndexInvalid, 1);
        Container::VariableArray<Math::Matrix4x4> badPalette(pose.Palette.size());
        for (size_t i = 0; i < badPalette.size(); ++i)
        {
            badPalette[i] = pose.Palette[i];
        }
        const uint32_t usedJoint = fixture.Mesh->GetVertices()[fixture.Mesh->GetIndices()[0]].JointIndices[0];
        badPalette[usedJoint] = Math::MatrixUtils::CreateWorldRowVector(
            {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            Math::Quaternion::Identity, Math::Vector3::One);
        auto badPose = pose;
        badPose.Palette = {badPalette.data(), badPalette.size()};
        const bool bNanInvalid = !Animation::EvaluateSurfaceAnchor(fixture.Desc(), *fixture.Mesh, badPose).bValid;
        Check(bNanInvalid, "使用中のパレット要素の NaN は無効", 0, bNanInvalid, 1);

        Fixture collapsed(FixtureKind::Collapsed);
        const auto collapsedPose = collapsed.Pose(0.5f);
        auto fallbackDesc = collapsed.Desc();
        fallbackDesc.FallbackJoint = 2;
        fallbackDesc.FallbackLocalOffset = {0.2f, -0.1f, 0.3f};
        const auto fallback = Animation::EvaluateSurfaceAnchor(fallbackDesc, *collapsed.Mesh, collapsedPose);
        const auto expected = Math::MatrixUtils::TransformPointRowVector(
            collapsedPose.JointModelMatrices[fallbackDesc.FallbackJoint] * collapsedPose.OwnerWorld,
            fallbackDesc.FallbackLocalOffset);
        const double fallbackError = Distance(fallback.Position, expected);
        Check(fallback.bValid && fallback.bFromFallbackJoint, "潰れた三角形は関節へ戻る", 0, fallback.bFromFallbackJoint, 1);
        CheckWithin(fallbackError, PositionTolerance, "フォールバックの関節局所オフセット", 0);

        Animation::JointTriangleBuckets staleBuckets;
        Require(staleBuckets.Build(*fixture.Mesh, fixture.JointCount), "古いバケットの準備");
        auto vertices = fixture.Mesh->GetVertices();
        fixture.Mesh->SetVertices(std::move(vertices));
        Require(!staleBuckets.IsBuiltFor(*fixture.Mesh), "頂点の設定でリビジョンが進む");
        const uint32_t joints[] = {0, 1, 2};
        Animation::StabQuery query;
        query.Ray = Math::Ray({0, 0, 10}, {0, 0, -1});
        query.MaxDistance = 20;
        query.CandidateJoints = joints;
        const auto stale = Animation::FindSurfacePoint(query, staleBuckets, *fixture.Mesh, pose);
        const bool bBucketInvalid = stale.Kind == Animation::StabResultKind::Miss
            && stale.Reason == Animation::StabMissReason::InvalidInput;
        Check(bBucketInvalid, "古いバケットは Miss(InvalidInput)", 0, int(stale.Reason), int(Animation::StabMissReason::InvalidInput));
        std::printf("SkinnedSurfaceAnchorTest 無効入力: revision=%d NaN=%d 範囲外=%d 古いバケット=%d fallback=%d 最大位置誤差=%.9g\n",
                    bRevisionInvalid, bNanInvalid, bIndexInvalid, bBucketInvalid, fallback.bFromFallbackJoint, fallbackError);
    }

    void TestSmoothedAcceleration()
    {
        Fixture fixture(FixtureKind::Translation);
        Animation::SurfaceAnchorMotion motions[2];
        Animation::SurfaceAnchorMotionSettings settings[2];
        settings[1].SmoothingTimeConstantSeconds = 0.0f;
        for (auto& setting : settings)
        {
            // 通常の速度は 1 m/s、速度反転時の生加速度は最大 120 m/s²。
            // どちらも通し、10 m の瞬間移動による 600 m/s は速度上限で検出する。
            setting.MaxSpeed = 50.0f;
            setting.MaxAcceleration = 1000.0f;
            setting.MaxDeltaTime = 0.1f;
        }
        constexpr float dt = 1.0f / 60.0f;
        double peaks[2] = {}, sums[2] = {};
        int counts[2] = {}, discontinuities = 0;
        Animation::SurfaceAnchorFrame lastFrame;
        for (int sample = 0; sample <= 120; ++sample)
        {
            const auto pose = fixture.Pose(sample * dt);
            lastFrame = Animation::EvaluateSurfaceAnchor(fixture.Desc(), *fixture.Mesh, pose);
            Require(lastFrame.bValid, "並進するアンカーの評価");
            for (int mode = 0; mode < 2; ++mode)
            {
                const auto result = motions[mode].Update(lastFrame, dt, settings[mode]);
                discontinuities += result.bDiscontinuity ? 1 : 0;
                Check(!result.bDiscontinuity, "通常のキー補間は不連続にしない", sample, result.bDiscontinuity, 0);
                Check(result.bHasAcceleration == (sample >= 2), "三フレーム目から加速度がある", sample, result.bHasAcceleration, sample >= 2);
                if (result.bHasAcceleration)
                {
                    const double magnitude = Length(result.Acceleration);
                    Check(std::isfinite(magnitude), "加速度は有限", sample, magnitude, 0);
                    peaks[mode] = std::fmax(peaks[mode], magnitude);
                    sums[mode] += magnitude;
                    ++counts[mode];
                }
            }
        }
        Require(counts[0] > 0 && counts[1] > 0 && sums[0] > 0 && sums[1] > 0, "加速度の測定が空でない");
        const double smoothRatio = peaks[0] / (sums[0] / counts[0]);
        const double rawRatio = peaks[1] / (sums[1] / counts[1]);
        // 62.5 ms の速度反転を 60 Hz で観測すると生のピーク/平均は約 3.8。
        // τ=80 ms の平滑で約 2.1 になるため、両者を余裕を持って分ける 2.5 を境界にする。
        constexpr double ratioLimit = 2.5;
        CheckWithin(smoothRatio, ratioLimit, "平滑で加速度の尖りを抑える", 0);
        Check(std::isfinite(rawRatio) && rawRatio > ratioLimit, "平滑なしの尖りで試験の感度を確かめる", 0, rawRatio, ratioLimit);
        int resets = 0, cleared = 0;
        lastFrame.Position.x += 10.0f;
        for (int mode = 0; mode < 2; ++mode)
        {
            const auto teleport = motions[mode].Update(lastFrame, dt, settings[mode]);
            Check(teleport.bDiscontinuity && !teleport.bHasVelocity && !teleport.bHasAcceleration,
                "瞬間移動で履歴を消す", mode, teleport.bDiscontinuity, 1);
            const auto next = motions[mode].Update(lastFrame, dt, settings[mode]);
            const bool bCleared = !next.bDiscontinuity && next.bHasVelocity && !next.bHasAcceleration
                && Length(next.Velocity) == 0.0 && Length(next.SmoothedVelocity) == 0.0 && Length(next.Acceleration) == 0.0;
            Check(bCleared, "瞬間移動の次は速度を新しく開始し加速度履歴はない", mode, bCleared, 1);
            resets += teleport.bDiscontinuity ? 1 : 0;
            cleared += bCleared ? 1 : 0;
        }
        std::printf("SkinnedSurfaceAnchorTest 加速度平滑: 平滑ピーク/平均=%.9g 生ピーク/平均=%.9g 閾値=2.5 平滑ピーク=%.9g 平滑平均=%.9g 生ピーク=%.9g 生平均=%.9g 通常不連続=%d 瞬間移動検出=%d/2 履歴消去=%d/2\n",
                    smoothRatio, rawRatio, peaks[0], sums[0] / counts[0], peaks[1], sums[1] / counts[1], discontinuities, resets, cleared);
    }
} // namespace

void TestSkinnedSurfaceAnchor()
{
    g_Failures = 0;
    TestPosition();
    TestRotationVelocity();
    TestSearch();
    TestInvalid();
    TestSmoothedAcceleration();
    if (g_Failures != 0)
    {
        std::fprintf(stderr, "SkinnedSurfaceAnchorTest 失敗件数=%d\n", g_Failures);
        std::fflush(stdout);
        std::abort();
    }
    std::printf("SkinnedSurfaceAnchorTest 合格\n");
}
