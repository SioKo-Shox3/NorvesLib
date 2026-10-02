#include "Input/InputBindingJson.h"
#include "Input/InputBindingNames.h"
#include "Text/JsonDocument.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <clocale>
#include <cmath>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    const char* DefaultJson=R"({"schema":"bindings.v1","contexts":[
      {"id":"Gameplay","cursor":"locked","actions":[
        {"id":"Move","type":"axis2d","deadzone":0.2,"curve":{"kind":"power","gamma":2},"bindings":[
          {"source":"key","code":"W","component":"y"},{"source":"key","code":"S","component":"y","scale":-1},
          {"source":"gamepad_axis","code":"left_x"},{"source":"gamepad_axis","code":"left_y","component":"y"}]},
        {"id":"Look","type":"axis2d","output":"frame_delta","mouse_sensitivity":0.25,"rate_sensitivity":120,"bindings":[
          {"source":"mouse_delta","code":"x"},{"source":"mouse_delta","code":"y","component":"y","invert":true}]},
        {"id":"Jump","type":"button","timing":{"tap":0.2,"double_tap_gap":0.3,"hold":0.5},"bindings":[
          {"source":"key","code":"Space"},{"source":"gamepad_button","code":"a","slot":1},
          {"source":"mouse_button","code":"left","modifiers":["ctrl"]}]},
        {"id":"Throttle","type":"axis1d","bindings":[{"source":"gamepad_trigger","code":"right","slot":3}]},
        {"id":"Next","type":"button","bindings":[{"source":"mouse_wheel","code":"vertical"}]}
      ]},
      {"id":"Menu","actions":[{"id":"Confirm","type":"button","bindings":[{"source":"key","code":"Enter"}]}]}
    ]})";
    Container::String Canonical(const InputBindingSet& set)
    {
        Container::String text;assert(InputBindingJson::WriteDefaults(set,text));return text;
    }
    Container::String OverrideAction(const char* fields)
    {
        Container::String text=R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[{"id":"Jump",)";
        text.append(fields);text.append("}]}]}");return text;
    }
    void RejectOverride(const InputBindingSet& defaults,const Container::String& json)
    {
        InputBindingSet out;
        assert(out.AddContext("DiscardMe"_id,ECursorMode::Normal));
        InputBindingJsonReport report;
        assert(!InputBindingJson::ApplyOverrides(defaults,json,out,&report));
        assert(!report.Error.empty() && Canonical(out)==Canonical(defaults));
    }
}
int main()
{
    InputBindingSet defaults;InputBindingJsonReport report;
    assert(InputBindingJson::ParseDefaults(DefaultJson,defaults,&report));
    assert(report.Error.empty() && report.WarningCount==0);
    assert(defaults.GetContexts().size()==2 && defaults.FindContext("Gameplay"_id)->CursorMode==ECursorMode::Locked);
    assert(defaults.FindAction("Gameplay"_id,"Move"_id)->Settings.AxisResponse.DeadZone==0.2f);
    assert(defaults.FindAction("Gameplay"_id,"Jump"_id)->Bindings[1].Source.Slot==1);
    const auto canonical=Canonical(defaults);
    InputBindingSet roundtrip;assert(InputBindingJson::ParseDefaults(canonical,roundtrip));assert(Canonical(roundtrip)==canonical);
    Container::String bom="\xEF\xBB\xBF";bom.append(DefaultJson);
    assert(InputBindingJson::ParseDefaults(bom,roundtrip));assert(Canonical(roundtrip)==canonical);
    // object列挙はborrowed viewであり、非object/範囲外は空を返す。
    JsonDocument document;assert(JsonDocument::TryParse(canonical,document));
    const auto root=document.GetRoot();assert(root.GetObjectSize()==2 && root.GetMemberName(0)=="schema");
    assert(root.GetMemberValue(0).AsString()=="bindings.v1");
    assert(root.GetMemberName(99).empty() && !root.GetMemberValue(99).IsValid());
    assert(JsonValue{}.GetObjectSize()==0 && JsonValue{}.GetMemberName(0).empty());
    // 未知field/action/contextは無視し、警告を返す。未知actionの不正な内容は既知へ波及しない。
    const char* unknown=R"({"schema":"bindings.v1","future":{"anything":[1,2]},"contexts":[
      {"id":"FutureContext","actions":"ignored"},
      {"id":"Gameplay","future":true,"actions":[{"id":"FutureAction","bindings":"ignored"},
        {"id":"Jump","future":0,"timing":{"hold":0.75,"future":0}}]}]})";
    InputBindingSet changed;
    assert(InputBindingJson::ApplyOverrides(defaults,unknown,changed,&report));
    assert(report.WarningCount==6 && report.Error.empty());
    assert(changed.FindAction("Gameplay"_id,"Jump"_id)->Settings.ButtonTiming.HoldSeconds==0.75);
    assert(changed.FindAction("Gameplay"_id,"Jump"_id)->Bindings.size()==3);
    assert(!changed.FindContext("FutureContext"_id) && !changed.FindAction("Gameplay"_id,"FutureAction"_id));
    Container::String diff;assert(InputBindingJson::WriteOverrides(defaults,changed,diff));
    assert(JsonDocument::TryParse(diff,document));
    auto action=document.GetRoot().FindMember("contexts").GetArrayElement(0).FindMember("actions").GetArrayElement(0);
    assert(action.FindMember("timing").FindMember("hold").AsNumber()==0.75);
    assert(!action.HasMember("bindings") && !action.HasMember("type") && !action.HasMember("mouse_sensitivity"));
    assert(InputBindingJson::ApplyOverrides(defaults,diff,roundtrip));assert(Canonical(roundtrip)==Canonical(changed));
    assert(InputBindingJson::ApplyOverrides(defaults,R"({"schema":"bindings.v1","contexts":[],"":0})",roundtrip,&report));
    assert(report.WarningCount==1 && report.Error.empty() && Canonical(roundtrip)==canonical);
    InputBindingSet emptyDefaults;
    assert(InputBindingJson::ParseDefaults(R"({"schema":"bindings.v1","contexts":[],"":0})",emptyDefaults,&report));
    assert(report.WarningCount==1 && emptyDefaults.GetContexts().empty());
    // 明示空bindingは解除。absentとは違い、差分往復でも消えない。
    assert(InputBindingJson::ApplyOverrides(defaults,OverrideAction("\"bindings\":[]"),changed));
    assert(changed.FindAction("Gameplay"_id,"Jump"_id)->Bindings.empty());
    assert(InputBindingJson::WriteOverrides(defaults,changed,diff));
    assert(InputBindingJson::ApplyOverrides(defaults,diff,roundtrip));assert(Canonical(roundtrip)==Canonical(changed));
    assert(InputBindingJson::WriteOverrides(defaults,defaults,diff));
    assert(JsonDocument::TryParse(diff,document));assert(document.GetRoot().FindMember("contexts").GetArraySize()==0);
    assert(InputBindingJson::ApplyOverrides(defaults,diff,roundtrip));assert(Canonical(roundtrip)==canonical);
    // 数値codeの変換はfinite/整数/範囲の検証後。mouse code 0は有効、key Noneは無効。
    assert(InputBindingJson::ApplyOverrides(defaults,OverrideAction(R"("bindings":[{"source":"mouse_button","code":0}])"),changed));
    assert(changed.FindAction("Gameplay"_id,"Jump"_id)->Bindings[0].Source.Kind==EInputBindingSource::MouseButton);
    for(const char* code:{"-1","0.5","65536","4294967297","1e999","1e-9999","null","true","\"Unknown\""})
    {
        Container::String fields=R"("bindings":[{"source":"key","code":)";fields.append(code);fields.append("}]");
        RejectOverride(defaults,OverrideAction(fields.c_str()));
    }
    for(const char* fields:{
        R"("bindings":[{"source":"key","code":0}])",
        R"("bindings":[{"source":"key","code":"Space","slot":1}])",
        R"("bindings":[{"source":"gamepad_button","code":3}])",
        R"("bindings":[{"source":"gamepad_button","code":"a","slot":256}])",
        R"("bindings":[{"source":"gamepad_button","code":"a","slot":1.2}])",
        R"("bindings":[{"source":"key","code":"Space","modifiers":["super"]}])",
        R"("bindings":[{"source":"key","code":"Space","modifiers":["shift","shift"]}])",
        R"("bindings":[{"source":"key","code":"Space","component":"y"}])",
        R"("bindings":[{"source":"key","code":"Space","threshold":2}])",
        R"("bindings":[{"source":"key","code":"Space","invert":1}])",
        R"("bindings":[{"source":"key","source":"mouse_button","code":"Space"}])",
        R"("bindings":null)",R"("bindings":true)",R"("type":"axis1d")",R"("output":"frame_delta")",
        R"("deadzone":1)",R"("curve":{"gamma":0})",R"("curve":{"expo":2})",R"("curve":{"kind":"unknown"})",
        R"("timing":{"hold":0})",R"("timing":{"tap":-1})",R"("mouse_sensitivity":1e100)",R"("rate_sensitivity":-1)",
        R"("bindings":[{"source":"key","code":"Space","threshold":1.000000001}])",
        R"("bindings":[{"source":"key","code":"Space","threshold":-1e-100}])",
        R"("deadzone":-1e-100)",R"("mouse_sensitivity":-1e-100)",R"("rate_sensitivity":-1e-100)",
        R"("curve":{"expo":1.000000001})",R"("curve":{"expo":-1e-100})",R"("curve":{"gamma":-1e-100})",
        R"("timing":{"tap":0.2,"tap":0.3})"}) RejectOverride(defaults,OverrideAction(fields));
    for(const char* bad:{"", "{", "[]",R"({"schema":"bindings.v2","contexts":[]})",
        R"({"schema":"bindings.v1","schema":"bindings.v1","contexts":[]})",
        R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[]},{"id":"Gameplay","actions":[]}]})",
        R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[{"id":"Jump"},{"id":"Jump"}]}]})"})
    {
        RejectOverride(defaults,bad);
        auto preserved=defaults;
        assert(!InputBindingJson::ParseDefaults(bad,preserved,&report));assert(Canonical(preserved)==canonical);
    }
    for(const char* fields:{
        R"("bindings":[],"deadzone":-1e-100)",R"("bindings":[],"mouse_sensitivity":-1e-100)",
        R"("bindings":[],"rate_sensitivity":-1e-100)",R"("bindings":[],"curve":{"expo":1.000000001})",
        R"("bindings":[],"curve":{"gamma":1e-100})",
        R"("bindings":[{"source":"key","code":"Space","threshold":1.000000001}])"})
    {
        Container::String bad=R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[{"id":"Jump","type":"button",)";
        bad.append(fields);bad.append("}]}]}");
        auto preserved=defaults;
        assert(!InputBindingJson::ParseDefaults(bad,preserved,&report));
        assert(!report.Error.empty() && Canonical(preserved)==canonical);
    }
    // 先に有効なactionを書き換えても、後続に不正値があれば全体を既定へ戻す。
    RejectOverride(defaults,R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[
        {"id":"Jump","bindings":[]},{"id":"Look","deadzone":2}]}]})");
    auto alias=defaults;assert(!InputBindingJson::ApplyOverrides(alias,"broken",alias));assert(Canonical(alias)==canonical);
    assert(InputBindingJson::ApplyOverrides(alias,OverrideAction("\"bindings\":[]"),alias));
    assert(alias.FindAction("Gameplay"_id,"Jump"_id)->Bindings.empty());
    report.Error=DefaultJson;assert(InputBindingJson::ParseDefaults(report.Error,roundtrip,&report));assert(Canonical(roundtrip)==canonical);
    // 過大/深すぎるtextをparser前に拒否し、文字列中の括弧は深さに数えない。
    Container::String large(InputBindingJson::MaximumTextBytes+1,' ');RejectOverride(defaults,large);
    Container::String deep;for(size_t i=0;i<InputBindingJson::MaximumDepth+1;++i) deep.push_back('[');
    for(size_t i=0;i<InputBindingJson::MaximumDepth+1;++i) deep.push_back(']');RejectOverride(defaults,deep);
    assert(InputBindingJson::ApplyOverrides(defaults,R"({"schema":"bindings.v1","contexts":[],"unknown":"[[{\"}]]"})",roundtrip));
    // 差分で表現できない構造/型変更は書き出さず、既存textを維持する。
    Container::String preservedText="keep";InputBindingSet different;
    assert(!InputBindingJson::WriteOverrides(defaults,different,preservedText) && preservedText=="keep");
    different=defaults;auto typeChanged=*different.FindAction("Gameplay"_id,"Jump"_id);
    typeChanged.Settings.Type=EInputMappingValueType::Axis1D;
    assert(different.ReplaceAction("Gameplay"_id,typeChanged));
    assert(!InputBindingJson::WriteOverrides(defaults,different,preservedText) && preservedText=="keep");
    InputBindingSet invalidName;assert(invalidName.AddContext(Identity("bad\nname"),ECursorMode::Normal));
    assert(!InputBindingJson::WriteDefaults(invalidName,preservedText) && preservedText=="keep");
    // JsonDocumentの数値はC localeを変更しても全tokenを読む。変更不能ならそのlocaleだけskip。
    Container::String oldLocale=std::setlocale(LC_NUMERIC,nullptr);
    for(const char* locale:{"de_DE.UTF-8","German_Germany.1252","fr_FR.UTF-8"})
    {
        if(!std::setlocale(LC_NUMERIC,locale)) continue;
        assert(JsonDocument::TryParse("{\"n\":0.5}",document));assert(document.GetRoot().FindMember("n").AsNumber()==0.5);
    }
    assert(std::setlocale(LC_NUMERIC,oldLocale.c_str()));
    assert(!JsonDocument::TryParse("{\"n\":1e9999}",document));
    assert(!JsonDocument::TryParse("{\"n\":1e-9999}",document));
    assert(JsonDocument::TryParse("{\"n\":4.9406564584124654e-324}",document));
    assert(document.GetRoot().FindMember("n").AsNumber()>0);
    std::cout << "InputBindingJsonTest passed\n";
    return 0;
}
