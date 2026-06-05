#include "Rendering/Viewport.h"
#include "Rendering/SceneProxy.h"
#include "Math/MatrixUtils.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::Rendering
{

    bool Viewport::Initialize(const ViewportSettings &settings)
    {
        if (m_bInitialized)
        {
            return false;
        }

        m_X = settings.X;
        m_Y = settings.Y;
        m_Width = settings.Width;
        m_Height = settings.Height;
        m_MinDepth = settings.MinDepth;
        m_MaxDepth = settings.MaxDepth;

        // TODO: レンダーターゲット作成

        m_bInitialized = true;
        return true;
    }

    void Viewport::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_RenderTarget = nullptr;
        m_OutputTexture = nullptr;

        m_bInitialized = false;
    }

    void Viewport::SetCamera(const CameraProxy &camera)
    {
        m_Camera = camera;
        UpdateMatrices();
    }

    void Viewport::SetRect(float x, float y, float width, float height)
    {
        m_X = x;
        m_Y = y;
        m_Width = width;
        m_Height = height;
    }

    void Viewport::GetRect(float &outX, float &outY, float &outWidth, float &outHeight) const
    {
        outX = m_X;
        outY = m_Y;
        outWidth = m_Width;
        outHeight = m_Height;
    }

    void Viewport::SetDepthRange(float minDepth, float maxDepth)
    {
        m_MinDepth = minDepth;
        m_MaxDepth = maxDepth;
    }

    void Viewport::GetDepthRange(float &outMinDepth, float &outMaxDepth) const
    {
        outMinDepth = m_MinDepth;
        outMaxDepth = m_MaxDepth;
    }

    void Viewport::GetPixelRect(uint32_t screenWidth, uint32_t screenHeight,
                                uint32_t &outX, uint32_t &outY,
                                uint32_t &outWidth, uint32_t &outHeight) const
    {
        outX = static_cast<uint32_t>(m_X * screenWidth);
        outY = static_cast<uint32_t>(m_Y * screenHeight);
        outWidth = static_cast<uint32_t>(m_Width * screenWidth);
        outHeight = static_cast<uint32_t>(m_Height * screenHeight);
    }

    void Viewport::UpdateMatrices()
    {
        // ビュー行列を計算
        // カメラの位置と向きからビュー行列を構築
        Math::Vector3 position(m_Camera.PositionX, m_Camera.PositionY, m_Camera.PositionZ);
        Math::Vector3 forward(m_Camera.ForwardX, m_Camera.ForwardY, m_Camera.ForwardZ);
        Math::Vector3 up(m_Camera.UpX, m_Camera.UpY, m_Camera.UpZ);
        Math::Vector3 target = position + forward;

        m_ViewMatrix = Math::MatrixUtils::CreateLookAt(position, target, up);

        // プロジェクション行列を計算
        float aspectRatio = GetAspectRatio();
        if (m_Camera.Projection == ProjectionType::Orthographic)
        {
            // 正射影
            m_ProjectionMatrix = Math::MatrixUtils::CreateOrthographic(
                m_Camera.OrthoWidth,
                m_Camera.OrthoWidth / aspectRatio,
                m_Camera.NearPlane,
                m_Camera.FarPlane);
        }
        else
        {
            // 透視投影
            m_ProjectionMatrix = Math::MatrixUtils::CreatePerspectiveFieldOfView(
                m_Camera.FieldOfView * (3.14159265f / 180.0f), // ラジアンに変換
                aspectRatio,
                m_Camera.NearPlane,
                m_Camera.FarPlane);
        }

        // ビュープロジェクション行列
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;
    }

} // namespace NorvesLib::Core::Rendering
