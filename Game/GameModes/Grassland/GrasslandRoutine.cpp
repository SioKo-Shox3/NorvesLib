// 草原の谷と尾根を、既定の 1 層の材質・物理空・太陽で描く開発用シーン。
// 地形の出どころは GrasslandTerrain.json と GenerateGrasslandHeight.py。近景 4×4 と外周の区画を使う。
// near は地表から 0.6 m、valley は谷底から尾根、low は谷沿いを低い角度で見る固定視点。
#include "GameModes/Grassland/GrasslandMode.h"
#include "GameModes/Grassland/GrasslandScatter.h"

#if NORVES_BUILD_DEVELOPMENT
#include "Core/Public/Asset/AssetFileReader.h"
#endif
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/GrassFieldComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Component/TerrainComponent.h"
#include "Core/Public/Component/ViewClearanceComponent.h"
#include "Core/Public/Component/WorldFieldInteractorComponent.h"
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

        constexpr float kRunRadius = 10.0f;
        constexpr float kRunSpeed = 5.0f;
        constexpr float kRunCenterDistance = 18.0f;
        constexpr float kTwoPi = 6.28318530717958647692f;

        void UpdateRunningBody(GrasslandData& data, float deltaTime)
        {
            if (data.m_pRunningBody == nullptr)
            {
                return;
            }
            const auto heightField = data.m_pTerrain != nullptr ? data.m_pTerrain->GetHeightField() : nullptr;
            if (heightField == nullptr)
            {
                data.m_pRunningBody->SetActive(false);
                return;
            }
            if (std::isfinite(deltaTime) && deltaTime > 0.0f)
            {
                data.m_RunAngle = std::fmod(data.m_RunAngle + deltaTime * (kRunSpeed / kRunRadius), kTwoPi);
            }
            const float cosine = std::cos(data.m_RunAngle);
            const float sine = std::sin(data.m_RunAngle);
            const float x = data.m_RunCenter.x + kRunRadius * cosine;
            const float z = data.m_RunCenter.z + kRunRadius * sine;
            const float height = heightField->SampleHeight(x, z);
            if (!std::isfinite(height))
            {
                data.m_pRunningBody->SetActive(false);
                return;
            }
            data.m_pRunningBody->SetPosition(x, height, z);
            // 局所の +Z を円の接線へ向ける。高さだけを地形に合わせ、胴は水平に保つ。
            data.m_pRunningBody->SetRotation(NorvesLib::Math::QuaternionUtils::LookRotation(NorvesLib::Math::Vector3(-sine, 0.0f, cosine)));
            data.m_pRunningBody->SetActive(true);
        }

        bool CreateRunningBody(GameModeContext& ctx, GrasslandData& data, const Game::Scenes::FreeCameraPose& pose)
        {
            Entity* body = ctx.ScopeRef.SpawnObject<Entity>();
            if (body == nullptr)
            {
                LOG_ERROR("grassland: 走行体の Entity を作れませんでした");
                return false;
            }
            data.m_pRunningBody = body;
            // 地形を読めるまでは、原点で草を押さない。
            body->SetActive(false);
            auto* component = ctx.WorldRef.CreateComponent<Component::WorldFieldInteractorComponent>(body);
            if (component == nullptr)
            {
                LOG_ERROR("grassland: 走行体の Component を作れませんでした");
                return false;
            }
            // +Z が前。前胴・後胴を半径 0.28 m、高さ 0.45 m の球で仮置きする。
            // 四肢は骨格が決まるまでソケットを使わず、局所位置の球で仮置きする。
            const NorvesLib::Math::Vector3 centers[] = {
                {0.0f, 0.45f, 0.30f}, {0.0f, 0.45f, -0.30f},
                {-0.22f, 0.13f, 0.40f}, {0.22f, 0.13f, 0.40f},
                {-0.22f, 0.13f, -0.40f}, {0.22f, 0.13f, -0.40f}
            };
            for (uint32_t i = 0; i < 6; ++i)
            {
                Component::WorldFieldInteractorShape shape;
                shape.P0 = centers[i];
                shape.Radius = i < 2 ? 0.28f : 0.13f;
                if (!component->AddShape(shape))
                {
                    LOG_ERROR("grassland: 走行体の形を追加できませんでした");
                    return false;
                }
            }
            const NorvesLib::Math::Vector3 forward = NorvesLib::Core::Input::FreeFlyCameraController::ForwardFromAngles(pose.Yaw, 0.0f);
            data.m_RunCenter = NorvesLib::Math::Vector3(pose.Position[0], 0.0f, pose.Position[2]) + forward * kRunCenterDistance;
            // 最初はカメラに近い円周上（前方 8 m）に置く。
            data.m_RunAngle = std::atan2(-forward.z, -forward.x);
            UpdateRunningBody(data, 0.0f);
            return true;
        }

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
            if (data.m_TerrainFarPlane > 0.0f)
            {
                camera->SetFarPlane(std::fmax(camera->GetFarPlane(), data.m_TerrainFarPlane));
            }
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
            component->SetLayerTablePath("TerrainSpecs/Grassland/GrasslandLayers.json");
            component->SetTilePathPrefix("Assets/Terrain/Grassland/Tiles/GrasslandTile_");
            data.m_pTerrain = component;
