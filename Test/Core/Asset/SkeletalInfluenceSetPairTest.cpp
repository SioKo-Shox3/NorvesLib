#include "Resource/SkeletalInfluenceAttributes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Skeletal;
int main()
{
    using Kind=InfluenceAttributeKind;
    const InfluenceAttributeAccessor two[]={ {Kind::Joints,0,3},{Kind::Weights,0,5},{Kind::Joints,1,14},{Kind::Weights,1,15} };
    const auto valid=CheckSortedInfluenceSetPairs(two);
    assert(valid.bValid && valid.SetCount==2);
    assert(CheckSortedInfluenceSetPairs({two,2}).bValid);
    assert(!CheckSortedInfluenceSetPairs({}).bValid);
    assert(!CheckSortedInfluenceSetPairs({nullptr,size_t{2}}).bValid);
    assert(!CheckSortedInfluenceSetPairs({two,3}).bValid);
    const InfluenceAttributeAccessor missing[]={ {Kind::Joints,0,3},{Kind::Weights,1,5} };
    assert(!CheckSortedInfluenceSetPairs(missing).bValid);
    const InfluenceAttributeAccessor duplicate[]={ {Kind::Joints,0,3},{Kind::Joints,0,4} };
    assert(!CheckSortedInfluenceSetPairs(duplicate).bValid);
    const InfluenceAttributeAccessor gap[]={ {Kind::Joints,1,3},{Kind::Weights,1,5} };
    assert(!CheckSortedInfluenceSetPairs(gap).bValid);
    const InfluenceAttributeAccessor skipped[]={ {Kind::Joints,0,3},{Kind::Weights,0,5},{Kind::Joints,2,14},{Kind::Weights,2,15} };
    assert(!CheckSortedInfluenceSetPairs(skipped).bValid);
    const InfluenceAttributeAccessor reversed[]={ {Kind::Weights,0,5},{Kind::Joints,0,3} };
    assert(!CheckSortedInfluenceSetPairs(reversed).bValid);
    const InfluenceAttributeAccessor huge[]={ {Kind::Joints,4294967295u,3},{Kind::Weights,4294967295u,5} };
    assert(!CheckSortedInfluenceSetPairs(huge).bValid);
    const InfluenceAttributeAccessor other[]={ {Kind::Other,0,3},{Kind::Weights,0,5} };
    assert(!CheckSortedInfluenceSetPairs(other).bValid);
    assert(!CheckSortedInfluenceSetPairs({two,std::numeric_limits<size_t>::max()-1}).bValid);
    std::cout << "SkeletalInfluenceSetPairTest PASS: consecutive_pairs_duplicates_gaps_bounds\n";
    return 0;
}
