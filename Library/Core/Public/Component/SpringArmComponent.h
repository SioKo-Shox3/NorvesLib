#pragma once

#include "Component.h"
#include "SpringArmTypes.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class WorldObject;
}

namespace NorvesLib::Core::Component
{

    // ========================================
    // SpringArmComponent
    // ========================================

    /**
     * @brief スプリングアーム（カメラブーム）コンポーネント
     *
     * ピボット（注視対象の WorldObject）を中心とした球面座標でアームを駆動し、
     * オーナーの WorldObject のトランスフォーム（位置・回転）を毎フレーム更新します。
     * オーナーには通常 CameraComponent が同居し、CameraComponent::BuildCameraProxy が
     * オーナーの回転から forward/up/right を導出します（ローカル +Z = forward の規約）。
     *
     * 3層構成における中間層:
     *   MayaCameraController（入力 → SpringArmIntent）
     *     → SpringArmComponent（アーム駆動・オーナー Transform 書き込み）
     *       → CameraComponent（レンズ）
     *
     * 注視点 = pivot->GetPosition() + TargetOffset。
     * カメラ位置 = 注視点を中心とした (ArmLength, Yaw, Pitch) の球面座標。
     * これは MayaCameraController::RecalculatePosition と同一式です。
     *
     * 寿命設計:
     * - ピボットは ObjectId（PivotObjectId）で保持し、毎回 World::FindObjectById で解決します。
     *   ObjectHeap ハンドルは使いません。
     * - ピボットが破棄されると解決は nullptr になり、Tick はオーナー Transform を維持して
     *   暴れません（use-after-free 回避）。
     * - PivotObjectId の有効性は同一 World ライフサイクル内に限ります。World は
     *   Initialize/Finalize で ObjectId を 1 にリセットするため、World をまたいで
     *   （例: 再 Initialize 後やシリアライズ復元で）保持した PivotObjectId は別個体へ
     *   誤解決し得ます。永続化する場合は世代付き ID 等が必要です。
     *
     * Pan の扱い（Maya 忠実）:
     * - Pan は TargetOffset ではなく **ピボット WorldObject の Position** を動かします
     *   （焦点移動）。TargetOffset は静的な構図補正として残します。
     */
    class SpringArmComponent : public Component
    {
        REFLECTION_CLASS(SpringArmComponent, Component)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        SpringArmComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit SpringArmComponent(const FieldInitializer *initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit SpringArmComponent(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~SpringArmComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;
        virtual void Tick(float deltaTime) override;

        // ========================================
        // ピボット管理（ObjectId 解決）
        // ========================================

        /**
         * @brief ピボットを設定（pivot->GetObjectId() を保持）
         * @param pivot 注視対象。nullptr なら無効化（PivotObjectId=0）
         */
        void SetPivot(const WorldObject *pivot);

        /**
         * @brief ピボット ObjectId を直接設定
         */
        void SetPivotObjectId(uint64_t id);

        /**
         * @brief ピボット ObjectId を取得
         */
        uint64_t GetPivotObjectId() const { return PivotObjectId; }

        /**
         * @brief ピボットをクリア（PivotObjectId=0）
         */
        void ClearPivot();

        /**
         * @brief 現在ピボットが解決可能か（owner→World→FindObjectById が非null）
         */
        bool HasValidPivot() const;

        /**
         * @brief 現在のピボット WorldObject を解決して返す（解決不可なら nullptr）
         */
        WorldObject *ResolvePivot() const;

        // ========================================
        // アームパラメーター
        // ========================================

        /**
         * @brief アーム長を設定（[MinArmLength, MaxArmLength] でクランプ）
         */
        void SetArmLength(float armLength);
        float GetArmLength() const { return ArmLength; }

        /**
         * @brief 方位角を設定（度、0-360 に正規化）
         */
        void SetYaw(float yaw);
        float GetYaw() const { return Yaw; }

        /**
         * @brief 仰角を設定（度、[MinPitch, MaxPitch] でクランプ）
         */
        void SetPitch(float pitch);
        float GetPitch() const { return Pitch; }

        /**
         * @brief 静的な構図補正オフセットを設定（注視点 = pivot 位置 + これ）
         */
        void SetTargetOffset(const Math::Vector3 &offset);
        const Math::Vector3 &GetTargetOffset() const { return TargetOffset; }

        /**
         * @brief 仰角の制限を設定（度）
         */
        void SetPitchLimits(float minPitch, float maxPitch);
        float GetMinPitch() const { return MinPitch; }
        float GetMaxPitch() const { return MaxPitch; }

        /**
         * @brief アーム長の制限を設定
         */
        void SetArmLengthLimits(float minArmLength, float maxArmLength);
        float GetMinArmLength() const { return MinArmLength; }
        float GetMaxArmLength() const { return MaxArmLength; }

        // ========================================
        // 入力意図の適用
        // ========================================

        /**
         * @brief 1フレーム分の入力意図をアームへ反映
         *
         * - Yaw  += intent.YawDelta（正規化）
         * - Pitch += intent.PitchDelta（[MinPitch, MaxPitch] クランプ）
         * - ArmLength -= intent.DollyDelta（[MinArmLength, MaxArmLength] クランプ）
         * - Pan: intent.PanDelta（スクリーン基底）を現在のカメラ basis（right/up）へ
         *   投影してワールド移動量を作り、**ピボットの Position** に加算（焦点移動）。
         *   ピボットが解決できない場合 Pan は無視されます。
         */
        void ApplyIntent(const SpringArmIntent &intent);

        /**
         * @brief 現在のアーム状態から owner WorldObject の Transform を即座に再計算して書き込む
         *
         * 現在の (ArmLength / Yaw / Pitch) と解決済みピボットから、owner WorldObject の
         * Transform（位置・回転）を 1 回だけ確定します。通常は World の自動 Tick が
         * `SpringArmComponent::Tick` を毎フレーム駆動しますが、それを待たず Enter 直後などに
         * カメラ姿勢を 1 回確定したい場合に用います。
         *
         * 契約:
         * - GameThread 前提（owner Transform を直接書き込むため）。
         * - ピボットが解決できない場合は owner Transform を変更せず維持します
         *   （破棄済みポインタには一切触れないため use-after-free 安全）。
         * - ピボット未解決時の警告抑制カウンタ（m_MissingPivotWarnTicks）は通常 `Tick` と
         *   同様に進みます。本メソッドと `Tick` は完全に同一のロジックを共有します
         *   （`Tick` は本メソッドへ委譲します）。
         *
         * 命名は MeshComponent::RefreshRenderTransformCache()（Tick とは独立に状態を確定する
         * 公開 API）と整合させています。
         */
        void RefreshOwnerTransform();

    protected:
        // ========================================
        // 内部駆動
        // ========================================

        /**
         * @brief ピボット解決済み前提でオーナーの Transform を更新
         *
         * 注視点 = pivot->GetPosition() + TargetOffset。
         * カメラ位置 = 球面座標（MayaCameraController::RecalculatePosition と同一式）。
         * forward = (target - cameraPos).Normalized()。
         * rotation = QuaternionUtils::LookRotation(forward, Vector3::Up)。
         * owner->SetPosition(cameraPos); owner->SetRotation(rotation);
         *
         * LookRotation の規約（ローカル +Z = forward）により、CameraComponent が
         * オーナー回転から導く forward = rotation * +Z は注視点を向きます。
         */
        void DriveOwnerTransform(const WorldObject *pivot);

        /**
         * @brief 現在の (ArmLength, Yaw, Pitch) から注視点中心の相対オフセットを算出
         *
         * MayaCameraController::RecalculatePosition と同一式（注視点からの相対）。
         */
        Math::Vector3 ComputeArmOffset() const;

        // ========================================
        // リフレクションプロパティ
        // ========================================

        PROPERTY(uint64_t, PivotObjectId)   // ピボットの ObjectId（0=無効）
        PROPERTY(float, ArmLength)          // アーム長（注視点からの距離）
        PROPERTY(float, Yaw)               // 方位角（度）
        PROPERTY(float, Pitch)            // 仰角（度）
        PROPERTY(Math::Vector3, TargetOffset) // 静的な構図補正オフセット
        PROPERTY(float, MinPitch)         // 仰角の下限（度）
        PROPERTY(float, MaxPitch)         // 仰角の上限（度）
        PROPERTY(float, MinArmLength)     // アーム長の下限
        PROPERTY(float, MaxArmLength)     // アーム長の上限

    private:
        // ピボット未解決時の警告ログ抑制用カウンター（リフレクション対象外）
        uint32_t m_MissingPivotWarnTicks = 0;
    };

    // SpringArmComponent へのスマートポインタ
    using SpringArmComponentPtr = Container::TSharedPtr<SpringArmComponent>;
    using SpringArmComponentWeakPtr = Container::TWeakPtr<SpringArmComponent>;

} // namespace NorvesLib::Core::Component
