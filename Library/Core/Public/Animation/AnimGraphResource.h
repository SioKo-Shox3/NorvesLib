#pragma once
#include "Animation/AnimParams.h"
#include "Animation/PoseOps.h"
#include "Animation/AnimationClipResource.h"
#include "Object/Resource.h"
#include "Object/Reflection.h"

namespace NorvesLib::Core
{
    class SkeletonResource;
    namespace Animation
    {
        inline constexpr uint32_t InvalidAnimNode = UINT32_MAX;
        enum class AnimNodeKind : uint8_t
        {
            Clip,
            BlendSpace1D,
            BlendSpace2D,
            Blend2,
            Layered,
            StateMachine,
            Select
        };
        enum class AnimCompare : uint8_t
        {
            Equal,
            NotEqual,
            Less,
            LessEqual,
            Greater,
            GreaterEqual
        };
        enum class AnimTransitionCurve : uint8_t
        {
            Linear,
            Smoothstep
        };
        enum class AnimInterrupt : uint8_t
        {
            None,
            Current,
            Next,
            Either
        };
        struct AnimScalar
        {
            AnimParamHandle Parameter = InvalidAnimParam;
            float Constant = 0;
            float Read(const AnimParamSet& params) const;
        };
        struct AnimCondition
        {
            AnimParamHandle Parameter = InvalidAnimParam;
            AnimCompare Compare = AnimCompare::Equal;
            AnimParamValue Value;
        };
        struct AnimTransition
        {
            uint32_t From = InvalidAnimNode;
            uint32_t To = InvalidAnimNode;
            float Duration = 0.15f;
            float ExitTime = -1;
            int32_t Priority = 0;
            AnimTransitionCurve Curve = AnimTransitionCurve::Linear;
            AnimInterrupt Interrupt = AnimInterrupt::None;
            Container::VariableArray<AnimCondition> Conditions;
            Identity SourceMarker, TargetMarker;
        };
        enum class AnimRootPolicy : uint8_t
        {
            Inherit,
            Animation,
            Velocity
        };
        struct AnimState
        {
            Identity Name;
            uint32_t Node = InvalidAnimNode;
            AnimRootPolicy RootPolicy = AnimRootPolicy::Inherit;
        };
        struct AnimLayer
        {
            uint32_t Node = InvalidAnimNode;
            uint32_t Mask = InvalidAnimNode;
            AnimScalar Weight{InvalidAnimParam, 1};
            bool bAdditive = false;
        };
        struct AnimGraphNode
        {
            Identity Name;
            AnimNodeKind Kind = AnimNodeKind::Clip;
            Container::VariableArray<uint32_t> Children;
            Container::VariableArray<float> AxisX, AxisY;
            AnimScalar X, Y, Weight{InvalidAnimParam, 0.5f};
            AnimParamHandle Select = InvalidAnimParam;
            uint32_t Clip = InvalidAnimNode;
            float PlaybackRate = 1;
            bool bLoop = true;
            Container::VariableArray<AnimLayer> Layers;
            Container::VariableArray<AnimState> States;
            Container::VariableArray<AnimTransition> Transitions;
            uint32_t InitialState = 0;
            // 空名は独立再生。添字はコンパイル時に束縛する。
            Identity SyncGroup;
            uint32_t SyncGroupIndex = InvalidAnimNode;
        };
        struct AnimSyncGroupDefinition
        {
            Identity Name;
            AnimScalar DriveSpeed;
            float MinimumStrideRate = .5f, MaximumStrideRate = 2;
            bool bStrideEnabled = false;
            Container::VariableArray<Identity> Markers;
        };
        struct AnimGraphData
        {
            Container::VariableArray<AnimParamDefinition> Parameters;
            Container::VariableArray<AnimSyncGroupDefinition> SyncGroups;
            Container::VariableArray<BoneMask> Masks;
            Container::VariableArray<Identity> MaskNames;
            Container::VariableArray<AnimGraphNode> Nodes;
            Container::VariableArray<uint32_t> EvaluationOrder;
            Container::VariableArray<Container::TSharedPtr<AnimationClipResource>> Clips;
            Container::VariableArray<int32_t> Parents;
            Container::VariableArray<Identity> JointNames;
            uint32_t Root = InvalidAnimNode;
        };
        enum class AnimGraphError : uint8_t
        {
            None,
            InvalidJson,
            UnsupportedVersion,
            InvalidSchema,
            DuplicateName,
            UnknownClip,
            UnknownJoint,
            UnknownParameter,
            UnknownNode,
            UnknownMask,
            InvalidTransition,
            Cycle,
            SkeletonMismatch,
            InvalidClip,
            MarkerMismatch
        };
        struct AnimGraphReport
        {
            AnimGraphError Error = AnimGraphError::None;
            Container::String Detail;
        };
        class IClipResolver
        {
          public:
            virtual ~IClipResolver() = default;
            virtual Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const = 0;
        };
        // 失敗時outは空。不変データの候補を全検証してからResourceへ公開する。
        [[nodiscard]] bool CompileAnimGraph(const Container::String& json, const SkeletonResource& skeleton,
                                            const IClipResolver&, Container::TSharedPtr<const AnimGraphData>& out,
                                            AnimGraphReport& report);
    } // namespace Animation
    class AnimGraphResource : public Resource
    {
        REFLECTION_CLASS(AnimGraphResource, Resource)
      public:
        AnimGraphResource();
        explicit AnimGraphResource(const FieldInitializer*);
        explicit AnimGraphResource(const IUnknown*);
        ~AnimGraphResource() override;
        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;
        [[nodiscard]] bool Compile(const Container::String&, const SkeletonResource&, const Animation::IClipResolver&,
                                   Animation::AnimGraphReport&);
        const Container::TSharedPtr<const Animation::AnimGraphData>& GetData() const
        {
            return m_Data;
        }

      private:
        Container::TSharedPtr<const Animation::AnimGraphData> m_Data;
    };
} // namespace NorvesLib::Core
