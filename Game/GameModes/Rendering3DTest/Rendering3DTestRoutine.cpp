#include "Rendering3DTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/LightComponent.h"
#include "Core/Public/Component/PointLightComponent.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderResourceContexts.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Input/InputState.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Rendering/SceneView.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/MegaGeometryPass.h"
#include "Core/Public/Resource/GLTFAnalyzer.h"

#include "Core/Public/Math/Matrix4x4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"

#include <cmath>

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
using namespace NorvesLib::Core::Engine;
using namespace NorvesLib::Core;

namespace Game::GameModes
{

    GameModeEnterResult Rendering3DTestRoutine::Enter(GameModeContext &ctx, Rendering3DTestData &data)
    {
        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト開始");
        LOG_INFO("=================================================");

        // ========================================
        // カメラコントローラーの初期化（シーン所有）
        // ========================================
        // 原点を注視点とし、距離5.0、Yaw=0°、Pitch=30°で初期化
        // （MayaCameraController は入力→SpringArmIntent の感度換算ロジックのみ使用）
        data.m_CameraController.Initialize(
            NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), // target
            5.0f,                                       // distance
            0.0f,                                       // yaw
            30.0f);                                     // pitch
        LOG_INFO("MayaCameraController initialized");

        // ========================================
        // カメラ用 WorldObject の構築
        // （SpringArmComponent + CameraComponent による新カメラ経路）
        // ========================================
        {
            auto &world = ctx.WorldRef;

            // ピボット（注視対象）オブジェクト：旧 Initialize の target と同じ原点
            data.m_pPivotObject = world.SpawnObject<WorldObject>();
            data.m_pPivotObject->SetPosition(0.0f, 0.0f, 0.0f);

            // カメラ本体オブジェクト：SpringArm が毎フレーム位置・回転を上書きする
            data.m_pCameraObject = world.SpawnObject<WorldObject>();

            // SpringArmComponent：ピボットを中心とした球面アームを駆動
            data.m_pSpringArm = world.CreateComponent<Component::SpringArmComponent>(data.m_pCameraObject);
            data.m_pSpringArm->SetPivot(data.m_pPivotObject); // ObjectId を保持（ポインタ非所有）
            data.m_pSpringArm->SetArmLength(5.0f);             // 旧 distance=5.0 と一致
            data.m_pSpringArm->SetYaw(0.0f);                   // 旧 yaw=0 と一致
            data.m_pSpringArm->SetPitch(30.0f);                // 旧 pitch=30 と一致

            // CameraComponent：レンズ設定を保持しアクティブカメラとして登録
            data.m_pCameraComponent = world.CreateComponent<Component::CameraComponent>(data.m_pCameraObject);
            data.m_pCameraComponent->SetActiveCamera(true);

            // 初期フレームでカメラ位置を確定させる
            // （World::Tick が SpringArmComponent::Tick を毎フレーム駆動するが、Enter 直後の
            //  整合のために、Component::Tick を直呼びせず公開 API RefreshOwnerTransform() で
            //  カメラ姿勢を一度だけ初期確定する）
            data.m_pSpringArm->RefreshOwnerTransform();

            LOG_INFO("Camera (SpringArmComponent + CameraComponent) initialized");
        }

