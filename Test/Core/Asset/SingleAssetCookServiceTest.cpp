// 単体cookの公開要求と旧CLI境界を検証する。有効cookの全経路は実CLI byte比較でも追う。
#include "Tools/AssetCook/SingleAssetCook.h"
#include "Tools/AssetCook/AssetCookLegacyOptions.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    bool Equals(const Core::Container::AnsiString& value,const char* text)
    {
        const size_t size=std::strlen(text);
        return value.size()==size && (size==0 || std::memcmp(value.data(),text,size)==0);
    }
}
int main()
{
    SingleAssetCookRequest request;
    Core::Container::AnsiString error="held";
    CHECK(!CookSingleAsset(request,error) && Equals(error,"missing required arguments"));
    Detail::CookOptions options;
    options.InputPath="source.gltf";options.PackagePath="Cooked/rig.nvpkg";options.ManifestPath="manifest.json";
    options.LogicalPath="Models/rig.gltf";options.Kind="model";options.EntryName="__model__";
    options.EntryTypeText="Skl0";options.Format="nvskel.v0.skinned.pnujiw.u32";options.Variant="default";
    options.ImportSettings.OverridePath="sidecar.json";options.ImportSettings.bRequired=true;
    options.bSkipIfUnchanged=true;
    options.SkeletalImport.Decode.InfluencePolicy=Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour;
    options.SkeletalImport.Decode.WarnDroppedWeight=.02;
    options.SkeletalImport.Decode.CubicSplinePolicy=Core::Skeletal::SkeletalCubicSplinePolicy::Bake;
    options.SkeletalImport.Decode.CubicMaximumDepth=7;
    options.SkeletalImport.bPolicySpecified=true;options.SkeletalImport.bCubicSpecified=true;
    request=Detail::MakeSingleCookRequest(options);
    CHECK(request.InputPath==options.InputPath && request.PackagePath==options.PackagePath && request.ManifestPath==options.ManifestPath);
    CHECK(Equals(request.LogicalPath,"Models/rig.gltf") && Equals(request.Kind,"model") && Equals(request.EntryName,"__model__"));
    CHECK(Equals(request.EntryTypeText,"Skl0") && Equals(request.Format,"nvskel.v0.skinned.pnujiw.u32") && Equals(request.Variant,"default"));
    CHECK(request.ImportSettingsOverridePath==options.ImportSettings.OverridePath && request.bRequireSidecar && !request.bNoSidecar && request.bSkipIfUnchanged);
    CHECK(request.SkeletalDecode.InfluencePolicy==options.SkeletalImport.Decode.InfluencePolicy && request.SkeletalDecode.WarnDroppedWeight==.02);
    CHECK(request.SkeletalDecode.CubicSplinePolicy==options.SkeletalImport.Decode.CubicSplinePolicy && request.SkeletalDecode.CubicMaximumDepth==7);
    options.LogicalPath="changed";CHECK(Equals(request.LogicalPath,"Models/rig.gltf"));
    request={};request.InputPath="__not_read__";request.PackagePath="__not_written__";request.ManifestPath="__not_written_manifest__";
    request.LogicalPath="Raw/test.bin";request.Kind="unknown";request.EntryName="__raw__";
    request.EntryTypeText="Raw";request.Format="raw.v0";request.Variant="default";
    CHECK(!CookSingleAsset(request,error) && Equals(error,"--kind must be raw, texture, model, or audio"));
    std::puts("SingleAssetCookServiceTest PASS: request_ownership_options_validation");
    return 0;
}
