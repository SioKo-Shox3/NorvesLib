#include "Rendering/RenderingCoordinator.h"
#include "Rendering/Screen.h"
#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FramePacket.h"
#include <chrono>
#include <algorithm>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // RenderingCoordinator
    // ========================================

    bool RenderingCoordinator::Initialize(const RenderingCoordinatorSettings &settings)
    {
        if (m_bInitialized)
        {
            return true;
        }

        m_Width = settings.Width;
        m_Height = settings.Height;
        m_bVSyncEnabled = settings.bVSync;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        m_MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;

        // TODO: RHIデバイスの作成
        // m_Device = RHI::CreateDevice(settings.bEnableValidation);

        // Screenの初期化
        ScreenSettings screenSettings;
        screenSettings.Width = settings.Width;
        screenSettings.Height = settings.Height;
        screenSettings.WindowHandle = settings.WindowHandle;
        screenSettings.bVSync = settings.bVSync;

        if (!m_Screen.Initialize(m_Device, screenSettings))
        {
            return false;
        }

        // メインSceneViewの作成
        SceneViewSettings sceneViewSettings;
        sceneViewSettings.Width = settings.Width;
        sceneViewSettings.Height = settings.Height;

        m_MainSceneView = Container::MakeShared<SceneView>();
        if (!m_MainSceneView->Initialize(sceneViewSettings))
        {
            m_Screen.Shutdown();
            return false;
        }

        // ScreenにSceneViewを追加
        m_Screen.AddView(m_MainSceneView, 0);
        m_Views.push_back(m_MainSceneView);

        // FramePacketManagerの初期化
        m_PacketManager.Initialize();

        // DrawCommand用バッファを確保
        m_FrameDrawCommands.reserve(m_MaxDrawCallsPerFrame);

        m_bInitialized = true;
        return true;
    }

    void RenderingCoordinator::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // Viewの破棄
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Shutdown();
            }
        }
        m_Views.clear();
        m_MainSceneView.reset();

        // Screenの破棄
        m_Screen.Shutdown();

        // FramePacketManagerの終了
        m_PacketManager.Shutdown();

        // RHIリソースの解放
        m_CommandList.reset();
        m_SwapChain.reset();
        m_Device.reset();

        m_bInitialized = false;
    }

    void RenderingCoordinator::BeginFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 現在時刻を取得
        auto now = std::chrono::high_resolution_clock::now();
        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

        // デルタタイム計算
        float deltaTime = static_cast<float>(currentTime - m_LastFrameTime);
        m_LastFrameTime = currentTime;
        m_TotalTime += deltaTime;

        // 書き込み用パケットを取得
        m_CurrentPacket = m_PacketManager.AcquireForWrite();
        if (m_CurrentPacket)
        {
            m_CurrentPacket->FrameNumber = m_Stats.FrameNumber;
            m_CurrentPacket->DeltaTime = deltaTime;
            m_CurrentPacket->TotalTime = m_TotalTime;
        }

        m_Stats.DeltaTime = deltaTime;
        if (deltaTime > 0.0f)
        {
            m_Stats.FPS = 1.0f / deltaTime;
        }

        m_Screen.BeginFrame();
    }

    void RenderingCoordinator::CollectScene()
    {
        if (!m_bInitialized || !m_CurrentPacket)
        {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // 各SceneViewでシーン収集
        // 注: ProxyはWorldからSceneViewに直接渡されているため、
        //     ここでは統計情報の更新のみ行う
        for (auto &view : m_Views)
        {
            if (!view)
            {
                continue;
            }

            // SceneViewのProxy情報は既にWorldから設定済み
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.CollectionTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    void RenderingCoordinator::GenerateDrawCommands()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_FrameDrawCommands.clear();

        // 各SceneViewでDrawCommand生成
        // 注: カリングとバッチングはSceneView::Render()内でViewportごとに行われる
        auto startCmdGen = std::chrono::high_resolution_clock::now();

        for (auto &view : m_Views)
        {
            if (SceneView *sceneView = dynamic_cast<SceneView *>(view.get()))
            {
                // DrawCommandを収集
                const auto &viewCommands = sceneView->GetDrawCommands();
                m_FrameDrawCommands.insert(m_FrameDrawCommands.end(),
                                           viewCommands.begin(), viewCommands.end());
            }
        }

        auto endCmdGen = std::chrono::high_resolution_clock::now();
        m_Stats.CommandGenerationTimeMs = std::chrono::duration<float, std::milli>(endCmdGen - startCmdGen).count();

        m_Stats.DrawCalls = static_cast<uint32_t>(m_FrameDrawCommands.size());
    }

    void RenderingCoordinator::EndFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 書き込み完了をマーク
        if (m_CurrentPacket)
        {
            m_PacketManager.FinishWrite(m_CurrentPacket);
            m_CurrentPacket = nullptr;
        }

        m_Screen.EndFrame();
        m_Stats.FrameNumber++;
    }

    void RenderingCoordinator::RenderFrame(FramePacket *packet)
    {
        if (!m_bInitialized || !packet)
        {
            return;
        }

        // 各Viewをレンダリング
        // SceneView::Render()内でCollectProxies, CullProxies, BatchProxies,
        // GenerateCommands, RenderCommandsが順次実行される
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Render();
            }
        }

        // Viewの合成
        m_Screen.CompositeViews();

        // GPUにサブミット
        SubmitToGPU();

        // プレゼント
        Present();
    }

    void RenderingCoordinator::ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands)
    {
        if (!m_bInitialized)
        {
            return;
        }

        for (const DrawCommand &cmd : commands)
        {
            // TODO: RHI経由でGPUにコマンドを発行
            // 1. マテリアルのバインド
            // 2. メッシュのバインド
            // 3. ユニフォームの設定
            // 4. ドローコールの発行
            (void)cmd; // 未使用警告抑制
        }
    }

    void RenderingCoordinator::SubmitToGPU()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // TODO: RHIコマンドリストの実行
        // if (m_CommandList)
        // {
        //     m_CommandList->Close();
        //     m_Device->ExecuteCommandList(m_CommandList.get());
        // }
    }

    void RenderingCoordinator::Present()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // TODO: スワップチェーンのプレゼント
        // if (m_SwapChain)
        // {
        //     m_SwapChain->Present(m_bVSyncEnabled ? 1 : 0);
        // }
    }

    Container::TSharedPtr<SceneView> RenderingCoordinator::CreateSceneView(const SceneViewSettings &settings)
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        auto sceneView = Container::MakeShared<SceneView>();
        if (!sceneView->Initialize(settings))
        {
            return nullptr;
        }

        m_Views.push_back(sceneView);
        return sceneView;
    }

    void RenderingCoordinator::DestroyView(Container::TSharedPtr<View> view)
    {
        if (!m_bInitialized || !view)
        {
            return;
        }

        // Screenから削除
        m_Screen.RemoveView(view);

        // リストから削除
        auto it = std::find(m_Views.begin(), m_Views.end(), view);
        if (it != m_Views.end())
        {
            (*it)->Shutdown();
            m_Views.erase(it);
        }
    }

    void RenderingCoordinator::Resize(uint32_t width, uint32_t height)
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_Width = width;
        m_Height = height;

        // Screenのリサイズ
        m_Screen.Resize(width, height);

        // 各Viewのリサイズ
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Resize(width, height);
            }
        }

        // TODO: スワップチェーンのリサイズ
    }

} // namespace NorvesLib::Core::Rendering
