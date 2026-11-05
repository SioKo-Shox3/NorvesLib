#include "CombatSliceScene.h"

#include "Component/CameraComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MeshComponent.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeScope.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/InteractableComponent.h"
#include "Logging/LogMacros.h"
#include "Math/QuaternionUtils.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"

#if NORVES_ENABLE_CORE_TEXT
#include "Component/TextComponent.h"
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    using namespace NorvesLib::Core::Rendering;
    namespace M = NorvesLib::Math;
    namespace W = NorvesLib::Modules::Gameplay::World;
    namespace L = CombatSliceLayout;

    namespace
    {
        bool RegisterMesh(GameModeContext& ctx, const Container::VariableArray<Mesh3DVertex>& vertices,
                          const Container::VariableArray<uint32_t>& indices, MeshDataHandle& handle)
        {
            if (!ctx.RenderResourcesRef.Meshes().Register(handle, vertices.data(),
                    static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)), indices.data(),
                    static_cast<uint32_t>(indices.size())))
            {
                NORVES_LOG_ERROR("CombatSlice", "環境のメッシュを登録できません。");
                return false;
            }
            ctx.ScopeRef.TrackMesh(handle);
            return true;
        }

        bool AttachMesh(GameModeContext& ctx, Entity* owner, MeshDataHandle mesh, MaterialHandle material)
        {
            auto* component = ctx.WorldRef.CreateComponent<Component::MeshComponent>(owner);
            if (!component)
            {
                NORVES_LOG_ERROR("CombatSlice", "環境のメッシュ用Componentを作成できません。");
                return false;
            }
            component->SetMeshHandle(mesh);
            component->SetMaterial(0, material);
            return true;
        }

        void GenerateBedMesh(Container::VariableArray<Mesh3DVertex>& vertices,
                             Container::VariableArray<uint32_t>& indices)
        {
            // 平面を六面に並べ、底面が y=0 の幅1・高さ0.3・奥行き1.8の箱にする。
            vertices.clear();
            indices.clear();
            Container::VariableArray<Mesh3DVertex> plane;
            Container::VariableArray<uint32_t> triangles;
            ProceduralMeshGenerator::GeneratePlane(1, 1, 1, 1, plane, triangles);
            const M::Vector3 centers[] = {{0, .15f, .9f}, {0, .15f, -.9f}, {.5f, .15f, 0},
                                          {-.5f, .15f, 0}, {0, .3f, 0}, {0, 0, 0}};
            const M::Vector3 scales[] = {{1, 1, .3f}, {1, 1, .3f}, {.3f, 1, 1.8f},
                                         {.3f, 1, 1.8f}, {1, 1, 1.8f}, {1, 1, 1.8f}};
            constexpr float halfPi = 1.57079632679f;
            const M::Quaternion rotations[] = {
                M::Quaternion(M::Vector3::UnitX, halfPi), M::Quaternion(M::Vector3::UnitX, -halfPi),
                M::Quaternion(M::Vector3::UnitZ, -halfPi), M::Quaternion(M::Vector3::UnitZ, halfPi),
                M::Quaternion(), M::Quaternion(M::Vector3::UnitX, 2 * halfPi)};
            for (unsigned face = 0; face < 6; ++face)
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
    } // namespace

    bool BuildCombatSliceEnvironment(GameModeContext& ctx, CombatSliceSceneIds& ids, CombatSliceEnvironment& env)
    {
        if (env.FloorMaterial.IsValid() || env.BedMaterial.IsValid() || ids.Floor || ids.Light || ids.Camera ||
            ids.Zone || ids.Bed || ids.Prompt)
        {
            NORVES_LOG_ERROR("CombatSlice", "環境が既に生成されているため、重ねて生成できません。");
            return false;
        }

        MaterialCreateData material;
        material.DebugName = "CombatSliceFloor";
        material.BaseColor[0] = .35f;
        material.BaseColor[1] = .42f;
        material.BaseColor[2] = .35f;
        material.Metallic = 0;
        material.Roughness = .8f;
        material.bTwoSided = true;
        env.FloorMaterial = ctx.RenderResourcesRef.Materials().Create(material);
        if (!env.FloorMaterial.IsValid())
        {
            NORVES_LOG_ERROR("CombatSlice", "床の材質を作成できません。");
            return false;
        }
        material.DebugName = "CombatSliceBed";
        material.BaseColor[0] = .45f;
        material.BaseColor[1] = .25f;
        material.BaseColor[2] = .12f;
        env.BedMaterial = ctx.RenderResourcesRef.Materials().Create(material);
        if (!env.BedMaterial.IsValid())
        {
            NORVES_LOG_ERROR("CombatSlice", "寝床の材質を作成できません。");
            return false;
        }

        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MeshDataHandle floorMesh;
        MeshDataHandle bedMesh;
        ProceduralMeshGenerator::GeneratePlane(20, 20, 1, 1, vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, floorMesh))
        {
            return false;
        }
        GenerateBedMesh(vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, bedMesh))
        {
            return false;
        }

        auto* floor = ctx.ScopeRef.SpawnObject<Entity>();
        auto* lightOwner = ctx.ScopeRef.SpawnObject<Entity>();
        auto* cameraOwner = ctx.ScopeRef.SpawnObject<Entity>();
        auto* zoneOwner = ctx.ScopeRef.SpawnObject<Entity>();
        auto* bed = ctx.ScopeRef.SpawnObject<Entity>();
        ids.Floor = floor ? floor->GetObjectId() : 0;
        ids.Light = lightOwner ? lightOwner->GetObjectId() : 0;
        ids.Camera = cameraOwner ? cameraOwner->GetObjectId() : 0;
        ids.Zone = zoneOwner ? zoneOwner->GetObjectId() : 0;
        ids.Bed = bed ? bed->GetObjectId() : 0;
        if (!floor || !lightOwner || !cameraOwner || !zoneOwner || !bed)
        {
            NORVES_LOG_ERROR("CombatSlice", "床・光・カメラ・遭遇領域・寝床のEntityを作成できません。");
            return false;
        }
        floor->SetPosition({0, 0, 0});
        bed->SetPosition(L::BedPosition.ToVector3());
        zoneOwner->SetPosition(L::ZoneCenter.ToVector3());
        if (!AttachMesh(ctx, floor, floorMesh, env.FloorMaterial) ||
            !AttachMesh(ctx, bed, bedMesh, env.BedMaterial))
        {
            return false;
        }

        auto* light = ctx.WorldRef.CreateComponent<Component::DirectionalLightComponent>(lightOwner);
        auto* camera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(cameraOwner);
        auto* zone = ctx.WorldRef.CreateComponent<W::EncounterZoneComponent>(zoneOwner);
        auto* interactable = ctx.WorldRef.CreateComponent<W::InteractableComponent>(bed);
        if (!light || !camera || !zone || !interactable)
        {
            NORVES_LOG_ERROR("CombatSlice", "光・カメラ・遭遇領域・寝床のComponentを作成できません。");
            return false;
        }
        lightOwner->SetRotation(M::QuaternionUtils::LookRotation(M::Vector3(-.4f, -1, -.3f), M::Vector3::UnitY));
        light->SetLightColor(1, .95f, .85f);
        light->SetIntensity(60000);

        const M::Vector3 cameraPosition(6, 5, -9);
        const M::Vector3 cameraTarget(-1, .9f, -2);
        cameraOwner->SetPosition(cameraPosition);
        cameraOwner->SetRotation(M::QuaternionUtils::LookRotation(cameraTarget - cameraPosition, M::Vector3::UnitY));
        camera->SetActiveCamera(true);
        camera->SetFieldOfView(60);
        camera->SetNearPlane(.05f);
        camera->SetFarPlane(100);
        if (!camera->SetAperture(16) || !camera->SetShutterSpeed(.01f) || !camera->SetISO(100) ||
            !camera->SetExposureCompensation(0))
        {
            NORVES_LOG_ERROR("CombatSlice", "固定カメラの露出を設定できません。");
            return false;
        }

        zone->getZoneId() = Container::String(L::ZoneId);
        zone->getShape() = L::ZoneShape;
        zone->getRadius() = L::ZoneRadius;
        zone->getHalfHeight() = L::ZoneHalfHeight;
        zone->getEnterMargin() = L::ZoneEnterMargin;
        zone->getExitMargin() = L::ZoneExitMargin;
        zone->getResetDelaySeconds() = L::ZoneResetDelaySeconds;
        zone->getEnemyId() = Container::String(L::EnemyId);
        interactable->getActionId() = Container::String(L::BedActionId);
        interactable->getPromptTextKey() = Container::String(L::BedPromptTextKey);
        interactable->getEnterRadius() = L::BedEnterRadius;
        interactable->getExitRadius() = L::BedExitRadius;
        interactable->getFacingConeDegrees() = L::BedFacingConeDegrees;
        interactable->getbDisabledWhileEngaged() = L::bBedDisabledWhileEngaged;
        interactable->getbRequiresFreeMouth() = L::bBedRequiresFreeMouth;