#if NORVES_BUILD_DEVELOPMENT
            if (data.m_bStreaming)
            {
                component->SetTileStreaming(true);
            }
#endif
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

#if NORVES_BUILD_DEVELOPMENT
        constexpr Streaming::StreamingScopeId kGrasslandStreamingScopeId = 0x47524153534c414eULL;

        void StopGrasslandStreaming(GameModeContext& ctx, GrasslandData& data)
        {
            auto& system = ctx.EngineRef.GetStreamingSystem();
            if (data.m_bStreamingManifestLoaded)
            {
                system.StopAndJoin(kGrasslandStreamingScopeId);
                data.m_bStreamingManifestLoaded = false;
            }
            if (data.m_TerrainHandler)
            {
                data.m_TerrainHandler->UnbindSite("grassland");
            }
            if (data.m_bScatterHandlerRegistered)
            {
                system.UnregisterHandler(data.m_ScatterHandler.get());
                data.m_bScatterHandlerRegistered = false;
            }
            if (data.m_bTerrainHandlerRegistered)
            {
                system.UnregisterHandler(data.m_TerrainHandler.get());
                data.m_bTerrainHandlerRegistered = false;
            }
            data.m_ScatterHandler.reset();
            data.m_TerrainHandler.reset();
            if (data.m_bStreamingSettingsChanged)
            {
                system.SetSettings(data.m_PreviousStreamingSettings);
                data.m_bStreamingSettingsChanged = false;
            }
            data.m_StreamingManifest = Streaming::CellManifest{};
            data.m_bPreviousStreamingFocus = false;
        }

        bool StartGrasslandStreaming(GameModeContext& ctx, GrasslandData& data)
        {
            auto& scatter = ctx.EngineRef.GetScatterSystem();
            if (data.m_Scatter == Scatter::kInvalidScatterHandle ||
                !scatter.SetScatterName(data.m_Scatter, "grassland") ||
                !scatter.SetDistanceResidency(data.m_Scatter, false))
            {
                LOG_ERROR("GRASSLAND_STREAMING 散布の登録または常駐設定に失敗しました");
                return false;
            }

            Asset::AssetReadRequest request;
            request.InputPath = "World/Grassland.cells.json";
            request.bAllowAbsolutePath = false;
            request.MaxReadBytes = 1024u * 1024u;
            const auto file = Asset::AssetFileReader().Read(request);
            if (!file.Succeeded())
            {
                LOG_ERROR("GRASSLAND_STREAMING status=%u reason=%s セルのマニフェストを読めません",
                          static_cast<unsigned>(file.Status), file.Reason.c_str());
                return false;
            }
            const uint8_t* bytes = file.Blob.GetData();
            size_t size = file.Blob.GetSize();
            if (size >= 3 && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu)
            {
                bytes += 3;
                size -= 3;
            }
            Container::String text;
            if (size != 0)
            {
                text.append(reinterpret_cast<const char*>(bytes), size);
            }
            const char* kinds[] = {Streaming::kTerrainTilePayloadKind, Streaming::kScatterChunkPayloadKind,
                                   Streaming::kStaticCollidersPayloadKind};
            Streaming::CellManifestError error;
            if (!Streaming::CellManifest::TryParse(text, {kinds, 3}, data.m_StreamingManifest, &error))
            {
                LOG_ERROR("GRASSLAND_STREAMING error=%s cell=%d payload=%d %s",
                          Streaming::ToString(error.Code), error.CellIndex, error.PayloadIndex, error.Message.c_str());
                return false;
            }

            auto& system = ctx.EngineRef.GetStreamingSystem();
            data.m_TerrainHandler = Container::MakeUnique<Streaming::TerrainTilePayloadHandler>(ctx.EngineRef.GetTerrainSystem());
            data.m_ScatterHandler = Container::MakeUnique<Streaming::ScatterChunkPayloadHandler>(scatter);
            if (!data.m_TerrainHandler->BindSite("grassland", &data.m_pTerrain->GetTileSite()))
            {
                LOG_ERROR("GRASSLAND_STREAMING 地形の配置先を束ねられません");
                return false;
            }
            if (!system.RegisterHandler(data.m_TerrainHandler.get()))
            {
                LOG_ERROR("GRASSLAND_STREAMING 地形のハンドラーを登録できません");
                return false;
            }
            data.m_bTerrainHandlerRegistered = true;
            if (!system.RegisterHandler(data.m_ScatterHandler.get()))
            {
                LOG_ERROR("GRASSLAND_STREAMING 散布のハンドラーを登録できません");
                return false;
            }
            data.m_bScatterHandlerRegistered = true;

            data.m_PreviousStreamingSettings = system.GetSettings();
            Streaming::StreamingSystemSettings settings;
            settings.Ring.ActiveRadius = 192.0f;
            settings.Ring.DormantRadius = 320.0f;
            settings.Ring.UnloadRadius = 448.0f;
            if (!system.SetSettings(settings))
            {
                LOG_ERROR("GRASSLAND_STREAMING セルの距離を設定できません");
                return false;
            }
            data.m_bStreamingSettingsChanged = true;
            if (!system.LoadManifestFromText(text, &error, kGrasslandStreamingScopeId))
            {
                LOG_ERROR("GRASSLAND_STREAMING error=%s cell=%d payload=%d %s",
                          Streaming::ToString(error.Code), error.CellIndex, error.PayloadIndex, error.Message.c_str());
                return false;
            }
            data.m_bStreamingManifestLoaded = true;
            ctx.ScopeRef.TrackStreaming(system, kGrasslandStreamingScopeId);
            if (const auto* camera = data.m_Rig.GetCamera())
            {
                if (const auto* owner = camera->GetOwner())
                {
                    system.SetFocus({owner->GetPosition(), Math::Vector3::Zero});
                }
            }
            data.m_bPreviousStreamingFocus = false;
            LOG_INFO("GRASSLAND_STREAMING cells=%u セルの流し込みを開始します",
                     static_cast<unsigned>(data.m_StreamingManifest.GetCells().size()));
            return true;
        }
