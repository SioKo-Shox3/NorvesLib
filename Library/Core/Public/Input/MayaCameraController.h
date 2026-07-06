#pragma once

#include "Input/IInputController.h"
#include "Input/InputState.h"
#include "Component/SpringArmTypes.h"
#include "Math/Vector3.h"
#include "Rendering/SceneProxy.h"

namespace NorvesLib::Core::Input
{

    /**
     * @brief Maya準拠のカメラコントローラー
     *
     * Mayaと同じ操作体系でカメラを操作します。
     *
     * 操作:
     * - Alt + LMB ドラッグ: Orbit (Tumble) - 注視点を中心にカメラを球面回転
     * - Alt + MMB ドラッグ: Pan (Track) - カメラと注視点を同時に平行移動
     * - Alt + RMB ドラッグ: Dolly (Zoom) - 注視点に近づく/遠ざかる
     * - スクロール: Dolly - 注視点に近づく/遠ざかる
     *
     * 内部モデル:
     * - 注視点（Target）を中心とした球面座標
     * - Yaw: 水平回転角度（度）
     * - Pitch: 垂直回転角度（度、-89～+89でクランプ）
     * - Distance: 注視点からカメラまでの距離
     */
    class MayaCameraController : public IInputController
    {
    public:
        MayaCameraController();
        ~MayaCameraController() override = default;

        // ========================================
        // 初期化
        // ========================================

        /**
         * @brief カメラコントローラーを初期化
         * @param target 注視点
         * @param distance 注視点からの距離
         * @param yaw 水平回転角度（度）
         * @param pitch 垂直回転角度（度）
         */
        void Initialize(const Math::Vector3 &target, float distance, float yaw = 0.0f, float pitch = 30.0f);

        // ========================================
        // 入力イベント（IInputController）
        // ========================================

        /**
         * @brief マウスボタンの押下/解放を内部状態へ反映する
         *
         * L/M/R いずれかのボタンであればドラッグ対象として consume する。
         * それ以外（X1/X2 等）は consume しない。
         */
        bool OnMouseButton(const MouseButtonEvent &event) override;

        /**
         * @brief 押下中ボタンに応じて Orbit/Pan/Dolly を適用する
         *
         * Left→Orbit, Middle→Pan, Right→Dolly。いずれかのドラッグを適用した
         * 場合のみ consume する（非押下時は伝播させる）。
         */
        bool OnMouseMove(const MouseMoveEvent &event) override;

        /**
         * @brief スクロール量に応じて Dolly を適用する（常に consume）
         */
        bool OnMouseScroll(const MouseScrollEvent &event) override;

        /**
         * @brief 入力状態を SpringArmIntent へ変換する（状態を書き換えない const 版）
         * @param input 現在の入力状態
         * @param deltaTime フレーム間隔（秒）
         * @param currentArmLength 現在のアーム長（距離依存量のスケールに使う）
         * @return この1フレームの操作意図
         *
         * イベント駆動の OnMouseButton/OnMouseMove/OnMouseScroll（内部の
         * m_Target/m_Distance/m_Yaw/m_Pitch を直接更新する）とは独立した、
         * poll 型の入力換算 API です。内部状態は一切変更しません。
         * 3層構成（MayaCameraController→SpringArmComponent→CameraComponent）で
         * SpringArmComponent を単一の真実源として駆動する構成のときは、
         * このメソッドで作った意図を SpringArmComponent::ApplyIntent へ渡し、
         * OnMouseButton/OnMouseMove/OnMouseScroll（イベント駆動側）は
         * InputRouter に登録しない運用を想定しています
         *（両方を同時に使うと m_Target 等と SpringArmComponent が二重に
         *   状態を持ち、Pan の焦点位置がずれる可能性があるため）。
         *
         * 距離依存量（Pan量・Dolly量）は呼び出し側が渡す現在の ArmLength
         * （currentArmLength）でスケールします。内部 m_Distance には依存しません。
         *
         * 換算規約（単一フレームに集約された入力に対し、OnMouseMove/OnMouseScroll と
         * 同一の感度係数を用いる。ただし m_Distance を currentArmLength に置換）:
         * - LMB ドラッグ → YawDelta/PitchDelta（m_OrbitSpeed 換算）
         * - MMB ドラッグ → PanDelta（スクリーン基底 x=right, y=up。
         *   panAmount = m_PanSpeed * currentArmLength）
         * - RMB ドラッグ / スクロール → DollyDelta（m_DollySpeed/m_ScrollDollySpeed 換算。
         *   currentArmLength でスケール。+で近づく＝ArmLengthを縮める向き）
         *
         * 注意: poll 版は `input` に蓄積された1フレーム分の Delta/ScrollDelta を
         * currentArmLength で1回だけスケールします。イベント駆動側（OnMouseMove/
         * OnMouseScroll）は同一フレームに複数イベントが来ると、その都度更新後の
         * m_Distance を使って逐次スケールするため、複数イベントが重なる場合は
         * 両者の結果が厳密には一致しません（単一イベント／単一フレーム集約の
         * 入力であれば一致します）。
         */
        Component::SpringArmIntent BuildIntent(const InputState &input, float deltaTime, float currentArmLength) const;

