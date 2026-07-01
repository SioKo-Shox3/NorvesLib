#include "Rendering/CameraPicking.h"
#include "Math/Vector3.h"
#include "Math/VectorUtils.h"
#include <cmath>

namespace NorvesLib::Core::Rendering
{

namespace
{
    constexpr float RECT_EPSILON = 1.0e-3f;
    constexpr float PLANE_NORMAL_EPSILON = 1.0e-6f;
    constexpr float RADIUS_EPSILON = 1.0e-4f;

    float ClampFloat(float value, float minValue, float maxValue)
    {
        return std::fmax(minValue, std::fmin(value, maxValue));
    }

    bool IsFiniteVector(const Math::Vector3& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    bool BuildSidePlane(
        const Math::Vector3& firstDirection,
        const Math::Vector3& secondDirection,
        const Math::Vector3& centerDirection,
        const Math::Vector3& cameraOrigin,
        Math::Plane& outPlane)
    {
        Math::Vector3 normal = Math::VectorUtils::Cross(firstDirection, secondDirection);
        if (Math::VectorUtils::Length(normal) <= PLANE_NORMAL_EPSILON)
        {
            return false;
        }

        normal = Math::VectorUtils::Normalize(normal);
        if (Math::VectorUtils::Dot(normal, centerDirection) < 0.0f)
        {
            normal = normal * -1.0f;
        }

        outPlane = Math::Plane(normal, cameraOrigin);
        return true;
    }
} // namespace

    bool BuildPickingRay(const CameraProxy& camera, float screenX, float screenY, Math::Ray& outRay)
    {
        const ViewportRect& vp = camera.Viewport;
        const float width = vp.Width;
        const float height = vp.Height;
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }

        const float localX = screenX - vp.X;
        const float localY = screenY - vp.Y;
        if (localX < 0.0f || localX > width || localY < 0.0f || localY > height)
        {
            return false;
        }

        const float ndcX = 2.0f * localX / width - 1.0f;
        const float ndcY = 1.0f - 2.0f * localY / height;
        const Math::Vector3 cameraPosition(camera.PositionX, camera.PositionY, camera.PositionZ);
        const Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
        const Math::Vector3 up(camera.UpX, camera.UpY, camera.UpZ);

        const Math::Vector3 viewZ = Math::VectorUtils::Normalize(forward * -1.0f);
        const Math::Vector3 right = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(up, viewZ));
        const Math::Vector3 camUp = Math::VectorUtils::Cross(viewZ, right);
        const Math::Vector3 lookDir = Math::VectorUtils::Normalize(forward);

        if (camera.Projection == ProjectionType::Orthographic)
        {
            const Math::Vector3 origin =
                cameraPosition
                + right * (ndcX * camera.OrthoWidth * 0.5f)
                + camUp * (ndcY * camera.OrthoHeight * 0.5f);
            outRay = Math::Ray(origin, lookDir);
            return true;
        }

        const float fieldOfViewRadians = camera.FieldOfView * (Math::Constants::PI / 180.0f);
        const float tanHalfFov = std::tan(fieldOfViewRadians * 0.5f);
        const float aspectRatio = width / height;
        const Math::Vector3 direction = Math::VectorUtils::Normalize(
            lookDir
            + right * (ndcX * tanHalfFov * aspectRatio)
            + camUp * (ndcY * tanHalfFov));

