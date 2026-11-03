// 三人称カメラの時間に沿った振る舞いと設定の解析を、独立したWorldと物理照会で検証する。
#include "Gameplay/Camera/FollowCameraComponent.h"
#include "Animation/SkeletalAssetResource.h"
#include "Asset/AssetFileReader.h"
#include "Component/CameraComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Game/Input/GameInputActions.h"
#include "Input/InputRouter.h"
#include "Input/InputSystem.h"
#include "Math/CriticalDamping.h"
#include "Math/MathTypes.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>

namespace Game::Gameplay
{
    class FollowCameraTestAccess
    {
      public:
        static FollowCameraProfile& Profile(FollowCameraComponent& camera) { return camera.m_Profile; }
        static void DisableProfileReload(FollowCameraComponent& camera) { camera.m_ProfileReload = {}; }
        static void SetSubjectSpeed(FollowCameraComponent& camera, float speed) { camera.m_SubjectSpeedOverride = speed; }
        static void SetSubjectHandles(FollowCameraComponent& camera,
                                      NorvesLib::Core::Scene::ColliderHandle collider,
                                      NorvesLib::Core::Scene::BodyHandle body)
        {
            camera.m_SubjectColliderOverride = collider;
            camera.m_SubjectBodyOverride = body;
        }
        // 視点の入力の直後と同じ、待機時間とヨー速度をリセットした状態にする。
        static void MarkLookInput(FollowCameraComponent& camera)
        {
            camera.m_TimeSinceLook = 0;
            camera.m_YawVelocity = 0;
        }
        static float Trauma(const FollowCameraComponent& camera) { return camera.m_Trauma; }
        static uint64_t LockOnId(const FollowCameraComponent& camera)
        {
            auto t = camera.m_LockOn.lock();
            return t ? t->GetEntityId() : 0;
        }
    };
} // namespace Game::Gameplay

namespace
{
    namespace Core = NorvesLib::Core;
    namespace Math = NorvesLib::Math;
    using namespace Core::Scene;
    using namespace Game::Gameplay;
    using Access = FollowCameraTestAccess;
    constexpr float FrameDelta = 1.f / 60;

#define CAMERA_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "FollowCameraBehaviorTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    void CheckNear(int line, const char* expression, float actual, float expected, float tolerance = 1e-4f)
    {
        if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance)
        {
            std::fprintf(stderr, "FollowCameraBehaviorTest %d: %s 実測=%g 期待=%g 許容=%g\n",
                         line, expression, actual, expected, tolerance);
            std::abort();
        }
    }
