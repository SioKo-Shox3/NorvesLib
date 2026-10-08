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
    // SCBKはPython struct.packと独立FNVで作った固定値に合わせる。
    options={}; options.CubicSplinePolicy=SkeletalCubicSplinePolicy::Bake;
    assert(IsValidSkeletalGltfDecodeOptions(options));
    const uint8_t bakeExpected[]={0x53,0x43,0x42,0x4b,0x01,0x00,0x00,0x00,0x00,0x7b,0x14,0xae,0x47,0xe1,0x7a,0x84,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0x3f,0x01,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0xf5,0x61,0xb7,0x03,0x71,0x98,0x5c,0x3f,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0x14,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x00,0x00};
    const auto bakeCanonical=EncodeSkeletalImportPolicy(options);
    assert(bakeCanonical.bValid && bakeCanonical.Size==sizeof(bakeExpected) && std::memcmp(bakeCanonical.Bytes,bakeExpected,sizeof(bakeExpected))==0);
    assert(AppendSkeletalImportPolicyHash(0,options).Value==0x416f9c95032b3ba1ull);
    assert(AppendSkeletalImportPolicyHash(14695981039346656037ull,options).Value==0x3d87564da183137cull);
    assert(AppendSkeletalImportPolicyHash(0x123456789abcdef0ull,options).Value==0xb150b18b1dc91f31ull);
    const auto bakeHash=AppendSkeletalImportPolicyHash(0,options).Value;
    const auto different=[&](const SkeletalGltfDecodeOptions& changed)
    {
        const auto hash=AppendSkeletalImportPolicyHash(0,changed);
        assert(hash.bValid && hash.Value!=bakeHash);
    };
    auto changed=options; changed.CubicTranslationToleranceMeters=0.002; different(changed);
    changed=options; changed.CubicRotationToleranceRadians=0.003; different(changed);
    changed=options; changed.CubicScaleTolerance=0.002; different(changed);
    changed=options; changed.CubicMaximumDepth=19; different(changed);
    changed=options; changed.CubicMaximumSamplesPerChannel=100; different(changed);
    changed=options; changed.CubicMaximumSamplesPerAsset=50; different(changed);
    changed=options; changed.InfluencePolicy=SkeletalInfluencePolicy::ReduceToFour; different(changed);
    assert(AppendSkeletalImportPolicyHash(0,options,2).Value!=bakeHash);
    for(double invalid:{0.0,-1.0,CubicVectorNumericErrorFloor,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        changed=options; changed.CubicTranslationToleranceMeters=invalid;
        assert(!IsValidSkeletalGltfDecodeOptions(changed) && !EncodeSkeletalImportPolicy(changed).bValid);
        changed=options; changed.CubicScaleTolerance=invalid;
        assert(!IsValidSkeletalGltfDecodeOptions(changed));
    }
    for(double invalid:{0.0,CubicRotationNumericErrorBudget,3.2,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        changed=options; changed.CubicRotationToleranceRadians=invalid;
        assert(!IsValidSkeletalGltfDecodeOptions(changed));
    }
    changed=options; changed.CubicMaximumDepth=MaximumCubicSubdivisionDepth+1;
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicMaximumSamplesPerChannel=1;
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicMaximumSamplesPerChannel=MaximumCubicSamplesPerChannel+1;
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicMaximumSamplesPerAsset=1;
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicMaximumSamplesPerAsset=MaximumCubicSamplesPerAsset+1;
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicMaximumDepth=0; changed.CubicMaximumSamplesPerAsset=2;
    assert(IsValidSkeletalGltfDecodeOptions(changed));
    changed=options; changed.CubicSplinePolicy=static_cast<SkeletalCubicSplinePolicy>(255);
    assert(!IsValidSkeletalGltfDecodeOptions(changed));
    for(size_t field=0;field<6;++field)
    {
        changed={};
        switch(field)
        {
        case 0: changed.CubicTranslationToleranceMeters=0.002; break;
        case 1: changed.CubicRotationToleranceRadians=0.002; break;
        case 2: changed.CubicScaleTolerance=0.002; break;
        case 3: changed.CubicMaximumDepth=19; break;
        case 4: changed.CubicMaximumSamplesPerChannel=100; break;
        case 5: changed.CubicMaximumSamplesPerAsset=100; break;
        }
        assert(!IsValidSkeletalGltfDecodeOptions(changed));
    }
    assert(!report.bCubicScanStarted && !report.bCubicScanComplete && report.BakedCubicChannelCount==0 &&
        report.FailedAnimationChannelIndex==UINT64_MAX && !report.bHasCubicBakeFailure);
    // SMDP固定列とFNVはPython struct.packによる独立oracle。
    {
        options = {};
        options.MorphPolicy = SkeletalMorphPolicy::Drop;
        const uint8_t expectedDrop[] = {0x53,0x4d,0x44,0x50,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00};
        const auto encodedDrop = EncodeSkeletalImportPolicy(options);
        assert(encodedDrop.bValid && encodedDrop.Size == sizeof(expectedDrop) &&
            std::memcmp(encodedDrop.Bytes, expectedDrop, sizeof(expectedDrop)) == 0);
        assert(AppendSkeletalImportPolicyHash(0ull, options).Value == 0x3e404d7a724437ull);
        assert(AppendSkeletalImportPolicyHash(14695981039346656037ull, options).Value == 0xea60648fb9b11c24ull);
        assert(AppendSkeletalImportPolicyHash(1311768467463790320ull, options).Value == 0xa746ffec483e72e7ull);
        assert(AppendSkeletalImportPolicyHash(0, options, 2).Value != AppendSkeletalImportPolicyHash(0, options).Value);
        auto reject = options;
        reject.MorphPolicy = SkeletalMorphPolicy::Reject;
        assert(AppendSkeletalImportPolicyHash(0, reject).Value != AppendSkeletalImportPolicyHash(0, options).Value);
    }
    {
        options = {};
        options.MorphPolicy = SkeletalMorphPolicy::Drop;
        options.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
        const uint8_t expectedDrop[] = {0x53,0x4d,0x44,0x50,0x01,0x00,0x00,0x00,0x01,0x19,0x00,0x00,0x00,0x53,0x52,0x45,0x44,0x01,0x00,0x00,0x00,0x01,0x7b,0x14,0xae,0x47,0xe1,0x7a,0x84,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0x3f,0x01,0x00,0x00,0x00};
        const auto encodedDrop = EncodeSkeletalImportPolicy(options);
        assert(encodedDrop.bValid && encodedDrop.Size == sizeof(expectedDrop) &&
            std::memcmp(encodedDrop.Bytes, expectedDrop, sizeof(expectedDrop)) == 0);
        assert(AppendSkeletalImportPolicyHash(0ull, options).Value == 0xdcd48481ba4798d2ull);
        assert(AppendSkeletalImportPolicyHash(14695981039346656037ull, options).Value == 0x5902ec3d110af8f7ull);
        assert(AppendSkeletalImportPolicyHash(1311768467463790320ull, options).Value == 0xacc2c52e1b46a062ull);
        assert(AppendSkeletalImportPolicyHash(0, options, 2).Value != AppendSkeletalImportPolicyHash(0, options).Value);
        auto reject = options;
        reject.MorphPolicy = SkeletalMorphPolicy::Reject;
        assert(AppendSkeletalImportPolicyHash(0, reject).Value != AppendSkeletalImportPolicyHash(0, options).Value);
    }
    {
        options = {};
        options.MorphPolicy = SkeletalMorphPolicy::Drop;
        options.CubicSplinePolicy = SkeletalCubicSplinePolicy::Bake;
        const uint8_t expectedDrop[] = {0x53,0x4d,0x44,0x50,0x01,0x00,0x00,0x00,0x01,0x42,0x00,0x00,0x00,0x53,0x43,0x42,0x4b,0x01,0x00,0x00,0x00,0x00,0x7b,0x14,0xae,0x47,0xe1,0x7a,0x84,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0x3f,0x01,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0xf5,0x61,0xb7,0x03,0x71,0x98,0x5c,0x3f,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0x14,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00};
        const auto encodedDrop = EncodeSkeletalImportPolicy(options);
        assert(encodedDrop.bValid && encodedDrop.Size == sizeof(expectedDrop) &&
            std::memcmp(encodedDrop.Bytes, expectedDrop, sizeof(expectedDrop)) == 0);
        assert(AppendSkeletalImportPolicyHash(0ull, options).Value == 0x3a2649047c336611ull);
        assert(AppendSkeletalImportPolicyHash(14695981039346656037ull, options).Value == 0xc0e19686114cfbf6ull);
        assert(AppendSkeletalImportPolicyHash(1311768467463790320ull, options).Value == 0xb3b183bde97027e1ull);
        assert(AppendSkeletalImportPolicyHash(0, options, 2).Value != AppendSkeletalImportPolicyHash(0, options).Value);
        auto reject = options;
        reject.MorphPolicy = SkeletalMorphPolicy::Reject;
        assert(AppendSkeletalImportPolicyHash(0, reject).Value != AppendSkeletalImportPolicyHash(0, options).Value);
    }
    {
        options = {};
        options.MorphPolicy = SkeletalMorphPolicy::Drop;
        options.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
        options.CubicSplinePolicy = SkeletalCubicSplinePolicy::Bake;
        const uint8_t expectedDrop[] = {0x53,0x4d,0x44,0x50,0x01,0x00,0x00,0x00,0x01,0x42,0x00,0x00,0x00,0x53,0x43,0x42,0x4b,0x01,0x00,0x00,0x00,0x01,0x7b,0x14,0xae,0x47,0xe1,0x7a,0x84,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0x3f,0x01,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0xf5,0x61,0xb7,0x03,0x71,0x98,0x5c,0x3f,0xfc,0xa9,0xf1,0xd2,0x4d,0x62,0x50,0x3f,0x14,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00};
        const auto encodedDrop = EncodeSkeletalImportPolicy(options);
        assert(encodedDrop.bValid && encodedDrop.Size == sizeof(expectedDrop) &&
            std::memcmp(encodedDrop.Bytes, expectedDrop, sizeof(expectedDrop)) == 0);
        assert(AppendSkeletalImportPolicyHash(0ull, options).Value == 0xb07e25b49e56b358ull);
        assert(AppendSkeletalImportPolicyHash(14695981039346656037ull, options).Value == 0x5d4ea5781719d217ull);
        assert(AppendSkeletalImportPolicyHash(1311768467463790320ull, options).Value == 0xa64ea8c4b515e6a8ull);
        assert(AppendSkeletalImportPolicyHash(0, options, 2).Value != AppendSkeletalImportPolicyHash(0, options).Value);
        auto reject = options;
        reject.MorphPolicy = SkeletalMorphPolicy::Reject;
        assert(AppendSkeletalImportPolicyHash(0, reject).Value != AppendSkeletalImportPolicyHash(0, options).Value);
    }
    for (unsigned morph = 0; morph < 256; ++morph)
    {
        for (unsigned influence = 0; influence < 2; ++influence)
        {
            for (unsigned cubic = 0; cubic < 2; ++cubic)
            {
                options = {};
                options.MorphPolicy = static_cast<SkeletalMorphPolicy>(morph);
                options.InfluencePolicy = static_cast<SkeletalInfluencePolicy>(influence);
                options.CubicSplinePolicy = static_cast<SkeletalCubicSplinePolicy>(cubic);
                assert(IsValidSkeletalGltfDecodeOptions(options) == (morph < 2));
                assert(EncodeSkeletalImportPolicy(options).bValid == (morph < 2));
                assert(AppendSkeletalImportPolicyHash(0, options).bValid == (morph < 2));
            }
        }
    }
    options = {};
    options.MorphPolicy = SkeletalMorphPolicy::Drop;
    options.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
    options.FailDroppedWeight = std::numeric_limits<double>::infinity();
    assert(!EncodeSkeletalImportPolicy(options).bValid);
    options = {};
    options.MorphPolicy = SkeletalMorphPolicy::Drop;
    options.CubicSplinePolicy = SkeletalCubicSplinePolicy::Bake;
    options.CubicMaximumDepth = 25;
    assert(!EncodeSkeletalImportPolicy(options).bValid);
    assert(!report.bMorphScanComplete && report.DroppedMorphTargetCount == 0 &&
        report.DroppedMorphMeshWeightCount == 0 && report.DroppedMorphNodeWeightCount == 0 && report.DroppedMorphAnimationChannelCount == 0);
    std::cout << "SkeletalImportPolicyTest PASS: strict_identity_reduce_legacy_cubic_canonical_golden_hash_limits_invalid\n";
    return 0;
}
