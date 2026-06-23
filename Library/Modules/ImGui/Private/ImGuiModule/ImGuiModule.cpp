#include "ImGuiModule/ImGuiModule.h"
#include "ImGuiModule/ImGuiOverlayPass.h"
#include "ImGuiModule/NorvesImGuiStyle.h"
#include "ImGuiModule/IImGuiView.h"
#include "ImGuiModule/ImGuiViewRegistry.h"

#include "Module/IModule.h"
#include "Module/IRenderModule.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "RHI/IDevice.h"
#include "RHI/IRenderPass.h"
#include "Input/InputSystem.h"
#include "Input/InputState.h"
#include "Input/InputTypes.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

// ImGuiModule — ImGui デバッグオーバーレイのファーストモジュール(第2段 B)。
//
// IModule(寿命) + IRenderModule(描画参加)を実装する。ImGui コンテキストを GameThread で
// 生成・所有し、毎フレーム Tick で NewFrame→UI 構築→Render→ImDrawData ディープクローンを
// 行う。RenderThread はクローンのみを読み、ライブ ImGui コンテキストには一切触れない。
// 描画(RHI リソース生成 + 録画)は ImGuiOverlayPass + 抽象 IImGuiRenderer(Core の Vulkan
// 実装)に閉じ、本モジュールは生 Vulkan を見ない。
//
// 2B-ii(見た目仕上げ): カスタムフォント(Inter 本文 + FontAwesome アイコン + NotoSansJP
// 日本語を 1 ハンドルにマージ)を静的アトラスで事前構築し、テーマ(NorvesImGuiStyle)・
// DPI スケール・キーボード/文字入力ブリッジを加える。
//
// Piece 2(view 登録機構): 固有の窓内容(旧ショーケース窓)はモジュールが持たず、外部
// (Game の SubRoutine 等)が IImGuiView を RegisterImGuiView() で登録する。Tick は NewFrame と
// Render の間で登録済み全 view の OnImGui() を順に呼ぶ(view が 0 件なら何も描かない)。
//
// 2B-ii-b(FreeType + 全範囲強制 bake): フォントラスタライザを FreeType に切替える
// (imgui 既定の stb_truetype より crisp。NorvesThirdParty_ImGui の IMGUI_ENABLE_FREETYPE
// 定義で imgui_draw.cpp が FreeType ローダを選ぶ。本モジュール側のコード変更は不要)。
// さらに、imgui 1.92 の RendererHasTextures 下では Build() は明示範囲を事前 bake せず
// 実描画グリフのみ lazy bake する性質があるため、初期化時に設定済みの全グリフ範囲を
// 強制的に bake(materialize)して静的アトラスを確定する。これにより実行時にアトラスが
// 成長せず、RenderThread が ImTextureData(テクスチャ Status)に触れる必要が恒久的に消える
// (#2/#3/2B-ii-a で残っていた「未描画グリフ不可視」制約の解消)。
namespace NorvesLib::Modules::Gui
{
    namespace
    {
        constexpr const char *kLogCategory = "ImGui";
        constexpr const char *kModuleName = "NorvesImGuiModule";

        // フォント基準サイズ(px)。DPI スケールを乗じて実ラスタサイズにする。
        constexpr float kBaseFontSize = 16.0f;

        /**
         * @brief NORVES_ASSET_DIR からフォントファイルの絶対パスを組み立てる
         *
         * NORVES_ASSET_DIR は CMake で Assets ルートへ展開される(forward slash 正規化済み)。
         * 末尾に "/Fonts/<name>" を連結して返す。マクロ未定義時は相対パスにフォールバック
         * する(通常は定義される)。
         */
        Core::Container::AnsiString MakeFontPath(const char *fileName)
        {
#if defined(NORVES_ASSET_DIR)
            Core::Container::AnsiString path(NORVES_ASSET_DIR);
#else
            Core::Container::AnsiString path("Assets");
#endif
            path += "/Fonts/";
            path += fileName;
            return path;
        }

