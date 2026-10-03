#include "Tools/AssetCook/ImportCliOptions.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <initializer_list>

using namespace NorvesLib::Tools::AssetCook;
using Options = NorvesLib::Core::AssetImport::ImportSettingsFileOptions;
namespace
{
    bool Parse(std::initializer_list<const char*> args, Options& options)
    {
        for (int index=0; index<static_cast<int>(args.size()); ++index)
        {
            const char* error=nullptr;
            if (ParseImportArgument(static_cast<int>(args.size()),args.begin(),index,options,error) !=
                ImportArgumentResult::Accepted)
            {
                assert(error);
                return false;
            }
        }
        return true;
    }
}
int main()
{
    Options options;
    assert(!HasImportArguments(options));
    assert(Parse({"--import-settings","some path.json","--require-sidecar"},options));
    assert(options.OverridePath=="some path.json" && options.bRequired && !options.bDisabled);
    options={};
    assert(Parse({"--require-sidecar","--import-settings=other.json"},options));
    assert(options.OverridePath=="other.json" && options.bRequired);
    options={}; assert(Parse({"--no-sidecar"},options) && options.bDisabled);
    for (const char* invalid : {"--import-settings", "--import-settings=", "--no-sidecar=false",
        "--require-sidecar=true", "--no-sidecar=", "--require-sidecar="})
    {
        options={}; assert(!Parse({invalid},options)); assert(!HasImportArguments(options));
    }
    options={}; assert(!Parse({"--import-settings","--require-sidecar"},options));
    assert(!HasImportArguments(options));
    options={}; assert(!Parse({"--import-settings",""},options));
    for (const char* flag : {"--no-sidecar","--require-sidecar","--import-settings=config.json"})
    {
        options={}; assert(!Parse({flag,flag},options));
    }
    for (const char* other : {"--require-sidecar","--import-settings=config.json"})
    {
        options={}; assert(!Parse({"--no-sidecar",other},options));
        assert(options.bDisabled && !options.bRequired && options.OverridePath.empty());
        options={}; assert(!Parse({other,"--no-sidecar"},options)); assert(!options.bDisabled);
    }
    const char* unknown[]={"--inspect"}; int index=0; const char* error=nullptr;
    options={};
    assert(ParseImportArgument(1,unknown,index,options,error)==ImportArgumentResult::Unhandled);
    assert(index==0 && error==nullptr && !HasImportArguments(options));
    std::cout << "ImportCliOptionsTest PASS: paths_flags_conflicts_duplicates_failure_preservation\n";
    return 0;
}
