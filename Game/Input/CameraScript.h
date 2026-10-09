#pragma once

// ---------------------------------------------------------------------------
// カメラの入力の台本（--camera-script=<file>。開発ビルドだけ）
//
// 自由飛行カメラへ、決まった入力を決まったフレーム数だけ流すための台本。撮影で「この操作をしたらここへ動く」を
// 再現するのに使う。1フレームは固定刻み（1/60 秒）で数えるので、実機のフレームレートに結果が依らない。
//
// 書式（UTF-8 のテキスト。'#' から行末まではコメント、空行は無視）:
//   <フレーム数> [項目...]
// 項目は、そのフレーム数のあいだ毎フレーム同じ入力を流す。省いた項目は何も入力しない。
//   keys=W+A+Shift   押しているキー（W A S D Q E Shift を '+' でつなぐ）
//   rmb              右ボタンを押している（マウスの移動で視点が回る）
//   mouse=dx,dy      マウスの 1 フレームあたりの移動量[px]（右ボタンを押しているときだけ効く）
//   scroll=v         ホイールの 1 フレームあたりの回転量[段]
//   lstick=x,y       左スティック（-1〜1。Y は上が正）
//   rstick=x,y       右スティック
//   lt=v rt=v        左右のトリガー（0〜1）
// 例:
//   30 keys=W              # 30 フレーム前進
//   20 rmb mouse=5,0       # 右ボタンを押して右へ 5px ずつ 20 フレーム
//   10                     # 10 フレーム何もしない
// ---------------------------------------------------------------------------

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Input/InputState.h"

#include <cstddef>
#include <cstdint>

namespace Game::Input
{

    /// 台本の1行（同じ入力を Frames フレーム流す）
    struct CameraScriptStep
    {
        uint32_t Frames = 0;
        uint32_t KeyMask = 0;
        bool bRightButton = false;
        float MouseDeltaX = 0.0f;
        float MouseDeltaY = 0.0f;
        float Scroll = 0.0f;
        float LeftStick[2] = {0.0f, 0.0f};
        float RightStick[2] = {0.0f, 0.0f};
        float LeftTrigger = 0.0f;
        float RightTrigger = 0.0f;
    };

    class CameraScript
    {
    public:
        /// 1行で流せるフレーム数の上限
        static constexpr uint32_t kMaxFramesPerStep = 100000;

        /// キーのビット（CameraScriptStep::KeyMask）
        static constexpr uint32_t kKeyW = 1u << 0;
        static constexpr uint32_t kKeyA = 1u << 1;
        static constexpr uint32_t kKeyS = 1u << 2;
        static constexpr uint32_t kKeyD = 1u << 3;
        static constexpr uint32_t kKeyQ = 1u << 4;
        static constexpr uint32_t kKeyE = 1u << 5;
        static constexpr uint32_t kKeyShift = 1u << 6;

        /// テキストを台本として読む。成功したら true。失敗したら false で、行番号つきの理由を outError に入れる
        bool Parse(const char *text, std::size_t length, NorvesLib::Core::Container::String &outError);

        /// ファイルを読んで Parse する
        bool Load(const char *path, NorvesLib::Core::Container::String &outError);

        /// 先頭へ戻す
        void Rewind();

        /// すべてのフレームを流し終えたか
        bool IsFinished() const
        {
            return m_StepIndex >= m_Steps.size();
        }

        std::size_t GetStepCount() const
        {
            return m_Steps.size();
        }

        /// 台本の総フレーム数
        uint32_t GetTotalFrames() const;

        /// 次の1フレーム分の入力を out へ書いて進める。流し終えていれば false（out は触らない）
        bool NextFrame(NorvesLib::Core::Input::InputState &out);

    private:
        NorvesLib::Core::Container::VariableArray<CameraScriptStep> m_Steps;
        std::size_t m_StepIndex = 0;
        uint32_t m_FrameInStep = 0;
    };

} // namespace Game::Input
