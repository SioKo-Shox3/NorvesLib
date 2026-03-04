#include "Rendering3DTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/LightComponent.h"
#include "Core/Public/Component/PointLightComponent.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderResourceManager.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Rendering/SceneView.h"
#include "Core/Public/Math/Matrix4x4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"

#include <cmath>

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Engine;
using namespace NorvesLib::Core;

namespace Game::GameModes
{

    void Rendering3DTestRoutine::Enter(IStateMachine *proc, Rendering3DTestData &data)
    {
        (void)proc;

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト開始");
        LOG_INFO("=================================================");

        // ========================================
        // 1. プロシージャルメッシュの生成とGPU登録
        // ========================================
        {
            auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();

            // 球体メッシュの生成
            VariableArray<Mesh3DVertex> sphereVertices;
            VariableArray<uint32_t> sphereIndices;
            ProceduralMeshGenerator::GenerateUVSphere(1.0f, 32, 16, sphereVertices, sphereIndices);

            bool bSphereOk = resourceManager.RegisterMesh(
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

            bool bGroundOk = resourceManager.RegisterMesh(
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

            bool bLightSphereOk = resourceManager.RegisterMesh(
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
            auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();

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

            data.m_CheckerTextureHandle = resourceManager.CreateTexture(
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
        // 1.6 Silver PBRテクスチャのロード
        // ========================================
        {
            auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();

            data.m_SilverAlbedoTexture = resourceManager.LoadTexture("Assets/Textures/Silver/silver_albedo.png");
            data.m_SilverNormalTexture = resourceManager.LoadTexture("Assets/Textures/Silver/silver_normal-ogl.png");
            data.m_SilverMetallicTexture = resourceManager.LoadTexture("Assets/Textures/Silver/silver_metallic.png");
            data.m_SilverRoughnessTexture = resourceManager.LoadTexture("Assets/Textures/Silver/silver_roughness.png");
            data.m_SilverAOTexture = resourceManager.LoadTexture("Assets/Textures/Silver/silver_ao.png");

            if (data.m_SilverAlbedoTexture.IsValid())
            {
                NORVES_LOG_INFO("Rendering3DTest", "Silver PBR textures loaded");
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to load Silver PBR textures");
            }
        }

        // ========================================
        // 2. WorldObjectの作成とメッシュコンポーネントの追加
        // ========================================
        {
            auto &world = GEngine->GetWorld();

            // --- 球体オブジェクト ---
            data.m_pSphereObject = new WorldObject();
            data.m_pSphereObject->Initialize();

            // 球体を地面の上に配置（半径1.0 + 地面Y=-1.0 → Y=0.5で浮かせる）
            data.m_pSphereObject->SetPosition(0.0f, 0.5f, 0.0f);

            data.m_pSphereMeshComponent = new Component::MeshComponent();
            data.m_pSphereMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pSphereMeshComponent->SetMaterial(0, MaterialHandle{1});
            data.m_pSphereMeshComponent->SetCastShadow(true);
            // オブジェクトカラー（白 = テクスチャカラーをそのまま使用）
            data.m_pSphereMeshComponent->SetCustomData(0, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(1, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(2, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(3, 1.0f);
            // Silver PBRテクスチャを適用
            if (data.m_SilverAlbedoTexture.IsValid())
            {
                data.m_pSphereMeshComponent->SetAlbedoTexture(data.m_SilverAlbedoTexture);
                data.m_pSphereMeshComponent->SetNormalTexture(data.m_SilverNormalTexture);
                data.m_pSphereMeshComponent->SetMetallicTexture(data.m_SilverMetallicTexture);
                data.m_pSphereMeshComponent->SetRoughnessTexture(data.m_SilverRoughnessTexture);
                data.m_pSphereMeshComponent->SetAOTexture(data.m_SilverAOTexture);
            }

            data.m_pSphereObject->AddComponent(data.m_pSphereMeshComponent);
            world.AddObject(data.m_pSphereObject);

            LOG_INFO("Sphere WorldObject created and added to World");

            // --- 地面オブジェクト ---
            data.m_pGroundObject = new WorldObject();
            data.m_pGroundObject->Initialize();

            // 地面をY=-1.0に配置
            data.m_pGroundObject->SetPosition(0.0f, -1.0f, 0.0f);

            data.m_pGroundMeshComponent = new Component::MeshComponent();
            data.m_pGroundMeshComponent->SetMeshHandle(data.m_GroundMeshHandle);
            data.m_pGroundMeshComponent->SetMaterial(0, MaterialHandle{1});
            data.m_pGroundMeshComponent->SetCastShadow(false);
            // オブジェクトカラー（暗い緑灰色）→ CustomData
            data.m_pGroundMeshComponent->SetCustomData(0, 0.35f);
            data.m_pGroundMeshComponent->SetCustomData(1, 0.45f);
            data.m_pGroundMeshComponent->SetCustomData(2, 0.3f);
            data.m_pGroundMeshComponent->SetCustomData(3, 1.0f);
            // チェッカーボードテクスチャを設定
            if (data.m_CheckerTextureHandle.IsValid())
            {
                data.m_pGroundMeshComponent->SetAlbedoTexture(data.m_CheckerTextureHandle);
            }

            data.m_pGroundObject->AddComponent(data.m_pGroundMeshComponent);
            world.AddObject(data.m_pGroundObject);

            LOG_INFO("Ground WorldObject created and added to World");

            // --- ポイントライト光源球体オブジェクト ---
            data.m_pLightSphereObject = new WorldObject();
            data.m_pLightSphereObject->Initialize();

            // 球体の横に配置（X=2.0, Y=0.5, Z=0.0）
            data.m_pLightSphereObject->SetPosition(2.0f, 0.5f, 0.0f);

            data.m_pLightSphereMeshComponent = new Component::MeshComponent();
            data.m_pLightSphereMeshComponent->SetMeshHandle(data.m_LightSphereMeshHandle);
            data.m_pLightSphereMeshComponent->SetMaterial(0, MaterialHandle{1});
            data.m_pLightSphereMeshComponent->SetCastShadow(false); // 光源自体は影を落とさない
            // 明るい黄色（発光体の見た目）
            data.m_pLightSphereMeshComponent->SetCustomData(0, 1.0f);
            data.m_pLightSphereMeshComponent->SetCustomData(1, 0.9f);
            data.m_pLightSphereMeshComponent->SetCustomData(2, 0.3f);
            data.m_pLightSphereMeshComponent->SetCustomData(3, 1.0f);
            // エミッシブ設定（自発光 → ブルーム対象になる）
            data.m_pLightSphereMeshComponent->SetEmissiveColor(1.0f, 0.9f, 0.3f);
            data.m_pLightSphereMeshComponent->SetEmissiveStrength(8.0f);

            data.m_pLightSphereObject->AddComponent(data.m_pLightSphereMeshComponent);

            // PointLightComponentの追加（SceneViewへのLightProxy登録はWorld::SyncToSceneView()で自動化）
            data.m_pPointLightComponent = new Component::PointLightComponent();
            data.m_pPointLightComponent->SetLightColor(1.0f, 0.9f, 0.3f);
            data.m_pPointLightComponent->SetIntensity(3.0f);
            data.m_pPointLightComponent->SetRange(8.0f);
            data.m_pPointLightComponent->SetLightVisible(true);
            data.m_pPointLightComponent->SetCastShadows(false);
            data.m_pLightSphereObject->AddComponent(data.m_pPointLightComponent);

            world.AddObject(data.m_pLightSphereObject);

            LOG_INFO("Light sphere WorldObject created and added to World");

            // --- ディレクショナルライト（シャドウ方向と一致） ---
            data.m_pDirectionalLightObject = new WorldObject();
            data.m_pDirectionalLightObject->Initialize();
            data.m_pDirectionalLightObject->SetPosition(0.0f, 0.0f, 0.0f);

            data.m_pDirectionalLightComponent = new Component::LightComponent();
            // LightComponentはデフォルトでDirectional型
            data.m_pDirectionalLightComponent->SetLightColor(1.0f, 1.0f, 1.0f);
            data.m_pDirectionalLightComponent->SetIntensity(1.5f);
            data.m_pDirectionalLightComponent->SetLightDirection(-0.577f, -0.577f, -0.577f);
            data.m_pDirectionalLightComponent->SetLightVisible(true);
            data.m_pDirectionalLightComponent->SetCastShadows(true);
            data.m_pDirectionalLightObject->AddComponent(data.m_pDirectionalLightComponent);

            world.AddObject(data.m_pDirectionalLightObject);

            LOG_INFO("Directional light created and added to World");
        }
    }

    void Rendering3DTestRoutine::Do(IStateMachine *proc, Rendering3DTestData &data, float deltaTime)
    {
        (void)proc;

        data.m_ElapsedTime += deltaTime;

        // 球体をY軸回転させる
        if (data.m_pSphereObject)
        {
            float angle = data.m_ElapsedTime * data.m_RotationSpeed;
            NorvesLib::Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            NorvesLib::Math::Quaternion rotation(yAxis, angle);
            data.m_pSphereObject->SetRotation(rotation);
        }
    }

    void Rendering3DTestRoutine::Leave(IStateMachine *proc, Rendering3DTestData &data)
    {
        (void)proc;

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト終了");
        LOG_INFO("=================================================");

        // メッシュの登録解除
        if (data.m_bMeshesRegistered)
        {
            auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();
            resourceManager.UnregisterMesh(data.m_SphereMeshHandle);
            resourceManager.UnregisterMesh(data.m_GroundMeshHandle);
            resourceManager.UnregisterMesh(data.m_LightSphereMeshHandle);
            data.m_bMeshesRegistered = false;
        }

        // WorldObjectはWorld破棄時にInner/Outerで自動解放されるため
        // ここでは参照のクリアのみ
        data.m_pSphereObject = nullptr;
        data.m_pGroundObject = nullptr;
        data.m_pLightSphereObject = nullptr;
        data.m_pDirectionalLightObject = nullptr;
        data.m_pSphereMeshComponent = nullptr;
        data.m_pGroundMeshComponent = nullptr;
        data.m_pLightSphereMeshComponent = nullptr;
        data.m_pDirectionalLightComponent = nullptr;
        data.m_pPointLightComponent = nullptr;
    }

} // namespace Game::GameModes
