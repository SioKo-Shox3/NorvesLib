/**
 * @file MapCapture.h
 * @brief 1 回組み立てたシーンを真上からタイルごとに撮り、PNG と目録を保存する。
 *
 * 起動の形は --scene=<id> --map-capture=<spec.json>。引数と各呼び出し先の結線は呼び出し側が担う。
 * 撮影の間はシーンの空を既定の物理空に置き換え、太陽の向きを spec で固定する。
 * 荒野の空の霞・太陽の輝度の調整や、夜の集落の夜空は撮影に出ない。
 * 1. カメラの適用前も読み込みの落ち着きを数える。自由飛行カメラ（FreeCameraRig）が一度も
 *    適用されないまま落ち着いたシーンは、地図を撮れない理由をログへ出して終了（1）を頼む。
 * 2. 最初にカメラを適用した描画の後、タイル 0 の移動後の待ちを始める。
 * 3. タイルごとにストリーミングと基準からの描画を待ち、バックバッファを要求して結果を待つ。
 * 4. 取得した寸法が設定のバックバッファと一致するか確かめる。
 * 5. 余白と端の不要部分を切り、タイルの PNG を保存する。
 * 6. 次のタイルへ進んで待ち直す。全部保存したら目録を書いて正常終了（0）を頼む。
 * 要求・結果待ち・保存の失敗は終了（1）を頼む。有効な間はエンジン単独の撮影と終了を保留する。
 */
#pragma once

#include "Debug/CaptureSettle.h"
#include "Core/Public/Scene/MapCapturePlan.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class World;
} // namespace NorvesLib::Core

namespace NorvesLib::Core::Component
{
    class CameraComponent;
} // namespace NorvesLib::Core::Component

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
} // namespace NorvesLib::Core::Rendering

namespace Game::Debug
{
    class MapCapture
    {
    public:
        /// 設定を読み、撮影計画と出力先を用意する。失敗時は無効にして理由を返す。
        bool Configure(const NorvesLib::Core::Container::String& specPath, uint64_t settledRenderedFrames,
                       NorvesLib::Core::Container::String* outError);

        /// 設定に成功していれば、終了を頼んだ後も有効のままにする。
        bool IsEnabled() const
        {
            return m_Phase != Phase::Disabled;
        }
        /// 有効な間はエンジン単独の撮影と終了を止める。
        bool IsHoldingEngineCapture() const
        {
            return IsEnabled();
        }
        /// 完了または失敗になるまで、自由飛行の入力に代わってカメラを動かす。
        bool IsControllingCamera() const
        {
            return IsEnabled() && m_Phase != Phase::Done && m_Phase != Phase::Failed;
        }

        /// 現在のタイルへカメラを置き、射影と露出を設定する。
        void ApplyToCamera(NorvesLib::Core::Entity& cameraEntity,
                           NorvesLib::Core::Component::CameraComponent& camera);
        /// 描画へ渡すカメラを撮影用に上書きし、カメラの適用を記録する。
        void ApplyToProxy(NorvesLib::Core::Rendering::CameraProxy& proxy);
        /// ゲームの更新後、描画前に環境を上書きし、読み込み状態を見る。
        void OnPreRender(NorvesLib::Core::World* pWorld, NorvesLib::Core::Rendering::RenderWorld& renderWorld,
                         bool bOtherPending);
        /// 描画後に待機・撮影要求・切り取り・保存・終了を進める。
        void OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending);

    private:
        enum class Phase : uint8_t
        {
            Disabled,
            Settling,
            WaitingResult,
            Done,
            Failed
        };

        void Fail(const char* reason);

        NorvesLib::Core::Scene::MapCapturePlan m_Plan;
        NorvesLib::Core::Rendering::RenderLayer m_CullingMask = NorvesLib::Core::Rendering::RenderLayer::MapVisible;
        CaptureSettle m_Settle;
        Phase m_Phase = Phase::Disabled;
        size_t m_TileIndex = 0;
        uint64_t m_WaitStartRenderedFrame = 0;
        float m_ShutterSpeed = 1.0f;
        float m_SunAltitudeDegrees = 55.0f;
        float m_SunAzimuthDegrees = 135.0f;
        bool m_bHeightFog = false;
        bool m_bAerialPerspective = false;
        bool m_bCameraApplied = false;
        bool m_bFirstMoveStarted = false;
        bool m_bSunStatusLogged = false;
    };
} // namespace Game::Debug
