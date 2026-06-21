#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/FramePacket.h"
#include "RHI/RHITypes.h"

#include "ImGuiModule/ImGuiDrawSnapshot.h"

// IImGuiRenderer / IDevice / IRenderPass は RHITypes.h で前方宣言されないため、ここで
// 明示前方宣言する(本ヘッダはポインタ型しか扱わないため各ヘッダの include は不要)。
namespace NorvesLib::RHI
{
    class IImGuiRenderer;
    class IDevice;
    class IRenderPass;
} // namespace NorvesLib::RHI

// 前方宣言(本ヘッダは ImDrawData をポインタでしか扱わない)。
struct ImDrawData;

// ImGuiOverlayPass — ImGui ドローを overlay seam で最終段描画する IViewPass(第2段 B-i)。
//
// 目的: overlay seam(RenderingCoordinator::RenderFrame の executor 戻り後・End 前)で、
// ViewRenderContext::OverlayLoadRenderPass / OverlayLoadFramebuffer(経路依存=legacy /
// composite)越しに最終 blit 後の back buffer へ ImGui のデモ UI を load-blend する。
//
// RHI 境界: 本パスは抽象 RHI::IImGuiRenderer(device 所有)と抽象 RHI(ICommandList/
// IRenderPass/IFramebuffer)のみに触れ、生 Vulkan / imgui_impl_* を一切 include しない。
// フォントアトラス・パイプライン等の RHI リソース生成は IImGuiRenderer(Core の Vulkan
// 実装)側に閉じ、本パスは録画(RecordDrawData)のみを行う。
//
// スレッド/スナップショット所有(MT 安全の核心):
//   ImGui の ImDrawData は ImGuiContext 所有で翌 NewFrame で上書きされるため、RT が
//   ライブ実体を読むと GameThread の次フレーム構築と競合する。GT↔RT のペーシングは
//   swapchain in-flight でなく FramePacket プール(FRAME_PACKET_BUFFER_COUNT スロット)で
//   決まり、GT は毎フレーム RT を待たないため、単一の共有クローンを毎フレーム上書きすると
//   use-after-free になる。本パスは FramePacket スロットごとに 1 個のクローン
//   (m_SlotSnapshots[FRAME_PACKET_BUFFER_COUNT])を持ち:
//     - GameThread: ImGuiModule::Tick が ImGui::Render 後に SetPendingDrawData() で
//       ライブ ImDrawData(本フレームの GameThread スレッド内でのみ有効)を借用設定する。
//       続いて OnAssignedToPacket(slot) が「書き込み中パケットのスロット index」へ
//       m_SlotSnapshots[slot] をディープクローンする(当該スロットは Writing=GT 専有)。
//     - RenderThread: Execute が context.OverlayPacketSlotIndex(処理中パケットのスロット
//       index)で m_SlotSnapshots[idx] を読む(当該スロットは Reading=RT 専有)。
//   FramePacket プールが「スロットが Empty(RT 回収済)になるまで GT が Writing しない」
//   ことを保証するため、同一スロットへの GT クローン書込みと RT 読取は時間的に重ならず、
//   ドロップ挙動やスレッド速度に依存せず証明可能に安全。
//
// 寿命/初期化(2B-i② で GameThread 前倒し):
//   InitializeGameThread(ImGuiModule::Initialize から・GameThread・最初のフレーム投入前)で
//   device->CreateImGuiRenderer() 経由の IImGuiRenderer を取得し Initialize + BuildFontAtlas +
//   UploadFontAtlas(全テクスチャを GPU へ同期アップロードし Status=OK 確定)まで完了し
//   m_bInitialized を立てる。グラフィックスキューがアイドル(RenderThread 未投入)な時点で
//   アップロードするため、ImTextureData::Status の跨スレッド書込みが構造的に消える。
//   IViewPass::Initialize(録画窓内・RenderThread)は既に GameThread 初期化済みなら no-op で
//   true を返すフォールバックに留め、テクスチャアップロードは行わない。
// Setup は no-op。Execute で seam の load render pass を Begin→RecordDrawData→End する
// (Begin/EndRecording は呼ばない=録画窓は RenderingCoordinator が管理)。RecordDrawData は
// テクスチャを更新しない(録画のみ)。
// Shutdown で IImGuiRenderer を Shutdown する(renderer 実体は device 所有で delete
// しない)。Shutdown は RenderThread 静止後・device 生存中に ImGuiModule から駆動。
namespace NorvesLib::Modules::Gui
{
    class ImGuiOverlayPass final : public NorvesLib::Core::Rendering::IViewPass
    {
    public:
        ImGuiOverlayPass() = default;