#define CAMERA_NEAR(...) CheckNear(__LINE__, #__VA_ARGS__, __VA_ARGS__)
    float AngleDelta(float from, float to) { return std::remainder(to - from, 360.f); }
    void NearPose(const Math::Transform& actual, const Math::Transform& expected)
    {
        CAMERA_NEAR((actual.position - expected.position).Length(), 0);
        CAMERA_NEAR((actual.scale - expected.scale).Length(), 0);
        // 符号の異なる同じ回転も、三つの基底軸で比較する。
        for (const auto axis : {Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ})
            CAMERA_NEAR((actual.rotation * axis - expected.rotation * axis).Length(), 0);
    }

    struct QueryObstacle
    {
        PhysicsQueryHit Hit;
        PhysicsCollisionMask Layer = DefaultPhysicsLayer;
        bool bTrigger = false;
        bool bRayOnly = false;
    };
    class Provider final : public IPhysicsSceneQueryProvider
    {
      public:
        // 空ならNoHit。距離・識別子・レイヤーを持つ候補から、フィルタを通る最短の当たりを返す。
        Core::Container::VariableArray<QueryObstacle> Obstacles;
        mutable PhysicsQueryDesc Last;
        mutable unsigned QueryCount = 0;
        mutable unsigned RayQueryCount = 0, SweepQueryCount = 0;
        EPhysicsSceneQueryResult ExecuteQuery(const PhysicsQueryDesc& query,
                                              Core::Container::VariableArray<PhysicsQueryHit>& out) const override
        {
            Last = query;
            ++QueryCount;
            const bool bRay = query.Kind == EPhysicsQueryKind::RaycastClosest;
            if (bRay)
            {
                ++RayQueryCount;
            }
            if (query.Kind == EPhysicsQueryKind::SweepSphere)
            {
                ++SweepQueryCount;
            }
            out.clear();
            const QueryObstacle* closest = nullptr;
            for (const auto& obstacle : Obstacles)
                if ((!obstacle.bRayOnly || bRay) &&
                    query.Filter.Accepts(obstacle.Layer, obstacle.bTrigger, obstacle.Hit.Collider, obstacle.Hit.Body) &&
                    obstacle.Hit.Distance >= 0 && obstacle.Hit.Distance <= query.MaxDistance &&
                    (!closest || obstacle.Hit.Distance < closest->Hit.Distance))
                    closest = &obstacle;
            if (!closest || query.MaxHits == 0)
                return EPhysicsSceneQueryResult::NoHit;
            out.push_back(closest->Hit);
            return EPhysicsSceneQueryResult::Success;
        }
        void SetHitDistance(float distance)
        {
            Obstacles.clear();
            QueryObstacle obstacle;
            obstacle.Hit.Distance = distance;
            obstacle.Hit.Collider = {1, 1};
            obstacle.Hit.Body = {1, 1};
            Obstacles.push_back(obstacle);
        }
        void SetRayHitDistance(float distance)
        {
            SetHitDistance(distance);
            Obstacles[0].bRayOnly = true;
        }
        EPhysicsSceneQueryResult Raycast(const Math::Ray&, float, PhysicsRaycastHit& out) const override
        { out = {}; return EPhysicsSceneQueryResult::NoHit; }
        EPhysicsSceneQueryResult OverlapSphere(const Math::Sphere&,
            Core::Container::VariableArray<PhysicsOverlapHit>& out) const override
        { out.clear(); return EPhysicsSceneQueryResult::NoHit; }
        EPhysicsSceneQueryResult OverlapBox(const Math::OBB&,
            Core::Container::VariableArray<PhysicsOverlapHit>& out) const override
        { out.clear(); return EPhysicsSceneQueryResult::NoHit; }
        EPhysicsSceneQueryResult OverlapCapsule(const Math::Capsule&,
            Core::Container::VariableArray<PhysicsOverlapHit>& out) const override
        { out.clear(); return EPhysicsSceneQueryResult::NoHit; }
        EPhysicsSceneQueryResult IsAlive(ColliderHandle handle, bool& alive) const override
        { alive = handle.IsValid(); return EPhysicsSceneQueryResult::Success; }
        EPhysicsSceneQueryResult IsAlive(BodyHandle handle, bool& alive) const override
        { alive = handle.IsValid(); return EPhysicsSceneQueryResult::Success; }
        EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t& sequence) const override
        { sequence = 1; return EPhysicsSceneQueryResult::Success; }
    };

    struct CameraFixture
    {
        Core::World World;
        Provider Physics;
        SceneQuery Query;
        Core::Entity *Physical = nullptr, *Visual = nullptr, *Owner = nullptr, *ViewSource = nullptr;
        FollowCameraComponent* Follow = nullptr;
        Core::Component::CameraComponent* Camera = nullptr;
        CameraFixture()
        {
            World.Initialize();
            Physical = World.SpawnEntity<Core::Entity>();
            CAMERA_CHECK(Physical);
            Visual = World.SpawnEntity<Core::Entity>(Physical);
            Owner = World.SpawnEntity<Core::Entity>();
            ViewSource = World.SpawnEntity<Core::Entity>();
            CAMERA_CHECK(Visual && Owner && ViewSource);
            Follow = World.CreateComponent<FollowCameraComponent>(Owner);
            Camera = World.CreateComponent<Core::Component::CameraComponent>(Owner);
            CAMERA_CHECK(Follow && Camera);
            CAMERA_CHECK(Query.BindPhysicsProvider(Physics) == EPhysicsSceneQueryResult::Success);
            CAMERA_CHECK(Follow->SetSubject(Visual, Physical));
            CAMERA_CHECK(Follow->SetViewSource(ViewSource));
            Follow->BindSceneQuery(&Query);
            Follow->SetPitch(0);
            Follow->SetYaw(0);
            Follow->SetArmLength(5);
            Follow->SetTargetOffset({0, .4f, 0});
            Visual->SetRenderInterpolationEnabled(true);
            // 値の差し込みをファイルの再読込で置き換えない。
            Access::DisableProfileReload(*Follow);
            Access::SetSubjectSpeed(*Follow, 0);
        }
        ~CameraFixture()
        {
            Follow->BindSceneQuery(nullptr);
            CAMERA_CHECK(Query.UnbindPhysicsProvider(Physics) == EPhysicsSceneQueryResult::Success);
            World.Finalize();
        }
        // 固定更新は呼び手が別に進める。描画用変換を確定し、Input群→Camera群を一度ずつ進める。
        void Step(float dt = FrameDelta)
        {
            World.UpdateRenderTransforms();
            Follow->TickInputGroup(dt);
            Follow->TickCameraGroup(dt);
            World.UpdateWorldTransforms();
            CAMERA_CHECK(Follow->GetLastCollisionResult() == Core::Camera::CameraCollisionResult::Success);
        }
        void Advance(unsigned frames, float dt = FrameDelta)
        {
            for (unsigned frame = 0; frame < frames; ++frame) Step(dt);
        }
        void SetSubjectYaw(float degrees)
        {
            Physical->SetRotation(Math::Quaternion(Math::Vector3::UnitY, degrees * Math::Constants::PI / 180));
        }
    };

    // ヨーの戻しは±180度と0/360度の境界を、どちらの向きでも最短角で横切る。
    void TestShortestYaw()
    {
        struct YawCase { float Initial, Subject, ExpectedDelta; };
        const YawCase cases[] = {{170, 10, 20}, {350, -170, 20}, {10, 170, -20}};
        for (const auto& value : cases)
        {
            CameraFixture fixture;
            auto& camera = *fixture.Follow;
            fixture.SetSubjectYaw(value.Subject);
            camera.SetYaw(value.Initial);
            auto& profile = Access::Profile(camera);
            profile.YawReturnDelay = 0;
            Access::SetSubjectSpeed(camera, profile.MinimumYawSpeed);
            float total = 0, signedTotal = 0;
            for (unsigned frame = 0; frame < 240; ++frame)
            {
                const float previous = camera.GetYaw();
                fixture.Step();
                const float delta = AngleDelta(previous, camera.GetYaw());
                CAMERA_CHECK(delta * value.ExpectedDelta >= -1e-3f);
                total += std::fabs(delta);
                signedTotal += delta;
                CAMERA_CHECK(total < 180);
            }
            CAMERA_NEAR(signedTotal, value.ExpectedDelta, .01f);
            CAMERA_NEAR(AngleDelta(camera.GetYaw(), value.Initial + value.ExpectedDelta), 0, .01f);
            std::printf("最短角: 開始=%g度 累計=%g度 符号付き=%g度 終端=%g度\n",
                        value.Initial, total, signedTotal, camera.GetYaw());
        }
    }

    // 入力後の待機時間・最低速度・ロックオンがヨーの戻しを抑制する。
    void TestYawReturn()
    {
        CameraFixture delayed;
        auto& camera = *delayed.Follow;
        auto& profile = Access::Profile(camera);
        profile.YawReturnDelay = .5f;
        Access::SetSubjectSpeed(camera, profile.MinimumYawSpeed);
        Access::MarkLookInput(camera);
        delayed.Advance(4, .1f);
        CAMERA_NEAR(camera.GetYaw(), 0);
        const float before = std::fabs(AngleDelta(camera.GetYaw(), 180));
        delayed.Advance(120);
        const float after = std::fabs(AngleDelta(camera.GetYaw(), 180));
        CAMERA_CHECK(after < before * .05f);
        std::printf("ヨーの戻し: 待機0.4秒で0度、戻し後の角度誤差=%g度\n", after);

        CameraFixture slow;
        auto& slowProfile = Access::Profile(*slow.Follow);
        Access::SetSubjectSpeed(*slow.Follow, slowProfile.MinimumYawSpeed * .5f);
        slow.Advance(180);
        CAMERA_NEAR(slow.Follow->GetYaw(), 0);

        CameraFixture locked;
        auto* target = locked.World.SpawnEntity<Core::Entity>();
        CAMERA_CHECK(target);
        target->SetPosition({5, 0, 0});
        CAMERA_CHECK(locked.Follow->SetLockOnTarget(target));
        Access::SetSubjectSpeed(*locked.Follow, Access::Profile(*locked.Follow).MinimumYawSpeed);
        // カメラ群を進める前にも、ロックオン中のInput群だけでヨーが戻らないことを確かめる。
        locked.Follow->TickInputGroup(Access::Profile(*locked.Follow).YawReturnDelay + .1f);
        CAMERA_NEAR(locked.Follow->GetYaw(), 0);
        locked.Advance(240);
        CAMERA_NEAR(AngleDelta(locked.Follow->GetYaw(), 270), 0, .05f);
        CAMERA_CHECK(std::fabs(AngleDelta(locked.Follow->GetYaw(), 180)) > 80);
        std::printf("ヨーの抑制: 低速=%g度 ロックオン=%g度\n", slow.Follow->GetYaw(), locked.Follow->GetYaw());
    }

    // Traumaは1に制限されて指定時間で消え、消えた後は揺れなしの姿勢に一致する。
    void TestTrauma()
    {
        CameraFixture shaken, calm;
        auto& camera = *shaken.Follow;
        camera.AddTrauma(1);
        for (unsigned count = 0; count < 10; ++count) camera.AddTrauma(.7f);
        CAMERA_NEAR(Access::Trauma(camera), 1);
        bool observedShake = false;
        float elapsed = 0;
        const float duration = 1 / Access::Profile(camera).TraumaDecay;
        const unsigned limit = static_cast<unsigned>(std::ceil(duration / FrameDelta)) + 1;
        for (unsigned frame = 0; frame < limit && Access::Trauma(camera) > 0; ++frame)
        {
            shaken.Step();
            calm.Step();
            elapsed += FrameDelta;
            const auto axis = Math::Vector3::UnitZ;
            observedShake |= (shaken.Owner->GetWorldTransform().rotation * axis -
                              calm.Owner->GetWorldTransform().rotation * axis).Length() > 1e-5f;
            CAMERA_CHECK(Access::Trauma(camera) >= 0 && Access::Trauma(camera) <= 1);
        }
        CAMERA_CHECK(observedShake);
        CAMERA_NEAR(Access::Trauma(camera), 0);
        CAMERA_CHECK(elapsed >= duration - 1e-4f && elapsed <= duration + FrameDelta + 1e-4f);
        NearPose(shaken.Owner->GetWorldTransform(), calm.Owner->GetWorldTransform());
        for (unsigned frame = 0; frame < 30; ++frame)
        {
            shaken.Step();
            calm.Step();
            NearPose(shaken.Owner->GetWorldTransform(), calm.Owner->GetWorldTransform());
        }
        std::printf("揺れ: Trauma=0の時刻=%g秒 期待=%g秒 姿勢一致\n", elapsed, duration);
    }

    // フェードは実効アーム長に対し両端で飽和し、その間は線形になる。
    void TestFade()
    {
        CameraFixture fixture;
        auto& camera = *fixture.Follow;
        const auto& profile = Access::Profile(camera);
        CAMERA_CHECK(camera.SetCollisionSettings(profile.Collision));
        // 短縮側だけを使い、アームが伸びるときの平滑化とは独立して確かめる。
        const float lengths[] = {profile.FadeEnd + .2f, (profile.FadeStart + profile.FadeEnd) * .5f,
                                 profile.FadeStart - .1f};
        const float expected[] = {1, .5f, 0};
        for (unsigned index = 0; index < 3; ++index)
        {
            fixture.Physics.SetHitDistance(lengths[index] + profile.Collision.Margin);
            fixture.Step();
            CAMERA_NEAR(camera.GetCollisionOutput().EffectiveLength, lengths[index]);
            CAMERA_NEAR(camera.GetSubjectFade(), expected[index]);
            std::printf("フェード: 実効長=%g 値=%g\n", camera.GetCollisionOutput().EffectiveLength, camera.GetSubjectFade());
        }
    }


    // 速度演出は単調に増え、基準速度で飽和し、強さゼロとアーム長の上限を守る。
    void TestSpeedEffects()
    {
        CameraFixture fixture;
        auto& camera = *fixture.Follow;
        auto& profile = Access::Profile(camera);
        const float ratios[] = {0, .25f, .5f, 1, 1.5f, 2};
        for (float strength : {1.f, 0.f})
        {
            profile.SpeedEffectsStrength = strength;
            float previousLength = -1, previousFov = -1, saturatedLength = 0, saturatedFov = 0;
            for (float ratio : ratios)
            {
                Access::SetSubjectSpeed(camera, profile.SpeedReference * ratio);
                fixture.Advance(600);
                const float length = camera.GetCollisionOutput().EffectiveLength;
                const float fov = fixture.Camera->GetFieldOfView();
                const float speed = ratio < 1 ? ratio : 1;
                CAMERA_NEAR(length, camera.GetArmLength() + profile.ArmGain * speed * strength, 1e-3f);
                CAMERA_NEAR(fov, profile.BaseFov + profile.FovGain * speed * speed * strength, 1e-3f);
                CAMERA_CHECK(length + 1e-4f >= previousLength && fov + 1e-4f >= previousFov);
                if (ratio == 1)
                {
                    saturatedLength = length;
                    saturatedFov = fov;
                }
                else if (ratio > 1)
                {
                    CAMERA_NEAR(length, saturatedLength, 1e-3f);
                    CAMERA_NEAR(fov, saturatedFov, 1e-3f);
                }
                previousLength = length;
                previousFov = fov;
                std::printf("速度演出: 強さ=%g 速度比=%g 実効長=%.6f FOV=%.6f\n", strength, ratio, length, fov);
            }
        }
        profile.SpeedEffectsStrength = 1;
        camera.SetArmLength(profile.MaximumArmLength - .1f);
        Access::SetSubjectSpeed(camera, profile.SpeedReference * 2);
        fixture.Advance(600);
        const float cappedLength = camera.GetCollisionOutput().EffectiveLength;
        CAMERA_CHECK(cappedLength <= profile.MaximumArmLength + 1e-4f);
        CAMERA_NEAR(cappedLength, profile.MaximumArmLength, 1e-3f);
        std::printf("速度演出: 上限近くの実効長=%.6f 上限=%.6f\n", cappedLength, profile.MaximumArmLength);
    }

    // ロックオンは目標へ向き、注視点の寄せを画面の上限と距離に応じて制限する。
    void TestLockOnAimAndBias()
    {
        for (float targetX : {2.f, 20.f})
        {
            CameraFixture fixture;
            auto* target = fixture.World.SpawnEntity<Core::Entity>();
            CAMERA_CHECK(target);
            target->SetPosition({targetX, .4f, 0});
            CAMERA_CHECK(fixture.Follow->SetLockOnTarget(target));
            fixture.Advance(600);
            const auto& profile = Access::Profile(*fixture.Follow);
            const auto& pose = fixture.Owner->GetWorldTransform();
            const auto forward = pose.rotation * Math::Vector3::UnitZ;
            const auto pivot = pose.position + forward * fixture.Follow->GetCollisionOutput().EffectiveLength;
            const auto subject = fixture.Visual->GetRenderWorldTransform().position + fixture.Follow->GetTargetOffset();
            const float distance = (target->GetRenderWorldTransform().position - subject).Length();
            const float biasLimit = profile.LockBias * distance;
            const float screenLimit = fixture.Follow->GetArmLength() *
                std::tan(fixture.Camera->GetFieldOfView() * .4f * Math::Constants::PI / 180);
            const float expected = biasLimit < screenLimit ? biasLimit : screenLimit;
            const float displacement = (pivot - subject).Length();
            CAMERA_NEAR(AngleDelta(fixture.Follow->GetYaw(), 270), 0, .05f);
            CAMERA_CHECK(displacement <= biasLimit + 1e-3f && displacement <= screenLimit + 1e-3f);
            CAMERA_NEAR(displacement, expected, 1e-3f);
            CAMERA_CHECK(targetX < 10 ? biasLimit < screenLimit : screenLimit < biasLimit);
            std::printf("ロックオン: 目標距離=%.6f ヨー=%.6f 寄せ=%.6f 距離上限=%.6f 画面上限=%.6f\n",
                        distance, fixture.Follow->GetYaw(), displacement, biasLimit, screenLimit);
        }
    }

    // Gameplayの視点入力は閾値以下ならロックを保ち、閾値を越えると解除する。
    void TestLockOnLookBreak()
    {
        using namespace Core::Input;
        CameraFixture fixture;
        auto* target = fixture.World.SpawnEntity<Core::Entity>();
        CAMERA_CHECK(target && fixture.Follow->SetLockOnTarget(target));
        InputSystem source;
        InputRouter router;
        source.SetRouter(&router);
        InputMapper mapper(source.GetState());
        InputBindingSet bindings;
        InputActionDefinition look;
        look.Id = Game::InputActions::Look;
        look.Settings.Type = EInputMappingValueType::Axis2D;
        look.Settings.Output = EInputAxisOutput::FrameDelta;
        InputBinding horizontal;
        horizontal.Source = {EInputBindingSource::MouseDelta, 0, 0};
        look.Bindings = {horizontal};
        CAMERA_CHECK(bindings.AddContext(Game::InputActions::GameplayContext, NorvesLib::ECursorMode::Normal));
        CAMERA_CHECK(bindings.AddAction(Game::InputActions::GameplayContext, look));
        CAMERA_CHECK(mapper.ConfigureWithContext(bindings, Game::InputActions::GameplayContext));
        mapper.Attach(router);
        fixture.Follow->BindInput(&mapper);
        const float limit = Access::Profile(*fixture.Follow).LockBreakDegrees;
        for (unsigned index = 0; index < 2; ++index)
        {
            const float input = index == 0 ? limit : limit + 1;
            const double time = static_cast<double>(index);
            CAMERA_CHECK(source.BeginFrame(time) && mapper.BeginFrame(time));
            CAMERA_CHECK(source.InjectRawMouseDelta(input, 0));
            CAMERA_CHECK(mapper.Update(time, FrameDelta));
            const auto action = mapper.GetAction(Game::InputActions::Look);
            CAMERA_CHECK(action.Valid && action.Active && action.Type == EInputMappingValueType::Axis2D);
            CAMERA_NEAR(action.Axis.x, input);
            fixture.Follow->TickInputGroup(FrameDelta);
            CAMERA_CHECK(index == 0 ? Access::LockOnId(*fixture.Follow) == target->GetObjectId()
                                    : Access::LockOnId(*fixture.Follow) == 0);
            source.EndFrame();
            std::printf("ロック解除入力: 入力=%.3f度 閾値=%.3f度 ロック=%s\n",
                        input, limit, index == 0 ? "維持" : "解除");
        }
        fixture.Follow->BindInput(nullptr);
    }

    // 破棄された目標へのロックは安全に解除する。
    void TestLockOnDestroyedTarget()
    {
        CameraFixture fixture;
        auto* target = fixture.World.SpawnEntity<Core::Entity>();
        CAMERA_CHECK(target);
        const uint64_t id = target->GetObjectId();
        target->SetPosition({5, .4f, 0});
        CAMERA_CHECK(fixture.Follow->SetLockOnTarget(target));
        fixture.Advance(10);
        CAMERA_CHECK(Access::LockOnId(*fixture.Follow) == id);
        CAMERA_CHECK(fixture.World.RemoveEntity(target));
        CAMERA_CHECK(!fixture.World.FindEntityByObjectId(id));
        fixture.Step();
        CAMERA_CHECK(Access::LockOnId(*fixture.Follow) == 0);
        std::puts("ロックオン: 破棄した目標を解除");
    }

    // 有効な関節へのロックは姿勢の無効化で解除し、存在しない関節の指定でも既存のロックを解除する。
    void TestLockOnJoint()
    {
        Core::ResourceRegistry registry;
        CAMERA_CHECK(registry.Initialize());
        auto skeleton = registry.CreateTransient<Core::SkeletonResource>("CameraJointSkeleton");
        auto resource = registry.CreateTransient<Core::SkinnedMeshResource>("CameraJointMesh");
        auto clip = registry.CreateTransient<Core::AnimationClipResource>("CameraJointClip");
        auto asset = registry.CreateTransient<Core::SkeletalAssetResource>("CameraJointAsset");
        CAMERA_CHECK(skeleton && resource && clip && asset);
        Core::Container::VariableArray<Core::Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].InverseBindMatrix.fill(0);
        for (unsigned index = 0; index < 4; ++index)
        {
            joints[0].InverseBindMatrix[index * 5] = 1;
        }
        skeleton->SetJoints(std::move(joints));
        CAMERA_CHECK(skeleton->Load());
        Core::Container::VariableArray<Core::Skeletal::SkeletalVertex> vertices(1);
        vertices[0].Normal = {0, 1, 0};
        vertices[0].JointIndices[0] = 0;
        vertices[0].JointWeights[0] = 1;
        resource->SetVertices(std::move(vertices));
        resource->SetIndices(Core::Container::VariableArray<uint32_t>{0, 0, 0});
        CAMERA_CHECK(resource->Load());
        Core::Skeletal::SkeletalAnimationClip clipData;
        clipData.Name = "Still";
        clipData.DurationSeconds = 1;
        Core::Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = Core::Skeletal::SkeletalAnimationPath::Translation;
        channel.Samples.push_back({0, {0, 0, 0, 0}});
        channel.Samples.push_back({1, {0, 0, 0, 0}});
        clipData.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(clipData));
        CAMERA_CHECK(clip->Load());
        asset->SetResources(resource, skeleton, clip);
        CAMERA_CHECK(asset->Load());
        CameraFixture fixture;
        auto* target = fixture.World.SpawnEntity<Core::Entity>();
        auto* mesh = target ? fixture.World.CreateComponent<Core::Component::SkinnedMeshComponent>(target) : nullptr;
        CAMERA_CHECK(mesh);
        target->SetPosition({5, .4f, 0});
        fixture.World.UpdateWorldTransforms();
        mesh->SetSkeletalAsset(asset);
        mesh->SetPlaying(false);
        CAMERA_CHECK(mesh->EvaluatePose());
        Math::Transform joint;
        CAMERA_CHECK(mesh->TryGetJointWorldTransform(0, joint));
        CAMERA_CHECK(fixture.Follow->SetLockOnJoint(target, 0));
        fixture.Step();
        CAMERA_CHECK(Access::LockOnId(*fixture.Follow) == target->GetObjectId());
        mesh->SetAnimationTimeSeconds(.5f);
        CAMERA_CHECK(!mesh->TryGetJointWorldTransform(0, joint));
        fixture.Step();
        CAMERA_CHECK(Access::LockOnId(*fixture.Follow) == 0);
        CAMERA_CHECK(mesh->EvaluatePose() && mesh->TryGetJointWorldTransform(0, joint));
        CAMERA_CHECK(fixture.Follow->SetLockOnTarget(target));
        CAMERA_CHECK(!fixture.Follow->SetLockOnJoint(target, 1));
        CAMERA_CHECK(Access::LockOnId(*fixture.Follow) == 0);
        std::puts("ロックオン: 有効関節の姿勢無効化と存在しない関節の指定で解除");
    }

    class FakeSurfaceAnchor final : public Core::Camera::ILockOnTarget
    {
      public:
        Math::Vector3 Position;
        float Radius = 0;

        bool TryResolve(const Core::World&, Core::Camera::LockOnPoint& out) const override
        {
            out.Position = Position;
            out.Radius = Radius;
            return true;
        }
        uint64_t GetEntityId() const override { return 0; }
    };

    // 外部のアンカーが対象の周囲を動いても、弱参照を保ちヨーを追従させる。
    void TestLockOnMovingSurfaceAnchor()
    {
        CameraFixture fixture;
        auto anchor = Core::Container::MakeShared<FakeSurfaceAnchor>();
        const auto subject = fixture.Visual->GetRenderWorldTransform().position + fixture.Follow->GetTargetOffset();
        anchor->Position = subject + Math::Vector3{5, 0, 0};
        anchor->Radius = .35f;
        CAMERA_CHECK(fixture.Follow->SetLockOn(anchor));
        for (unsigned frame = 0; frame < 600; ++frame)
        {
            // 移動を滑らかに減速し、終端では既存の静止目標と同じ角度の許容で確かめる。
            const float elapsed = static_cast<float>(frame + 1) * FrameDelta;
            const float angle = (1.f - std::exp(-elapsed)) * Math::Constants::PI * .5f;
            anchor->Position = subject + Math::Vector3{5 * std::cos(angle), 0, 5 * std::sin(angle)};
            fixture.Step();
            CAMERA_CHECK(fixture.Follow->IsLockedOn());
        }
        const auto direction = anchor->Position - fixture.Owner->GetWorldTransform().position;
        const float targetYaw = float(std::atan2(direction.x, direction.z) * 57.29577951308232 + 180);
        CAMERA_NEAR(AngleDelta(fixture.Follow->GetYaw(), targetYaw), 0, .05f);
        std::puts("表面アンカー: 移動中もロックを維持しヨーが追従");
    }

    // アンカーの持ち主が先に消えても安全に解除し、切れた弱参照の設定は拒否する。
    void TestLockOnDestroyedSurfaceAnchor()
    {
        CameraFixture fixture;
        auto anchor = Core::Container::MakeShared<FakeSurfaceAnchor>();
        anchor->Position = {5, .4f, 0};
        Core::Container::TWeakPtr<const Core::Camera::ILockOnTarget> target = anchor;
        CAMERA_CHECK(fixture.Follow->SetLockOn(target));
        fixture.Advance(10);
        CAMERA_CHECK(fixture.Follow->IsLockedOn());
        anchor.reset();
        fixture.Step();
        CAMERA_CHECK(!fixture.Follow->IsLockedOn());
        fixture.Advance(10);
        CAMERA_CHECK(!fixture.Follow->SetLockOn(target));
        CAMERA_CHECK(!fixture.Follow->IsLockedOn());
        std::puts("表面アンカー: 持ち主の破棄後に安全に解除し切れた弱参照を拒否");
    }

    // 自動焦点はレイの距離とロック目標の前方射影へ収束し、無効時は初期値を保つ。
    void TestAutoFocus()
    {
        CameraFixture fixture;
        auto& profile = Access::Profile(*fixture.Follow);
        profile.bAutoFocus = true;
        fixture.Physics.SetRayHitDistance(7.5f);
        fixture.Advance(480);
        CAMERA_NEAR(fixture.Camera->GetFocusDistance(), 7.5f, 1e-3f);
        CAMERA_NEAR(fixture.Follow->GetCollisionOutput().EffectiveLength, fixture.Follow->GetArmLength(), 1e-3f);
        CAMERA_CHECK(fixture.Physics.RayQueryCount > 0 && fixture.Physics.SweepQueryCount > 0);
        std::printf("自動焦点: レイ距離=7.500000 焦点=%.6f 衝突の実効長=%.6f\n",
                    fixture.Camera->GetFocusDistance(), fixture.Follow->GetCollisionOutput().EffectiveLength);
        fixture.Physics.SetRayHitDistance(12.25f);
        fixture.Advance(480);
        CAMERA_NEAR(fixture.Camera->GetFocusDistance(), 12.25f, 1e-3f);
        std::printf("自動焦点: レイ距離=12.250000 焦点=%.6f\n", fixture.Camera->GetFocusDistance());
        auto* target = fixture.World.SpawnEntity<Core::Entity>();
        CAMERA_CHECK(target);
        target->SetPosition({5, .4f, 0});
        CAMERA_CHECK(fixture.Follow->SetLockOnTarget(target));
        const unsigned previousRayCount = fixture.Physics.RayQueryCount;
        fixture.Advance(600);
        const auto& pose = fixture.Owner->GetWorldTransform();
        const float projection = Math::Vector3::Dot(target->GetRenderWorldTransform().position - pose.position,
                                                    pose.rotation * Math::Vector3::UnitZ);
        CAMERA_NEAR(fixture.Camera->GetFocusDistance(), projection, 1e-3f);
        CAMERA_CHECK(fixture.Physics.RayQueryCount == previousRayCount);
        std::printf("自動焦点: ロック目標の前方射影=%.6f 焦点=%.6f\n", projection, fixture.Camera->GetFocusDistance());

        CameraFixture disabled;
        CAMERA_CHECK(!Access::Profile(*disabled.Follow).bAutoFocus);
        const float initial = disabled.Camera->GetFocusDistance();
        disabled.Physics.SetRayHitDistance(7.5f);
        disabled.Advance(120);
        CAMERA_NEAR(disabled.Camera->GetFocusDistance(), initial);
        CAMERA_CHECK(disabled.Camera->SetFocusDistance(4.25f));
        disabled.Advance(120);
        CAMERA_NEAR(disabled.Camera->GetFocusDistance(), 4.25f);
        CAMERA_CHECK(disabled.Physics.RayQueryCount == 0);
        std::printf("自動焦点: 無効時の初期値=%.6f 指定値=%.6f\n", initial, disabled.Camera->GetFocusDistance());
    }

    using ComparisonComponentBase = Core::Component::Component;
    class SimulationPositionFollower : public ComparisonComponentBase
    {
        REFLECTION_CLASS(SimulationPositionFollower, ComparisonComponentBase)

      public:
        Core::Entity* Physical = nullptr;
        Math::Vector3 HalfLives, Position, Velocity;
        unsigned Calls = 0;
        bool bInitialized = false;

        static void RunTick(ComparisonComponentBase& component, float dt)
        {
            auto& self = static_cast<SimulationPositionFollower&>(component);
            CAMERA_CHECK(self.Physical);
            const auto target = self.Physical->GetPosition();
            if (!self.bInitialized)
            {
                self.Position = target;
                self.bInitialized = true;
            }
            else
            {
                CAMERA_CHECK(Math::TryCriticalDamp(self.Position, self.Velocity, target, self.HalfLives, dt));
            }
            ++self.Calls;
        }
    };
    IMPLEMENT_CLASS(SimulationPositionFollower, ComparisonComponentBase)

    // 固定ステップの回数が揺れても、描画位置に対するカメラの追従は震えない。
    void TestVariableFixedSteps()
    {
        CameraFixture fixture;
        auto& world = fixture.World;
        FollowCameraComponent::RegisterTick(world);
        Core::Component::ComponentTickRegistration registration;
        registration.Name = "Test.SimulationPositionFollower";
        registration.Run = &SimulationPositionFollower::RunTick;
        world.RegisterComponentTick(SimulationPositionFollower::StaticClass(), registration);
        auto* comparison = world.CreateComponent<SimulationPositionFollower>(fixture.Owner);
        CAMERA_CHECK(comparison);
        CAMERA_CHECK(comparison->GetTickGroup() == Core::Component::ETickGroup::Default);
        comparison->Physical = fixture.Physical;
        comparison->HalfLives = Access::Profile(*fixture.Follow).PositionHalfLives;

        constexpr double h = 1.0 / 60.0;
        constexpr double speed = 6.0;
        std::mt19937 random(0x1234u);
        double accumulator = 0, elapsed = 0;
        unsigned fixedIndex = 0, frame = 0, samples = 0, stepCounts[3]{};
        Math::Vector3 cameraPrevious, cameraBeforePrevious, comparisonPrevious, comparisonBeforePrevious;
        float cameraMaximum = 0, comparisonMaximum = 0;
        while (elapsed < 10.0)
        {
            const float dt = static_cast<float>(h * (0.4 + 1.2 * static_cast<double>(random()) / 4294967296.0));
            accumulator += dt;
            elapsed += dt;
            unsigned steps = 0;
            while (accumulator >= h)
            {
                world.PrepareRenderInterpolationStep();
                ++fixedIndex;
                fixture.Physical->SetPosition({static_cast<float>(fixedIndex * speed * h), 0, 0});
                world.UpdateWorldTransforms();
                world.CaptureRenderInterpolationStep();
                accumulator -= h;
                ++steps;
            }
            CAMERA_CHECK(steps <= 2);
            ++stepCounts[steps];
            CAMERA_CHECK(world.SetRenderInterpolationAlpha(static_cast<float>(accumulator / h)));
            world.Tick(dt);
            world.LateTick(dt);
            world.UpdateWorldTransforms();
            ++frame;
            CAMERA_CHECK(comparison->Calls == frame);
            CAMERA_CHECK(fixture.Follow->GetCameraTickCount() == frame);
            CAMERA_CHECK(fixture.Follow->GetLastCollisionResult() == Core::Camera::CameraCollisionResult::Success);
            const auto visual = fixture.Visual->GetRenderWorldTransform().position;
            const auto cameraDifference = fixture.Owner->GetWorldTransform().position - visual;
            const auto comparisonDifference = comparison->Position - visual;
            if (elapsed >= 5.0 && frame >= 3)
            {
                const float cameraSecondDifference = (cameraDifference - cameraPrevious * 2.f + cameraBeforePrevious).Length();
                const float comparisonSecondDifference =
                    (comparisonDifference - comparisonPrevious * 2.f + comparisonBeforePrevious).Length();
                CAMERA_CHECK(std::isfinite(cameraSecondDifference) && std::isfinite(comparisonSecondDifference));
                cameraMaximum = std::max(cameraMaximum, cameraSecondDifference);
                comparisonMaximum = std::max(comparisonMaximum, comparisonSecondDifference);
                ++samples;
            }
            cameraBeforePrevious = cameraPrevious;
            cameraPrevious = cameraDifference;
            comparisonBeforePrevious = comparisonPrevious;
            comparisonPrevious = comparisonDifference;
        }
        std::printf("固定ステップの揺れ: カメラ最大=%.9g m 比較最大=%.9g m 0回=%u 1回=%u 2回=%u 測定=%u\n",
                    cameraMaximum, comparisonMaximum, stepCounts[0], stepCounts[1], stepCounts[2], samples);
        std::fflush(stdout);
        CAMERA_CHECK(stepCounts[0] && stepCounts[1] && stepCounts[2] && samples >= 2);
        CAMERA_CHECK(cameraMaximum <= 1e-4f);
        CAMERA_CHECK(comparisonMaximum > 1e-3f);
    }

    // 対象自身の開始時の重なりを照会から除外し、アームを保つ。
    void TestSubjectCollisionExclusion()
    {
        CameraFixture excluded, included;
        excluded.Step();
        included.Step();
        const ColliderHandle collider{7, 1};
        const BodyHandle body{7, 1};
        QueryObstacle self;
        self.Hit.Collider = collider;
        self.Hit.Body = body;
        self.Hit.Distance = 0;
        self.Hit.bStartPenetrating = true;
        excluded.Physics.Obstacles.push_back(self);
        included.Physics.Obstacles.push_back(self);
        Access::SetSubjectHandles(*excluded.Follow, collider, body);
        const float previousLength = included.Follow->GetCollisionOutput().EffectiveLength;
        excluded.Step();
        CAMERA_CHECK(excluded.Physics.Last.Filter.IgnoreColliders[0] == collider);
        CAMERA_CHECK(excluded.Physics.Last.Filter.IgnoreBodies[0] == body);
        CAMERA_NEAR(excluded.Follow->GetCollisionOutput().EffectiveLength, excluded.Follow->GetArmLength());

        included.World.UpdateRenderTransforms();
        included.Follow->TickInputGroup(FrameDelta);
        included.Follow->TickCameraGroup(FrameDelta);
        included.World.UpdateWorldTransforms();
        const auto result = included.Follow->GetLastCollisionResult();
        const float includedLength = included.Follow->GetCollisionOutput().EffectiveLength;
        std::printf("対象自身の除外: 除外あり 実効長=%.6f 結果=Success 除外なし 実効長=%.6f 結果=%s (%d)\n",
                    excluded.Follow->GetCollisionOutput().EffectiveLength, includedLength,
                    result == Core::Camera::CameraCollisionResult::StartOverlapping ? "StartOverlapping" : "その他",
                    static_cast<int>(result));
        CAMERA_CHECK(result == Core::Camera::CameraCollisionResult::StartOverlapping);
        CAMERA_NEAR(includedLength, previousLength);

        CameraFixture contactExcluded, contactIncluded;
        contactExcluded.Step();
        contactIncluded.Step();
        self.Hit.bStartPenetrating = false;
        contactExcluded.Physics.Obstacles.push_back(self);
        contactIncluded.Physics.Obstacles.push_back(self);
        Access::SetSubjectHandles(*contactExcluded.Follow, collider, body);
        contactExcluded.Step();
        CAMERA_CHECK(contactExcluded.Physics.Last.Filter.IgnoreColliders[0] == collider);
        CAMERA_CHECK(contactExcluded.Physics.Last.Filter.IgnoreBodies[0] == body);
        CAMERA_NEAR(contactExcluded.Follow->GetCollisionOutput().EffectiveLength,
                    contactExcluded.Follow->GetArmLength());

        contactIncluded.World.UpdateRenderTransforms();
        contactIncluded.Follow->TickInputGroup(FrameDelta);
        contactIncluded.Follow->TickCameraGroup(FrameDelta);
        contactIncluded.World.UpdateWorldTransforms();
        const auto contactResult = contactIncluded.Follow->GetLastCollisionResult();
        const float contactLength = contactIncluded.Follow->GetCollisionOutput().EffectiveLength;
        std::printf("対象自身の除外（距離0の接触）: 除外あり 実効長=%.6f 結果=Success 除外なし 実効長=%.6f 結果=%s (%d)\n",
                    contactExcluded.Follow->GetCollisionOutput().EffectiveLength, contactLength,
                    contactResult == Core::Camera::CameraCollisionResult::Success ? "Success" : "その他",
                    static_cast<int>(contactResult));
        std::fflush(stdout);
        CAMERA_CHECK(contactResult == Core::Camera::CameraCollisionResult::Success);
        CAMERA_CHECK(contactLength <= 1e-4f);
    }

    void SameProfile(const FollowCameraProfile& actual, const FollowCameraProfile& expected)
    {
#define PROFILE_FIELD(field) CAMERA_CHECK(actual.field == expected.field)
        PROFILE_FIELD(PositionHalfLives.x); PROFILE_FIELD(PositionHalfLives.y); PROFILE_FIELD(PositionHalfLives.z);
        PROFILE_FIELD(ShakeDegrees.x); PROFILE_FIELD(ShakeDegrees.y); PROFILE_FIELD(ShakeDegrees.z);
        PROFILE_FIELD(YawReturnDelay); PROFILE_FIELD(YawHalfLife); PROFILE_FIELD(MinimumYawSpeed);
        PROFILE_FIELD(SpeedReference); PROFILE_FIELD(ArmGain); PROFILE_FIELD(FovGain); PROFILE_FIELD(BaseFov);
        PROFILE_FIELD(FovHalfLife); PROFILE_FIELD(SpeedEffectsStrength); PROFILE_FIELD(MaximumArmLength);
        PROFILE_FIELD(FocusHalfLife); PROFILE_FIELD(MaximumFocusDistance); PROFILE_FIELD(bAutoFocus);
        PROFILE_FIELD(FadeStart); PROFILE_FIELD(FadeEnd); PROFILE_FIELD(TraumaDecay); PROFILE_FIELD(ShakeStrength);
        PROFILE_FIELD(LockBias); PROFILE_FIELD(LockBreakDegrees); PROFILE_FIELD(LockArmGain);
        PROFILE_FIELD(Collision.ProbeRadius); PROFILE_FIELD(Collision.Margin); PROFILE_FIELD(Collision.ExtendHalfLife);
        PROFILE_FIELD(Collision.MaximumExtendSpeed);
#undef PROFILE_FIELD
    }
    bool ParseText(const char* text, FollowCameraProfile& profile)
    {
        return ParseFollowCameraProfile({reinterpret_cast<const uint8_t*>(text), std::strlen(text)}, profile);
    }

    // 設定は資産の値と省略時の既定値を読み込み、範囲外・型違いを出力を変えずに拒否する。
    void TestProfileParsing()
    {
        Core::Asset::AssetFileReader reader(Core::Container::AnsiString(NORVES_SOURCE_ROOT "/Assets"));
        const auto read = reader.Read("Gameplay/FollowCameraProfile.json");
        CAMERA_CHECK(read.Succeeded());
        FollowCameraProfile parsed;
        parsed.PositionHalfLives = {3, 4, 5};
        parsed.SpeedEffectsStrength = .9f;
        parsed.bAutoFocus = true;
        CAMERA_CHECK(ParseFollowCameraProfile(read.Blob.GetSpan(), parsed));
        // 配布資産の全設定は既定プロファイルと一致する。
        SameProfile(parsed, FollowCameraProfile{});
        CAMERA_CHECK(ParseText(R"({"version":1,"speedEffects":{"strength":0.75},"focus":{"enabled":true}})", parsed));
        CAMERA_NEAR(parsed.SpeedEffectsStrength, .75f);
        CAMERA_CHECK(parsed.bAutoFocus);
        const auto before = parsed;
        const char* invalid[] = {
            R"({"version":1,"halfLives":{"xz":0.9},"speedEffects":{"strength":2}})",
            R"({"version":1,"halfLives":{"xz":"a"}})",
            R"({"version":1,"focus":{"enabled":1}})"
        };
        for (const auto* text : invalid)
        {
            CAMERA_CHECK(!ParseText(text, parsed));
            SameProfile(parsed, before);
        }
        CAMERA_CHECK(ParseText(R"({"version":1})", parsed));
        SameProfile(parsed, FollowCameraProfile{});
        std::printf("設定: 資産=%zuバイト 読込成功、範囲外1件・型違い2件を拒否し全項目を保持、省略時は既定値\n", read.Blob.GetSize());
    }
} // namespace

int main()
{
    TestShortestYaw();
    TestYawReturn();
    TestSpeedEffects();
    TestTrauma();
    TestFade();
    TestLockOnAimAndBias();
    TestLockOnLookBreak();
    TestLockOnDestroyedTarget();
    TestLockOnJoint();
    TestLockOnMovingSurfaceAnchor();
    TestLockOnDestroyedSurfaceAnchor();
    TestAutoFocus();
    TestProfileParsing();
    TestVariableFixedSteps();
    TestSubjectCollisionExclusion();
    std::puts("FollowCameraBehaviorTest PASS");
    return 0;
}
