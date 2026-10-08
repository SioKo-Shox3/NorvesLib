#include "Resource/SkeletalInfluenceAttributes.h"
#include "Text/JsonDocument.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Skeletal;
namespace
{
    Container::String Text(const char* source)
    {
        Container::String text;
        while (*source)
        {
            text.push_back(static_cast<Container::String::value_type>(*source++));
        }
        return text;
    }
}
int main()
{
    using Status=InfluenceSetCollectionStatus;
    Container::VariableArray<SkeletalInfluenceSet> sets;
    JsonDocument document;
    assert(JsonDocument::TryParse(Text(R"({"WEIGHTS_1":15,"POSITION":0,"JOINTS_0":3,"JOINTS_1":14,"WEIGHTS_0":5,"_JOINTS_5":99})"),document));
    assert(CollectSkeletalInfluenceSets(document.GetRoot(),sets)==Status::Success);
    assert(sets.size()==2 && sets[0].JointsAccessor==3 && sets[0].WeightsAccessor==5 &&
        sets[1].JointsAccessor==14 && sets[1].WeightsAccessor==15);
    assert(ValidateStrictInfluenceAttributes(document.GetRoot())==StrictInfluenceStatus::AdditionalSet);
    for (const char* invalid : {
        R"([])",R"({})",R"({"JOINTS_0":3})",R"({"WEIGHTS_0":5})",
        R"({"JOINTS_1":3,"WEIGHTS_1":5})",R"({"JOINTS_0":3,"WEIGHTS_0":5,"JOINTS_2":6,"WEIGHTS_2":7})",
        R"({"JOINTS_0":3,"WEIGHTS_0":5,"JOINTS_0":4})",R"({"JOINTS_0":3,"WEIGHTS_0":5,"JOINTS_01":6})",
        R"({"JOINTS_0":3,"WEIGHTS_0":null})",R"({"JOINTS_0":3,"WEIGHTS_0":-1})",
        R"({"JOINTS_0":3,"WEIGHTS_0":5.5})",R"({"JOINTS_0":3,"WEIGHTS_0":4294967296})",
        R"({"JOINTS_0":3,"WEIGHTS_0":true})",R"({"JOINTS_0":3,"WEIGHTS_0":"5"})"})
    {
        assert(JsonDocument::TryParse(Text(invalid),document));
        assert(CollectSkeletalInfluenceSets(document.GetRoot(),sets)!=Status::Success);
        assert(sets.size()==2 && sets[0].JointsAccessor==3 && sets[0].WeightsAccessor==5 &&
            sets[1].JointsAccessor==14 && sets[1].WeightsAccessor==15);
    }
    assert(JsonDocument::TryParse(Text(R"({"JOINTS_0":0,"WEIGHTS_0":4294967295})"),document));
    assert(CollectSkeletalInfluenceSets(document.GetRoot(),sets)==Status::Success && sets.size()==1 && sets[0].WeightsAccessor==4294967295u);
    // 番号の存在・accessor型/countは後段が検証する。ここは整数記述だけを所有する。
    document.Reset();
    assert(sets.size()==1 && sets[0].WeightsAccessor==4294967295u);
    std::cout << "SkeletalInfluenceSetsJsonTest PASS: unordered_attributes_pairs_indices_failure_preservation\n";
    return 0;
}
