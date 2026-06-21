#include "ImGuiModule/ImGuiOverlayPass.h"
#include "ImGuiModule/ImGuiDrawSnapshot.h"

#include "Rendering/ViewRenderContext.h"
#include "Rendering/FramePacket.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IImGuiRenderer.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

namespace NorvesLib::Modules::Gui
{
    namespace
    {
        constexpr const char *kLogCategory = "ImGui";
        constexpr const char *kPassName = "ImGuiOverlayPass";

        // overlay seam が渡す生ポインタ(IRenderPass*/IFramebuffer*)を、所有を持たない
        // TSharedPtr(aliasing 構築)へ橋渡しするヘルパ。空の制御ブロックと別れて参照
        // カウントを持たないため破棄時に delete されない。RHI の BeginRenderPass は
        // TSharedPtr を要求するが、seam の生存は RenderingCoordinator が保証する。
        template <typename T>
        Core::Container::TSharedPtr<T> NonOwning(T *raw)
        {
            return Core::Container::TSharedPtr<T>(Core::Container::TSharedPtr<T>{}, raw);
        }

        // overlay seam は swapchain を公開しないため、imgui バックエンドの内部フレーム
        // リング/最小イメージ数には保守的な既定値を渡す(MAX_FRAMES_IN_FLIGHT=1 では
        // swapchain image 数は 2〜3。imgui はこの値を内部リングのサイズ決めに使うのみで、
        // resize 時は IImGuiRenderer::NotifySwapChainRecreated が最小値を更新する)。
        constexpr uint32_t kImGuiMinImageCount = 2;
        constexpr uint32_t kImGuiImageCount = 2;
    } // namespace

    const char *ImGuiOverlayPass::GetName() const
    {
        return kPassName;
    }

    bool ImGuiOverlayPass::InitializeGameThread(RHI::IDevice *device, RHI::IRenderPass *loadRenderPass)
    {
        // MT 安全化(2B-i②): GameThread・最初のフレーム投入前(=RenderThread アイドルで
        // グラフィックスキューを GameThread が専有)に呼ばれる。device からバックエンド
        // renderer を生成し、パイプライン生成 + フォントアトラス CPU 構築 + GPU アップロード
        // (Status=OK 確定)までを完了する。これで以後 RenderThread はテクスチャに触れず、
        // ImTextureData::Status の跨スレッド書込みが構造的に消える。
        if (m_bInitialized)
        {
            return true;
        }
        if (device == nullptr || loadRenderPass == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "ImGuiOverlayPass InitializeGameThread skipped: Device/loadRenderPass null");
            return false;
        }

