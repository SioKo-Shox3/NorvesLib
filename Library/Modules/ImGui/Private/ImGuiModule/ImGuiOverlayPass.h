#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/FramePacket.h"
#include "Rendering/DynamicBufferRing.h"
#include "RHI/RHITypes.h"

#include "ImGuiModule/ImGuiDrawSnapshot.h"

#include "CoreTypes.h"

// IDevice は RHITypes.h で前方宣言されないため、ここで明示前方宣言する
// (本ヘッダはポインタ/借用しか扱わないため各ヘッダの include は不要)。
namespace NorvesLib::RHI
{
    class IDevice;
} // namespace NorvesLib::RHI

// 前方宣言(本ヘッダは ImDrawData をポインタでしか扱わない)。
struct ImDrawData;

// ImGuiOverlayPass — ImGui ドローを overlay seam で最終段描画する IViewPass(脱 Core 後)。
//
// 目的: overlay seam(RenderingCoordinator::RenderFrame の executor 戻り後・End 前)で、
// ViewRenderContext::OverlayLoadRenderPass / OverlayLoadFramebuffer(経路依存=legacy /
// composite)越しに最終 blit 後の back buffer へ ImGui の UI を load-blend する。
//
// RHI 境界(脱 Core の核心): 本パスは Core の抽象 RHI(RHI::IDevice/ICommandList/
// IRenderPass/IFramebuffer/IPipeline/IDescriptorSet/ITexture/ISampler)と汎用 Mesh2D
// 描画経路(DrawCommand::CreateMesh2D + SceneRenderer の Mesh2D 分岐 + DynamicBufferRing)
// のみを用い、ImDrawData を自前で頂点/インデックスバッファへ詰めて描画する。imgui core
// (imgui.h / ImDrawData)以外の imgui 依存(imgui_impl_vulkan・Core の旧 IImGuiRenderer)は
// 一切持たない。生 Vulkan も触れない。これにより Core から ImGui 固有処理が完全に消える。
//
// スレッド/スナップショット所有(MT 安全の核心・従来踏襲):
//   ImGui の ImDrawData は ImGuiContext 所有で翌 NewFrame で上書きされるため、RT が
//   ライブ実体を読むと GameThread の次フレーム構築と競合する。GT↔RT のペーシングは
//   swapchain in-flight でなく FramePacket プール(FRAME_PACKET_BUFFER_COUNT スロット)で
//   決まり、GT は毎フレーム RT を待たないため、単一の共有クローンを毎フレーム上書きすると
//   use-after-free になる。本パスは FramePacket スロットごとに 1 個のクローン
//   (m_SlotSnapshots[FRAME_PACKET_BUFFER_COUNT])を持ち:
//     - GameThread: ImGuiModule::Tick が ImGui::Render 後に SetPendingDrawData() で
//       ライブ ImDrawData を借用設定し、OnAssignedToPacket(slot) が「書き込み中パケットの
//       スロット index」へ m_SlotSnapshots[slot] をディープクローンする(Writing=GT 専有)。
//     - RenderThread: Execute が context.OverlayPacketSlotIndex(処理中パケットのスロット
//       index)で m_SlotSnapshots[idx] を読む(Reading=RT 専有)。
//   同一スロットへの GT クローン書込みと RT 読取はプール排他で時間的に重ならない。
//
// GPU 資源の添字(飛行中のスロット):
//   頂点/インデックス/UBO の DynamicBufferRing のスロットと m_SlotDescriptorSets は、パケットの
//   スロットでなく飛行中のスロット(context.FrameIndex)で引く。RenderThread は提出の直後に
//   パケットを Empty へ戻すため、パケットのスロットは GPU の完了を保証せず、同じスロットを続けて
//   描くことがある。飛行中のスロットは BeginFrame のフェンスで前の利用の GPU 完了が保証されるので、
//   その組の頂点/UBO を書き換えても前の提出が読む中身を壊さない。FrameIndex が組の数
//   (IN_FLIGHT_SET_COUNT)以上なら描かずに警告する。
//
// テクスチャ(MT 安全・レガシー単一アトラス維持):
//   フォントアトラスは GameThread の InitializeGameThread で
//   io.Fonts->GetTexDataAsRGBA32 の CPU ピクセルから ITexture を一度だけ生成・アップロード
//   し、io.Fonts->SetTexID へ非 Invalid のセンチネルを GameThread で確定する(RT はテクスチャに
//   一切触れない)。アトラスは 1 枚のみで Execute は cmd.GetTexID() を解決に使わず常に当該
//   スロットの DescriptorSet を直接バインドするため、TexID は「ImDrawCmd が有効テクスチャを
//   指す目印」で足りる。imgui 1.92 の動的テクスチャ(ImGuiBackendFlags_RendererHasTextures)は
//   使わず、レガシー単一アトラスに固定する(ImTextureData::Status の跨スレッド共有書込みを
//   構造的に排除する現方針の維持)。
//
// 寿命/初期化の二段:
//   ① InitializeGameThread(device)(ImGuiModule::Initialize から・GameThread・最初の
//      フレーム投入前): フォント ITexture + サンプラーを生成・アップロードする(MT 安全の要)。
//   ② IViewPass::Initialize(seam・RenderThread・録画窓内): mesh2d パイプライン +
//      DescriptorSet(フォントテクスチャ/サンプラー束ね) + VB/IB/UBO 用 DynamicBufferRing を
//      生成する(ShaderManager は seam の ViewRenderContext からのみ得られるため・パイプライン/
//      バッファ生成はテクスチャ Status を触らず MT 安全)。両段とも完了で m_bInitialized。
// Setup は no-op。Execute は seam の load render pass を Begin→自前 Mesh2D 描画→End する。
// Shutdown で全 RHI リソースを解放する(RenderThread 静止後・device 生存中に ImGuiModule 駆動)。
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
         * @brief フォントアトラスを GameThread で GPU へアップロードする(MT 安全化の要・①)
         *
         * ImGuiModule::Initialize(GameThread・最初のフレーム投入前=RenderThread アイドルで
         * グラフィックスキューを GameThread が専有)から呼ばれる。io.Fonts->GetTexDataAsRGBA32 の
         * CPU ピクセル(RGBA8)を ITexture へ一度だけアップロードし、linear/clamp サンプラーを
         * 生成して保持する。続けて io.Fonts->SetTexID へ非 Invalid のセンチネルを設定する(全
         * フレームキャプチャ前に GameThread で確定する必要があるため)。DescriptorSet/パイプライン/
         * バッファの生成は seam の Initialize(RenderThread・ShaderManager 利用可)で行うが、
         * テクスチャ実体の生成・アップロードと TexID 確定はここ(GameThread)で済ませ、以後
         * RenderThread はテクスチャに一切触れない。
         *
         * @param device バックエンド生成元の RHI device(借用・seam と同一実体)
         * @return フォントテクスチャ/サンプラーの生成・アップロードに成功した場合 true
         */
        bool InitializeGameThread(NorvesLib::RHI::IDevice *device);

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
        // GPU 資源(VB/IB/UBO リングのスロットと DescriptorSet)の組の数。飛行中のスロット
        // (context.FrameIndex)で引くので、スワップチェーンの飛行中フレーム数の上限以上にする。
        static constexpr uint32_t IN_FLIGHT_SET_COUNT = NorvesLib::Core::Rendering::FRAME_PACKET_BUFFER_COUNT;

        // 単一フレームの ImDrawData を自前描画する(Execute・RenderThread)。snapshot の全
        // ImDrawList を 1 本の VB/IB へ連結 Upload し、各 ImDrawCmd を Mesh2D DrawCommand へ
        // 変換して context.Renderer->ExecuteDrawCommands で発行する。inFlightIndex は飛行中の
        // スロット(context.FrameIndex)で、GPU 資源の組を選ぶ。
        void RenderDrawData(NorvesLib::Core::Rendering::ViewRenderContext &context,
                            const ::ImDrawData *drawData,
                            uint32_t inFlightIndex);

        // GameThread ①: フォント ITexture(借用 device 所有でなく本パス所有)。
        NorvesLib::RHI::TexturePtr m_FontTexture;
        // GameThread ①: linear/clamp サンプラー(本パス所有)。
        NorvesLib::RHI::SamplerPtr m_FontSampler;

        // seam ②: mesh2d パイプライン(overlay load RP に対して生成・本パス所有)。
        NorvesLib::RHI::PipelinePtr m_Pipeline;
        // seam ②: 飛行中のスロットごとの DescriptorSet(本パス所有・context.FrameIndex で引く)。
        // 各組は「共有フォントテクスチャ/サンプラー(binding1)」+「同じ飛行中スロットの固定 UBO
        // buffer(binding0)」を Initialize で一度だけ束ね、以後 Update しない(=描画中に
        // vkUpdateDescriptorSets を起こさない)。Execute はその UBO buffer の中身のみを
        // DynamicBufferRing 経由で書き換える(CPU マップ)。同じ飛行中スロットの前の利用は
        // BeginFrame のフェンスで GPU 完了済みなので、書き換えは前の提出と重ならない。
        // アトラスは 1 枚のみ・全組でテクスチャ同一のため、Execute は cmd.GetTexID() を
        // 解決に使わず常に当該組の DescriptorSet を直接バインドする。
        NorvesLib::RHI::DescriptorSetPtr m_SlotDescriptorSets[IN_FLIGHT_SET_COUNT];
        // seam ②: scale/translate UBO(飛行中のスロットごと・固定 16B でリサイズしない・本パス所有)。
        NorvesLib::Core::Rendering::DynamicBufferRing m_UniformRing;
        // seam ②: 頂点バッファリング(飛行中のスロットごと・本パス所有)。Execute で全 ImDrawList を連結 Upload。
        NorvesLib::Core::Rendering::DynamicBufferRing m_VertexRing;
        // seam ②: インデックスバッファリング(飛行中のスロットごと・本パス所有)。
        NorvesLib::Core::Rendering::DynamicBufferRing m_IndexRing;

        // FramePacket スロットごとの ImDrawData ディープクローン(本パス所有・CPU の写し)。
        // GameThread が OnAssignedToPacket で書込み、RenderThread が Execute で読む。パケットの
        // スロットはプール排他(Writing=GT / Reading=RT)なので CPU 側では重ならない。GPU 資源は
        // パケットのスロットでなく飛行中のスロットで引く(上の各リング/DescriptorSet を参照)。
        ImGuiDrawSnapshot m_SlotSnapshots[NorvesLib::Core::Rendering::FRAME_PACKET_BUFFER_COUNT];

        // 本フレームのライブ ImDrawData(GameThread 借用・次 NewFrame まで有効)。RT は読まない。
        const ::ImDrawData *m_PendingDrawData = nullptr;

        // ① 完了(フォントテクスチャ/サンプラー生成済み)。② 完了は基底 IViewPass の
        // m_bInitialized(IsInitialized() で参照)で表す。Execute は m_bInitialized でのみ描画する。
        bool m_bFontReady = false;
    };
} // namespace NorvesLib::Modules::Gui
