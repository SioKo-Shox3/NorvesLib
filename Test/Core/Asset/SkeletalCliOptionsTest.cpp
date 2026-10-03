#include "Tools/AssetCook/SkeletalCliOptions.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
#include <iostream>
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    bool Parse(std::initializer_list<const char*> arguments, SkeletalCliOptions& options, bool skeletal = true)
    {
        const char* error = nullptr;
        for (int index = 0; index < static_cast<int>(arguments.size()); ++index)
        {
            const auto result = ParseSkeletalArgument(static_cast<int>(arguments.size()), arguments.begin(), index, options, error);
            if (result != ImportArgumentResult::Accepted) return false;
        }
        return ValidateSkeletalArguments(options, skeletal, error);
    }
}
int main()
{
    SkeletalCliOptions options;
    assert(Parse({}, options) && !options.HasAny());
    assert(Parse({"--skin-influences", "strict"}, options));
    options = {};
    assert(Parse({"--skin-fail-dropped-weight=0.04", "--skin-influences=reduce", "--skin-warn-dropped-weight", "0.01"}, options));
    assert(options.Decode.InfluencePolicy == NorvesLib::Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour);
    assert(options.Decode.FailDroppedWeight == 0.04 && options.Decode.WarnDroppedWeight == 0.01);
    for (const char* invalid : {"", "nan", "inf", "-inf", "-0.1", "1.1", "0.1junk", " 0.1", "0.1 ", "+0.1", "0x1", "1e999"})
    {
        const char* argv[] = {"--skin-warn-dropped-weight", invalid};
        int index = 0; const char* error = nullptr;
        const auto saved = options;
        options.bWarnSpecified = false;
        const auto previous = options;
        assert(ParseSkeletalArgument(2, argv, index, options, error) == ImportArgumentResult::Rejected && index == 0 && error);
        assert(options.Decode.WarnDroppedWeight == previous.Decode.WarnDroppedWeight && options.bWarnSpecified == previous.bWarnSpecified);
        options = saved;
    }
    for (const auto arguments : {
        std::initializer_list<const char*>{"--skin-influences"},
        {"--skin-influences=auto"}, {"--skin-influences=reduce", "--skin-influences=strict"},
        {"--skin-influences=strict", "--skin-warn-dropped-weight=0.01"},
        {"--skin-warn-dropped-weight=0.01"},
        {"--skin-influences=reduce", "--skin-warn-dropped-weight=0.5"},
        {"--skin-influences=reduce", "--skin-fail-dropped-weight=0.001"},
        {"--skin-influences=reduce", "--skin-warn-dropped-weight=0", "--skin-warn-dropped-weight=0"},
        {"--skin-influences=reduce", "--skin-fail-dropped-weight=1", "--skin-fail-dropped-weight=1"},
        {"--skin-influences", "--skin-fail-dropped-weight=1"}})
    {
        options = {}; assert(!Parse(arguments, options));
    }
    options = {}; assert(!Parse({"--skin-influences=strict"}, options, false));
    options = {}; assert(!Parse({"--skin-influences=reduce"}, options, false));
    options = {}; assert(Parse({}, options, false));
    options = {}; assert(Parse({"--skin-influences=reduce", "--skin-warn-dropped-weight=-0", "--skin-fail-dropped-weight=1e0"}, options));
    const char* unknown[] = {"--morph=drop"}; int index = 0; const char* error = nullptr;
    assert(ParseSkeletalArgument(1, unknown, index, options, error) == ImportArgumentResult::Unhandled && index == 0 && !error);
    using namespace NorvesLib::Core::Skeletal;
    options = {};
    assert(Parse({"--cubicspline", "bake", "--cubic-translation-tolerance=0.002", "--cubic-rotation-tolerance-deg", "0.2",
        "--cubic-scale-tolerance=0.003", "--cubic-max-depth=0", "--cubic-max-channel-samples=100", "--cubic-max-asset-samples", "200"}, options));
    assert(options.Decode.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake && options.HasCubicSettings());
    assert(options.Decode.CubicTranslationToleranceMeters == .002 && options.Decode.CubicScaleTolerance == .003);
    assert(std::abs(options.Decode.CubicRotationToleranceRadians - .003490658503988659) < 1e-17);
    assert(options.Decode.CubicMaximumDepth == 0 && options.Decode.CubicMaximumSamplesPerChannel == 100 && options.Decode.CubicMaximumSamplesPerAsset == 200);
    options = {};
    assert(Parse({"--cubic-max-depth=24", "--cubic-max-channel-samples=1048576", "--cubic-max-asset-samples=4194304",
        "--cubic-rotation-tolerance-deg=180", "--cubicspline=bake", "--skin-influences=reduce"}, options));
    assert(options.Decode.CubicRotationToleranceRadians == 3.14159265358979323846);
    for (const auto arguments : {
        std::initializer_list<const char*>{"--cubicspline"}, {"--cubicspline=auto"}, {"--cubicspline=Bake"},
        {"--cubicspline=bake", "--cubicspline=bake"}, {"--cubic-max-depth=20"},
        {"--cubicspline=reject", "--cubic-translation-tolerance=0.001"},
        {"--cubicspline=bake", "--cubic-scale-tolerance=0"},
        {"--cubicspline=bake", "--cubic-scale-tolerance=0.000001"},
        {"--cubicspline=bake", "--cubic-rotation-tolerance-deg=181"},
        {"--cubicspline=bake", "--cubic-rotation-tolerance-deg=0.0001"},
        {"--cubicspline=bake", "--cubic-max-depth=25"},
        {"--cubicspline=bake", "--cubic-max-channel-samples=1"},
        {"--cubicspline=bake", "--cubic-max-channel-samples=1048577"},
        {"--cubicspline=bake", "--cubic-max-asset-samples=4194305"}})
    {
        options = {};
        assert(!Parse(arguments, options));
    }
    for (const char* flag : {"--cubic-translation-tolerance", "--cubic-rotation-tolerance-deg", "--cubic-scale-tolerance"})
    {
        for (const char* invalid : {"", "nan", "inf", "-inf", "-1", "0.1junk", " 0.1", "0.1 ", "+0.1", "0x1", "1e999"})
        {
            options = {};
            const char* args[] = {flag, invalid}; int position = 0;
            assert(ParseSkeletalArgument(2, args, position, options, error) == ImportArgumentResult::Rejected);
            assert(position == 0 && !options.HasAny());
            assert(options.Decode.CubicTranslationToleranceMeters == DefaultCubicTranslationToleranceMeters);
        }
        options = {};
        assert(Parse({"--cubicspline=bake", flag, "0.1"}, options));
        const auto previous = options;
        const char* args[] = {flag, "0.2"}; int position = 0;
        assert(ParseSkeletalArgument(2, args, position, options, error) == ImportArgumentResult::Rejected && position == 0);
        assert(options.Decode.CubicTranslationToleranceMeters == previous.Decode.CubicTranslationToleranceMeters &&
            options.Decode.CubicRotationToleranceRadians == previous.Decode.CubicRotationToleranceRadians &&
            options.Decode.CubicScaleTolerance == previous.Decode.CubicScaleTolerance);
    }
    for (const char* flag : {"--cubic-max-depth", "--cubic-max-channel-samples", "--cubic-max-asset-samples"})
    {
        for (const char* invalid : {"", "-1", "+2", "2.0", "2e0", " 2", "2 ", "nan", "4294967296", "2junk"})
        {
            options = {};
            const char* args[] = {flag, invalid}; int position = 0;
            assert(ParseSkeletalArgument(2, args, position, options, error) == ImportArgumentResult::Rejected && position == 0 && !options.HasAny());
        }
        options = {};
        assert(!Parse({"--cubicspline=bake", flag, "2", flag, "3"}, options));
    }
    options = {};
    assert(!Parse({"--cubicspline=bake"}, options, false));
    options = {};
    assert(!Parse({"--cubicspline=reject"}, options, false));
    options = {};
    assert(Parse({"--cubicspline=reject"}, options));
    std::cout << "SkeletalCliOptionsTest PASS: policy_thresholds_order_duplicates_invalid_atomicity_scope\n";
    return 0;
}