        // ========================================
        // 1. プロシージャルメッシュの生成とGPU登録
        // ========================================
        {
            auto &meshes = ctx.RenderResourcesRef.Meshes();

            // 球体メッシュの生成
            VariableArray<Mesh3DVertex> sphereVertices;
            VariableArray<uint32_t> sphereIndices;
            ProceduralMeshGenerator::GenerateUVSphere(1.0f, 32, 16, sphereVertices, sphereIndices);

            bool bSphereOk = meshes.Register(
                data.m_SphereMeshHandle,
                sphereVertices.data(),
                static_cast<uint32_t>(sphereVertices.size() * sizeof(Mesh3DVertex)),
                sphereIndices.data(),
                static_cast<uint32_t>(sphereIndices.size()));

            if (bSphereOk)
            {
                NORVES_LOG_INFO("Rendering3DTest", "Sphere mesh registered: %zu vertices, %zu indices",
                                sphereVertices.size(), sphereIndices.size());
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to register sphere mesh");
            }

            // 地面メッシュの生成（10x10のPlane）
            VariableArray<Mesh3DVertex> groundVertices;
            VariableArray<uint32_t> groundIndices;
            ProceduralMeshGenerator::GeneratePlane(10.0f, 10.0f, 4, 4, groundVertices, groundIndices);

            bool bGroundOk = meshes.Register(
                data.m_GroundMeshHandle,
                groundVertices.data(),
                static_cast<uint32_t>(groundVertices.size() * sizeof(Mesh3DVertex)),
                groundIndices.data(),
                static_cast<uint32_t>(groundIndices.size()));

            if (bGroundOk)
            {
                NORVES_LOG_INFO("Rendering3DTest", "Ground mesh registered: %zu vertices, %zu indices",
                                groundVertices.size(), groundIndices.size());
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to register ground mesh");
            }

            data.m_bMeshesRegistered = bSphereOk && bGroundOk;

            // ポイントライト光源球体メッシュの生成（小さい球体: 半径0.15）
            VariableArray<Mesh3DVertex> lightSphereVertices;
            VariableArray<uint32_t> lightSphereIndices;
            ProceduralMeshGenerator::GenerateUVSphere(0.15f, 16, 8, lightSphereVertices, lightSphereIndices);

            bool bLightSphereOk = meshes.Register(
                data.m_LightSphereMeshHandle,
                lightSphereVertices.data(),
                static_cast<uint32_t>(lightSphereVertices.size() * sizeof(Mesh3DVertex)),
                lightSphereIndices.data(),
                static_cast<uint32_t>(lightSphereIndices.size()));

            if (bLightSphereOk)
            {
                NORVES_LOG_INFO("Rendering3DTest", "Light sphere mesh registered: %zu vertices, %zu indices",
                                lightSphereVertices.size(), lightSphereIndices.size());
            }

            data.m_bMeshesRegistered = bSphereOk && bGroundOk && bLightSphereOk;
        }

        // ========================================
        // 1.5 プロシージャルチェッカーボードテクスチャ生成
        // ========================================
        {
            auto &textures = ctx.RenderResourcesRef.Textures();

            constexpr uint32_t TEX_SIZE = 256;
            constexpr uint32_t CHECKER_SIZE = 32; // 32ピクセルごとに色が切り替わる
            VariableArray<uint8_t> checkerData(TEX_SIZE * TEX_SIZE * 4);

            for (uint32_t y = 0; y < TEX_SIZE; ++y)
            {
                for (uint32_t x = 0; x < TEX_SIZE; ++x)
                {
                    uint32_t idx = (y * TEX_SIZE + x) * 4;
                    bool bWhite = ((x / CHECKER_SIZE) + (y / CHECKER_SIZE)) % 2 == 0;
                    uint8_t c = bWhite ? 230 : 50;
                    checkerData[idx + 0] = c;
                    checkerData[idx + 1] = c;
                    checkerData[idx + 2] = c;
                    checkerData[idx + 3] = 255;
                }
            }

            TextureCreateInfo texInfo;
            texInfo.Width = TEX_SIZE;
            texInfo.Height = TEX_SIZE;
            texInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            texInfo.DebugName = "CheckerboardTexture";

            data.m_CheckerTextureHandle = textures.CreateTexture(
                texInfo, checkerData.data(), static_cast<uint32_t>(checkerData.size()));

            if (data.m_CheckerTextureHandle.IsValid())
            {
                NORVES_LOG_INFO("Rendering3DTest", "Checkerboard texture created (256x256)");
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to create checkerboard texture");
            }
        }

