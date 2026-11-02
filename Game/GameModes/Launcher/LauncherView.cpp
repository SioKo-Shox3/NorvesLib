#include "GameModes/Launcher/LauncherView.h"

#if defined(NORVES_ENABLE_IMGUI)

#include "GameModes/Launcher/LauncherMenu.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Logging/LogMacros.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "imgui.h"
#include "stb_image.h"

namespace Game::GameModes
{

    namespace
    {
        using NorvesLib::Modules::Gui::ImGuiFontSlot;

        // 登録した画像と寸法はシーンの一覧と同じ添字で保持する。
        struct LauncherThumbnail
        {
            uint64_t Id = 0;
            int Width = 0;
            int Height = 0;
        };
        LauncherThumbnail g_LauncherThumbnails[Game::Scenes::kSceneCatalogCount] = {};

        // 文言（UTF-8 の固定の文字列）。ここに書いた文字は RegisterLauncherDisplayText で字のアトラスへ載る。
        constexpr const char *kTitleText = "NorvesLib";
        constexpr const char *kSubtitleText = "シーンを選んで始めます";
        constexpr const char *kQuitText = "ゲームを終了";
        constexpr const char *kMissingText = "資産が足りません";
        constexpr const char *kMissingFetchPrefix = "資産が足りません。取得: ";
        constexpr const char *kHintText = "矢印キー・WASD・マウス・ゲームパッドで選び、Enter・クリック・Aボタンで決めます";
        constexpr const char *kEllipsisText = "…";

        // 配色（夕景の空に合わせた暖色のアクセント）
        constexpr ImU32 kColorAccent = IM_COL32(255, 176, 92, 255);
        constexpr ImU32 kColorText = IM_COL32(244, 238, 230, 255);
        constexpr ImU32 kColorTextDim = IM_COL32(206, 208, 220, 235);
        constexpr ImU32 kColorWarning = IM_COL32(255, 196, 96, 255);
        constexpr ImU32 kColorShadow = IM_COL32(0, 0, 0, 150);
        constexpr ImU32 kColorCard = IM_COL32(14, 16, 28, 214);
        constexpr ImU32 kColorCardSelected = IM_COL32(28, 26, 38, 232);
        constexpr ImU32 kColorEdge = IM_COL32(255, 255, 255, 40);

        /// 1080 を基準にした画面の高さの比で、基準の長さ（px）を実際の長さにする
        struct Metrics
        {
            float U = 1.0f;
            float Px(float basis) const
            {
                return basis * U;
            }
        };

        ImFont *GetFont(ImGuiIO &io, ImGuiFontSlot slot)
        {
            const int index = static_cast<int>(slot);
            if (index >= 0 && index < io.Fonts->Fonts.Size && io.Fonts->Fonts[index] != nullptr)
            {
                return io.Fonts->Fonts[index];
            }
            return io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
        }

        int Utf8CharLength(unsigned char lead)
        {
            if (lead < 0x80)
            {
                return 1;
            }
            if ((lead >> 5) == 0x6)
            {
                return 2;
            }
            if ((lead >> 4) == 0xE)
            {
                return 3;
            }
            if ((lead >> 3) == 0x1E)
            {
                return 4;
            }
            return 1;
        }

