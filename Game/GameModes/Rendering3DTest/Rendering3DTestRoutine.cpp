#include "Rendering3DTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Component/BoardComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/LightComponent.h"
#include "Core/Public/Component/PointLightComponent.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderResourceContexts.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Input/InputState.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Rendering/SceneView.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Debug/DebugConfig.h"
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
namespace Math = NorvesLib::Math;

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
        data.m_CameraController.Initialize(
            NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), // target
            5.0f,                                       // distance
            0.0f,                                       // yaw
            30.0f);                                     // pitch
        LOG_INFO("MayaCameraController initialized");

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
                ctx.ScopeRef.TrackMesh(data.m_SphereMeshHandle);
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
                ctx.ScopeRef.TrackMesh(data.m_GroundMeshHandle);
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
                ctx.ScopeRef.TrackMesh(data.m_LightSphereMeshHandle);
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
        // 2. Entityの作成とメッシュコンポーネントの追加
        // ========================================
        {
            auto &world = ctx.WorldRef;

            // --- 球体オブジェクト ---
            data.m_pSphereObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pSphereObject);

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

            LOG_INFO("Sphere Entity created and added to World");

            // --- 地面オブジェクト ---
            data.m_pGroundObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pGroundObject);

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

            LOG_INFO("Ground Entity created and added to World");

            // --- ポイントライト光源球体オブジェクト ---
            data.m_pLightSphereObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pLightSphereObject);

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
            LOG_INFO("Light sphere Entity created and added to World");

            // --- ディレクショナルライト（シャドウ方向と一致） ---
            data.m_pDirectionalLightObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pDirectionalLightObject);
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
        // 2.5 F5 ScreenSpace Board showcase
        // ========================================
        {
            auto canvasView = ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().GetCanvasView();
            if (canvasView)
            {
                auto &world = ctx.WorldRef;
                auto &textures = ctx.RenderResourcesRef.Textures();

                constexpr uint32_t atlasWidth = 128;
                constexpr uint32_t atlasHeight = 64;
                constexpr uint32_t atlasCellWidth = 32;
                constexpr uint32_t atlasCellHeight = 32;
                VariableArray<uint8_t> atlasData(atlasWidth * atlasHeight * 4u);
                const Math::Vector4 atlasColors[] =
                {
                    Math::Vector4(0.95f, 0.20f, 0.25f, 1.0f),
                    Math::Vector4(0.20f, 0.80f, 0.35f, 1.0f),
                    Math::Vector4(0.20f, 0.45f, 1.00f, 1.0f),
                    Math::Vector4(0.95f, 0.75f, 0.20f, 1.0f),
                    Math::Vector4(0.80f, 0.25f, 0.95f, 1.0f),
                    Math::Vector4(0.20f, 0.85f, 0.85f, 1.0f),
                    Math::Vector4(1.00f, 0.50f, 0.20f, 1.0f),
                    Math::Vector4(0.90f, 0.90f, 0.90f, 1.0f),
                };

                for (uint32_t y = 0; y < atlasHeight; ++y)
                {
                    for (uint32_t x = 0; x < atlasWidth; ++x)
                    {
                        const uint32_t cellX = x / atlasCellWidth;
                        const uint32_t cellY = y / atlasCellHeight;
                        const uint32_t cellIndex = cellY * (atlasWidth / atlasCellWidth) + cellX;
                        const uint32_t localX = x % atlasCellWidth;
                        const uint32_t localY = y % atlasCellHeight;
                        const float normalizedX = static_cast<float>(localX) / static_cast<float>(atlasCellWidth - 1u);
                        const float normalizedY = static_cast<float>(localY) / static_cast<float>(atlasCellHeight - 1u);
                        const bool bDiagonal = localX >= localY / 2u;
                        const bool bCutout = localX > atlasCellWidth / 3u &&
                                             localX < (atlasCellWidth * 2u) / 3u &&
                                             localY > atlasCellHeight / 4u &&
                                             localY < (atlasCellHeight * 3u) / 4u;
                        const float alphaScale = bCutout ? 0.35f : 1.0f;
                        const Math::Vector4 &cellColor = atlasColors[cellIndex % static_cast<uint32_t>(sizeof(atlasColors) / sizeof(atlasColors[0]))];
                        const float brightness = bDiagonal ? 1.0f : 0.45f;
                        const uint32_t pixelIndex = (y * atlasWidth + x) * 4u;
                        atlasData[pixelIndex + 0u] = static_cast<uint8_t>(255.0f * cellColor.x * brightness);
                        atlasData[pixelIndex + 1u] = static_cast<uint8_t>(255.0f * cellColor.y * (0.7f + 0.3f * normalizedX));
                        atlasData[pixelIndex + 2u] = static_cast<uint8_t>(255.0f * cellColor.z * (0.7f + 0.3f * normalizedY));
                        atlasData[pixelIndex + 3u] = static_cast<uint8_t>(255.0f * alphaScale);
                    }
                }

                TextureCreateInfo atlasInfo;
                atlasInfo.Width = atlasWidth;
                atlasInfo.Height = atlasHeight;
                atlasInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
                atlasInfo.DebugName = "F6BoardAtlas";
                data.m_F6AtlasTextureHandle = textures.CreateTexture(
                    atlasInfo,
                    atlasData.data(),
                    static_cast<uint32_t>(atlasData.size()));

                const VariableArray<Math::Vector4> atlasRects =
                    Component::BoardComponent::ComputeSpriteSheetUVRects(
                        atlasWidth,
                        atlasHeight,
                        atlasCellWidth,
                        atlasCellHeight);

                struct F5BoardSpec
                {
                    float X;
                    float Y;
                    float Width;
                    float Height;
                    BlendMode BlendModeProp;
                    Math::Vector4 Tint;
                    bool bFlipX;
                    bool bFlipY;
                    Math::Vector2 Pivot;
                    Math::Vector2 SizePx;
                    uint32_t LayerPriority;
                    uint32_t OrderInLayer;
                    uint32_t AtlasRectIndex;
                };

                const F5BoardSpec boardSpecs[] =
                {
                    {72.0f, 72.0f, 160.0f, 96.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.90f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 0u, 0u},
                    {112.0f, 104.0f, 180.0f, 104.0f, BlendMode::Opaque, Math::Vector4(1.00f, 1.00f, 1.00f, 1.00f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 1u, 1u},
                    {176.0f, 132.0f, 132.0f, 72.0f, BlendMode::Additive, Math::Vector4(0.85f, 0.85f, 0.85f, 0.50f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 2u, 2u},
                    {472.0f, 108.0f, 72.0f, 72.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.80f), true, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(140.0f, 40.0f), 0u, 3u, 4u},
                    {400.0f, 260.0f, 44.0f, 132.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.85f), false, true, Math::Vector2(0.5f, 1.0f), Math::Vector2(96.0f, 160.0f), 1u, 0u, 5u},
                };

                constexpr uint32_t boardSpecCount = static_cast<uint32_t>(sizeof(boardSpecs) / sizeof(boardSpecs[0]));

                data.m_F4BoardObjects.clear();
                data.m_F4BoardComponents.clear();
                data.m_F4BoardObjects.reserve(boardSpecCount);
                data.m_F4BoardComponents.reserve(boardSpecCount);

                for (const F5BoardSpec &boardSpec : boardSpecs)
                {
                    Entity *boardObject = world.SpawnObject<Entity>();
                    ctx.ScopeRef.TrackObject(boardObject);
                    boardObject->SetPosition(boardSpec.X, boardSpec.Y, 0.0f);
                    boardObject->SetScale(boardSpec.Width, boardSpec.Height, 1.0f);

                    auto *boardComponent = world.CreateComponent<Component::BoardComponent>(boardObject);
                    boardComponent->SetBoardSpace(BoardSpace::ScreenSpace);
                    boardComponent->SetRenderLayer(RenderLayer::UI);
                    boardComponent->SetBlendMode(boardSpec.BlendModeProp);
                    boardComponent->SetTint(boardSpec.Tint);
                    boardComponent->SetFlipX(boardSpec.bFlipX);
                    boardComponent->SetFlipY(boardSpec.bFlipY);
                    boardComponent->SetPivot(boardSpec.Pivot);
                    boardComponent->SetSizePx(boardSpec.SizePx);
                    boardComponent->SetLayerPriority(boardSpec.LayerPriority);
                    boardComponent->SetOrderInLayer(boardSpec.OrderInLayer);
                    boardComponent->SetVisible(true);
                    if (data.m_F6AtlasTextureHandle.IsValid())
                    {
                        boardComponent->SetTextureHandle(data.m_F6AtlasTextureHandle);
                        if (boardSpec.AtlasRectIndex < atlasRects.size())
                        {
                            boardComponent->SetUVRect(atlasRects[boardSpec.AtlasRectIndex]);
                        }
                    }

                    data.m_F4BoardObjects.push_back(boardObject);
                    data.m_F4BoardComponents.push_back(boardComponent);
                }

                LOG_INFO("F5 ScreenSpace Board showcase created with blend, tint, flip, pivot, size, and atlas UV variants");
            }
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
            data.m_pBoulderPlaceholderObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pBoulderPlaceholderObject);
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
                    // cancelled 済みで到着した結果は破棄する。Leave 側で
                    // GLTFAnalyzer::CancelModelLoad と finalize 済みハンドルの
                    // 明示解放を行うため、ここでの use-after-free・リークは防がれる。
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
        // 入力に基づくカメラ更新（シーン所有）
        // ========================================
        const auto &inputState = ctx.InputRef.GetState();

        // カメラコントローラー更新
        data.m_CameraController.Update(inputState, deltaTime);

