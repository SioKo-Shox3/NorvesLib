// 材質名・元番号・生成slotの共通照合と失敗保持を検証する。
#include "Resource/MaterialSelection.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib::Core;
using Status = MaterialSelectionStatus;
using Domain = MaterialIdentityDomain;
Container::Span<const uint8_t> Name(const char* value)
{
    return {reinterpret_cast<const uint8_t*>(value),std::strlen(value)};
}
MaterialSelectorView Named(const char* value)
{
    return {MaterialSelectorKind::UniqueName,0,Name(value),false};
}
MaterialSelectorView Indexed(uint32_t index,const char* expected)
{
    return {MaterialSelectorKind::IndexAndExpectedName,index,Name(expected),true};
}
int main()
{
    uint32_t scratch[128], rows[8];
    const auto reset = [&]() { for (auto& value:rows) { value=12345; } };
    const auto held = [&]() { for (auto value:rows) { CHECK(value==12345); } };
    MaterialIdentityView catalog[]={{1,Name("Body")},{0,Name("骨🐺")},{2,Name("Body [0]")}};
    MaterialSelectorView queries[]={Named("骨🐺"),Indexed(1,"Body"),Named("Body [0]")};
    reset();
    auto result=ResolveMaterialSelection(Domain::SourceMaterial,catalog,queries,scratch,rows);
    CHECK(result.Succeeded() && rows[0]==1 && rows[1]==0 && rows[2]==2 && rows[3]==12345);
    CHECK(result.Selector==SIZE_MAX && result.CatalogRow==UINT32_MAX);
    // 行の並べ替え後も元番号を照合する。生成名の推測はしない。
    MaterialIdentityView reversed[]={catalog[2],catalog[1],catalog[0]};
    result=ResolveMaterialSelection(Domain::SourceMaterial,reversed,queries,scratch,rows);
    CHECK(result.Succeeded() && rows[0]==1 && rows[1]==2 && rows[2]==0);
    MaterialSelectorView duplicates[]={Named("Body"),Indexed(1,"Body")};
    reset();
    result=ResolveMaterialSelection(Domain::SourceMaterial,catalog,duplicates,scratch,rows);
    CHECK(result.Status==Status::DuplicateTarget && result.Selector==1 && result.ConflictingSelector==0 && result.CatalogRow==0);held();
    duplicates[1]=duplicates[0];
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,duplicates,scratch,rows).Status==Status::DuplicateTarget);held();
    duplicates[0]=Indexed(1,"Body");duplicates[1]=duplicates[0];
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,duplicates,scratch,rows).Status==Status::DuplicateTarget);held();
    MaterialSelectorView lateMiss[]={Indexed(1,"Body"),Indexed(3,"Absent")};
    result=ResolveMaterialSelection(Domain::SourceMaterial,catalog,lateMiss,scratch,rows);
    CHECK(result.Status==Status::Unmatched && result.Selector==1 && result.CatalogRow==UINT32_MAX);held();
    MaterialSelectorView one[]={Named("Absent")};
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,rows).Status==Status::Unmatched);held();
    one[0]=Indexed(1,"Renamed");
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,rows).Status==Status::ExpectedNameMismatch);held();
    one[0]=Indexed(3,"Body");
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,rows).Status==Status::Unmatched);held();
    one[0]=Indexed(1,"Body");one[0].bExpectedNamePresent=false;
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,rows).Status==Status::InvalidSelector);held();
    MaterialIdentityView unnamed[]={{0,{}}};one[0]=Indexed(0,"");
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,unnamed,one,scratch,rows).Succeeded());
    one[0]=Named("");reset();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,unnamed,one,scratch,rows).Status==Status::InvalidSelector);held();
    CHECK(ResolveMaterialSelection(Domain::GeneratedSlot,unnamed,one,scratch,rows).Succeeded());
    unnamed[0].Name=Name("Material_0");one[0]=Indexed(0,"");reset();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,unnamed,one,scratch,rows).Status==Status::ExpectedNameMismatch);held();
    one[0]=Named("Material_0");
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,unnamed,one,scratch,rows).Succeeded());
    MaterialIdentityView same[]={{0,Name("Body")},{1,Name("Body")},{2,{}},{3,{}}};
    result=ResolveMaterialSelection(Domain::SourceMaterial,same,{},scratch,rows);
    CHECK(result.Succeeded() && result.DuplicateNameGroups==2 && result.FirstDuplicateRow==2 && result.SecondDuplicateRow==3);
    one[0]=Named("Body");reset();
    result=ResolveMaterialSelection(Domain::SourceMaterial,same,one,scratch,rows);
    CHECK(result.Status==Status::AmbiguousName && result.DuplicateNameGroups==2);held();
    one[0]=Indexed(1,"Body");
    result=ResolveMaterialSelection(Domain::SourceMaterial,same,one,scratch,rows);
    CHECK(result.Succeeded() && result.DuplicateNameGroups==2 && rows[0]==1);
    one[0]=Indexed(0,"");reset();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,{},one,scratch,rows).Status==Status::Unmatched);held();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,{},{},{},{}).Succeeded());
    catalog[2].IdentityIndex=0;
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,{},scratch,rows).Status==Status::InvalidCatalog);held();
    catalog[2].IdentityIndex=2;
    const uint8_t bad[]={0xc0,0x80};catalog[2].Name={bad,2};
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,{},scratch,rows).Status==Status::InvalidName);held();
    const uint8_t nul[]={'A',0,'B'};catalog[2].Name={nul,3};
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,{},scratch,rows).Status==Status::InvalidName);held();
    catalog[2].Name=Name("Body [0]");one[0]=Named("Body");
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,{scratch,4},rows).Status==Status::InsufficientStorage);held();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,{}).Status==Status::InsufficientStorage);held();
    CHECK(ResolveMaterialSelection(static_cast<Domain>(9),catalog,one,scratch,rows).Status==Status::InvalidDomain);held();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,{nullptr,3},one,scratch,rows).Status==Status::InvalidInput);held();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,{scratch,8}).Status==Status::OverlappingStorage);held();
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,{reinterpret_cast<uint32_t*>(catalog),8},rows).Status==Status::OverlappingStorage);held();
    catalog[2].Name={reinterpret_cast<const uint8_t*>(rows),sizeof(rows)};
    CHECK(ResolveMaterialSelection(Domain::SourceMaterial,catalog,one,scratch,rows).Status==Status::OverlappingStorage);held();
    size_t count=999;
    CHECK(!GetMaterialSelectionScratchCount(SIZE_MAX,2,count) && count==999);
    CHECK(GetMaterialSelectionScratchCount(3,2,count) && count==7);
    std::puts("MaterialSelectionTest PASS: exact_names_indices_duplicates_domains_atomic_alias");
    return 0;
}