        /// 行頭に来てはいけない文字（句読点・閉じ括弧・長音など）か
        bool IsLineHeadProhibited(const char *text, const char *textEnd)
        {
            static constexpr const char *kProhibited[] = {"、", "。", "，", "．", "）", "」", "』", "】", "！", "？", "ー", "…"};
            for (const char *prohibited : kProhibited)
            {
                const std::size_t length = std::strlen(prohibited);
                if (static_cast<std::size_t>(textEnd - text) >= length && std::strncmp(text, prohibited, length) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        struct TextLine
        {
            const char *Begin = nullptr;
            const char *End = nullptr;
        };

        /// 1行の幅が maxWidth に収まるよう、1文字ずつ区切って折り返す（日本語は語の切れ目が無い）。
        /// 戻り値は行の数で、収まらない続きがあれば bTruncated を真にする
        int WrapLines(ImFont *font, const char *text, float maxWidth, int maxLines, TextLine *lines, bool &bTruncated)
        {
            bTruncated = false;
            const float size = font->LegacySize;
            const char *textEnd = text + std::strlen(text);
            const char *cursor = text;
            int count = 0;
            while (cursor < textEnd && count < maxLines)
            {
                const char *lineBegin = cursor;
                const char *lineEnd = cursor;
                while (lineEnd < textEnd && *lineEnd != '\n')
                {
                    const char *next = lineEnd + Utf8CharLength(static_cast<unsigned char>(*lineEnd));
                    if (next > textEnd)
                    {
                        next = textEnd;
                    }
                    const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, lineBegin, next).x;
                    if (width > maxWidth && lineEnd > lineBegin)
                    {
                        if (IsLineHeadProhibited(lineEnd, textEnd))
                        {
                            // 禁則の文字をぶら下げず、直前の文字から次の行へ送る。
                            // 連続する禁則の文字も一緒に送り、空行になる場合は幅の制限を優先する。
                            const char *previous = lineEnd;
                            do
                            {
                                --previous;
                                while (previous > lineBegin && (static_cast<unsigned char>(*previous) & 0xC0) == 0x80)
                                {
                                    --previous;
                                }
                            } while (previous > lineBegin && IsLineHeadProhibited(previous, textEnd));
                            if (previous > lineBegin)
                            {
                                lineEnd = previous;
                            }
                        }
                        break;
                    }
                    // 1文字だけで幅を超える場合も、その文字を消費して先へ進める。
                    lineEnd = next;
                }
                lines[count].Begin = lineBegin;
                lines[count].End = lineEnd;
                ++count;
                cursor = lineEnd;
                if (cursor < textEnd && *cursor == '\n')
                {
                    ++cursor;
                }
            }
            bTruncated = cursor < textEnd;
            return count;
        }

        /// 行を描く。続きが切れている最後の行は、末尾を削って「…」を付ける
        void DrawLines(ImDrawList *drawList, ImFont *font, const TextLine *lines, int count, bool bTruncated,
                       ImVec2 origin, float lineHeight, float maxWidth, ImU32 color, bool bCentered = false)
        {
            const float size = font->LegacySize;
            for (int index = 0; index < count; ++index)
            {
                const char *end = lines[index].End;
                const bool bEllipsis = bTruncated && index == count - 1;
                const float ellipsisWidth =
                    bEllipsis ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, kEllipsisText, nullptr).x : 0.0f;
                if (bEllipsis)
                {
                    while (end > lines[index].Begin &&
                           font->CalcTextSizeA(size, FLT_MAX, 0.0f, lines[index].Begin, end).x + ellipsisWidth >
                               maxWidth)
                    {
                        --end;
                        while (end > lines[index].Begin && (static_cast<unsigned char>(*end) & 0xC0) == 0x80)
                        {
                            --end;
                        }
                    }
                }
                const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, lines[index].Begin, end).x;
                const float offsetX = bCentered ? std::max((maxWidth - width - ellipsisWidth) * 0.5f, 0.0f) : 0.0f;
                const ImVec2 position(origin.x + offsetX, origin.y + lineHeight * static_cast<float>(index));
                drawList->AddText(font, size, position, color, lines[index].Begin, end);
                if (bEllipsis)
                {
                    drawList->AddText(font, size, ImVec2(position.x + width, position.y), color, kEllipsisText);
                }
            }
        }

        /// 影付きの1行
        void DrawShadowedText(ImDrawList *drawList, ImFont *font, ImVec2 position, ImU32 color, const char *text,
                              float shadowOffset)
        {
            const float size = font->LegacySize;
            drawList->AddText(font, size, ImVec2(position.x + shadowOffset, position.y + shadowOffset), kColorShadow,
                              text);
            drawList->AddText(font, size, position, color, text);
        }

        float TextWidth(ImFont *font, const char *text)
        {
            return font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text).x;
        }

        /// 文字列から決まる色相（見本画像が無いカードの暗いグラデーション）
        float HueFromText(const char *text)
        {
            uint32_t hash = 2166136261u;
            for (const char *cursor = text; *cursor != '\0'; ++cursor)
            {
                hash = (hash ^ static_cast<unsigned char>(*cursor)) * 16777619u;
            }
            return static_cast<float>(hash % 360u) / 360.0f;
        }

