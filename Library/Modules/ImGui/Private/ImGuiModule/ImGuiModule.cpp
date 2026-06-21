#include "ImGuiModule/ImGuiModule.h"
#include "ImGuiModule/ImGuiOverlayPass.h"

#include "Module/IModule.h"
#include "Module/IRenderModule.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "RHI/IDevice.h"
#include "RHI/IRenderPass.h"
#include "Input/InputSystem.h"
#include "Input/InputState.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

#include "imgui.h"

// ImGuiModule — ImGui デバッグオーバーレイのファーストモジュール(第2段 B-i)。
//
// IModule(寿命) + IRenderModule(描画参加)を実装する。ImGui コンテキストを GameThread で
// 生成・所有し、毎フレーム Tick で NewFrame→UI 構築(既定スタイルのデモ)→Render→
// ImDrawData ディープクローンを行う。RenderThread はクローンのみを読み、ライブ ImGui
// コンテキストには一切触れない。描画(RHI リソース生成 + 録画)は ImGuiOverlayPass +
// 抽象 IImGuiRenderer(Core の Vulkan 実装)に閉じ、本モジュールは生 Vulkan を見ない。
namespace NorvesLib::Modules::Gui
{
    namespace
    {
        constexpr const char *kLogCategory = "ImGui";
        constexpr const char *kModuleName = "NorvesImGuiModule";

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
                // バックエンドが自動実行する(RecordDrawData 内)。
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
                // .ini 永続化は無効化(ファイル I/O を持ち込まない)。
                io.IniFilename = nullptr;
                io.LogFilename = nullptr;

                // 既定スタイル(StyleColorsDark)。カスタムテーマ/フォント/FreeType は 2B-ii。
                ::ImGui::StyleColorsDark();

                // per-slot スナップショットは overlay pass が FramePacket スロットごとに所有する
                // (旧: モジュールが単一スナップショットを保持して借用設定していた経路は廃止)。

                // ---- MT 安全化: バックエンド初期化 + フォントアトラス GPU アップロード(GameThread) ----
                if (!InitializeBackend())
                {
                    // バックエンド初期化失敗。コンテキストを破棄してロールバックする
                    // (InstallAll が本モジュール Initialize の false を検知し逆順ロールバック)。
                    NORVES_LOG_ERROR(kLogCategory, "ImGuiModule Initialize failed: InitializeBackend");
                    ::ImGui::SetCurrentContext(m_Context);
                    ::ImGui::DestroyContext(m_Context);
                    m_Context = nullptr;
                    return false;
                }

                NORVES_LOG_INFO(kLogCategory, "ImGuiModule Initialize: context + backend ready (fonts uploaded)");
                return true;
            }

            /**
             * @brief imgui バックエンドを GameThread で初期化しフォントアトラスをアップロードする
             *
             * GEngine の RenderingCoordinator から device と overlay load render pass を取得し、
             * overlay pass の InitializeGameThread を駆動する。事前に「捨てフレーム」
             * (NewFrame→Render)を 1 回回してフォントアトラスのテクスチャ(既定フォントの
             * 既定グリフ範囲)を materialize し、静的アトラスとして GPU へ一度だけ載せる。
             * 以後この既定範囲のグリフのみを使う限りアトラスは成長せず、RenderThread が
             * テクスチャ(ImTextureData::Status)に触れることはない。
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

                // 捨てフレームでフォントアトラスのテクスチャを materialize する。imgui 1.92 の
                // 動的アトラスは NewFrame で既定フォントの既定グリフ範囲を baking し、TexList へ
                // WantCreate のテクスチャを積む。display size を妥当値にしておく(NewFrame の
                // サニティチェックが 0 を嫌うため)。ここでは UI は描かず(成果物は本 Tick)、
                // アトラスを確定させることだけが目的。
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

                    ::ImGui::NewFrame();
                    // 本 Tick が描く UI(既定デモウィンドウ)と同じグリフ集合を捨てフレームで
                    // 要求して baking させ、静的アトラスを確定する。これにより本番フレームで
                    // 新規グリフ(=アトラス成長=テクスチャ更新要求)が発生せず、RenderThread が
                    // ImTextureData に触れる必要が生じない。2B-ii で全グリフ事前構築に発展する。
                    ::ImGui::ShowDemoWindow(nullptr);
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

                // 既定スタイルのデモウィンドウ(本ユニットの可視成果物)。
                ::ImGui::ShowDemoWindow(nullptr);

                ::ImGui::Render();

                // ImGui::GetDrawData() は context 所有・翌 NewFrame で再利用されるため RT は
                // 直接読めない。ここでは本フレームのライブ ImDrawData を overlay pass へ借用
                // 設定するに留める(GameThread 内・次 NewFrame まで有効)。実際のディープクローンは
                // pass の OnAssignedToPacket が「書き込み中パケットのスロット index」へ行う
                // (FramePacket スロット寿命連動で per-slot に束ね、RT 読取と排他=MT 安全)。
                m_OverlayPass.SetPendingDrawData(::ImGui::GetDrawData());
            }

            void Shutdown() override
            {
                // RenderThread 静止後・device 生存中に駆動される前提(寿命順序は Registry +
                // RenderWorld 静止バリアが保証)。overlay pass の RHI リソースを先に解放し、
                // その後 ImGui コンテキストを破棄する。
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
             * @brief display size と最小マウス入力を ImGui IO へ反映する(GameThread)
             *
             * 2B-i は最小入力(マウス位置/左右中ボタン/ホイール)に留める。詳細な
             * キーボード/IME/カーソル形状連動は 2B-ii。ライブ入力は InputState の
             * ポーリングから読み、ImGui IO へ書く(スナップショット主義に整合)。
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
                    const Core::Input::MouseState &mouse = input.GetMouseState();
                    io.AddMousePosEvent(mouse.PositionX, mouse.PositionY);
                    io.AddMouseButtonEvent(0, input.IsMouseButtonDown(Core::Input::MouseButton::Left));
                    io.AddMouseButtonEvent(1, input.IsMouseButtonDown(Core::Input::MouseButton::Right));
                    io.AddMouseButtonEvent(2, input.IsMouseButtonDown(Core::Input::MouseButton::Middle));
                    if (mouse.ScrollDelta != 0.0f)
                    {
                        io.AddMouseWheelEvent(0.0f, mouse.ScrollDelta);
                    }
                }

                io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            }

            // ImGui コンテキスト(GameThread 所有)。Initialize で生成・Shutdown で破棄。
            ::ImGuiContext *m_Context = nullptr;

            // overlay パス(モジュール所有・寿命一致)。GetOverlayPass で借用返し。
            // ImDrawData の per-slot ディープクローンは overlay pass が FramePacket スロット
            // ごとに所有する(MT 安全の所有者は pass 側に一元化)。
            ImGuiOverlayPass m_OverlayPass;
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