        // ========================================
        // 1.6 マテリアルの作成（テクスチャは非同期読み込み）
        // ========================================
        {
            auto &renderResources = ctx.RenderResourcesRef;
            auto &textures = renderResources.Textures();
            auto &materials = renderResources.Materials();

            // --- Silver PBRマテリアル（テクスチャなしで先に作成、後で差し替え） ---
            {
                MaterialCreateData silverMatInfo;
                silverMatInfo.DebugName = "SilverPBR";
                data.m_SilverMaterial = materials.Create(silverMatInfo);

                // テクスチャの非同期読み込みリクエスト
                auto silverUpdate = MakeShared<PendingMaterialUpdate>();
                silverUpdate->TargetMaterial = data.m_SilverMaterial;
                silverUpdate->CreateData = silverMatInfo;
                silverUpdate->PendingTextureCount = 5;

                textures.LoadTextureAsync("Assets/Textures/Silver/silver_albedo.png",
                                                 [silverUpdate, &materials](TextureHandle handle)
                                                 {
                                                     silverUpdate->CreateData.AlbedoTexture = handle;
                                                     if (--silverUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/Silver/silver_normal-ogl.png",
                                                 [silverUpdate, &materials](TextureHandle handle)
                                                 {
                                                     silverUpdate->CreateData.NormalTexture = handle;
                                                     if (--silverUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/Silver/silver_metallic.png",
                                                 [silverUpdate, &materials](TextureHandle handle)
                                                 {
                                                     silverUpdate->CreateData.MetallicTexture = handle;
                                                     if (--silverUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/Silver/silver_roughness.png",
                                                 [silverUpdate, &materials](TextureHandle handle)
                                                 {
                                                     silverUpdate->CreateData.RoughnessTexture = handle;
                                                     if (--silverUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/Silver/silver_ao.png",
                                                 [silverUpdate, &materials](TextureHandle handle)
                                                 {
                                                     silverUpdate->CreateData.AOTexture = handle;
                                                     if (--silverUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                                     }
                                                 });

                data.m_PendingMaterialUpdates.push_back(silverUpdate);
                NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material created (textures loading async)");
            }

            // --- CobbleStoneFloor（石畳）マテリアル（テクスチャなしで先に作成） ---
            {
                MaterialCreateData cobbleMatInfo;
                cobbleMatInfo.HeightScale = 0.05f;
                cobbleMatInfo.DebugName = "CobbleStoneFloor";
                data.m_CobbleStoneMaterial = materials.Create(cobbleMatInfo);

                auto cobbleUpdate = MakeShared<PendingMaterialUpdate>();
                cobbleUpdate->TargetMaterial = data.m_CobbleStoneMaterial;
                cobbleUpdate->CreateData = cobbleMatInfo;
                cobbleUpdate->PendingTextureCount = 5;

                textures.LoadTextureAsync("Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_diff_4k.png",
                                                 [cobbleUpdate, &materials](TextureHandle handle)
                                                 {
                                                     cobbleUpdate->CreateData.AlbedoTexture = handle;
                                                     if (--cobbleUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_nor_gl_4k.png",
                                                 [cobbleUpdate, &materials](TextureHandle handle)
                                                 {
                                                     cobbleUpdate->CreateData.NormalTexture = handle;
                                                     if (--cobbleUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_rough_4k.png",
                                                 [cobbleUpdate, &materials](TextureHandle handle)
                                                 {
                                                     cobbleUpdate->CreateData.RoughnessTexture = handle;
                                                     if (--cobbleUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_ao_4k.png",
                                                 [cobbleUpdate, &materials](TextureHandle handle)
                                                 {
                                                     cobbleUpdate->CreateData.AOTexture = handle;
                                                     if (--cobbleUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                                                     }
                                                 });
                textures.LoadTextureAsync("Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png",
                                                 [cobbleUpdate, &materials](TextureHandle handle)
                                                 {
                                                     cobbleUpdate->CreateData.HeightTexture = handle;
                                                     if (--cobbleUpdate->PendingTextureCount == 0)
                                                     {
                                                         materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                                                         NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                                                     }
                                                 });

                data.m_PendingMaterialUpdates.push_back(cobbleUpdate);
                NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material created (textures loading async)");
            }

            // 地面マテリアル作成（チェッカーボードは同期で作成済み）
            MaterialCreateData groundMatInfo;
            groundMatInfo.AlbedoTexture = data.m_CheckerTextureHandle;
            groundMatInfo.DebugName = "Ground";
            data.m_GroundMaterial = materials.Create(groundMatInfo);

            // 光源球体マテリアル作成（エミッシブ、テクスチャ不要）
            MaterialCreateData lightSphereMatInfo;
            lightSphereMatInfo.EmissiveColor[0] = 1.0f;
            lightSphereMatInfo.EmissiveColor[1] = 0.9f;
            lightSphereMatInfo.EmissiveColor[2] = 0.3f;
            lightSphereMatInfo.EmissiveStrength = 8.0f;
            lightSphereMatInfo.DebugName = "LightSphere";
            data.m_LightSphereMaterial = materials.Create(lightSphereMatInfo);
        }

        // ========================================
        // 2. WorldObjectの作成とメッシュコンポーネントの追加
        // ========================================
        {
            auto &world = ctx.WorldRef;

            // --- 球体オブジェクト ---
            data.m_pSphereObject = world.SpawnObject<WorldObject>();

            // 球体を地面の上に配置（半径1.0 + 地面Y=-1.0 → Y=0.5で浮かせる）
            data.m_pSphereObject->SetPosition(0.0f, 0.5f, 0.0f);

            data.m_pSphereMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pSphereObject);
            data.m_pSphereMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pSphereMeshComponent->SetCastShadow(true);
            // オブジェクトカラー（白 = テクスチャカラーをそのまま使用）
            data.m_pSphereMeshComponent->SetCustomData(0, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(1, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(2, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(3, 1.0f);
            // CobbleStoneFloor（石畳）マテリアルを適用
            data.m_pSphereMeshComponent->SetMaterial(0, data.m_CobbleStoneMaterial);

            LOG_INFO("Sphere WorldObject created and added to World");

            // --- 地面オブジェクト ---
            data.m_pGroundObject = world.SpawnObject<WorldObject>();

            // 地面をY=-1.0に配置
            data.m_pGroundObject->SetPosition(0.0f, -1.0f, 0.0f);

            data.m_pGroundMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pGroundObject);
            data.m_pGroundMeshComponent->SetMeshHandle(data.m_GroundMeshHandle);
            data.m_pGroundMeshComponent->SetCastShadow(false);
            // オブジェクトカラー（暗い緑灰色）→ CustomData
            data.m_pGroundMeshComponent->SetCustomData(0, 0.35f);
            data.m_pGroundMeshComponent->SetCustomData(1, 0.45f);
            data.m_pGroundMeshComponent->SetCustomData(2, 0.3f);
            data.m_pGroundMeshComponent->SetCustomData(3, 1.0f);
            // 地面マテリアルを適用
            data.m_pGroundMeshComponent->SetMaterial(0, data.m_GroundMaterial);

            LOG_INFO("Ground WorldObject created and added to World");

            // --- ポイントライト光源球体オブジェクト ---
            data.m_pLightSphereObject = world.SpawnObject<WorldObject>();

            // 球体の横に配置（X=4.0, Y=1.0, Z=0.0）――少し遠め
            data.m_pLightSphereObject->SetPosition(4.0f, 1.0f, 0.0f);

            data.m_pLightSphereMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pLightSphereObject);
            data.m_pLightSphereMeshComponent->SetMeshHandle(data.m_LightSphereMeshHandle);
            data.m_pLightSphereMeshComponent->SetCastShadow(false); // 光源自体は影を落とさない
            // 明るい黄色（発光体の見た目）
            data.m_pLightSphereMeshComponent->SetCustomData(0, 1.0f);
            data.m_pLightSphereMeshComponent->SetCustomData(1, 0.9f);
            data.m_pLightSphereMeshComponent->SetCustomData(2, 0.3f);
            data.m_pLightSphereMeshComponent->SetCustomData(3, 1.0f);
            // 光源球体マテリアル（エミッシブ設定はマテリアル側に移動済み）
            data.m_pLightSphereMeshComponent->SetMaterial(0, data.m_LightSphereMaterial);

            // PointLightComponentの追加（SceneViewへのLightProxy登録はWorld::SyncToSceneView()で自動化）
            data.m_pPointLightComponent = world.CreateComponent<Component::PointLightComponent>(data.m_pLightSphereObject);
            data.m_pPointLightComponent->SetLightColor(1.0f, 0.9f, 0.3f);
            data.m_pPointLightComponent->SetIntensity(2.0f);
            data.m_pPointLightComponent->SetRange(10.0f);
            data.m_pPointLightComponent->SetLightVisible(true);
            data.m_pPointLightComponent->SetCastShadows(false);
            LOG_INFO("Light sphere WorldObject created and added to World");

            // --- ディレクショナルライト（シャドウ方向と一致） ---
            data.m_pDirectionalLightObject = world.SpawnObject<WorldObject>();
            data.m_pDirectionalLightObject->SetPosition(0.0f, 0.0f, 0.0f);

            data.m_pDirectionalLightComponent = world.CreateComponent<Component::LightComponent>(data.m_pDirectionalLightObject);
            // LightComponentはデフォルトでDirectional型
            data.m_pDirectionalLightComponent->SetLightColor(1.0f, 1.0f, 1.0f);
            data.m_pDirectionalLightComponent->SetIntensity(1.0f);
            data.m_pDirectionalLightComponent->SetLightDirection(-0.577f, -0.577f, -0.577f);
            data.m_pDirectionalLightComponent->SetLightVisible(true);
            data.m_pDirectionalLightComponent->SetCastShadows(true);
            LOG_INFO("Directional light created and added to World");
        }

        // ========================================
        // 3. glTFモデルのロード
        // ========================================
        {
            auto& world = ctx.WorldRef;
            auto &renderResources = ctx.RenderResourcesRef;
            ModelLoadResourceContext modelLoadContext{
                renderResources.Textures(),
                renderResources.MegaGeometry()};

            // 非同期ロード中に表示する簡易プレースホルダ
            data.m_pBoulderPlaceholderObject = world.SpawnObject<WorldObject>();
            data.m_pBoulderPlaceholderObject->SetPosition(3.0f, 0.5f, 0.0f);
            data.m_pBoulderPlaceholderObject->SetScale(0.75f, 0.75f, 0.75f);

            data.m_pBoulderPlaceholderMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pBoulderPlaceholderObject);
            data.m_pBoulderPlaceholderMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pBoulderPlaceholderMeshComponent->SetMaterial(0, data.m_SilverMaterial);
            data.m_pBoulderPlaceholderMeshComponent->SetCastShadow(true);

            data.m_bBoulderModelLoaded = false;
            data.m_bBoulderModelLoadPending = true;

            auto asyncState = MakeShared<BoulderAsyncState>();
            data.m_BoulderAsyncState = asyncState;

            const String modelPath = data.m_ModelPath.empty()
                                         ? String("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf")
                                         : data.m_ModelPath;
            data.m_BoulderLoadRequestId = Resource::GLTFAnalyzer::LoadModelAsync(
                modelPath,
                modelLoadContext,
                [asyncState](ModelHandle handle)
                {
                    // TODO(Phase2): per-request の cancel+release API が無いため、cancelled
                    //   済みで到着した有効ハンドルはここで解放できない（mode 遷移時にリーク
                    //   の可能性）。現状は use-after-free 防止のみを保証する。
                    asyncState->m_Handle = handle;
                    asyncState->m_bLoaded = handle.IsValid();
                    asyncState->m_bCompleted.Store(true);
                });

            if (data.m_BoulderLoadRequestId == 0)
            {
                data.m_bBoulderModelLoadPending = false;
                asyncState->m_bLoaded = false;
                asyncState->m_bCompleted.Store(true);
                NORVES_LOG_ERROR("Rendering3DTest", "Boulderモデルの非同期ロード開始に失敗しました");
            }
            else
            {
                NORVES_LOG_INFO("Rendering3DTest", "Boulder model async load started: %s", modelPath.c_str());
            }
        }

        return GameModeEnterResult::Succeeded;
    }

    void Rendering3DTestRoutine::Tick(GameModeContext &ctx, Rendering3DTestData &data, float deltaTime)
    {
        // ========================================
        // 入力に基づくカメラ意図の注入
        // （SpringArmComponent::Tick は World::Tick が毎フレーム自動駆動するため
        //  GameMode 側では ApplyIntent のみ呼ぶ。SetMainCamera は不要）
        // ========================================
        const auto &inputState = ctx.InputRef.GetState();

        if (data.m_pSpringArm)
        {
            Component::SpringArmIntent intent = data.m_CameraController.BuildIntent(
                inputState, deltaTime, data.m_pSpringArm->GetArmLength());
            data.m_pSpringArm->ApplyIntent(intent);

            // デバッグ: スクロール値とカメラ距離を出力
            float scroll = inputState.GetMouseState().ScrollDelta;
            if (std::abs(scroll) > 0.0f)
            {
                float armLength = data.m_pSpringArm->GetArmLength();
                auto camPos = data.m_pCameraObject ? data.m_pCameraObject->GetPosition()
                                                   : NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f);
                NORVES_LOG_DEBUG("Input", "ScrollDelta={:.3f}, ArmLength={:.3f}, CamPos=({:.2f}, {:.2f}, {:.2f})",
                                 scroll, armLength, camPos.x, camPos.y, camPos.z);
            }
        }

        data.m_ElapsedTime += deltaTime;

        if (data.m_BoulderAsyncState &&
            data.m_BoulderAsyncState->m_bCompleted.Load() &&
            !data.m_BoulderAsyncState->m_bCancelled.Load())
        {
            auto state = data.m_BoulderAsyncState;
            data.m_BoulderAsyncState.reset(); // 一度だけ消費

            data.m_BoulderModelHandle = state->m_Handle;
            data.m_bBoulderModelLoaded = state->m_bLoaded;
            data.m_bBoulderModelLoadPending = false;

            if (!data.m_bBoulderModelLoaded)
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Boulderモデルのロードに失敗しました");
            }
            else
            {
                auto &world = ctx.WorldRef;
                auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
                auto megaMeshHandle = megaGeometry.GetModelMegaMeshHandle(data.m_BoulderModelHandle);
                if (!megaMeshHandle.IsValid())
                {
                    NORVES_LOG_ERROR("Rendering3DTest", "BoulderモデルからMegaMeshを取得できませんでした");
                    megaGeometry.ReleaseModel(data.m_BoulderModelHandle);
                    data.m_BoulderModelHandle = ModelHandle::Invalid();
                    data.m_bBoulderModelLoaded = false;
                }
                else
                {
                    if (data.m_pBoulderPlaceholderObject)
                    {
                        world.RemoveObject(data.m_pBoulderPlaceholderObject);
                        data.m_pBoulderPlaceholderObject = nullptr;
                        data.m_pBoulderPlaceholderMeshComponent = nullptr;
                    }

                    if (!data.m_pBoulderObject)
                    {
                        data.m_pBoulderObject = world.SpawnObject<WorldObject>();
                        data.m_pBoulderObject->SetPosition(3.0f, 0.0f, 0.0f);

                        data.m_pBoulderMegaGeometryComponent = world.CreateComponent<Component::MegaGeometryComponent>(data.m_pBoulderObject);
                        data.m_pBoulderMegaGeometryComponent->SetMegaMeshHandle(megaMeshHandle);
                        data.m_pBoulderMegaGeometryComponent->SetCastShadow(true);
                    }

                    NORVES_LOG_INFO("Rendering3DTest", "Boulder model loaded and added to World");
                }
            }
        }

        // 球体をY軸回転させる
        if (data.m_pSphereObject)
        {
            float angle = data.m_ElapsedTime * data.m_RotationSpeed;
            NorvesLib::Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            NorvesLib::Math::Quaternion rotation(yAxis, angle);
            data.m_pSphereObject->SetRotation(rotation);
        }

    }

    void Rendering3DTestRoutine::Leave(GameModeContext &ctx, Rendering3DTestData &data, GameModeExitReason reason)
    {
        (void)reason;

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト終了");
        LOG_INFO("=================================================");

        auto &world = ctx.WorldRef;

        // 1) 非同期ロードのキャンセル：以降到着する callback の結果を破棄する。
        //    callback は asyncState を共有所有しているため、ここで reset しても
        //    callback 完了まで状態は生存し use-after-free にはならない。
        if (data.m_BoulderAsyncState)
        {
            data.m_BoulderAsyncState->m_bCancelled.Store(true);
            data.m_BoulderAsyncState.reset();
        }

        // 2) WorldObject を先に削除（SceneView Proxy を同期除去）。
        //    Proxy が参照する mesh/model ハンドルを解放する前に Proxy を消すことで
        //    stale ハンドル参照を避ける。placeholder と boulder 本体は排他なので
        //    各ポインタを独立に null チェックする。
        if (data.m_pSphereObject)
        {
            world.RemoveObject(data.m_pSphereObject);
            data.m_pSphereObject = nullptr;
            data.m_pSphereMeshComponent = nullptr;
        }
        if (data.m_pGroundObject)
        {
            world.RemoveObject(data.m_pGroundObject);
            data.m_pGroundObject = nullptr;
            data.m_pGroundMeshComponent = nullptr;
        }
        if (data.m_pLightSphereObject)
        {
            world.RemoveObject(data.m_pLightSphereObject);
            data.m_pLightSphereObject = nullptr;
            data.m_pLightSphereMeshComponent = nullptr;
            data.m_pPointLightComponent = nullptr;
        }
        if (data.m_pBoulderPlaceholderObject)
        {
            world.RemoveObject(data.m_pBoulderPlaceholderObject);
            data.m_pBoulderPlaceholderObject = nullptr;
            data.m_pBoulderPlaceholderMeshComponent = nullptr;
        }
        if (data.m_pBoulderObject)
        {
            world.RemoveObject(data.m_pBoulderObject);
            data.m_pBoulderObject = nullptr;
            data.m_pBoulderMegaGeometryComponent = nullptr;
        }
        if (data.m_pDirectionalLightObject)
        {
            world.RemoveObject(data.m_pDirectionalLightObject);
            data.m_pDirectionalLightObject = nullptr;
            data.m_pDirectionalLightComponent = nullptr;
        }
        // カメラオブジェクトの破棄
        // SpringArmComponent は m_pCameraObject の Inner なので RemoveObject で連鎖破棄される
        if (data.m_pCameraObject)
        {
            world.RemoveObject(data.m_pCameraObject);
            data.m_pCameraObject = nullptr;
            data.m_pSpringArm = nullptr;
            data.m_pCameraComponent = nullptr;
        }
        // ピボットは独立した WorldObject なので別途 RemoveObject する
        if (data.m_pPivotObject)
        {
            world.RemoveObject(data.m_pPivotObject);
            data.m_pPivotObject = nullptr;
        }

        // 3) メッシュの登録解除（Proxy 除去後）
        if (data.m_bMeshesRegistered)
        {
            auto &meshes = ctx.RenderResourcesRef.Meshes();
            meshes.Unregister(data.m_SphereMeshHandle);
            meshes.Unregister(data.m_GroundMeshHandle);
            meshes.Unregister(data.m_LightSphereMeshHandle);
            data.m_bMeshesRegistered = false;
        }

        // 4) glTF モデルの解放（boulder object 削除後）
        if (data.m_bBoulderModelLoaded)
        {
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            megaGeometry.ReleaseModel(data.m_BoulderModelHandle);
            data.m_BoulderModelHandle = ModelHandle::Invalid();
            data.m_bBoulderModelLoaded = false;
        }

        // 5) 残りの状態クリア
        data.m_BoulderLoadRequestId = 0;
        data.m_bBoulderModelLoadPending = false;

        // 非同期マテリアル更新のクリア
        data.m_PendingMaterialUpdates.clear();
    }

} // namespace Game::GameModes
