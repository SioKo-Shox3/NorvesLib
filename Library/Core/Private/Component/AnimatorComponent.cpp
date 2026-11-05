#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Math/MatrixUtils.h"
#include "Animation/SkeletonResource.h"
#include "Platform/PlatformFile.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Animation
{
    int32_t PoseModifierContext::FindJointIndex(Identity name) const
    {
        return m_SkeletonAsset ? m_SkeletonAsset->FindJointIndex(name) : -1;
    }
} // namespace NorvesLib::Core::Animation
namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(AnimatorComponent, Component)
    AnimatorLookTarget AnimatorLookTarget::None()
    {
        return {};
    }
    AnimatorLookTarget AnimatorLookTarget::AtWorldPosition(const Math::Vector3& position)
    {
        AnimatorLookTarget target;
        target.Kind = AnimatorLookTargetKind::WorldPosition;
        target.WorldPosition = position;
        return target;
    }
    AnimatorLookTarget AnimatorLookTarget::AtEntity(uint64_t objectId)
    {
        AnimatorLookTarget target;
        target.Kind = AnimatorLookTargetKind::Entity;
        target.EntityObjectId = objectId;
        return target;
    }
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
        InvalidateRootMotionFrame(true);
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
        InvalidateRootMotionFrame(true);
        return true;
    }
    void AnimatorComponent::SetLookTarget(int32_t priority, const AnimatorLookTarget& target)
    {
        if (target.Kind == AnimatorLookTargetKind::WorldPosition &&
            (!std::isfinite(target.WorldPosition.x) || !std::isfinite(target.WorldPosition.y) ||
             !std::isfinite(target.WorldPosition.z)))
        {
            return;
        }
        auto entry = m_LookTargets.begin();
        while (entry != m_LookTargets.end() && entry->Priority > priority)
        {
            ++entry;
        }
        if (entry != m_LookTargets.end() && entry->Priority == priority)
        {
            entry->Target = target;
        }
        else
        {
            m_LookTargets.insert(entry, LookTargetEntry{priority, target});
        }
    }
    void AnimatorComponent::ClearLookTarget(int32_t priority)
    {
        for (auto entry = m_LookTargets.begin(); entry != m_LookTargets.end(); ++entry)
        {
            if (entry->Priority == priority)
            {
                m_LookTargets.erase(entry);
                return;
            }
        }
    }
    bool AnimatorComponent::TryResolveLookTarget(Math::Vector3& outWorldPosition) const
    {
        if (m_LookTargets.empty())
        {
            return false;
        }
        const auto& target = m_LookTargets.front().Target;
        if (target.Kind == AnimatorLookTargetKind::WorldPosition)
        {
            outWorldPosition = target.WorldPosition;
            return true;
        }
        if (target.Kind == AnimatorLookTargetKind::Entity)
        {
            const auto* owner = GetOwner();
            const auto* world = owner ? owner->GetWorld() : nullptr;
            const auto* entity = world ? world->FindEntityByObjectId(target.EntityObjectId) : nullptr;
            if (entity)
            {
                outWorldPosition = entity->GetWorldTransform().position;
                return true;
            }
        }
        return false;
    }
    bool AnimatorComponent::HasLookTarget(int32_t priority) const
    {
        for (const auto& entry : m_LookTargets)
        {
            if (entry.Priority == priority)
            {
                return true;
            }
        }
        return false;
    }
    uint32_t AnimatorComponent::FindSyncGroup(Identity name) const
    {
        return m_Instance.FindSyncGroup(name);
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
    bool AnimatorComponent::GetInt(Animation::AnimParamHandle handle, int32_t& out) const
    {
        const auto& params = m_Instance.Parameters();
        const auto* definition = params.Definition(handle);
        const auto* value = params.Get(handle);
        if (!definition || !value || definition->Type != Animation::AnimParamType::Int)
            return false;
        out = value->Int;
        return true;
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
        if (!m_Instance.Parameters().ApplyDriveSignals(value))
        {
            return false;
        }
        m_LastDriveSignals = value;
        m_bHasDriveSignals = true;
        return true;
    }
    bool AnimatorComponent::TryGetLastDriveSignals(Animation::AnimDriveSignals& out) const
    {
        if (!m_bHasDriveSignals)
        {
            return false;
        }
        out = m_LastDriveSignals;
        return true;
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
    bool AnimatorComponent::ApplyPoseModifiersJson(
        const Container::String& json, const Animation::Procedural::PoseModifierTypeTable& types,
        Animation::Procedural::PoseModifierConfigReport* report)
    {
        using namespace Animation::Procedural;
        PoseModifierConfigReport localReport;
        auto& result = report ? *report : localReport;
        result = {};
        if (m_bApplyingModifiers)
        {
            result.Error = PoseModifierConfigError::Busy;
            return false;
        }
        if (!m_BoundSkeleton)
        {
            result.Error = PoseModifierConfigError::NoSkeleton;
            return false;
        }
        Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> candidate;
        if (!ParsePoseModifierConfig(json, *m_BoundSkeleton, m_Graph ? &m_Instance.Parameters() : nullptr,
                                     types, candidate, result))
        {
            return false;
        }
        std::stable_sort(candidate.begin(), candidate.end(),
                         [](const auto& a, const auto& b) { return a->Order() < b->Order(); });
        m_Modifiers = std::move(candidate);
        return true;
    }
    bool AnimatorComponent::ApplyPoseModifiersJson(
        const Container::String& json, Animation::Procedural::PoseModifierConfigReport* report)
    {
        return ApplyPoseModifiersJson(json, Animation::Procedural::GetBuiltinPoseModifierTypes(), report);
    }
    bool AnimatorComponent::ApplyPoseModifiersFile(
        const Container::String& path, const Animation::Procedural::PoseModifierTypeTable& types,
        Animation::Procedural::PoseModifierConfigReport* report)
    {
        using namespace Animation::Procedural;
        PoseModifierConfigReport localReport;
        auto& result = report ? *report : localReport;
        result = {};
        if (m_bApplyingModifiers)
        {
            result.Error = PoseModifierConfigError::Busy;
            return false;
        }
        if (!m_BoundSkeleton)
        {
            result.Error = PoseModifierConfigError::NoSkeleton;
            return false;
        }
        Container::VariableArray<uint8_t> bytes;
        if (!Platform::ReadFileBytes(path, bytes).bSuccess)
        {
            result.Error = PoseModifierConfigError::FileReadFailed;
            result.Path = path;
            return false;
        }
        const size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        Container::String json;
        if constexpr (sizeof(TCHAR) == 1)
        {
            // 厳密な UTF-8 の検証は JsonDocument::TryParse が行う。
            for (size_t i = skip; i < bytes.size(); ++i)
            {
                json.push_back(TCHAR(bytes[i]));
            }
        }
        else if (bytes.size() > skip)
        {
            const auto length = bytes.size() - skip;
            if (length > size_t(std::numeric_limits<int>::max()))
            {
                result.Error = PoseModifierConfigError::InvalidJson;
                return false;
            }
            const char* data = reinterpret_cast<const char*>(bytes.data() + skip);
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, int(length), nullptr, 0);
            Container::VariableArray<wchar_t> wide;
            if (count <= 0)
            {
                result.Error = PoseModifierConfigError::InvalidJson;
                return false;
            }
            wide.resize(size_t(count));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, int(length), wide.data(), count) != count)
            {
                result.Error = PoseModifierConfigError::InvalidJson;
                return false;
            }
            for (const auto unit : wide)
            {
                json.push_back(TCHAR(unit));
            }
        }
        return ApplyPoseModifiersJson(json, types, &result);
    }
    bool AnimatorComponent::ApplyPoseModifiersFile(
        const Container::String& path, Animation::Procedural::PoseModifierConfigReport* report)
    {
        return ApplyPoseModifiersFile(path, Animation::Procedural::GetBuiltinPoseModifierTypes(), report);
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
    void AnimatorComponent::InvalidateRootMotionFrame(bool sourceChanged)
    {
        if (sourceChanged && ++m_RootMotionFrame.SourceGeneration == 0)
            ++m_RootMotionFrame.SourceGeneration;
        m_RootMotionFrame.bValid = false;
        m_RootMotionFrame.bConsumed = true;
        m_RootMotionFrame.Delta = {};
        m_RootMotionFrame.UpdateSeconds = 0;
    }
    bool AnimatorComponent::HasValidRootMotionSource() const
    {
        auto* mesh = Mesh();
        return mesh && m_Graph && m_Graph->IsLoaded() && m_BoundAsset && mesh->GetSkeletalAsset() == m_BoundAsset &&
               m_BoundAsset->GetSkeleton() == m_BoundSkeleton && m_BoundAsset->GetMesh() == m_BoundMesh &&
               m_Graph->GetData() == m_Instance.GetGraph();
    }
    bool AnimatorComponent::TryConsumeRootMotionFrame(uint64_t serial, Animation::RootMotionDelta& out)
    {
        if (!HasValidRootMotionSource())
        {
            InvalidateRootMotionFrame(true);
            return false;
        }
        if (!m_RootMotionFrame.bValid || m_RootMotionFrame.bConsumed || serial != m_RootMotionFrame.UpdateSerial)
            return false;
        out = m_RootMotionFrame.Delta;
        m_RootMotionFrame.bConsumed = true;
        (void)m_Instance.ConsumeRootMotion();
        return true;
    }
    bool AnimatorComponent::UpdateAnimation(float dt)
    {
        InvalidateRootMotionFrame(false);
        if (++m_RootMotionFrame.UpdateSerial == 0)
            ++m_RootMotionFrame.UpdateSerial;
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
        m_RootMotionFrame.Delta = m_Instance.GetLastRootMotionDelta();
        m_RootMotionFrame.UpdateSeconds = step;
        m_RootMotionFrame.bValid = true;
        m_RootMotionFrame.bConsumed = false;
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
        context.SyncPhases = m_Instance.GetSyncPhases();
        if (const auto* owner = mesh->GetOwner())
        {
            const auto& world = owner->GetWorldTransform();
            context.MeshToWorld = Math::MatrixUtils::CreateWorldRowVector(world.position, world.rotation, world.scale);
        }
        else
        {
            context.MeshToWorld = Math::Matrix4x4::Identity;
        }
        context.MeshNodeTransform = mesh->GetMeshNodeGlobalTransform();
        context.SetDriveSignals(m_bHasDriveSignals ? &m_LastDriveSignals : nullptr);
        context.SetOwner(GetOwner());
        context.SetSkeletonAsset(m_BoundSkeleton.get());
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
    void AnimatorComponent::TickAnimationGroup(float dt)
    {
        (void)UpdateAnimation(dt);
    }
    void AnimatorComponent::TickPoseFinalizeGroup()
    {
        if (m_bEvaluate)
            (void)EvaluateAnimation();
        m_Instance.Events().Dispatch();
    }
    void AnimatorComponent::BuildDebugSnapshot(AnimatorDebugSnapshot& out) const
    {
        out.Parameters.clear();
        out.Nodes.clear();
        out.SyncGroups.clear();
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
        const auto phases = m_Instance.GetSyncPhases();
        for (size_t i = 0; i < graph.SyncGroups.size() && i < phases.size(); ++i)
            out.SyncGroups.push_back({graph.SyncGroups[i].Name, phases[i]});

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
