#include "Animation/AnimGraphInstance.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Animation
{
    uint32_t AnimGraphInstance::FindSyncGroup(Identity name) const
    {
        if (m_Graph)
            for (uint32_t i = 0; i < m_Graph->SyncGroups.size(); ++i)
                if (m_Graph->SyncGroups[i].Name == name)
                    return i;
        return InvalidAnimNode;
    }
    bool AnimGraphInstance::RefreshSyncMaps()
    {
        bool changed = false;
        for (size_t i = 0; i < m_Graph->Nodes.size(); ++i)
        {
            const auto& n = m_Graph->Nodes[i];
            if (n.SyncGroupIndex != InvalidAnimNode &&
                m_SyncClips[i].Revision != m_Graph->Clips[n.Clip]->GetMetadataRevision())
                changed = true;
        }
        if (!changed)
            return true;
        auto candidate = m_SyncClips;
        for (size_t i = 0; i < m_Graph->Nodes.size(); ++i)
        {
            const auto& n = m_Graph->Nodes[i];
            if (n.SyncGroupIndex == InvalidAnimNode)
                continue;
            const auto& clip = *m_Graph->Clips[n.Clip];
            if (candidate[i].Revision == clip.GetMetadataRevision())
                continue;
            if (!BuildAnimSyncMap(clip.GetClipMetadata(), clip.GetClip().DurationSeconds,
                                  m_Graph->SyncGroups[n.SyncGroupIndex].Markers, candidate[i].Map))
                return false;
            candidate[i].Revision = clip.GetMetadataRevision();
        }
        m_Traversals.clear();
        m_SyncClips = std::move(candidate);
        for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
            SeedSyncNode(i);
        return true;
    }
    void AnimGraphInstance::SeedSyncNode(uint32_t node)
    {
        const auto& n = m_Graph->Nodes[node];
        if (n.SyncGroupIndex == InvalidAnimNode)
            return;
        const auto& group = m_SyncGroups[n.SyncGroupIndex];
        if (group.bInitialized)
            m_Runtime[node].Time = m_SyncClips[node].Map.PhaseToTime(group.Phase + m_SyncOffsets[node]);
    }
    bool AnimGraphInstance::AdvanceSyncGroups(float dt)
    {
        for (uint32_t index = 0; index < m_SyncGroups.size(); ++index)
        {
            auto& group = m_SyncGroups[index];
            const auto& definition = m_Graph->SyncGroups[index];
            uint32_t leader = InvalidAnimNode;
            float weight = 0;
            for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
            {
                const auto& n = m_Graph->Nodes[i];
                if (n.SyncGroupIndex != index || m_Weights[i] <= 0)
                    continue;
                if (leader == InvalidAnimNode || m_Weights[i] > weight || (m_Weights[i] == weight && i == group.Leader))
                {
                    leader = i;
                    weight = m_Weights[i];
                }
            }
            if (leader == InvalidAnimNode)
                continue;
            const auto& node = m_Graph->Nodes[leader];
            const auto& map = m_SyncClips[leader].Map;
            if (!group.bInitialized)
            {
                group.Phase = map.TimeToPhase(m_Runtime[leader].Time) - m_SyncOffsets[leader];
                group.bInitialized = true;
            }
            group.Leader = leader;
            group.StrideRate = 1;
            if (definition.bStrideEnabled)
            {
                group.StrideRate = std::clamp(1.f, definition.MinimumStrideRate, definition.MaximumStrideRate);
                double distance = 0, totalWeight = 0;
                for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
                {
                    const auto& n = m_Graph->Nodes[i];
                    if (n.SyncGroupIndex != index || m_Weights[i] <= 0)
                        continue;
                    distance += double(m_Weights[i]) * GetNominalSpeed(n.Clip) * m_SyncClips[i].Map.Length;
                    totalWeight += m_Weights[i];
                }
                const double baseSpeed =
                    totalWeight > 0 ? distance / totalWeight * std::fabs(node.PlaybackRate) / map.Length : 0;
                const double speed = definition.DriveSpeed.Read(m_Parameters);
                if (!std::isfinite(speed))
                    return false;
                if (baseSpeed > 1e-8)
                    group.StrideRate =
                        float(std::clamp(std::max(0.0, speed) / baseSpeed, double(definition.MinimumStrideRate),
                                         double(definition.MaximumStrideRate)));
            }
            const double previous = map.PhaseToTime(group.Phase + m_SyncOffsets[leader]),
                         current = previous + double(dt) * node.PlaybackRate * group.StrideRate;
            // 時間が進まない時は往復変換の丸めで位相を動かさず、停止中のpoint再発火を防ぐ。
            const double phase = current == previous ? group.Phase : map.TimeToPhase(current) - m_SyncOffsets[leader];
            if (!std::isfinite(current) || !std::isfinite(phase) || std::fabs(phase) > 4503599627370496.0)
                return false;
            for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
            {
                const auto& n = m_Graph->Nodes[i];
                if (n.SyncGroupIndex != index || m_Weights[i] <= 0)
                    continue;
                const auto& local = m_SyncClips[i].Map;
                const double from = local.PhaseToTime(group.Phase + m_SyncOffsets[i]),
                             to = local.PhaseToTime(phase + m_SyncOffsets[i]);
                m_Runtime[i].Time = to;
                AnimClipTraversal traversal{
                    i, n.Clip, from, to, m_Weights[i], true, n.SyncGroup, node.PlaybackRate < 0};
                traversal.Timing = {&local, &map, previous, current, m_SyncOffsets[i], m_SyncOffsets[leader]};
                m_Traversals.push_back(traversal);
            }
            group.Phase = phase;
            m_SyncPhases[index] = std::min(float(phase - std::floor(phase)), std::nextafter(1.f, 0.f));
        }
        return true;
    }
} // namespace NorvesLib::Core::Animation
