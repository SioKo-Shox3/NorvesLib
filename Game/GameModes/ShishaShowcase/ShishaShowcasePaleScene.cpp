// PaleFlat の白と標準材質のグレーカードを並べ、太陽高度と曇天による見え方を比べる。
#include "GameModes/ShishaShowcase/ShishaShowcaseBaseScene.h"

#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMode.h"

#include <cmath>
#include <cstdio>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;

    namespace
    {
        constexpr uint32_t kPaleSphereMeshId = 951;
        constexpr uint32_t kGrayCardMeshId = 952;

        struct PaleSceneState
        {
            MaterialHandle m_PaleMaterial;
            bool m_bStandinMaterialApplied = false;
        };

        bool CreateMaterials(ShishaShowcaseSceneContext& ctx, PaleSceneState& state, MaterialHandle& grayMaterial)
        {
            MaterialCreateData pale;
            pale.DebugName = "ShishaShowcasePaleFlat";
            pale.Shading = ShadingModel::PaleFlat;
            pale.Metallic = 0.0f;
            pale.Roughness = 1.0f;
            pale.bTwoSided = true;
            state.m_PaleMaterial = ctx.Mode.RenderResourcesRef.Materials().Create(pale);
            if (!state.m_PaleMaterial.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMaterial(state.m_PaleMaterial);

            // 118/255 の sRGB はリニアで約 18% の反射率になる。
            constexpr uint8_t kGray[] = {118, 118, 118, 255};
            TextureCreateInfo textureInfo;
            textureInfo.Width = 1;
            textureInfo.Height = 1;
            textureInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_SRGB;
            textureInfo.DebugName = "ShishaShowcaseGrayCardTexture";
            const auto texture = ctx.Mode.RenderResourcesRef.Textures().CreateTexture(textureInfo, kGray, sizeof(kGray));
            if (!texture.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackTexture(texture);

            MaterialCreateData gray;
            gray.DebugName = "ShishaShowcaseGrayCard";
            gray.AlbedoTexture = texture;
            gray.Metallic = 0.0f;
            gray.Roughness = 0.9f;
            grayMaterial = ctx.Mode.RenderResourcesRef.Materials().Create(gray);
            if (!grayMaterial.IsValid())
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMaterial(grayMaterial);
            return true;
        }

        bool PlaceMesh(ShishaShowcaseSceneContext& ctx, uint32_t meshId,
                       const Container::VariableArray<Mesh3DVertex>& vertices,
                       const Container::VariableArray<uint32_t>& indices,
                       MaterialHandle material, float x, float y, float z)
        {
            const MeshDataHandle mesh{meshId};
            if (!ctx.Mode.RenderResourcesRef.Meshes().Register(
                    mesh, vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                    indices.data(), static_cast<uint32_t>(indices.size())))
            {
                return false;
            }
            ctx.Mode.ScopeRef.TrackMesh(mesh);
            auto* entity = ctx.Mode.ScopeRef.SpawnObject<Entity>();
            if (entity == nullptr)
            {
                return false;
            }
            auto* component = ctx.Mode.WorldRef.CreateComponent<Component::MeshComponent>(entity);
            if (component == nullptr)
            {
                return false;
            }
            entity->SetPosition(x * ctx.Scale, y * ctx.Scale, z * ctx.Scale);
            component->SetMeshHandle(mesh);
            component->SetMaterial(0, material);
            component->SetCastShadow(true);
            return true;
        }

        bool PlaceObjects(ShishaShowcaseSceneContext& ctx, MaterialHandle pale, MaterialHandle gray)
        {
            Container::VariableArray<Mesh3DVertex> vertices;
            Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GenerateUVSphere(0.35f * ctx.Scale, 32, 16, vertices, indices);
            if (!PlaceMesh(ctx, kPaleSphereMeshId, vertices, indices, pale, -1.1f, 1.0f, 0.3f))
            {
                return false;
            }

            ProceduralMeshGenerator::GeneratePlane(0.5f * ctx.Scale, 0.5f * ctx.Scale, 1, 1, vertices, indices);
            // 板を鉛直に立て、既定のカメラの方へ水平に向ける。
            const float distance = std::sqrt(1.1f * 1.1f + 4.3f * 4.3f);
            const float sinYaw = 1.1f / distance;
            const float cosYaw = 4.3f / distance;
            for (auto& vertex : vertices)
            {
                const float x = vertex.Position[0];
                const float z = vertex.Position[2];
                vertex.Position[0] = x * cosYaw;
                vertex.Position[1] = z;
                vertex.Position[2] = -x * sinYaw;
                vertex.Normal[0] = -sinYaw;
                vertex.Normal[1] = 0.0f;
                vertex.Normal[2] = -cosYaw;
            }
            return PlaceMesh(ctx, kGrayCardMeshId, vertices, indices, gray, 1.1f, 1.0f, 0.3f);
        }
    } // namespace

    bool BuildShishaShowcasePaleScene(ShishaShowcaseSceneContext& ctx)
    {
        if (!BuildShishaShowcaseStage(ctx))
        {
            return false;
        }
        auto state = Container::MakeShared<PaleSceneState>();
        ctx.Data.m_SceneState = state;
        MaterialHandle grayMaterial;
        return CreateMaterials(ctx, *state, grayMaterial) &&
               PlaceObjects(ctx, state->m_PaleMaterial, grayMaterial);
    }

    bool UpdateShishaShowcasePaleScene(ShishaShowcaseSceneContext& ctx)
    {
        if (!UpdateShishaShowcaseStage(ctx))
        {
            return false;
        }
        const auto state = Container::StaticPointerCast<PaleSceneState>(ctx.Data.m_SceneState);
        auto* standin = GetShishaShowcaseStandin(ctx);
        if (!state || standin == nullptr)
        {
            return false;
        }
        if (!state->m_bStandinMaterialApplied)
        {
            standin->SetMaterial(state->m_PaleMaterial);
            state->m_bStandinMaterialApplied = true;
            std::snprintf(ctx.Data.m_ReadyKeyValues, sizeof(ctx.Data.m_ReadyKeyValues),
                          "skinned=1 pale_flat_objects=2 gray_card=1 sun_elevation_deg=%.6f overcast=%d",
                          static_cast<double>(ctx.Data.m_SunElevationDegrees), ctx.Data.m_bOvercast ? 1 : 0);
        }
        return true;
    }

    void LeaveShishaShowcasePaleScene(ShishaShowcaseSceneContext& ctx)
    {
        LeaveShishaShowcaseStage(ctx);
        ctx.Data.m_SceneState.reset();
    }
} // namespace Game::GameModes
