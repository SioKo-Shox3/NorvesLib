#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/PersistentResourceCache.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
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

        if (!resourceCache)
        {
            NORVES_LOG_ERROR("SceneRenderer", "ResourceCache is null");
            return false;
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

        // TODO: 実際のRHI描画コール実装
        // 現時点ではスタブ実装

        // 統計更新
        ++m_Stats.DrawCallCount;
        m_Stats.TriangleCount += command.IndexCount / 3;

        // 実際の描画は以下のような流れになる:
        // 1. パイプライン/シェーダーのバインド
        // 2. 頂点/インデックスバッファのバインド
        // 3. 定数バッファ/デスクリプタのバインド
        // 4. ドローコール

        /*
        // パイプラインバインド
        commandList->SetPipeline(command.Pipeline);

        // 頂点バッファバインド
        commandList->SetVertexBuffer(0, command.VertexBuffer, command.VertexOffset);

        // インデックスバッファバインド
        commandList->SetIndexBuffer(command.IndexBuffer, command.IndexOffset, IndexFormat::UInt32);

        // 定数バッファバインド
        // ...

        // ドローコール
        if (command.InstanceCount > 1)
        {
            commandList->DrawIndexedInstanced(command.IndexCount, command.InstanceCount, 0, 0, 0);
        }
        else
        {
            commandList->DrawIndexed(command.IndexCount, 0, 0);
        }
        */
    }

    void SceneRenderer::ExecuteBatch(const Container::VariableArray<DrawCommand> &commands,
                                     size_t startIndex, size_t count,
                                     RHI::ICommandList *commandList)
    {
        // バッチ実行（同じマテリアル/パイプラインのコマンドをまとめて処理）
        // TODO: インスタンシング対応
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

} // namespace NorvesLib::Core::Rendering
