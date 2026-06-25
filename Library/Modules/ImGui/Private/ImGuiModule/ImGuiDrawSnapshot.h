#pragma once

#include "imgui.h"

#include "CoreTypes.h"

// ImGuiDrawSnapshot — GameThread→RenderThread を跨ぐ ImDrawData のディープクローン。
//
// 背景(threading の核心):
//   ImGui::Render() 後の ImGui::GetDrawData() が返す ImDrawData は、その内部の
//   各 ImDrawList が ImGuiContext 所有であり、翌フレームの NewFrame で再利用
//   (上書き)される。RenderThread がこのライブ ImDrawData を直接読むと、
//   GameThread の次フレーム構築と競合する。よって各 ImDrawList の出力バッファ
//   (VtxBuffer/IdxBuffer/CmdBuffer)を ImDrawList::CloneOutput() で独自ストレージ
//   へ複製し、複製を指す ImDrawData を再構築して RenderThread へ渡す。
//   CloneOutput() は imgui 公式のマルチスレッド描画用クローン手段(出力 3 バッファ
//   のみを IM_NEW した ImDrawList へ複製する。imgui.h:3526 参照)。
//
// 同期前提(FramePacket スロット寿命連動・タイミング非依存):
//   GT↔RT のペーシングは swapchain in-flight 数でなく FramePacket プール
//   (FRAME_PACKET_BUFFER_COUNT スロット)で決まり、GT は毎フレーム RT の読了を
//   待たない(AcquireForWrite はプール満杯でドロップ、WaitForRender は resize 時のみ)。
//   そのため単一の共有スナップショットを毎 Tick 上書きすると、RT が前フレームの
//   クローンを読む時間窓と GT の次フレーム破棄/上書きが重なり得る(use-after-free)。
//   対策として本スナップショットは FramePacket スロットごとに 1 個保持され
//   (ImGuiOverlayPass が snapshot[FRAME_PACKET_BUFFER_COUNT] を持つ)、GT は
//   「書き込み中パケットのスロット index」へ Commit、RT は「処理中パケットのスロット
//   index」から読む。FramePacket プールは「スロットが Empty(=RT 回収済)になるまで
//   GT が Writing しない」ことを保証するため、同一スロットへの GT 書込みと RT 読取は
//   決して重ならない。結果、ドロップ挙動やスレッド速度に依存せず証明可能に安全。
//
// テクスチャ(imgui 1.92 動的アトラス):
//   ImDrawData::Textures は ImGui::GetPlatformIO().Textures[] (ImVector<ImTextureData*>*)
//   を指す。このコンテナ実体は ImGuiContext 所有で、ImGui::Render(EndFrame) 内の
//   UpdateTexturesEndFrame が毎フレーム resize(0)→再構築するため、RT がライブ実体を
//   走査すると container レベルで競合する。よって本スナップショットは container を
//   スロット所有の ImVector<ImTextureData*> へコピーし、クローンの Textures をその
//   コピーへ向ける(container 競合を排除)。要素の ImTextureData* 実体は context 所有で
//   フレーム跨ぎ安定(stable pointer)のためポインタはそのまま保持する。
//   注意: ImTextureData の Status/TexID フィールド自体は GT(ImFontAtlasUpdateNewFrame)
//   と RT(ImGui_ImplVulkan_UpdateTexture)が共有更新するため、container コピーだけでは
//   この read-modify-write 競合は解消しない(残課題。レポート参照)。
namespace NorvesLib::Modules::Gui
{
    /**
     * @brief ImDrawData を GameThread 側で複製し RenderThread へ安全に渡すスナップショット
     *
     * 所有: ImGuiOverlayPass が FramePacket スロットごとに 1 個保持する
     * (snapshot[FRAME_PACKET_BUFFER_COUNT])。各スロットスナップショットの寿命は
     * モジュール/パス所有(RenderThread より長命)。
     * スレッド: Capture() は GameThread(ImGuiOverlayPass::CommitSnapshotToSlot 経由・
     * 書き込み中パケットのスロット index へ)、GetDrawData() の読み取りは RenderThread
     * (overlay seam・処理中パケットのスロット index から)。同一スロットに対する両者の
     * 時間窓は FramePacket プールのスロット排他で決して重ならない。
     */
    class ImGuiDrawSnapshot
    {
    public:
        ImGuiDrawSnapshot() = default;
        ~ImGuiDrawSnapshot();

        ImGuiDrawSnapshot(const ImGuiDrawSnapshot &) = delete;
        ImGuiDrawSnapshot &operator=(const ImGuiDrawSnapshot &) = delete;
        ImGuiDrawSnapshot(ImGuiDrawSnapshot &&) = delete;
        ImGuiDrawSnapshot &operator=(ImGuiDrawSnapshot &&) = delete;

        /**
         * @brief ライブ ImDrawData をクローンへ複製する(GameThread)
         *
         * 前回クローンの ImDrawList を破棄し、各 ImDrawList を CloneOutput() で
         * 複製して保持し、display 情報(DisplayPos/DisplaySize/FramebufferScale)を
         * 複製 ImDrawData へ写す。Textures は container をスロット所有 ImVector へ
         * コピーし、クローンの Textures をそのコピーへ向ける。source が null/Valid=false
         * のときはクローンを空(CmdListsCount=0)にする。
         *
         * 呼び出し側(ImGuiOverlayPass)は本メソッドを「書き込み中パケットのスロット index」に
         * 対応するスナップショットに対してのみ呼ぶこと(スロット排他で RT 読取と分離)。
         *
         * @param source ImGui::GetDrawData() の戻り(null 可)
         */
        void Capture(const ::ImDrawData *source);

        /**
         * @brief 複製済み ImDrawData を取得する(RenderThread・借用)
         *
         * ImGuiOverlayPass::Execute が自前 mesh2d 描画のため走査する。複製の寿命は
         * 本スナップショット(=スロット所有)が保証する。Capture が一度も呼ばれて
         * いない/空のときは CmdListsCount=0 の有効な ImDrawData を返す。
         *
         * @return 複製を指す ImDrawData(本オブジェクト生存中のみ有効)
         */
        const ::ImDrawData *GetDrawData() const
        {
            return &m_DrawData;
        }

        /**
         * @brief 直近 Capture が描画コマンドを 1 つ以上含むか
         */
        bool HasDrawData() const
        {
            return m_DrawData.CmdListsCount > 0;
        }

    private:
        /// 複製済み ImDrawList を IM_DELETE で解放し配列を空にする。
        void ReleaseClonedLists();

        // CloneOutput() が IM_NEW した複製 ImDrawList(本クラスが IM_DELETE で所有解放)。
        // m_DrawData.CmdLists(ImVector)も同じポインタを指すが、所有解放の根拠は本配列。
        Core::Container::VariableArray<::ImDrawList *> m_ClonedLists;

        // Textures container のスロット所有コピー。クローンの ImDrawData::Textures が
        // ライブ PlatformIO.Textures(EndFrame で毎フレーム resize)を直接指さないよう、
        // ポインタ一覧をここへ複製し m_DrawData.Textures をこれへ向ける。
        ImVector<::ImTextureData *> m_TexturesCopy;

        // 複製を指す再構築済み ImDrawData。GetDrawData() がこれを返す。
        ::ImDrawData m_DrawData;
    };
} // namespace NorvesLib::Modules::Gui
