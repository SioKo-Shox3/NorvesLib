#include "Resource/ImportSettingsFile.h"
#include "Resource/ImportSettingsHash.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace NorvesLib::Core::AssetImport;
namespace
{
    void Write(const std::filesystem::path& path,const char* content,size_t size)
    {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        assert(file.is_open()); file.write(content,static_cast<std::streamsize>(size)); file.flush(); assert(file.good());
    }
    void Write(const std::filesystem::path& path,const char* content)
    {
        Write(path,content,std::strlen(content));
    }
    struct Directory
    {
        std::filesystem::path Path;
        Directory()
        {
            char suffix[64]={};
            const auto number=std::to_chars(suffix,suffix+sizeof(suffix),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(number.ec==std::errc{});
            Path=std::filesystem::temp_directory_path()/"norves-import-settings";
            Path+=std::filesystem::path(suffix,number.ptr);
            assert(std::filesystem::create_directory(Path));
        }
        ~Directory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path,ignored);
        }
    };
}
int main()
{
    static_assert(IsMissingWindowsSettingsFileError(2));
    static_assert(IsMissingWindowsSettingsFileError(3));
    static_assert(!IsMissingWindowsSettingsFileError(0));
    static_assert(!IsMissingWindowsSettingsFileError(5));
    static_assert(!IsMissingWindowsSettingsFileError(53));
    static_assert(!IsMissingWindowsSettingsFileError(67));
    static_assert(!IsMissingWindowsSettingsFileError(123));
    static_assert(!IsMissingWindowsSettingsFileError(267));
    Directory directory;
    const auto source=directory.Path/"model.glb";
    auto sidecar=source; sidecar+=".import.json";
    LoadedImportSettings loaded;
    assert(LoadImportSettingsFile(source,{},loaded).Result==SettingsFileResult::Success);
    assert(!loaded.bPresent && loaded.Settings.Scale==1 && loaded.Path==sidecar);
    assert(AppendImportSettingsHash(123,loaded.bPresent,loaded.Settings).Value==123);

    loaded.Settings.Scale=7; loaded.bPresent=true; loaded.Path=directory.Path/"retained";
    const auto retainedPath=loaded.Path;
    auto expectFailure=[&](const ImportSettingsFileOptions& options,SettingsFileResult result)
    {
        assert(LoadImportSettingsFile(source,options,loaded).Result==result);
        assert(loaded.Settings.Scale==7 && loaded.bPresent && loaded.Path==retainedPath);
    };
    ImportSettingsFileOptions required; required.bRequired=true;
    expectFailure(required,SettingsFileResult::Missing);
    ImportSettingsFileOptions overridden; overridden.OverridePath=directory.Path/"explicit.json";
    expectFailure(overridden,SettingsFileResult::Missing);
    ImportSettingsFileOptions conflict; conflict.bDisabled=true; conflict.bRequired=true;
    expectFailure(conflict,SettingsFileResult::InvalidOptions);
    conflict.bRequired=false; conflict.OverridePath=sidecar;
    expectFailure(conflict,SettingsFileResult::InvalidOptions);
    assert(LoadImportSettingsFile({}, {}, loaded).Result==SettingsFileResult::InvalidOptions);
    assert(loaded.Settings.Scale==7 && loaded.Path==retainedPath);

#if defined(_WIN32)
    // MSVC filesystemではinvalid-nameもnot_foundへ分類されるが、設定無しとして無視しない。
    assert(LoadImportSettingsFile(directory.Path/"bad?.glb",{},loaded).Result==SettingsFileResult::StatusFailed);
    assert(loaded.Settings.Scale==7 && loaded.bPresent && loaded.Path==retainedPath);
#endif

    Write(sidecar,R"({"version":1,"units":{"scale":2}})");
    assert(LoadImportSettingsFile(source,{},loaded).Result==SettingsFileResult::Success);
    assert(loaded.bPresent && loaded.Path==sidecar && loaded.Settings.Scale==2);
    const auto firstHash=AppendImportSettingsHash(123,true,loaded.Settings);
    Write(sidecar,R"({ "meta":{"note":"different","seed":123},"units":{"scale":2},"version":1 })");
    assert(LoadImportSettingsFile(source,required,loaded).Result==SettingsFileResult::Success);
    assert(AppendImportSettingsHash(123,true,loaded.Settings).Value==firstHash.Value);
    Write(overridden.OverridePath,R"({"version":1,"units":{"scale":3}})");
    assert(LoadImportSettingsFile(source,overridden,loaded).Result==SettingsFileResult::Success);
    assert(loaded.Settings.Scale==3 && loaded.Path==overridden.OverridePath);
    assert(AppendImportSettingsHash(123,true,loaded.Settings).Value!=firstHash.Value);

    const char bomJson[]={char(0xef),char(0xbb),char(0xbf),'{','"','v','e','r','s','i','o','n','"',':','1','}'};
    Write(sidecar,bomJson,sizeof(bomJson));
    assert(LoadImportSettingsFile(source,{},loaded).Result==SettingsFileResult::Success && loaded.Settings.Scale==1);

    loaded.Settings.Scale=7; loaded.bPresent=true; loaded.Path=retainedPath;
    Write(sidecar,"");
    expectFailure({},SettingsFileResult::InvalidJson);
    Write(sidecar,"{");
    expectFailure({},SettingsFileResult::InvalidJson);
    const char nul[]={'{','}',0,' '};
    Write(sidecar,nul,sizeof(nul));
    expectFailure({},SettingsFileResult::InvalidJson);
    Write(sidecar,R"({"version":1,"units":{"scale":-1}})");
    expectFailure({},SettingsFileResult::InvalidSettings);
    {
        std::ofstream file(sidecar,std::ios::binary|std::ios::trunc);
        assert(file.is_open()); file.seekp(MaximumImportSettingsFileSize); file.put('x'); file.flush(); assert(file.good());
    }
    expectFailure({},SettingsFileResult::InvalidSize);
    assert(std::filesystem::remove(sidecar));
    assert(std::filesystem::create_directory(sidecar));
    expectFailure({},SettingsFileResult::NotRegularFile);
    assert(std::filesystem::remove(sidecar));

    // symlinkが許可される実行環境だけで、dangling linkをauto不在扱いしないことを検査する。
    std::error_code linkError;
    std::filesystem::create_symlink(directory.Path/"absent.json",sidecar,linkError);
    if (!linkError)
    {
        expectFailure({},SettingsFileResult::NotRegularFile);
        assert(std::filesystem::remove(sidecar));
    }
    Write(sidecar,"invalid JSON");
    ImportSettingsFileOptions disabled; disabled.bDisabled=true;
    assert(LoadImportSettingsFile(source,disabled,loaded).Result==SettingsFileResult::Success);
    assert(!loaded.bPresent && loaded.Settings.Scale==1 && loaded.Path.empty());
    std::cout << "ImportSettingsFileTest PASS: auto_required_override_disabled_BOM_errors_unchanged\n";
    return 0;
}
