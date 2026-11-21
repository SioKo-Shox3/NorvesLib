#pragma once

#include "Component.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{
    /**
     * @brief Entity のワールド姿勢とレンズ設定から描画用カメラ snapshot を構築します。
     *
     * CameraComponent は Entity の Inner として所有されます。BuildCameraProxy は
     * live Object を保持しない CameraProxy 値を生成するだけで、カメラの選定や
     * RenderThread への送信は呼び出し側が担当します。
     */
    class CameraComponent : public Component
    {
        REFLECTION_CLASS(CameraComponent, Component)

    public:
        CameraComponent();
        explicit CameraComponent(const FieldInitializer* initializer);
        explicit CameraComponent(const IUnknown* sourceObject);
        virtual ~CameraComponent();

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;

        void SetProjectionType(Rendering::ProjectionType type)
        {
            ProjectionTypeProp = type;
            MarkRenderStateDirty();
        }
        Rendering::ProjectionType GetProjectionType() const { return ProjectionTypeProp; }

        void SetFieldOfView(float fieldOfViewDegrees)
        {
            FieldOfView = fieldOfViewDegrees;
            MarkRenderStateDirty();
        }
        float GetFieldOfView() const { return FieldOfView; }

        void SetNearPlane(float nearPlane)
        {
            NearPlane = nearPlane;
            MarkRenderStateDirty();
        }
        float GetNearPlane() const { return NearPlane; }

        void SetFarPlane(float farPlane)
        {
            FarPlane = farPlane;
            MarkRenderStateDirty();
        }
        float GetFarPlane() const { return FarPlane; }

        void SetOrthoSize(float width, float height)
        {
            OrthoWidth = width;
            OrthoHeight = height;
            MarkRenderStateDirty();
        }
        float GetOrthoWidth() const { return OrthoWidth; }
        float GetOrthoHeight() const { return OrthoHeight; }

        void SetViewport(const Rendering::ViewportRect& viewport)
        {
            m_Viewport = viewport;
            MarkRenderStateDirty();
        }
        const Rendering::ViewportRect& GetViewport() const { return m_Viewport; }

        void SetCullingMask(Rendering::RenderLayer mask)
        {
            CullingMaskProp = mask;
            MarkRenderStateDirty();
        }
        Rendering::RenderLayer GetCullingMask() const { return CullingMaskProp; }

        void SetRenderOrder(uint8_t order)
        {
            RenderOrder = order;
            MarkRenderStateDirty();
        }
        uint8_t GetRenderOrder() const { return RenderOrder; }

        void SetActiveCamera(bool bActive)
        {
            bIsActiveCamera = bActive;
            MarkRenderStateDirty();
        }
        bool IsActiveCamera() const { return bIsActiveCamera; }

        static bool TryBuildExposureSnapshot(
            float aperture,
            float shutterSpeed,
            float iso,
            float exposureCompensation,
            Rendering::CameraProxy& outSnapshot);

        bool SetAperture(float aperture);
        float GetAperture() const { return Aperture; }

        bool SetShutterSpeed(float shutterSpeed);
        float GetShutterSpeed() const { return ShutterSpeed; }

        bool SetISO(float iso);
        float GetISO() const { return ISO; }

        bool SetExposureCompensation(float exposureCompensation);
        float GetExposureCompensation() const { return ExposureCompensation; }

        /**
         * @brief 露出の決め方（手動/自動）を設定します。
         *
         * 自動では露出補正が自動露出の補正になり、絞り・シャッター速度・ISO の露出は
         * 最初の測定が出るまでの値として使われます。
         */
        void SetExposureMode(Rendering::CameraExposureMode exposureMode);
        Rendering::CameraExposureMode GetExposureMode() const { return m_ExposureMode; }

        /**
         * @brief アンチエイリアスの方式（FXAA/TAA）を設定します。
         *
         * TAA は TAA のパスを持つ View（ディファードの SceneView）だけで働き、そのとき FXAA は外れます。
         */
        void SetAntiAliasingMode(Rendering::CameraAntiAliasingMode antiAliasingMode);
        Rendering::CameraAntiAliasingMode GetAntiAliasingMode() const { return m_AntiAliasingMode; }

        /**
         * @brief トーンマップ後のグレーディングのコントラストを設定します。
         *
         * 負は View のトーンマップ設定の値（既定 1.05）を使います。コントラストは表示のリニア値を 0.5 を中心に
         * 伸ばして 0〜1 へ切るため、1 より大きいと暗部が黒へ切れます。
         * @return 有限ならtrue。
         */
        bool SetGradingContrast(float contrast);
        float GetGradingContrast() const { return m_GradingContrast; }

        /**
         * @brief 被写界深度のピント距離（m）を設定します。0はピンホール（被写界深度なし）。
         * @return 有限で0以上ならtrue。
         */
        bool SetFocusDistance(float focusDistance);
        float GetFocusDistance() const { return FocusDistance; }

        /** @brief FieldOfView は設定済みの垂直画角を使い、Physical は焦点距離・撮像面・ゲートフィットから描画時に垂直画角を求めます。 */
        void SetLensMode(Rendering::CameraLensMode lensMode);
        /** @brief 垂直画角の指定方式を返します。 */
        Rendering::CameraLensMode GetLensMode() const { return LensMode; }

        /** @brief 焦点距離（mm）を設定します。有限の正の値以外は false を返します。 */
        bool SetFocalLengthMm(float focalLengthMm);
        /** @brief 物理レンズの焦点距離（mm）を返します。 */
        float GetFocalLengthMm() const { return FocalLengthMm; }

        /** @brief 撮像面の幅と高さ（mm）を設定します。どちらかが有限の正の値でなければ、両方の値を保ち false を返します。 */
        bool SetSensorSizeMm(float widthMm, float heightMm);
        /** @brief 撮像面の幅（mm）を返します。 */
        float GetSensorWidthMm() const { return SensorWidthMm; }
        /** @brief 撮像面の高さ（mm）を返します。 */
        float GetSensorHeightMm() const { return SensorHeightMm; }

        /** @brief 撮像面を描画の縦横比に合わせる方式（Horizontal、Vertical、Fill）を設定します。 */
        void SetGateFit(Rendering::CameraGateFit gateFit);
        /** @brief 撮像面のゲートフィット方式を返します。 */
        Rendering::CameraGateFit GetGateFit() const { return GateFit; }

        /** @brief 露出とは独立に動きぼけのシャッター角（度）を設定します。0 は従来の動きぼけを使い、有限の 0〜360 度以外は false を返します。 */
        bool SetShutterAngleDegrees(float shutterAngleDegrees);
        /** @brief 動きぼけのシャッター角（度）を返します。 */
        float GetShutterAngleDegrees() const { return ShutterAngleDegrees; }

        /** @brief 絞り羽根の枚数を設定します。0 は円板を表し、0 または 3〜16 枚以外は false を返します。 */
        bool SetBokehBladeCount(uint32_t bladeCount);
        /** @brief 絞り羽根の枚数を返します。0 は円板を表します。 */
        uint32_t GetBokehBladeCount() const { return BokehBladeCount; }

        /**
         * @brief カメラの不連続（カット）を印します。次の BuildCameraProxy から CutSerial が 1 増えます。
         *
         * 補間を切るテレポート・ファストトラベル・時間の飛ばしなど、前のフレームのカメラから続かないフレームで呼びます。
         * 描画はそのフレームで前のカメラ・TAA の履歴・パストレーサーの連番の前の値を捨てます。保存はしません（PROPERTY でない）。
         */
        void MarkCameraCut() { ++m_CutSerial; }
        /** @brief これまでに印したカットの数（CameraProxy::CutSerial に写す値）。 */
        uint32_t GetCutSerial() const { return m_CutSerial; }

        /**
         * @brief owner のワールド Transform とレンズ値を CameraProxy へ snapshot 化します。
         * @return owner が存在して snapshot を構築できた場合 true。
         *
         * IsEnabled と IsActiveCamera による選別は呼び出し側の責務です。
         * AspectRatio は描画解像度側の値を維持するため上書きしません。
         * 同じ Entity の ViewClearanceComponent と SpringArm の注視点から ViewClearance を設定します。
         * スプリングアームを更新する Camera 群の後に呼び出してください。
         */
        virtual bool BuildCameraProxy(Rendering::CameraProxy& outProxy) const;

    protected:
        PROPERTY(Rendering::ProjectionType, ProjectionTypeProp)
        PROPERTY(float, FieldOfView)
        PROPERTY(float, NearPlane)
        PROPERTY(float, FarPlane)
        PROPERTY(float, OrthoWidth)
        PROPERTY(float, OrthoHeight)
        PROPERTY(uint8_t, RenderOrder)
        PROPERTY(Rendering::RenderLayer, CullingMaskProp)
        PROPERTY(bool, bIsActiveCamera)
        PROPERTY(float, Aperture)
        PROPERTY(float, ShutterSpeed)
        PROPERTY(float, ISO)
        PROPERTY(float, ExposureCompensation)
        PROPERTY(float, FocusDistance)
        PROPERTY(Rendering::CameraLensMode, LensMode)
        PROPERTY(float, FocalLengthMm)
        PROPERTY(float, SensorWidthMm)
        PROPERTY(float, SensorHeightMm)
        PROPERTY(Rendering::CameraGateFit, GateFit)
        PROPERTY(float, ShutterAngleDegrees)
        PROPERTY(uint32_t, BokehBladeCount)

        Rendering::ViewportRect m_Viewport{};
        Rendering::CameraExposureMode m_ExposureMode = Rendering::CameraExposureMode::Manual;
        Rendering::CameraAntiAliasingMode m_AntiAliasingMode = Rendering::CameraAntiAliasingMode::FXAA;
        float m_GradingContrast = -1.0f;
        // カットの通し番号。PROPERTY にしない（保存・エディタの対象でない）。
        uint32_t m_CutSerial = 0;

    private:
        void SetDefaults();
    };

    using CameraComponentPtr = Container::TSharedPtr<CameraComponent>;
    using CameraComponentWeakPtr = Container::TWeakPtr<CameraComponent>;
} // namespace NorvesLib::Core::Component
