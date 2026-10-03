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
    bool bSkip=false;
    assert(ParseSkipArgument("--skip-if-unchanged",bSkip,error)==ImportArgumentResult::Accepted && bSkip);
    assert(ParseSkipArgument("--skip-if-unchanged",bSkip,error)==ImportArgumentResult::Rejected && bSkip);
    for (const char* argument : {"--skip-if-unchanged=", "--skip-if-unchanged=true", "--skip-if-unchanged=false"})
    {
        bSkip=false;
        assert(ParseSkipArgument(argument,bSkip,error)==ImportArgumentResult::Rejected && !bSkip);
    }
    for (const char* argument : {"", "--skip", "--skip-if-unchange", "--skip-if-unchanged-later", "--input"})
    {
        bSkip=false;
        assert(ParseSkipArgument(argument,bSkip,error)==ImportArgumentResult::Unhandled && !bSkip);
    }
    std::filesystem::path inspectPath="retained";
    const char* inspectSeparate[]={"AssetCook","--inspect","a model.glb"};
    assert(ParseInspectCommandLine(3,inspectSeparate,inspectPath,error)==ImportArgumentResult::Accepted && inspectPath=="a model.glb");
    const char* inspectEquals[]={"AssetCook","--inspect=b.gltf"};
    assert(ParseInspectCommandLine(2,inspectEquals,inspectPath,error)==ImportArgumentResult::Accepted && inspectPath=="b.gltf");
    for (const char* argument : {"--inspect","--inspect=","--inspect=--out"})
    {
        const char* argv[]={"AssetCook",argument};
        assert(ParseInspectCommandLine(2,argv,inspectPath,error)==ImportArgumentResult::Rejected && inspectPath=="b.gltf");
    }
    const char* mixed[]={"AssetCook","--inspect","b.gltf","--out","bad"};
    assert(ParseInspectCommandLine(5,mixed,inspectPath,error)==ImportArgumentResult::Rejected && inspectPath=="b.gltf");
    const char* noInspect[]={"AssetCook","--input","b.gltf"};
    assert(ParseInspectCommandLine(3,noInspect,inspectPath,error)==ImportArgumentResult::Unhandled);
    std::cout << "ImportCliOptionsTest PASS: paths_flags_conflicts_duplicates_failure_preservation\n";
    return 0;
}
