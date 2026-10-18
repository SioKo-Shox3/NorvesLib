#pragma once

// ---------------------------------------------------------------------------
// 自由飛行カメラをシーンへ据える部品
//
// 新しいシーンが既定で使うカメラ。カメラの実体（Entity + CameraComponent）を作り、入力ルーターへ集約役
// （CameraInputCollector）を登録し、毎フレームの入力で FreeFlyCameraController を進めて描画側のメインカメラへ渡す。
// シーンは次の形で使う。
//   Enter : Enter を呼ぶ（初期の視点は SceneCatalog の名前付き視点。GetCamera() で露出などを設定する）
//   Tick  : Tick を毎フレーム呼ぶ（ポーズメニューが開いている間は bSuspended を真にする）
//   Leave : Leave を呼ぶ（入力の登録を外す。実体は GameModeScope::Cleanup が返す）
// カメラパス（シーンのデモ飛行。SceneCatalog の ScenePathDescriptor）を持つシーンでは、P キー（ゲームパッドは Y）で
// 再生・停止する。再生中に自由飛行の入力（移動・視点）があると止まり、その位置から自由飛行へ戻る。
// 止めたあとの再生は、止めたパスの時刻へ戻って続ける。--camera-path-time=<秒> は起動時にパスの途中へ置く（撮影用）。
// 開発ビルドの --camera-path-play は、シーンの組み立てが済んだ（NotifySceneReady）ところから、その時刻（無ければ 0 秒）で
// パスを再生したままにする（計測用）。再生中は約 1 秒ごとにパスの時刻をログ（CAMERA_PATH_TIME）へ出す。
// 開発ビルドだけ、--camera-script=<file> の台本（Input/CameraScript.h）を自由飛行カメラへ流せる。台本を流している間は
// 実際の入力を無視し、1フレームを固定刻み（1/60 秒）で進める。台本が終わるまで IsScriptRunning() が真になる。
// 台本の撮影の段取り（home・epoch・capture・wait-ready）は SetScriptCapture で渡した窓口（Debug/CameraScriptCapture.h）へ頼む。
// home で戻る基準の視点は、Enter の視点か、最後に PlaceAt で置き直した視点。
// ---------------------------------------------------------------------------

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/Input/CameraPath.h"
#include "Core/Public/Input/FreeFlyCameraController.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Input/CameraInputCollector.h"
#include "Input/CameraScript.h"
#include "Scenes/SceneCatalog.h"

namespace NorvesLib::Core
{
    class Entity;
}
namespace NorvesLib::Core::Component
{
    class CameraComponent;
}

namespace Game::Debug
{
    class CameraScriptCapture;
}

namespace Game::Scenes
{

    class SceneViewSwitch;

    class FreeCameraRig
    {
    public:
        /// 台本を流すときの 1 フレームの長さ（秒）。決定的な撮影の固定刻みと同じ
        static constexpr float kScriptFrameSeconds = 1.0f / 60.0f;

        /**
         * @brief シーンのカメラパスを設定する（Enter の前に呼ぶ）
         * @param pPath シーンのカメラパスの定義。nullptr か不正なキー列ならパスなし
         * @param startTimeSeconds 0 以上なら、Enter で最初の視点を使わずパスのこの時刻に置く（停止したまま）。負なら使わない
         * @param bPlayWhenReady 真なら、NotifySceneReady でパスの再生を始める（開始の時刻は startTimeSeconds。負なら 0）
         */
        void ConfigurePath(const Game::Scenes::ScenePathDescriptor *pPath, float startTimeSeconds, bool bPlayWhenReady = false);

        /// 台本の撮影の段取りを頼む窓口（非所有。無ければ nullptr で、段取りのある台本は失敗の印をログに出す）。Enter の前に呼ぶ
        void SetScriptCapture(Game::Debug::CameraScriptCapture *pScriptCapture)
        {
            m_pScriptCapture = pScriptCapture;
        }

        /**
         * @brief カメラを作って入力を登録する
         * @param pose 初期の視点
         * @param scriptPath 台本のパス。空なら流さない（開発ビルド以外では無視する）
         * @return 作れたら true。失敗したときは何も残さない
         */
        bool Enter(NorvesLib::Core::GameMode::GameModeContext &ctx, const FreeCameraPose &pose, const char *scriptPath);

