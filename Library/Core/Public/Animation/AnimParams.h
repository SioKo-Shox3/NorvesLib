#pragma once
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Text/IdentityPool.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    using AnimParamHandle = uint32_t;
    inline constexpr AnimParamHandle InvalidAnimParam = UINT32_MAX;
    enum class AnimParamType : uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger
    };
    enum class AnimSignalBinding : uint8_t
    {
        None,
        Speed,
        VelocityX,
        VelocityY,
        VelocityZ,
        AccelerationX,
        AccelerationY,
        AccelerationZ,
        TurnRate,
        Grounded,
        GroundNormalX,
        GroundNormalY,
        GroundNormalZ
    };
    struct AnimDriveSignals
    {
        float Speed = 0;
        Math::Vector3 Velocity = Math::Vector3::Zero;
        Math::Vector3 Acceleration = Math::Vector3::Zero;
        float TurnRate = 0;
        bool bGrounded = false;
        Math::Vector3 GroundNormal = Math::Vector3::UnitY;
    };
    struct AnimParamValue
    {
        float Float = 0;
        int32_t Int = 0;
        bool Bool = false;
    };
    struct AnimParamDefinition
    {
        Identity Name;
        AnimParamType Type = AnimParamType::Float;
        AnimParamValue Default;
        AnimSignalBinding Binding = AnimSignalBinding::None;
    };
    class AnimParamSet
    {
      public:
        [[nodiscard]] bool Initialize(Container::Span<const AnimParamDefinition>);
        [[nodiscard]] AnimParamHandle Find(Identity name) const;
        [[nodiscard]] bool SetFloat(AnimParamHandle, float);
        [[nodiscard]] bool SetInt(AnimParamHandle, int32_t);
        [[nodiscard]] bool SetBool(AnimParamHandle, bool);
        [[nodiscard]] bool SetTrigger(AnimParamHandle);
        [[nodiscard]] bool ResetTrigger(AnimParamHandle);
        [[nodiscard]] bool ApplyDriveSignals(const AnimDriveSignals&);
        const AnimParamValue* Get(AnimParamHandle) const;
        const AnimParamDefinition* Definition(AnimParamHandle) const;
        size_t Size() const noexcept
        {
            return m_Values.size();
        }
        // 全状態機械の判定後に一度だけ消費し、同じUpdate内での参照順依存を作らない。
        void ConsumeTriggers();

      private:
        bool IsType(AnimParamHandle, AnimParamType) const;
        Container::VariableArray<AnimParamDefinition> m_Definitions;
        Container::VariableArray<AnimParamValue> m_Values;
    };
} // namespace NorvesLib::Core::Animation
