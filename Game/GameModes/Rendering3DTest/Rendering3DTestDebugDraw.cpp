#include "Rendering3DTestDebugDraw.h"

#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Math/GeometryTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"

namespace Game::GameModes
{
    void SubmitRendering3DTestDebugDraw()
    {
        NorvesLib::Core::GEngine.GetDebugDraw().AddAABB(
            NorvesLib::Math::AABB::FromCenterExtents(
                NorvesLib::Math::Vector3(0.0f, 0.5f, 0.0f),
                NorvesLib::Math::Vector3(1.0f, 1.0f, 1.0f)),
            NorvesLib::Math::Vector4(1.0f, 1.0f, 0.0f, 1.0f));
    }
} // namespace Game::GameModes
