#include "GameModes/MegaBananas/MegaBananasInfoView.h"

#if defined(NORVES_ENABLE_IMGUI)

#include "Core/Public/Debug/DebugConfig.h"

#include "imgui.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace Game::GameModes
{

    namespace
    {
        using NorvesLib::Modules::Gui::ImGuiFontSlot;

        // 文言（UTF-8 の固定の文字列）。ここに書いた文字は RegisterBananasInfoDisplayText で字のアトラスへ載る。
        constexpr const char *kTitleText = "バナナの山";
        constexpr const char *kCountLabel = "本数";
        constexpr const char *kPileLabel = "うち山に積んだ本数";
        constexpr const char *kTrianglesLabel = "1 本の三角形数";
        constexpr const char *kTotalLabel = "元の三角形の総数";
        constexpr const char *kClustersLabel = "1 本のクラスタ数";
        constexpr const char *kGpuLabel = "MegaGeometry の GPU 時間";
        constexpr const char *kDrawnClustersLabel = "描いたクラスタ数";
        // GPU 時間はエンジンの計測（--trace で有効になる）が動いているときだけ取れる
        constexpr const char *kGpuUnavailableValue = "--trace を付けて起動すると出ます";
        constexpr const char *kDrawnUnavailableValue = "計測できません";
        constexpr const char *kMissingText = "バナナの資産がありません。python Scripts/GenerateSceneModels.py --ids banana";
        constexpr const char *kHintText = "H キーで閉じます";
        // 動的に組む文字列（クラスタ数の「（N 段）」）で使う字
        constexpr const char *kLevelText = "（段）";

        constexpr ImU32 kColorAccent = IM_COL32(255, 176, 92, 255);
        constexpr ImU32 kColorText = IM_COL32(244, 238, 230, 255);
        constexpr ImU32 kColorTextDim = IM_COL32(206, 208, 220, 235);
        constexpr ImU32 kColorWarning = IM_COL32(255, 196, 96, 255);
        constexpr ImU32 kColorPanel = IM_COL32(14, 16, 28, 214);
        constexpr ImU32 kColorEdge = IM_COL32(255, 255, 255, 40);

        ImFont *GetFont(ImGuiIO &io, ImGuiFontSlot slot)
        {
            const int index = static_cast<int>(slot);
            if (index >= 0 && index < io.Fonts->Fonts.Size && io.Fonts->Fonts[index] != nullptr)
            {
                return io.Fonts->Fonts[index];
            }
            return io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
        }

        /// 3 桁ごとにカンマを入れた整数の文字列
        void FormatGrouped(uint64_t value, char *out, size_t outSize)
        {
            char digits[32];
            std::snprintf(digits, sizeof(digits), "%llu", static_cast<unsigned long long>(value));
            size_t length = 0;
            while (digits[length] != '\0')
            {
                ++length;
            }
            size_t written = 0;
            for (size_t index = 0; index < length && written + 2 < outSize; ++index)
            {
                if (index > 0 && (length - index) % 3 == 0)
                {
                    out[written++] = ',';
                }
                out[written++] = digits[index];
            }
            out[written] = '\0';
        }

        struct Row
        {
            const char *Label;
            char Value[96];
            bool bDim;
        };
    } // namespace

    void RegisterBananasInfoDisplayText()
    {
        using NorvesLib::Modules::Gui::AddImGuiDisplayText;
        for (const char *text : {kTitleText, kCountLabel, kPileLabel, kTrianglesLabel, kTotalLabel, kClustersLabel,
                                 kGpuLabel, kDrawnClustersLabel, kGpuUnavailableValue, kDrawnUnavailableValue,
                                 kMissingText, kHintText, kLevelText})
        {
            AddImGuiDisplayText(text);
        }
    }

    void MegaBananasInfoView::OnImGui()
    {
        if (m_pInfo == nullptr || m_pVisible == nullptr || !*m_pVisible)
        {
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f)
        {
            return;
        }
        ImFont *titleFont = GetFont(io, ImGuiFontSlot::DisplayHeading);
        ImFont *bodyFont = GetFont(io, ImGuiFontSlot::DisplayBody);
        ImFont *hintFont = GetFont(io, ImGuiFontSlot::DisplayCaption);
        if (titleFont == nullptr || bodyFont == nullptr || hintFont == nullptr)
        {
            return;
        }

        const BananasInfo &info = *m_pInfo;

        // 表の行を組む
        Row rows[8];
        int rowCount = 0;
        const auto addRow = [&](const char *label, bool bDim) -> Row &
        {
            Row &row = rows[rowCount++];
            row.Label = label;
            row.Value[0] = '\0';
            row.bDim = bDim;
            return row;
        };
        char grouped[48];
        FormatGrouped(info.BananaCount, grouped, sizeof(grouped));
        std::snprintf(addRow(kCountLabel, false).Value, sizeof(Row::Value), "%s", grouped);
        FormatGrouped(info.PileCount, grouped, sizeof(grouped));
        std::snprintf(addRow(kPileLabel, true).Value, sizeof(Row::Value), "%s", grouped);
        FormatGrouped(info.TrianglesPerBanana, grouped, sizeof(grouped));
        std::snprintf(addRow(kTrianglesLabel, false).Value, sizeof(Row::Value), "%s", grouped);
        FormatGrouped(info.SourceTriangleTotal, grouped, sizeof(grouped));
        std::snprintf(addRow(kTotalLabel, false).Value, sizeof(Row::Value), "%s", grouped);
        FormatGrouped(info.MeshClusterCount, grouped, sizeof(grouped));
        std::snprintf(addRow(kClustersLabel, true).Value, sizeof(Row::Value), "%s（%u 段）", grouped,
                      static_cast<unsigned>(info.MeshLodLevelCount));
#if NORVES_BUILD_DEVELOPMENT
        {
            Row &gpuRow = addRow(kGpuLabel, false);
            if (info.bGpuTimeValid)
            {
                std::snprintf(gpuRow.Value, sizeof(Row::Value), "%.2f ms", static_cast<double>(info.MegaGeometryGpuMs));
            }
            else
            {
                std::snprintf(gpuRow.Value, sizeof(Row::Value), "%s", kGpuUnavailableValue);
            }
            Row &drawnRow = addRow(kDrawnClustersLabel, true);
            if (info.bDrawnClustersValid)
            {
                FormatGrouped(info.DrawnClusters, grouped, sizeof(grouped));
                std::snprintf(drawnRow.Value, sizeof(Row::Value), "%s", grouped);
            }
            else
            {
                std::snprintf(drawnRow.Value, sizeof(Row::Value), "%s", kDrawnUnavailableValue);
            }
        }
#endif

        // 1080 を基準にした画面の高さの比で、基準の長さ（px）を実際の長さにする
        const float unit = display.y / 1080.0f;
        const auto px = [unit](float basis) { return basis * unit; };
        const float pad = px(20.0f);
        const float lineHeight = bodyFont->LegacySize * 1.35f;
        const float titleHeight = titleFont->LegacySize * 1.3f;
        const float labelWidth = px(300.0f);
        const float valueWidth = px(640.0f);
        const float panelWidth = pad * 2.0f + labelWidth + valueWidth;
        float panelHeight = pad + titleHeight + lineHeight * static_cast<float>(rowCount) + hintFont->LegacySize * 1.6f + pad;
        if (info.bAssetMissing)
        {
            panelHeight += lineHeight;
        }
        const ImVec2 panelMin(px(24.0f), px(24.0f));
        const ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);

        ImGui::SetNextWindowPos(panelMin);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("##NorvesBananasInfo", nullptr, kWindowFlags))
        {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(panelMin, panelMax, kColorPanel, px(10.0f));
            drawList->AddRect(panelMin, panelMax, kColorEdge, px(10.0f), 0, px(1.5f));

            float y = panelMin.y + pad;
            drawList->AddText(titleFont, titleFont->LegacySize, ImVec2(panelMin.x + pad, y), kColorAccent, kTitleText);
            y += titleHeight;
            for (int index = 0; index < rowCount; ++index)
            {
                const ImU32 color = rows[index].bDim ? kColorTextDim : kColorText;
                drawList->AddText(bodyFont, bodyFont->LegacySize, ImVec2(panelMin.x + pad, y), kColorTextDim,
                                  rows[index].Label);
                drawList->AddText(bodyFont, bodyFont->LegacySize, ImVec2(panelMin.x + pad + labelWidth, y), color,
                                  rows[index].Value);
                y += lineHeight;
            }
            if (info.bAssetMissing)
            {
                drawList->AddText(bodyFont, bodyFont->LegacySize, ImVec2(panelMin.x + pad, y), kColorWarning,
                                  kMissingText);
                y += lineHeight;
            }
            drawList->AddText(hintFont, hintFont->LegacySize, ImVec2(panelMin.x + pad, y + px(4.0f)), kColorTextDim,
                              kHintText);
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI
