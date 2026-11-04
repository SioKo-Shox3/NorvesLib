#include "Animation/Procedural/FollowThroughModifier.h"

#include <utility>

namespace NorvesLib::Core::Animation::Procedural
{
    FollowThroughModifier::FollowThroughModifier(FollowThroughSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t FollowThroughModifier::Order() const
    {
        return PoseModifierDefaultOrder::FollowThrough;
    }

    bool FollowThroughModifier::Apply(LocalPose&, PoseModifierContext&)
    {
        return true;
    }

    void FollowThroughModifier::RequestReset()
    {
    }

    float FollowThroughModifier::GetLastDroppedSeconds() const
    {
        return 0;
    }

    float FollowThroughModifier::GetLastMassScale() const
    {
        return 0;
    }

    const FollowThroughSettings& FollowThroughModifier::GetSettings() const
    {
        return m_Settings;
    }

    bool CreateFollowThroughModifier(const PoseModifierCreateArgs&, Container::TSharedPtr<IPoseModifier>&,
                                     PoseModifierConfigReport&)
    {
        return false;
    }
} // namespace NorvesLib::Core::Animation::Procedural
