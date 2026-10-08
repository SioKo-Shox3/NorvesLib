#include "Animation/AnimGraphResource.h"
#include "Animation/AnimSync.h"
#include "Animation/SkeletonResource.h"
#include "Asset/CookedSkeletonV1.h"
#include "Asset/RigSplitWire.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation
{
    float AnimScalar::Read(const AnimParamSet& params) const
    {
        const auto* value = params.Get(Parameter);
        return Parameter == InvalidAnimParam ? Constant : value ? value->Float : Constant;
    }
    namespace
    {
        bool Fail(AnimGraphReport& r, AnimGraphError e, const char* detail)
        {
            if (r.Error == AnimGraphError::None)
            {
                r.Error = e;
                r.Detail.clear();
                for (const char* p = detail; *p; ++p)
                {
                    r.Detail.push_back(TCHAR(*p));
                }
            }
            return false;
        }
        bool Name(JsonValue value, Identity& out)
        {
            if (!value.IsString() || value.AsString().empty())
            {
                return false;
            }
            out = Identity(value.AsString());
            return true;
        }
        bool Number(JsonValue value, float& out)
        {
            const double n = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(n) || std::fabs(n) > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = float(n);
            return true;
        }
        bool Integer(JsonValue value, int32_t& out)
        {
            const double n = value.AsNumber();
            if (!value.IsIntegerLiteral() || !std::isfinite(n) || n < INT32_MIN || n > INT32_MAX)
            {
                return false;
            }
            out = int32_t(n);
            return true;
        }
        bool UniqueObject(JsonValue value)
        {
            if (!value.IsObject())
            {
                return false;
            }
            for (size_t i = 0; i < value.GetObjectSize(); ++i)
            {
                for (size_t j = 0; j < i; ++j)
                {
                    if (value.GetMemberName(i) == value.GetMemberName(j))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        bool Word(JsonValue value, const char* text)
        {
            if (!value.IsString())
            {
                return false;
            }
            const auto& s = value.AsString();
            size_t i = 0;
            for (; text[i]; ++i)
            {
                if (i >= s.size() || s[i] != TCHAR(text[i]))
                {
                    return false;
                }
            }
            return i == s.size();
        }
        template <class Items, class Key> uint32_t Find(const Items& items, Identity name, Key key)
        {
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (key(items[i]) == name)
                {
                    return uint32_t(i);
                }
            }
            return InvalidAnimNode;
        }
        uint32_t Param(const AnimGraphData& d, JsonValue name)
        {
            Identity id;
            if (!Name(name, id))
            {
                return InvalidAnimParam;
            }
            return Find(d.Parameters, id, [](const auto& p) { return p.Name; });
        }
        bool Scalar(const AnimGraphData& d, JsonValue value, AnimScalar& out, AnimGraphReport& r)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (value.IsNumber())
            {
                return Number(value, out.Constant) || Fail(r, AnimGraphError::InvalidSchema, "scalar");
            }
            const auto id = Param(d, value);
            if (id == InvalidAnimParam || d.Parameters[id].Type != AnimParamType::Float)
            {
                return Fail(r, AnimGraphError::UnknownParameter, "scalar_parameter");
            }
            out.Parameter = id;
            return true;
        }
        bool ReadParams(JsonValue array, AnimGraphData& d, AnimGraphReport& r)
        {
            if (!array.IsValid())
            {
                return true;
            }
            if (!array.IsArray() || array.GetArraySize() > 256)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "params");
            }
            for (size_t i = 0; i < array.GetArraySize(); ++i)
            {
                auto v = array.GetArrayElement(i);
                AnimParamDefinition p;
                if (!UniqueObject(v) || !Name(v.FindMember("name"), p.Name))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "param_name");
                }
                if (Find(d.Parameters, p.Name, [](const auto& x) { return x.Name; }) != InvalidAnimParam)
                {
                    return Fail(r, AnimGraphError::DuplicateName, "param_name");
                }
                auto type = v.FindMember("type"), def = v.FindMember("value");
                if (Word(type, "float"))
                {
                    p.Type = AnimParamType::Float;
                    if (def.IsValid() && !Number(def, p.Default.Float))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "float_default");
                    }
                }
                else if (Word(type, "int"))
                {
                    p.Type = AnimParamType::Int;
                    if (def.IsValid() && !Integer(def, p.Default.Int))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "int_default");
                    }
                }
                else if (Word(type, "bool") || Word(type, "trigger"))
                {
                    p.Type = Word(type, "bool") ? AnimParamType::Bool : AnimParamType::Trigger;
                    if (def.IsValid() && !def.IsBoolean())
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "bool_default");
                    }
                    p.Default.Bool = def.AsBool();
                }
                else
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "param_type");
                }
                const char* bindings[] = {"none",          "speed",         "velocityX",     "velocityY", "velocityZ",
                                          "accelerationX", "accelerationY", "accelerationZ", "turnRate",  "grounded",
                                          "groundNormalX", "groundNormalY", "groundNormalZ"};
                auto bind = v.FindMember("bind");
                if (bind.IsValid())
                {
                    bool found = false;
                    for (uint8_t b = 0; b < 13; ++b)
                    {
                        if (Word(bind, bindings[b]))
                        {
                            p.Binding = AnimSignalBinding(b);
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "signal_bind");
                    }
                }
                d.Parameters.push_back(p);
            }
            AnimParamSet probe;
            return probe.Initialize(d.Parameters) || Fail(r, AnimGraphError::InvalidSchema, "param_binding_type");
        }
        bool ReadMasks(JsonValue array, AnimGraphData& d, AnimGraphReport& r)
        {
            if (!array.IsValid())
            {
                return true;
            }
            if (!array.IsArray() || array.GetArraySize() > 256)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "masks");
            }
            for (size_t i = 0; i < array.GetArraySize(); ++i)
            {
                auto v = array.GetArrayElement(i);
                Identity name, root;
                if (!UniqueObject(v) || !Name(v.FindMember("name"), name) || !Name(v.FindMember("root"), root))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "mask_name");
                }
                if (Find(d.MaskNames, name, [](auto n) { return n; }) != InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::DuplicateName, "mask_name");
                }
                const auto joint = Find(d.JointNames, root, [](auto n) { return n; });
                if (joint == InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::UnknownJoint, "mask_root");
                }
                int32_t fade = 0;
                if (v.HasMember("fadeDepth") && (!Integer(v.FindMember("fadeDepth"), fade) || fade < 0))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "mask_depth");
                }
                BoneMask mask;
                if (!mask.Build(d.Parents, joint, uint32_t(fade)))
                {
                    return Fail(r, AnimGraphError::SkeletonMismatch, "mask_hierarchy");
                }
                d.MaskNames.push_back(name);
                d.Masks.push_back(std::move(mask));
            }
            return true;
        }
        bool ReadConditions(JsonValue array, const AnimGraphData& d, AnimTransition& t, AnimGraphReport& r)
        {
            if (!array.IsValid())
            {
                return true;
            }
            if (!array.IsArray() || array.GetArraySize() > 256)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "conditions");
            }
            for (size_t i = 0; i < array.GetArraySize(); ++i)
            {
                auto v = array.GetArrayElement(i);
                AnimCondition c;
                if (!UniqueObject(v))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "condition");
                }
                c.Parameter = Param(d, v.FindMember("param"));
                if (c.Parameter == InvalidAnimParam)
                {
                    return Fail(r, AnimGraphError::UnknownParameter, "condition_param");
                }
                const char* ops[] = {"eq", "ne", "lt", "le", "gt", "ge"};
                bool found = false;
                for (uint8_t op = 0; op < 6; ++op)
                {
                    if (Word(v.FindMember("op"), ops[op]))
                    {
                        c.Compare = AnimCompare(op);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "condition_op");
                }
                const auto type = d.Parameters[c.Parameter].Type;
                auto value = v.FindMember("value");
                if (type == AnimParamType::Float)
                {
                    if (!Number(value, c.Value.Float))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "condition_float");
                    }
                }
                else if (type == AnimParamType::Int)
                {
                    if (!Integer(value, c.Value.Int))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "condition_int");
                    }
                }
                else
                {
                    if (!value.IsBoolean() || uint8_t(c.Compare) > 1)
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "condition_bool");
                    }
                    c.Value.Bool = value.AsBool();
                }
                t.Conditions.push_back(c);
            }
            return true;
        }
        bool ReadMachine(JsonValue v, AnimGraphNode& n, const AnimGraphData& d, AnimGraphReport& r)
        {
            auto states = v.FindMember("states");
            if (!states.IsArray() || states.GetArraySize() == 0 || states.GetArraySize() > 512)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "states");
            }
            for (size_t i = 0; i < states.GetArraySize(); ++i)
            {
                auto state = states.GetArrayElement(i);
                AnimState s;
                Identity node;
                if (!UniqueObject(state) || !Name(state.FindMember("name"), s.Name) ||
                    !Name(state.FindMember("node"), node))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "state");
                }
                if (Find(n.States, s.Name, [](const auto& a) { return a.Name; }) != InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::DuplicateName, "state");
                }
                s.Node = Find(d.Nodes, node, [](const auto& a) { return a.Name; });
                if (s.Node == InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::UnknownNode, "state_node");
                }
                auto rootPolicy = state.FindMember("rootMotion");
                if (rootPolicy.IsValid())
                {
                    if (Word(rootPolicy, "animation"))
                        s.RootPolicy = AnimRootPolicy::Animation;
                    else if (Word(rootPolicy, "velocity"))
                        s.RootPolicy = AnimRootPolicy::Velocity;
                    else if (!Word(rootPolicy, "inherit"))
                        return Fail(r, AnimGraphError::InvalidSchema, "state_root_motion");
                }
                n.States.push_back(s);
                n.Children.push_back(s.Node);
            }
            Identity initial;
            if (v.HasMember("initial"))
            {
                if (!Name(v.FindMember("initial"), initial))
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "initial");
                }
                n.InitialState = Find(n.States, initial, [](const auto& a) { return a.Name; });
                if (n.InitialState == InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "initial");
                }
            }
            auto transitions = v.FindMember("transitions");
            if (!transitions.IsValid())
            {
                return true;
            }
            if (!transitions.IsArray() || transitions.GetArraySize() > 2048)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "transitions");
            }
            for (size_t i = 0; i < transitions.GetArraySize(); ++i)
            {
                auto value = transitions.GetArrayElement(i);
                AnimTransition t;
                Identity from, to;
                if (!UniqueObject(value) || !Name(value.FindMember("from"), from) || !Name(value.FindMember("to"), to))
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "transition_names");
                }
                t.From = Word(value.FindMember("from"), "*")
                             ? InvalidAnimNode
                             : Find(n.States, from, [](const auto& a) { return a.Name; });
                t.To = Find(n.States, to, [](const auto& a) { return a.Name; });
                if ((t.From == InvalidAnimNode && !Word(value.FindMember("from"), "*")) || t.To == InvalidAnimNode)
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "transition_target");
                }
                if (value.HasMember("sourceMarker") || value.HasMember("targetMarker"))
                {
                    if (!Name(value.FindMember("sourceMarker"), t.SourceMarker) ||
                        !Name(value.FindMember("targetMarker"), t.TargetMarker))
                        return Fail(r, AnimGraphError::InvalidTransition, "transition_markers");
                }
                if (value.HasMember("duration") &&
                    (!Number(value.FindMember("duration"), t.Duration) || t.Duration < 0))
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "transition_duration");
                }
                if (value.HasMember("exitTime") &&
                    (!Number(value.FindMember("exitTime"), t.ExitTime) || t.ExitTime < 0))
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "exit_time");
                }
                if (value.HasMember("priority") && !Integer(value.FindMember("priority"), t.Priority))
                {
                    return Fail(r, AnimGraphError::InvalidTransition, "priority");
                }
                auto curve = value.FindMember("curve");
                if (curve.IsValid())
                {
                    if (Word(curve, "smoothstep"))
                    {
                        t.Curve = AnimTransitionCurve::Smoothstep;
                    }
                    else if (!Word(curve, "linear"))
                    {
                        return Fail(r, AnimGraphError::InvalidTransition, "curve");
                    }
                }
                auto interrupt = value.FindMember("interrupt");
                if (interrupt.IsValid())
                {
                    const char* kinds[] = {"none", "current", "next", "either"};
                    bool found = false;
                    for (uint8_t k = 0; k < 4; ++k)
                    {
                        if (Word(interrupt, kinds[k]))
                        {
                            t.Interrupt = AnimInterrupt(k);
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        return Fail(r, AnimGraphError::InvalidTransition, "interrupt");
                    }
                }
                if (!ReadConditions(value.FindMember("conditions"), d, t, r))
                {
                    return false;
                }
                n.Transitions.push_back(std::move(t));
            }
            std::stable_sort(n.Transitions.begin(), n.Transitions.end(),
                             [](const auto& a, const auto& b) { return a.Priority > b.Priority; });
            return true;
        }
        bool ReadNode(JsonValue v, AnimGraphNode& n, AnimGraphData& d, const IClipResolver& resolver,
                      AnimGraphReport& r)
        {
            const char* kinds[] = {"clip", "blend1d", "blend2d", "blend2", "layered", "stateMachine", "select"};
            bool found = false;
            for (uint8_t k = 0; k < 7; ++k)
            {
                if (Word(v.FindMember("type"), kinds[k]))
                {
                    n.Kind = AnimNodeKind(k);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "node_type");
            }
            auto child = [&](JsonValue name, uint32_t& out)
            {
                Identity id;
                if (!Name(name, id))
                {
                    return Fail(r, AnimGraphError::UnknownNode, "node_ref");
                }
                out = Find(d.Nodes, id, [](const auto& x) { return x.Name; });
                return out != InvalidAnimNode || Fail(r, AnimGraphError::UnknownNode, "node_ref");
            };
            if (n.Kind == AnimNodeKind::Clip)
            {
                auto name = v.FindMember("clip");
                if (!name.IsString() || name.AsString().empty())
                {
                    return Fail(r, AnimGraphError::UnknownClip, "clip");
                }
                auto clip = resolver.ResolveClip(name.AsString());
                if (!clip || !clip->IsLoaded())
                {
                    return Fail(r, AnimGraphError::UnknownClip, "clip");
                }
                if (!clip->GetPoseRuntime().bLegacyValid)
                {
                    return Fail(r, AnimGraphError::InvalidClip, "clip_channels");
                }
                ClipMetadataReport metadataReport;
                if (!ValidateClipMetadata(clip->GetClipMetadata(), clip->GetClip().DurationSeconds, metadataReport))
                    return Fail(r, AnimGraphError::InvalidClip, "clip_metadata");
                const auto rootJoint = clip->GetClipMetadata().Root.Joint;
                if (rootJoint != UINT32_MAX && (rootJoint >= d.Parents.size() || d.Parents[rootJoint] != -1))
                    return Fail(r, AnimGraphError::UnknownJoint, "root_motion_joint");
                n.Clip = uint32_t(d.Clips.size());
                d.Clips.push_back(std::move(clip));
                if (v.HasMember("loop"))
                {
                    if (!v.FindMember("loop").IsBoolean())
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "loop");
                    }
                    n.bLoop = v.FindMember("loop").AsBool();
                }
                if (v.HasMember("rate") && !Number(v.FindMember("rate"), n.PlaybackRate))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "rate");
                }
                if (v.HasMember("syncGroup") && !Name(v.FindMember("syncGroup"), n.SyncGroup))
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "sync_group");
                }
                return true;
            }
            if (n.Kind == AnimNodeKind::StateMachine)
            {
                return ReadMachine(v, n, d, r);
            }
            if (!Scalar(d, v.FindMember("x"), n.X, r) || !Scalar(d, v.FindMember("y"), n.Y, r) ||
                !Scalar(d, v.FindMember("weight"), n.Weight, r))
            {
                return false;
            }
            if (n.Kind == AnimNodeKind::BlendSpace1D)
            {
                auto samples = v.FindMember("samples");
                if (!samples.IsArray() || samples.GetArraySize() == 0 || samples.GetArraySize() > 512)
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "samples");
                }
                for (size_t i = 0; i < samples.GetArraySize(); ++i)
                {
                    auto sample = samples.GetArrayElement(i);
                    float x = 0;
                    uint32_t id;
                    if (!UniqueObject(sample) || !Number(sample.FindMember("position"), x) ||
                        (!n.AxisX.empty() && x <= n.AxisX.back()))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "sample_axis");
                    }
                    if (!child(sample.FindMember("node"), id))
                    {
                        return false;
                    }
                    n.AxisX.push_back(x);
                    n.Children.push_back(id);
                }
                return true;
            }
            if (n.Kind == AnimNodeKind::Layered)
            {
                uint32_t base;
                if (!child(v.FindMember("base"), base))
                {
                    return false;
                }
                n.Children.push_back(base);
                auto layers = v.FindMember("layers");
                if (!layers.IsArray() || layers.GetArraySize() > 256)
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "layers");
                }
                for (size_t i = 0; i < layers.GetArraySize(); ++i)
                {
                    auto value = layers.GetArrayElement(i);
                    AnimLayer layer;
                    if (!UniqueObject(value))
                    {
                        return Fail(r, AnimGraphError::InvalidSchema, "layer");
                    }
                    if (!child(value.FindMember("node"), layer.Node) ||
                        !Scalar(d, value.FindMember("weight"), layer.Weight, r))
                    {
                        return false;
                    }
                    auto mask = value.FindMember("mask");
                    if (mask.IsValid())
                    {
                        Identity name;
                        if (!Name(mask, name))
                        {
                            return Fail(r, AnimGraphError::UnknownMask, "mask");
                        }
                        layer.Mask = Find(d.MaskNames, name, [](auto x) { return x; });
                        if (layer.Mask == InvalidAnimNode)
                        {
                            return Fail(r, AnimGraphError::UnknownMask, "mask");
                        }
                    }
                    auto mode = value.FindMember("mode");
                    if (mode.IsValid())
                    {
                        if (Word(mode, "additive"))
                        {
                            layer.bAdditive = true;
                        }
                        else if (!Word(mode, "override"))
                        {
                            return Fail(r, AnimGraphError::InvalidSchema, "layer_mode");
                        }
                    }
                    n.Children.push_back(layer.Node);
                    n.Layers.push_back(layer);
                }
                return true;
            }
            auto children = v.FindMember("children");
            if (!children.IsArray() || children.GetArraySize() == 0 || children.GetArraySize() > 512)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "children");
            }
            for (size_t i = 0; i < children.GetArraySize(); ++i)
            {
                uint32_t id;
                if (!child(children.GetArrayElement(i), id))
                {
                    return false;
                }
                n.Children.push_back(id);
            }
            if (n.Kind == AnimNodeKind::Blend2 && n.Children.size() != 2)
            {
                return Fail(r, AnimGraphError::InvalidSchema, "blend2_children");
            }
            if (n.Kind == AnimNodeKind::Select)
            {
                n.Select = Param(d, v.FindMember("param"));
                if (n.Select == InvalidAnimParam || (d.Parameters[n.Select].Type != AnimParamType::Int &&
                                                     d.Parameters[n.Select].Type != AnimParamType::Bool))
                {
                    return Fail(r, AnimGraphError::UnknownParameter, "select_param");
                }
            }
            if (n.Kind == AnimNodeKind::BlendSpace2D)
            {
                auto axis = [&](JsonValue a, Container::VariableArray<float>& out)
                {
                    if (!a.IsArray() || a.GetArraySize() < 2 || a.GetArraySize() > 128)
                    {
                        return false;
                    }
                    for (size_t i = 0; i < a.GetArraySize(); ++i)
                    {
                        float x;
                        if (!Number(a.GetArrayElement(i), x) || (!out.empty() && x <= out.back()))
                        {
                            return false;
                        }
                        out.push_back(x);
                    }
                    const double step = double(out[1]) - out[0];
                    for (size_t i = 2; i < out.size(); ++i)
                    {
                        if (std::fabs((double(out[i]) - out[i - 1]) - step) > 1e-5 * std::max(1.0, std::fabs(step)))
                        {
                            return false;
                        }
                    }
                    return true;
                };
                if (!axis(v.FindMember("axisX"), n.AxisX) || !axis(v.FindMember("axisY"), n.AxisY) ||
                    n.Children.size() != n.AxisX.size() * n.AxisY.size())
                {
                    return Fail(r, AnimGraphError::InvalidSchema, "grid");
                }
            }
            return true;
        }
        bool ReadSyncGroups(JsonValue value, AnimGraphData& d, AnimGraphReport& r)
        {
            if (value.IsValid())
            {
                if (!value.IsArray() || value.GetArraySize() > 64)
                    return Fail(r, AnimGraphError::InvalidSchema, "sync_groups");
                for (size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    auto object = value.GetArrayElement(i);
                    AnimSyncGroupDefinition group;
                    if (!UniqueObject(object) || !Name(object.FindMember("name"), group.Name))
                        return Fail(r, AnimGraphError::InvalidSchema, "sync_group_name");
                    if (Find(d.SyncGroups, group.Name, [](const auto& x) { return x.Name; }) != InvalidAnimNode)
                        return Fail(r, AnimGraphError::DuplicateName, "sync_group_name");
                    if (object.HasMember("speed"))
                    {
                        group.bStrideEnabled = true;
                        if (!Scalar(d, object.FindMember("speed"), group.DriveSpeed, r))
                            return false;
                    }
                    if ((object.HasMember("minRate") &&
                         !Number(object.FindMember("minRate"), group.MinimumStrideRate)) ||
                        (object.HasMember("maxRate") &&
                         !Number(object.FindMember("maxRate"), group.MaximumStrideRate)) ||
                        group.MinimumStrideRate < 0 || group.MaximumStrideRate < group.MinimumStrideRate ||
                        group.MaximumStrideRate > 8)
                        return Fail(r, AnimGraphError::InvalidSchema, "sync_stride_rate");
                    d.SyncGroups.push_back(std::move(group));
                }
            }
            for (auto& node : d.Nodes)
            {
                if (!node.SyncGroup.IsValid())
                    continue;
                if (!node.bLoop || d.Clips[node.Clip]->GetClip().DurationSeconds <= 0)
                    return Fail(r, AnimGraphError::InvalidClip, "sync_loop");
                auto index = Find(d.SyncGroups, node.SyncGroup, [](const auto& x) { return x.Name; });
                if (index == InvalidAnimNode)
                {
                    if (d.SyncGroups.size() >= 64)
                        return Fail(r, AnimGraphError::InvalidSchema, "sync_group_limit");
                    index = uint32_t(d.SyncGroups.size());
                    AnimSyncGroupDefinition group;
                    group.Name = node.SyncGroup;
                    d.SyncGroups.push_back(std::move(group));
                }
                node.SyncGroupIndex = index;
                auto& group = d.SyncGroups[index];
                if (group.Markers.empty())
                    for (const auto& marker : d.Clips[node.Clip]->GetClipMetadata().Markers)
                        group.Markers.push_back(marker.Name);
            }
            for (const auto& node : d.Nodes)
            {
                if (node.SyncGroupIndex == InvalidAnimNode)
                    continue;
                const auto& clip = *d.Clips[node.Clip];
                AnimSyncMap map;
                if (!BuildAnimSyncMap(clip.GetClipMetadata(), clip.GetClip().DurationSeconds,
                                      d.SyncGroups[node.SyncGroupIndex].Markers, map))
                    return Fail(r, AnimGraphError::MarkerMismatch, "sync_markers");
            }
            for (const auto& machine : d.Nodes)
                for (const auto& transition : machine.Transitions)
                {
                    if (!transition.SourceMarker.IsValid())
                        continue;
                    Container::VariableArray<uint8_t> visited(d.Nodes.size(), 0);
                    bool found = false;
                    auto visit = [&](auto&& self, uint32_t index) -> bool {
                        if (visited[index])
                            return true;
                        visited[index] = 1;
                        const auto& node = d.Nodes[index];
                        if (node.SyncGroupIndex != InvalidAnimNode)
                        {
                            if (d.Clips[node.Clip]->GetClipMetadata().Markers.empty())
                                return false;
                            const auto hasSource = [&](uint32_t state) {
                                Container::VariableArray<uint8_t> seen(d.Nodes.size(), 0);
                                auto search = [&](auto&& recurse, uint32_t id) -> bool {
                                    if (seen[id])
                                        return false;
                                    seen[id] = 1;
                                    const auto& source = d.Nodes[id];
                                    if (source.SyncGroupIndex == node.SyncGroupIndex &&
                                        !d.Clips[source.Clip]->GetClipMetadata().Markers.empty())
                                        return true;
                                    for (auto child : source.Children)
                                        if (recurse(recurse, child))
                                            return true;
                                    return false;
                                };
                                return search(search, machine.States[state].Node);
                            };
                            if (transition.From != InvalidAnimNode)
                            {
                                if (!hasSource(transition.From))
                                    return false;
                            }
                            else
                                for (uint32_t state = 0; state < machine.States.size(); ++state)
                                    if (state != transition.To && !hasSource(state))
                                        return false;
                            const auto& names = d.SyncGroups[node.SyncGroupIndex].Markers;
                            if (std::find(names.begin(), names.end(), transition.SourceMarker) == names.end() ||
                                std::find(names.begin(), names.end(), transition.TargetMarker) == names.end())
                                return false;
                            found = true;
                        }
                        for (auto child : node.Children)
                            if (!self(self, child))
                                return false;
                        return true;
                    };
                    if (!visit(visit, machine.States[transition.To].Node) || !found)
                        return Fail(r, AnimGraphError::MarkerMismatch, "transition_markers");
                }
            return true;
        }
    } // namespace
    bool CompileAnimGraph(const Container::String& json, const SkeletonResource& skeleton,
                          const IClipResolver& resolver, Container::TSharedPtr<const AnimGraphData>& out,
                          AnimGraphReport& report)
    {
        out.reset();
        report = {};
        JsonDocument doc;
        if (!JsonDocument::TryParse(json, doc))
        {
            return Fail(report, AnimGraphError::InvalidJson, "json");
        }
        const auto root = doc.GetRoot();
        int32_t version = 0;
        if (!UniqueObject(root))
        {
            return Fail(report, AnimGraphError::InvalidSchema, "root");
        }
        if (!Integer(root.FindMember("version"), version) || version != 1)
        {
            return Fail(report, AnimGraphError::UnsupportedVersion, "version");
        }
        if (!skeleton.GetPoseRuntime().bValid)
        {
            return Fail(report, AnimGraphError::SkeletonMismatch, "skeleton");
        }
        auto d = Container::MakeShared<AnimGraphData>();
        d->Parents = skeleton.GetPoseRuntime().Parents;
        if (skeleton.IsSplitV1())
        {
            for (const auto& j : skeleton.GetSplitSkeleton()->Topology.Joints)
            {
                Container::String name;
                if (!Skeletal::SplitWire::NativeName(j.Name, name))
                {
                    return Fail(report, AnimGraphError::SkeletonMismatch, "joint_name");
                }
                d->JointNames.push_back(Identity(name));
            }
        }
        else
        {
            for (const auto& j : skeleton.GetJoints())
            {
                d->JointNames.push_back(Identity(j.Name));
            }
        }
        if (!ReadParams(root.FindMember("params"), *d, report) || !ReadMasks(root.FindMember("masks"), *d, report))
        {
            return false;
        }
        const auto nodes = root.FindMember("nodes");
        if (!nodes.IsArray() || nodes.GetArraySize() == 0 || nodes.GetArraySize() > 512)
        {
            return Fail(report, AnimGraphError::InvalidSchema, "nodes");
        }
        d->Nodes.resize(nodes.GetArraySize());
        for (size_t i = 0; i < d->Nodes.size(); ++i)
        {
            auto n = nodes.GetArrayElement(i);
            if (!UniqueObject(n) || !Name(n.FindMember("id"), d->Nodes[i].Name))
            {
                return Fail(report, AnimGraphError::InvalidSchema, "node_id");
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (d->Nodes[j].Name == d->Nodes[i].Name)
                {
                    return Fail(report, AnimGraphError::DuplicateName, "node_id");
                }
            }
        }
        for (size_t i = 0; i < d->Nodes.size(); ++i)
        {
            if (!ReadNode(nodes.GetArrayElement(i), d->Nodes[i], *d, resolver, report))
            {
                return false;
            }
        }
        if (!ReadSyncGroups(root.FindMember("syncGroups"), *d, report))
            return false;
        Identity rootName;
        if (!Name(root.FindMember("root"), rootName))
        {
            return Fail(report, AnimGraphError::UnknownNode, "root_node");
        }
        d->Root = Find(d->Nodes, rootName, [](const auto& n) { return n.Name; });
        if (d->Root == InvalidAnimNode)
        {
            return Fail(report, AnimGraphError::UnknownNode, "root_node");
        }
        Container::VariableArray<uint8_t> visited(d->Nodes.size(), 0);
        auto visit = [&](auto&& self, uint32_t index) -> bool
        {
            if (visited[index] == 1)
            {
                return false;
            }
            if (visited[index] == 2)
            {
                return true;
            }
            visited[index] = 1;
            for (uint32_t child : d->Nodes[index].Children)
            {
                if (!self(self, child))
                {
                    return false;
                }
            }
            visited[index] = 2;
            d->EvaluationOrder.push_back(index);
            return true;
        };
        for (uint32_t i = 0; i < d->Nodes.size(); ++i)
        {
            if (!visit(visit, i))
            {
                return Fail(report, AnimGraphError::Cycle, "node_cycle");
            }
        }
        out = std::move(d);
        return true;
    }
} // namespace NorvesLib::Core::Animation

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(AnimGraphResource, Resource)
    AnimGraphResource::AnimGraphResource() = default;
    AnimGraphResource::AnimGraphResource(const FieldInitializer* f) : Resource(f)
    {
    }
    AnimGraphResource::AnimGraphResource(const IUnknown* source) : Resource(source)
    {
    }
    AnimGraphResource::~AnimGraphResource()
    {
        Finalize();
    }
    void AnimGraphResource::Initialize()
    {
        Resource::Initialize();
    }
    void AnimGraphResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }
    bool AnimGraphResource::Load()
    {
        SetResourceState(m_Data ? ResourceState::Loaded : ResourceState::Failed);
        return bool(m_Data);
    }
    void AnimGraphResource::Unload()
    {
        m_Data.reset();
        SetResourceState(ResourceState::Unloaded);
    }
    bool AnimGraphResource::Compile(const Container::String& json, const SkeletonResource& skeleton,
                                    const Animation::IClipResolver& resolver, Animation::AnimGraphReport& report)
    {
        if (IsLoaded())
        {
            report.Error = Animation::AnimGraphError::InvalidSchema;
            report.Detail = _T("graph_loaded");
            return false;
        }
        Container::TSharedPtr<const Animation::AnimGraphData> candidate;
        if (!Animation::CompileAnimGraph(json, skeleton, resolver, candidate, report))
        {
            return false;
        }
        m_Data = std::move(candidate);
        return true;
    }
    size_t AnimGraphResource::GetMemorySize() const
    {
        using namespace Animation;
        size_t bytes = sizeof(*this);
        if (!m_Data)
        {
            return bytes;
        }
        bytes += sizeof(AnimGraphData) + m_Data->Parameters.capacity() * sizeof(AnimParamDefinition) +
                 m_Data->Masks.capacity() * sizeof(BoneMask) + m_Data->MaskNames.capacity() * sizeof(Identity) +
                 m_Data->Nodes.capacity() * sizeof(AnimGraphNode) +
                 m_Data->EvaluationOrder.capacity() * sizeof(uint32_t) +
                 m_Data->Clips.capacity() * sizeof(Container::TSharedPtr<AnimationClipResource>) +
                 m_Data->Parents.capacity() * sizeof(int32_t) + m_Data->JointNames.capacity() * sizeof(Identity);
        bytes += m_Data->SyncGroups.capacity() * sizeof(AnimSyncGroupDefinition);
        for (const auto& group : m_Data->SyncGroups)
            bytes += group.Markers.capacity() * sizeof(Identity);
        for (const auto& mask : m_Data->Masks)
        {
            bytes += mask.Weights.capacity() * sizeof(float);
        }
        for (const auto& n : m_Data->Nodes)
        {
            bytes += n.Children.capacity() * sizeof(uint32_t) +
                     (n.AxisX.capacity() + n.AxisY.capacity()) * sizeof(float) +
                     n.Layers.capacity() * sizeof(AnimLayer) + n.States.capacity() * sizeof(AnimState) +
                     n.Transitions.capacity() * sizeof(AnimTransition);
            for (const auto& t : n.Transitions)
            {
                bytes += t.Conditions.capacity() * sizeof(AnimCondition);
            }
        }
        return bytes;
    }
} // namespace NorvesLib::Core
