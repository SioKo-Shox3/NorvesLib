// glTFのsource数値契約。発光換算前のdouble精度を保持する。
#include "Resource/GltfMaterialSource.h"
#include "Resource/MaterialImportPolicy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#undef assert
#define assert(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::fprintf(stderr,"assertion: %s line=%d\n",#expression,__LINE__); \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Core::Gltf;
using namespace NorvesLib::Core::AssetImport;
int main()
{
    MaterialSource source;
    assert(ValidateMaterialSource(source,0).Succeeded());
    assert(source.Metallic==1 && source.Roughness==1 && source.NormalScale==1 && source.OcclusionStrength==1 && source.AlphaCutoff==.5);
    assert(source.EmissiveStrength==1 && source.EmissiveFactor[0]==0 && !source.DoubleSided);
    source.NormalScale=-2;source.AlphaCutoff=1.25;
    assert(ValidateMaterialSource(source,0).Succeeded());
    source.Textures[4]={true,0};
    assert(ValidateMaterialSource(source,1).Succeeded());
    ImportedEmission emission;
    assert(ImportEmission(source.EmissiveFactor,source.EmissiveStrength,{},emission)==MaterialPolicyStatus::Success && !emission.Emitting);
    source.EmissiveFactor[0]=1;
    assert(ImportEmission(source.EmissiveFactor,source.EmissiveStrength,{},emission)==MaterialPolicyStatus::MissingNitsPerUnit);
    source.EmissiveStrength=0;
    assert(ValidateMaterialSource(source,1).Succeeded());
    assert(ImportEmission(source.EmissiveFactor,source.EmissiveStrength,{},emission)==MaterialPolicyStatus::Success && !emission.Emitting);
    source.EmissiveFactor[0]=1e-200;source.EmissiveStrength=1e-130;
    assert(ValidateMaterialSource(source,1).Succeeded());
    assert(ImportEmission(source.EmissiveFactor,source.EmissiveStrength,{true,1e308},emission)==MaterialPolicyStatus::Success);
    assert(emission.Emitting && emission.Nits>2e-23f && emission.Nits<2.2e-23f);
    for (size_t role=0;role<5;++role)
    {
        MaterialSource changed;
        changed.Textures[role]={true,5};
        const auto failure=ValidateMaterialSource(changed,5);
        assert(failure.Status==MaterialReadStatus::InvalidTextureIndex);
        assert(std::strcmp(failure.Field,MaterialTextureField(static_cast<MaterialTextureRole>(role)))==0);
        changed.Textures[role].Index=4;
        assert(ValidateMaterialSource(changed,5).Succeeded());
    }
    const double bads[]={-1,2,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()};
    for (double value:bads)
    {
        MaterialSource changed;
        changed.BaseColor[3]=value;
        assert(!ValidateMaterialSource(changed,0).Succeeded());
        changed={};changed.Metallic=value;
        assert(!ValidateMaterialSource(changed,0).Succeeded());
        changed={};changed.Roughness=value;
        assert(!ValidateMaterialSource(changed,0).Succeeded());
        changed={};changed.OcclusionStrength=value;
        assert(!ValidateMaterialSource(changed,0).Succeeded());
        changed={};changed.EmissiveFactor[2]=value;
        assert(!ValidateMaterialSource(changed,0).Succeeded());
    }
    MaterialSource changed;
    changed.NormalScale=std::numeric_limits<double>::infinity();
    assert(!ValidateMaterialSource(changed,0).Succeeded());
    changed={};changed.AlphaCutoff=-1;
    assert(!ValidateMaterialSource(changed,0).Succeeded());
    changed={};changed.EmissiveStrength=-1;
    assert(!ValidateMaterialSource(changed,0).Succeeded());
    changed={};changed.AlphaMode=static_cast<MaterialAlphaMode>(3);
    assert(ValidateMaterialSource(changed,0).Status==MaterialReadStatus::UnsupportedAlphaMode);
    std::puts("GltfMaterialSourceTest PASS: schema_bounds_texture_indices_emission_double_precision");
    return 0;
}
