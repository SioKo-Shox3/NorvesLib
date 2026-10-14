#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::Scenes
{
    class SceneViewSwitch;
} // namespace Game::Scenes

namespace Game::Debug
{
    /**
     * @brief 1 回の起動で、シーンの複数の名前付き視点を順に撮って PNG に保存する（--capture-views）
     *
     * 視点ごとに Game を起動し直すと、重いシーン（荒野の地形の区画など）の読み込みを毎回やり直すので、
     * 組み立てを 1 回で済ませ、カメラだけを置き直して撮る。
     *
     * 1 つ目の視点は起動時の視点で、ApplicationProcessor の --capture-png と同じ数え方で撮る: 読み込み中
     * （非同期の資産・骨格の資産・SceneReady の前）を一度見た後、読み込み中でない最初の描画を基準にして
     * 決定的な撮影のエポック（時間・TAA・RTGI・自動露出の数え直し）を始め、そこから settledRenderedFrames
     * 描いたフレームを撮る。2 つ目からは、GameMode に視点の切り替えを頼み（Scenes/SceneViewSwitch.h）、
     * 置き直してから viewSettleRenderedFrames（既定 kDefaultViewSettleRenderedFrames）の間はストリーミング（仮想テクスチャ・ジオメトリのページ）が
     * 追いつくのを待ち、そこから 1 つ目と同じ手順でエポックを始め直して撮る。
     *
     * 撮り終えるまでは、ApplicationProcessor の --capture-png の撮影と終了を止めておく（IsHoldingEngineCapture を
     * GameApplicationHandler::HasPendingAssetConsumers が返す）。全部を撮ったら終了（0）を、失敗したら終了（1）を頼む。
     * 保存先は <outputDirectory>/<視点の名前>.png。
     */
    class MultiViewCapture
    {
    public:
        /// 視点を置き直してから、エポックを始める基準を探し始めるまでに待つ描画フレーム数の既定
        static constexpr uint64_t kDefaultViewSettleRenderedFrames = 120;

        /**
         * @brief 撮る視点と保存先を設定する
         * @param viewNames 撮る順の視点の名前（先頭は起動時の視点。"default" は先頭の既定の視点の別名）
         * @param outputDirectory PNG の保存先のフォルダ
         * @param settledRenderedFrames エポックの基準から撮るまでの描画フレーム数（--capture-png と同じ値）
         * @param viewSettleRenderedFrames 視点を置き直してから基準を探し始めるまでに待つ描画フレーム数
         * @param pViewSwitch 視点の切り替えを頼む窓口（非所有）。2 つ以上の視点を撮るときは必須
         */
        void Configure(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& viewNames,
                       const NorvesLib::Core::Container::String& outputDirectory, uint64_t settledRenderedFrames,
                       uint64_t viewSettleRenderedFrames, Game::Scenes::SceneViewSwitch* pViewSwitch);

        bool IsEnabled() const { return !m_ViewNames.empty(); }

        /// ApplicationProcessor の --capture-png の撮影と終了を止めておくか（有効な間は撮り終えた後も止める）
        bool IsHoldingEngineCapture() const { return IsEnabled(); }

        /**
         * @brief 描画の前に呼ぶ（読み込み中を見たら基準を外す）
         * @param bOtherPending ほかに読み込みの反映を待っているものがあるか（GameApplicationHandler の待ち）
         */
        void OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending);

        /** @brief 描画の後に呼ぶ（基準とエポック・撮影の要求・保存・視点の切り替え） */
        void OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending);

    private:
        enum class Phase : uint8_t
        {
            /// 読み込みが落ち着くのを待ち、基準からのフレームを数える
            Settling,
            /// 撮影の結果を待つ
            WaitingResult,
            /// GameMode が視点を置き直すのを待つ
            WaitingSwitch,
            Done,
            Failed,
        };

        bool IsLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                       uint64_t renderedFrameCount) const;
        void Fail(const char* reason);

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> m_ViewNames;
        NorvesLib::Core::Container::String m_OutputDirectory;
        uint64_t m_SettledRenderedFrames = 0;
        uint64_t m_ViewSettleRenderedFrames = kDefaultViewSettleRenderedFrames;
        Game::Scenes::SceneViewSwitch* m_pViewSwitch = nullptr;

        Phase m_Phase = Phase::Settling;
        size_t m_ViewIndex = 0;
        bool m_bObservedPendingAssets = false;
        bool m_bBaselineLatched = false;
        uint64_t m_BaselineRenderedFrame = 0;
        /// この描画フレーム数に届くまでは読み込み中として扱う（視点を置き直した後の待ち）
        uint64_t m_SettleUntilRenderedFrame = 0;
        /// 撮影の要求・視点の切り替えの依頼を出したときの描画フレーム数（待ちの上限を数える）
        uint64_t m_WaitStartRenderedFrame = 0;
    };
} // namespace Game::Debug