        /**
         * @brief Core::Input::KeyCode を ImGuiKey へ写像する
         *
         * 未対応キーは ImGuiKey_None を返す(呼び出し側でスキップ)。修飾キーは個別キー
         * (LeftShift 等)を返し、ImGui 側の Mods 集約は io.AddKeyEvent が内部処理する。
         */
        ImGuiKey ToImGuiKey(Core::Input::KeyCode code)
        {
            using KC = Core::Input::KeyCode;
            switch (code)
            {
            // アルファベット
            case KC::A: return ImGuiKey_A;
            case KC::B: return ImGuiKey_B;
            case KC::C: return ImGuiKey_C;
            case KC::D: return ImGuiKey_D;
            case KC::E: return ImGuiKey_E;
            case KC::F: return ImGuiKey_F;
            case KC::G: return ImGuiKey_G;
            case KC::H: return ImGuiKey_H;
            case KC::I: return ImGuiKey_I;
            case KC::J: return ImGuiKey_J;
            case KC::K: return ImGuiKey_K;
            case KC::L: return ImGuiKey_L;
            case KC::M: return ImGuiKey_M;
            case KC::N: return ImGuiKey_N;
            case KC::O: return ImGuiKey_O;
            case KC::P: return ImGuiKey_P;
            case KC::Q: return ImGuiKey_Q;
            case KC::R: return ImGuiKey_R;
            case KC::S: return ImGuiKey_S;
            case KC::T: return ImGuiKey_T;
            case KC::U: return ImGuiKey_U;
            case KC::V: return ImGuiKey_V;
            case KC::W: return ImGuiKey_W;
            case KC::X: return ImGuiKey_X;
            case KC::Y: return ImGuiKey_Y;
            case KC::Z: return ImGuiKey_Z;
            // 数字(最上段)
            case KC::Num0: return ImGuiKey_0;
            case KC::Num1: return ImGuiKey_1;
            case KC::Num2: return ImGuiKey_2;
            case KC::Num3: return ImGuiKey_3;
            case KC::Num4: return ImGuiKey_4;
            case KC::Num5: return ImGuiKey_5;
            case KC::Num6: return ImGuiKey_6;
            case KC::Num7: return ImGuiKey_7;
            case KC::Num8: return ImGuiKey_8;
            case KC::Num9: return ImGuiKey_9;
            // ファンクション
            case KC::F1: return ImGuiKey_F1;
            case KC::F2: return ImGuiKey_F2;
            case KC::F3: return ImGuiKey_F3;
            case KC::F4: return ImGuiKey_F4;
            case KC::F5: return ImGuiKey_F5;
            case KC::F6: return ImGuiKey_F6;
            case KC::F7: return ImGuiKey_F7;
            case KC::F8: return ImGuiKey_F8;
            case KC::F9: return ImGuiKey_F9;
            case KC::F10: return ImGuiKey_F10;
            case KC::F11: return ImGuiKey_F11;
            case KC::F12: return ImGuiKey_F12;
            // 修飾キー
            case KC::LeftShift: return ImGuiKey_LeftShift;
            case KC::RightShift: return ImGuiKey_RightShift;
            case KC::LeftCtrl: return ImGuiKey_LeftCtrl;
            case KC::RightCtrl: return ImGuiKey_RightCtrl;
            case KC::LeftAlt: return ImGuiKey_LeftAlt;
            case KC::RightAlt: return ImGuiKey_RightAlt;
            // 特殊キー
            case KC::Space: return ImGuiKey_Space;
            case KC::Escape: return ImGuiKey_Escape;
            case KC::Tab: return ImGuiKey_Tab;
            case KC::Enter: return ImGuiKey_Enter;
            case KC::Backspace: return ImGuiKey_Backspace;
            case KC::Delete: return ImGuiKey_Delete;
            case KC::Insert: return ImGuiKey_Insert;
            case KC::Home: return ImGuiKey_Home;
            case KC::End: return ImGuiKey_End;
            case KC::PageUp: return ImGuiKey_PageUp;
            case KC::PageDown: return ImGuiKey_PageDown;
            // 矢印
            case KC::Up: return ImGuiKey_UpArrow;
            case KC::Down: return ImGuiKey_DownArrow;
            case KC::Left: return ImGuiKey_LeftArrow;
            case KC::Right: return ImGuiKey_RightArrow;
            // テンキー
            case KC::Numpad0: return ImGuiKey_Keypad0;
            case KC::Numpad1: return ImGuiKey_Keypad1;
            case KC::Numpad2: return ImGuiKey_Keypad2;
            case KC::Numpad3: return ImGuiKey_Keypad3;
            case KC::Numpad4: return ImGuiKey_Keypad4;
            case KC::Numpad5: return ImGuiKey_Keypad5;
            case KC::Numpad6: return ImGuiKey_Keypad6;
            case KC::Numpad7: return ImGuiKey_Keypad7;
            case KC::Numpad8: return ImGuiKey_Keypad8;
            case KC::Numpad9: return ImGuiKey_Keypad9;
            case KC::NumpadAdd: return ImGuiKey_KeypadAdd;
            case KC::NumpadSubtract: return ImGuiKey_KeypadSubtract;
            case KC::NumpadMultiply: return ImGuiKey_KeypadMultiply;
            case KC::NumpadDivide: return ImGuiKey_KeypadDivide;
            case KC::NumpadDecimal: return ImGuiKey_KeypadDecimal;
            case KC::NumpadEnter: return ImGuiKey_KeypadEnter;
            // その他
            case KC::CapsLock: return ImGuiKey_CapsLock;
            case KC::NumLock: return ImGuiKey_NumLock;
            case KC::ScrollLock: return ImGuiKey_ScrollLock;
            case KC::PrintScreen: return ImGuiKey_PrintScreen;
            case KC::Pause: return ImGuiKey_Pause;
            // 記号
            case KC::Semicolon: return ImGuiKey_Semicolon;
            case KC::Equal: return ImGuiKey_Equal;
            case KC::Comma: return ImGuiKey_Comma;
            case KC::Minus: return ImGuiKey_Minus;
            case KC::Period: return ImGuiKey_Period;
            case KC::Slash: return ImGuiKey_Slash;
            case KC::GraveAccent: return ImGuiKey_GraveAccent;
            case KC::LeftBracket: return ImGuiKey_LeftBracket;
            case KC::Backslash: return ImGuiKey_Backslash;
            case KC::RightBracket: return ImGuiKey_RightBracket;
            case KC::Apostrophe: return ImGuiKey_Apostrophe;
            default: return ImGuiKey_None;
            }
        }

        /**
         * @brief ImGui デバッグオーバーレイモジュール(IModule + IRenderModule)
         *
         * GetOverlayPass() で所有する ImGuiOverlayPass を借用返しする。overlay seam が
         * そのパスの Initialize(遅延)/Setup/Execute を駆動する。Shutdown は seam では
         * 駆動されない(IRenderModule 契約=モジュール責務)ため本モジュールの Shutdown で
         * パスの Shutdown を呼ぶ(RenderThread 静止後・device 生存中の前提)。
         */
        class ImGuiModule final : public Core::Module::IModule, public Core::Module::IRenderModule
        {
        public:
            // --- IModule 識別 ---
            Core::Identity GetModuleId() const override
            {
                return Core::Identity(kModuleName);
            }

            const char *GetName() const override
            {
                return kModuleName;
            }

            // --- IModule 寿命(GameThread 単一スレッド) ---
            bool Install(Core::Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Install");
                return true;
            }

            bool Initialize() override
            {
                // ImGui コンテキストを GameThread で生成・所有する。2B-i② で MT 安全化のため、
                // imgui バックエンドの初期化とフォントアトラスの GPU アップロードもこの
                // GameThread 初期化フェーズ(最初のフレーム投入前=RenderThread アイドルで
                // グラフィックスキューを GameThread が専有)で完了させる。これで ImTextureData の
                // Status を GameThread/RenderThread が無ロック共有更新する競合が構造的に消える。
                // 失敗時は false(InstallAll が逆順ロールバック)。
                IMGUI_CHECKVERSION();
                m_Context = ::ImGui::CreateContext();
                if (m_Context == nullptr)
                {
                    NORVES_LOG_ERROR(kLogCategory, "ImGuiModule Initialize failed: CreateContext");
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);

                ImGuiIO &io = ::ImGui::GetIO();
                // imgui 1.92 動的テクスチャ機構。アトラスの GPU アップロードを描画中に
                // バックエンドが自動実行する(RecordDrawData 内)。本構成では GameThread で
                // 事前にアップロードし、RenderThread はテクスチャに触れない(2B-i②)。
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
                // .ini 永続化は無効化(ファイル I/O を持ち込まない)。
                io.IniFilename = nullptr;
                io.LogFilename = nullptr;

                // DPI スケール(取得元が無ければ 1.0)。フォントサイズ・スタイル両方に効かせる。
                m_DpiScale = GetDpiScale();

                // ---- 2B-ii: テーマ + フォント + DPI ----
                // テーマ(ダーク + アクセント 1 色)を 1 箇所で適用する。StyleColorsDark を
                // 起点に NorvesImGuiStyle が角丸/余白/配色を上書きする。
                NorvesImGuiStyle::ApplyNorvesStyle(::ImGui::GetStyle());

                // フォント(Inter + FontAwesome マージ + NotoSansJP マージ)を FreeType で構築する。
                // 全グリフの強制 bake は InitializeBackend の ForceBakeAllGlyphs が GPU アップロード
                // 前に行い、実行時にアトラスが成長しない(RenderThread がテクスチャ更新を要求しない
                // =#2/#3 の構造的解消)。
                if (!SetupFonts(io))
                {
                    NORVES_LOG_ERROR(kLogCategory, "ImGuiModule Initialize failed: SetupFonts");
                    ::ImGui::DestroyContext(m_Context);
                    m_Context = nullptr;
                    return false;
                }

                // DPI スケールをスタイルへ反映(角丸/余白/枠を拡大)。フォントサイズは
                // SetupFonts 内で既に kBaseFontSize * dpiScale で構築済み。
                if (m_DpiScale != 1.0f)
                {
                    ::ImGui::GetStyle().ScaleAllSizes(m_DpiScale);
                }

                // 文字入力(IME 確定後の Unicode)を購読し、Tick で io.AddInputCharacter へ流す。
                // WindowProc(GameThread)→InjectCharEvent→OnCharEvent は GameThread 同期実行の
                // ため、Tick(同 GameThread)とロックレスにバッファ共有できる。
                if (Core::Engine::GEngine != nullptr)
                {
                    Core::Engine::GEngine->GetInputSystem().OnCharEvent().Add(this, &ImGuiModule::OnCharInput);
                    m_bCharSubscribed = true;
                }

                // ---- MT 安全化: バックエンド初期化 + フォントアトラス GPU アップロード(GameThread) ----
                if (!InitializeBackend())
                {
                    // バックエンド初期化失敗。購読解除 + コンテキスト破棄でロールバックする。
                    NORVES_LOG_ERROR(kLogCategory, "ImGuiModule Initialize failed: InitializeBackend");
                    if (m_bCharSubscribed && Core::Engine::GEngine != nullptr)
                    {
                        Core::Engine::GEngine->GetInputSystem().OnCharEvent().Remove(this, &ImGuiModule::OnCharInput);
                        m_bCharSubscribed = false;
                    }
                    ::ImGui::SetCurrentContext(m_Context);
                    ::ImGui::DestroyContext(m_Context);
                    m_Context = nullptr;
                    return false;
                }

                NORVES_LOG_INFO(kLogCategory,
                                "ImGuiModule Initialize: context + backend ready (custom fonts uploaded, uiScale=%.2f)",
                                m_DpiScale);
                return true;
            }

            /**
             * @brief imgui バックエンドを GameThread で初期化しフォントアトラスをアップロードする
             *
             * GEngine の RenderingCoordinator から device と overlay load render pass を取得し、
             * overlay pass の InitializeGameThread を駆動する。事前に ForceBakeAllGlyphs() で
             * 設定済み全グリフ範囲(ASCII / 日本語常用 / FontAwesome アイコン)を強制 bake して
             * から、空の捨てフレーム(NewFrame→Render のみ)を回して GPU へ一度だけ載せる。
             * ForceBakeAllGlyphs が全 common グリフを materialize するため固定窓の描画は不要で、
             * imgui 1.92 の lazy bake 下でも全 common グリフが初回に bake+アップロードされ、以後
             * アトラスは成長せず RenderThread がテクスチャ(ImTextureData::Status)に触れることはない。
             *
             * @return 初期化+アップロードに成功した場合 true
             */
            bool InitializeBackend()
            {
                if (Core::Engine::GEngine == nullptr)
                {
                    NORVES_LOG_ERROR(kLogCategory, "InitializeBackend failed: GEngine null");
                    return false;
                }

                auto &coordinator = Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator();
                Core::Container::TSharedPtr<RHI::IDevice> device = coordinator.GetDevice();
                RHI::IRenderPass *loadRenderPass = coordinator.GetOverlayLoadRenderPass();
                if (Core::Container::IsNull(device) || loadRenderPass == nullptr)
                {
                    NORVES_LOG_WARNING(kLogCategory,
                                       "InitializeBackend skipped: device/overlay load render pass unavailable");
                    return false;
                }

                // 設定済み全グリフ範囲を強制 bake してから空の捨てフレームを回す。imgui 1.92 の
                // RendererHasTextures 下では Build() は明示範囲を事前 bake せず実描画グリフのみ
                // lazy bake するため、ここで全 common グリフ(ASCII / 日本語常用 / FA アイコン)を
                // materialize して静的アトラスを確定する。これにより本番フレームで新規グリフ
                // (=アトラス成長=テクスチャ更新要求)が発生せず、RenderThread が ImTextureData に
                // 触れる必要が生じない。捨てフレームは NewFrame→Render のみ(固定窓の描画は不要
                // =グリフ確定は ForceBakeAllGlyphs が担う)。
                ::ImGui::SetCurrentContext(m_Context);
                {
                    ImGuiIO &io = ::ImGui::GetIO();
                    uint32_t width = 1280;
                    uint32_t height = 720;
                    Core::Engine::GEngine->GetRenderWorld().GetResolution(width, height);
                    if (width == 0 || height == 0)
                    {
                        width = 1280;
                        height = 720;
                    }
                    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
                    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
                    io.DeltaTime = 1.0f / 60.0f;

                    // 全範囲強制 bake(NewFrame の外で実行可。FindGlyph がアトラスへ glyph を
                    // materialize する)。捨てフレームより先に呼び、混在テキストを描く前に
                    // common グリフを確定させる。
                    ForceBakeAllGlyphs(io);

                    ::ImGui::NewFrame();
                    ::ImGui::Render();
                }

                // device + load RP を渡して overlay pass の GameThread 初期化を駆動する。
                // 内部で renderer 生成 → Initialize(パイプライン) → BuildFontAtlas →
                // UploadFontAtlas(全テクスチャを GPU へ同期アップロードし Status=OK 確定)。
                if (!m_OverlayPass.InitializeGameThread(device.get(), loadRenderPass))
                {
                    NORVES_LOG_WARNING(kLogCategory, "InitializeBackend failed: overlay InitializeGameThread");
                    return false;
                }
                return true;
            }

            void Tick(float deltaTime) override
            {
                // GameThread・SyncToSceneView 後・描画前。NewFrame→UI→Render→snapshot を
                // 行い、RenderThread が読むスナップショットを確定する。
                if (m_Context == nullptr)
                {
                    return;
                }
                ::ImGui::SetCurrentContext(m_Context);

                ImGuiIO &io = ::ImGui::GetIO();
                io.DeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);

                UpdateDisplayAndInput(io);

                ::ImGui::NewFrame();

                // 登録済み view の UI を構築する(Piece 2: view 登録機構)。固有の窓内容は
                // モジュールが持たず、外部(Game の SubRoutine 等)が RegisterImGuiView() で
                // 登録した IImGuiView::OnImGui() を登録順に呼ぶ。view が 0 件なら何も描かない
                // (overlay は空=no-op)。GameThread 単一スレッドで反復するためロック不要。
                // ※ ImGui 既定のデモウィンドウ(ShowDemoWindow)は表示しない。
                for (IImGuiView *view : GetRegisteredImGuiViews())
                {
                    if (view != nullptr)
                    {
                        view->OnImGui();
                    }
                }

                ::ImGui::Render();

                // 本フレームのライブ ImDrawData を overlay pass へ借用設定する(次 NewFrame まで
                // 有効)。per-slot ディープクローンは pass の OnAssignedToPacket が行う。
                m_OverlayPass.SetPendingDrawData(::ImGui::GetDrawData());
            }

            void Shutdown() override
            {
                // RenderThread 静止後・device 生存中に駆動される前提。overlay pass の RHI
                // リソースを先に解放し、その後 ImGui コンテキストを破棄する。
                if (m_bCharSubscribed && Core::Engine::GEngine != nullptr)
                {
                    Core::Engine::GEngine->GetInputSystem().OnCharEvent().Remove(this, &ImGuiModule::OnCharInput);
                    m_bCharSubscribed = false;
                }

                m_OverlayPass.Shutdown();

                if (m_Context != nullptr)
                {
                    ::ImGui::SetCurrentContext(m_Context);
                    ::ImGui::DestroyContext(m_Context);
                    m_Context = nullptr;
                }
                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Shutdown");
            }

            void Uninstall(Core::Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Uninstall");
            }

            // --- IRenderModule 描画参加 ---
            Core::Rendering::IViewPass *GetOverlayPass() override
            {
                // 借用ポインタ。寿命はモジュール所有(Shutdown まで有効)。
                return &m_OverlayPass;
            }

        private:
            /**
             * @brief カスタムフォントを静的アトラスで構築する(GameThread・Initialize)
             *
             * (a) Inter-Regular を本文フォントとして Default グリフ範囲で読む。
             * (b) MergeMode で FontAwesome6-Solid をアイコン範囲(ICON_MIN_FA..ICON_MAX_16_FA)・
             *     等幅(GlyphMinAdvanceX)・縦位置微調整(GlyphOffset.y)でマージする。
             * (c) MergeMode で NotoSansJP を GetGlyphRangesJapanese(常用~2999字)でマージする。
             * 全フォントを 1 ハンドル(Inter ベース)へ束ね、混在テキストを単一フォントで描ける。
             * ラスタライザは FreeType(IMGUI_ENABLE_FREETYPE)。明示範囲の実 materialize は
             * ForceBakeAllGlyphs() が担い、合わせて実行時アトラス成長を防ぐ(#2/#3 解消)。
             *
             * @return すべてのフォント追加に成功した場合 true
             */
            bool SetupFonts(ImGuiIO &io)
            {
                const float fontSize = kBaseFontSize * m_DpiScale;

                io.Fonts->Clear();

                // フォントパス(NORVES_ASSET_DIR/Fonts/...)。AnsiString は寿命をローカルで
                // 持ち、AddFontFromFileTTF は内部で TTF データを読み込み所有するため安全。
                const Core::Container::AnsiString interPath = MakeFontPath("Inter-Regular.ttf");
                const Core::Container::AnsiString faPath = MakeFontPath("FontAwesome6-Solid.otf");
                const Core::Container::AnsiString jpPath = MakeFontPath("NotoSansJP.ttf");

                // (a) Inter 本文(ベースフォント)。OversampleH=2 でサブピクセル品質。
                ImFontConfig interCfg;
                interCfg.OversampleH = 2;
                interCfg.OversampleV = 1;
                ImFont *baseFont = io.Fonts->AddFontFromFileTTF(
                    interPath.c_str(), fontSize, &interCfg, io.Fonts->GetGlyphRangesDefault());
                if (baseFont == nullptr)
                {
                    NORVES_LOG_ERROR(kLogCategory, "SetupFonts failed: AddFont Inter (%s)", interPath.c_str());
                    return false;
                }

                // (b) FontAwesome マージ。アイコン範囲は静的配列で寿命をモジュールが持つ
                //     (GlyphRanges は LEGACY でフォント生存中ポインタが有効である必要がある)。
                m_IconRanges[0] = static_cast<ImWchar>(ICON_MIN_FA);
                m_IconRanges[1] = static_cast<ImWchar>(ICON_MAX_16_FA);
                m_IconRanges[2] = 0;
                ImFontConfig faCfg;
                faCfg.MergeMode = true;
                faCfg.OversampleH = 2;
                faCfg.OversampleV = 1;
                faCfg.GlyphMinAdvanceX = fontSize; // 等幅化(アイコンを揃える)
                faCfg.GlyphOffset = ImVec2(0.0f, 1.0f * m_DpiScale); // 縦位置微調整(ベースラインへ寄せる)
                ImFont *faFont = io.Fonts->AddFontFromFileTTF(
                    faPath.c_str(), fontSize, &faCfg, m_IconRanges);
                if (faFont == nullptr)
                {
                    NORVES_LOG_ERROR(kLogCategory, "SetupFonts failed: AddFont FontAwesome (%s)", faPath.c_str());
                    return false;
                }

                // (c) NotoSansJP マージ。GetGlyphRangesJapanese(常用~2999字)。範囲ポインタは
                //     imgui が内部静的領域に持つため寿命管理不要(Default/Japanese 共に静的)。
                ImFontConfig jpCfg;
                jpCfg.MergeMode = true;
                jpCfg.OversampleH = 2;
                jpCfg.OversampleV = 1;
                ImFont *jpFont = io.Fonts->AddFontFromFileTTF(
                    jpPath.c_str(), fontSize, &jpCfg, io.Fonts->GetGlyphRangesJapanese());
                if (jpFont == nullptr)
                {
                    NORVES_LOG_ERROR(kLogCategory, "SetupFonts failed: AddFont NotoSansJP (%s)", jpPath.c_str());
                    return false;
                }

                // アトラス構築(ローダ初期化 + フォントメトリクス確定)。imgui 1.92 の
                // RendererHasTextures 下では Build() は明示範囲を事前 bake せず、実描画グリフが
                // lazy bake される。よって全グリフの materialize は別途 ForceBakeAllGlyphs()
                // (InitializeBackend・GPU アップロード前)で行う。
                if (!io.Fonts->Build())
                {
                    NORVES_LOG_ERROR(kLogCategory, "SetupFonts failed: io.Fonts->Build()");
                    return false;
                }

                // 本文ベースフォント(Inter + FA + JP マージ済み)のハンドルを保持する。
                // ForceBakeAllGlyphs が Sources/GlyphRanges を走査して全グリフを bake する。
                m_BaseFont = baseFont;
                m_BakedFontSize = fontSize;

                NORVES_LOG_INFO(kLogCategory,
                                "SetupFonts: Inter + FontAwesome + NotoSansJP merged (FreeType, size=%.1fpx)",
                                fontSize);
                return true;
            }

            /**
             * @brief 設定済み全グリフ範囲を強制 bake する(静的アトラス確定・GameThread)
             *
             * imgui 1.92 の RendererHasTextures 下では io.Fonts->Build() は明示範囲を事前 bake
             * せず、テキスト描画時に必要なグリフのみ lazy bake される。そのままだと実行時に
             * 未描画グリフが要求された瞬間にアトラスが成長し、RenderThread が ImTextureData の
             * Status を書込む経路が復活してしまう(MT 安全化が崩れる)。
             *
             * 本メソッドはマージ済みベースフォントの全 Source(Inter Default / FontAwesome /
             * NotoSansJP Japanese)の GlyphRanges を走査し、各コードポイントに対し公開 API の
             * ImFontBaked::FindGlyph を呼んで glyph をアトラスへ materialize する。これは imgui
             * 内部の ImFontAtlasBuildLegacyPreloadAllGlyphRanges と同じ全範囲プリベイクを、
             * imgui_internal.h を持ち込まずに公開 API のみ(ImFont::Sources / GetFontBaked /
             * ImFontBaked::FindGlyph)で実現する。実グリフを持たないコードポイント(アイコン
             * フォントの空き番)は fallback に解決され追加コストは発生しない。
             *
             * 呼出し後に全 common グリフがアトラスに載るため、続く UploadFontAtlas が一度だけ
             * GPU へ上げて Status=OK を確定し、以後 RenderThread はテクスチャに触れない。
             */
            void ForceBakeAllGlyphs(ImGuiIO &io)
            {
                if (m_BaseFont == nullptr)
                {
                    return;
                }

                ImFontBaked *baked = m_BaseFont->GetFontBaked(m_BakedFontSize);
                if (baked == nullptr)
                {
                    NORVES_LOG_WARNING(kLogCategory, "ForceBakeAllGlyphs: GetFontBaked returned null");
                    return;
                }

                // fallback / ellipsis を先に確定(描画で必ず参照される)。
                if (m_BaseFont->FallbackChar != 0)
                {
                    baked->FindGlyph(m_BaseFont->FallbackChar);
                }
                if (m_BaseFont->EllipsisChar != 0)
                {
                    baked->FindGlyph(m_BaseFont->EllipsisChar);
                }

                // マージ済みフォントの全 Source の GlyphRanges を走査して全コードポイントを bake。
                // GlyphRanges は [min,max] ペアのゼロ終端配列(LEGACY 仕様)。Source が範囲を
                // 持たない場合は Default(ASCII/Latin)へフォールバックする。
                uint32_t bakedCount = 0;
                for (ImFontConfig *src : m_BaseFont->Sources)
                {
                    if (src == nullptr)
                    {
                        continue;
                    }
                    const ImWchar *ranges = src->GlyphRanges ? src->GlyphRanges : io.Fonts->GetGlyphRangesDefault();
                    for (; ranges[0] != 0; ranges += 2)
                    {
                        for (unsigned int c = ranges[0]; c <= ranges[1] && c <= IM_UNICODE_CODEPOINT_MAX; ++c)
                        {
                            baked->FindGlyph(static_cast<ImWchar>(c));
                            ++bakedCount;
                        }
                    }
                }

                // アトラスを再構築してピクセルを確定(materialize した glyph を反映)。
                // TexWidth/Height は GPU テクスチャ上限の懸念把握のためログする。
                io.Fonts->Build();
                const int texW = io.Fonts->TexData != nullptr ? io.Fonts->TexData->Width : 0;
                const int texH = io.Fonts->TexData != nullptr ? io.Fonts->TexData->Height : 0;
                NORVES_LOG_INFO(kLogCategory,
                                "ForceBakeAllGlyphs: prebaked %u glyph codepoints (atlas %dx%d, max=%d)",
                                bakedCount, texW, texH, io.Fonts->TexMaxWidth);
                if (texW > io.Fonts->TexMaxWidth || texH > io.Fonts->TexMaxHeight)
                {
                    NORVES_LOG_WARNING(kLogCategory,
                                       "ForceBakeAllGlyphs: atlas %dx%d exceeds max %dx%d (glyph範囲/サイズ要調整)",
                                       texW, texH, io.Fonts->TexMaxWidth, io.Fonts->TexMaxHeight);
                }
            }

            /**
             * @brief DPI スケールを取得する(取得元が無ければ 1.0)
             *
             * 現状の Screen/Window 抽象は DPI を公開していないため、Windows では
             * GetDpiForSystem(Win8.1+)を直接参照する手もあるが、本ユニットでは依存を増やさず
             * 1.0 を既定とする(コメント)。将来 Screen 側が content scale を公開したら反映する。
             *
             * @return DPI スケール(1.0 = 96dpi 等倍)
             */
            float GetDpiScale() const
            {
                // レンダリング解像度に応じた UI スケール。基準 1080p を等倍とし、画面高さに
                // 比例させる(720p≒0.67=小さく / 1440p≒1.33 / 4K≒2.0=大きく)。極端値を避ける
                // ため [0.5, 2.5] にクランプ。解像度が取れない場合は等倍。
                // 注: 初期化時の解像度で確定し、実行時のウィンドウリサイズには追従しない。
                if (Core::Engine::GEngine == nullptr)
                {
                    return 1.0f;
                }
                uint32_t width = 0;
                uint32_t height = 0;
                Core::Engine::GEngine->GetRenderWorld().GetResolution(width, height);
                if (height == 0)
                {
                    return 1.0f;
                }
                constexpr float kReferenceHeight = 1080.0f;
                float scale = static_cast<float>(height) / kReferenceHeight;
                if (scale < 0.5f)
                {
                    scale = 0.5f;
                }
                if (scale > 2.5f)
                {
                    scale = 2.5f;
                }
                return scale;
            }

            /**
             * @brief 文字入力イベントハンドラ(OnCharEvent 購読・GameThread)
             *
             * IME 確定後の Unicode コードポイントをバッファへ積む。Tick(同 GameThread)で
             * io.AddInputCharacter へ流して消費する。WindowProc と Tick は同一 GameThread で
             * 直列実行されるためロック不要。
             */
            void OnCharInput(const Core::Input::CharEvent &event)
            {
                if (event.Codepoint != 0)
                {
                    m_PendingChars.push_back(event.Codepoint);
                }
            }

            /**
             * @brief display size とマウス/キーボード/文字入力を ImGui IO へ反映する(GameThread)
             *
             * 2B-i のマウス橋渡しに加え、キー状態のポーリング → io.AddKeyEvent、修飾キーの
             * AddKeyEvent(Mods)、バッファ済み文字 → io.AddInputCharacter を行う。ライブ入力は
             * InputState のポーリングから読む(スナップショット主義に整合)。
             */
            void UpdateDisplayAndInput(ImGuiIO &io)
            {
                uint32_t width = 1280;
                uint32_t height = 720;
                if (Core::Engine::GEngine != nullptr)
                {
                    Core::Engine::GEngine->GetRenderWorld().GetResolution(width, height);
                    if (width == 0 || height == 0)
                    {
                        width = 1280;
                        height = 720;
                    }

                    const Core::Input::InputState &input =
                        Core::Engine::GEngine->GetInputSystem().GetState();

                    // --- マウス ---
                    const Core::Input::MouseState &mouse = input.GetMouseState();
                    io.AddMousePosEvent(mouse.PositionX, mouse.PositionY);
                    io.AddMouseButtonEvent(0, input.IsMouseButtonDown(Core::Input::MouseButton::Left));
                    io.AddMouseButtonEvent(1, input.IsMouseButtonDown(Core::Input::MouseButton::Right));
                    io.AddMouseButtonEvent(2, input.IsMouseButtonDown(Core::Input::MouseButton::Middle));
                    if (mouse.ScrollDelta != 0.0f)
                    {
                        io.AddMouseWheelEvent(0.0f, mouse.ScrollDelta);
                    }

                    // --- キーボード(状態ポーリング → AddKeyEvent) ---
                    // 各キーの現在 down/up を ImGui へ。AddKeyEvent は内部で前回値と差分を取り、
                    // 変化時のみ記録するため毎フレーム全キーを投げても冪等。
                    const int keyCount = static_cast<int>(Core::Input::KeyCode::Count);
                    for (int i = 1; i < keyCount; ++i) // 0 は None
                    {
                        Core::Input::KeyCode code = static_cast<Core::Input::KeyCode>(i);
                        ImGuiKey imguiKey = ToImGuiKey(code);
                        if (imguiKey != ImGuiKey_None)
                        {
                            io.AddKeyEvent(imguiKey, input.IsKeyDown(code));
                        }
                    }

                    // 修飾キーの集約状態(Ctrl/Shift/Alt)。ショートカット解決に必要。
                    io.AddKeyEvent(ImGuiMod_Ctrl, input.IsCtrlDown());
                    io.AddKeyEvent(ImGuiMod_Shift, input.IsShiftDown());
                    io.AddKeyEvent(ImGuiMod_Alt, input.IsAltDown());

                    // --- 文字入力(バッファ済み Unicode を流す) ---
                    for (uint32_t cp : m_PendingChars)
                    {
                        io.AddInputCharacter(cp);
                    }
                    m_PendingChars.clear();
                }

                io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            }

            // ImGui コンテキスト(GameThread 所有)。Initialize で生成・Shutdown で破棄。
            ::ImGuiContext *m_Context = nullptr;

            // overlay パス(モジュール所有・寿命一致)。GetOverlayPass で借用返し。
            ImGuiOverlayPass m_OverlayPass;

            // DPI スケール(Initialize で確定)。フォントサイズ・スタイルへ反映。
            float m_DpiScale = 1.0f;

            // FontAwesome アイコン範囲(GlyphRanges はフォント生存中ポインタ有効が必須のため
            // モジュールが寿命を持つ静的配列で保持する)。
            ImWchar m_IconRanges[3] = {0, 0, 0};

            // マージ済みベースフォント(Inter + FA + JP)とその bake サイズ。ForceBakeAllGlyphs が
            // Sources/GlyphRanges を走査して全グリフを materialize するのに使う(SetupFonts で確定)。
            ImFont *m_BaseFont = nullptr;
            float m_BakedFontSize = kBaseFontSize;

            // 文字入力バッファ(OnCharInput で積み Tick で消費・GameThread 直列でロック不要)。
            Core::Container::VariableArray<uint32_t> m_PendingChars;
            bool m_bCharSubscribed = false;
        };
    } // namespace
} // namespace NorvesLib::Modules::Gui

namespace NorvesLib::Core::Module
{
    IModule *RegisterImGuiModule(ModuleRegistry &registry)
    {
        return registry.Register(Container::MakeUnique<Modules::Gui::ImGuiModule>());
    }
} // namespace NorvesLib::Core::Module
