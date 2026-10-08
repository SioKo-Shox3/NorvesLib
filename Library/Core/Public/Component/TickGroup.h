#pragma once

#include <cstdint>

namespace NorvesLib::Core::Component
{
    // フレーム全体の粗い順序だけを定義する。個別の依存関係はDelegateで連携する。
    enum class ETickGroup : uint8_t
    {
        Input,
        Movement,
        Default,
        Animation,
        PoseFinalize,
        PostPhysics,
        Camera,
        PreRender,
        Count
    };

    using TickGroupMask = uint16_t;
    inline constexpr uint8_t TickGroupCount = static_cast<uint8_t>(ETickGroup::Count);
    inline constexpr TickGroupMask AllTickGroups = (TickGroupMask{1} << TickGroupCount) - 1;

    constexpr bool IsValidTickGroup(ETickGroup group)
    {
        return static_cast<uint8_t>(group) < TickGroupCount;
    }

    constexpr TickGroupMask TickGroupBit(ETickGroup group)
    {
        return IsValidTickGroup(group) ? static_cast<TickGroupMask>(TickGroupMask{1} << static_cast<uint8_t>(group)) : 0;
    }

    constexpr bool IsPostPhysicsTickGroup(ETickGroup group)
    {
        return IsValidTickGroup(group) && group >= ETickGroup::PostPhysics;
    }

    constexpr const char* GetTickGroupName(ETickGroup group)
    {
        switch (group)
        {
        case ETickGroup::Input: return "Input";
        case ETickGroup::Movement: return "Movement";
        case ETickGroup::Default: return "Default";
        case ETickGroup::Animation: return "Animation";
        case ETickGroup::PoseFinalize: return "PoseFinalize";
        case ETickGroup::PostPhysics: return "PostPhysics";
        case ETickGroup::Camera: return "Camera";
        case ETickGroup::PreRender: return "PreRender";
        default: return "Invalid";
        }
    }

    /**
     * @brief Componentの型が持つ更新設定。シーン保存・リフレクションの対象にはしない。
     * 主群変更はmaskを1群へ戻す。複数群を使う型は、その後にmaskを設定する。
     * フレーム途中の変更は、次の収集で実行順へ反映する。
     */
    class TickGroupConfiguration
    {
    public:
        constexpr ETickGroup GetGroup() const
        {
            return m_Group;
        }
        constexpr int16_t GetPriority() const
        {
            return m_Priority;
        }
        constexpr TickGroupMask GetMask() const
        {
            return m_Mask;
        }

        constexpr bool SetGroup(ETickGroup group)
        {
            if (!IsValidTickGroup(group))
            {
                return false;
            }
            m_Group = group;
            m_Mask = TickGroupBit(group);
            return true;
        }

        constexpr void SetPriority(int16_t priority)
        {
            m_Priority = priority;
        }

        constexpr bool SetMask(TickGroupMask mask)
        {
            if ((mask & ~AllTickGroups) != 0 || (mask & TickGroupBit(m_Group)) == 0)
            {
                return false;
            }
            m_Mask = mask;
            return true;
        }

        constexpr bool ParticipatesIn(ETickGroup group) const
        {
            return (m_Mask & TickGroupBit(group)) != 0;
        }

    private:
        ETickGroup m_Group = ETickGroup::Default;
        int16_t m_Priority = 0;
        TickGroupMask m_Mask = TickGroupBit(ETickGroup::Default);
    };
} // namespace NorvesLib::Core::Component
