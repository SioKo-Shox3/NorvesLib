#pragma once

#include "Input/InputState.h"
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
    class MayaCameraController
    {
    public:
        MayaCameraController();
        ~MayaCameraController() = default;

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
        // 更新
        // ========================================

        /**
         * @brief 入力状態に基づいてカメラを更新
         * @param input 現在の入力状態
         * @param deltaTime フレーム間隔（秒）
         */
        void Update(const InputState &input, float deltaTime);

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