        /**
         * @brief デバッグ用のコントローラ名
         */
        const char *DebugName() const override
        {
            return "MayaCameraController";
        }

        // ========================================
        // カメラ状態の取得
        // ========================================

        /**
         * @brief カメラ位置を取得
         */
        Math::Vector3 GetPosition() const;

        /**
         * @brief カメラの前方ベクトルを取得（正規化済み）
         */
        Math::Vector3 GetForward() const;

        /**
         * @brief カメラの上方ベクトルを取得（正規化済み）
         */
        Math::Vector3 GetUp() const;

        /**
         * @brief カメラの右方ベクトルを取得（正規化済み）
         */
        Math::Vector3 GetRight() const;

        /**
         * @brief 注視点を取得
         */
        Math::Vector3 GetTarget() const;

        /**
         * @brief 注視点からの距離を取得
         */
        float GetDistance() const;

        /**
         * @brief Yaw角度を取得（度）
         */
        float GetYaw() const;

        /**
         * @brief Pitch角度を取得（度）
         */
        float GetPitch() const;

        // ========================================
        // CameraProxyへの反映
        // ========================================

        /**
         * @brief カメラ状態をCameraProxyに反映
         * @param camera 反映先のCameraProxy
         */
        void ApplyTo(Rendering::CameraProxy &camera) const;

        // ========================================
        // 感度設定
        // ========================================

        /**
         * @brief Orbit（回転）速度を設定
         * @param speed 回転速度（度/ピクセル、デフォルト: 0.3）
         */
        void SetOrbitSpeed(float speed);

        /**
         * @brief Pan（平行移動）速度を設定
         * @param speed 移動速度（ワールド単位/ピクセル、デフォルト: 0.005）
         */
        void SetPanSpeed(float speed);

        /**
         * @brief Dolly（ズーム）速度を設定
         * @param speed ドラッグ時のズーム速度（デフォルト: 0.01）
         */
        void SetDollySpeed(float speed);

        /**
         * @brief スクロールによるDolly速度を設定
         * @param speed スクロールズーム速度（デフォルト: 0.1）
         */
        void SetScrollDollySpeed(float speed);

        /**
         * @brief 最小距離を設定
         * @param minDistance 最小距離（デフォルト: 0.1）
         */
        void SetMinDistance(float minDistance);

        /**
         * @brief 最大距離を設定
         * @param maxDistance 最大距離（デフォルト: 10000.0）
         */
        void SetMaxDistance(float maxDistance);

    private:
        // ========================================
        // 内部操作
        // ========================================

        /**
         * @brief Orbit操作（Alt + LMB）
         * @param deltaX マウスX移動量
         * @param deltaY マウスY移動量
         */
        void Orbit(float deltaX, float deltaY);

        /**
         * @brief Pan操作（Alt + MMB）
         * @param deltaX マウスX移動量
         * @param deltaY マウスY移動量
         */
        void Pan(float deltaX, float deltaY);

        /**
         * @brief Dolly操作（Alt + RMB / スクロール）
         * @param delta ズーム量（正:近づく、負:遠ざかる）
         */
        void Dolly(float delta);

        /**
         * @brief 球面座標からカメラ位置を再計算
         */
        void RecalculatePosition();

        // ========================================
        // パラメーター
        // ========================================

        // 注視点
        Math::Vector3 m_Target;

        // 球面座標
        float m_Distance; ///< 注視点からの距離
        float m_Yaw;      ///< 水平回転角度（度）
        float m_Pitch;    ///< 垂直回転角度（度）

        // 計算済みカメラ位置
        Math::Vector3 m_Position;

        // ドラッグ中ボタンの押下状態（イベント駆動）
        bool m_bLeftDown = false;
        bool m_bMiddleDown = false;
        bool m_bRightDown = false;

        // 感度設定
        float m_OrbitSpeed;       ///< 回転速度（度/ピクセル）
        float m_PanSpeed;         ///< 平行移動速度
        float m_DollySpeed;       ///< ドラッグズーム速度
        float m_ScrollDollySpeed; ///< スクロールズーム速度

        // 距離制限
        float m_MinDistance;
        float m_MaxDistance;

        // ピッチ制限（度）
        static constexpr float PITCH_MIN = -89.0f;
        static constexpr float PITCH_MAX = 89.0f;
    };

} // namespace NorvesLib::Core::Input
