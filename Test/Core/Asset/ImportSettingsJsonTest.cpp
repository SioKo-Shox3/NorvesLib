#include "Resource/ImportSettings.h"
#include "Text/JsonDocument.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::AssetImport;
namespace
{
    SettingsResult Parse(const char* text, ImportSettings& out)
    {
        Container::String source;
        while (*text)
        {
            source.push_back(static_cast<Container::String::value_type>(static_cast<unsigned char>(*text++)));
        }
        JsonDocument document;
        assert(JsonDocument::TryParse(source, document));
        return ParseSettings(document.GetRoot(), out);
    }
}
int main()
{
    const auto identity = EncodeCanonicalSettings({});
    for (const char* text : {
        R"({"version":1})",
        R"({"meta":{"generator":"test","seed":42,"note":"ignored"},"version":1})",
        R"({ "mesh":{"flipV":false,"winding":"keep","flipU":false},"origin":{"mode":"keep","offset":[0,-0,0]},"axes":{"forward":"+Z","up":"+Y","mirrorX":false},"units":{"scale":1},"version":1 })",
        R"({"version":1,"repair":{},"lod":{},"material":{},"collision":{},"clip":{}})"})
    {
        ImportSettings settings;
        settings.Scale = 8;
        assert(Parse(text, settings) == SettingsResult::Success);
        const auto canonical = EncodeCanonicalSettings(settings);
        assert(canonical.Size == identity.Size && std::memcmp(canonical.Bytes, identity.Bytes, identity.Size) == 0);
    }
    ImportSettings custom;
    assert(Parse(R"({"version":1,"units":{"fit":{"axis":"up","meters":0.6}},"axes":{"up":"+Z","forward":"+X","mirrorX":true},"origin":{"mode":"custom","offset":[1,-2,3]},"mesh":{"winding":"auto","flipU":true,"flipV":true}})", custom) == SettingsResult::Success);
    assert(custom.Fit == FitAxis::Up && custom.FitMeters == 0.6 && custom.Up == SignedAxis::PositiveZ &&
        custom.Forward == SignedAxis::PositiveX && custom.bMirrorX && custom.Origin == OriginMode::Custom &&
        custom.OriginOffset[1] == -2 && custom.Winding == WindingMode::Auto && custom.bFlipU && custom.bFlipV);
    const auto before = EncodeCanonicalSettings(custom);
    for (const char* invalid : {
        R"([])", R"({})", R"({"version":2})", R"({"version":"1"})", R"({"version":1,"oops":0})",
        R"({"version":1,"version":1})", R"({"version":1,"units":null})",
        R"({"version":1,"units":{"scale":2,"scale":3}})", R"({"version":1,"units":{"scale":"2"}})",
        R"({"version":1,"units":{"scale":0}})", R"({"version":1,"units":{"scale":-1}})",
        R"({"version":1,"units":{"fit":{"axis":"up"}}})", R"({"version":1,"units":{"fit":{"axis":"","meters":1}}})",
        R"({"version":1,"units":{"fit":{"axis":"side","meters":1}}})",
        R"({"version":1,"units":{"fit":{"axis":"up","meters":0}}})",
        R"({"version":1,"units":{"scale":1,"fit":{"axis":"up","meters":1}}})",
        R"({"version":1,"axes":{"up":"+Z"}})", R"({"version":1,"axes":{"up":"-Y","forward":"+Y"}})",
        R"({"version":1,"axes":{"up":"Y"}})", R"({"version":1,"axes":{"mirrorX":1}})",
        R"({"version":1,"axes":{"up":"+Y","up":"+Y"}})", R"({"version":1,"axes":{"unknown":true}})",
        R"({"version":1,"origin":{"mode":"missing"}})", R"({"version":1,"origin":{"offset":[1,2]}})",
        R"({"version":1,"origin":{"mode":"custom","offset":[1,null,3]}})",
        R"({"version":1,"origin":{"mode":"keep","offset":[1,0,0]}})",
        R"({"version":1,"mesh":{"winding":"CCW"}})", R"({"version":1,"mesh":{"flipU":null}})",
        R"({"version":1,"mesh":{"flipV":true,"flipV":false}})", R"({"version":1,"repair":{"enabled":true}})",
        R"({"version":1,"lod":null})", R"({"version":1,"material":{"arm":"auto"}})",
        R"({"version":1,"collision":{"mode":"convex"}})", R"({"version":1,"clip":{"name":"run"}})",
        R"({"version":1,"meta":{"seed":1.5}})", R"({"version":1,"meta":{"generator":null}})",
        R"({"version":1,"meta":{"extra":"x"}})", R"({"version":1,"meta":{"note":"a","note":"b"}})"})
    {
        assert(Parse(invalid, custom) != SettingsResult::Success);
        const auto after = EncodeCanonicalSettings(custom);
        assert(after.Size == before.Size && std::memcmp(after.Bytes, before.Bytes, before.Size) == 0);
    }
    assert(Parse(R"({"version":1,"origin":{"mode":"surface_centroid"}})", custom) == SettingsResult::UnsupportedFeature);
    const auto afterUnsupported = EncodeCanonicalSettings(custom);
    assert(afterUnsupported.Size == before.Size && std::memcmp(afterUnsupported.Bytes, before.Bytes, before.Size) == 0);
    assert(ParseSettings({}, custom) == SettingsResult::InvalidRoot);
    std::cout << "ImportSettingsJsonTest PASS: schema_duplicates_defaults_meta_failure_preservation\n";
    return 0;
}
