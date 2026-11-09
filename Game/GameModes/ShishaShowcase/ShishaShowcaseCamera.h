// 目の位置と注視点から、固定または時間で周回する共通カメラを作る。
#pragma once

#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/Math/Vector3.h"

namespace NorvesLib::Core
{
    class Entity;
}
namespace NorvesLib::Core::Component
{
    class CameraComponent;
}

namespace Game::GameModes
{
    class ShishaShowcaseCamera
    {
    public:
        bool Enter(NorvesLib::Core::GameMode::GameModeContext& ctx);
        void SetView(const NorvesLib::Math::Vector3& eye, const NorvesLib::Math::Vector3& target);
        void Update(NorvesLib::Core::GameMode::GameModeContext& ctx, double elapsedSeconds,
                    float orbitDegreesPerSecond);
        void Leave();

    private:
        NorvesLib::Math::Vector3 m_Eye{0.0f, 1.4f, -4.0f};
        NorvesLib::Math::Vector3 m_Target{0.0f, 1.0f, 0.0f};
        // 実体は GameModeScope が追跡した Entity と、その Inner が所有する。
        NorvesLib::Core::Entity* m_pEntity = nullptr;
        NorvesLib::Core::Component::CameraComponent* m_pCamera = nullptr;
    };
} // namespace Game::GameModes
