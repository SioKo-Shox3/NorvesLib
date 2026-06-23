#pragma once

#include "Input/IInputController.h"
#include "Input/InputTypes.h"
#include "Rendering/SceneProxy.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::Component
{
    class LightComponent;
} // namespace NorvesLib::Core::Component

namespace NorvesLib::Core::Input
{

    /**
     * @brief ライトコントローラー（イベント駆動 IInputController）
     *
     * キーボード操作でDirectionalライトの方向/強度を操作します。
     * InputRouter にゲーム優先度で登録し、自分のバインドキー（矢印・+/-・Shift）
     * のみを consume（その他は透過）。連続 hold は OnKey で held フラグを更新し、
     * Update(deltaTime) で適用します。
     *
     * デフォルト操作:
     * - 矢印キー: ライトの方向を変更（Yaw/Pitch）
     * - +/-: ライトの強度を変更
     * - Shift + 矢印キー: 高速回転
     *
     * 操作対象は LightComponent（推奨）または LightProxy（後方互換）。
     */
    class LightController : public IInputController
    {
    public:
        LightController();
        ~LightController() override = default;

        // ========================================
        // 初期化・設定
        // ========================================

        /**
         * @brief 操作対象のライトを設定（LightProxy 直接・後方互換）
         * @param light 操作対象のLightProxy
         */
        void SetTargetLight(Rendering::LightProxy *light);

        /**
         * @brief 操作対象のライトを取得
         */
        Rendering::LightProxy *GetTargetLight() const;

        /**
         * @brief 操作対象の LightComponent を設定（推奨経路）
         *
         * 設定時、コンポーネントの現在の方向/強度から内部の Yaw/Pitch と
         * 強度を seed し、初回入力での値ジャンプを防ぐ。LightComponent が
         * 設定されている間は LightProxy 経路より優先して反映する。
         * @param light 操作対象の LightComponent（nullptr で解除）
         */
        void SetTargetComponent(Component::LightComponent *light);

        /**
         * @brief 操作対象の LightComponent を取得
         */
        Component::LightComponent *GetTargetComponent() const;

        /**
         * @brief ライト方向を直接設定（Yaw/Pitch角度）
         * @param yaw 水平回転角度（度）
         * @param pitch 垂直回転角度（度）
         */
        void SetDirection(float yaw, float pitch);

        // ========================================
        // 入力イベント（IInputController）
        // ========================================

        /**
         * @brief キーイベントを内部 held フラグへ反映する
         *
         * 自分のバインドキー（Yaw/Pitch/強度の各キーと Shift）の Pressed/Released
         * を held フラグへ反映し、それらのキーは consume（true）する。その他の
         * キーは透過（false）させ、下位（debug 等）のコントローラへ届ける。
         */
        bool OnKey(const KeyEvent &event) override;

        /**
         * @brief デバッグ用のコントローラ名
         */
        const char *DebugName() const override
        {
            return "LightController";
        }

        // ========================================
        // 更新
        // ========================================

        /**
         * @brief held フラグと経過時間に基づいてライトを更新
         * @param deltaTime フレーム間隔（秒）
         *
         * OnKey が更新した held フラグに従い、Yaw/Pitch の回転と強度変更を適用
         * する（Shift 押下中は SHIFT_MULTIPLIER 倍）。InputState には依存しない。
         */
        void Update(float deltaTime);

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
         * @brief Yaw/Pitchから対象（Component優先・なければProxy）の方向を再計算
         */
        void RecalculateDirection();

        /**
         * @brief 現在の強度を取得（Component優先・なければProxy・無ければ0）
         */
        float GetCurrentIntensity() const;

        /**
         * @brief 強度を対象（Component優先・なければProxy）へ反映する
         */
        void ApplyIntensity(float intensity);

        /**
         * @brief 与えられた KeyCode が自分のバインドキーかを判定する
         */
        bool IsBoundKey(KeyCode code) const;

        // 操作対象（Component を優先。後方互換で Proxy も保持）
        Component::LightComponent *m_TargetComponent = nullptr;
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

        // 押下中フラグ（OnKey で更新し Update で適用）
        bool m_bHoldYawNeg = false;
        bool m_bHoldYawPos = false;
        bool m_bHoldPitchNeg = false;
        bool m_bHoldPitchPos = false;
        bool m_bHoldIntensityUp = false;
        bool m_bHoldIntensityDown = false;
        bool m_bHoldShift = false;

        // Shift倍率
        static constexpr float SHIFT_MULTIPLIER = 3.0f;
    };

} // namespace NorvesLib::Core::Input
