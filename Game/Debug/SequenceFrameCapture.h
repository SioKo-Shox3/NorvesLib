#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::Debug
{
    /**
     * @brief 1回の起動の中で、指定した描画フレームの最終出力（BackBuffer）を続けて PNG に保存する
     *
     * 起動引数 --capture-sequence=<接頭辞> と --capture-sequence-rendered-frames=<n1,n2,...> で使う。
     * フレームは --capture-png と同じく、アセットの読み込みが落ち着いてから描いたフレーム数で数え、
     * <接頭辞><n>.png へ保存する。TAA などの時間方向の履歴を保ったまま、動くカメラの複数の時点を撮るのに使う。
     * ApplicationProcessor の --capture-png（最後の1枚と終了）と併用でき、取得の要求は同時に1つだけなので、
     * こちらの撮影はそれより前のフレームに限る。
     */
    class SequenceFrameCapture
    {
    public:
        /** @brief 保存先の接頭辞と、撮る描画フレーム数（昇順に並べ替え、重複は除く）を設定する。 */
        void Configure(const NorvesLib::Core::Container::String& pathPrefix,
                       const NorvesLib::Core::Container::VariableArray<uint64_t>& renderedFrames);

        bool IsEnabled() const { return !m_PathPrefix.empty() && !m_RenderedFrames.empty(); }

        /** @brief 描画の前に呼ぶ（読み込み中のアセットを見たことを覚える）。 */
        void OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld);

        /** @brief 描画の後に呼ぶ（落ち着いてからのフレーム数を数え、撮影を要求し、結果を保存する）。 */
        void OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld);

    private:
        NorvesLib::Core::Container::String m_PathPrefix;
        NorvesLib::Core::Container::VariableArray<uint64_t> m_RenderedFrames;
        size_t m_NextIndex = 0;
        bool m_bObservedPendingAssets = false;
        bool m_bBaselineLatched = false;
        uint64_t m_BaselineRenderedFrame = 0;
        bool m_bCaptureRequested = false;
        uint64_t m_CaptureRequestedRenderedFrame = 0;
        bool m_bFailed = false;
    };
} // namespace Game::Debug
