#pragma once

#include "Component.h"
#include "SpringArmTypes.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
}

namespace NorvesLib::Core::Component
{
    /**
     * @brief pivot を中心とした球面座標から owner Entity の world transform を駆動します。
     *
     * pivot は raw pointer ではなく ObjectId だけを保持し、利用時に owner の World から
     * 再解決します。これにより pending destroy や破棄後は安全に未解決となります。
     */
    class SpringArmComponent : public Component
    {
        REFLECTION_CLASS(SpringArmComponent, Component)

    public:
        SpringArmComponent();
        explicit SpringArmComponent(const FieldInitializer* initializer);
        explicit SpringArmComponent(const IUnknown* sourceObject);
        virtual ~SpringArmComponent();

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;
        virtual void Tick(float deltaTime) override;

        /**
         * @brief 同一 World の pivot を設定します。nullptr は設定解除として成功します。
         * @return 設定した場合 true。別 World、pending destroy、owner 自身は false。
         */
        bool SetPivot(const Entity* pivot);
        void SetPivotObjectId(uint64_t objectId);
        uint64_t GetPivotObjectId() const { return PivotObjectId; }
        void ClearPivot();
        bool HasValidPivot() const;
        Entity* ResolvePivot() const;

        void SetArmLength(float armLength);
        float GetArmLength() const { return ArmLength; }
        void SetYaw(float yaw);
        float GetYaw() const { return Yaw; }
        void SetPitch(float pitch);
        float GetPitch() const { return Pitch; }
        void SetTargetOffset(const Math::Vector3& offset);
        const Math::Vector3& GetTargetOffset() const { return TargetOffset; }
        void SetPitchLimits(float minPitch, float maxPitch);
        float GetMinPitch() const { return MinPitch; }
        float GetMaxPitch() const { return MaxPitch; }
        void SetArmLengthLimits(float minArmLength, float maxArmLength);
        float GetMinArmLength() const { return MinArmLength; }
        float GetMaxArmLength() const { return MaxArmLength; }

        void ApplyIntent(const SpringArmIntent& intent);
        void RefreshOwnerTransform();

    protected:
        Math::Vector3 ComputeArmOffset() const;
        void DriveOwnerTransform(const Entity& pivot);

        PROPERTY(uint64_t, PivotObjectId)
        PROPERTY(float, ArmLength)
        PROPERTY(float, Yaw)
        PROPERTY(float, Pitch)
        PROPERTY(Math::Vector3, TargetOffset)
        PROPERTY(float, MinPitch)
        PROPERTY(float, MaxPitch)
        PROPERTY(float, MinArmLength)
        PROPERTY(float, MaxArmLength)
    };

    using SpringArmComponentPtr = Container::TSharedPtr<SpringArmComponent>;
    using SpringArmComponentWeakPtr = Container::TWeakPtr<SpringArmComponent>;
} // namespace NorvesLib::Core::Component