        /**
         * @brief 本フレームのライブ ImDrawData を借用設定する(GameThread)
         *
         * ImGuiModule::Tick が ImGui::Render() 直後に呼ぶ。drawData は ImGui::GetDrawData()
         * の戻りで、同 GameThread フレーム内(次 NewFrame まで)有効なライブ借用。RT は
         * これを読まない(per-slot クローンのみ読む)。OnAssignedToPacket がこの借用を
         * 書き込み中スロットのクローンへ複製する。null 可。
         */
        void SetPendingDrawData(const ::ImDrawData *drawData)
        {
            m_PendingDrawData = drawData;
        }

        /**
         * @brief imgui バックエンドを GameThread で初期化しフォントアトラスをアップロードする
         *
         * MT 安全化の核（2B-i②）。従来は overlay seam（RenderThread・録画窓内）で遅延
         * 初期化し、フォントアトラスの GPU アップロードを RecordDrawData 内で行っていたが、
         * それは ImTextureData::Status を GameThread（NewFrame）と RenderThread（描画）が
         * 無ロックで共有書込みする競合を生む。本メソッドは ImGuiModule::Initialize（GameThread・
         * 最初のフレーム投入前＝RenderThread アイドルでグラフィックスキューを GameThread が
         * 専有）から呼ばれ、device からバックエンド renderer を生成し、Initialize（パイプライン
         * 生成）→ BuildFontAtlas → UploadFontAtlas（全テクスチャ同期アップロード・Status=OK 確定）
         * までを完了する。以後 RenderThread はテクスチャに一切触れない。
         *
         * @param device       バックエンド生成元の RHI device（借用・seam と同一実体）
         * @param loadRenderPass overlay 描画に用いる load render pass（借用・legacy/composite
         *                       いずれも構成同一のため一方で生成したパイプラインが両経路で有効）
         * @return 初期化＋アップロードに成功した場合 true。失敗時は overlay は描画されない
         */
        bool InitializeGameThread(NorvesLib::RHI::IDevice *device,
                                  NorvesLib::RHI::IRenderPass *loadRenderPass);

        const char *GetName() const override;

        bool Initialize(NorvesLib::Core::Rendering::ViewRenderContext &context) override;
        void Shutdown() override;

        void Setup(NorvesLib::Core::Rendering::ViewRenderContext &context) override;
        void Execute(NorvesLib::Core::Rendering::ViewRenderContext &context) override;

        /**
         * @brief 書き込み中パケットのスロット index へ per-slot クローンを確定する(GameThread)
         *
         * RenderingCoordinator が本パスを書き込み中パケットへ登録する際に呼ぶ(当該パケットは
         * Writing=GameThread 専有・RT は同スロットを読まない)。SetPendingDrawData で設定済みの
         * ライブ ImDrawData を m_SlotSnapshots[slotIndex] へディープクローンする。slotIndex が
         * 範囲外(プール枯渇等の無効値)のときは何もしない(その場合 overlay は当該フレーム未描画)。
         */
        void OnAssignedToPacket(uint32_t slotIndex) override;

    private:
        // device->CreateImGuiRenderer() の戻り(借用・device 所有)。delete しない。
        NorvesLib::RHI::IImGuiRenderer *m_Renderer = nullptr;

        // FramePacket スロットごとの ImDrawData ディープクローン(本パス所有)。GameThread が
        // OnAssignedToPacket で書込み、RenderThread が Execute で読む。スロット排他で安全。
        ImGuiDrawSnapshot m_SlotSnapshots[NorvesLib::Core::Rendering::FRAME_PACKET_BUFFER_COUNT];

        // 本フレームのライブ ImDrawData(GameThread 借用・次 NewFrame まで有効)。RT は読まない。
        const ::ImDrawData *m_PendingDrawData = nullptr;
    };
} // namespace NorvesLib::Modules::Gui
