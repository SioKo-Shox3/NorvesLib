// 草原の谷と尾根を、既定の 1 層の材質・物理空・太陽で描く開発用シーン。
// 地形の出どころは GrasslandTerrain.json と GenerateGrasslandHeight.py。クック済みの高さ場と 4×4 の区画を使う。
// near は地表から 0.6 m、valley は谷底から尾根、low は谷沿いを低い角度で見る固定視点。
#include "GameModes/Grassland/GrasslandMode.h"
#include "GameModes/Grassland/GrasslandScatter.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/GrassFieldComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Component/TerrainComponent.h"
#include "Core/Public/Component/ViewClearanceComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/MathTypes.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Rendering/Wind.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Physics/TerrainColliderBinding.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <cmath>
#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace P = NorvesLib::Modules::Physics;
namespace I = NorvesLib::Core::Input;
namespace Math = NorvesLib::Math;

namespace Game::GameModes
{

#if NORVES_BUILD_DEVELOPMENT
    bool GrasslandViewInputCollector::OnKey(const I::KeyEvent& event)
    {
        if (event.Code != I::KeyCode::H)
        {
            return false;
        }
        if (event.Action == I::InputAction::Pressed)
        {
            m_bToggleRequested = true;
        }
        return true;
    }

    bool GrasslandViewInputCollector::ConsumeToggleRequest()
    {
        const bool bRequested = m_bToggleRequested;
        m_bToggleRequested = false;
        return bRequested;
    }
#endif

