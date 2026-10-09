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
// 開発ビルドだけ、--camera-script=<file> の台本（Input/CameraScript.h）を自由飛行カメラへ流せる。台本を流している間は
// 実際の入力を無視し、1フレームを固定刻み（1/60 秒）で進める。台本が終わるまで IsScriptRunning() が真になる。
// ---------------------------------------------------------------------------

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/GameMode/GameModeContext.h"
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

namespace Game::Scenes
{

    class FreeCameraRig
    {
    public:
        /// 台本を流すときの 1 フレームの長さ（秒）。決定的な撮影の固定刻みと同じ
        static constexpr float kScriptFrameSeconds = 1.0f / 60.0f;

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

        const NorvesLib::Core::Input::FreeFlyCameraController &GetController() const
        {
            return m_Controller;
        }

    private:
        void Publish(NorvesLib::Core::GameMode::GameModeContext &ctx);
        void LogPose(const char *phase) const;

        NorvesLib::Core::Input::FreeFlyCameraController m_Controller;
        Game::Input::CameraInputCollector m_Collector;
        NorvesLib::Core::Entity *m_pCameraEntity = nullptr;
        NorvesLib::Core::Component::CameraComponent *m_pCamera = nullptr;
        NorvesLib::Core::Rendering::CameraProxy m_ProxyTemplate;
        bool m_bInputRegistered = false;

        Game::Input::CameraScript m_Script;
        bool m_bScriptActive = false;
        uint32_t m_ScriptFramesPlayed = 0;
    };

} // namespace Game::Scenes
