/**
 * @file CaptureSettle.h
 * @brief ストリーミングが落ち着くのを待ち、決定的な撮影の基準と撮影可能なフレームを決める
 *
 * MultiViewCapture と、今後地図のタイルを撮る MapCapture が使う。カメラ移動後の待ち、ほかの保留、
 * 非同期資産・骨格の資産、決定的な撮影で SceneReady になる前を読み込み中とする。
 * 読み込み中を一度見てから、読み込みが終わった最初の描画を基準にし、決定的な撮影が有効なら
 * 撮影と描画のエポックを始める。その後、指定の描画フレーム数を経たら撮影可能とする。
 * 描画前後に読み込み中を見たら基準を外し、描画フレーム数が基準より戻ったら基準を付け直す。
 */
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::Debug
{
    class CaptureSettle
    {
    public:
        /// 撮影までのフレーム数を設定し、読み込みの観測・基準・移動後の待ちを初期化する
        void Reset(uint64_t settledRenderedFrames);

        /// 移動後の待ちを設定し、読み込み中を観測した状態で基準を外す
        void RestartAfterMove(uint64_t renderedFrameCount, uint64_t waitRenderedFrames);

        /**
         * @brief 描画前の読み込み中を確認し、基準を外す
         * @param renderWorld 初期化済みの描画ワールド
         * @param bOtherPending ほかに読み込みの反映を待っているものがあるか
         */
        void OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending);

        /**
         * @brief 描画後に基準とエポックを設定し、撮影可能なフレームかを返す
         * @param renderWorld 初期化済みの描画ワールド
         * @param bOtherPending ほかに読み込みの反映を待っているものがあるか
         * @param renderedFrameCount 呼び出し側が描画後に取得した描画フレーム数
         * @param logTag 基準を記録するログの接頭辞
         * @param itemName 基準を記録する対象の名前
         */
        bool OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                          uint64_t renderedFrameCount, const char* logTag, const char* itemName);

    private:
        bool IsLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                       uint64_t renderedFrameCount) const;

        uint64_t m_SettledRenderedFrames = 0;
        bool m_bObservedPendingAssets = false;
        bool m_bBaselineLatched = false;
        uint64_t m_BaselineRenderedFrame = 0;
        /// この描画フレーム数に届くまでは読み込み中として扱う
        uint64_t m_SettleUntilRenderedFrame = 0;
    };
} // namespace Game::Debug