    namespace
    {
        // 空の太陽は仰角 40°・方位 30°（Rendering3DTest の既定と同じ）
        constexpr float kSunAltitudeDegrees = 40.0f;
        constexpr float kSunAzimuthDegrees = 30.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）。
        constexpr float kExposureEV100 = 15.0f;
        // 草原では +X 方向に吹く共通の風を有効にする。
        constexpr bool kWindEnabled = true;
        constexpr float kWindDirectionYawDegrees = 0.0f;
        constexpr float kWindBaseSpeed = 3.5f;
        // 突風の強さ・周期・広がりと、直交方向の乱流の強さ。
        constexpr float kWindGustStrength = 0.6f;
        constexpr float kWindGustFrequencyHz = 0.125f;
        constexpr float kWindGustScaleMeters = 25.0f;
        constexpr float kWindTurbulence = 0.2f;
        // SceneReady を出すまでの Tick の数。決定的な撮影は「読み込み中」を一度観測してから数え始めるので、
        // 最初のフレームは組み立て中として見せる。
        constexpr uint32_t kReadyTickCount = 3;

#if NORVES_BUILD_DEVELOPMENT
        /// 代役の箱は幅 0.35 m・高さ 0.55 m・前後の長さ 0.9 m。原点は体の中心に置く。
        bool CreateHeroView(GameModeContext& ctx, GrasslandData& data)
        {
            data.m_bHeroCreationAttempted = true;
            constexpr uint8_t kBrown[] = {112, 68, 40, 255};
            TextureCreateInfo textureInfo;
            textureInfo.Width = 1;
            textureInfo.Height = 1;
            textureInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            textureInfo.DebugName = "GrasslandHeroBrown";
            const auto texture = ctx.RenderResourcesRef.Textures().CreateTexture(textureInfo, kBrown, sizeof(kBrown));
            if (!texture.IsValid())
            {
                return false;
            }
            ctx.ScopeRef.TrackTexture(texture);
            MaterialCreateData materialInfo;
            materialInfo.DebugName = "GrasslandHero";
            materialInfo.AlbedoTexture = texture;
            materialInfo.Metallic = 0.0f;
            materialInfo.Roughness = 0.85f;
            const auto material = ctx.RenderResourcesRef.Materials().Create(materialInfo);
            if (!material.IsValid())
            {
                return false;
            }
            ctx.ScopeRef.TrackMaterial(material);

            const Math::Vector3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                              {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            // AnimationDebugScene の箱と同じく、各面を外側から見て反時計回りに並べる。
            const Math::Vector3 corners[6][4] = {
                {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},
                {{-1,-1,1},{-1,1,1},{-1,1,-1},{-1,-1,-1}},
                {{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},
                {{-1,-1,1},{-1,-1,-1},{1,-1,-1},{1,-1,1}},
                {{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,1}},
                {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}};
            Mesh3DVertex vertices[24]{};
            uint32_t indices[36]{};
            constexpr uint32_t kFaceIndices[] = {0, 1, 2, 0, 2, 3};
            for (uint32_t face = 0; face < 6; ++face)
            {
                for (uint32_t corner = 0; corner < 4; ++corner)
                {
                    const auto& point = corners[face][corner];
                    vertices[face * 4 + corner] = {
                        {point.x * 0.175f, point.y * 0.275f, point.z * 0.45f},
                        {normals[face].x, normals[face].y, normals[face].z},
                        {corner == 1 || corner == 2 ? 1.0f : 0.0f, corner >= 2 ? 1.0f : 0.0f}};
                }
                for (uint32_t index = 0; index < 6; ++index)
                {
                    indices[face * 6 + index] = face * 4 + kFaceIndices[index];
                }
            }
            // シーンの手続きメッシュ用の番号。既存の登録を上書きしない。
            const MeshDataHandle mesh{960};
            if (ctx.RenderResourcesRef.Meshes().GetGPUData(mesh) != nullptr ||
                !ctx.RenderResourcesRef.Meshes().Register(mesh, vertices, sizeof(vertices), indices, 36))
            {
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);

            auto* hero = ctx.ScopeRef.SpawnObject<Entity>();
            auto* cameraObject = ctx.ScopeRef.SpawnObject<Entity>();
            if (hero == nullptr || cameraObject == nullptr)
            {
                return false;
            }
            auto* heroMesh = ctx.WorldRef.CreateComponent<Component::MeshComponent>(hero);
            if (heroMesh == nullptr)
            {
                return false;
            }
            heroMesh->SetVisible(false);
            heroMesh->SetMeshHandle(mesh);
            heroMesh->SetMaterial(0, material);
            auto* arm = ctx.WorldRef.CreateComponent<Component::SpringArmComponent>(cameraObject);
            auto* camera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(cameraObject);
            auto* clearance = ctx.WorldRef.CreateComponent<Component::ViewClearanceComponent>(cameraObject);
            if (arm == nullptr || camera == nullptr || clearance == nullptr || !arm->SetPivot(hero))
            {
                return false;
            }
            arm->SetArmLength(2.5f);
            arm->SetPitch(-10.0f);
            arm->SetTargetOffset(Math::Vector3(0.0f, 0.4f, 0.0f));
            camera->SetAperture(16.0f);
            camera->SetISO(100.0f);
            camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
            camera->SetExposureCompensation(0.0f);
            camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
            data.m_pHero = hero;
            data.m_pHeroMesh = heroMesh;
            data.m_pHeroCamera = camera;
            data.m_pHeroSpringArm = arm;
            return true;
        }

        void SetHeroView(GrasslandData& data, bool bEnabled)
        {
            data.m_bHeroView = bEnabled;
            if (data.m_pHeroCamera != nullptr)
            {
                data.m_pHeroCamera->SetActiveCamera(bEnabled);
            }
            if (data.m_pHeroMesh != nullptr)
            {
                data.m_pHeroMesh->SetVisible(bEnabled);
            }
            if (auto* freeCamera = data.m_Rig.GetCamera())
            {
                freeCamera->SetActiveCamera(!bEnabled);
            }
        }

        void EnterHeroView(GameModeContext& ctx, GrasslandData& data)
        {
            if (data.m_pHero == nullptr && (data.m_bHeroCreationAttempted || !CreateHeroView(ctx, data)))
            {
                LOG_WARNING("grassland: 主人公視点を作れませんでした。シーンへ入り直すと再試行します");
                return;
            }
            const auto& controller = data.m_Rig.GetController();
            const float yawRadians = controller.GetYaw() * Math::Constants::PI / 180.0f;
            const Math::Vector3 forward(-std::sin(yawRadians), 0.0f, std::cos(yawRadians));
            Math::Vector3 position = controller.GetPosition() + forward * 3.0f;
            bool bHeightSampled = false;
            if (data.m_pTerrain != nullptr)
            {
                const auto heightField = data.m_pTerrain->GetHeightField();
                if (heightField)
                {
                    // 地形の Entity は原点配置。高さ場はクック時の OriginY を含む世界座標の高さを返す。
                    const float height = heightField->SampleHeight(position.x, position.z);
                    if (std::isfinite(height))
                    {
                        position.y = height;
                        bHeightSampled = true;
                    }
                }
            }
            if (!bHeightSampled)
            {
                position.y = controller.GetPosition().y - 0.6f;
                LOG_WARNING("grassland: 地面の高さを取得できず、主人公をカメラの高さから 0.6 m 下へ仮置きします");
            }
            position.y += 0.275f;
            data.m_pHero->SetPosition(position);
            data.m_pHero->SetRotation(Math::QuaternionUtils::LookRotation(forward, Math::Vector3::Up));
            // 自由飛行のヨーは前方の角度、アームのヨーは支点からカメラへの角度なので変換する。
            data.m_pHeroSpringArm->SetYaw(180.0f - controller.GetYaw());
            ctx.WorldRef.UpdateWorldTransforms();
            SetHeroView(data, true);
        }

        void PublishHeroCamera(GameModeContext& ctx, GrasslandData& data)
        {
            data.m_pHeroSpringArm->RefreshOwnerTransform();
            CameraProxy proxy = data.m_Rig.ProxyTemplate();
            if (data.m_pHeroCamera->BuildCameraProxy(proxy))
            {
                ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
            }
        }
#endif

        bool CreateTerrain(GameModeContext& ctx, GrasslandData& data)
        {
            Entity* terrain = ctx.ScopeRef.SpawnObject<Entity>();
            if (terrain == nullptr)
            {
                LOG_ERROR("grassland: 地形の Entity を作れませんでした");
                return false;
            }
            auto* component = ctx.WorldRef.CreateComponent<Component::TerrainComponent>(terrain);
            if (component == nullptr)
            {
                LOG_ERROR("grassland: 地形の Component を作れませんでした");
                return false;
            }
            component->SetTerrainPath("Assets/Terrain/Grassland/Grassland.nvtrn");
            component->SetTilePathPrefix("Assets/Terrain/Grassland/Tiles/GrasslandTile_");
            data.m_pTerrain = component;
            return true;
        }

        bool CreateGrassField(GameModeContext& ctx)
        {
            Entity* field = ctx.ScopeRef.SpawnObject<Entity>();
            if (field == nullptr)
            {
                LOG_ERROR("grassland: 草の場の Entity を作れませんでした");
                return false;
            }
            Component::GrassFieldComponent* component = nullptr;
            try
            {
                component = new Component::GrassFieldComponent();
            }
            catch (...)
            {
                LOG_ERROR("grassland: 草の場の Component を作れませんでした");
                return false;
            }
            if (component == nullptr)
            {
                LOG_ERROR("grassland: 草の場の Component を作れませんでした");
                return false;
            }
            // AddComponent が BeginPlay を呼ぶ前に、読み込む草の場のパスを設定する。
            component->SetCookedFieldPath("Assets/Vegetation/Grassland/GrasslandGrassField.nvgfld");
            if (!field->AddComponent(component))
            {
                delete component;
                LOG_ERROR("grassland: 草の場の Component を追加できませんでした");
                return false;
            }
            return true;
        }

        void ApplyEnvironment(GameModeContext& ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = kSunAltitudeDegrees;
            sky.SunAzimuthDegrees = kSunAzimuthDegrees;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            WindParameters wind = MakeDefaultWindParameters();
            wind.bEnabled = kWindEnabled;
            wind.DirectionYawDegrees = kWindDirectionYawDegrees;
            wind.BaseSpeed = kWindBaseSpeed;
            wind.GustStrength = kWindGustStrength;
            wind.GustFrequencyHz = kWindGustFrequencyHz;
            wind.GustScaleMeters = kWindGustScaleMeters;
            wind.Turbulence = kWindTurbulence;
            ctx.EngineRef.GetRenderWorld().SetWindParameters(wind);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 環境の設定を、ほかのモードへ持ち越さないよう既定へ戻す
        void ResetEnvironment(GameModeContext& ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetWindParameters(MakeDefaultWindParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }
    } // namespace

    GameModeEnterResult GrasslandRoutine::Enter(GameModeContext& ctx, GrasslandData& data)
    {
        LOG_INFO("grassland を開始します");
        data.m_TickCount = 0;
        data.m_pTerrain = nullptr;
        data.m_Scatter = Scatter::kInvalidScatterHandle;
        data.m_bScatterPending = data.m_ScatterStressCount > 0;
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kGrasslandSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("grassland: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateTerrain(ctx, data) || !CreateGrassField(ctx))
        {
            ctx.EngineRef.GetScatterSystem().Unload(data.m_Scatter);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        if (!data.m_bScatterPending)
        {
            data.m_Scatter = LoadGrasslandScatter(ctx, 0);
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("grassland: カメラを作れませんでした");
            ctx.EngineRef.GetScatterSystem().Unload(data.m_Scatter);
            data.m_Scatter = Scatter::kInvalidScatterHandle;
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        Component::CameraComponent* camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
#if NORVES_BUILD_DEVELOPMENT
        data.m_ViewInput.OnInputReset();
        ctx.EngineRef.GetInputRouter().RegisterController(&data.m_ViewInput, I::InputRouter::PriorityGame);
        data.m_bViewInputRegistered = true;
#endif
        return GameModeEnterResult::Succeeded;
    }

    void GrasslandRoutine::Tick(GameModeContext& ctx, GrasslandData& data, float deltaTime)
    {
        ++data.m_TickCount;
#if NORVES_BUILD_DEVELOPMENT
        const bool bViewRequested = data.m_pViewSwitch != nullptr && data.m_pViewSwitch->IsRequested();
        if (bViewRequested && data.m_bHeroView)
        {
            // 名前付き視点の依頼は自由飛行へ戻してから受け、撮影の経路を優先する。
            SetHeroView(data, false);
        }
#endif
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kGrasslandSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
#if NORVES_BUILD_DEVELOPMENT
        const bool bToggle = data.m_ViewInput.ConsumeToggleRequest();
        if (bToggle && !bSuspended && !data.m_Rig.IsScriptRunning() && !bViewRequested)
        {
            if (data.m_bHeroView)
            {
                SetHeroView(data, false);
            }
            else
            {
                EnterHeroView(ctx, data);
            }
        }
        // 停止中のリグにも入力を捨てさせる。リグの公開後、同じ Tick の最後のカメラを主人公で上書きする。
        data.m_Rig.Tick(ctx, deltaTime, bSuspended || data.m_bHeroView);
        if (data.m_bHeroView)
        {
            PublishHeroCamera(ctx, data);
        }
#else
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
#endif
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        // すべての区画が揃ってから撮影を許可する。欠損区画を含む地形を準備完了として扱わない。
        const bool bTerrainReady = data.m_pTerrain != nullptr && data.m_pTerrain->IsFullyPlaced() &&
                                   data.m_pTerrain->GetPlacedTileCount() == data.m_pTerrain->GetTileCount();
        if (bTerrainReady && !data.m_bTerrainColliderAttempted)
        {
            data.m_bTerrainColliderAttempted = true;
            const P::EPhysicsResult result = P::AttachTerrainCollider(ctx.WorldRef, *data.m_pTerrain, data.m_pTerrainCollider);
            if (result != P::EPhysicsResult::Success)
            {
                LOG_WARNING("grassland: 地形のコライダーを作れませんでした (result=%u)", static_cast<unsigned>(result));
            }
        }
        if (data.m_bScatterPending && bTerrainReady)
        {
            data.m_Scatter = LoadGrasslandScatter(ctx, data.m_ScatterStressCount);
            data.m_bScatterPending = false;
        }
        Component::CameraComponent* scatterCamera = data.m_Rig.GetCamera();
#if NORVES_BUILD_DEVELOPMENT
        if (data.m_bHeroView)
        {
            scatterCamera = data.m_pHeroCamera;
        }
#endif
        if (Component::CameraComponent* camera = scatterCamera)
        {
            if (const Entity* owner = camera->GetOwner())
            {
                ctx.EngineRef.GetScatterSystem().Update(owner->GetPosition());
            }
        }
        const bool bScatterReady = !data.m_bScatterPending &&
            (data.m_Scatter == Scatter::kInvalidScatterHandle || ctx.EngineRef.GetScatterSystem().IsSettled(data.m_Scatter));
        const bool bReady = bTerrainReady && bScatterReady && data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kGrasslandSceneId, bReady);
    }

    void GrasslandRoutine::Leave(GameModeContext& ctx, GrasslandData& data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        ctx.EngineRef.GetScatterSystem().Unload(data.m_Scatter);
        data.m_Scatter = Scatter::kInvalidScatterHandle;
        data.m_bScatterPending = false;
#if NORVES_BUILD_DEVELOPMENT
        if (data.m_bViewInputRegistered)
        {
            ctx.EngineRef.GetInputRouter().UnregisterController(&data.m_ViewInput);
            data.m_bViewInputRegistered = false;
        }
        data.m_ViewInput.OnInputReset();
        SetHeroView(data, false);
        data.m_pHero = nullptr;
        data.m_pHeroMesh = nullptr;
        data.m_pHeroCamera = nullptr;
        data.m_pHeroSpringArm = nullptr;
        data.m_bHeroCreationAttempted = false;
#endif
        data.m_Rig.Leave(ctx);
        data.m_pTerrain = nullptr;
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        // 地形・草の場・カメラの Entity は GameModeScope::Cleanup が返す。各 Component の EndPlay で登録を返す。
        // 空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("grassland を終了します");
    }

} // namespace Game::GameModes
