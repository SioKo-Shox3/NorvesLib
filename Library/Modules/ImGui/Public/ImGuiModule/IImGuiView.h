#pragma once

// IImGuiView — ImGui オーバーレイへ 1 つの UI ビューを差し込む公開インターフェース
// (Piece 2: view 登録機構)。
//
// ImGui モジュールは「UI の仕組み」(コンテキスト所有・NewFrame/Render・フォント・
// スナップショット)だけを所有し、固有の窓内容は持たない。外部(Game の SubRoutine 等)が
// 本インターフェースを実装した view を RegisterImGuiView() で登録すると、ImGuiModule::Tick が
// NewFrame と Render の間で全登録 view の OnImGui() を順に呼ぶ。
//
// 設計上の制約: 本ヘッダは imgui.h を include しない純インターフェースに保つ。OnImGui() の
// 中身(ImGui:: 呼び出し)は登録側 Game の .cpp が imgui.h を引いて実装する。これにより
// Game は imgui ヘッダへ広く依存せず、必要な TU だけが imgui を見る。
//
// 寿命/スレッド: view は借用(モジュールは所有しない)。登録側が view より長く生存させ、
// 破棄前に UnregisterImGuiView() で外す責任を持つ。登録/解除/描画はすべて GameThread
// (ImGuiModule::Tick と同一スレッド)前提のためロックは不要。

namespace NorvesLib::Modules::Gui
{
    /**
     * @brief ImGui オーバーレイへ差し込む 1 ビューのインターフェース
     *
     * OnImGui() は毎フレーム(NewFrame と Render の間)に GameThread で呼ばれる。実装は
     * ImGui:: の Begin/End・ウィジェット呼び出しでこのフレームの UI を構築する。本ヘッダは
     * imgui 非依存に保つため、実装側 .cpp が imgui.h を include する。
     */
    class IImGuiView
    {
    public:
        virtual ~IImGuiView() = default;

        /**
         * @brief このフレームの ImGui UI を構築する(GameThread・NewFrame と Render の間)
         *
         * ImGui:: 呼び出しで窓/ウィジェットを描く。複数 view が登録されている場合は登録順に
         * 呼ばれる(同一フレームの同一描画コンテキストを共有)。
         */
        virtual void OnImGui() = 0;

        /**
         * @brief ビュー名(デバッグ/ログ用)。既定は "ImGuiView"。
         */
        virtual const char *GetViewName() const
        {
            return "ImGuiView";
        }
    };

    /**
     * @brief ImGui view を登録する(GameThread・借用)
     *
     * 以後 ImGuiModule::Tick が毎フレーム view->OnImGui() を呼ぶ。view の所有権は移らない
     * (登録側が寿命を持つ)。同一ポインタの二重登録は弾く(no-op)。view が nullptr の場合も
     * no-op。GameThread 前提のためロック不要。
     */
    void RegisterImGuiView(IImGuiView *view);

    /**
     * @brief ImGui view の登録を解除する(GameThread・借用)
     *
     * 登録テーブルから view を除去する。未登録(または nullptr)なら no-op。view を破棄する
     * 前に必ず呼び、以後 OnImGui() が呼ばれないようにする。GameThread 前提のためロック不要。
     */
    void UnregisterImGuiView(IImGuiView *view);
} // namespace NorvesLib::Modules::Gui
