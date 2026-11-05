#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/Procedural/BodyLeanModifier.h"
#include "Animation/Procedural/SpineWaveModifier.h"
#include "Animation/Procedural/SpringBoneModifier.h"
#include "Animation/Procedural/FollowThroughModifier.h"
#include "Animation/SkeletonResource.h"
#include "Math/Curves.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation::Procedural
{
    namespace
    {
        using Error = PoseModifierConfigError;

        Container::String Text(const char* text)
        {
            Container::String result;
            for (const char* p = text; *p; ++p)
            {
                result.push_back(TCHAR(*p));
            }
            return result;
        }
        Container::String MemberPath(const Container::String& base, const Container::String& member)
        {
            auto path = base;
            if (!member.empty())
            {
                if (!path.empty() && member[0] != TCHAR('['))
                {
                    path.push_back(TCHAR('.'));
                }
                path += member;
            }
            return path;
        }
        Container::String ElementPath(const Container::String& base, size_t index)
        {
            auto path = base;
            path.push_back(TCHAR('['));
            TCHAR digits[3 * sizeof(size_t)];
            size_t count = 0;
            do
            {
                digits[count++] = TCHAR('0' + index % 10);
                index /= 10;
            } while (index);
            while (count)
            {
                path.push_back(digits[--count]);
            }
            path.push_back(TCHAR(']'));
            return path;
        }
        bool Fail(PoseModifierConfigReport& report, Error error, const Container::String& path)
        {
            if (report.Error == Error::None)
            {
                report.Error = error;
                report.Path = path;
            }
            return false;
        }
        bool Word(JsonValue value, const char* text)
        {
            if (!value.IsString() || !text)
            {
                return false;
            }
            const auto& string = value.AsString();
            size_t i = 0;
            for (; text[i]; ++i)
            {
                if (i >= string.size() || string[i] != TCHAR(text[i]))
                {
                    return false;
                }
            }
            return i == string.size();
        }
        bool Number(JsonValue value, float& out)
        {
            const double number = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(number) || std::fabs(number) > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = float(number);
            return true;
        }
        bool Integer(JsonValue value, int32_t& out)
        {
            const double number = value.AsNumber();
            if (!value.IsIntegerLiteral() || !std::isfinite(number) || number < INT32_MIN || number > INT32_MAX)
            {
                return false;
            }
            out = int32_t(number);
            return true;
        }
        bool Keys(JsonValue object, Container::Span<const char* const> allowed,
                  const Container::String& path, PoseModifierConfigReport& report)
        {
            if (!CheckPoseModifierKeys(object, allowed, report))
            {
                report.Path = MemberPath(path, report.Path);
                return false;
            }
            return true;
        }
        struct WeightConfig
        {
            float Constant = 1;
            Identity Parameter;
            Container::VariableArray<Math::CurveKeyframe> Curve;
        };
        class ConfiguredPoseModifier final : public IPoseModifier
        {
          public:
            ConfiguredPoseModifier(Container::TSharedPtr<IPoseModifier> modifier, int32_t order, bool bEnabled,
                                   WeightConfig weight)
                : m_Modifier(std::move(modifier)), m_Order(order), m_bEnabled(bEnabled), m_Weight(std::move(weight))
            {
            }
            int32_t Order() const override
            {
                return m_Order;
            }
            bool Apply(LocalPose& pose, PoseModifierContext& context) override
            {
                if (!m_bEnabled)
                {
                    return true;
                }
                float weight = m_Weight.Constant;
                if (!m_Weight.Curve.empty())
                {
                    const auto handle = context.Parameters.Find(m_Weight.Parameter);
                    const auto* definition = context.Parameters.Definition(handle);
                    const auto* value = context.Parameters.Get(handle);
                    double evaluated = 0;
                    if (!definition || definition->Type != AnimParamType::Float || !value ||
                        !Math::TryEvaluatePiecewiseLinear<Math::CurveKeyframe>(m_Weight.Curve, value->Float, evaluated))
                    {
                        return false;
                    }
                    weight = float(std::clamp(evaluated, 0.0, 1.0));
                }
                if (weight <= 0)
                {
                    return true;
                }
                // 中の Apply が失敗・例外で抜けても呼び出し元の重みを復元する。
                struct RestoreWeight
                {
                    float& Target;
                    float Previous;
                    ~RestoreWeight()
                    {
                        Target = Previous;
                    }
                } restore{context.Weight, context.Weight};
                context.Weight = weight;
                return m_Modifier->Apply(pose, context);
            }

          private:
            Container::TSharedPtr<IPoseModifier> m_Modifier;
            int32_t m_Order;
            bool m_bEnabled;
            WeightConfig m_Weight;
        };
        bool ReadWeight(JsonValue value, const AnimParamSet* params, WeightConfig& out,
                        const Container::String& path, PoseModifierConfigReport& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (value.IsNumber())
            {
                if (!Number(value, out.Constant) || value.AsNumber() < 0 || value.AsNumber() > 1)
                {
                    return Fail(report, Error::OutOfRange, path);
                }
                return true;
            }
            const char* const allowed[] = {"param", "curve"};
            if (!Keys(value, allowed, path, report))
            {
                return false;
            }
            const auto param = value.FindMember("param");
            const auto paramPath = MemberPath(path, Text("param"));
            if (!param.IsValid())
            {
                return Fail(report, Error::MissingKey, paramPath);
            }
            if (!param.IsString())
            {
                return Fail(report, Error::TypeMismatch, paramPath);
            }
            out.Parameter = Identity(param.AsString());
            const auto* definition = params ? params->Definition(params->Find(out.Parameter)) : nullptr;
            if (!definition || definition->Type != AnimParamType::Float)
            {
                return Fail(report, Error::UnknownParam, paramPath);
            }
            const auto curve = value.FindMember("curve");
            const auto curvePath = MemberPath(path, Text("curve"));
            if (!curve.IsValid())
            {
                return Fail(report, Error::MissingKey, curvePath);
            }
            if (!curve.IsArray())
            {
                return Fail(report, Error::TypeMismatch, curvePath);
            }
            if (curve.GetArraySize() < 2)
            {
                return Fail(report, Error::InvalidCurve, curvePath);
            }
            for (size_t i = 0; i < curve.GetArraySize(); ++i)
            {
                const auto key = curve.GetArrayElement(i);
                const auto keyPath = ElementPath(curvePath, i);
                if (!key.IsArray())
                {
                    return Fail(report, Error::TypeMismatch, keyPath);
                }
                if (key.GetArraySize() != 2)
                {
                    return Fail(report, Error::InvalidCurve, keyPath);
                }
                const auto time = key.GetArrayElement(0);
                const auto weight = key.GetArrayElement(1);
                if (!time.IsNumber())
                {
                    return Fail(report, Error::TypeMismatch, ElementPath(keyPath, 0));
                }
                if (!weight.IsNumber())
                {
                    return Fail(report, Error::TypeMismatch, ElementPath(keyPath, 1));
                }
                Math::CurveKeyframe parsed;
                parsed.Time = time.AsNumber();
                if (!std::isfinite(parsed.Time) || parsed.Time < 0)
                {
                    return Fail(report, Error::OutOfRange, ElementPath(keyPath, 0));
                }
                if (!Number(weight, parsed.Value) || weight.AsNumber() < 0 || weight.AsNumber() > 1)
                {
                    return Fail(report, Error::OutOfRange, ElementPath(keyPath, 1));
                }
                out.Curve.push_back(parsed);
            }
            return Math::IsValidPiecewiseLinearCurve<Math::CurveKeyframe>(out.Curve) ||
                   Fail(report, Error::InvalidCurve, curvePath);
        }
    } // namespace

    bool TryGetDefaultPoseModifierOrder(Identity name, int32_t& out)
    {
        const struct
        {
            const char* Name;
            int32_t Order;
        } orders[] = {
            {"Lean", PoseModifierDefaultOrder::Lean}, {"Spine", PoseModifierDefaultOrder::Spine},
            {"Look", PoseModifierDefaultOrder::Look}, {"FootIK", PoseModifierDefaultOrder::FootIK},
            {"SpringBone", PoseModifierDefaultOrder::SpringBone}, {"FollowThrough", PoseModifierDefaultOrder::FollowThrough}};
        for (const auto& order : orders)
        {
            if (name == Identity(order.Name))
            {
                out = order.Order;
                return true;
            }
        }
        return false;
    }
    PoseModifierTypeTable GetBuiltinPoseModifierTypes()
    {
        static const PoseModifierTypeEntry entries[] = {
            {"Lean", PoseModifierDefaultOrder::Lean, &CreateBodyLeanModifier},
            {"Spine", PoseModifierDefaultOrder::Spine, &CreateSpineWaveModifier},
            {"SpringBone", PoseModifierDefaultOrder::SpringBone, &CreateSpringBoneModifier},
            {"FollowThrough", PoseModifierDefaultOrder::FollowThrough, &CreateFollowThroughModifier}};
        return {entries};
    }
    bool ResolvePoseModifierJoint(JsonValue name, const SkeletonResource& skeleton, int32_t& outIndex,
                                  PoseModifierConfigReport& report)
    {
        if (!name.IsString())
        {
            return Fail(report, Error::TypeMismatch, {});
        }
        const auto index = skeleton.FindJointIndex(Identity(name.AsString()));
        if (index < 0)
        {
            return Fail(report, Error::UnknownJoint, {});
        }
        outIndex = index;
        return true;
    }
    bool CheckPoseModifierKeys(JsonValue object, Container::Span<const char* const> allowed,
                               PoseModifierConfigReport& report)
    {
        if (!object.IsObject())
        {
            return Fail(report, Error::TypeMismatch, {});
        }
        // 重複を先に全件調べ、未知キーとの同居時も重複を報告する。
        for (size_t i = 0; i < object.GetObjectSize(); ++i)
        {
            for (size_t j = 0; j < i; ++j)
            {
                if (object.GetMemberName(i) == object.GetMemberName(j))
                {
                    return Fail(report, Error::DuplicateKey, object.GetMemberName(i));
                }
            }
        }
        for (size_t i = 0; i < object.GetObjectSize(); ++i)
        {
            bool bKnown = false;
            for (const char* key : allowed)
            {
                if (key && object.GetMemberName(i) == Text(key))
                {
                    bKnown = true;
                    break;
                }
            }
            if (!bKnown)
            {
                return Fail(report, Error::UnknownKey, object.GetMemberName(i));
            }
        }
        return true;
    }
    bool ParsePoseModifierConfig(const Container::String& json, const SkeletonResource& skeleton,
                                 const AnimParamSet* params, const PoseModifierTypeTable& types,
                                 Container::VariableArray<Container::TSharedPtr<IPoseModifier>>& out,
                                 PoseModifierConfigReport& report)
    {
        report = {};
        JsonDocument document;
        if (!JsonDocument::TryParse(json, document))
        {
            return Fail(report, Error::InvalidJson, {});
        }
        const auto root = document.GetRoot();
        const char* const rootKeys[] = {"version", "modifiers"};
        if (!Keys(root, rootKeys, {}, report))
        {
            return false;
        }
        const auto version = root.FindMember("version");
        const auto versionPath = Text("version");
        if (!version.IsValid())
        {
            return Fail(report, Error::MissingKey, versionPath);
        }
        if (!version.IsIntegerLiteral())
        {
            return Fail(report, Error::TypeMismatch, versionPath);
        }
        int32_t parsedVersion;
        if (!Integer(version, parsedVersion) || parsedVersion != 1)
        {
            return Fail(report, Error::UnsupportedVersion, versionPath);
        }
        const auto modifiers = root.FindMember("modifiers");
        if (!modifiers.IsValid())
        {
            return Fail(report, Error::MissingKey, Text("modifiers"));
        }
        if (!modifiers.IsArray())
        {
            return Fail(report, Error::TypeMismatch, Text("modifiers"));
        }
        Container::VariableArray<Container::TSharedPtr<IPoseModifier>> candidate;
        const char* const modifierKeys[] = {"type", "order", "enabled", "weight", "settings"};
        for (size_t i = 0; i < modifiers.GetArraySize(); ++i)
        {
            const auto item = modifiers.GetArrayElement(i);
            const auto path = ElementPath(Text("modifiers"), i);
            if (!Keys(item, modifierKeys, path, report))
            {
                return false;
            }
            const auto type = item.FindMember("type");
            const auto typePath = MemberPath(path, Text("type"));
            if (!type.IsValid())
            {
                return Fail(report, Error::MissingKey, typePath);
            }
            if (!type.IsString())
            {
                return Fail(report, Error::TypeMismatch, typePath);
            }
            const PoseModifierTypeEntry* entry = nullptr;
            for (const auto& registered : types.Entries)
            {
                if (Word(type, registered.Name))
                {
                    entry = &registered;
                    break;
                }
            }
            if (!entry)
            {
                return Fail(report, Error::UnknownType, typePath);
            }
            int32_t order = entry->DefaultOrder;
            const auto orderValue = item.FindMember("order");
            const auto orderPath = MemberPath(path, Text("order"));
            if (orderValue.IsValid())
            {
                if (!orderValue.IsIntegerLiteral())
                {
                    return Fail(report, Error::TypeMismatch, orderPath);
                }
                if (!Integer(orderValue, order))
                {
                    return Fail(report, Error::OutOfRange, orderPath);
                }
            }
            else
            {
                (void)TryGetDefaultPoseModifierOrder(Identity(type.AsString()), order);
            }
            if (order < 0 || order > 10000)
            {
                return Fail(report, Error::OutOfRange, orderPath);
            }
            const auto enabledValue = item.FindMember("enabled");
            if (enabledValue.IsValid() && !enabledValue.IsBoolean())
            {
                return Fail(report, Error::TypeMismatch, MemberPath(path, Text("enabled")));
            }
            WeightConfig weight;
            if (!ReadWeight(item.FindMember("weight"), params, weight, MemberPath(path, Text("weight")), report))
            {
                return false;
            }
            const auto settings = item.FindMember("settings");
            const auto settingsPath = MemberPath(path, Text("settings"));
            if (settings.IsValid() && !settings.IsObject())
            {
                return Fail(report, Error::TypeMismatch, settingsPath);
            }
            if (!entry->Create)
            {
                return Fail(report, Error::CreateFailed, path);
            }
            Container::TSharedPtr<IPoseModifier> modifier;
            const bool bCreated = entry->Create({skeleton, settings}, modifier, report);
            if (report.Error != Error::None)
            {
                report.Path = MemberPath(settingsPath, report.Path);
                return false;
            }
            if (!bCreated || !modifier)
            {
                return Fail(report, Error::CreateFailed, path);
            }
            candidate.push_back(Container::MakeShared<ConfiguredPoseModifier>(
                std::move(modifier), order, enabledValue.AsBool(true), std::move(weight)));
        }
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
