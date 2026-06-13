#include "Rendering/SceneRenderer.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/SceneView.h"
#include "Rendering/PersistentResourceCache.h"
#include "Rendering/RenderResources.h"
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

    }

    void SceneRenderer::EndFrame()
    {
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
        ExecuteDrawCommands(DrawCommandView::FromArray(commands), commandList);
    }

    void SceneRenderer::ExecuteDrawCommands(DrawCommandView commands,
                                            RHI::ICommandList *commandList)
    {
        if (commands.empty() || !commandList)
        {
            return;
        }

        // TODO: バッチング最適化
        // 現時点では単純に順次実行
        for (const DrawCommand &command : commands)
        {
            ExecuteDrawCommand(command, commandList);
        }
    }

    void SceneRenderer::ExecuteFrameCommands(const Container::VariableArray<FrameCommand> &commands,
                                             RHI::ICommandList *commandList)
    {
        if (commands.empty() || !commandList)
        {
            return;
        }

        for (const FrameCommand &command : commands)
        {
            ExecuteFrameCommand(command, commandList);
        }
    }

    void SceneRenderer::ExecuteDrawCommand(const DrawCommand &command, RHI::ICommandList *commandList)
    {
        if (!commandList)
        {
            return;
        }

        // ========================================
        // Dispatchコマンド
        // ========================================
        if (command.IsComputeCommand())
        {
            // パイプラインをバインド
            RHI::PipelinePtr pipeline = command.Pipeline;
            if (!pipeline)
            {
                return;
            }

            commandList->SetPipeline(pipeline);

            // ディスクリプタセットをバインド
            if (command.DescriptorSet)
            {
                commandList->SetDescriptorSet(command.DescriptorSet, command.DescriptorSetSlot);
            }

            // Dispatch実行
            commandList->Dispatch(
                command.Compute.ThreadGroupCountX,
                command.Compute.ThreadGroupCountY,
                command.Compute.ThreadGroupCountZ);

            ++m_Stats.DispatchCount;
            return;
        }

        // ========================================
        // グラフィックス描画コマンド
        // ========================================

        // パイプラインをバインド（コマンド指定があれば優先、なければデフォルト）
        RHI::PipelinePtr pipeline = command.Pipeline ? command.Pipeline : m_DefaultPipeline;
        if (!pipeline)
        {
            return;
        }

        if (pipeline != m_BoundPipeline)
        {
            commandList->SetPipeline(pipeline);
            m_BoundPipeline = pipeline;
        }

        // ディスクリプタセットをバインド
        if (command.DescriptorSet)
        {
            commandList->SetDescriptorSet(command.DescriptorSet, command.DescriptorSetSlot);
        }

        const auto &draw = command.Draw;

        // MeshHandleからGPUデータを解決
        if (draw.MeshHandle.IsValid() && m_ResourceCache)
        {
            // PersistentResourceCache経由でGPUバッファを取得
        }

        // 頂点/インデックスバッファがある場合のインデックス描画
        if (draw.IndexCount > 0 && draw.MeshHandle.IsValid())
        {
            commandList->DrawIndexedInstanced(
                draw.IndexCount,
                std::max(1u, draw.InstanceCount),
                draw.IndexOffset,
                static_cast<int32_t>(draw.VertexOffset),
                draw.FirstInstance);
        }
        else
        {
            // 頂点バッファなし描画（シェーダー内蔵の頂点データ）
            uint32_t vertexCount = draw.VertexOffset > 0 ? draw.VertexOffset : 3;
            if (draw.bInstanced && draw.InstanceCount > 1)
            {
                commandList->DrawInstanced(vertexCount, draw.InstanceCount, 0, draw.FirstInstance);
            }
            else
            {
                commandList->Draw(vertexCount, 0);
            }
        }

        // 統計更新
        ++m_Stats.DrawCallCount;
        if (draw.IndexCount > 0)
        {
            m_Stats.TriangleCount += draw.IndexCount / 3;
        }
    }

    void SceneRenderer::ExecuteFrameCommand(const FrameCommand &command, RHI::ICommandList *commandList)
    {
        if (!commandList)
        {
            return;
        }

        switch (command.Type)
        {
        case FrameCommandType::GeometryPass:
        {
            const GeometryPassCommand &geometryPass = command.GeometryPass;
            if (!geometryPass.RenderPass || !geometryPass.Framebuffer || !geometryPass.DrawCommands)
            {
                return;
            }

            commandList->BeginRenderPass(geometryPass.RenderPass, geometryPass.Framebuffer);
            commandList->SetViewport(geometryPass.Viewport);
            commandList->SetScissor(geometryPass.Scissor);
            m_BoundPipeline = nullptr;

            for (const DrawCommand &drawCommand : *geometryPass.DrawCommands)
            {
                RHI::PipelinePtr pipeline = drawCommand.Pipeline ? drawCommand.Pipeline : m_DefaultPipeline;
                if (pipeline && pipeline != m_BoundPipeline)
                {
                    commandList->SetPipeline(pipeline);
                    m_BoundPipeline = pipeline;
                }

                if (drawCommand.Draw.MeshHandle.IsValid() && geometryPass.Meshes)
                {
                    RecordMeshDrawCall(drawCommand,
                                       commandList,
                                       geometryPass.Meshes,
                                       drawCommand.DescriptorSet,
                                       drawCommand.DescriptorSetSlot);
                    continue;
                }

                ExecuteDrawCommand(drawCommand, commandList);
            }

            commandList->EndRenderPass();
            return;
        }

        case FrameCommandType::FullscreenPass:
        {
            const FullscreenPassCommand &fullscreenPass = command.FullscreenPass;
            if (!fullscreenPass.RenderPass || !fullscreenPass.Framebuffer)
            {
                return;
            }

            commandList->BeginRenderPass(fullscreenPass.RenderPass, fullscreenPass.Framebuffer);
            commandList->SetViewport(fullscreenPass.Viewport);
            commandList->SetScissor(fullscreenPass.Scissor);
            m_BoundPipeline = nullptr;

            if (fullscreenPass.Pipeline && fullscreenPass.VertexCount > 0)
            {
                commandList->SetPipeline(fullscreenPass.Pipeline);
                if (fullscreenPass.DescriptorSet)
                {
                    commandList->SetDescriptorSet(fullscreenPass.DescriptorSet,
                                                  fullscreenPass.DescriptorSetSlot);
                }

                commandList->Draw(fullscreenPass.VertexCount, 0);
                ++m_Stats.DrawCallCount;
                m_Stats.TriangleCount += fullscreenPass.VertexCount / 3;
            }

            commandList->EndRenderPass();
            return;
        }

        case FrameCommandType::TextureBarrier:
        {
            const TextureBarrierCommand &barrier = command.TextureBarrier;
            if (!barrier.Texture)
            {
                return;
            }

            commandList->TextureBarrier(barrier.Texture,
                                        barrier.BeforeState,
                                        barrier.AfterState,
                                        barrier.MipLevel,
                                        barrier.ArrayIndex,
                                        barrier.MipCount,
                                        barrier.ArrayCount);
            return;
        }

        case FrameCommandType::MegaGeometryPass:
        {
            const MegaGeometryPassCommand &megaGeometryPass = command.MegaGeometry;
            if (!megaGeometryPass.Pass)
            {
                return;
            }

            megaGeometryPass.Pass->RecordFrameCommand(megaGeometryPass, commandList);
            return;
        }
        }

        return;
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
                                           MeshResources *meshResources,
                                           RHI::DescriptorSetPtr descriptorSet,
                                           uint32_t descriptorSetSlot)
    {
        if (!commandList || !meshResources)
        {
            return false;
        }

        // MeshHandleからGPUデータを解決
        const auto *gpuData = meshResources->GetGPUData(command.Draw.MeshHandle);
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
        commandList->DrawIndexedInstanced(
            gpuData->IndexCount,
            std::max(1u, command.Draw.InstanceCount),
            0,
            0,
            command.Draw.FirstInstance);

        // 統計更新
        ++m_Stats.DrawCallCount;
        m_Stats.TriangleCount += gpuData->IndexCount / 3;

        return true;
    }

} // namespace NorvesLib::Core::Rendering