#if NORVES_BUILD_DEVELOPMENT
        if (!inputState.IsAltDown())
        {
            const DebugViewMode currentDebugViewMode = ctx.EngineRef.GetRenderWorld().GetMainViewportDebugViewMode();
            DebugViewMode nextDebugViewMode = currentDebugViewMode;
            bool bDebugViewModeRequested = true;

            if (inputState.IsKeyPressed(NorvesLib::Core::Input::KeyCode::F1))
            {
                nextDebugViewMode = DebugViewMode::Normal;
            }
            else if (inputState.IsKeyPressed(NorvesLib::Core::Input::KeyCode::F2))
            {
                nextDebugViewMode = DebugViewMode::Unlit;
            }
            else if (inputState.IsKeyPressed(NorvesLib::Core::Input::KeyCode::F3))
            {
                nextDebugViewMode = DebugViewMode::Wireframe;
            }
            else if (inputState.IsKeyPressed(NorvesLib::Core::Input::KeyCode::F4))
            {
                nextDebugViewMode = DebugViewMode::MegaGeometryClusters;
            }
            else if (inputState.IsKeyPressed(NorvesLib::Core::Input::KeyCode::F5))
            {
                uint8_t nextModeIndex = static_cast<uint8_t>(currentDebugViewMode) + 1;
                if (nextModeIndex >= static_cast<uint8_t>(DebugViewMode::Count))
                {
                    nextModeIndex = 0;
                }
                nextDebugViewMode = static_cast<DebugViewMode>(nextModeIndex);
            }
            else
            {
                bDebugViewModeRequested = false;
            }

            if (bDebugViewModeRequested && nextDebugViewMode != currentDebugViewMode)
            {
                ctx.EngineRef.GetRenderWorld().SetDebugViewModeAll(nextDebugViewMode);
                const DebugViewMode reflectedDebugViewMode = ctx.EngineRef.GetRenderWorld().GetMainViewportDebugViewMode();
                data.m_DebugViewMode = reflectedDebugViewMode;
                NORVES_LOG_INFO("DebugView", "DebugViewMode -> %s", DebugViewModeToString(reflectedDebugViewMode));
            }
        }
