#include "Resource/ImportSettingsHash.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::AssetImport;
int main()
{
    const uint64_t seeds[] = {0,0xcbf29ce484222325ull,0x123456789abcdef0ull};
    const uint64_t expected[] = {0xb448b2727d5b3be8ull,0x5006c0cee8fee799ull,0x337bfeea93b6b578ull};
    for (size_t index=0;index<3;++index)
    {
        const auto absent=AppendImportSettingsHash(seeds[index],false,{});
        const auto present=AppendImportSettingsHash(seeds[index],true,{});
        assert(absent.bValid && absent.Value==seeds[index]);
        assert(present.bValid && present.Value==expected[index]);
        assert(AppendImportSettingsHash(seeds[index],false,{},2).Value==seeds[index]);
        assert(AppendImportSettingsHash(seeds[index],true,{},2).Value!=present.Value);
        ImportSettings changed; changed.Scale=2;
        assert(AppendImportSettingsHash(seeds[index],true,changed).Value!=present.Value);
        changed={}; changed.OriginOffset[1]=-0.0;
        assert(AppendImportSettingsHash(seeds[index],true,changed).Value==present.Value);
        changed.Scale=std::numeric_limits<double>::quiet_NaN();
        assert(!AppendImportSettingsHash(seeds[index],true,changed).bValid);
        assert(AppendImportSettingsHash(seeds[index],false,changed).Value==seeds[index]);
    }
    std::cout << "ImportSettingsHashTest PASS: known_FNV_states_absence_values_version_zero\n";
    return 0;
}
