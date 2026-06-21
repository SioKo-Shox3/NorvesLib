#include "ImGuiModule/ImGuiDrawSnapshot.h"

namespace NorvesLib::Modules::Gui
{
    ImGuiDrawSnapshot::~ImGuiDrawSnapshot()
    {
        ReleaseClonedLists();
    }

    void ImGuiDrawSnapshot::ReleaseClonedLists()
    {
        // CloneOutput() は IM_NEW で割り当てるため IM_DELETE で解放する。
        for (::ImDrawList *list : m_ClonedLists)
        {
            if (list != nullptr)
            {
                IM_DELETE(list);
            }
        }
        m_ClonedLists.clear();
    }

    void ImGuiDrawSnapshot::Capture(const ::ImDrawData *source)
    {
        // 前回の複製を破棄する。本スナップショットは FramePacket スロット 1 個に専属し、
        // 呼び出し側は「書き込み中パケットのスロット index」に対応するスナップショットへ
        // のみ Capture する。当該スロットが Writing である間は RT が同スロットを読まない
        // ことをプールが保証するため、ここでの破棄が RT の読取中クローンに当たることはない。
        ReleaseClonedLists();
        m_TexturesCopy.clear();

        // 複製 ImDrawData を一旦リセット(空=CmdListsCount 0 で no-op になる)。
        m_DrawData = ::ImDrawData{};

        if (source == nullptr || !source->Valid || source->CmdListsCount <= 0)
        {
            // 描画コマンド無し: 空のまま返す(overlay は何も描かない)。
            return;
        }

        // 各 ImDrawList の出力 3 バッファ(Vtx/Idx/Cmd)を CloneOutput() で複製する。
        const int listCount = source->CmdLists.Size;
        m_ClonedLists.reserve(static_cast<size_t>(listCount));

        for (int i = 0; i < listCount; ++i)
        {
            const ::ImDrawList *src = source->CmdLists[i];
            if (src == nullptr)
            {
                continue;
            }
            ::ImDrawList *clone = src->CloneOutput();
            if (clone == nullptr)
            {
                continue;
            }
            m_ClonedLists.push_back(clone);
        }

        // 複製 ImDrawList* 配列を指す ImDrawData を再構築する。CmdLists(ImVector)へ
        // 複製ポインタを写し、display 情報を引き継ぐ。
        const int clonedCount = static_cast<int>(m_ClonedLists.size());
        m_DrawData.Valid = true;
        m_DrawData.CmdListsCount = clonedCount;
        m_DrawData.CmdLists.resize(clonedCount);
        for (int i = 0; i < clonedCount; ++i)
        {
            m_DrawData.CmdLists[i] = m_ClonedLists[static_cast<size_t>(i)];
        }

        // 合計頂点/index 数を再計算する(複製で件数は不変だが厳密に積む)。
        int totalVtx = 0;
        int totalIdx = 0;
        for (int i = 0; i < clonedCount; ++i)
        {
            const ::ImDrawList *list = m_DrawData.CmdLists[i];
            totalVtx += list->VtxBuffer.Size;
            totalIdx += list->IdxBuffer.Size;
        }
        m_DrawData.TotalVtxCount = totalVtx;
        m_DrawData.TotalIdxCount = totalIdx;

        m_DrawData.DisplayPos = source->DisplayPos;
        m_DrawData.DisplaySize = source->DisplaySize;
        m_DrawData.FramebufferScale = source->FramebufferScale;
        m_DrawData.OwnerViewport = source->OwnerViewport;

        // Textures: ライブ PlatformIO.Textures は ImGui::Render(EndFrame) 内の
        // UpdateTexturesEndFrame が毎フレーム resize(0)→再構築する container であり、
        // RT がこれを直接走査すると container レベルで GT と競合する。よって container の
        // ポインタ一覧をスロット所有 m_TexturesCopy へ複製し、クローンの Textures を
        // そのコピーへ向ける。要素 ImTextureData* 実体は context 所有でフレーム跨ぎ安定の
        // ためポインタはそのまま保持する(実体の deep copy はしない)。source->Textures が
        // null のとき(手動テクスチャ管理時)はクローンも null にして RT のテクスチャ更新
        // ループを完全スキップさせる。
        if (source->Textures != nullptr)
        {
            const ImVector<::ImTextureData *> &srcTextures = *source->Textures;
            m_TexturesCopy.reserve(srcTextures.Size);
            for (int i = 0; i < srcTextures.Size; ++i)
            {
                m_TexturesCopy.push_back(srcTextures[i]);
            }
            m_DrawData.Textures = &m_TexturesCopy;
        }
        else
        {
            m_DrawData.Textures = nullptr;
        }
    }
} // namespace NorvesLib::Modules::Gui
