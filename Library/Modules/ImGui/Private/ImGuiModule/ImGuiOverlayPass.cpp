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

    bool ImGuiOverlayPass::Initialize(Core::Rendering::ViewRenderContext &context)
    {
        // 録画窓内の遅延初期化。device 経由で抽象 IImGuiRenderer を取得し、seam の
        // OverlayLoadRenderPass に対して初期化する。失敗時は false を返し seam が
        // 当該 overlay を恒久無効化する(描画素通り)。
        if (context.Device == nullptr || context.OverlayLoadRenderPass == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "ImGuiOverlayPass Initialize skipped: Device/OverlayLoadRenderPass null");
            return false;
        }

        // IImGuiRenderer は device 所有の借用ポインタ(未対応バックエンドでは nullptr)。
        m_Renderer = context.Device->CreateImGuiRenderer();
        if (m_Renderer == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "ImGuiOverlayPass Initialize failed: CreateImGuiRenderer returned null (backend unsupported)");
            return false;
        }

        // フォントアトラス・パイプライン等の RHI リソース生成は renderer(Vulkan 実装)
        // 側に閉じる。load render pass は最終 blit 後の back buffer 経路依存(legacy /
        // composite)を seam がセット済み。
        if (!m_Renderer->Initialize(context.OverlayLoadRenderPass, kImGuiMinImageCount, kImGuiImageCount))
        {
            NORVES_LOG_WARNING(kLogCategory, "ImGuiOverlayPass Initialize failed: IImGuiRenderer::Initialize");
            m_Renderer = nullptr; // 実体は device 所有なので解放はしない(借用解除のみ)
            return false;
        }

        if (!m_Renderer->BuildFontAtlas())
        {
            NORVES_LOG_WARNING(kLogCategory, "ImGuiOverlayPass Initialize failed: BuildFontAtlas");
            m_Renderer->Shutdown();
            m_Renderer = nullptr;
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO(kLogCategory, "ImGuiOverlayPass Initialize: imgui backend ready");
        return true;
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