#if NORVES_ENABLE_CORE_TEXT
        auto* promptOwner = ctx.ScopeRef.SpawnObject<Entity>();
        if (!promptOwner)
        {
            NORVES_LOG_ERROR("CombatSlice", "操作プロンプトのEntityを作成できません。");
            return false;
        }
        ids.Prompt = promptOwner->GetObjectId();
        promptOwner->SetPosition({24, 48, 0});
        auto* prompt = ctx.WorldRef.CreateComponent<Component::TextComponent>(promptOwner);
        if (!prompt)
        {
            NORVES_LOG_ERROR("CombatSlice", "操作プロンプトのTextComponentを作成できません。");
            return false;
        }
        prompt->SetText("[E] REST");
        prompt->SetFontAtlas(ctx.EngineRef.GetDefaultFontAtlas().get());
        prompt->SetBoardSpace(BoardSpace::ScreenSpace);
        prompt->SetTint(M::Vector4(1, 1, 1, 1));
        prompt->SetVisible(false);
#else
        ids.Prompt = 0;
#endif

        ctx.WorldRef.UpdateWorldTransforms();
        CameraProxy initial;
        if (!camera->BuildCameraProxy(initial))
        {
            NORVES_LOG_ERROR("CombatSlice", "固定カメラの描画用データを構築できません。");
            return false;
        }
        ctx.EngineRef.GetRenderWorld().SetMainCamera(initial);
        return true;
    }

    void ReleaseCombatSliceEnvironment(GameModeContext& ctx, CombatSliceEnvironment& env)
    {
        if (env.FloorMaterial.IsValid())
        {
            ctx.RenderResourcesRef.Materials().Release(env.FloorMaterial);
            env.FloorMaterial = {};
        }
        if (env.BedMaterial.IsValid())
        {
            ctx.RenderResourcesRef.Materials().Release(env.BedMaterial);
            env.BedMaterial = {};
        }
    }
} // namespace Game::GameModes
