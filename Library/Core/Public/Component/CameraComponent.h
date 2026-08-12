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
        virtual void Tick(float deltaTime) override;

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
         * @brief owner のワールド Transform とレンズ値を CameraProxy へ snapshot 化します。
         * @return owner が存在して snapshot を構築できた場合 true。
         *
         * IsEnabled と IsActiveCamera による選別は呼び出し側の責務です。
         * AspectRatio は描画解像度側の値を維持するため上書きしません。
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

        Rendering::ViewportRect m_Viewport{};

    private:
        void SetDefaults();
    };

    using CameraComponentPtr = Container::TSharedPtr<CameraComponent>;
    using CameraComponentWeakPtr = Container::TWeakPtr<CameraComponent>;
} // namespace NorvesLib::Core::Component
