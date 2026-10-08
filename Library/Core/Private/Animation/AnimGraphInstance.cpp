#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletonResource.h"
#include "Asset/CookedSkeletonV1.h"
#include "Asset/RigSplitWire.h"
#include "Resource/SkinnedMeshResource.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        float Unit(float x)
        {
            return std::clamp(x, 0.f, 1.f);
        }
        float Alpha(float elapsed, float duration, AnimTransitionCurve curve)
        {
            const float t = duration <= 0 ? 1 : Unit(elapsed / duration);
            return curve == AnimTransitionCurve::Smoothstep ? t * t * (3 - 2 * t) : t;
        }
        size_t AxisInterval(const Container::VariableArray<float>& axis, float x, float& alpha)
        {
            if (axis.size() == 1 || x <= axis.front())
            {
                alpha = 0;
                return 0;
            }
            if (x >= axis.back())
            {
                alpha = 1;
                return axis.size() - 2;
            }
            auto next = std::upper_bound(axis.begin(), axis.end(), x);
            const size_t first = size_t(next - axis.begin()) - 1;
            alpha = float((double(x) - axis[first]) / (double(axis[first + 1]) - axis[first]));
            return first;
        }
        template <class T> bool Compare(T a, T b, AnimCompare op)
        {
            switch (op)
            {
            case AnimCompare::Equal:
                return a == b;
            case AnimCompare::NotEqual:
                return a != b;
            case AnimCompare::Less:
                return a < b;
            case AnimCompare::LessEqual:
                return a <= b;
            case AnimCompare::Greater:
                return a > b;
            case AnimCompare::GreaterEqual:
                return a >= b;
            }
            return false;
        }
    } // namespace
    AnimGraphInstance::~AnimGraphInstance()
    {
        Reset();
    }
    AnimGraphInstance::AnimGraphInstance(AnimGraphInstance&& other)
    {
        if (!other.m_Events.IsDispatching())
            Swap(other);
    }
    AnimGraphInstance& AnimGraphInstance::operator=(AnimGraphInstance&& other)
    {
        // 配送中のインスタンスはcallbackが参照しているため移動しない。
        if (this != &other && !m_Events.IsDispatching() && !other.m_Events.IsDispatching())
        {
            Reset();
            Swap(other);
        }
        return *this;
    }
    void AnimGraphInstance::Swap(AnimGraphInstance& other)
    {
        using std::swap;
        swap(m_RootClips, other.m_RootClips);
        swap(m_NodeMotion, other.m_NodeMotion);
        swap(m_MotionScratch, other.m_MotionScratch);
        swap(m_MotionModels, other.m_MotionModels);
        swap(m_PendingRootMotion, other.m_PendingRootMotion);
        swap(m_Graph, other.m_Graph);
        swap(m_Parameters, other.m_Parameters);
        swap(m_Events, other.m_Events);
        swap(m_Contexts, other.m_Contexts);
        swap(m_Runtime, other.m_Runtime);
        swap(m_Poses, other.m_Poses);
        swap(m_Reference, other.m_Reference);
        swap(m_Weights, other.m_Weights);
        swap(m_EvaluationWeights, other.m_EvaluationWeights);
        swap(m_Durations, other.m_Durations);
        swap(m_Traversals, other.m_Traversals);
        swap(m_ResetVisited, other.m_ResetVisited);
        swap(m_Output, other.m_Output);
        swap(m_Skeleton, other.m_Skeleton);
        swap(m_Mesh, other.m_Mesh);
        swap(m_Transform, other.m_Transform);
        swap(m_bValid, other.m_bValid);
    }
    void AnimGraphInstance::Reset()
    {
        if (m_Events.IsDispatching())
            return;
        m_Events.Reset();
        m_bValid = false;
        m_Graph.reset();
        m_Contexts.clear();
        m_RootClips.clear();
        m_NodeMotion.clear();
        m_MotionModels.clear();
        m_MotionScratch = {};
        m_PendingRootMotion = {};
        m_Runtime.clear();
        m_Poses.clear();
        m_Reference.clear();
        m_Weights.clear();
        m_EvaluationWeights.clear();
        m_Durations.clear();
        m_Traversals.clear();
        m_ResetVisited.clear();
        m_Output.clear();
        m_Skeleton = nullptr;
        m_Mesh = nullptr;
        m_Parameters = AnimParamSet{};
    }
    bool AnimGraphInstance::Initialize(const AnimGraphResource& graph, const SkeletonResource& skeleton,
                                       const SkinnedMeshResource& mesh, const Math::Matrix4x4& transform)
    {
        if (m_Events.IsDispatching())
            return false;
        Reset();
        const auto& data = graph.GetData();
        if (!graph.IsLoaded() || !data || data->Nodes.empty() || data->Clips.empty() ||
            data->Parents != skeleton.GetPoseRuntime().Parents || !m_Parameters.Initialize(data->Parameters))
        {
            return false;
        }
        // graphは名前と親添字で同一骨格へ束縛する。名前が変わった別骨格を同じ数だけで通さない。
        for (size_t i = 0; i < data->JointNames.size(); ++i)
        {
            Identity actual;
            if (skeleton.IsSplitV1())
            {
                Container::String name;
                if (!Skeletal::SplitWire::NativeName(skeleton.GetSplitSkeleton()->Topology.Joints[i].Name, name))
                {
                    return false;
                }
                actual = Identity(name);
            }
            else
            {
                actual = Identity(skeleton.GetJoints()[i].Name);
            }
            if (data->JointNames[i] != actual)
            {
                return false;
            }
        }
        m_Graph = data;
        m_Skeleton = &skeleton;
        m_Mesh = &mesh;
        m_Transform = transform;
        m_Contexts.resize(data->Clips.size());
        for (size_t i = 0; i < data->Clips.size(); ++i)
        {
            if (!SkeletalPoseBuilder::Prepare(skeleton, *data->Clips[i], mesh, transform, m_Contexts[i]))
            {
                Reset();
                return false;
            }
        }
        const size_t count = data->Nodes.size(), joints = data->Parents.size();
        m_Runtime.resize(count);
        m_Poses.resize(count);
        m_Reference.resize(count);
        m_Weights.resize(count, 0);
        m_EvaluationWeights.resize(count, 0);
        m_Durations.resize(count, 0);
        m_ResetVisited.resize(count, 0);
        m_Traversals.reserve(count);
        m_Output.resize(joints);
        for (size_t i = 0; i < count; ++i)
        {
            auto& rt = m_Runtime[i];
            rt.Current = data->Nodes[i].InitialState;
            if (data->Nodes[i].Kind == AnimNodeKind::Clip && data->Nodes[i].bLoop &&
                data->Clips[data->Nodes[i].Clip]->GetMetadata().Loop.bEnabled)
                rt.Time = data->Clips[data->Nodes[i].Clip]->GetMetadata().Loop.Start;
            if (data->Nodes[i].Kind == AnimNodeKind::Clip && data->Nodes[i].PlaybackRate < 0 && !data->Nodes[i].bLoop)
            {
                rt.Time = data->Clips[data->Nodes[i].Clip]->GetClip().DurationSeconds;
            }
            rt.Edges.resize(data->Nodes[i].Children.size(), 0);
            rt.Saved.resize(joints);
            rt.SavedReference.resize(joints);
            m_Poses[i].resize(joints);
            m_Reference[i].resize(joints);
            Edges(uint32_t(i));
        }
        for (uint32_t i : data->EvaluationOrder)
        {
            const auto& n = data->Nodes[i];
            if (n.Kind == AnimNodeKind::Clip)
            {
                m_Durations[i] = n.PlaybackRate == 0 ? std::numeric_limits<double>::infinity()
                                                     : double(data->Clips[n.Clip]->GetClip().DurationSeconds) /
                                                           std::fabs(double(n.PlaybackRate));
            }
            else
            {
                for (uint32_t child : n.Children)
                {
                    m_Durations[i] = std::max(m_Durations[i], m_Durations[child]);
                }
            }
        }
        m_RootClips.resize(data->Clips.size());
        m_NodeMotion.resize(count);
        m_MotionScratch.Resize(joints);
        m_MotionModels.resize(joints);
        if (!RefreshRootMetadata(true))
        {
            Reset();
            return false;
        }
        m_bValid = true;
        if (!EvaluateNodes(true))
        {
            Reset();
            return false;
        }
        m_Output = m_Poses[data->Root];
        return true;
    }
    bool AnimGraphInstance::SetMeshTransform(const Math::Matrix4x4& transform)
    {
        if (!ResourcesCurrent())
        {
            return false;
        }
        if (std::memcmp(m_Transform.values, transform.values, sizeof(transform.values)) == 0)
        {
            return true;
        }
        // 割込snapshotは旧Mのローカル空間に属するため、変更時は明示再Initializeで破棄する。
        for (const auto& runtime : m_Runtime)
        {
            if (runtime.bSnapshot)
            {
                return false;
            }
        }
        Container::VariableArray<SkeletalPoseContext> contexts(m_Contexts.size());
        for (size_t i = 0; i < contexts.size(); ++i)
        {
            if (!SkeletalPoseBuilder::Prepare(*m_Skeleton, *m_Graph->Clips[i], *m_Mesh, transform, contexts[i]))
            {
                return false;
            }
        }
        m_Contexts = std::move(contexts);
        m_Transform = transform;
        return RefreshRootMetadata(true) && EvaluateNodes(true);
    }
    bool AnimGraphInstance::ResourcesCurrent() const
    {
        if (!m_bValid || !m_Graph || !m_Skeleton || !m_Mesh)
        {
            return false;
        }
        for (size_t i = 0; i < m_Contexts.size(); ++i)
        {
            if (!SkeletalPoseBuilder::IsPreparedFor(m_Contexts[i], *m_Skeleton, *m_Graph->Clips[i], *m_Mesh,
                                                    m_Transform))
            {
                return false;
            }
        }
        return true;
    }
    void AnimGraphInstance::Edges(uint32_t index)
    {
        const auto& n = m_Graph->Nodes[index];
        auto& rt = m_Runtime[index];
        auto& w = rt.Edges;
        std::fill(w.begin(), w.end(), 0);
        switch (n.Kind)
        {
        case AnimNodeKind::Clip:
            break;
        case AnimNodeKind::Blend2:
            w[1] = Unit(n.Weight.Read(m_Parameters));
            w[0] = 1 - w[1];
            break;
        case AnimNodeKind::BlendSpace1D:
        {
            float a = 0;
            const auto first = AxisInterval(n.AxisX, n.X.Read(m_Parameters), a);
            w[first] = 1 - a;
            if (first + 1 < w.size())
            {
                w[first + 1] = a;
            }
            break;
        }
        case AnimNodeKind::BlendSpace2D:
        {
            float x = 0, y = 0;
            const auto ix = AxisInterval(n.AxisX, n.X.Read(m_Parameters), x),
                       iy = AxisInterval(n.AxisY, n.Y.Read(m_Parameters), y);
            const size_t a = iy * n.AxisX.size() + ix, b = a + n.AxisX.size();
            w[a] = (1 - x) * (1 - y);
            w[a + 1] = x * (1 - y);
            w[b] = (1 - x) * y;
            w[b + 1] = x * y;
            break;
        }
        case AnimNodeKind::Layered:
            w[0] = 1;
            for (size_t i = 0; i < n.Layers.size(); ++i)
            {
                w[i + 1] = Unit(n.Layers[i].Weight.Read(m_Parameters));
            }
            break;
        case AnimNodeKind::Select:
        {
            const auto* p = m_Parameters.Get(n.Select);
            const auto* d = m_Parameters.Definition(n.Select);
            const int32_t selected = d->Type == AnimParamType::Bool ? int32_t(p->Bool) : p->Int;
            w[size_t(std::clamp(selected, 0, int32_t(w.size() - 1)))] = 1;
            break;
        }
        case AnimNodeKind::StateMachine:
            if (rt.Next == InvalidAnimNode)
            {
                w[rt.Current] = 1;
            }
            else
            {
                const float a = Alpha(rt.Elapsed, rt.Duration, rt.Curve);
                if (!rt.bSnapshot)
                {
                    w[rt.Current] = 1 - a;
                }
                w[rt.Next] += a;
            }
            break;
        }
    }
    bool AnimGraphInstance::Matches(const AnimTransition& t, uint32_t index, uint32_t state) const
    {
        const auto& n = m_Graph->Nodes[index];
        const auto& rt = m_Runtime[index];
        if (t.From != InvalidAnimNode && t.From != state)
        {
            return false;
        }
        if (t.To == state || t.To == rt.Next)
        {
            return false;
        }
        const double duration = m_Durations[n.States[state].Node];
        if (t.ExitTime >= 0 &&
            (duration > 0 ? (state == rt.Next ? rt.NextStateTime : rt.StateTime) / duration : 1) < t.ExitTime)
        {
            return false;
        }
        for (const auto& c : t.Conditions)
        {
            const auto* p = m_Parameters.Get(c.Parameter);
            const auto type = m_Parameters.Definition(c.Parameter)->Type;
            const bool matched = type == AnimParamType::Float ? Compare(p->Float, c.Value.Float, c.Compare)
                                 : type == AnimParamType::Int ? Compare(p->Int, c.Value.Int, c.Compare)
                                                              : Compare(p->Bool, c.Value.Bool, c.Compare);
            if (!matched)
            {
                return false;
            }
        }
        return true;
    }
    void AnimGraphInstance::ResetSubgraph(uint32_t root)
    {
        std::fill(m_ResetVisited.begin(), m_ResetVisited.end(), 0);
        auto visit = [&](auto&& self, uint32_t i) -> void
        {
            if (m_ResetVisited[i])
            {
                return;
            }
            m_ResetVisited[i] = 1;
            // DAGで共有され、現在出力に寄与するnodeの時刻を巻き戻さない。
            if (m_EvaluationWeights[i] > 0)
            {
                return;
            }
            auto& rt = m_Runtime[i];
            const auto& node = m_Graph->Nodes[i];
            rt.Time = node.Kind == AnimNodeKind::Clip && node.PlaybackRate < 0 && !node.bLoop
                          ? m_Graph->Clips[node.Clip]->GetClip().DurationSeconds
                          : 0;
            if (node.Kind == AnimNodeKind::Clip && node.bLoop && m_Graph->Clips[node.Clip]->GetMetadata().Loop.bEnabled)
                rt.Time = m_Graph->Clips[node.Clip]->GetMetadata().Loop.Start;
            rt.StateTime = 0;
            rt.NextStateTime = 0;
            rt.Current = m_Graph->Nodes[i].InitialState;
            rt.Next = InvalidAnimNode;
            rt.Elapsed = 0;
            rt.bSnapshot = false;
            for (uint32_t child : m_Graph->Nodes[i].Children)
            {
                self(self, child);
            }
            Edges(i);
        };
        visit(visit, root);
    }
    bool AnimGraphInstance::StartTransition(uint32_t index, uint32_t target, float duration, AnimTransitionCurve curve,
                                            AnimInterrupt interrupt)
    {
        auto& rt = m_Runtime[index];
        const auto& n = m_Graph->Nodes[index];
        if (target >= n.States.size() || !std::isfinite(duration) || duration < 0)
        {
            return false;
        }
        // 現在graphの寄与を固定し、共有nodeの時刻と割込時点の姿勢を保つ。
        // 内部作業poseだけを更新し、公開済みm_Outputは変更しない。
        if (!EvaluateNodes(false, index))
        {
            return false;
        }
        if (rt.Next != InvalidAnimNode)
        {
            rt.Saved = m_Poses[index];
            rt.SavedReference = m_Reference[index];
            rt.bSnapshot = true;
        }
        else
        {
            rt.bSnapshot = false;
        }
        ResetSubgraph(n.States[target].Node);
        rt.Next = target;
        rt.Elapsed = 0;
        rt.Duration = duration;
        rt.Curve = curve;
        rt.Interrupt = interrupt;
        rt.NextStateTime = 0;
        if (duration == 0)
        {
            rt.Current = target;
            rt.Next = InvalidAnimNode;
            rt.StateTime = 0;
            rt.bSnapshot = false;
        }
        Edges(index);
        return true;
    }
    bool AnimGraphInstance::Update(float dt)
    {
        if (m_Events.IsDispatching() || !ResourcesCurrent() || !std::isfinite(dt) || dt < 0 || !RefreshRootMetadata())
        {
            return false;
        }
        m_Traversals.clear();
        std::fill(m_Weights.begin(), m_Weights.end(), 0);
        m_Weights[m_Graph->Root] = 1;
        for (size_t order = m_Graph->EvaluationOrder.size(); order > 0; --order)
        {
            const uint32_t index = m_Graph->EvaluationOrder[order - 1];
            const auto& n = m_Graph->Nodes[index];
            auto& rt = m_Runtime[index];
            if (m_Weights[index] <= 0)
            {
                continue;
            }
            if (n.Kind == AnimNodeKind::StateMachine)
            {
                // 連鎖は0秒遷移も含めて4回まで。Triggerは全機械の後でまとめて消費する。
                for (unsigned chain = 0; chain < 4; ++chain)
                {
                    const AnimTransition* selected = nullptr;
                    for (const auto& t : n.Transitions)
                    {
                        const bool transitioning = rt.Next != InvalidAnimNode;
                        if (transitioning && rt.Interrupt == AnimInterrupt::None)
                        {
                            continue;
                        }
                        const bool current = !transitioning || rt.Interrupt == AnimInterrupt::Current ||
                                             rt.Interrupt == AnimInterrupt::Either;
                        const bool next = transitioning && (rt.Interrupt == AnimInterrupt::Next ||
                                                            rt.Interrupt == AnimInterrupt::Either);
                        if ((current && Matches(t, index, rt.Current)) || (next && Matches(t, index, rt.Next)))
                        {
                            selected = &t;
                            break;
                        }
                    }
                    if (!selected)
                    {
                        break;
                    }
                    if (!StartTransition(index, selected->To, selected->Duration, selected->Curve, selected->Interrupt))
                    {
                        return false;
                    }
                    if (selected->Duration > 0)
                    {
                        break;
                    }
                }
                rt.StateTime += dt;
                if (rt.Next != InvalidAnimNode)
                {
                    rt.NextStateTime += dt;
                    rt.Elapsed = std::min(rt.Duration, rt.Elapsed + dt);
                    if (rt.Elapsed >= rt.Duration)
                    {
                        rt.Current = rt.Next;
                        rt.StateTime = rt.NextStateTime;
                        rt.Next = InvalidAnimNode;
                        rt.bSnapshot = false;
                    }
                }
            }
            Edges(index);
            for (size_t edge = 0; edge < n.Children.size(); ++edge)
            {
                m_Weights[n.Children[edge]] += m_Weights[index] * rt.Edges[edge];
            }
        }
        for (uint32_t index : m_Graph->EvaluationOrder)
        {
            const auto& n = m_Graph->Nodes[index];
            auto& rt = m_Runtime[index];
            if (n.Kind != AnimNodeKind::Clip || m_Weights[index] <= 0)
            {
                continue;
            }
            const double previous = rt.Time;
            const double advanced = previous + double(dt) * n.PlaybackRate;
            if (!std::isfinite(advanced))
            {
                return false;
            }
            rt.Time = n.bLoop ? advanced
                              : std::clamp(advanced, 0.0, double(m_Graph->Clips[n.Clip]->GetClip().DurationSeconds));
            m_Traversals.push_back(
                {index, n.Clip, previous, rt.Time, m_Weights[index], n.bLoop, n.SyncGroup, n.PlaybackRate < 0});
        }
        if (!AdvanceRootMotion())
            return false;
        m_Events.Update(*m_Graph, m_Traversals, dt);
        m_Parameters.ConsumeTriggers();
        return true;
    }
    bool AnimGraphInstance::EvaluateNodes(bool initializeReference, uint32_t requiredRoot)
    {
        std::fill(m_EvaluationWeights.begin(), m_EvaluationWeights.end(), 0);
        m_EvaluationWeights[m_Graph->Root] = 1;
        if (requiredRoot < m_EvaluationWeights.size())
        {
            m_EvaluationWeights[requiredRoot] += 1;
        }
        for (size_t order = m_Graph->EvaluationOrder.size(); order > 0; --order)
        {
            const auto i = m_Graph->EvaluationOrder[order - 1];
            const auto& n = m_Graph->Nodes[i];
            for (size_t edge = 0; edge < n.Children.size(); ++edge)
            {
                m_EvaluationWeights[n.Children[edge]] += m_EvaluationWeights[i] * m_Runtime[i].Edges[edge];
            }
        }
        for (uint32_t index : m_Graph->EvaluationOrder)
        {
            const auto& n = m_Graph->Nodes[index];
            const auto& rt = m_Runtime[index];
            if (!initializeReference && m_EvaluationWeights[index] <= 0)
            {
                continue;
            }
            if (n.Kind == AnimNodeKind::Clip)
            {
                if (initializeReference && !SkeletalPoseBuilder::SampleClipToLocalPose(
                                               m_Contexts[n.Clip], *m_Graph->Clips[n.Clip], 0, m_Reference[index]))
                {
                    return false;
                }
                double time = rt.Time;
                const double duration = m_Graph->Clips[n.Clip]->GetClip().DurationSeconds;
                const auto& loop = m_Graph->Clips[n.Clip]->GetMetadata().Loop;
                const double start = loop.bEnabled ? loop.Start : 0, end = loop.bEnabled ? loop.End : duration;
                if (n.bLoop && end > start)
                {
                    time = start + std::fmod(time - start, end - start);
                    if (time < start)
                        time += end - start;
                }
                if (!SkeletalPoseBuilder::SampleClipToLocalPose(m_Contexts[n.Clip], *m_Graph->Clips[n.Clip],
                                                                float(time), m_Poses[index]))
                {
                    return false;
                }
                if (!RemoveRootMotion(n.Clip, m_Poses[index]))
                    return false;
                continue;
            }
            // 先頭frame自体は一度だけ読む。複合nodeの基準は現edgeで合成し、選択変更を加算差分にしない。
            auto compose = [&](const Container::VariableArray<LocalPose>& inputs, LocalPose& pose,
                               bool reference) -> bool
            {
                if (n.Kind == AnimNodeKind::Layered)
                {
                    pose = inputs[n.Children[0]];
                    for (size_t i = 0; i < n.Layers.size(); ++i)
                    {
                        const auto& layer = n.Layers[i];
                        const float weight = rt.Edges[i + 1];
                        if (weight <= 0)
                        {
                            continue;
                        }
                        const Container::Span<const float> mask = layer.Mask == InvalidAnimNode
                                                                      ? Container::Span<const float>{}
                                                                      : m_Graph->Masks[layer.Mask].Weights;
                        const bool ok = layer.bAdditive
                                            ? reference || PoseOps::AddInto(pose, inputs[layer.Node],
                                                                            m_Reference[layer.Node], weight, mask)
                                            : PoseOps::BlendInto(pose, inputs[layer.Node], weight, mask);
                        if (!ok)
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (n.Kind == AnimNodeKind::StateMachine && rt.Next != InvalidAnimNode && rt.bSnapshot)
                {
                    pose = reference ? rt.SavedReference : rt.Saved;
                    return PoseOps::BlendInto(pose, inputs[n.States[rt.Next].Node],
                                              Alpha(rt.Elapsed, rt.Duration, rt.Curve));
                }
                float total = 0;
                for (size_t edge = 0; edge < n.Children.size(); ++edge)
                {
                    const float weight = rt.Edges[edge];
                    if (weight <= 0)
                    {
                        continue;
                    }
                    const auto& input = inputs[n.Children[edge]];
                    if (total == 0)
                    {
                        pose = input;
                    }
                    else if (!PoseOps::BlendInto(pose, input, weight / (total + weight)))
                    {
                        return false;
                    }
                    total += weight;
                }
                return total > 0;
            };
            if (!compose(m_Reference, m_Reference[index], true) || !compose(m_Poses, m_Poses[index], false))
            {
                return false;
            }
        }
        return true;
    }
    bool AnimGraphInstance::Evaluate()
    {
        if (!ResourcesCurrent() || !RefreshRootMetadata() || !EvaluateNodes(false))
        {
            return false;
        }
        m_Output = m_Poses[m_Graph->Root];
        return true;
    }
    const LocalPose& AnimGraphInstance::GetLocalPose() const
    {
        return m_Output;
    }
    bool AnimGraphInstance::BuildJointModelMatrices(const LocalPose& pose, PoseScratch& scratch,
                                                    Container::VariableArray<Math::Matrix4x4>& out) const
    {
        if (!ResourcesCurrent())
        {
            out.clear();
            return false;
        }
        return SkeletalPoseBuilder::BuildJointModelMatrices(m_Contexts.front(), pose, scratch, out);
    }
    bool AnimGraphInstance::BuildPose(const LocalPose& pose, PoseScratch& scratch, SkeletalPoseSnapshot& out) const
    {
        if (!ResourcesCurrent())
        {
            out.Clear();
            return false;
        }
        return SkeletalPoseBuilder::BuildPose(m_Contexts.front(), pose, scratch, out);
    }
    bool AnimGraphInstance::RequestState(Identity machine, Identity state, float seconds)
    {
        if (!ResourcesCurrent() || !std::isfinite(seconds) || seconds < 0 || !RefreshRootMetadata())
        {
            return false;
        }
        for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
        {
            const auto& n = m_Graph->Nodes[i];
            if (n.Kind != AnimNodeKind::StateMachine || n.Name != machine)
            {
                continue;
            }
            for (uint32_t s = 0; s < n.States.size(); ++s)
            {
                if (n.States[s].Name == state)
                {
                    return StartTransition(i, s, seconds, AnimTransitionCurve::Linear, AnimInterrupt::Either);
                }
            }
        }
        return false;
    }
    bool AnimGraphInstance::GetState(Identity machine, AnimStateStatus& out) const
    {
        if (!ResourcesCurrent())
        {
            return false;
        }
        for (uint32_t i = 0; i < m_Graph->Nodes.size(); ++i)
        {
            const auto& n = m_Graph->Nodes[i];
            if (n.Kind != AnimNodeKind::StateMachine || n.Name != machine)
            {
                continue;
            }
            const auto& rt = m_Runtime[i];
            const double duration = m_Durations[n.States[rt.Current].Node];
            out = {};
            out.Current = n.States[rt.Current].Name;
            out.bTransitioning = rt.Next != InvalidAnimNode;
            if (out.bTransitioning)
            {
                out.Next = n.States[rt.Next].Name;
            }
            out.NormalizedTime = duration > 0 ? rt.StateTime / duration : 1;
            out.bReachedEnd = out.NormalizedTime >= 1;
            out.Transition = out.bTransitioning ? Alpha(rt.Elapsed, rt.Duration, rt.Curve) : 0;
            return true;
        }
        return false;
    }
} // namespace NorvesLib::Core::Animation
