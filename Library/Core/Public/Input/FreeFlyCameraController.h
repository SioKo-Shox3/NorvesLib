#pragma once

#include "Input/InputState.h"
#include "Math/Vector3.h"
#include "Rendering/SceneProxy.h"

namespace NorvesLib::Core::Input
{

    /**
     * @brief 自由飛行カメラの感度と速さ
     */
    struct FreeFlyCameraSettings
    {
        /// マウスで視点を回す感度（度/ピクセル）
        float LookDegreesPerPixel = 0.15f;
        /// 右スティックを倒し切ったときの視点の回る速さ（度/秒）
        float StickLookDegreesPerSecond = 120.0f;
        /// スティックの不感帯（0〜1。この値までは0として扱い、残りを0〜1へ伸ばす）
        float StickDeadZone = 0.15f;
        /// トリガー（LT/RT）の不感帯（0〜1）
        float TriggerDeadZone = 0.05f;
        /// 移動の速さの初期値（m/秒）
        float InitialMoveSpeed = 4.0f;
        /// 移動の速さの下限・上限（m/秒）
        float MinMoveSpeed = 0.1f;
        float MaxMoveSpeed = 500.0f;
        /// Shift（ゲームパッドは左スティックの押し込み）を押している間の速さの倍率
        float SprintMultiplier = 4.0f;
        /// ホイール1段で移動の速さに掛ける倍率（奥へ回すと増え、手前へ回すと減る）
        float ScrollSpeedFactor = 1.25f;
    };

    /**
     * @brief 自由飛行カメラのコントローラー
     *
     * シーンを自由に見て回る視点操作。位置と向き（Yaw/Pitch）だけを持ち、毎フレームのポーリング入力
     * （InputState）から更新する。IInputController ではないので、入力の配送（消費）には関与しない。
     * 上位の UI が消費した入力を混ぜないよう、呼ぶ側が配送後の値状態（CameraInputCollector など）を渡す。
     *
     * 操作:
     * - 右ボタンを押している間: マウスの移動で視点を回す
     * - W/S: 前後、A/D: 左右、E/Q: 上下（上下はワールドの上方向）
     * - Shift: 速く移動する
     * - ホイール: 移動の速さを変える（奥へ回すと速く、手前へ回すと遅く）
     * - ゲームパッド: 左スティックで前後左右、右スティックで視点、RT/LT で上下、左スティックの押し込みで速く
     *
     * 座標の約束（ワールドは右手系・Y 上。+Z を向いたとき画面の右は -X）:
     * - Yaw 0 度は +Z 向き。Yaw が増えると画面の右へ回る（-X 側へ向く）。Pitch は正で上向き（±89 度に収める）
     * - 前方向 = (-sinYaw cosPitch, sinPitch, cosYaw cosPitch)、右方向 = (-cosYaw, 0, -sinYaw)
     */
    class FreeFlyCameraController
    {
    public:
        FreeFlyCameraController() = default;

        /**
         * @brief 位置と向きを決める（移動の速さは初期値へ戻す）
         * @param position カメラの位置
         * @param yawDegrees 水平の向き（度）
         * @param pitchDegrees 垂直の向き（度。正で上向き）
         */
        void Initialize(const Math::Vector3 &position, float yawDegrees, float pitchDegrees);

        /**
         * @brief 1フレーム分の入力で位置と向きを進める
         * @param input そのフレームの値状態（マウスの移動量・ホイール量はフレームの累積）
         * @param deltaTime フレーム間隔（秒）
         */
        void Update(const InputState &input, float deltaTime);

        /// 感度と速さの設定
        void SetSettings(const FreeFlyCameraSettings &settings);
        const FreeFlyCameraSettings &GetSettings() const
        {
            return m_Settings;
        }

        Math::Vector3 GetPosition() const
        {
            return m_Position;
        }
        float GetYaw() const
        {
            return m_Yaw;
        }
        float GetPitch() const
        {
            return m_Pitch;
        }
        /// 今の移動の速さ（m/秒。Shift の倍率を含まない）
        float GetMoveSpeed() const
        {
            return m_MoveSpeed;
        }

        /// 前方向（正規化済み）
        Math::Vector3 GetForward() const;
        /// 右方向（水平。正規化済み）
        Math::Vector3 GetRight() const;
        /// 上方向（正規化済み）
        Math::Vector3 GetUp() const;

        /// 位置と向きを CameraProxy へ書く（投影や露出などの他の項目は触らない）
        void ApplyTo(Rendering::CameraProxy &camera) const;

        /// Yaw/Pitch（度）が表す前方向
        static Math::Vector3 ForwardFromAngles(float yawDegrees, float pitchDegrees);

        /// Pitch の上限（度の絶対値）
        static constexpr float PitchLimitDegrees = 89.0f;

    private:
        /// スティック軸の不感帯を外して -1〜1 へ伸ばす
        float ApplyStickDeadZone(float value) const;
        /// 全スロットのゲームパッドの値を合わせて -1〜1 に収める
        float GetPadAxis(const InputState &input, GamepadAxis axis) const;
        float GetPadTrigger(const InputState &input, GamepadTrigger trigger) const;

        FreeFlyCameraSettings m_Settings;
        Math::Vector3 m_Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        float m_MoveSpeed = 4.0f;
    };

} // namespace NorvesLib::Core::Input
