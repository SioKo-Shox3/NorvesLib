// 灰色の地面と昼の空を作り、元の glTF から代役を読み込んで人間大に揃える。
#include "GameModes/ShishaShowcase/ShishaShowcaseBaseScene.h"

#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Animation/SkeletalPoseBounds.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Resource/GLTFAnalyzer.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMarker.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMode.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    namespace Math = NorvesLib::Math;

    struct ShishaShowcaseStageState
    {
        bool m_bFailed = false;
        bool m_bCloudLayerChanged = false;
        CloudLayerParameters m_PreviousCloudLayer;
        Container::TSharedPtr<SkeletalAssetResource> m_Asset;
        Component::SkinnedMeshComponent* m_pSkinned = nullptr;
        MaterialHandle m_StandinMaterial;
        float m_ClipDurationSeconds = 0.0f;
    };

    namespace
    {
        constexpr float kStandinHeightMeters = 1.8f;
        // 既存の各場面が使う地面の ID（900〜940）と重ねない。
        constexpr uint32_t kGroundMeshId = 950;

        bool CreateGround(ShishaShowcaseSceneContext& ctx, ShishaShowcaseStageState& state)
        {
            // 単色でもアルベドをテクスチャで渡す。ラスタの色は材質の BaseColor を使わない。
            constexpr uint8_t kGray[] = {128, 128, 128, 255};
            TextureCreateInfo textureInfo;
            textureInfo.Width = 1;
            textureInfo.Height = 1;
            textureInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            textureInfo.DebugName = "ShishaShowcaseGroundGray";
            const auto texture = ctx.Mode.RenderResourcesRef.Textures().CreateTexture(textureInfo, kGray, sizeof(kGray));
            if (!texture.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackTexture(texture);

            MaterialCreateData groundMaterial;
            groundMaterial.DebugName = "ShishaShowcaseGround";
            groundMaterial.AlbedoTexture = texture;
            groundMaterial.Metallic = 0.0f;
            groundMaterial.Roughness = 0.9f;
            const auto material = ctx.Mode.RenderResourcesRef.Materials().Create(groundMaterial);
            if (!material.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMaterial(material);

            MaterialCreateData standinMaterial;
            standinMaterial.DebugName = "ShishaShowcaseStandin";
            standinMaterial.Metallic = 0.0f;
            standinMaterial.Roughness = 0.7f;
            // 無指定のアルベドは白。両面の検証資産を周回の全方向から見せる。
            standinMaterial.bTwoSided = true;
            state.m_StandinMaterial = ctx.Mode.RenderResourcesRef.Materials().Create(standinMaterial);
            if (!state.m_StandinMaterial.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMaterial(state.m_StandinMaterial);

            Container::VariableArray<Mesh3DVertex> vertices;
            Container::VariableArray<uint32_t> indices;
            const float groundSize = 400.0f * ctx.Scale;
            ProceduralMeshGenerator::GeneratePlane(groundSize, groundSize, 1, 1, vertices, indices);
            const MeshDataHandle mesh{kGroundMeshId};
            if (!ctx.Mode.RenderResourcesRef.Meshes().Register(
                    mesh, vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                    indices.data(), static_cast<uint32_t>(indices.size())))
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMesh(mesh);
            auto* ground = ctx.Mode.ScopeRef.SpawnObject<Entity>();
            if (ground == nullptr)
            {
                return false;
            }
            auto* component = ctx.Mode.WorldRef.CreateComponent<Component::MeshComponent>(ground);
            if (component == nullptr)
            {
                return false;
            }
            component->SetMeshHandle(mesh);
            component->SetMaterial(0, material);
            component->SetCastShadow(false);
            return true;
        }

        bool FailStage(ShishaShowcaseSceneContext& ctx, ShishaShowcaseStageState& state, const char* reason)
        {
            state.m_bFailed = true;
            LOG_ERROR("シ者の代役を読み込むか配置する処理に失敗しました");
            char values[128]{};
            std::snprintf(values, sizeof(values), "reason=%s", reason);
            LogShishaShowcaseMarker(ctx.Data.m_pScene->Name, "failure", values);
            return false;
        }

        bool AttachStandin(ShishaShowcaseSceneContext& ctx, ShishaShowcaseStageState& state)
        {
            if (!state.m_Asset || !state.m_Asset->IsLoaded())
            {
                return FailStage(ctx, state, "asset_load_failed");
            }
            const auto clip = state.m_Asset->GetClip(size_t{0});
            const auto mesh = state.m_Asset->GetMesh();
            if (!clip || !clip->IsLoaded() || !clip->IsValid() || !mesh)
            {
                return FailStage(ctx, state, "missing_clip_or_mesh");
            }
            auto* entity = ctx.Mode.ScopeRef.SpawnObject<Entity>();
            if (entity == nullptr)
            {
                return FailStage(ctx, state, "entity_creation_failed");
            }
            auto* component = ctx.Mode.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(entity);
            if (component == nullptr)
            {
                return FailStage(ctx, state, "component_creation_failed");
            }
            component->SetSkeletalAsset(state.m_Asset);
            if (!component->SetAnimationClip(clip))
            {
                return FailStage(ctx, state, "clip_binding_failed");
            }
            // World の自動再生を止め、決定的な Tick の積算時間だけで姿勢を進める。
            component->SetPlaying(false);
            component->SetLooping(true);
            component->SetAnimationTimeSeconds(0.0f);
            component->SetMaterial(state.m_StandinMaterial);
            component->SetCastShadow(true);
            if (!component->EvaluatePose())
            {
                return FailStage(ctx, state, "pose_evaluation_failed");
            }

            // 時刻 0 のスキニング済み頂点の実寸から正規化する。ノード変換もパレットに含まれる。
            // ValidU8Float は人型でなく二本の骨を持つ三角形なので、その縦寸法を人間の身長に合わせる。
            Math::AABB bounds;
            const auto& vertices = mesh->GetVertices();
            const auto& palette = component->GetPoseSnapshot().BonePalette;
            if (vertices.empty() || !Animation::ComputeExactBounds(
                    {vertices.data(), vertices.size()}, {palette.data(), palette.size()}, true, bounds))
            {
                return FailStage(ctx, state, "invalid_standin_bounds");
            }
            const float sourceHeight = bounds.Max.y - bounds.Min.y;
            if (!std::isfinite(sourceHeight) || sourceHeight <= 0.000001f)
            {
                return FailStage(ctx, state, "invalid_standin_height");
            }
            const float assetScale = kStandinHeightMeters / sourceHeight;
            const float appliedScale = assetScale * ctx.Scale;
            if (!std::isfinite(appliedScale) || appliedScale <= 0.0f)
            {
                return FailStage(ctx, state, "invalid_standin_scale");
            }
            const auto center = bounds.Center();
            entity->SetScale(appliedScale, appliedScale, appliedScale);
            entity->SetPosition(-center.x * appliedScale, -bounds.Min.y * appliedScale, -center.z * appliedScale);
            state.m_pSkinned = component;
            state.m_ClipDurationSeconds = clip->GetClip().DurationSeconds;

            ctx.Camera.SetView(Math::Vector3(0.0f, 1.4f * ctx.Scale, -4.0f * ctx.Scale),
                               Math::Vector3(0.0f, 1.08f * ctx.Scale, 0.0f));
            std::snprintf(ctx.Data.m_ReadyKeyValues, sizeof(ctx.Data.m_ReadyKeyValues),
                          "skinned=1 scale=%.6f asset_scale=%.6f applied_scale=%.6f standin_height_m=%.6f",
                          static_cast<double>(ctx.Scale), static_cast<double>(assetScale),
                          static_cast<double>(appliedScale), static_cast<double>(sourceHeight * appliedScale));
            return true;
        }
    } // namespace

    bool BuildShishaShowcaseStage(ShishaShowcaseSceneContext& ctx)
    {
        auto state = Container::MakeShared<ShishaShowcaseStageState>();
        ctx.Data.m_StageState = state;
        auto sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = true;
        sky.SunAltitudeDegrees = ctx.Data.m_SunElevationDegrees;
        sky.SunAzimuthDegrees = 30.0f;
        auto& renderWorld = ctx.Mode.EngineRef.GetRenderWorld();
        if (ctx.Data.m_bOvercast)
        {
            // 空の太陽が方向光にも使われるため、太陽の輝度を下げて直接光を弱める。
            sky.MieDensityScale = 8.0f;
            sky.SunLuminanceNits *= 0.1f;
            state->m_PreviousCloudLayer = renderWorld.GetRenderingCoordinator().GetCloudLayer();
            state->m_bCloudLayerChanged = true;
            CloudLayerParameters clouds;
            clouds.bEnabled = true;
            clouds.Coverage = 1.0f;
            renderWorld.SetCloudLayer(clouds);
        }
        renderWorld.SetSkyAtmosphere(sky);
        renderWorld.SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
        renderWorld.SetStaticEnvironmentIntensityScale(1.0f);
        renderWorld.SetStaticEnvironmentMaxRadiance(0.0f);
        ctx.Camera.SetView(Math::Vector3(0.0f, 1.4f * ctx.Scale, -4.0f * ctx.Scale),
                           Math::Vector3(0.0f, 1.08f * ctx.Scale, 0.0f));
        if (!CreateGround(ctx, *state))
        {
            return false;
        }

        // 小さな検証用資産を同期で解析する。元の glTF を読み、クック済みのマニフェストに依存しない。
        auto decoded = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(
            Container::String(TEXT("Assets/Models/M9Skinned/ValidU8Float.gltf")));
        if (!decoded.Succeeded() || decoded.Data.Clips.empty())
        {
            return FailStage(ctx, *state, "asset_decode_failed");
        }
        auto& registry = GEngine.GetResourceRegistry();
        auto mesh = registry.CreateTransient<SkinnedMeshResource>(TEXT("ShishaShowcaseMesh"));
        auto skeleton = registry.CreateTransient<SkeletonResource>(TEXT("ShishaShowcaseSkeleton"));
        auto clip = registry.CreateTransient<AnimationClipResource>(TEXT("ShishaShowcaseClip"));
        state->m_Asset = registry.CreateTransient<SkeletalAssetResource>(TEXT("ShishaShowcaseAsset"));
        if (!mesh || !skeleton || !clip || !state->m_Asset)
        {
            return FailStage(ctx, *state, "resource_creation_failed");
        }
        mesh->SetVertices(std::move(decoded.Data.Vertices));
        mesh->SetIndices(std::move(decoded.Data.Indices));
        mesh->SetSubmeshTables(std::move(decoded.Data.SubMeshes), std::move(decoded.Data.MaterialSlots));
        mesh->SetMeshNodeGlobalTransform(decoded.Data.MeshNodeGlobalTransform);
        skeleton->SetJoints(std::move(decoded.Data.Joints));
        clip->SetClip(std::move(decoded.Data.Clips[0]));
        if (!mesh->Load() || !skeleton->Load() || !clip->Load())
        {
            return FailStage(ctx, *state, "resource_load_failed");
        }
        state->m_Asset->SetResources(mesh, skeleton, clip);
        if (!state->m_Asset->IsLoaded())
        {
            return FailStage(ctx, *state, "asset_assembly_failed");
        }
        return true;
    }

    bool UpdateShishaShowcaseStage(ShishaShowcaseSceneContext& ctx)
    {
        auto state = ctx.Data.m_StageState;
        if (!state || state->m_bFailed)
        {
            return false;
        }
        if (state->m_pSkinned == nullptr && !AttachStandin(ctx, *state))
        {
            return false;
        }
        const float time = state->m_ClipDurationSeconds > 0.0f
                               ? static_cast<float>(std::fmod(ctx.ElapsedSeconds, state->m_ClipDurationSeconds))
                               : 0.0f;
        state->m_pSkinned->SetAnimationTimeSeconds(time);
        return true;
    }

    void LeaveShishaShowcaseStage(ShishaShowcaseSceneContext& ctx)
    {
        const auto& state = ctx.Data.m_StageState;
        if (state && state->m_bCloudLayerChanged)
        {
            ctx.Mode.EngineRef.GetRenderWorld().SetCloudLayer(state->m_PreviousCloudLayer);
        }
        ctx.Data.m_StageState.reset();
    }

    Component::SkinnedMeshComponent* GetShishaShowcaseStandin(const ShishaShowcaseSceneContext& ctx)
    {
        return ctx.Data.m_StageState ? ctx.Data.m_StageState->m_pSkinned : nullptr;
    }

    bool BuildShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx)
    {
        return BuildShishaShowcaseStage(ctx);
    }

    bool UpdateShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx)
    {
        return UpdateShishaShowcaseStage(ctx);
    }

    void LeaveShishaShowcaseBaseScene(ShishaShowcaseSceneContext& ctx)
    {
        LeaveShishaShowcaseStage(ctx);
    }
} // namespace Game::GameModes
