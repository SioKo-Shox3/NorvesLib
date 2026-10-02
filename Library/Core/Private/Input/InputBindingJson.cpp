#include "Input/InputBindingJson.h"
#include "Input/InputBindingNames.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include "Logging/LogMacros.h"
#include <initializer_list>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Input::InputBindingJson
{
    namespace
    {
        template<typename E> struct Choice { const char* Name; E Value; };
        constexpr Choice<ECursorMode> Cursors[]={{"normal",ECursorMode::Normal},{"hidden",ECursorMode::Hidden},{"confined",ECursorMode::Confined},{"locked",ECursorMode::Locked}};
        constexpr Choice<EInputMappingValueType> Types[]={{"button",EInputMappingValueType::Button},{"axis1d",EInputMappingValueType::Axis1D},{"axis2d",EInputMappingValueType::Axis2D}};
        constexpr Choice<EInputAxisOutput> Outputs[]={{"normalized",EInputAxisOutput::Normalized},{"frame_delta",EInputAxisOutput::FrameDelta}};
        constexpr Choice<EInputResponseCurve> Curves[]={{"linear",EInputResponseCurve::Linear},{"power",EInputResponseCurve::Power},{"expo",EInputResponseCurve::Expo}};
        constexpr Choice<EInputAxisComponent> Components[]={{"x",EInputAxisComponent::X},{"y",EInputAxisComponent::Y}};
        bool Fail(InputBindingJsonReport& report, const char* message)
        {
            report.Error=message;return false;
        }
        void Warn(InputBindingJsonReport& report, const char* kind, const Container::String& name)
        {
            if(report.WarningCount<16) NORVES_LOG_WARNING("Input", "未知の%sを無視します: %s",kind,name.empty()?"":name.c_str());
            ++report.WarningCount;
        }
        bool Fields(JsonValue object, std::initializer_list<const char*> allowed, InputBindingJsonReport& report)
        {
            if(!object.IsObject()) return Fail(report,"設定値がobjectではありません");
            for(size_t i=0;i<object.GetObjectSize();++i)
            {
                const auto& name=object.GetMemberName(i);
                bool known=false;for(const char* field:allowed) if(name==field) known=true;
                if(!known) { Warn(report,"field",name);continue; }
                for(size_t j=0;j<i;++j) if(object.GetMemberName(j)==name) return Fail(report,"既知fieldが重複しています");
            }
            return true;
        }
        template<typename E,size_t N> bool ReadChoice(JsonValue value, E& out, const Choice<E>(&choices)[N])
        {
            if(!value.IsValid()) return true;
            if(!value.IsString()) return false;
            for(const auto& choice:choices) if(value.AsString()==choice.Name) { out=choice.Value;return true; }
            return false;
        }
        template<typename E,size_t N> const char* ChoiceName(E value,const Choice<E>(&choices)[N])
        {
            for(const auto& choice:choices) if(choice.Value==value) return choice.Name;
            return nullptr;
        }
        bool ReadFloat(JsonValue value, float& out,
            double minimum=-static_cast<double>(std::numeric_limits<float>::max()),
            double maximum=static_cast<double>(std::numeric_limits<float>::max()),
            bool minimumExclusive=false,bool maximumExclusive=false)
        {
            if(!value.IsValid()) return true;
            if(!value.IsNumber()) return false;
            return TryParseInputFloatNumber(value.AsNumber(),out,minimum,maximum,minimumExclusive,maximumExclusive);
        }
        bool ReadDouble(JsonValue value, double& out)
        {
            if(!value.IsValid()) return true;
            if(!value.IsNumber() || !std::isfinite(value.AsNumber())) return false;
            out=value.AsNumber();return true;
        }
        bool ValidName(const char* text,size_t size)
        {
            if(!text || size==0) return false;
            for(size_t i=0;i<size;++i) if(static_cast<unsigned char>(text[i])<0x20) return false;
            return true;
        }
        bool ValidId(JsonValue value)
        {
            return value.IsString() && ValidName(value.AsString().data(),value.AsString().size());
        }
        bool SameName(Identity id,const Container::String& name)
        {
            const auto view=id.GetView();
            return view.size()==name.size() && std::memcmp(view.data(),name.data(),name.size())==0;
        }
        const InputContextDefinition* FindContextName(const InputBindingSet& settings,const Container::String& name)
        {
            for(const auto& context:settings.GetContexts()) if(SameName(context.Id,name)) return &context;
            return nullptr;
        }
        const InputActionDefinition* FindActionName(const InputContextDefinition& context,const Container::String& name)
        {
            for(const auto& action:context.Actions) if(SameName(action.Id,name)) return &action;
            return nullptr;
        }
        bool DuplicateId(JsonValue array,size_t index,const Container::String& name)
        {
            for(size_t i=0;i<index;++i)
            {
                const auto id=array.GetArrayElement(i).FindMember("id");
                if(id.IsString() && id.AsString()==name) return true;
            }
            return false;
        }
        bool Envelope(const Container::String& text,JsonDocument& doc,InputBindingJsonReport& report)
        {
            if(text.empty()) return Fail(report,"設定JSONが空です");
            if(text.size()>MaximumTextBytes) return Fail(report,"設定JSONが1MiBを超えています");
            size_t depth=0;bool quoted=false,escape=false;
            for(const auto ch:text)
            {
                if(quoted)
                {
                    if(escape) escape=false;
                    else if(ch=='\\') escape=true;
                    else if(ch=='"') quoted=false;
                    continue;
                }
                if(ch=='"') quoted=true;
                else if(ch=='{' || ch=='[') { if(++depth>MaximumDepth) return Fail(report,"設定JSONの入れ子が深すぎます"); }
                else if(ch=='}' || ch==']') { if(depth==0) return Fail(report,"設定JSONの括弧が不正です");--depth; }
            }
            if(quoted || depth!=0) return Fail(report,"設定JSONが途中で終わっています");
            const bool bom=text.size()>=3 && static_cast<unsigned char>(text[0])==0xEF &&
                static_cast<unsigned char>(text[1])==0xBB && static_cast<unsigned char>(text[2])==0xBF;
            if(!(bom?JsonDocument::TryParse(text.substr(3),doc,&report.Error):JsonDocument::TryParse(text,doc,&report.Error))) return false;
            const auto root=doc.GetRoot();
            if(!Fields(root,{"schema","contexts"},report)) return false;
            const auto schema=root.FindMember("schema");
            if(!schema.IsString() || schema.AsString()!="bindings.v1") return Fail(report,"対応していない入力設定schemaです");
            if(!root.FindMember("contexts").IsArray()) return Fail(report,"contextsが配列ではありません");
            return true;
        }
        bool ReadSettings(JsonValue value,InputActionSettings& settings,bool requireType,InputBindingJsonReport& report)
        {
            if(requireType && !value.HasMember("type")) return Fail(report,"actionのtypeがありません");
            if(!ReadChoice(value.FindMember("type"),settings.Type,Types) || !ReadChoice(value.FindMember("output"),settings.Output,Outputs) ||
                !ReadFloat(value.FindMember("deadzone"),settings.AxisResponse.DeadZone,0,1,false,true) ||
                !ReadFloat(value.FindMember("mouse_sensitivity"),settings.MouseSensitivity,0) ||
                !ReadFloat(value.FindMember("rate_sensitivity"),settings.RateSensitivity,0)) return Fail(report,"actionの型または感度が不正です");
            const auto curve=value.FindMember("curve");
            if(curve.IsValid())
            {
                if(!Fields(curve,{"kind","gamma","expo"},report)) return false;
                if(!ReadChoice(curve.FindMember("kind"),settings.AxisResponse.Curve,Curves) ||
                    !ReadFloat(curve.FindMember("gamma"),settings.AxisResponse.Gamma,0,std::numeric_limits<float>::max(),true) ||
                    !ReadFloat(curve.FindMember("expo"),settings.AxisResponse.Expo,0,1)) return Fail(report,"感度curveが不正です");
            }
            const auto timing=value.FindMember("timing");
            if(timing.IsValid())
            {
                if(!Fields(timing,{"tap","double_tap_gap","hold"},report)) return false;
                if(!ReadDouble(timing.FindMember("tap"),settings.ButtonTiming.TapMaxSeconds) ||
                    !ReadDouble(timing.FindMember("double_tap_gap"),settings.ButtonTiming.DoubleTapMaxGapSeconds) ||
                    !ReadDouble(timing.FindMember("hold"),settings.ButtonTiming.HoldSeconds)) return Fail(report,"buttonの時間設定が不正です");
            }
            return IsValidInputActionSettings(settings) || Fail(report,"action設定値が許容範囲外です");
        }
        bool ReadBinding(JsonValue value,InputBinding& binding,InputBindingJsonReport& report)
        {
            if(!Fields(value,{"source","code","slot","component","scale","invert","modifiers","threshold"},report)) return false;
            const auto source=value.FindMember("source"),code=value.FindMember("code");
            if(!source.IsString() || !TryParseInputSourceName(source.AsString().data(),source.AsString().size(),binding.Source.Kind)) return Fail(report,"入力sourceが不正です");
            const bool validCode=code.IsString()
                ? TryParseInputCodeName(binding.Source.Kind,code.AsString().data(),code.AsString().size(),binding.Source.Code)
                : code.IsNumber() && TryParseInputCodeNumber(binding.Source.Kind,code.AsNumber(),binding.Source.Code);
            if(!validCode) return Fail(report,"入力codeが不正です");
            const auto slot=value.FindMember("slot");
            if(slot.IsValid() && (!slot.IsNumber() || !TryParseInputSlotNumber(slot.AsNumber(),binding.Source.Slot))) return Fail(report,"入力slotが不正です");
            if(!ReadChoice(value.FindMember("component"),binding.Component,Components) || !ReadFloat(value.FindMember("scale"),binding.Scale) ||
                !ReadFloat(value.FindMember("threshold"),binding.ButtonThreshold,0,1)) return Fail(report,"bindingの値が不正です");
            const auto invert=value.FindMember("invert");
            if(invert.IsValid()) { if(!invert.IsBoolean()) return Fail(report,"invertがboolではありません");binding.Invert=invert.AsBool(); }
            const auto modifiers=value.FindMember("modifiers");
            if(modifiers.IsValid())
            {
                if(!modifiers.IsArray()) return Fail(report,"modifiersが配列ではありません");
                for(size_t i=0;i<modifiers.GetArraySize();++i)
                {
                    const auto modifier=modifiers.GetArrayElement(i);
                    if(!modifier.IsString()) return Fail(report,"modifier名が不正です");
                    InputModifierMask bit=0;
                    if(modifier.AsString()=="shift") bit=InputModifierShift;
                    else if(modifier.AsString()=="ctrl") bit=InputModifierCtrl;
                    else if(modifier.AsString()=="alt") bit=InputModifierAlt;
                    if(bit==0 || (binding.RequiredModifiers&bit)!=0) return Fail(report,"modifierが未知または重複しています");
                    binding.RequiredModifiers|=bit;
                }
            }
            return true;
        }
        bool ReadAction(JsonValue value,InputActionDefinition& action,bool defaults,InputBindingJsonReport& report)
        {
            if(!Fields(value,{"id","type","output","deadzone","curve","mouse_sensitivity","rate_sensitivity","timing","bindings"},report)) return false;
            const auto originalType=action.Settings.Type;const auto originalOutput=action.Settings.Output;
            if(!ReadSettings(value,action.Settings,defaults,report)) return false;
            if(!defaults && (action.Settings.Type!=originalType || action.Settings.Output!=originalOutput)) return Fail(report,"user差分ではactionの型/出力種別を変更できません");
            const auto bindings=value.FindMember("bindings");
            if(defaults || bindings.IsValid())
            {
                if(!bindings.IsArray()) return Fail(report,"bindingsが配列ではありません");
                Container::VariableArray<InputBinding> candidate;
                candidate.reserve(bindings.GetArraySize());
                for(size_t i=0;i<bindings.GetArraySize();++i)
                {
                    InputBinding binding;
                    if(!ReadBinding(bindings.GetArrayElement(i),binding,report)) return false;
                    candidate.push_back(binding);
                }
                action.Bindings=std::move(candidate);
            }
            return IsValidInputActionBindings(action.Settings,{action.Bindings.data(),action.Bindings.size()}) || Fail(report,"bindingとaction型の組合せが不正です");
        }
        bool ReadContexts(JsonValue contexts,InputBindingSet& settings,bool defaults,InputBindingJsonReport& report)
        {
            for(size_t i=0;i<contexts.GetArraySize();++i)
            {
                const auto value=contexts.GetArrayElement(i),id=value.FindMember("id");
                if(!ValidId(id)) return Fail(report,"contextのidが不正です");
                const auto* existing=FindContextName(settings,id.AsString());
                if(!defaults && !existing) { Warn(report,"context",id.AsString());continue; }
                if(DuplicateId(contexts,i,id.AsString())) return Fail(report,"contextのidが重複しています");
                if(!Fields(value,{"id","cursor","actions"},report)) return false;
                ECursorMode cursor=existing?existing->CursorMode:ECursorMode::Normal;
                if(!ReadChoice(value.FindMember("cursor"),cursor,Cursors)) return Fail(report,"cursor modeが不正です");
                const Identity contextId=defaults?Identity(id.AsString()):existing->Id;
                if(defaults)
                {
                    if(!settings.AddContext(contextId,cursor)) return Fail(report,"contextを追加できません");
                }
                else if(!settings.SetContextCursorMode(contextId,cursor)) return Fail(report,"contextを変更できません");
                const auto actions=value.FindMember("actions");
                if(!defaults && !actions.IsValid()) continue;
                if(!actions.IsArray()) return Fail(report,"actionsが配列ではありません");
                for(size_t j=0;j<actions.GetArraySize();++j)
                {
                    const auto actionValue=actions.GetArrayElement(j),actionId=actionValue.FindMember("id");
                    if(!ValidId(actionId)) return Fail(report,"actionのidが不正です");
                    const auto* prior=FindActionName(*settings.FindContext(contextId),actionId.AsString());
                    if(!defaults && !prior) { Warn(report,"action",actionId.AsString());continue; }
                    if(DuplicateId(actions,j,actionId.AsString())) return Fail(report,"actionのidが重複しています");
                    InputActionDefinition action=prior?*prior:InputActionDefinition{};
                    if(!ReadAction(actionValue,action,defaults,report)) return false;
                    if(defaults) action.Id=Identity(actionId.AsString());
                    if(!(defaults?settings.AddAction(contextId,action):settings.ReplaceAction(contextId,action))) return Fail(report,"action設定を適用できません");
                }
            }
            return true;
        }
        bool SameSettings(const InputActionSettings& a,const InputActionSettings& b)
        {
            return a.Type==b.Type && a.Output==b.Output && a.AxisResponse.DeadZone==b.AxisResponse.DeadZone &&
                a.AxisResponse.Curve==b.AxisResponse.Curve && a.AxisResponse.Gamma==b.AxisResponse.Gamma && a.AxisResponse.Expo==b.AxisResponse.Expo &&
                a.MouseSensitivity==b.MouseSensitivity && a.RateSensitivity==b.RateSensitivity &&
                a.ButtonTiming.TapMaxSeconds==b.ButtonTiming.TapMaxSeconds && a.ButtonTiming.DoubleTapMaxGapSeconds==b.ButtonTiming.DoubleTapMaxGapSeconds &&
                a.ButtonTiming.HoldSeconds==b.ButtonTiming.HoldSeconds;
        }
        bool SameBindings(const Container::VariableArray<InputBinding>& a,const Container::VariableArray<InputBinding>& b)
        {
            if(a.size()!=b.size()) return false;
            for(size_t i=0;i<a.size();++i)
            {
                const auto& x=a[i];const auto& y=b[i];
                if(x.Source.Kind!=y.Source.Kind || x.Source.Code!=y.Source.Code || x.Source.Slot!=y.Source.Slot || x.Component!=y.Component ||
                    x.Scale!=y.Scale || x.Invert!=y.Invert || x.RequiredModifiers!=y.RequiredModifiers || x.ButtonThreshold!=y.ButtonThreshold) return false;
            }
            return true;
        }
        bool SameAction(const InputActionDefinition& a,const InputActionDefinition& b)
        {
            return SameSettings(a.Settings,b.Settings) && SameBindings(a.Bindings,b.Bindings);
        }
        bool ValidSet(const InputBindingSet& settings)
        {
            for(const auto& context:settings.GetContexts())
            {
                if(!context.Id.IsValid() || !ValidName(context.Id.GetView().data(),context.Id.GetView().size()) || !IsValidCursorMode(context.CursorMode)) return false;
                for(const auto& action:context.Actions)
                    if(!InputBindingSet::IsValidAction(action) || !ValidName(action.Id.GetView().data(),action.Id.GetView().size())) return false;
            }
            return true;
        }
        void WriteSettings(JsonWriter& writer,const InputActionSettings& s,const InputActionSettings* base)
        {
            if(!base) { writer.WriteString("type",ChoiceName(s.Type,Types));writer.WriteString("output",ChoiceName(s.Output,Outputs)); }
            if(!base || s.AxisResponse.DeadZone!=base->AxisResponse.DeadZone) writer.WriteNumber("deadzone",s.AxisResponse.DeadZone);
            if(!base || s.MouseSensitivity!=base->MouseSensitivity) writer.WriteNumber("mouse_sensitivity",s.MouseSensitivity);
            if(!base || s.RateSensitivity!=base->RateSensitivity) writer.WriteNumber("rate_sensitivity",s.RateSensitivity);
            if(!base || s.AxisResponse.Curve!=base->AxisResponse.Curve || s.AxisResponse.Gamma!=base->AxisResponse.Gamma || s.AxisResponse.Expo!=base->AxisResponse.Expo)
            {
                writer.BeginObject("curve");
                if(!base || s.AxisResponse.Curve!=base->AxisResponse.Curve) writer.WriteString("kind",ChoiceName(s.AxisResponse.Curve,Curves));
                if(!base || s.AxisResponse.Gamma!=base->AxisResponse.Gamma) writer.WriteNumber("gamma",s.AxisResponse.Gamma);
                if(!base || s.AxisResponse.Expo!=base->AxisResponse.Expo) writer.WriteNumber("expo",s.AxisResponse.Expo);
                writer.EndObject();
            }
            if(!base || s.ButtonTiming.TapMaxSeconds!=base->ButtonTiming.TapMaxSeconds || s.ButtonTiming.DoubleTapMaxGapSeconds!=base->ButtonTiming.DoubleTapMaxGapSeconds || s.ButtonTiming.HoldSeconds!=base->ButtonTiming.HoldSeconds)
            {
                writer.BeginObject("timing");
                if(!base || s.ButtonTiming.TapMaxSeconds!=base->ButtonTiming.TapMaxSeconds) writer.WriteNumber("tap",s.ButtonTiming.TapMaxSeconds);
                if(!base || s.ButtonTiming.DoubleTapMaxGapSeconds!=base->ButtonTiming.DoubleTapMaxGapSeconds) writer.WriteNumber("double_tap_gap",s.ButtonTiming.DoubleTapMaxGapSeconds);
                if(!base || s.ButtonTiming.HoldSeconds!=base->ButtonTiming.HoldSeconds) writer.WriteNumber("hold",s.ButtonTiming.HoldSeconds);
                writer.EndObject();
            }
        }
        void WriteBindings(JsonWriter& writer,const Container::VariableArray<InputBinding>& bindings)
        {
            writer.BeginArray("bindings");
            for(const auto& binding:bindings)
            {
                writer.BeginObject();
                writer.WriteString("source",GetInputSourceName(binding.Source.Kind));
                writer.WriteString("code",GetInputCodeName(binding.Source.Kind,binding.Source.Code));
                writer.WriteNumber("slot",binding.Source.Slot);
                writer.WriteString("component",ChoiceName(binding.Component,Components));
                writer.WriteNumber("scale",binding.Scale);writer.WriteBool("invert",binding.Invert);
                writer.WriteNumber("threshold",binding.ButtonThreshold);
                writer.BeginArray("modifiers");
                if(binding.RequiredModifiers&InputModifierShift) writer.WriteStringElement("shift");
                if(binding.RequiredModifiers&InputModifierCtrl) writer.WriteStringElement("ctrl");
                if(binding.RequiredModifiers&InputModifierAlt) writer.WriteStringElement("alt");
                writer.EndArray();writer.EndObject();
            }
            writer.EndArray();
        }
        bool Serialize(const InputBindingSet& settings,const InputBindingSet* defaults,Container::String& out)
        {
            if(!ValidSet(settings) || (defaults && (!ValidSet(*defaults) || defaults->GetContexts().size()!=settings.GetContexts().size()))) return false;
            if(defaults) for(const auto& context:settings.GetContexts())
            {
                const auto* base=defaults->FindContext(context.Id);
                if(!base || base->Actions.size()!=context.Actions.size()) return false;
                for(const auto& action:context.Actions)
                {
                    const auto* original=defaults->FindAction(context.Id,action.Id);
                    if(!original || original->Settings.Type!=action.Settings.Type || original->Settings.Output!=action.Settings.Output) return false;
                }
            }
            JsonWriter writer(true);writer.BeginObject();writer.WriteString("schema","bindings.v1");writer.BeginArray("contexts");
            for(const auto& context:settings.GetContexts())
            {
                const auto* base=defaults?defaults->FindContext(context.Id):nullptr;
                bool changed=!base || base->CursorMode!=context.CursorMode;
                for(const auto& action:context.Actions)
                    if(!base || !SameAction(action,*defaults->FindAction(context.Id,action.Id))) changed=true;
                if(!changed) continue;
                writer.BeginObject();writer.WriteString("id",context.Id.ToString());
                if(!base || base->CursorMode!=context.CursorMode) writer.WriteString("cursor",ChoiceName(context.CursorMode,Cursors));
                writer.BeginArray("actions");
                for(const auto& action:context.Actions)
                {
                    const auto* original=base?defaults->FindAction(context.Id,action.Id):nullptr;
                    if(original && SameAction(action,*original)) continue;
                    writer.BeginObject();writer.WriteString("id",action.Id.ToString());
                    WriteSettings(writer,action.Settings,original?&original->Settings:nullptr);
                    if(!original || !SameBindings(action.Bindings,original->Bindings)) WriteBindings(writer,action.Bindings);
                    writer.EndObject();
                }
                writer.EndArray();writer.EndObject();
            }
            writer.EndArray();writer.EndObject();
            if(!writer.IsComplete()) return false;
            auto text=writer.ToString();
            if(text.size()>MaximumTextBytes) return false;
            out=std::move(text);return true;
        }
    }
    bool ParseDefaults(const Container::String& json,InputBindingSet& out,InputBindingJsonReport* report)
    {
        InputBindingJsonReport diagnostics;JsonDocument doc;InputBindingSet candidate;
        const bool success=Envelope(json,doc,diagnostics) && ReadContexts(doc.GetRoot().FindMember("contexts"),candidate,true,diagnostics);
        if(success) out=std::move(candidate);
        if(report) *report=std::move(diagnostics);
        return success;
    }
    bool ApplyOverrides(const InputBindingSet& defaults,const Container::String& json,InputBindingSet& out,InputBindingJsonReport* report)
    {
        InputBindingJsonReport diagnostics;JsonDocument doc;
        InputBindingSet fallback=defaults,candidate=defaults;
        const bool success=ValidSet(defaults) && Envelope(json,doc,diagnostics) && ReadContexts(doc.GetRoot().FindMember("contexts"),candidate,false,diagnostics);
        out=success?std::move(candidate):std::move(fallback);
        if(!success && diagnostics.Error.empty()) diagnostics.Error="既定設定が不正です";
        if(report) *report=std::move(diagnostics);
        return success;
    }
    bool WriteDefaults(const InputBindingSet& settings,Container::String& out) { return Serialize(settings,nullptr,out); }
    bool WriteOverrides(const InputBindingSet& defaults,const InputBindingSet& current,Container::String& out) { return Serialize(current,&defaults,out); }
}
