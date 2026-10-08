#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Entity.h"
#include "Animation/SkeletonResource.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(AnimatorComponent, Component)
    AnimatorComponent::AnimatorComponent()
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }
    AnimatorComponent::AnimatorComponent(const FieldInitializer* f) : Component(f)
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }
    AnimatorComponent::AnimatorComponent(const IUnknown* source) : Component(source)
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }
    AnimatorComponent::~AnimatorComponent() = default;
    void AnimatorComponent::Initialize()
    {
        Component::Initialize();
    }
    SkinnedMeshComponent* AnimatorComponent::Mesh() const
    {
        return GetOwner() ? GetOwner()->GetComponent<SkinnedMeshComponent>() : nullptr;
    }
    void AnimatorComponent::Detach()
    {
        if (m_Graph)
        {
            if (auto* mesh = Mesh())
            {
                mesh->SetExternalAnimationDriven(false);
            }
        }
        m_Instance.Reset();
        m_BoundSkeleton.reset();
        m_BoundMesh.reset();
        m_BoundAsset.reset();
        m_Graph.reset();
        m_FinalPose.clear();
        m_ModifierModels.clear();
    }
    void AnimatorComponent::EndPlay()
    {
        Detach();
        ClearModifiers();
        OnEvent.Clear();
        Component::EndPlay();
    }
    void AnimatorComponent::Finalize()
    {
        Detach();
        ClearModifiers();
        OnEvent.Clear();
        Component::Finalize();
    }
    bool AnimatorComponent::SetGraph(const Container::TSharedPtr<AnimGraphResource>& graph)
    {
        if (m_bApplyingModifiers || m_Instance.Events().IsDispatching())
        {
            return false;
        }
        if (!graph)
        {
            Detach();
            return true;
        }
        auto* mesh = Mesh();
        if (!mesh || !GetOwner() || GetOwner()->GetComponent<AnimatorComponent>() != this || !graph->IsLoaded())
        {
            return false;
        }
        const auto& asset = mesh->GetSkeletalAsset();
        if (!asset || !asset->IsLoaded() || !asset->GetSkeleton() || !asset->GetMesh())
        {
            return false;
        }
        Animation::AnimGraphInstance instance;
        if (!instance.Initialize(*graph, *asset->GetSkeleton(), *asset->GetMesh(), mesh->GetMeshNodeGlobalTransform()))
        {
            return false;
        }
        m_Instance.Events().InterruptAll();
        m_Instance.Events().Dispatch();
        instance.Events().OnEvent.Add(this, &AnimatorComponent::ForwardEvent);
        m_Instance = std::move(instance);
        m_Graph = graph;
        m_BoundAsset = asset;
        m_BoundSkeleton = asset->GetSkeleton();
        m_BoundMesh = asset->GetMesh();
        const size_t count = m_Instance.GetLocalPose().size();
        m_FinalPose.resize(count);
        m_ModifierScratch.Resize(count);
        m_ModifierModels.resize(count);
        mesh->SetExternalAnimationDriven(true);
        m_LastDelta = 0;
        return true;
    }
    Animation::AnimParamHandle AnimatorComponent::FindParam(Identity name) const
    {
        return m_Instance.Parameters().Find(name);
    }
    bool AnimatorComponent::SetFloat(Animation::AnimParamHandle h, float value)
    {
        return m_Instance.Parameters().SetFloat(h, value);
    }
    bool AnimatorComponent::SetInt(Animation::AnimParamHandle h, int32_t value)
    {
        return m_Instance.Parameters().SetInt(h, value);
    }
    bool AnimatorComponent::SetBool(Animation::AnimParamHandle h, bool value)
    {
        return m_Instance.Parameters().SetBool(h, value);
    }
    bool AnimatorComponent::SetTrigger(Animation::AnimParamHandle h)
    {
        return m_Instance.Parameters().SetTrigger(h);
    }
    bool AnimatorComponent::SetDriveSignals(const Animation::AnimDriveSignals& value)
    {
        return m_Instance.Parameters().ApplyDriveSignals(value);
    }
    bool AnimatorComponent::RequestState(Identity machine, Identity state, float seconds)
    {
        return m_Instance.RequestState(machine, state, seconds);
    }
    bool AnimatorComponent::GetState(Identity machine, Animation::AnimStateStatus& out) const
    {
        return m_Instance.GetState(machine, out);
    }
    bool AnimatorComponent::AddModifier(const Container::TSharedPtr<Animation::IPoseModifier>& modifier)
    {
        if (m_bApplyingModifiers || !modifier)
        {
            return false;
        }
        for (const auto& item : m_Modifiers)
        {
            if (item == modifier)
            {
                return false;
            }
        }
        m_Modifiers.push_back(modifier);
        std::stable_sort(m_Modifiers.begin(), m_Modifiers.end(),
                         [](const auto& a, const auto& b) { return a->Order() < b->Order(); });
        return true;
    }
    void AnimatorComponent::ClearModifiers()
    {
        if (!m_bApplyingModifiers)
        {
            m_Modifiers.clear();
        }
    }
    bool AnimatorComponent::Step(float seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(m_Step + seconds))
        {
            return false;
        }
        m_Step += seconds;
        return true;
    }
    bool AnimatorComponent::UpdateAnimation(float dt)
    {
        auto* mesh = Mesh();
        if (!mesh || !m_Graph || !m_Graph->IsLoaded() || mesh->GetSkeletalAsset() != m_BoundAsset || !m_BoundAsset ||
            m_BoundAsset->GetSkeleton() != m_BoundSkeleton || m_BoundAsset->GetMesh() != m_BoundMesh ||
            m_Graph->GetData() != m_Instance.GetGraph() || !std::isfinite(dt) || dt < 0)
        {
            return false;
        }
        if (!m_Instance.SetMeshTransform(mesh->GetMeshNodeGlobalTransform()))
        {
            return false;
        }
        const float step = m_bFrozen ? m_Step : dt + m_Step;
        if (!std::isfinite(step) || !m_Instance.Update(step))
        {
            return false;
        }
        m_Step = 0;
        m_LastDelta = step;
        return true;
    }
    bool AnimatorComponent::EvaluateAnimation()
    {
        auto* mesh = Mesh();
        if (!mesh || !m_Graph || !m_Graph->IsLoaded() || m_Graph->GetData() != m_Instance.GetGraph() ||
            mesh->GetSkeletalAsset() != m_BoundAsset || !m_BoundAsset ||
            m_BoundAsset->GetSkeleton() != m_BoundSkeleton || m_BoundAsset->GetMesh() != m_BoundMesh ||
            !m_Instance.SetMeshTransform(mesh->GetMeshNodeGlobalTransform()) || !m_Instance.Evaluate())
        {
            return false;
        }
        m_FinalPose = m_Instance.GetLocalPose();
        Animation::PoseModifierContext context(m_BoundAsset->GetSkeleton()->GetPoseRuntime(), m_LastDelta,
                                               m_Instance.Parameters(), m_Instance, m_FinalPose, m_ModifierScratch,
                                               m_ModifierModels);
        m_bApplyingModifiers = true;
        bool applied = true;
        for (const auto& modifier : m_Modifiers)
        {
            context.Invalidate();
            if (!modifier->Apply(m_FinalPose, context))
            {
                applied = false;
                break;
            }
        }
        m_bApplyingModifiers = false;
        if (!applied)
        {
            return false;
        }
        return mesh->SubmitLocalPose(m_FinalPose);
    }
    void AnimatorComponent::OnTickGroup(ETickGroup group, float dt)
    {
        if (group == ETickGroup::Animation)
        {
            (void)UpdateAnimation(dt);
        }
        else if (group == ETickGroup::PoseFinalize)
        {
            if (m_bEvaluate)
                (void)EvaluateAnimation();
            m_Instance.Events().Dispatch();
        }
    }
    void AnimatorComponent::BuildDebugSnapshot(AnimatorDebugSnapshot& out) const
    {
        out.Parameters.clear();
        out.Nodes.clear();
        out.bFrozen = m_bFrozen;
        out.bReady = bool(m_Graph && m_Graph->IsLoaded());
        if (!m_Instance.GetGraph())
        {
            return;
        }
        const auto& params = m_Instance.Parameters();
        for (uint32_t i = 0; i < params.Size(); ++i)
        {
            out.Parameters.push_back({params.Definition(i)->Name, params.Definition(i)->Type, *params.Get(i)});
        }
        const auto& graph = *m_Instance.GetGraph();
        const auto& weights = m_Instance.GetNodeWeights();
        for (size_t i = 0; i < graph.Nodes.size(); ++i)
        {
            AnimatorDebugNode node;
            node.Name = graph.Nodes[i].Name;
            node.Weight = weights[i];
            node.bStateMachine = graph.Nodes[i].Kind == Animation::AnimNodeKind::StateMachine;
            if (node.bStateMachine)
            {
                (void)m_Instance.GetState(node.Name, node.State);
            }
            out.Nodes.push_back(node);
        }
    }
} // namespace NorvesLib::Core::Component