        /**
         * @brief 1フレーム進めて、メインカメラを描画側へ渡す
         * @param bSuspended 真なら入力を受けない（ポーズメニューが開いている間）。カメラは今の位置のまま渡す
         */
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, float deltaTime, bool bSuspended);

        /// 入力の登録を外す
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx);

        /// シーンの組み立てが済んだことを伝える。ConfigurePath で再生を頼まれていれば、最初の 1 回でパスの再生を始める
        void NotifySceneReady();

        /// 実行中にカメラを視点 pose へ置き直す（撮影の視点の切り替え）。パスの再生は止め、押しっぱなしの入力は捨てる
        void PlaceAt(const FreeCameraPose &pose);

        /**
         * @brief 視点の切り替えの依頼（Scenes/SceneViewSwitch.h）があれば、シーンの名前付き視点の位置と向きへ置き直す
         *
         * 視点の位置をそのまま使うシーン向け（地形の高さを足す荒野は自前で置き直す）。依頼が無ければ何もしない。
         * 自由飛行の視点でない・未知の名前なら置かずに失敗を返す。Tick の前に呼ぶ。
         */
        void ApplyRequestedView(Game::Scenes::SceneViewSwitch *pViewSwitch, const SceneDescriptor &scene);

        /// 台本を流している最中か（読み込めなかった・流し終えたときは偽）
        bool IsScriptRunning() const
        {
            return m_bScriptActive;
        }

        /// カメラの部品。露出などの設定に使う。Enter が成功する前と Leave の後は nullptr
        NorvesLib::Core::Component::CameraComponent *GetCamera() const
        {
            return m_pCamera;
        }

        /// メインカメラへ渡す CameraProxy のひな形。トーンマップの曲線などを入れておく
        NorvesLib::Core::Rendering::CameraProxy &ProxyTemplate()
        {
            return m_ProxyTemplate;
        }

        /// カメラパスを再生中か
        bool IsPathPlaying() const
        {
            return m_bPathPlaying;
        }

        const NorvesLib::Core::Input::FreeFlyCameraController &GetController() const
        {
            return m_Controller;
        }

    private:
        void Publish(NorvesLib::Core::GameMode::GameModeContext &ctx);
        void LogPose(const char *phase) const;
        /// パスの時刻 time の位置と向きへカメラを置く
        void PlaceOnPath(float time);
        void StartPath();
        void StopPath(const char *reason);
        /// 台本の 1 フレームを流す（段取りの wait-ready の待ちを含む）
        void TickScript();

        NorvesLib::Core::Input::FreeFlyCameraController m_Controller;
        Game::Input::CameraInputCollector m_Collector;
        NorvesLib::Core::Entity *m_pCameraEntity = nullptr;
        NorvesLib::Core::Component::CameraComponent *m_pCamera = nullptr;
        NorvesLib::Core::Rendering::CameraProxy m_ProxyTemplate;
        bool m_bInputRegistered = false;

        Game::Input::CameraScript m_Script;
        bool m_bScriptActive = false;
        uint32_t m_ScriptFramesPlayed = 0;
        Game::Debug::CameraScriptCapture *m_pScriptCapture = nullptr;
        /// 台本の home で戻る視点（Enter か PlaceAt の視点）
        FreeCameraPose m_HomePose{};
        /// 台本の wait-ready で、ストリーミングが落ち着くのを待っている
        bool m_bScriptWaitingReady = false;
        uint32_t m_ScriptWaitedFrames = 0;

        NorvesLib::Core::Input::CameraPath m_Path;
        /// Enter でパスの途中へ置く時刻[秒]。負なら置かない
        float m_PathStartTime = -1.0f;
        bool m_bPathPlaying = false;
        /// 再生中のパスの時刻[秒]
        float m_PathTime = 0.0f;
        /// NotifySceneReady で再生を始める（--camera-path-play）。始めたら下ろす
        bool m_bPlayWhenReady = false;
        /// 再生した Tick の数（時刻のログの間隔に使う）
        uint32_t m_PathPlayedFrames = 0;
    };

} // namespace Game::Scenes
