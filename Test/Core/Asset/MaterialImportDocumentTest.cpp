// sidecar・元材質・ARM/発光/表面名の共通解決と所有を検証する。
#include "Resource/MaterialImportDocument.h"
#include "Resource/ImportSettingsFile.h"
#include "Text/JsonDocument.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <charconv>
#include <chrono>
#include <utility>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::AssetImport;
namespace
{
    JsonDocument Json(const char* text)
    {
        JsonDocument document;
        CHECK(JsonDocument::TryParseUtf8({reinterpret_cast<const uint8_t*>(text),std::strlen(text)},document));
        return document;
    }
    ImportSettingsDocument Settings(const char* text)
    {
        auto json=Json(text);ImportSettingsDocument result;
        CHECK(ParseImportSettingsDocument(json.GetRoot(),result)==SettingsResult::Success);
        return result;
    }
    SourceMaterialCatalog Catalog(const char* text)
    {
        auto json=Json(text);SourceMaterialCatalog result;
        CHECK(ReadSourceMaterialCatalog(json.GetRoot(),result)==SettingsResult::Success);
        return result;
    }
    bool Equals(const MaterialNameBytes& bytes,const char* text)
    {
        const size_t count=std::strlen(text);
        return bytes.size()==count && (count==0 || std::memcmp(bytes.data(),text,count)==0);
    }
    void OwnershipAndLayers()
    {
        auto settings=Settings(R"({"version":1,"units":{"scale":2},"material":{"profile":"ai_generated","emissiveNitsPerUnit":2},"materials":[{"name":"Body","arm":{"metallic":"texture"},"emissiveNitsPerUnit":3,"surface":"Stone"},{"index":0,"expectedName":"","surface":"Soft"}]})");
        auto catalog=Catalog(R"({"materials":[{}, {"name":"Body"}]})");
        std::swap(catalog[0],catalog[1]);
        ResolvedMaterialImportPlan plan;
        auto result=ResolveMaterialImportPlan(catalog,settings,{true,1},plan);
        CHECK(result.Succeeded() && plan.Materials.size()==2 && plan.Geometry.Scale==2);
        CHECK(plan.Materials[0].SourceIndex==0 && plan.Materials[0].Name.empty());
        CHECK(Equals(plan.Materials[1].Name,"Body") && Equals(plan.Materials[1].SurfaceName,"Stone"));
        CHECK(plan.Materials[0].bSurfacePresent && Equals(plan.Materials[0].SurfaceName,"Soft"));
        CHECK(plan.Materials[1].Settings.Arm.Channels[2].Mode==ArmMode::Texture);
        CHECK(plan.Materials[0].Settings.Arm.Channels[2].Mode==ArmMode::Ignore);
        CHECK(plan.Materials[0].Settings.Emission.NitsPerUnit==2 && plan.Materials[1].Settings.Emission.NitsPerUnit==3);
        settings={};catalog.clear();
        CHECK(Equals(plan.Materials[1].Name,"Body") && Equals(plan.Materials[1].SurfaceName,"Stone"));
        auto defaults=Settings(R"({"version":1,"material":{"profile":"ai_generated"}})");
        CHECK(ResolveMaterialImportPlan({},defaults,{true,9},plan).Succeeded());
        CHECK(plan.Materials.empty() && plan.ImplicitDefault.Emission.NitsPerUnit==9);
    }
    void SelectionAndFailure()
    {
        auto catalog=Catalog(R"({"materials":[{"name":"Body"},{"name":"Body"}]})");
        std::swap(catalog[0],catalog[1]);
        auto settings=Settings(R"({"version":1,"materials":[{"index":1,"expectedName":"Body","surface":"Stone"}]})");
        ResolvedMaterialImportPlan plan;
        auto result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Succeeded() && plan.DuplicateNameGroups==1 && plan.FirstDuplicateMaterialIndex==0 && plan.SecondDuplicateMaterialIndex==1);
        CHECK(plan.Materials[1].bSurfacePresent);
        const auto held=[&]() { CHECK(plan.Materials.size()==2 && Equals(plan.Materials[1].SurfaceName,"Stone")); };
        const char* failures[]={
            R"({"version":1,"materials":[{"name":"Body"}]})",
            R"({"version":1,"materials":[{"name":"Missing"}]})",
            R"({"version":1,"materials":[{"index":0,"expectedName":"Renamed"}]})",
            R"({"version":1,"materials":[{"index":0,"expectedName":"Body"},{"index":0,"expectedName":"Body","surface":"Stone"}]})"
        };
        for (const auto* text:failures)
        {
            settings=Settings(text);result=ResolveMaterialImportPlan(catalog,settings,{},plan);
            CHECK(result.Status==MaterialImportPlanStatus::SelectionFailed);held();
        }
        catalog=Catalog(R"({"materials":[{"name":"Body"}]})");
        settings=Settings(R"({"version":1,"materials":[{"name":"Body","surface":"Stone"},{"index":0,"expectedName":"Body","arm":{"metallic":"ignore"}}]})");
        result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Selection.Status==MaterialSelectionStatus::DuplicateTarget);held();
        settings=Settings(R"({"version":1,"materials":[{"index":0,"expectedName":""}]})");
        CHECK(ResolveMaterialImportPlan({},settings,{},plan).Selection.Status==MaterialSelectionStatus::Unmatched);held();
        catalog=Catalog(R"({"materials":[{}]})");
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Succeeded() && plan.Materials[0].Name.empty());
        catalog=Catalog(R"({"materials":[{"name":"Material_0"}]})");
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Selection.Status==MaterialSelectionStatus::ExpectedNameMismatch);
        settings=Settings(R"({"version":1,"materials":[{"name":"Material_0"}]})");
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Succeeded());
        settings.Materials[0].bSurfacePresent=true;
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Status==MaterialImportPlanStatus::InvalidConfiguration);
        CHECK(Equals(plan.Materials[0].Name,"Material_0") && !plan.Materials[0].bSurfacePresent);
    }
    void StrictParsing()
    {
        auto held=Settings(R"({"version":1,"units":{"scale":3}})");
        const char* invalid[]={
            R"({"version":1,"materials":[{}]})",
            R"({"version":1,"materials":[{"name":""}]})",
            R"({"version":1,"materials":[{"name":"Body","index":0,"expectedName":"Body"}]})",
            R"({"version":1,"materials":[{"index":0}]})",
            R"({"version":1,"materials":[{"expectedName":"Body"}]})",
            R"({"version":1,"materials":[{"index":-1,"expectedName":""}]})",
            R"({"version":1,"materials":[{"index":0.5,"expectedName":""}]})",
            R"({"version":1,"materials":[{"index":4294967296,"expectedName":""}]})",
            R"({"version":1,"materials":[{"name":"Body","surface":""}]})",
            R"({"version":1,"materials":[{"name":"Body","surface":"A\u0000BCD"}]})",
            R"({"version":1,"materials":[{"name":"Body","profile":"ai_generated"}]})",
            R"({"version":1,"materials":[{"name":"Body","name":"Other"}]})",
            R"({"version":1,"materials":null})",
            R"({"version":1,"material":{"unknown":true}})",
            R"({"version":1,"collision":{"shape":"box"}})"
        };
        for (const auto* text:invalid)
        {
            auto json=Json(text);
            CHECK(ParseImportSettingsDocument(json.GetRoot(),held)!=SettingsResult::Success);
            CHECK(held.Geometry.Scale==3 && held.Materials.empty());
        }
        const char* invalidCatalog[]={R"({"materials":null})",R"({"materials":[{"name":null}]})",R"({"materials":[{"name":"a","name":"b"}]})",R"({"materials":[],"materials":[]})",R"({"materials\u0000x":[]})",R"({"materials":[{"name":"A\u0000BCD"}]})"};
        auto catalog=Catalog(R"({"materials":[{"name":"Held"}]})");
        for (const auto* text:invalidCatalog)
        {
            auto json=Json(text);CHECK(ReadSourceMaterialCatalog(json.GetRoot(),catalog)!=SettingsResult::Success);
            CHECK(catalog.size()==1 && Equals(catalog[0].Name,"Held"));
        }
        auto json=Json(R"({"version":1,"material":{"profile":"ai_generated"}})");ImportSettings old;
        CHECK(ParseSettings(json.GetRoot(),old)==SettingsResult::UnsupportedFeature);
        json=Json(R"({"version":1,"materials":[]})");CHECK(ParseSettings(json.GetRoot(),old)==SettingsResult::UnknownField);
        json=Json(R"({"version":1,"material":{},"units":{"scale":2}})");CHECK(ParseSettings(json.GetRoot(),old)==SettingsResult::Success && old.Scale==2);
    }
    void InvalidManualPlan()
    {
        auto catalog=Catalog(R"({"materials":[{"name":"A"},{"name":"B"}]})");
        auto settings=Settings(R"({"version":1,"materials":[{"name":"A","surface":"One"},{"name":"B","surface":"Two"}]})");
        ResolvedMaterialImportPlan plan;
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Succeeded());
        const auto held=[&]()
        {
            CHECK(plan.Materials.size()==2 && plan.Geometry.Scale==1 && plan.DuplicateNameGroups==0);
            CHECK(!plan.Materials[0].Settings.Emission.Present && !plan.Materials[1].Settings.Emission.Present);
            CHECK(Equals(plan.Materials[0].Name,"A") && Equals(plan.Materials[1].Name,"B"));
            CHECK(Equals(plan.Materials[0].SurfaceName,"One") && Equals(plan.Materials[1].SurfaceName,"Two"));
        };
        settings.Materials[0].Settings.Emission={true,7};
        settings.Materials[1].Settings.Emission={true,-1};
        auto result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Status==MaterialImportPlanStatus::SettingsFailed && result.EntryIndex==1);held();
        settings.Materials[1].Settings.Emission={};
        settings.Materials[1].SurfaceName={0xc0,0x80};
        result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Status==MaterialImportPlanStatus::InvalidConfiguration && result.EntryIndex==1);held();
        catalog[1].SourceIndex=0;
        result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Selection.Status==MaterialSelectionStatus::InvalidCatalog);held();
        catalog[1].SourceIndex=2;
        result=ResolveMaterialImportPlan(catalog,settings,{},plan);
        CHECK(result.Selection.Status==MaterialSelectionStatus::InvalidCatalog);held();
    }
    void UnicodeAndFile()
    {
        const char* raw=reinterpret_cast<const char*>(u8R"({"materials":[{"name":"骨🐺"}]})");
        auto catalog=Catalog(raw);
        const char* sidecar=R"({"version":1,"materials":[{"name":"\u9aa8\ud83d\udc3a","surface":"Stone"}]})";
        auto settings=Settings(sidecar);ResolvedMaterialImportPlan plan;
        CHECK(ResolveMaterialImportPlan(catalog,settings,{},plan).Succeeded());
        CHECK(Equals(plan.Materials[0].Name,reinterpret_cast<const char*>(u8"骨🐺")));
        char suffix[32];
        const auto result=std::to_chars(suffix,suffix+sizeof(suffix),std::chrono::steady_clock::now().time_since_epoch().count());
        CHECK(result.ec==std::errc{});
        auto directory=std::filesystem::temp_directory_path()/"norves-material-document-";
        directory+=std::filesystem::path(suffix,result.ptr);
        CHECK(std::filesystem::create_directory(directory));
        const auto source=directory/"test.glb";
        auto path=source;path+=".import.json";
        {
            std::ofstream file(path,std::ios::binary);CHECK(file.good());
            file.write("\xef\xbb\xbf",3);file.write(sidecar,static_cast<std::streamsize>(std::strlen(sidecar)));
            file.close();CHECK(file.good());
        }
        LoadedImportSettingsDocument loaded;
        CHECK(LoadImportSettingsDocument(source,{},loaded).Result==SettingsFileResult::Success && loaded.bPresent && loaded.Path==path);
        CHECK(ResolveMaterialImportPlan(catalog,loaded.Settings,{},plan).Succeeded());
        LoadedImportSettings legacy;
        CHECK(LoadImportSettingsFile(source,{},legacy).Result==SettingsFileResult::InvalidSettings && !legacy.bPresent);
        CHECK(std::filesystem::remove(path));CHECK(std::filesystem::remove(directory));
    }
}
int main()
{
    OwnershipAndLayers();SelectionAndFailure();StrictParsing();InvalidManualPlan();UnicodeAndFile();
    std::puts("MaterialImportDocumentTest PASS: owner_selector_settings_surface_atomic_file_legacy");
    return 0;
}