        outRay = Math::Ray(cameraPosition, direction);
        return true;
    }

    bool BuildScreenRectFrustum(
        const CameraProxy& camera,
        float x0,
        float y0,
        float x1,
        float y1,
        Math::Frustum& outFrustum)
    {
        if (camera.Projection == ProjectionType::Orthographic)
        {
            return false;
        }

        const ViewportRect& vp = camera.Viewport;
        if (vp.Width <= 0.0f || vp.Height <= 0.0f)
        {
            return false;
        }

        const float viewportMinX = vp.X;
        const float viewportMinY = vp.Y;
        const float viewportMaxX = vp.X + vp.Width;
        const float viewportMaxY = vp.Y + vp.Height;
        float minX = std::fmin(x0, x1);
        float maxX = std::fmax(x0, x1);
        float minY = std::fmin(y0, y1);
        float maxY = std::fmax(y0, y1);

        minX = ClampFloat(minX, viewportMinX, viewportMaxX);
        maxX = ClampFloat(maxX, viewportMinX, viewportMaxX);
        minY = ClampFloat(minY, viewportMinY, viewportMaxY);
        maxY = ClampFloat(maxY, viewportMinY, viewportMaxY);
        if ((maxX - minX) <= RECT_EPSILON || (maxY - minY) <= RECT_EPSILON)
        {
            return false;
        }

        if (camera.NearPlane <= 0.0f || camera.FarPlane <= camera.NearPlane)
        {
            return false;
        }

        Math::Ray topLeft;
        Math::Ray topRight;
        Math::Ray bottomLeft;
        Math::Ray bottomRight;
        if (!BuildPickingRay(camera, minX, minY, topLeft)
            || !BuildPickingRay(camera, maxX, minY, topRight)
            || !BuildPickingRay(camera, minX, maxY, bottomLeft)
            || !BuildPickingRay(camera, maxX, maxY, bottomRight))
        {
            return false;
        }

        const Math::Vector3 cameraOrigin = topLeft.Origin;
        Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
        if (Math::VectorUtils::Length(forward) <= PLANE_NORMAL_EPSILON)
        {
            return false;
        }
        forward = Math::VectorUtils::Normalize(forward);

        Math::Vector3 centerDirection =
            topLeft.Direction + topRight.Direction + bottomLeft.Direction + bottomRight.Direction;
        if (Math::VectorUtils::Length(centerDirection) <= PLANE_NORMAL_EPSILON)
        {
            return false;
        }
        centerDirection = Math::VectorUtils::Normalize(centerDirection);

        Math::Plane leftPlane;
        Math::Plane rightPlane;
        Math::Plane bottomPlane;
        Math::Plane topPlane;
        if (!BuildSidePlane(topLeft.Direction, bottomLeft.Direction, centerDirection, cameraOrigin, leftPlane)
            || !BuildSidePlane(bottomRight.Direction, topRight.Direction, centerDirection, cameraOrigin, rightPlane)
            || !BuildSidePlane(bottomLeft.Direction, bottomRight.Direction, centerDirection, cameraOrigin, bottomPlane)
            || !BuildSidePlane(topRight.Direction, topLeft.Direction, centerDirection, cameraOrigin, topPlane))
        {
            return false;
        }

        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Left)] = leftPlane;
        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Right)] = rightPlane;
        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Bottom)] = bottomPlane;
        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Top)] = topPlane;
        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Near)] =
            Math::Plane(forward, cameraOrigin + forward * camera.NearPlane);
        outFrustum.Planes[static_cast<int>(Math::FrustumPlane::Far)] =
            Math::Plane(forward * -1.0f, cameraOrigin + forward * camera.FarPlane);
        return true;
    }

    bool BuildSelectionSphere(
        const CameraProxy& camera,
        float centerX,
        float centerY,
        float edgeX,
        float edgeY,
        float centerAlongRayDistance,
        Math::Sphere& outSphere)
    {
        if (!std::isfinite(centerX)
            || !std::isfinite(centerY)
            || !std::isfinite(edgeX)
            || !std::isfinite(edgeY)
            || !std::isfinite(centerAlongRayDistance)
            || centerAlongRayDistance <= 0.0f)
        {
            return false;
        }

        Math::Ray centerRay;
        if (!BuildPickingRay(camera, centerX, centerY, centerRay)
            || !IsFiniteVector(centerRay.Origin)
            || !IsFiniteVector(centerRay.Direction))
        {
            return false;
        }

        const Math::Vector3 center = centerRay.Origin + centerRay.Direction * centerAlongRayDistance;
        if (!IsFiniteVector(center))
        {
            return false;
        }

        Math::Ray edgeRay;
        if (!BuildPickingRay(camera, edgeX, edgeY, edgeRay)
            || !IsFiniteVector(edgeRay.Origin)
            || !IsFiniteVector(edgeRay.Direction))
        {
            return false;
        }

        const Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
        const float forwardLength = Math::VectorUtils::Length(forward);
        if (!std::isfinite(forwardLength) || forwardLength <= PLANE_NORMAL_EPSILON)
        {
            return false;
        }

        const Math::Vector3 normal = Math::VectorUtils::Normalize(forward);
        if (!IsFiniteVector(normal))
        {
            return false;
        }

        const float denom = Math::VectorUtils::Dot(normal, edgeRay.Direction);
        if (!std::isfinite(denom) || std::fabs(denom) <= PLANE_NORMAL_EPSILON)
        {
            return false;
        }

        const float t = Math::VectorUtils::Dot(normal, center - edgeRay.Origin) / denom;
        if (!std::isfinite(t) || t <= 0.0f)
        {
            return false;
        }

        const Math::Vector3 edgePoint = edgeRay.Origin + edgeRay.Direction * t;
        if (!IsFiniteVector(edgePoint))
        {
            return false;
        }

        const float radius = Math::VectorUtils::Length(edgePoint - center);
        if (!std::isfinite(radius) || radius <= RADIUS_EPSILON)
        {
            return false;
        }

        outSphere.Center = center;
        outSphere.Radius = radius;
        return true;
    }

} // namespace NorvesLib::Core::Rendering
