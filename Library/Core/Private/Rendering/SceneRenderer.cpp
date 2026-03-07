#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/PersistentResourceCache.h"
#include "Rendering/RenderResourceManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"
#include <chrono>

namespace NorvesLib::Core::Rendering
{

    SceneRenderer::~SceneRenderer()
    {
        Shutdown();
    }

    bool SceneRenderer::Initialize(RHI::IDevice *device,
                                   PersistentResourceCache *resourceCache,
                                   RHI::TransientResourcePool *transientPool)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!device)
        {
            NORVES_LOG_ERROR("SceneRenderer", "Device is null");
            return false;
        }

        // ResourceCacheはオプション（なくてもDrawDirectモードで動作可能）
        if (!resourceCache)
        {
            NORVES_LOG_WARNING("SceneRenderer", "ResourceCache is null - mesh-based DrawCommands will not work");
        }

        m_Device = device;
        m_ResourceCache = resourceCache;
        m_TransientPool = transientPool;
        m_bInitialized = true;

        NORVES_LOG_INFO("SceneRenderer", "SceneRenderer initialized");
        return true;
    }

    void SceneRenderer::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_DefaultPipeline.reset();
        m_BoundPipeline.reset();
        m_Device = nullptr;
        m_ResourceCache = nullptr;
        m_TransientPool = nullptr;
        m_bInitialized = false;

        NORVES_LOG_INFO("SceneRenderer", "SceneRenderer shutdown");
    }

    void SceneRenderer::BeginFrame()
    {
        ++m_CurrentFrame;
        ResetStats();

        if (m_ResourceCache)
        {
            m_ResourceCache->BeginFrame(m_CurrentFrame);
        }

        if (m_TransientPool)
        {
            m_TransientPool->BeginFrame(m_CurrentFrame);
        }
    }

    void SceneRenderer::EndFrame()
    {
        if (m_TransientPool)
        {
            m_TransientPool->EndFrame();
        }

        // 定期的に未使用リソースを解放（例: 300フレームごと）
        if (m_CurrentFrame % 300 == 0 && m_ResourceCache)
        {
            m_ResourceCache->ReleaseUnused(300);
        }
    }

    void SceneRenderer::Render(SceneView *sceneView, RHI::ICommandList *commandList)
    {
        if (!sceneView || !commandList)
        {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // SceneViewからDrawCommandを取得
        const auto &drawCommands = sceneView->GetDrawCommands();

        // DrawCommandを実行
        ExecuteDrawCommands(drawCommands, commandList);

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.RenderTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    void SceneRenderer::ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands,
                                            RHI::ICommandList *commandList)
    {
        if (commands.empty() || !commandList)
        {
            return;
        }

        // TODO: バッチング最適化
        // 現時点では単純に順次実行
        for (const auto &command : commands)
        {
            ExecuteDrawCommand(command, commandList);
        }
    }

    void SceneRenderer::ExecuteDrawCommand(const DrawCommand &command, RHI::ICommandList *commandList)
    {
        if (!commandList)
        {
            return;
        }

        // パイプラインをバインド（デフォルトパイプライン使用）
        // TODO: マテリアルシステム完成後はMaterialHandleからパイプラインを解決
        RHI::PipelinePtr pipeline = m_DefaultPipeline;
        if (!pipeline)
        {
            return;
        }

        if (pipeline != m_BoundPipeline)
        {
            commandList->SetPipeline(pipeline);
            m_BoundPipeline = pipeline;
        }

        // MeshHandleからGPUデータを解決
        if (command.MeshHandle.IsValid() && m_ResourceCache)
        {
            // PersistentResourceCache経由でGPUバッファを取得
            // Note: ResourceIdとMeshDataHandle.Idが対応する
            // ここではCachedMeshGPUDataへの直接アクセスが必要
            // → SceneRenderer用のルックアップ関数が必要

            // 現時点ではフォールバック: MeshHandleベースで直接描画
            // 将来的にはResourceRegistry経由でMeshResource→PersistentResourceCache→GPUデータ
        }

        // 頂点/インデックスバッファがある場合のインデックス描画
        if (command.IndexCount > 0 && command.MeshHandle.IsValid())
        {
            if (command.bInstanced && command.InstanceCount > 1)
            {
                commandList->DrawIndexedInstanced(
                    command.IndexCount,
                    command.InstanceCount,
                    command.IndexOffset,
                    static_cast<int32_t>(command.VertexOffset),
                    command.FirstInstance);
            }
            else
            {
                commandList->DrawIndexed(
                    command.IndexCount,
                    command.IndexOffset,
                    static_cast<int32_t>(command.VertexOffset));
            }
        }
        else
        {
            // 頂点バッファなし描画（シェーダー内蔵の頂点データ）
            // VertexOffset を頂点数として再利用（Directモード用）
            uint32_t vertexCount = command.VertexOffset > 0 ? command.VertexOffset : 3;
            if (command.bInstanced && command.InstanceCount > 1)
            {
                commandList->DrawInstanced(vertexCount, command.InstanceCount, 0, command.FirstInstance);
            }
            else
            {
                commandList->Draw(vertexCount, 0);
            }
        }

        // 統計更新
        ++m_Stats.DrawCallCount;
        if (command.IndexCount > 0)
        {
            m_Stats.TriangleCount += command.IndexCount / 3;
        }
    }

    void SceneRenderer::ExecuteBatch(const Container::VariableArray<DrawCommand> &commands,
                                     size_t startIndex, size_t count,
                                     RHI::ICommandList *commandList)
    {
        // バッチ実行（同じマテリアル/パイプラインのコマンドをまとめて処理）
        for (size_t i = startIndex; i < startIndex + count && i < commands.size(); ++i)
        {
            ExecuteDrawCommand(commands[i], commandList);
        }
    }

    void SceneRenderer::DrawDirect(RHI::ICommandList *commandList,
                                   RHI::PipelinePtr pipeline,
                                   uint32_t vertexCount)
    {
        if (!commandList || !pipeline)
        {
            return;
        }

        commandList->SetPipeline(pipeline);
        commandList->Draw(vertexCount, 0);

        ++m_Stats.DrawCallCount;
        m_Stats.TriangleCount += vertexCount / 3;
    }

    void SceneRenderer::ResetStats()
    {
        m_Stats = SceneRendererStats{};

        if (m_ResourceCache)
        {
            m_Stats.GPUMemoryUsed = m_ResourceCache->GetGPUMemoryUsage();
        }
    }

    bool SceneRenderer::RecordMeshDrawCall(const DrawCommand &command,
                                           RHI::ICommandList *commandList,
                                           RenderResourceManager *resourceManager,
                                           RHI::DescriptorSetPtr descriptorSet,
                                           uint32_t descriptorSetSlot)
    {
        if (!commandList || !resourceManager)
        {
            return false;
        }

        // MeshHandleからGPUデータを解決
        const auto *gpuData = resourceManager->GetMeshGPUData(command.MeshHandle);
        if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer)
        {
            return false;
        }

        // ディスクリプタセット設定
        if (descriptorSet)
        {
            commandList->SetDescriptorSet(descriptorSet, descriptorSetSlot);
        }

        // 頂点・インデックスバッファ設定
        commandList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
        commandList->SetIndexBuffer(gpuData->IndexBuffer, 0);

        // 描画
        if (command.bInstanced && command.InstanceCount > 1)
        {
            commandList->DrawIndexedInstanced(
                gpuData->IndexCount,
                command.InstanceCount,
                0,
                0,
                command.FirstInstance);
        }
        else
        {
            commandList->DrawIndexed(gpuData->IndexCount, 0, 0);
        }

        // 統計更新
        ++m_Stats.DrawCallCount;
        m_Stats.TriangleCount += gpuData->IndexCount / 3;

        return true;
    }

} // namespace NorvesLib::Core::Rendering
