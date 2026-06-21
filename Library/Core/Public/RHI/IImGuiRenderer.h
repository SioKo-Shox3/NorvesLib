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
     * スレッド: Initialize / BuildFontAtlas / UploadFontAtlas は GameThread の
     * 初期化フェーズ（フレーム未投入＝RenderThread アイドル）から呼ぶ前提。
     * RecordDrawData / NotifySwapChainRecreated は RenderThread（描画スレッド）から
     * 呼ぶ。RecordDrawData は GameThread のライブ状態を読まず、スナップショット
     * 越しに受け取ったドローデータのみを記録し、テクスチャは一切更新しない
     * （アップロードは GameThread の UploadFontAtlas で完了済み）。
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
         * @brief フォントアトラスの CPU ピクセルを構築する
         *
         * フォント設定変更後に呼ぶ。本メソッドはアトラスの CPU ピクセル生成に
         * 留まる（冪等）。GPU アップロードは UploadFontAtlas() が明示的に行う。
         *
         * @return 構築に成功した場合 true
         */
        virtual bool BuildFontAtlas() = 0;

        /**
         * @brief フォントアトラス（および現存する全テクスチャ）を GPU へ即時アップロードする
         *
         * MT 安全化の要。imgui 1.92 の動的テクスチャ機構は本来 RecordDrawData
         * （RenderThread）内で遅延アップロードするが、その経路は ImTextureData の
         * Status/TexID を GameThread（NewFrame）と RenderThread（描画）が無ロックで
         * 共有更新する read-modify-write 競合を生む。本メソッドは GameThread の
         * 初期化時（フレーム未投入＝グラフィックスキューがアイドルで RenderThread と
         * 競合し得ない時点）に全テクスチャを同期アップロードし Status=OK に確定する。
         * 以後 RecordDrawData はテクスチャを一切更新せず（Status=OK スキップ＋
         * Textures ポインタ無効化）、Status の跨スレッド書込みが構造的に消える。
         *
         * 前提: Initialize 済み（内部テクスチャ用コマンドプールが生成済み）であること。
         * 自身でキューへ submit し WaitIdle するため、呼び出し側スレッドが当該キューを
         * 専有している（他スレッドが同時 submit しない）時点で呼ぶこと。
         *
         * @return 全テクスチャのアップロードに成功した場合 true
         */
        virtual bool UploadFontAtlas() = 0;

        /**
         * @brief ImGui ドローデータを記録中の CommandList へ発行する
         *
         * 呼び出し時点で BeginRenderPass 済みの録画窓内であること（自身では
         * Begin/End RenderPass も CommandList の Begin/End も行わない）。
         * imguiDrawData は ImDrawData* を imgui 非依存にするため void* で受ける。
         * テクスチャは一切アップロード/更新しない（UploadFontAtlas で GameThread が
         * 事前確定済み・Status=OK）。RenderThread からのみ呼ぶ。
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
