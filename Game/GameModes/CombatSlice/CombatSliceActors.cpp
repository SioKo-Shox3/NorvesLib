#include "CombatSliceActors.h"

#include "Animation/SkeletalAssetResource.h"
#include "Component/HoldSlotComponent.h"
#include "Component/MeshComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SocketAttachmentComponent.h"
#include "Engine/NorvesEngine.h"
#include "GameMode/GameModeScope.h"
#include "Gameplay/Combat/CombatData.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Enemy/EnemyDefinition.h"
#include "Gameplay/Shisha/ShishaComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"
#include "Object/ResourceRegistry.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"

#include <utility>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    using namespace NorvesLib::Core::Rendering;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace A = NorvesLib::Core::Animation;
    namespace M = NorvesLib::Math;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    namespace Enemy = NorvesLib::Modules::Gameplay::Enemy;
    namespace W = NorvesLib::Modules::Gameplay::World;
    namespace L = CombatSliceLayout;

    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float CapsuleRadius = .25f;
        constexpr float CapsuleHalfSegment = .25f;
        constexpr float DogBodyHeight = .6f;
        constexpr float ShishaTorsoCenterHeight = 1.0f;
        constexpr float ShishaHandVisualOffset = .15f;
        constexpr float ShishaLegVisualOffset = -.25f;

        bool BuildRig(ResourceRegistry& registry, const char* skeletonName, const char* meshName,
                      const char* clipName, const char* assetName, const char* const* jointNames,
                      const M::Vector3* translations, uint32_t jointCount,
                      C::TSharedPtr<SkeletonResource>& skeleton, C::TSharedPtr<SkinnedMeshResource>& mesh,
                      C::TSharedPtr<AnimationClipResource>& clip, C::TSharedPtr<SkeletalAssetResource>& asset)
        {
            skeleton = registry.CreateTransient<SkeletonResource>(skeletonName);
            mesh = registry.CreateTransient<SkinnedMeshResource>(meshName);
            clip = registry.CreateTransient<AnimationClipResource>(clipName);
            asset = registry.CreateTransient<SkeletalAssetResource>(assetName);
            if (!skeleton || !mesh || !clip || !asset)
            {
                NORVES_LOG_ERROR("CombatSlice", "合成骨格の資源を作成できません。");
                return false;
            }

            C::VariableArray<S::SkeletalJoint> joints(jointCount);
            C::VariableArray<S::SkeletalVertex> vertices;
            C::VariableArray<uint32_t> indices;
            S::SkeletalAnimationClip animation;
            animation.Name = "still";
            animation.DurationSeconds = 1.0f;
            for (uint32_t joint = 0; joint < jointCount; ++joint)
            {
                joints[joint].Name = jointNames[joint];
                joints[joint].ParentIndex = joint == 0 ? -1 : 0;
                joints[joint].InverseBindMatrix = {1, 0, 0, 0, 0, 1, 0, 0,
                                                   0, 0, 1, 0, 0, 0, 0, 1};
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = joint;
                channel.Path = S::SkeletalAnimationPath::Translation;
                const auto& position = translations[joint];
                channel.Samples.push_back({0, {position.x, position.y, position.z, 0}});
                animation.Channels.push_back(std::move(channel));

                // 閾値は頂点数以下で警告するため、各関節に重み 1 の頂点を 3 個置く。
                const S::SkeletalPosition points[] = {{-.012f, 0, 0}, {.012f, 0, 0}, {0, .024f, 0}};
                for (const auto& point : points)
                {
                    S::SkeletalVertex vertex;
                    vertex.Position = point;
                    vertex.Normal = {0, 0, 1};
                    vertex.JointIndices[0] = joint;
                    vertex.JointWeights[0] = 1;
                    indices.push_back(static_cast<uint32_t>(vertices.size()));
                    vertices.push_back(vertex);
                }
            }
            skeleton->SetJoints(std::move(joints));
            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));
            clip->SetClip(std::move(animation));
            if (!skeleton->Load() || !mesh->Load() || !clip->Load())
            {
                NORVES_LOG_ERROR("CombatSlice", "合成骨格・メッシュ・クリップを読み込めません。");
                return false;
            }
            asset->SetResources(mesh, skeleton, clip);
            if (!asset->Load())
            {
                NORVES_LOG_ERROR("CombatSlice", "合成骨格の資産を読み込めません。");
                return false;
            }
            return true;
        }

        bool RegisterMesh(GameModeContext& ctx, const C::VariableArray<Mesh3DVertex>& vertices,
                          const C::VariableArray<uint32_t>& indices, MeshDataHandle& handle)
        {
            if (!ctx.RenderResourcesRef.Meshes().Register(handle, vertices.data(),
                    static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)), indices.data(),
                    static_cast<uint32_t>(indices.size())))
            {
                NORVES_LOG_ERROR("CombatSlice", "役者のメッシュを登録できません。");
                return false;
            }
            ctx.ScopeRef.TrackMesh(handle);
            return true;
        }

        void GenerateBlade(C::VariableArray<Mesh3DVertex>& vertices, C::VariableArray<uint32_t>& indices)
        {
            constexpr float thickness = .04f;
            constexpr float width = .06f;
            constexpr float length = L::BladeEnd - L::BladeStart;
            constexpr float center = (L::BladeStart + L::BladeEnd) * .5f;
            C::VariableArray<Mesh3DVertex> plane;
            C::VariableArray<uint32_t> triangles;
            ProceduralMeshGenerator::GeneratePlane(1, 1, 1, 1, plane, triangles);
            vertices.clear();
            indices.clear();
            const M::Vector3 centers[] = {{center, 0, width * .5f}, {center, 0, -width * .5f},
                                          {L::BladeEnd, 0, 0}, {L::BladeStart, 0, 0},
                                          {center, thickness * .5f, 0}, {center, -thickness * .5f, 0}};
            const M::Vector3 scales[] = {{length, 1, thickness}, {length, 1, thickness},
                                         {thickness, 1, width}, {thickness, 1, width},
                                         {length, 1, width}, {length, 1, width}};
            const M::Quaternion rotations[] = {
                M::Quaternion(M::Vector3::UnitX, Pi * .5f), M::Quaternion(M::Vector3::UnitX, -Pi * .5f),
                M::Quaternion(M::Vector3::UnitZ, -Pi * .5f), M::Quaternion(M::Vector3::UnitZ, Pi * .5f),
                M::Quaternion(), M::Quaternion(M::Vector3::UnitX, Pi)};
            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (auto vertex : plane)
                {
                    const M::Vector3 local(vertex.Position[0] * scales[face].x, 0,
                                           vertex.Position[2] * scales[face].z);
                    const auto position = centers[face] + rotations[face] * local;
                    const auto normal = rotations[face] * M::Vector3::UnitY;
                    vertex.Position[0] = position.x;
                    vertex.Position[1] = position.y;
                    vertex.Position[2] = position.z;
                    vertex.Normal[0] = normal.x;
                    vertex.Normal[1] = normal.y;
                    vertex.Normal[2] = normal.z;
                    vertices.push_back(vertex);
                }
                for (const auto index : triangles)
                {
                    indices.push_back(base + index);
                }
            }
        }

        bool AddVisual(GameModeContext& ctx, Entity* parent, MeshDataHandle mesh, MaterialHandle material,
                       const M::Vector3& position, const M::Vector3& scale,
                       const M::Quaternion& rotation = M::Quaternion())
        {
            auto* child = ctx.ScopeRef.SpawnObject<Entity>(parent);
            if (!child)
            {
                NORVES_LOG_ERROR("CombatSlice", "役者の見た目用 Entity を作成できません。");
                return false;
            }
            child->SetLocalPosition(position);
            child->SetLocalScale(scale);
            child->SetLocalRotation(rotation);
            auto* component = ctx.WorldRef.CreateComponent<Component::MeshComponent>(child);
            if (!component)
            {
                NORVES_LOG_ERROR("CombatSlice", "役者の見た目用 Component を作成できません。");
                return false;
            }
            component->SetMeshHandle(mesh);
            component->SetMaterial(0, material);
            return true;
        }

        bool AddDogHeadVisual(GameModeContext& ctx, Entity* dog, MeshDataHandle mesh, MaterialHandle material)
        {
            auto* head = ctx.ScopeRef.SpawnObject<Entity>();
            if (!head)
            {
                NORVES_LOG_ERROR("CombatSlice", "犬の頭用 Entity を作成できません。");
                return false;
            }
            head->SetPosition(L::DogStrikePosition.ToVector3() + L::DogHeadLocal.ToVector3());
            head->SetRenderInterpolationEnabled(true);
            auto* attachment = ctx.WorldRef.CreateComponent<Component::SocketAttachmentComponent>(head);
            A::SocketReport report;
            if (!attachment ||
                !attachment->ApplySettingsJson(C::String(R"({"attachProfiles":[{"name":"Head"}]})"), report))
            {
                NORVES_LOG_ERROR("CombatSlice", "犬の頭の取り付けを設定できません: %s", report.Detail.c_str());
                return false;
            }
            ctx.WorldRef.UpdateWorldTransforms();
            if (!attachment->Attach(dog->GetObjectId(), Identity(L::DogMouthSocket), {}, .1f) ||
                !AddVisual(ctx, head, mesh, material, M::Vector3::Zero, {.22f, .22f, .22f}))
            {
                NORVES_LOG_ERROR("CombatSlice", "犬の頭をソケットへ取り付けられません。");
                return false;
            }
            return true;
        }
    } // namespace

    bool BuildCombatSliceActors(GameModeContext& ctx, CombatSliceSceneIds& ids, CombatSliceActors& actors)
    {
        if (ids.Dog || ids.Sword || ids.Shisha || actors.DogSkeleton || actors.DogMesh || actors.DogClip ||
            actors.DogAsset || actors.ShishaSkeleton || actors.ShishaMesh || actors.ShishaClip || actors.ShishaAsset ||
            actors.DogMaterial.IsValid() || actors.SwordMaterial.IsValid() || actors.ShishaMaterial.IsValid())
        {
            NORVES_LOG_ERROR("CombatSlice", "役者が既に生成されているため、重ねて生成できません。");
            return false;
        }

        C::String error;
        Combat::CombatFactionTable factions;
        Combat::CombatAttackDefinition attack;
        Combat::CombatHurtboxDefinition hurtboxes;
        Enemy::EnemyDefinition definition;
        if (!Combat::LoadCombatFactionsFromAsset(L::FactionsPath, factions, error) ||
            !Combat::LoadCombatAttackFromAsset(L::SwordAttackPath, factions, attack, error) ||
            !Combat::LoadCombatHurtboxesFromAsset(L::HurtboxPath, factions, hurtboxes, error) ||
            !Enemy::LoadEnemyDefinitionFromAsset(L::EnemyDefinitionPath, definition, error))
        {
            NORVES_LOG_ERROR("CombatSlice", "役者の定義を読み込めません: %s", error.c_str());
            return false;
        }
        if (attack.Hitboxes.empty() || hurtboxes.Parts.empty())
        {
            NORVES_LOG_ERROR("CombatSlice", "剣またはシ者の判定部位がありません。");
            return false;
        }

        MaterialCreateData material;
        material.DebugName = "CombatSliceDog";
        material.BaseColor[0] = .58f;
        material.BaseColor[1] = .29f;
        material.BaseColor[2] = .12f;
        material.Metallic = 0;
        material.Roughness = .8f;
        material.bTwoSided = true;
        actors.DogMaterial = ctx.RenderResourcesRef.Materials().Create(material);
        material.DebugName = "CombatSliceSword";
        material.BaseColor[0] = .72f;
        material.BaseColor[1] = .76f;
        material.BaseColor[2] = .8f;
        material.Metallic = .7f;
        material.Roughness = .3f;
        actors.SwordMaterial = ctx.RenderResourcesRef.Materials().Create(material);
        material.DebugName = "CombatSliceShisha";
        material.BaseColor[0] = .42f;
        material.BaseColor[1] = .42f;
        material.BaseColor[2] = .44f;
        material.Metallic = 0;
        material.Roughness = .8f;
        actors.ShishaMaterial = ctx.RenderResourcesRef.Materials().Create(material);
        if (!actors.DogMaterial.IsValid() || !actors.SwordMaterial.IsValid() || !actors.ShishaMaterial.IsValid())
        {
            NORVES_LOG_ERROR("CombatSlice", "役者の材質を作成できません。");
            return false;
        }

        const char* dogJoints[] = {"Root", "Head"};
        const M::Vector3 dogTranslations[] = {M::Vector3::Zero, L::DogHeadLocal.ToVector3()};
        const char* shishaJoints[] = {"Root", "Head", "hand_l", "hand_r", "leg_l", "leg_r"};
        const M::Vector3 shishaTranslations[] = {
            L::ShishaRootLocal.ToVector3(), L::ShishaHeadLocal.ToVector3(),
            L::ShishaHandLeftLocal.ToVector3(), L::ShishaHandRightLocal.ToVector3(),
            L::ShishaLegLeftLocal.ToVector3(), L::ShishaLegRightLocal.ToVector3()};
        auto& registry = GEngine.GetResourceRegistry();
        if (!BuildRig(registry, "CombatSliceDogSkeleton", "CombatSliceDogMesh", "CombatSliceDogClip",
                      "CombatSliceDogAsset", dogJoints, dogTranslations, 2,
                      actors.DogSkeleton, actors.DogMesh, actors.DogClip, actors.DogAsset) ||
            !BuildRig(registry, "CombatSliceShishaSkeleton", "CombatSliceShishaMesh", "CombatSliceShishaClip",
                      "CombatSliceShishaAsset", shishaJoints, shishaTranslations, 6,
                      actors.ShishaSkeleton, actors.ShishaMesh, actors.ShishaClip, actors.ShishaAsset))
        {
            return false;
        }
        A::SocketReport socketReport;
        if (!actors.DogSkeleton->ApplySocketsJson(
                C::String(R"({"sockets":[{"name":"Mouth","parent":"Head","position":[0,0,0]}]})"), socketReport))
        {
            NORVES_LOG_ERROR("CombatSlice", "口のソケットを設定できません: %s", socketReport.Detail.c_str());
            return false;
        }

        C::VariableArray<Mesh3DVertex> vertices;
        C::VariableArray<uint32_t> indices;
        MeshDataHandle sphereMesh;
        MeshDataHandle capsuleMesh;
        MeshDataHandle bladeMesh;
        ProceduralMeshGenerator::GenerateUVSphere(1, 16, 12, vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, sphereMesh)) return false;
        ProceduralMeshGenerator::GenerateUVSphere(CapsuleRadius, 16, 12, vertices, indices);
        for (auto& vertex : vertices)
        {
            vertex.Position[1] += vertex.Position[1] >= 0 ? CapsuleHalfSegment : -CapsuleHalfSegment;
        }
        if (!RegisterMesh(ctx, vertices, indices, capsuleMesh)) return false;
        GenerateBlade(vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, bladeMesh)) return false;

        auto* dog = ctx.ScopeRef.SpawnObject<Entity>();
        auto* sword = ctx.ScopeRef.SpawnObject<Entity>();
        auto* shisha = ctx.ScopeRef.SpawnObject<Entity>();
        ids.Dog = dog ? dog->GetObjectId() : 0;
        ids.Sword = sword ? sword->GetObjectId() : 0;
        ids.Shisha = shisha ? shisha->GetObjectId() : 0;
        if (!dog || !sword || !shisha)
        {
            NORVES_LOG_ERROR("CombatSlice", "犬・剣・シ者の Entity を作成できません。");
            return false;
        }
        dog->SetPosition(L::DogStrikePosition.ToVector3());
        dog->SetRotation(M::Quaternion::Identity);
        sword->SetPosition(L::DogStrikePosition.ToVector3() + L::DogHeadLocal.ToVector3());
        sword->SetRenderInterpolationEnabled(true);
        shisha->SetPosition(L::ShishaPosition.ToVector3());
        shisha->SetRotation(M::Quaternion(M::Vector3::UnitY, Pi));

        auto* dogSkin = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(dog);
        auto* slots = ctx.WorldRef.CreateComponent<Component::HoldSlotComponent>(dog);
        auto* sensor = ctx.WorldRef.CreateComponent<W::ZoneSensorComponent>(dog);
        auto* attachment = ctx.WorldRef.CreateComponent<Component::SocketAttachmentComponent>(sword);
        auto* blade = ctx.WorldRef.CreateComponent<Component::MeshComponent>(sword);
        auto* hitbox = ctx.WorldRef.CreateComponent<Combat::HitboxComponent>(sword);
        if (!dogSkin || !slots || !sensor || !attachment || !blade || !hitbox)
        {
            NORVES_LOG_ERROR("CombatSlice", "犬または剣の Component を作成できません。");
            return false;
        }
        dogSkin->SetSkeletalAsset(actors.DogAsset);
        dogSkin->SetMaterial(actors.DogMaterial);
        dogSkin->SetPlaying(true);
        dogSkin->SetLooping(true);
        if (!dogSkin->EvaluatePose() ||
            !slots->ApplySlotsJson(C::String(R"({"holdSlots":[{"name":"Mouth","capacity":1,"acceptTags":["Sword"]}]})"),
                                   socketReport) ||
            !attachment->ApplySettingsJson(C::String(R"({"attachProfiles":[{"name":"Mouth"}]})"), socketReport))
        {
            NORVES_LOG_ERROR("CombatSlice", "犬の姿勢または剣の保持を設定できません: %s", socketReport.Detail.c_str());
            return false;
        }
        blade->SetMeshHandle(bladeMesh);
        blade->SetMaterial(0, actors.SwordMaterial);
        // ソケットは行ベクトルで姿勢を作り、通常の hitbox は四元数で点を回す。
        // 回転時も刀身の表示と判定を揃えるため、口と同位置の Head 関節へ束縛する。
        if (!hitbox->SetShape(attack.Hitboxes[0].Shape) || !hitbox->SetBinding(dog) ||
            !hitbox->SetJointBinding(Identity(L::DogHeadJoint)))
        {
            NORVES_LOG_ERROR("CombatSlice", "剣の判定形状または束縛先を設定できません。");
            return false;
        }
        hitbox->SetProfile(attack.Hitboxes[0].Profile);
        hitbox->SetActorId(ids.Dog);
        ctx.WorldRef.UpdateWorldTransforms();
        const Identity tag("Sword");
        if (!attachment->Attach(ids.Dog, Identity("Mouth"), Identity("Mouth"), .1f, {&tag, 1}))
        {
            NORVES_LOG_ERROR("CombatSlice", "剣を口へ取り付けられません。");
            return false;
        }

        auto* shishaSkin = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(shisha);
        auto* hurtbox = ctx.WorldRef.CreateComponent<Combat::HurtboxSetComponent>(shisha);
        auto* health = ctx.WorldRef.CreateComponent<Combat::HealthComponent>(shisha);
        if (!shishaSkin || !hurtbox || !health)
        {
            NORVES_LOG_ERROR("CombatSlice", "シ者の骨格・被弾・体力 Component を作成できません。");
            return false;
        }
        shishaSkin->SetSkeletalAsset(actors.ShishaAsset);
        shishaSkin->SetMaterial(actors.ShishaMaterial);
        shishaSkin->SetPlaying(true);
        shishaSkin->SetLooping(true);
        if (!shishaSkin->EvaluatePose())
        {
            NORVES_LOG_ERROR("CombatSlice", "シ者の姿勢を評価できません。");
            return false;
        }
        hurtbox->SetWeakJointThreshold(hurtboxes.WeakJoint);
        for (const auto& part : hurtboxes.Parts)
        {
            if (hurtbox->AddPart(part) == Combat::HurtboxSetComponent::InvalidPartIndex)
            {
                NORVES_LOG_ERROR("CombatSlice", "シ者の被弾部位を追加できません。");
                return false;
            }
        }

        // AddComponent は直ちに BeginPlay を呼ぶので、所有者へ追加する前に定義を設定する。
        const auto dogHandle = dog->GetEntityHandle();
        if (!dogHandle.IsValid())
        {
            NORVES_LOG_ERROR("CombatSlice", "犬の EntityHandle を取得できません。");
            return false;
        }
        auto enemy = C::MakeUnique<Game::Gameplay::ShishaComponent>();
        enemy->getDefinitionPath() = C::String(L::EnemyDefinitionPath);
        enemy->SetTarget(dogHandle);
        // 撃破完了後は World が部分木を破棄するため、先に Scope の追跡を外す。
        enemy->OnDefeatCompleted.Add([scope = &ctx.ScopeRef, shisha](const C::String&)
        {
            for (auto* child : shisha->GetChildEntities())
            {
                scope->Untrack(child);
            }
            scope->Untrack(shisha);
        });
        if (!enemy->SetDefinition(definition) || !shisha->AddComponent(enemy.get()))
        {
            NORVES_LOG_ERROR("CombatSlice", "シ者の定義を適用できません。");
            return false;
        }
        (void)enemy.release();
        if (static_cast<float>(health->getMaxHealth()) != definition.MaxHealth ||
            static_cast<float>(health->getHealth()) != definition.MaxHealth ||
            static_cast<float>(health->getStaggerThreshold()) != definition.StaggerThreshold)
        {
            NORVES_LOG_ERROR("CombatSlice", "シ者の体力へ定義が反映されません。");
            return false;
        }

        if (!AddVisual(ctx, dog, capsuleMesh, actors.DogMaterial, {0, DogBodyHeight, 0}, {1, 1, 1},
                       M::Quaternion(M::Vector3::UnitX, Pi * .5f)) ||
            !AddDogHeadVisual(ctx, dog, sphereMesh, actors.DogMaterial) ||
            !AddVisual(ctx, shisha, capsuleMesh, actors.ShishaMaterial,
                       L::ShishaRootLocal.ToVector3() + M::Vector3(0, ShishaTorsoCenterHeight, 0), {1.2f, .9f, .8f}) ||
            !AddVisual(ctx, shisha, sphereMesh, actors.ShishaMaterial, L::ShishaHeadLocal.ToVector3(), {.18f, .18f, .18f}) ||
            !AddVisual(ctx, shisha, capsuleMesh, actors.ShishaMaterial,
                       L::ShishaHandLeftLocal.ToVector3() + M::Vector3(0, ShishaHandVisualOffset, 0), {.48f, .54f, .48f}) ||
            !AddVisual(ctx, shisha, capsuleMesh, actors.ShishaMaterial,
                       L::ShishaHandRightLocal.ToVector3() + M::Vector3(0, ShishaHandVisualOffset, 0), {.48f, .54f, .48f}) ||
            !AddVisual(ctx, shisha, capsuleMesh, actors.ShishaMaterial,
                       L::ShishaLegLeftLocal.ToVector3() + M::Vector3(0, ShishaLegVisualOffset, 0), {.6f, .5f, .6f}) ||
            !AddVisual(ctx, shisha, capsuleMesh, actors.ShishaMaterial,
                       L::ShishaLegRightLocal.ToVector3() + M::Vector3(0, ShishaLegVisualOffset, 0), {.6f, .5f, .6f}))
        {
            return false;
        }
        ctx.WorldRef.UpdateWorldTransforms();
        return true;
    }

    void ReleaseCombatSliceActors(GameModeContext& ctx, CombatSliceActors& actors)
    {
        if (actors.DogMaterial.IsValid()) ctx.RenderResourcesRef.Materials().Release(actors.DogMaterial);
        if (actors.SwordMaterial.IsValid()) ctx.RenderResourcesRef.Materials().Release(actors.SwordMaterial);
        if (actors.ShishaMaterial.IsValid()) ctx.RenderResourcesRef.Materials().Release(actors.ShishaMaterial);
        actors = {};
    }
} // namespace Game::GameModes