        ImU32 HsvColor(float hue, float saturation, float value, float alpha)
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, value, r, g, b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, alpha));
        }

        /// カード1枚の置き場所
        struct CardRect
        {
            ImVec2 Min;
            ImVec2 Max;
        };
    } // namespace

    void RegisterLauncherDisplayText()
    {
        using NorvesLib::Modules::Gui::AddImGuiDisplayText;
        AddImGuiDisplayText(kTitleText);
        AddImGuiDisplayText(kSubtitleText);
        AddImGuiDisplayText(kQuitText);
        AddImGuiDisplayText(kMissingText);
        AddImGuiDisplayText(kMissingFetchPrefix);
        AddImGuiDisplayText(kHintText);
        AddImGuiDisplayText(kEllipsisText);
        for (std::size_t index = 0; index < Game::Scenes::kSceneCatalogCount; ++index)
        {
            const Game::Scenes::SceneDescriptor &scene = Game::Scenes::kSceneCatalog[index];
            AddImGuiDisplayText(scene.DisplayName);
            AddImGuiDisplayText(scene.Description);
            AddImGuiDisplayText(scene.FetchCommand);
        }
    }

    void RegisterLauncherThumbnails()
    {
        // 失敗した画像も含めて登録を一度だけ試し、重複登録と警告の繰り返しを防ぐ。
        static bool bRegistrationAttempted = false;
        if (bRegistrationAttempted)
        {
            return;
        }
        bRegistrationAttempted = true;

        NorvesLib::Core::Container::AnsiString missingIds;
        uint32_t missingCount = 0;
        uint32_t registeredCount = 0;
        for (std::size_t index = 0; index < Game::Scenes::kSceneCatalogCount; ++index)
        {
            const Game::Scenes::SceneDescriptor &scene = Game::Scenes::kSceneCatalog[index];
            LauncherThumbnail &thumbnail = g_LauncherThumbnails[index];
            if (scene.ThumbnailPath != nullptr && scene.ThumbnailPath[0] != '\0')
            {
                std::filesystem::path resolvedPath(scene.ThumbnailPath);
#if defined(NORVES_SOURCE_ASSET_DIR)
                // 実行時の作業ディレクトリによらず、リポジトリの Assets を基準に探す。
                resolvedPath = std::filesystem::path(NORVES_SOURCE_ASSET_DIR).parent_path() / resolvedPath;
#endif
                std::error_code error;
                if (std::filesystem::exists(resolvedPath, error) && !error)
                {
                    int width = 0;
                    int height = 0;
                    int channels = 0;
                    stbi_uc *pixels = stbi_load(resolvedPath.string().c_str(), &width, &height, &channels, 4);
                    // 登録できない寸法はここで除外し、失敗の警告を最後の一行へまとめる。
                    if (pixels != nullptr && width > 0 && height > 0 && width <= 4096 && height <= 4096)
                    {
                        thumbnail.Id = NorvesLib::Modules::Gui::RegisterImGuiImage(
                            pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
                    }
                    stbi_image_free(pixels);
                    if (thumbnail.Id != 0)
                    {
                        thumbnail.Width = width;
                        thumbnail.Height = height;
                    }
                }
            }
            if (thumbnail.Id == 0)
            {
                if (missingCount > 0)
                {
                    missingIds += ",";
                }
                missingIds += scene.Id;
                ++missingCount;
            }
            else
            {
                ++registeredCount;
            }
        }
        if (missingCount > 0)
        {
            LOG_WARNING_F("LAUNCHER_THUMBNAIL_MISSING count=%u ids=%s", missingCount, missingIds.c_str());
        }
        else
        {
            LOG_INFO_F("LAUNCHER_THUMBNAILS registered=%u", registeredCount);
        }
    }

    void LauncherView::SetAssetsMissing(std::size_t sceneIndex, bool bMissing)
    {
        if (sceneIndex < kMaxScenes)
        {
            m_AssetsMissing[sceneIndex] = bMissing;
        }
    }

    bool LauncherView::GetCardImageRect(std::size_t sceneIndex, float &outMinX, float &outMinY,
                                        float &outMaxX, float &outMaxY) const
    {
        if (sceneIndex >= Game::Scenes::kSceneCatalogCount || !m_CardImageRects[sceneIndex].bVisible)
        {
            return false;
        }
        const CardImageRect &rect = m_CardImageRects[sceneIndex];
        outMinX = rect.MinX;
        outMinY = rect.MinY;
        outMaxX = rect.MaxX;
        outMaxY = rect.MaxY;
        return true;
    }

    void LauncherView::OnImGui()
    {
        // 描画しなかったフレームや行について、前の矩形を公開しない。
        for (CardImageRect &rect : m_CardImageRects)
        {
            rect.bVisible = false;
        }

        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        if (m_pMenu == nullptr || display.x <= 0.0f || display.y <= 0.0f)
        {
            return;
        }

        ImFont *titleFont = GetFont(io, ImGuiFontSlot::DisplayTitle);
        ImFont *headingFont = GetFont(io, ImGuiFontSlot::DisplayHeading);
        ImFont *bodyFont = GetFont(io, ImGuiFontSlot::DisplayBody);
        ImFont *captionFont = GetFont(io, ImGuiFontSlot::DisplayCaption);
        if (titleFont == nullptr || headingFont == nullptr || bodyFont == nullptr || captionFont == nullptr)
        {
            return;
        }

        Metrics metrics;
        metrics.U = display.y / 1080.0f;

        constexpr int kCardCount = static_cast<int>(Game::Scenes::kSceneCatalogCount);
        constexpr int kMaxDescriptionLines = 2;
        constexpr int kMaxWarningLines = 3;

        // ---- 配置 ----
        // カードは見本画像（16:9）の全面で、名前・説明・足りない資産の案内は画像の下部に重ねる。
        const float margin = metrics.Px(96.0f);
        const float gap = metrics.Px(36.0f);
        const float textPad = metrics.Px(16.0f);
        const float cardRadius = metrics.Px(8.0f);
        const float nameLineHeight = headingFont->LegacySize * 1.25f;
        const float descLineHeight = bodyFont->LegacySize * 1.35f;
        const float warnLineHeight = captionFont->LegacySize * 1.3f;

        const float areaTop = metrics.Px(236.0f);
        const float areaBottom = display.y - metrics.Px(186.0f);
        const float areaHeight = areaBottom - areaTop;
        const float areaWidth = display.x - margin * 2.0f;

        const float minCardWidth = metrics.Px(360.0f);
        const float maxCardWidth = metrics.Px(640.0f);
        const int maxColumns = std::min(4, std::max(kCardCount, 1));
        int columns = 1;
        float cardWidth = 0.0f;
        // 全行を収めたときの幅を比較する。同じ幅なら列の少ない並びを選ぶ。
        for (int candidate = 1; candidate <= maxColumns; ++candidate)
        {
            const int candidateRows = std::max((kCardCount + candidate - 1) / candidate, 1);
            const float byWidth = (areaWidth - gap * static_cast<float>(candidate - 1)) / static_cast<float>(candidate);
            const float rowHeight =
                (areaHeight - gap * static_cast<float>(candidateRows - 1)) / static_cast<float>(candidateRows);
            const float width = std::min(maxCardWidth, std::min(byWidth, rowHeight * (16.0f / 9.0f)));
            if (width > cardWidth)
            {
                columns = candidate;
                cardWidth = width;
            }
        }

        const bool bScrollRows = cardWidth < minCardWidth;
        if (bScrollRows)
        {
            // 最小幅で横に入る最大の列数にし、縦方向だけを行単位で送る。
            columns = 1;
            for (int candidate = 2; candidate <= maxColumns; ++candidate)
            {
                if (minCardWidth * static_cast<float>(candidate) + gap * static_cast<float>(candidate - 1) <= areaWidth)
                {
                    columns = candidate;
                }
            }
            cardWidth = minCardWidth;
        }
        const int rows = (kCardCount + columns - 1) / columns;
        const float cardHeight = cardWidth * (9.0f / 16.0f);
        const int visibleRows = bScrollRows
                                    ? std::min(rows, std::max(1, static_cast<int>((areaHeight + gap) / (cardHeight + gap))))
                                    : rows;

        // 方向の入力を反映してから、選択行が見える位置へ先頭行を動かす。
        m_pMenu->ApplyNavigation(columns);
        m_FirstVisibleRow = std::min(m_FirstVisibleRow, std::max(rows - visibleRows, 0));
        const int navigationSelected = m_pMenu->GetSelected();
        if (navigationSelected >= 0 && navigationSelected < kCardCount && navigationSelected != m_pMenu->GetQuitIndex())
        {
            const int selectedRow = navigationSelected / columns;
            if (selectedRow < m_FirstVisibleRow)
            {
                m_FirstVisibleRow = selectedRow;
            }
            else if (selectedRow >= m_FirstVisibleRow + visibleRows)
            {
                m_FirstVisibleRow = selectedRow - visibleRows + 1;
            }
        }
        const int firstVisibleCard = m_FirstVisibleRow * columns;
        const int endVisibleCard = std::min(kCardCount, (m_FirstVisibleRow + visibleRows) * columns);
        const float gridHeight =
            cardHeight * static_cast<float>(visibleRows) + gap * static_cast<float>(std::max(visibleRows - 1, 0));
        const float gridTop = areaTop + std::max((areaHeight - gridHeight) * 0.5f, 0.0f);

        CardRect cards[Game::Scenes::kSceneCatalogCount > 0 ? Game::Scenes::kSceneCatalogCount : 1];
        for (int index = firstVisibleCard; index < endVisibleCard; ++index)
        {
            const int row = index / columns;
            const int column = index % columns;
            const int inRow = std::min(columns, kCardCount - row * columns);
            const float rowWidth = cardWidth * static_cast<float>(inRow) + gap * static_cast<float>(inRow - 1);
            const float rowLeft = (display.x - rowWidth) * 0.5f;
            cards[index].Min = ImVec2(rowLeft + (cardWidth + gap) * static_cast<float>(column),
                                      gridTop + (cardHeight + gap) * static_cast<float>(row - m_FirstVisibleRow));
            cards[index].Max = ImVec2(cards[index].Min.x + cardWidth, cards[index].Min.y + cardHeight);
        }

        const float quitWidth = metrics.Px(360.0f);
        const float quitHeight = metrics.Px(64.0f);
        const CardRect quitRect = {
            ImVec2((display.x - quitWidth) * 0.5f, display.y - metrics.Px(150.0f)),
            ImVec2((display.x + quitWidth) * 0.5f, display.y - metrics.Px(150.0f) + quitHeight),
        };

        // ---- 入力（マウスのホバーとクリック）----
        const bool bMouseValid = io.MousePos.x > -FLT_MAX * 0.5f && io.MousePos.y > -FLT_MAX * 0.5f;
        const bool bMouseMoved = bMouseValid && (std::fabs(io.MousePos.x - m_LastMouseX) > 0.5f ||
                                                 std::fabs(io.MousePos.y - m_LastMouseY) > 0.5f);

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##NorvesLauncher", nullptr, kWindowFlags))
        {
            for (int index = firstVisibleCard; index < endVisibleCard; ++index)
            {
                ImGui::PushID(index);
                ImGui::SetCursorScreenPos(cards[index].Min);
                const bool bClicked = ImGui::InvisibleButton(
                    "card", ImVec2(cards[index].Max.x - cards[index].Min.x, cards[index].Max.y - cards[index].Min.y));
                if (bClicked)
                {
                    m_pMenu->SetSelected(index);
                    m_pMenu->RequestActivate(index);
                }
                else if (bMouseMoved && ImGui::IsItemHovered())
                {
                    m_pMenu->SetSelected(index);
                }
                ImGui::PopID();
            }
            {
                ImGui::PushID(kCardCount);
                ImGui::SetCursorScreenPos(quitRect.Min);
                const bool bClicked = ImGui::InvisibleButton(
                    "quit", ImVec2(quitRect.Max.x - quitRect.Min.x, quitRect.Max.y - quitRect.Min.y));
                if (bClicked)
                {
                    m_pMenu->SetSelected(m_pMenu->GetQuitIndex());
                    m_pMenu->RequestActivate(m_pMenu->GetQuitIndex());
                }
                else if (bMouseMoved && ImGui::IsItemHovered())
                {
                    m_pMenu->SetSelected(m_pMenu->GetQuitIndex());
                }
                ImGui::PopID();
            }

            // ---- 描画 ----
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const int selected = m_pMenu->GetSelected();

            // 背景の空の上に、字が読めるよう上と下を暗くする
            drawList->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(display.x, metrics.Px(320.0f)),
                                              IM_COL32(4, 6, 14, 150), IM_COL32(4, 6, 14, 150), IM_COL32(4, 6, 14, 0),
                                              IM_COL32(4, 6, 14, 0));
            drawList->AddRectFilledMultiColor(ImVec2(0.0f, display.y * 0.30f), ImVec2(display.x, display.y),
                                              IM_COL32(6, 8, 18, 0), IM_COL32(6, 8, 18, 0), IM_COL32(6, 8, 18, 190),
                                              IM_COL32(6, 8, 18, 190));

            // タイトル
            const float titleY = metrics.Px(40.0f);
            DrawShadowedText(drawList, titleFont, ImVec2(margin, titleY), kColorText, kTitleText, metrics.Px(3.0f));
            const float subtitleY = titleY + titleFont->LegacySize * 1.12f;
            DrawShadowedText(drawList, bodyFont, ImVec2(margin + metrics.Px(4.0f), subtitleY), kColorText,
                             kSubtitleText, metrics.Px(2.0f));
            drawList->AddRectFilled(ImVec2(margin + metrics.Px(4.0f), subtitleY + bodyFont->LegacySize * 1.6f),
                                    ImVec2(margin + metrics.Px(100.0f), subtitleY + bodyFont->LegacySize * 1.6f +
                                                                              metrics.Px(4.0f)),
                                    kColorAccent, metrics.Px(2.0f));

            // カード
            for (int index = firstVisibleCard; index < endVisibleCard; ++index)
            {
                const Game::Scenes::SceneDescriptor &scene = Game::Scenes::kSceneCatalog[index];
                const bool bSelected = index == selected;
                const float lift = bSelected ? -metrics.Px(8.0f) : 0.0f;
                const ImVec2 min(cards[index].Min.x, cards[index].Min.y + lift);
                const ImVec2 max(cards[index].Max.x, cards[index].Max.y + lift);

                if (bSelected)
                {
                    for (int ring = 3; ring >= 1; --ring)
                    {
                        const float spread = metrics.Px(4.0f) * static_cast<float>(ring);
                        drawList->AddRect(ImVec2(min.x - spread, min.y - spread), ImVec2(max.x + spread, max.y + spread),
                                          IM_COL32(255, 176, 92, 34 / ring), cardRadius + spread, 0,
                                          metrics.Px(4.0f));
                    }
                }

                // 見本画像。画像が無いカードは、シーンの名前から決めた暗いグラデーションにする
                const float inset = metrics.Px(2.0f);
                const ImVec2 imageMin(min.x + inset, min.y + inset);
                const ImVec2 imageMax(max.x - inset, max.y - inset);
                m_CardImageRects[index] = {imageMin.x, imageMin.y, imageMax.x, imageMax.y, true};
                const LauncherThumbnail &thumbnail = g_LauncherThumbnails[index];
                const bool bImageReady = thumbnail.Id != 0 && NorvesLib::Modules::Gui::IsImGuiImageReady(thumbnail.Id);
                drawList->AddRectFilled(min, max, kColorCard, cardRadius);
                if (bImageReady)
                {
                    // 縦横比を保って矩形を埋め、はみ出す方向を中央で切り取る。
                    ImVec2 uvMin(0.0f, 0.0f);
                    ImVec2 uvMax(1.0f, 1.0f);
                    const float imageAspect = static_cast<float>(thumbnail.Width) / static_cast<float>(thumbnail.Height);
                    const float rectAspect = (imageMax.x - imageMin.x) / (imageMax.y - imageMin.y);
                    if (imageAspect > rectAspect)
                    {
                        const float uvWidth = rectAspect / imageAspect;
                        uvMin.x = (1.0f - uvWidth) * 0.5f;
                        uvMax.x = 1.0f - uvMin.x;
                    }
                    else
                    {
                        const float uvHeight = imageAspect / rectAspect;
                        uvMin.y = (1.0f - uvHeight) * 0.5f;
                        uvMax.y = 1.0f - uvMin.y;
                    }
                    drawList->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(thumbnail.Id)), imageMin, imageMax,
                                              uvMin, uvMax, IM_COL32_WHITE, std::max(cardRadius - inset, 0.0f));
                }
                else
                {
                    const float hue = HueFromText(scene.Id);
                    drawList->AddRectFilledMultiColor(imageMin, imageMax, HsvColor(hue, 0.38f, 0.34f, 1.0f),
                                                      HsvColor(hue + 0.06f, 0.34f, 0.28f, 1.0f),
                                                      HsvColor(hue + 0.06f, 0.46f, 0.10f, 1.0f),
                                                      HsvColor(hue, 0.50f, 0.12f, 1.0f));
                }

                // 説明文と足りない資産の案内（画像の下部に重ねる）
                const float textLeft = min.x + textPad;
                const float textWidth = cardWidth - textPad * 2.0f;

                TextLine descLines[kMaxDescriptionLines];
                bool bDescTruncated = false;
                const int descCount =
                    WrapLines(bodyFont, scene.Description, textWidth, kMaxDescriptionLines, descLines, bDescTruncated);

                char warning[512] = {};
                TextLine warnLines[kMaxWarningLines];
                bool bWarnTruncated = false;
                int warnCount = 0;
                if (m_AssetsMissing[index])
                {
                    if (scene.FetchCommand != nullptr)
                    {
                        std::snprintf(warning, sizeof(warning), "%s%s", kMissingFetchPrefix, scene.FetchCommand);
                    }
                    else
                    {
                        std::snprintf(warning, sizeof(warning), "%s", kMissingText);
                    }
                    warnCount = WrapLines(captionFont, warning, textWidth, kMaxWarningLines, warnLines, bWarnTruncated);
                }

                const float overlayHeight =
                    textPad + nameLineHeight + metrics.Px(4.0f) + descLineHeight * static_cast<float>(descCount) +
                    (warnCount > 0 ? metrics.Px(6.0f) + warnLineHeight * static_cast<float>(warnCount) : 0.0f) + textPad;
                const float overlayTop = std::max(max.y - overlayHeight, min.y);
                drawList->AddRectFilledMultiColor(
                    ImVec2(imageMin.x, std::max(overlayTop - metrics.Px(36.0f), imageMin.y)),
                    ImVec2(imageMax.x, overlayTop), IM_COL32(8, 10, 20, 0), IM_COL32(8, 10, 20, 0),
                    IM_COL32(8, 10, 20, 200), IM_COL32(8, 10, 20, 200));
                drawList->AddRectFilled(ImVec2(imageMin.x, overlayTop), imageMax, IM_COL32(8, 10, 20, 200));

                // すべてのカードの文字を、上下左右の内側の余白で切り取る。
                drawList->PushClipRect(ImVec2(textLeft, min.y + textPad), ImVec2(max.x - textPad, max.y - textPad), true);

                // 見本画像が無いカードは、画像の部分（重ねた文字の上）の中央にシーン名を出す
                if (!bImageReady)
                {
                    const float freeHeight = overlayTop - min.y;
                    if (freeHeight > headingFont->LegacySize * 1.6f)
                    {
                        TextLine placeholderLine[1];
                        bool bPlaceholderTruncated = false;
                        const int placeholderCount = WrapLines(headingFont, scene.DisplayName, textWidth, 1,
                                                               placeholderLine, bPlaceholderTruncated);
                        if (placeholderCount > 0)
                        {
                            DrawLines(drawList, headingFont, placeholderLine, placeholderCount, bPlaceholderTruncated,
                                      ImVec2(textLeft, min.y + (freeHeight - headingFont->LegacySize) * 0.5f),
                                      nameLineHeight, textWidth, IM_COL32(244, 238, 230, 150), true);
                        }
                    }
                }

                float cursorY = overlayTop + textPad;
                {
                    TextLine nameLine[1];
                    bool bNameTruncated = false;
                    const int nameCount =
                        WrapLines(headingFont, scene.DisplayName, textWidth, 1, nameLine, bNameTruncated);
                    DrawLines(drawList, headingFont, nameLine, nameCount, bNameTruncated, ImVec2(textLeft, cursorY),
                              nameLineHeight, textWidth, bSelected ? kColorAccent : kColorText);
                }
                cursorY += nameLineHeight + metrics.Px(4.0f);
                DrawLines(drawList, bodyFont, descLines, descCount, bDescTruncated, ImVec2(textLeft, cursorY),
                          descLineHeight, textWidth, kColorTextDim);
                cursorY += descLineHeight * static_cast<float>(descCount) + metrics.Px(6.0f);
                if (warnCount > 0)
                {
                    DrawLines(drawList, captionFont, warnLines, warnCount, bWarnTruncated, ImVec2(textLeft, cursorY),
                              warnLineHeight, textWidth, kColorWarning);
                }
                drawList->PopClipRect();

                // 枠は文字用のクリップの外で描く。
                drawList->AddRect(min, max, bSelected ? kColorAccent : kColorEdge, cardRadius, 0,
                                  bSelected ? metrics.Px(3.5f) : metrics.Px(1.5f));
            }

            // 見えていない行が続く方向を、領域の端の小さな三角で示す。
            const float indicatorHalfWidth = metrics.Px(7.0f);
            const float indicatorHeight = metrics.Px(6.0f);
            const float centerX = display.x * 0.5f;
            const ImU32 indicatorColor = (kColorAccent & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 150);
            if (m_FirstVisibleRow > 0)
            {
                drawList->AddTriangleFilled(ImVec2(centerX, areaTop),
                                            ImVec2(centerX + indicatorHalfWidth, areaTop + indicatorHeight),
                                            ImVec2(centerX - indicatorHalfWidth, areaTop + indicatorHeight), indicatorColor);
            }
            if (m_FirstVisibleRow + visibleRows < rows)
            {
                drawList->AddTriangleFilled(ImVec2(centerX - indicatorHalfWidth, areaBottom - indicatorHeight),
                                            ImVec2(centerX + indicatorHalfWidth, areaBottom - indicatorHeight),
                                            ImVec2(centerX, areaBottom), indicatorColor);
            }

            // ゲームを終了
            {
                const bool bSelected = selected == m_pMenu->GetQuitIndex();
                const float lift = bSelected ? -metrics.Px(4.0f) : 0.0f;
                const ImVec2 min(quitRect.Min.x, quitRect.Min.y + lift);
                const ImVec2 max(quitRect.Max.x, quitRect.Max.y + lift);
                if (bSelected)
                {
                    for (int ring = 3; ring >= 1; --ring)
                    {
                        const float spread = metrics.Px(4.0f) * static_cast<float>(ring);
                        drawList->AddRect(ImVec2(min.x - spread, min.y - spread), ImVec2(max.x + spread, max.y + spread),
                                          IM_COL32(255, 176, 92, 34 / ring), quitHeight * 0.5f + spread, 0,
                                          metrics.Px(4.0f));
                    }
                }
                drawList->AddRectFilled(min, max, bSelected ? kColorCardSelected : kColorCard, quitHeight * 0.5f);
                drawList->AddRect(min, max, bSelected ? kColorAccent : kColorEdge, quitHeight * 0.5f, 0,
                                  bSelected ? metrics.Px(3.5f) : metrics.Px(1.5f));
                const float labelWidth = TextWidth(headingFont, kQuitText);
                drawList->AddText(headingFont, headingFont->LegacySize,
                                  ImVec2(min.x + (quitWidth - labelWidth) * 0.5f,
                                         min.y + (quitHeight - headingFont->LegacySize) * 0.5f),
                                  bSelected ? kColorAccent : kColorText, kQuitText);
            }

            // 操作の案内
            {
                const float hintWidth = TextWidth(captionFont, kHintText);
                DrawShadowedText(drawList, captionFont,
                                 ImVec2((display.x - hintWidth) * 0.5f, display.y - metrics.Px(56.0f)),
                                 IM_COL32(214, 218, 232, 210), kHintText, metrics.Px(1.5f));
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        if (bMouseValid)
        {
            m_LastMouseX = io.MousePos.x;
            m_LastMouseY = io.MousePos.y;
        }
    }

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI
