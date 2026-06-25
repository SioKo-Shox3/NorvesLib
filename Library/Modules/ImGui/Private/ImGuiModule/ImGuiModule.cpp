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
#include "Input/InputTypes.h"
#include "Input/IInputController.h"
#include "Input/InputRouter.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

// ImGuiModule — ImGui デバッグオーバーレイのファーストモジュール(第2段 B)。
//
// IModule(寿命) + IRenderModule(描画参加)を実装する。ImGui コンテキストを GameThread で
// 生成・所有し、毎フレーム Tick で NewFrame→UI 構築→Render→ImDrawData ディープクローンを
// 行う。RenderThread はクローンのみを読み、ライブ ImGui コンテキストには一切触れない。
// 描画(RHI リソース生成 + ImDrawData の自前描画)は ImGuiOverlayPass が Core の抽象 RHI::I* と
// 汎用 Mesh2D 描画経路(DrawCommand::CreateMesh2D + SceneRenderer + DynamicBufferRing)のみで
// 行う。Core は ImGui を一切参照/リンクせず、imgui core(imgui.h/ImDrawData)を見るのは本
// モジュールだけ。imgui_impl_vulkan も生 Vulkan も使わない。
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
// さらに初期化時に設定済みの全グリフ範囲を強制的に bake(materialize)してから
// レガシー単一アトラス(GetTexDataAsRGBA32)を一度だけ確定する。これにより実行時にアトラスが
// 成長せず(=テクスチャ再アップロード要求が出ず)、RenderThread が ImTextureData(テクスチャ
// Status)に触れる必要が恒久的に消える(#2/#3/2B-ii-a の「未描画グリフ不可視」制約の解消)。
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
         * @brief ImGui デバッグオーバーレイモジュール(IModule + IRenderModule + IInputController)
         *
         * GetOverlayPass() で所有する ImGuiOverlayPass を借用返しする。overlay seam が
         * そのパスの Initialize(遅延)/Setup/Execute を駆動する。Shutdown は seam では
         * 駆動されない(IRenderModule 契約=モジュール責務)ため本モジュールの Shutdown で
         * パスの Shutdown を呼ぶ(RenderThread 静止後・device 生存中の前提)。
         *
         * C3(入力ルーティング): IInputController を実装し InputRouter へ PriorityOverlay
         * (=カメラの PriorityGame より上位)で登録する。各 On* は受け取ったイベントを
         * ライブ ImGui コンテキストの io へ供給し、WantCaptureMouse/WantCaptureKeyboard を
         * consume 可否として返す。true を返すと Router は下位(カメラ等)へ配送しないため、
         * 「ImGui 窓上ではカメラが動かない」排他が成立する。入力供給はイベント駆動になり、
         * 旧来の InputState ポーリング(UpdateDisplayAndInput 内)と OnCharEvent 購読は廃止した。
         */
        class ImGuiModule final : public Core::Module::IModule,
                                  public Core::Module::IRenderModule,
                                  public Core::Input::IInputController
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
            bool Install(Core::Engine::Engine &engine) override
            {
                // C3: 入力ルーターへ最優先(PriorityOverlay)で登録する。ImGui が consume した
                // イベントは下位(カメラ=PriorityGame)へ配送されないため、ImGui 窓上の操作は
                // カメラへ届かない。本モジュールが借用ポインタとして渡り、Uninstall で解除する。
                engine.GetInputRouter().RegisterController(this, Core::Input::InputRouter::PriorityOverlay);
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
                // 脱 Core: 自前 mesh2d 描画はレガシー単一アトラス(io.Fonts->GetTexDataAsRGBA32 +
                // SetTexID)に固定する。imgui 1.92 の動的テクスチャ(RendererHasTextures)は
                // 使わない(RT が ImTextureData::Status を触る経路を構造的に排除し続けるため)。
                // VtxOffset は自前経路が cmd.VtxOffset を正しく扱う(VertexOffset へ加算)ため有効化する。
                io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
                // .ini 永続化は無効化(ファイル I/O を持ち込まない)。
                io.IniFilename = nullptr;
                io.LogFilename = nullptr;
                // 診断用バックエンド名(imgui_impl_* は使わず自前 Mesh2D 描画のため明示)。
                io.BackendRendererName = "NorvesMesh2D";
                io.BackendPlatformName = "NorvesLib";

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

                // C3: 文字入力は OnCharEvent 購読ではなく IInputController::OnChar 経由で
                // 受け取り io.AddInputCharacter へ直接流す(Router が PriorityOverlay で配送)。
                // 旧来の OnCharEvent 購読 + バッファ(m_PendingChars) + Tick 流し込みは廃止した。

                // ---- MT 安全化: バックエンド初期化 + フォントアトラス GPU アップロード(GameThread) ----
                if (!InitializeBackend())
                {
                    // バックエンド初期化失敗。コンテキスト破棄でロールバックする。
                    NORVES_LOG_ERROR(kLogCategory, "ImGuiModule Initialize failed: InitializeBackend");
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
             * GEngine の RenderingCoordinator から device を取得し、overlay pass の
             * InitializeGameThread(device) を駆動してフォントアトラスを GPU へ載せる。脱 Core 後は
             * 自前 mesh2d 描画がレガシー単一アトラス(io.Fonts->GetTexDataAsRGBA32 + SetTexID)に
             * 固定のため、フォントテクスチャは GetTexDataAsRGBA32 が全範囲を一括 bake した CPU
             * ピクセルから一度だけアップロードされ、以後 RenderThread はテクスチャに触れない。
             * 事前に ForceBakeAllGlyphs() で設定済み全グリフ範囲(ASCII / 日本語常用 / FontAwesome
             * アイコン)を確実に materialize し、続く空の捨てフレーム(NewFrame→Render のみ)で
             * imgui の内部状態を確定させてからアトラスを確定する。mesh2d パイプライン/
             * DescriptorSet/バッファの生成は overlay seam(RenderThread・ShaderManager 利用可)で
             * 行うため、ここでは渡さない。
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
                if (Core::Container::IsNull(device))
                {
                    NORVES_LOG_WARNING(kLogCategory, "InitializeBackend skipped: device unavailable");
                    return false;
                }

                // 設定済み全グリフ範囲を強制 bake してから空の捨てフレームを回す。レガシー単一
                // アトラスでも明示範囲(ASCII / 日本語常用 / FA アイコン)を確実に materialize して
                // から GetTexDataAsRGBA32 で確定させ、本番フレームで新規グリフが要求されない
                // (=アトラス成長=テクスチャ再アップロード)状態を作る。捨てフレームは NewFrame→
                // Render のみ(固定窓の描画は不要=グリフ確定は ForceBakeAllGlyphs が担う)。
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

                // device を渡して overlay pass の GameThread 初期化(フォント ITexture +
                // サンプラー生成・アップロード)を駆動する。mesh2d パイプライン/DescriptorSet/
                // バッファは overlay seam の IViewPass::Initialize(RenderThread)が生成する。
                if (!m_OverlayPass.InitializeGameThread(device.get()))
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

                // 表示サイズのみ更新する。入力(マウス/キー/文字)は IInputController 経由で
                // イベント駆動供給されるため、ここではポーリングしない(C3)。
                UpdateDisplay(io);

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
                // C3: 入力はイベント駆動(IInputController)になり OnCharEvent 購読は持たないため
                // ここでの購読解除はない(ルーター登録解除は Uninstall が担う)。

                m_OverlayPass.Shutdown();

                if (m_Context != nullptr)
                {
                    ::ImGui::SetCurrentContext(m_Context);
                    ::ImGui::DestroyContext(m_Context);
                    m_Context = nullptr;
                }
                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Shutdown");
            }

            void Uninstall(Core::Engine::Engine &engine) override
            {
                // C3: ルーターから登録解除(冪等)。借用ポインタを破棄前に必ず外す。
                engine.GetInputRouter().UnregisterController(this);
                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Uninstall");
            }

            // --- IRenderModule 描画参加 ---
            Core::Rendering::IViewPass *GetOverlayPass() override
            {
                // 借用ポインタ。寿命はモジュール所有(Shutdown まで有効)。
                return &m_OverlayPass;
            }

            // --- IInputController イベント供給 + consume(C3) ---
            // 各 On* は受け取ったイベントをライブ ImGui コンテキストの io へ供給し、ImGui が
            // その種別の入力をキャプチャしているか(WantCaptureMouse/WantCaptureKeyboard)を
            // consume 可否として返す。Router は GameThread 専用・Tick と同一スレッドのため
            // context current 化はここで明示する(配送が Tick より前=context が他へ向いている
            // 可能性に備える。m_Context==nullptr なら供給先が無いので consume せず伝播)。
            //
            // 1 フレーム遅延について: WantCapture* は前フレームの NewFrame 結果に基づくため、
            // 本フレームに供給したイベントの可否判定は 1 フレーム古い。ドラッグ所有(掴んだ窓を
            // 引きずる間のマウス)は WantCaptureMouse に内包され、掴んだ瞬間から離すまで true が
            // 維持されるため、排他としては実用上問題ない(設計確認済み)。

            bool OnMouseButton(const Core::Input::MouseButtonEvent &event) override
            {
                if (m_Context == nullptr)
                {
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);
                ImGuiIO &io = ::ImGui::GetIO();

                int idx = -1;
                switch (event.Button)
                {
                case Core::Input::MouseButton::Left:
                    idx = 0;
                    break;
                case Core::Input::MouseButton::Right:
                    idx = 1;
                    break;
                case Core::Input::MouseButton::Middle:
                    idx = 2;
                    break;
                default:
                    // X1/X2 等は ImGui のマウスボタン 0..2 に対応しないため供給せず伝播。
                    return false;
                }
                io.AddMouseButtonEvent(idx, event.Action == Core::Input::InputAction::Pressed);
                return io.WantCaptureMouse;
            }

            bool OnMouseMove(const Core::Input::MouseMoveEvent &event) override
            {
                if (m_Context == nullptr)
                {
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);
                ImGuiIO &io = ::ImGui::GetIO();
                io.AddMousePosEvent(event.PositionX, event.PositionY);
                return io.WantCaptureMouse;
            }

            bool OnMouseScroll(const Core::Input::MouseScrollEvent &event) override
            {
                if (m_Context == nullptr)
                {
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);
                ImGuiIO &io = ::ImGui::GetIO();
                io.AddMouseWheelEvent(0.0f, event.Delta);
                return io.WantCaptureMouse;
            }

            bool OnKey(const Core::Input::KeyEvent &event) override
            {
                if (m_Context == nullptr)
                {
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);
                ImGuiIO &io = ::ImGui::GetIO();
                ImGuiKey imguiKey = ToImGuiKey(event.Code);
                if (imguiKey != ImGuiKey_None)
                {
                    // 押下は Pressed/Repeat を down、Released を up とする。修飾キー(Ctrl/Shift/Alt)
                    // の集約は個別の L/R 修飾キーイベントから ImGui が内部で行うため明示投入は不要。
                    const bool bDown = event.Action == Core::Input::InputAction::Pressed ||
                                       event.Action == Core::Input::InputAction::Repeat;
                    io.AddKeyEvent(imguiKey, bDown);
                }
                return io.WantCaptureKeyboard;
            }

            bool OnChar(const Core::Input::CharEvent &event) override
            {
                if (m_Context == nullptr)
                {
                    return false;
                }
                ::ImGui::SetCurrentContext(m_Context);
                ImGuiIO &io = ::ImGui::GetIO();
                if (event.Codepoint != 0)
                {
                    io.AddInputCharacter(event.Codepoint);
                }
                return io.WantCaptureKeyboard;
            }

            const char *DebugName() const override
            {
                return "ImGuiModule";
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
             * 呼出し後に全 common グリフがアトラスに載るため、続く GetTexDataAsRGBA32(overlay pass の
             * InitializeGameThread)が一度だけ CPU ピクセルを取得し ITexture へ GPU アップロードして
             * 確定する。以後 RenderThread はテクスチャに触れない(レガシー単一アトラス固定)。
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
             * @brief display size を ImGui IO へ反映する(GameThread・Tick)
             *
             * C3 でマウス/キーボード/文字入力のポーリング供給は廃止し、入力は
             * IInputController(On*)経由のイベント駆動供給に一本化した。本メソッドは
             * レンダリング解像度から io.DisplaySize / DisplayFramebufferScale を更新する
             * 表示設定のみを担う(NewFrame に必要)。
             */
            void UpdateDisplay(ImGuiIO &io)
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
