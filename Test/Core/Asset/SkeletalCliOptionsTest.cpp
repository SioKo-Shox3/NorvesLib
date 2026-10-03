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
    const char* unknown[] = {"--cubicspline=bake"}; int index = 0; const char* error = nullptr;
    assert(ParseSkeletalArgument(1, unknown, index, options, error) == ImportArgumentResult::Unhandled && index == 0 && !error);
    std::cout << "SkeletalCliOptionsTest PASS: policy_thresholds_order_duplicates_invalid_atomicity_scope\n";
    return 0;
}
