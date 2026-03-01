#include "Rendering3DTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderResourceManager.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
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

    void Rendering3DTestRoutine::Enter(IStateMachine* proc, Rendering3DTestData& data)
    {
        (void)proc;

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト開始");
        LOG_INFO("=================================================");

        // ========================================
        // 1. プロシージャルメッシュの生成とGPU登録
        // ========================================
        {
            auto& resourceManager = GEngine->GetRenderWorld().GetResourceManager();

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
        }

        // ========================================
        // 2. WorldObjectの作成とメッシュコンポーネントの追加
        // ========================================
        {
            auto& world = GEngine->GetWorld();

            // --- 球体オブジェクト ---
            data.m_pSphereObject = new WorldObject();
            data.m_pSphereObject->Initialize();

            // 球体を地面の上に配置（半径1.0 + 地面Y=-1.0 → Y=0.5で浮かせる）
            data.m_pSphereObject->SetPosition(0.0f, 0.5f, 0.0f);

            data.m_pSphereMeshComponent = new Component::MeshComponent();
            data.m_pSphereMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pSphereMeshComponent->SetMaterial(0, MaterialHandle{1});
            data.m_pSphereMeshComponent->SetCastShadow(true);
            // オブジェクトカラー（赤系）→ CustomData
            data.m_pSphereMeshComponent->SetCustomData(0, 0.8f);
            data.m_pSphereMeshComponent->SetCustomData(1, 0.2f);
            data.m_pSphereMeshComponent->SetCustomData(2, 0.2f);
            data.m_pSphereMeshComponent->SetCustomData(3, 1.0f);

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

            data.m_pGroundObject->AddComponent(data.m_pGroundMeshComponent);
            world.AddObject(data.m_pGroundObject);

            LOG_INFO("Ground WorldObject created and added to World");
        }
    }

    void Rendering3DTestRoutine::Do(IStateMachine* proc, Rendering3DTestData& data, float deltaTime)
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

    void Rendering3DTestRoutine::Leave(IStateMachine* proc, Rendering3DTestData& data)
    {
        (void)proc;

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト終了");
        LOG_INFO("=================================================");

        // メッシュの登録解除
        if (data.m_bMeshesRegistered)
        {
            auto& resourceManager = GEngine->GetRenderWorld().GetResourceManager();
            resourceManager.UnregisterMesh(data.m_SphereMeshHandle);
            resourceManager.UnregisterMesh(data.m_GroundMeshHandle);
            data.m_bMeshesRegistered = false;
        }

        // WorldObjectはWorld破棄時にInner/Outerで自動解放されるため
        // ここでは参照のクリアのみ
        data.m_pSphereObject = nullptr;
        data.m_pGroundObject = nullptr;
        data.m_pSphereMeshComponent = nullptr;
        data.m_pGroundMeshComponent = nullptr;
    }

} // namespace Game::GameModes