#endif

        // デバッグ: スクロール値とカメラ距離を出力
        {
            float scroll = inputState.GetMouseState().ScrollDelta;
            if (std::abs(scroll) > 0.0f)
            {
                float dist = data.m_CameraController.GetDistance();
                auto pos = data.m_CameraController.GetPosition();
                NORVES_LOG_DEBUG("Input", "ScrollDelta={:.3f}, CamDist={:.3f}, CamPos=({:.2f}, {:.2f}, {:.2f})",
                                 scroll, dist, pos.x, pos.y, pos.z);
            }
        }

        // カメラ状態をRenderWorldに反映
        {
            NorvesLib::Core::Rendering::CameraProxy cameraProxy;
            data.m_CameraController.ApplyTo(cameraProxy);
            ctx.EngineRef.GetRenderWorld().SetMainCamera(cameraProxy);
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
                        // スコープ追跡から外してから World から除去する。
                        // これ以降このポインタは解放されるため、Cleanup が
                        // 解放済みポインタへ RemoveObject しないようにする。
                        ctx.ScopeRef.Untrack(data.m_pBoulderPlaceholderObject);
                        world.RemoveObject(data.m_pBoulderPlaceholderObject);
                        data.m_pBoulderPlaceholderObject = nullptr;
                        data.m_pBoulderPlaceholderMeshComponent = nullptr;
                    }

                    if (!data.m_pBoulderObject)
                    {
                        data.m_pBoulderObject = world.SpawnObject<Entity>();
                        ctx.ScopeRef.TrackObject(data.m_pBoulderObject);
                        data.m_pBoulderObject->SetPosition(3.0f, 0.0f, 0.0f);

                        data.m_pBoulderMegaGeometryComponent = world.CreateComponent<Component::MegaGeometryComponent>(data.m_pBoulderObject);
                        data.m_pBoulderMegaGeometryComponent->SetMegaMeshHandle(megaMeshHandle);
                        data.m_pBoulderMegaGeometryComponent->SetCastShadow(true);
                    }

                    // 消費したモデルはスコープに解放を委ねる（成功パスのみ）。
                    // 失敗パスは即時 ReleaseModel 済みのため追跡してはならない。
                    ctx.ScopeRef.TrackModel(data.m_BoulderModelHandle);

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

        // 1) 非同期ロードの後始末（World/RenderResources 生存中に行う）。
        //    GameModeScope::Cleanup は Leave 直後に呼ばれるため、ここでは
        //    スコープが追跡していない非同期由来のリソースだけを閉じる。
        if (data.m_BoulderAsyncState)
        {
            // 以降到着する callback の結果を破棄する。callback は asyncState を
            // 共有所有しているため、ここで reset しても use-after-free にならない。
            auto state = data.m_BoulderAsyncState;
            data.m_BoulderAsyncState->m_bCancelled.Store(true);
            data.m_BoulderAsyncState.reset();

            // Flush がモデルを finalize し callback を発火したが、Tick がまだ
            // boulder として消費（＝スコープ追跡）していない隙間。ここで解放
            // しないとモデルがリークする。
            if (state->m_bCompleted.Load() && state->m_bLoaded && state->m_Handle.IsValid())
            {
                ctx.RenderResourcesRef.MegaGeometry().ReleaseModel(state->m_Handle);
            }
        }

        // まだ in-flight なロードはキャンセルする（Flush が finalize をスキップ
        // するためリークしない）。
        if (data.m_BoulderLoadRequestId != 0)
        {
            Resource::GLTFAnalyzer::CancelModelLoad(data.m_BoulderLoadRequestId);
            data.m_BoulderLoadRequestId = 0;
        }

        // 2) 追跡済みリソース（球体/地面/光源球体/ディレクショナル/placeholder/
        //    boulder の各オブジェクト・3 メッシュ・boulder モデル）の解放は
        //    GameModeScope::Cleanup（Leave 直後に StateMachine が呼ぶ）が
        //    正しい順序で行う。ここでは手動解放しない。
        if (data.m_F6AtlasTextureHandle.IsValid())
        {
            ctx.RenderResourcesRef.Textures().ReleaseTexture(data.m_F6AtlasTextureHandle);
            data.m_F6AtlasTextureHandle = TextureHandle::Invalid();
        }

        // 3) 再 Enter（Change 往復）に備えてキャッシュをクリアする。
        //    スコープは独自の追跡リストを使うため、ここでの null 化は安全。
        data.m_pSphereObject = nullptr;
        data.m_pSphereMeshComponent = nullptr;
        data.m_pGroundObject = nullptr;
        data.m_pGroundMeshComponent = nullptr;
        data.m_pLightSphereObject = nullptr;
        data.m_pLightSphereMeshComponent = nullptr;
        data.m_pPointLightComponent = nullptr;
        data.m_pBoulderPlaceholderObject = nullptr;
        data.m_pBoulderPlaceholderMeshComponent = nullptr;
        data.m_pBoulderObject = nullptr;
        data.m_pBoulderMegaGeometryComponent = nullptr;
        data.m_pDirectionalLightObject = nullptr;
        data.m_pDirectionalLightComponent = nullptr;
        data.m_F4BoardObjects.clear();
        data.m_F4BoardComponents.clear();

        data.m_bMeshesRegistered = false;
        data.m_bBoulderModelLoaded = false;
        data.m_BoulderModelHandle = ModelHandle::Invalid();
        data.m_bBoulderModelLoadPending = false;

        // 非同期マテリアル更新のクリア
        data.m_PendingMaterialUpdates.clear();
    }

} // namespace Game::GameModes
