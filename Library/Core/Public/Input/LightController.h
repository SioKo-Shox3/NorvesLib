#pragma once

#include "Input/InputTypes.h"
#include "Input/InputState.h"
#include "Rendering/SceneProxy.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::Input
{

    /**
     * @brief ライトコントローラー
     *
     * キーボード/マウス操作でDirectionalライトの方向を操作します。
     *
     * デフォルト操作:
     * - 矢印キー: ライトの方向を変更（Yaw/Pitch）
     * - +/-: ライトの強度を変更
     * - Shift + 矢印キー: 高速回転
     *
     * 操作対象のLightProxyを設定して使用します。
     */
    class LightController
    {
    public:
        LightController();
        ~LightController() = default;

        // ========================================
        // 初期化・設定
        // ========================================

        /**
         * @brief 操作対象のライトを設定
         * @param light 操作対象のLightProxy
         */
        void SetTargetLight(Rendering::LightProxy *light);

        /**
         * @brief 操作対象のライトを取得
         */
        Rendering::LightProxy *GetTargetLight() const;

        /**
         * @brief ライト方向を直接設定（Yaw/Pitch角度）
         * @param yaw 水平回転角度（度）
         * @param pitch 垂直回転角度（度）
         */
        void SetDirection(float yaw, float pitch);

        // ========================================
        // 更新
        // ========================================

        /**
         * @brief 入力状態に基づいてライトを更新
         * @param input 現在の入力状態
         * @param deltaTime フレーム間隔（秒）
         */
        void Update(const InputState &input, float deltaTime);

        // ========================================
        // 速度設定
        // ========================================

        /**
         * @brief 回転速度を設定
         * @param speed 回転速度（度/秒、デフォルト: 90.0）
         */
        void SetRotationSpeed(float speed);

        /**
         * @brief 強度変更速度を設定
         * @param speed 強度変更速度（/秒、デフォルト: 1.0）
         */
        void SetIntensitySpeed(float speed);

        // ========================================
        // キーバインド設定
        // ========================================

        /**
         * @brief 回転操作のキーを変更
         * @param yawNeg Yaw負方向（デフォルト: Left）
         * @param yawPos Yaw正方向（デフォルト: Right）
         * @param pitchNeg Pitch負方向（デフォルト: Down）
         * @param pitchPos Pitch正方向（デフォルト: Up）
         */
        void SetRotationKeys(KeyCode yawNeg, KeyCode yawPos, KeyCode pitchNeg, KeyCode pitchPos);

        /**
         * @brief 強度操作のキーを変更
         * @param increase 強度増加（デフォルト: Equal/+）
         * @param decrease 強度減少（デフォルト: Minus/-）
         */
        void SetIntensityKeys(KeyCode increase, KeyCode decrease);

        // ========================================
        // 状態取得
        // ========================================

        /**
         * @brief 現在のYaw角度を取得（度）
         */
        float GetYaw() const;

        /**
         * @brief 現在のPitch角度を取得（度）
         */
        float GetPitch() const;

    private:
        /**
         * @brief Yaw/PitchからLightProxyの方向ベクトルを再計算
         */
        void RecalculateDirection();

        // 操作対象
        Rendering::LightProxy *m_TargetLight;

        // ライトの方向（球面座標）
        float m_Yaw;   ///< 水平回転角度（度）
        float m_Pitch; ///< 垂直回転角度（度）

        // 速度設定
        float m_RotationSpeed;  ///< 回転速度（度/秒）
        float m_IntensitySpeed; ///< 強度変更速度（/秒）

        // キーバインド
        KeyCode m_KeyYawNeg;        ///< Yaw負方向
        KeyCode m_KeyYawPos;        ///< Yaw正方向
        KeyCode m_KeyPitchNeg;      ///< Pitch負方向
        KeyCode m_KeyPitchPos;      ///< Pitch正方向
        KeyCode m_KeyIntensityUp;   ///< 強度増加
        KeyCode m_KeyIntensityDown; ///< 強度減少

        // Shift倍率
        static constexpr float SHIFT_MULTIPLIER = 3.0f;
    };

} // namespace NorvesLib::Core::Input
