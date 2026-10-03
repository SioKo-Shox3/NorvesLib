#include "Resource/SkeletalImportPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Skeletal;
int main()
{
    SkeletalGltfDecodeOptions options;
    assert(IsValidSkeletalGltfDecodeOptions(options));
    const auto strict=EncodeSkeletalImportPolicy(options);
    assert(strict.bValid && strict.Size==0);
    for (uint64_t state : {uint64_t{0},uint64_t{14695981039346656037ull},uint64_t{0x123456789abcdef0ull}})
    {
        const auto hash=AppendSkeletalImportPolicyHash(state,options);
        assert(hash.bValid && hash.Value==state);
        assert(AppendSkeletalImportPolicyHash(state,options,99).Value==state);
    }
    options.WarnDroppedWeight=0.02;
    assert(!IsValidSkeletalGltfDecodeOptions(options));
    options={}; options.InfluencePolicy=SkeletalInfluencePolicy::ReduceToFour;
    const uint8_t expected[]={0x53,0x52,0x45,0x44,1,0,0,0,1,0x7b,0x14,0xae,0x47,0xe1,0x7a,0x84,0x3f,0,0,0,0,0,0,0xd0,0x3f};
    const auto canonical=EncodeSkeletalImportPolicy(options);
    assert(canonical.bValid && canonical.Size==sizeof(expected) && std::memcmp(canonical.Bytes,expected,sizeof(expected))==0);
    assert(AppendSkeletalImportPolicyHash(14695981039346656037ull,options).Value==0x7813d354b498e3c6ull);
    assert(AppendSkeletalImportPolicyHash(0,options).Value==0xb172c080088e7165ull);
    assert(AppendSkeletalImportPolicyHash(0x123456789abcdef0ull,options).Value==0x26ac6979f1fdea95ull);
    const auto initial=AppendSkeletalImportPolicyHash(0,options).Value;
    assert(AppendSkeletalImportPolicyHash(0,options,2).Value!=initial);
    options.WarnDroppedWeight=0.02;
    assert(AppendSkeletalImportPolicyHash(0,options).Value!=initial);
    options.WarnDroppedWeight=0.01; options.FailDroppedWeight=0.5;
    assert(AppendSkeletalImportPolicyHash(0,options).Value!=initial);
    options.WarnDroppedWeight=0; options.FailDroppedWeight=0;
    const auto zero=EncodeSkeletalImportPolicy(options);
    options.WarnDroppedWeight=-0.0; options.FailDroppedWeight=-0.0;
    const auto negativeZero=EncodeSkeletalImportPolicy(options);
    assert(negativeZero.bValid && std::memcmp(zero.Bytes,negativeZero.Bytes,sizeof(zero.Bytes))==0);
    for (double invalid : {-1.0,2.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        options={}; options.InfluencePolicy=SkeletalInfluencePolicy::ReduceToFour; options.FailDroppedWeight=invalid;
        assert(!IsValidSkeletalGltfDecodeOptions(options) && !EncodeSkeletalImportPolicy(options).bValid &&
            !AppendSkeletalImportPolicyHash(123,options).bValid);
    }
    options={}; options.InfluencePolicy=static_cast<SkeletalInfluencePolicy>(255);
    assert(!IsValidSkeletalGltfDecodeOptions(options));
    options={}; options.InfluencePolicy=SkeletalInfluencePolicy::ReduceToFour; options.WarnDroppedWeight=0.3;
    assert(!IsValidSkeletalGltfDecodeOptions(options));
    options.WarnDroppedWeight=1; options.FailDroppedWeight=1;
    assert(IsValidSkeletalGltfDecodeOptions(options));
    SkeletalGltfDecodeReport report;
    assert(report.TotalVertexCount==0 && report.ProcessedVertexCount==0 && !report.bInfluenceScanComplete &&
        report.FailedVertexIndex==std::numeric_limits<uint64_t>::max() && report.MeanDroppedWeight==0);
    std::cout << "SkeletalImportPolicyTest PASS: strict_identity_reduce_canonical_golden_hash_zero_invalid\n";
    return 0;
}
