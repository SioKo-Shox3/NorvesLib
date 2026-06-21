#pragma once

#include <cstdint>

namespace NorvesLib::RHI
{

    // 前方宣言（imgui / Vulkan 型は一切露出しない・RHI 抽象型のみ）
    class IRenderPass;
    class ICommandList;

    /**
     * @brief ImGui 描画バックエンドの抽象インターフェース
     *
     * ImGui のフォントアトラス生成・ドローデータ記録といった RHI 依存処理を
     * 抽象化する。実装は各バックエンド固有（現状 Vulkan）で Core の
     * Private/RHI/<Backend>/ に閉じ込め、生 imgui_impl_* や Vulkan 型を
     * このヘッダから漏らさない。Rendering 層・モジュール lib はこの抽象のみを見る。
     *
     * 所有権: 本インターフェースの実装は IDevice が所有する（IDevice の
     * ファクトリ IDevice::CreateImGuiRenderer() で生成し、IDevice 破棄で
     * 連れ破棄される）。呼び出し側は借用ポインタとして扱い delete しない。
     *
     * スレッド: 生成・各メソッド呼び出しは RenderThread（描画スレッド）から
     * 行う前提。GameThread のライブ状態は読まず、スナップショット越しに
     * 受け取ったドローデータのみを記録する。
     */
    class IImGuiRenderer
    {
    public:
        virtual ~IImGuiRenderer() = default;

        /**
         * @brief バックエンドを初期化する
         *
         * フォントアトラス・パイプライン等の RHI リソースを構築する。
         * 描画先となる presentation load render pass を渡す（overlay は
         * 最終 blit 後の back buffer へ load-blend するため）。
         *
         * @param loadRenderPass overlay 描画に用いる load render pass（借用）
         * @param minImageCount スワップチェーン最小イメージ数
         * @param imageCount スワップチェーンイメージ数
         * @return 初期化に成功した場合 true。失敗時 false（呼び出し側は描画を素通りさせる）
         */
        virtual bool Initialize(IRenderPass *loadRenderPass, uint32_t minImageCount, uint32_t imageCount) = 0;

        /**
         * @brief バックエンドを破棄する
         *
         * 生成済みの RHI リソースを解放する。RenderThread 静止後・device
         * 生存中に呼ばれる前提（寿命は IDevice に内包される）。
         */
        virtual void Shutdown() = 0;

        /**
         * @brief フォントアトラスを構築（GPU へアップロード）する
         *
         * フォント設定変更後に呼ぶ。RHI リソース生成はこの実装側に閉じ、
         * overlay 側は録画のみ行う。
         *
         * @return 構築に成功した場合 true
         */
        virtual bool BuildFontAtlas() = 0;

        /**
         * @brief ImGui ドローデータを記録中の CommandList へ発行する
         *
         * 呼び出し時点で BeginRenderPass 済みの録画窓内であること（自身では
         * Begin/End RenderPass も CommandList の Begin/End も行わない）。
         * imguiDrawData は ImDrawData* を imgui 非依存にするため void* で受ける。
         *
         * @param commandList 記録先の CommandList（借用・録画窓内）
         * @param imguiDrawData ImDrawData* を型消去した不透明ポインタ（null 可・null 時は no-op）
         */
        virtual void RecordDrawData(ICommandList *commandList, const void *imguiDrawData) = 0;

        /**
         * @brief スワップチェーン再生成をバックエンドへ通知する
         *
         * Resize 等でスワップチェーンが作り直された際にイメージ数を更新する。
         *
         * @param minImageCount 新しい最小イメージ数
         * @param imageCount 新しいイメージ数
         */
        virtual void NotifySwapChainRecreated(uint32_t minImageCount, uint32_t imageCount) = 0;
    };

} // namespace NorvesLib::RHI
