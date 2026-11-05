// 枯れ度の源の形と値を保存し、再生中だけ所有者に追従する源を登録する。
// 源の ID と現在値は実行時だけ保持し、シーンファイルには保存しない。
#pragma once

#include "Component.h"
#include "Vegetation/DrynessMap.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Component
{
    class WitherSourceComponent : public Component
    {
        REFLECTION_CLASS(WitherSourceComponent, Component)

    public:
        WitherSourceComponent();
        explicit WitherSourceComponent(const FieldInitializer* initializer);
        explicit WitherSourceComponent(const IUnknown* sourceObject);

        void BeginPlay() override;
        void EndPlay() override;

        void SetShape(Vegetation::DrynessShape shape) { getShape() = shape; }
        Vegetation::DrynessShape GetShape() const { return Shape; }
        void SetRadiusA(float radius) { getRadiusA() = radius; }
        float GetRadiusA() const { return RadiusA; }
        void SetRadiusB(float radius) { getRadiusB() = radius; }
        float GetRadiusB() const { return RadiusB; }
        void SetYaw(float yaw) { getYaw() = yaw; }
        float GetYaw() const { return Yaw; }
        void SetFalloff(float falloff) { getFalloff() = falloff; }
        float GetFalloff() const { return Falloff; }
        void SetDefaultLevel(float level) { getDefaultLevel() = level; }
        float GetDefaultLevel() const { return DefaultLevel; }
        void SetRecoverySeconds(float seconds) { getRecoverySeconds() = seconds; }
        float GetRecoverySeconds() const { return RecoverySeconds; }

        Vegetation::DrynessSourceId GetSourceId() const { return m_SourceId; }
        bool Recover();
        bool SetLevel(float level, float seconds);

        PROPERTY(Vegetation::DrynessShape, Shape)
        PROPERTY(float, RadiusA)
        PROPERTY(float, RadiusB)
        PROPERTY(float, Yaw)
        PROPERTY(float, Falloff)
        PROPERTY(float, DefaultLevel)
        PROPERTY(float, RecoverySeconds)

    private:
        void SetDefaults();
        void FollowOwner();

        Engine::Engine* m_RegisteredEngine = nullptr;
        Vegetation::DrynessSourceId m_SourceId = Vegetation::InvalidDrynessSourceId;
    };
} // namespace NorvesLib::Core::Component
