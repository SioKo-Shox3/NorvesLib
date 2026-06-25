#pragma once

#include "Component.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/RenderTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{

    // ========================================
    // CameraComponent
    // ========================================

    /**
     * @brief カメラコンポーネント
     *
     * シーンを描画するための視点（レンズ層）を提供するコンポーネント。
     * 投影設定（パースペクティブ/オルソ）、視野角、クリップ面、ビューポート、
     * カリングマスク、描画順序などを保持し、CameraProxyを構築して
     * SceneView/SceneProxyへ同期します。
     *
     * カメラの位置・姿勢はオーナーのWorldObjectのトランスフォームから取得します。
     * - 位置: WorldObject::GetPosition()
     * - 向き: WorldObject::GetRotation()（クォータニオン）から
     *   forward/up/right を導出（ローカル +Z が forward の規約）。
     *
     * 投影に必要なAspectRatioは描画解像度に追従させるため、ここでは設定せず
     * CameraProxyの既定値のままにします。
     */
    class CameraComponent : public Component
    {
        REFLECTION_CLASS(CameraComponent, Component)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        CameraComponent();

        /**
         * @brief 初期化子を使用したコンストラクタ
         */
        explicit CameraComponent(const FieldInitializer *initializer);

        /**
         * @brief コピーコンストラクタ
         */
        explicit CameraComponent(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~CameraComponent();

        // ========================================
        // ライフサイクル
        // ========================================

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;
        virtual void Tick(float deltaTime) override;

        // ========================================
        // レンズ設定
        // ========================================

        /**
         * @brief 投影タイプを設定
         */
        void SetProjectionType(Rendering::ProjectionType type)
        {
            ProjectionTypeProp = type;
            MarkRenderStateDirty();
        }
        Rendering::ProjectionType GetProjectionType() const { return ProjectionTypeProp; }

        /**
         * @brief 視野角を設定（度、パースペクティブ用）
         */
        void SetFieldOfView(float fovDegrees)
        {
            FieldOfView = fovDegrees;
            MarkRenderStateDirty();
        }
        float GetFieldOfView() const { return FieldOfView; }

        /**
         * @brief ニアクリップ面を設定
         */
        void SetNearPlane(float nearPlane)
        {
            NearPlane = nearPlane;
            MarkRenderStateDirty();
        }
        float GetNearPlane() const { return NearPlane; }

        /**
         * @brief ファークリップ面を設定
         */
        void SetFarPlane(float farPlane)
        {
            FarPlane = farPlane;
            MarkRenderStateDirty();
        }
        float GetFarPlane() const { return FarPlane; }

        /**
         * @brief オルソ投影サイズを設定
         * @param width オルソ幅
         * @param height オルソ高さ
         */
        void SetOrthoSize(float width, float height)
        {
            OrthoWidth = width;
            OrthoHeight = height;
            MarkRenderStateDirty();
        }
        float GetOrthoWidth() const { return OrthoWidth; }
        float GetOrthoHeight() const { return OrthoHeight; }

        /**
         * @brief ビューポート矩形を設定
         */
        void SetViewport(const Rendering::ViewportRect &viewport)
        {
            m_Viewport = viewport;
            MarkRenderStateDirty();
        }
        const Rendering::ViewportRect &GetViewport() const { return m_Viewport; }

        /**
         * @brief カリングマスクを設定
         */
        void SetCullingMask(Rendering::RenderLayer mask)
        {
            CullingMaskProp = mask;
            MarkRenderStateDirty();
        }
        Rendering::RenderLayer GetCullingMask() const { return CullingMaskProp; }

        /**
         * @brief 描画順序を設定（複数カメラ用）
         */
        void SetRenderOrder(uint8_t order)
        {
            RenderOrder = order;
            MarkRenderStateDirty();
        }
        uint8_t GetRenderOrder() const { return RenderOrder; }

        /**
         * @brief アクティブカメラ設定
         */
        void SetActiveCamera(bool bActive)
        {
            bIsActiveCamera = bActive;
            MarkRenderStateDirty();
        }
        bool IsActiveCamera() const { return bIsActiveCamera; }

        // ========================================
        // CameraProxy構築
        // ========================================

        /**
         * @brief CameraProxyを構築して返す
         * @param outProxy 出力先
         * @return 有効なProxyが生成できた場合true
         *
         * オーナーのWorldObjectから位置・姿勢を取得し、レンズ設定をコピーします。
         * 向きの規約（ローカル +Z が forward）:
         *   forward = rotation * Vector3::Forward;
         *   up      = rotation * Vector3::Up;
         *   right   = rotation * Vector3::Right;
         * AspectRatioは描画解像度に追従させるため設定しません。
         *
         * @note このメソッドはオーナーのTransformとレンズ設定をCameraProxyへ
         *   **詰め替えるだけ**で、有効性（IsEnabled）やアクティブカメラ（bIsActiveCamera）
         *   による取捨選択は行わない。アクティブカメラの選定は呼び出し側
         *   （フェーズ2のWorld::SyncToSceneView相当）の責務とする。
         */
        virtual bool BuildCameraProxy(Rendering::CameraProxy &outProxy) const;

    protected:
        // ========================================
        // リフレクションプロパティ
        // ========================================

        PROPERTY(Rendering::ProjectionType, ProjectionTypeProp)
        PROPERTY(float, FieldOfView)
        PROPERTY(float, NearPlane)
        PROPERTY(float, FarPlane)
        PROPERTY(float, OrthoWidth)
        PROPERTY(float, OrthoHeight)
        PROPERTY(uint8_t, RenderOrder)
        PROPERTY(Rendering::RenderLayer, CullingMaskProp)
        PROPERTY(bool, bIsActiveCamera)

        // ========================================
        // 内部メンバ（リフレクション対象外）
        // ========================================

        // ViewportRectはplain structで前例がないためPROPERTYにしない
        Rendering::ViewportRect m_Viewport{};
    };

    // CameraComponentへのスマートポインタ
    using CameraComponentPtr = Container::TSharedPtr<CameraComponent>;
    using CameraComponentWeakPtr = Container::TWeakPtr<CameraComponent>;

} // namespace NorvesLib::Core::Component