#endif

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
            ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(0.0f);
        }
    } // namespace

    GameModeEnterResult GrasslandRoutine::Enter(GameModeContext& ctx, GrasslandData& data)
    {
        LOG_INFO("grassland を開始します");
        data.m_TickCount = 0;
        data.m_pRunningBody = nullptr;
        data.m_RunAngle = 0.0f;
        data.m_pTerrain = nullptr;
        data.m_TerrainFarPlane = 0.0f;
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
        if (!CreateTerrain(ctx, data) || !CreateGrassField(ctx) || !CreateRunningBody(ctx, data, pose))
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
        if (data.m_bStreaming && !StartGrasslandStreaming(ctx, data))
        {
            StopGrasslandStreaming(ctx, data);
            ctx.EngineRef.GetScatterSystem().Unload(data.m_Scatter);
            data.m_Scatter = Scatter::kInvalidScatterHandle;
            data.m_Rig.Leave(ctx);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_ViewInput.OnInputReset();
        ctx.EngineRef.GetInputRouter().RegisterController(&data.m_ViewInput, I::InputRouter::PriorityGame);
        data.m_bViewInputRegistered = true;
#endif
        return GameModeEnterResult::Succeeded;
    }

    void GrasslandRoutine::Tick(GameModeContext& ctx, GrasslandData& data, float deltaTime)
    {
        ++data.m_TickCount;
        if (data.m_TerrainFarPlane == 0.0f && data.m_pTerrain != nullptr)
        {
            float minX = 0.0f;
            float minZ = 0.0f;
            float maxX = 0.0f;
            float maxZ = 0.0f;
            if (data.m_pTerrain->GetTerrainBoundsXZ(minX, minZ, maxX, maxZ))
            {
                const float width = maxX - minX;
                const float depth = maxZ - minZ;
                const float diagonal = std::sqrt(width * width + depth * depth);
                // 地形内のどの視点からも端まで描き、影も地形全体に落とす。
                auto* camera = data.m_Rig.GetCamera();
                data.m_TerrainFarPlane = std::fmax(camera->GetFarPlane(), diagonal * 1.1f);
                camera->SetFarPlane(data.m_TerrainFarPlane);
#if NORVES_BUILD_DEVELOPMENT
                if (data.m_pHeroCamera != nullptr)
                {
                    data.m_pHeroCamera->SetFarPlane(std::fmax(data.m_pHeroCamera->GetFarPlane(), data.m_TerrainFarPlane));
                }
#endif
                ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(diagonal);
                LOG_INFO("GRASSLAND_TERRAIN_EXTENT min=(%.3f,%.3f) max=(%.3f,%.3f) far_m=%.3f shadow_m=%.3f",
                         static_cast<double>(minX), static_cast<double>(minZ), static_cast<double>(maxX),
                         static_cast<double>(maxZ), static_cast<double>(data.m_TerrainFarPlane), static_cast<double>(diagonal));
            }
        }
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
        UpdateRunningBody(data, bSuspended ? 0.0f : deltaTime);
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        // 通常は全区画、流し込みではカメラを含むセルが揃うまで準備完了を待つ。
        bool bTerrainReady = data.m_pTerrain != nullptr && data.m_pTerrain->IsFullyPlaced() &&
                             data.m_pTerrain->GetPlacedTileCount() == data.m_pTerrain->GetTileCount();
        bool bCanAttachTerrainCollider = bTerrainReady;
#if NORVES_BUILD_DEVELOPMENT
        if (data.m_bStreaming)
        {
            bCanAttachTerrainCollider = data.m_pTerrain != nullptr && data.m_pTerrain->GetHeightField() != nullptr;
            bTerrainReady = false;
            const Component::CameraComponent* camera = data.m_bHeroView ? data.m_pHeroCamera : data.m_Rig.GetCamera();
            if (camera != nullptr && camera->GetOwner() != nullptr)
            {
                const Math::Vector3 position = camera->GetOwner()->GetPosition();
                Math::Vector3 velocity = Math::Vector3::Zero;
                if (data.m_bPreviousStreamingFocus && std::isfinite(deltaTime) && deltaTime > 0.0f)
                {
                    velocity = (position - data.m_PreviousStreamingFocus) / deltaTime;
                }
                data.m_PreviousStreamingFocus = position;
                data.m_bPreviousStreamingFocus = true;
                auto& system = ctx.EngineRef.GetStreamingSystem();
                system.SetFocus({position, velocity});
                for (const auto& cell : data.m_StreamingManifest.GetCells())
                {
                    if (Streaming::ContainsHorizontalPoint(cell.Bounds, position.x, position.z))
                    {
                        bTerrainReady = bCanAttachTerrainCollider &&
                            system.GetCellState(cell.Id) == Streaming::EStreamingCellState::Active;
                        break;
                    }
                }
            }
        }
#endif
        if (bCanAttachTerrainCollider && !data.m_bTerrainColliderAttempted)
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
#if NORVES_BUILD_DEVELOPMENT
        if (data.m_bStreaming)
        {
            StopGrasslandStreaming(ctx, data);
        }
#endif
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
        if (data.m_pRunningBody != nullptr)
        {
            // 除去が予約になる場合も、その場で形を空にして次のシーンへ持ち越さない。
            if (auto* component = data.m_pRunningBody->GetComponent<Component::WorldFieldInteractorComponent>())
            {
                component->ClearShapes();
            }
            ctx.ScopeRef.Untrack(data.m_pRunningBody);
            ctx.WorldRef.RemoveObject(data.m_pRunningBody);
            data.m_pRunningBody = nullptr;
        }
        data.m_RunAngle = 0.0f;
        data.m_pTerrain = nullptr;
        data.m_TerrainFarPlane = 0.0f;
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        // 地形・草の場・カメラの Entity は GameModeScope::Cleanup が返す。各 Component の EndPlay で登録を返す。
        // 空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("grassland を終了します");
    }

} // namespace Game::GameModes
