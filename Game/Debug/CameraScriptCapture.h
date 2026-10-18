#pragma once

#include "Core/Public/Container/String.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::Debug
{
    /**
     * @brief カメラの台本（Input/CameraScript.h）の撮影の段取り（epoch・capture・wait-ready）を受ける窓口
     *
     * 動くカメラの決まったフレームを撮るのに使う（遠景のぼけの動きの切り分け。Scripts/Terrain/ReferenceCheck.py --blur-motion）。
     * 持ち主は GameApplicationHandler で、--capture-views の保存先と先頭の視点の名前で Configure する。シーンの GameMode が
     * 自由飛行カメラ（Scenes/FreeCameraRig.h）へ非所有のポインタで渡し、台本を流す Tick から呼ぶ。GameThread だけが使う。
     *
     * 台本の Tick は描画の受け渡し（RenderWorld の BeginFrame・EndFrame）より前に回るので、Tick で頼んだエポックは
     * BeginFrame がこのフレームのパケットを 0 番にし、Tick で頼んだ撮影は EndFrame がこのフレームのパケットへ写す。
     * どちらも台本がカメラを置いたフレームと同じフレームに効く。ログの CAMERA_SCRIPT_EPOCH・CAMERA_SCRIPT_CAPTURE の
     * engine_frame と、保存の frame（撮れたパケットの番号）の差で、撮影の検査が対応を確かめる。
     *
     * 撮影の要求は同時に 1 つだけ（読み出しを終えるまで次を受けない）なので、台本は撮影の間を数フレーム空ける。
     * 断られた・保存できなかったときは終了（1）を頼む。保存先は <保存先>/<視点>.<名前>.png。
     */
    class CameraScriptCapture
    {
    public:
        /// 落ち着いたとみなすまでに続けて要る、読み込み中でない描画フレームの数
        static constexpr uint32_t kReadyStableFrames = 30;

        /// 保存先のフォルダと、ファイル名の頭にする視点の名前を設定する
        void Configure(const NorvesLib::Core::Container::String& outputDirectory,
                       const NorvesLib::Core::Container::String& viewName);

        bool IsConfigured() const { return !m_OutputDirectory.empty(); }

        /// このフレームを 0 番として、決定的な撮影のエポックを始め直す（台本の Tick から呼ぶ）
        void BeginEpoch();

        /**
         * @brief このフレームの最終出力を撮る（台本の Tick から呼ぶ）
         * @param tag ファイル名の名前（<視点>.<tag>.png）
         * @param position カメラの位置（ログに書く）
         * @param yaw カメラの向き[度]（ログに書く）
         * @param pitch カメラの向き[度]（ログに書く）
         */
        void RequestCapture(const NorvesLib::Core::Container::String& tag, const float position[3], float yaw, float pitch);

        /// ストリーミング（非同期の資産・ジオメトリのページ・骨格の資産）が kReadyStableFrames 続けて落ち着いているか
        bool IsReady() const { return m_ReadyFrames >= kReadyStableFrames; }

        /** @brief 描画の前に呼ぶ（読み込み中かを見て、落ち着いたフレームを数える） */
        void OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld);

        /** @brief 描画の後に呼ぶ（撮影の結果を受け取って保存する） */
        void OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld);

    private:
        void Fail(const char* reason);

        NorvesLib::Core::Container::String m_OutputDirectory;
        NorvesLib::Core::Container::String m_ViewName;
        uint32_t m_ReadyFrames = 0;
        bool m_bFailed = false;
        bool m_bWaitingResult = false;
        NorvesLib::Core::Container::String m_PendingTag;
        uint64_t m_PendingEngineFrame = 0;
        /// 結果を待ち始めてから描いた（OnPostRender の）回数
        uint64_t m_WaitedFrames = 0;
    };
} // namespace Game::Debug
