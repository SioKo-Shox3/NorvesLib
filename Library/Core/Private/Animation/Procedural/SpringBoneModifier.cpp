#include "Animation/Procedural/SpringBoneModifier.h"

#include <utility>

namespace NorvesLib::Core::Animation::Procedural
{
    SpringBoneModifier::SpringBoneModifier(SpringBoneSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t SpringBoneModifier::Order() const
    {
        return PoseModifierDefaultOrder::SpringBone;
    }

    bool SpringBoneModifier::Apply(LocalPose&, PoseModifierContext&)
    {
        return true;
    }

    void SpringBoneModifier::RequestReset()
    {
    }

    void SpringBoneModifier::SetWindAcceleration(const Math::Vector3&)
    {
    }

    float SpringBoneModifier::GetLastDroppedSeconds() const
    {
        return 0;
    }

    const SpringBoneSettings& SpringBoneModifier::GetSettings() const
    {
        return m_Settings;
    }

    bool SpringBoneModifier::TryGetParticleWorldPosition(size_t, size_t, Math::Vector3&) const
    {
        return false;
    }

    bool CreateSpringBoneModifier(const PoseModifierCreateArgs&, Container::TSharedPtr<IPoseModifier>&,
                                  PoseModifierConfigReport&)
    {
        return false;
    }
} // namespace NorvesLib::Core::Animation::Procedural
