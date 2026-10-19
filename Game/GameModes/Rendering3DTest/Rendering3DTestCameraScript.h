#pragma once

// ---------------------------------------------------------------------------
// Rendering3DTest の軌道カメラ（SpringArm）へカメラの台本（--camera-script=<file>。開発ビルドだけ）を流す部品
//
// 台本の書式は自由飛行カメラと同じ（Input/CameraScript.h）。軌道カメラへは次のように写す。
//   rmb mouse=dx,dy  回す（Alt + 左ドラッグの Orbit と同じ。自由飛行カメラで視点を回す入力を、注視点の周りに回す入力にする）
//   scroll=v         寄る・離れる（ホイールの Dolly）
// 感度は実際の操作と同じ（MayaCameraController::BuildIntent。回すのは 1px あたり 0.3°）。キー・スティック・トリガーは使わない。
// 台本を流している間は実際の入力を使わず、1フレームを固定刻み（1/60 秒）で進める。
// 撮影の段取り（home・epoch・capture・wait-ready）は自由飛行カメラと同じ窓口（Debug/CameraScriptCapture.h）へ頼む。
// home で戻る基準の視点は、Load したときの SpringArm の角度と腕の長さ。
// ---------------------------------------------------------------------------

#include "Input/CameraScript.h"

#include <cstdint>

namespace NorvesLib::Core::Component
{
    class SpringArmComponent;
}
namespace NorvesLib::Core::Input
{
    class MayaCameraController;
}
namespace Game::Debug
{
    class CameraScriptCapture;
}

namespace Game::GameModes
{

    class Rendering3DTestCameraScript
    {
    public:
        /// 台本を流すときの 1 フレームの長さ（秒）。決定的な撮影の固定刻みと同じ
        static constexpr float kFrameSeconds = 1.0f / 60.0f;

        /**
         * @brief 台本を読み、今の SpringArm の視点を基準の視点にする
         * @param scriptPath 台本のパス。空なら何もしない（開発ビルド以外では読まずに警告だけ出す）
         * @param pScriptCapture 撮影の段取りを頼む窓口（非所有。無ければ nullptr）
         * @return 台本を流し始められたら true
         */
        bool Load(const char *scriptPath, Game::Debug::CameraScriptCapture *pScriptCapture,
                  const NorvesLib::Core::Component::SpringArmComponent &springArm);

        /// 台本を流している最中か（読み込めなかった・流し終えたときは偽）
        bool IsRunning() const
        {
            return m_bActive;
        }

        /// 台本の 1 フレームを SpringArm へ流す（段取りの wait-ready の待ちを含む）
        void Tick(const NorvesLib::Core::Input::MayaCameraController &controller,
                  NorvesLib::Core::Component::SpringArmComponent &springArm);

        /// 流すのをやめる（シーンを出るとき）
        void Stop();

    private:
        Game::Input::CameraScript m_Script;
        Game::Debug::CameraScriptCapture *m_pScriptCapture = nullptr;
        bool m_bActive = false;
        uint32_t m_FramesPlayed = 0;
        /// 台本の wait-ready で、ストリーミングが落ち着くのを待っている
        bool m_bWaitingReady = false;
        uint32_t m_WaitedFrames = 0;
        /// 台本の home で戻る SpringArm の角度[度]と腕の長さ
        float m_HomeYaw = 0.0f;
        float m_HomePitch = 0.0f;
        float m_HomeArmLength = 0.0f;
    };

} // namespace Game::GameModes
