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
// 撮影の段取りの項目（入力ではなく、その行の決まったフレームで 1 回だけ働く。Debug/CameraScriptCapture.h）:
//   home             行の最初のフレームの入力の前に、カメラを基準の視点（起動時か、撮影が置き直した視点）へ戻す
//   epoch            行の最初のフレームを 0 番として、決定的な撮影のエポック（時間・TAA の履歴・RTGI・自動露出）を始め直す
//   capture=<名前>   行の最後のフレームを撮る（<視点>.<名前>.png。名前は英数字と - _ で 32 文字まで）
//   wait-ready       行を流し終えたあと、ストリーミングが落ち着くまで動かさずに待つ
// 例:
//   30 keys=W              # 30 フレーム前進
//   20 rmb mouse=5,0       # 右ボタンを押して右へ 5px ずつ 20 フレーム
//   10                     # 10 フレーム何もしない
//   1 epoch capture=a      # 次のフレームをエポックの 0 番にして撮る
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
        /// 撮影の段取り（ヘッダ冒頭の home・epoch・capture・wait-ready）
        bool bHome = false;
        bool bEpoch = false;
        bool bWaitReady = false;
        /// 空なら撮らない
        NorvesLib::Core::Container::String CaptureTag;
    };

    /// 1フレーム分の撮影の段取り（CameraScript::NextFrame が書く）
    struct CameraScriptFrameEvents
    {
        /// このフレームの入力の前に、カメラを基準の視点へ戻す
        bool bHome = false;
        /// このフレームをエポックの 0 番にする
        bool bEpoch = false;
        /// このフレームのあと、ストリーミングが落ち着くまで待つ
        bool bWaitReadyAfter = false;
        /// このフレームを撮る名前（撮らないなら nullptr。台本が持つ文字列を指す）
        const NorvesLib::Core::Container::String *pCaptureTag = nullptr;
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

        /// NextFrame と同じく進め、そのフレームの撮影の段取りを outEvents へ書く
        bool NextFrame(NorvesLib::Core::Input::InputState &out, CameraScriptFrameEvents &outEvents);

        /// 撮影の段取りの項目（home・epoch・capture・wait-ready）を含む行があるか
        bool HasCaptureDirectives() const;

    private:
        NorvesLib::Core::Container::VariableArray<CameraScriptStep> m_Steps;
        std::size_t m_StepIndex = 0;
        uint32_t m_FrameInStep = 0;
    };

} // namespace Game::Input
