// 草原の谷と尾根を、既定の 1 層の材質・物理空・太陽で描く開発用シーン。
// 地形の出どころは GrasslandTerrain.json と GenerateGrasslandHeight.py。クック済みの高さ場と 4×4 の区画を使う。
// near は地表から 0.6 m、valley は谷底から尾根、low は谷沿いを低い角度で見る固定視点。
#include "GameModes/Grassland/GrasslandMode.h"
#include "GameModes/Grassland/GrasslandScatter.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/GrassFieldComponent.h"
#include "Core/Public/Component/TerrainComponent.h"
#include "Core/Public/Component/WorldFieldInteractorComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
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

namespace Game::GameModes
{

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
        data.m_pRunningBody = nullptr;
        data.m_RunAngle = 0.0f;
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
        return GameModeEnterResult::Succeeded;
    }

    void GrasslandRoutine::Tick(GameModeContext& ctx, GrasslandData& data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kGrasslandSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        UpdateRunningBody(data, bSuspended ? 0.0f : deltaTime);
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
        if (Component::CameraComponent* camera = data.m_Rig.GetCamera())
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
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        // 地形・草の場・カメラの Entity は GameModeScope::Cleanup が返す。各 Component の EndPlay で登録を返す。
        // 空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("grassland を終了します");
    }

} // namespace Game::GameModes
