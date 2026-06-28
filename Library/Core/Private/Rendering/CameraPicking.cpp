#include "Rendering/CameraPicking.h"
#include "Math/Vector3.h"
#include "Math/VectorUtils.h"
#include <cmath>

namespace NorvesLib::Core::Rendering
{

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

} // namespace NorvesLib::Core::Rendering
