#include "Animation/AnimParams.h"
#include <cmath>
namespace NorvesLib::Core::Animation
{
    bool AnimParamSet::Initialize(Container::Span<const AnimParamDefinition> definitions)
    {
        if (definitions.size() >= InvalidAnimParam)
        {
            return false;
        }
        for (size_t i = 0; i < definitions.size(); ++i)
        {
            const auto& d = definitions[i];
            if (!d.Name.IsValid() || uint8_t(d.Type) > uint8_t(AnimParamType::Trigger) ||
                uint8_t(d.Binding) > uint8_t(AnimSignalBinding::GroundNormalZ) ||
                (d.Type == AnimParamType::Float && !std::isfinite(d.Default.Float)))
            {
                return false;
            }
            if (d.Binding != AnimSignalBinding::None &&
                (d.Binding == AnimSignalBinding::Grounded ? d.Type != AnimParamType::Bool
                                                          : d.Type != AnimParamType::Float))
            {
                return false;
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (d.Name == definitions[j].Name)
                {
                    return false;
                }
            }
        }
        m_Definitions.assign(definitions.begin(), definitions.end());
        m_Values.resize(definitions.size());
        for (size_t i = 0; i < definitions.size(); ++i)
        {
            m_Values[i] = definitions[i].Default;
        }
        ConsumeTriggers();
        return true;
    }
    AnimParamHandle AnimParamSet::Find(Identity name) const
    {
        for (size_t i = 0; i < m_Definitions.size(); ++i)
        {
            if (m_Definitions[i].Name == name)
            {
                return AnimParamHandle(i);
            }
        }
        return InvalidAnimParam;
    }
    bool AnimParamSet::IsType(AnimParamHandle h, AnimParamType type) const
    {
        return h < m_Definitions.size() && m_Definitions[h].Type == type;
    }
    bool AnimParamSet::SetFloat(AnimParamHandle h, float value)
    {
        if (!IsType(h, AnimParamType::Float) || !std::isfinite(value))
        {
            return false;
        }
        m_Values[h].Float = value;
        return true;
    }
    bool AnimParamSet::SetInt(AnimParamHandle h, int32_t value)
    {
        if (!IsType(h, AnimParamType::Int))
        {
            return false;
        }
        m_Values[h].Int = value;
        return true;
    }
    bool AnimParamSet::SetBool(AnimParamHandle h, bool value)
    {
        if (!IsType(h, AnimParamType::Bool))
        {
            return false;
        }
        m_Values[h].Bool = value;
        return true;
    }
    bool AnimParamSet::SetTrigger(AnimParamHandle h)
    {
        if (!IsType(h, AnimParamType::Trigger))
        {
            return false;
        }
        m_Values[h].Bool = true;
        return true;
    }
    bool AnimParamSet::ResetTrigger(AnimParamHandle h)
    {
        if (!IsType(h, AnimParamType::Trigger))
        {
            return false;
        }
        m_Values[h].Bool = false;
        return true;
    }
    void AnimParamSet::ConsumeTriggers()
    {
        for (size_t i = 0; i < m_Definitions.size(); ++i)
        {
            if (m_Definitions[i].Type == AnimParamType::Trigger)
            {
                m_Values[i].Bool = false;
            }
        }
    }
    const AnimParamValue* AnimParamSet::Get(AnimParamHandle h) const
    {
        return h < m_Values.size() ? &m_Values[h] : nullptr;
    }
    const AnimParamDefinition* AnimParamSet::Definition(AnimParamHandle h) const
    {
        return h < m_Definitions.size() ? &m_Definitions[h] : nullptr;
    }
    bool AnimParamSet::ApplyDriveSignals(const AnimDriveSignals& s)
    {
        const float values[] = {s.Speed,          s.Velocity.x,     s.Velocity.y,
                                s.Velocity.z,     s.Acceleration.x, s.Acceleration.y,
                                s.Acceleration.z, s.TurnRate,       0,
                                s.GroundNormal.x, s.GroundNormal.y, s.GroundNormal.z};
        for (float value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        for (size_t i = 0; i < m_Definitions.size(); ++i)
        {
            const auto binding = m_Definitions[i].Binding;
            if (binding == AnimSignalBinding::Grounded)
            {
                m_Values[i].Bool = s.bGrounded;
            }
            else if (binding != AnimSignalBinding::None)
            {
                m_Values[i].Float = values[uint8_t(binding) - 1];
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Animation