        // IImGuiRenderer は device 所有の借用ポインタ(未対応バックエンドでは nullptr)。
        m_Renderer = device->CreateImGuiRenderer();
        if (m_Renderer == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "ImGuiOverlayPass InitializeGameThread failed: CreateImGuiRenderer returned null (backend unsupported)");
            return false;
        }

        // パイプライン等の RHI リソース生成は renderer(Vulkan 実装)側に閉じる。load render pass
        // は legacy / composite いずれも構成同一(color1 Load + depth1 Load)のため、一方で生成
        // したパイプラインが両経路で有効。seam は実行時の経路に応じて対応する load RP を Begin する。
        if (!m_Renderer->Initialize(loadRenderPass, kImGuiMinImageCount, kImGuiImageCount))
        {
            NORVES_LOG_WARNING(kLogCategory, "ImGuiOverlayPass InitializeGameThread failed: IImGuiRenderer::Initialize");
            m_Renderer = nullptr; // 実体は device 所有なので解放はしない(借用解除のみ)
            return false;
        }

        if (!m_Renderer->BuildFontAtlas())
        {
            NORVES_LOG_WARNING(kLogCategory, "ImGuiOverlayPass InitializeGameThread failed: BuildFontAtlas");
            m_Renderer->Shutdown();
            m_Renderer = nullptr;
            return false;
        }

        // 全テクスチャを GameThread で GPU へ同期アップロードし Status=OK に確定する。
        // ここが MT 安全化の本体: RenderThread は以後アップロードを一切行わない。
        if (!m_Renderer->UploadFontAtlas())
        {
            NORVES_LOG_WARNING(kLogCategory, "ImGuiOverlayPass InitializeGameThread failed: UploadFontAtlas");
            m_Renderer->Shutdown();
            m_Renderer = nullptr;
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO(kLogCategory, "ImGuiOverlayPass InitializeGameThread: imgui backend ready (fonts uploaded on GameThread)");
        return true;
    }

    bool ImGuiOverlayPass::Initialize(Core::Rendering::ViewRenderContext & /*context*/)
    {
        // 2B-i② 以降、バックエンド初期化とフォントアップロードは GameThread の
        // InitializeGameThread が前倒しで完了する。本 seam 経路(RenderThread・録画窓内)は
        // 既に初期化済みなら no-op で true を返すフォールバックに留め、テクスチャアップロードを
        // 伴う初期化を RenderThread では一切行わない(MT 安全化の前提を崩さないため)。
        // 未初期化のまま到達した場合(GameThread 初期化が失敗/未実行)は false を返し、seam が
        // 当該 overlay を無効化して描画を素通りさせる(RenderThread でのアップロードはしない)。
        if (m_bInitialized && m_Renderer != nullptr)
        {
            return true;
        }
        NORVES_LOG_WARNING(kLogCategory,
                           "ImGuiOverlayPass Initialize(seam): backend not initialized on GameThread; overlay disabled");
        return false;
    }

    void ImGuiOverlayPass::Shutdown()
    {
        // RenderThread 静止後・device 生存中に ImGuiModule から駆動される。renderer 実体は
        // device 所有のため delete せず Shutdown(RHI リソース解放)のみ呼ぶ。
        if (m_Renderer != nullptr)
        {
            m_Renderer->Shutdown();
            m_Renderer = nullptr;
        }
        m_PendingDrawData = nullptr;
        m_bInitialized = false;
        NORVES_LOG_INFO(kLogCategory, "ImGuiOverlayPass Shutdown");
    }

    void ImGuiOverlayPass::Setup(Core::Rendering::ViewRenderContext & /*context*/)
    {
        // 一時リソースは使わない。準備フェーズは no-op。
    }

    void ImGuiOverlayPass::OnAssignedToPacket(uint32_t slotIndex)
    {
        // GameThread。書き込み中パケットのスロット index が渡る。当該スロットは Writing
        // (GT 専有)で RT は読まないため、ここで m_SlotSnapshots[slotIndex] へライブ
        // ImDrawData をディープクローンしても RT の per-slot 読取と競合しない。
        // slotIndex 無効(FRAME_PACKET_BUFFER_COUNT 以上=プール枯渇等)のときは何もしない。
        if (slotIndex >= Core::Rendering::FRAME_PACKET_BUFFER_COUNT)
        {
            return;
        }
        m_SlotSnapshots[slotIndex].Capture(m_PendingDrawData);
    }

    void ImGuiOverlayPass::Execute(Core::Rendering::ViewRenderContext &context)
    {
        // 録画窓内・executor 外。CommandList と load render pass/framebuffer が揃わない
        // 場合は何もしない(構造的に no-op)。
        if (context.CommandList == nullptr || context.OverlayLoadRenderPass == nullptr ||
            context.OverlayLoadFramebuffer == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "ImGuiOverlayPass Execute skipped: CommandList/OverlayLoadRenderPass/Framebuffer null");
            return;
        }
        if (m_Renderer == nullptr)
        {
            return;
        }

        // RenderThread。処理中パケットのスロット index で per-slot クローンを選ぶ。当該
        // スロットは Reading(RT 専有)で GT は同スロットへ書き込まないため、ここでクローンを
        // 読んでも use-after-free は起きない(FramePacket スロット寿命連動の証明根拠)。
        // index 無効(プール枯渇等)や空クローンのときは load render pass を開かず完全 no-op。
        const uint32_t slotIndex = context.OverlayPacketSlotIndex;
        if (slotIndex >= Core::Rendering::FRAME_PACKET_BUFFER_COUNT)
        {
            return;
        }
        const ImGuiDrawSnapshot &snapshot = m_SlotSnapshots[slotIndex];
        if (!snapshot.HasDrawData())
        {
            return;
        }
        const void *imguiDrawData = snapshot.GetDrawData();
        if (imguiDrawData == nullptr)
        {
            return;
        }

        RHI::ICommandList *commandList = context.CommandList;

        // 経路依存の load render pass を Begin(Begin/EndRecording は呼ばない)。最終 blit
        // 後の back buffer へ load-blend する(legacy=PresentationLoad / composite=Graph)。
        commandList->BeginRenderPass(NonOwning(context.OverlayLoadRenderPass),
                                     NonOwning(context.OverlayLoadFramebuffer));

        // ImGui ドローを録画中 CommandList へ発行する。viewport/scissor/pipeline 設定は
        // imgui バックエンド(IImGuiRenderer 実装)が内部で行うため本パスでは触れない。
        m_Renderer->RecordDrawData(commandList, imguiDrawData);

        commandList->EndRenderPass();
    }
} // namespace NorvesLib::Modules::Gui
