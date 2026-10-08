#include "Asset/TextAssetReloadTracker.h"
#include "Locomotion/QuadrupedLocomotionJson.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Locomotion;
namespace
{
    Container::Span<const uint8_t> Bytes(const Container::AnsiString& s)
    {
        return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    }
    void Replace(Container::AnsiString& text, size_t start, size_t count, const Container::AnsiString& replacement)
    {
        assert(start <= text.size() && count <= text.size() - start);
        Container::AnsiString result;
        result.append(text.data(), start);
        result.append(replacement.data(), replacement.size());
        result.append(text.data() + start + count, text.size() - start - count);
        text = std::move(result);
    }
    struct Fixture
    {
        std::filesystem::path Root;
        Fixture()
        {
            char name[96];
            std::snprintf(name, sizeof(name), "NorvesProfileReload_%lld",
                          static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            Root = std::filesystem::temp_directory_path() / name;
            assert(std::filesystem::create_directory(Root));
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
        void Write(const Container::AnsiString& bytes)
        {
            std::ofstream file(Root / "profile.json", std::ios::binary | std::ios::trunc);
            assert(file);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            assert(file.good());
        }
    };
} // namespace
int main()
{
    const auto source = AssetFileReader().Read("Gameplay/DogLocomotion.json");
    assert(source.Succeeded());
    Container::AnsiString valid;
    valid.append(reinterpret_cast<const char*>(source.Blob.GetData()), source.Blob.GetSize());
    QuadrupedLocomotionParams params;
    Container::String error;
    assert(ParseQuadrupedLocomotionJson(Bytes(valid), params, error));
    assert(params.Gaits[3].SpeedMax == 12 && params.JumpHeightMin == .6f);
    const auto checkRejected = [&](const Container::AnsiString& text) {
        params.Gaits[3].SpeedMax = 123;
        assert(!ParseQuadrupedLocomotionJson(Bytes(text), params, error));
        assert(params.Gaits[3].SpeedMax == 123 && !error.empty());
    };
    checkRejected("{");
    checkRejected("{}");
    auto changed = valid;
    const auto schema = changed.find("locomotion.v1");
    assert(schema != Container::AnsiString::npos);
    Replace(changed, schema, 13, "locomotion.v2");
    checkRejected(changed);
    changed = valid;
    Replace(changed, changed.find("\"schema\""), 8, "\"unknown\"");
    checkRejected(changed);
    changed = valid;
    Replace(changed, changed.find("\"schema\""), 0, "\"schema\":\"locomotion.v1\",");
    checkRejected(changed);
    changed = valid;
    const auto min = changed.find("\"heightMin\": 0.6");
    assert(min != Container::AnsiString::npos);
    Replace(changed, min, 16, "\"heightMin\": -1");
    checkRejected(changed);
    for (const char* replacement : {"-1e-50", "1.0000000001"})
    {
        changed = valid;
        const auto start = changed.find("\"airControl\": 0.25");
        assert(start != Container::AnsiString::npos);
        Container::AnsiString value = "\"airControl\": ";
        value.append(replacement);
        Replace(changed, start, 18, value);
        checkRejected(changed);
    }
    Container::AnsiString bom;
    bom.append("\xef\xbb\xbf", 3);
    bom.append(valid.data(), valid.size());
    assert(ParseQuadrupedLocomotionJson(Bytes(bom), params, error));
    Container::AnsiString deep;
    for (int n = 0; n < 17; ++n)
        deep.append("[");
    checkRejected(deep);
    const uint8_t invalidUtf8[] = {0xff};
    params.Gaits[3].SpeedMax = 123;
    assert(!ParseQuadrupedLocomotionJson({invalidUtf8, 1}, params, error) && params.Gaits[3].SpeedMax == 123);
    Fixture fixture;
    fixture.Write(valid);
    TextAssetReloadTracker tracker(fixture.Root.generic_string().c_str());
    assert(tracker.SetPath("profile.json"));
    assert(!tracker.SetPath("../escape.json") && tracker.GetPath() == "profile.json");
    AssetBlob observed;
    assert(tracker.Poll(0, observed) == TextAssetPollResult::Changed);
    assert(ParseQuadrupedLocomotionJson(observed.GetSpan(), params, error));
    assert(tracker.Poll(.1, observed) == TextAssetPollResult::NotDue);
    assert(tracker.Poll(.15, observed) == TextAssetPollResult::Unchanged);
    fixture.Write("{");
    assert(tracker.Poll(.25, observed) == TextAssetPollResult::Changed);
    assert(!ParseQuadrupedLocomotionJson(observed.GetSpan(), params, error) && params.Gaits[3].SpeedMax == 12);
    assert(tracker.Poll(.25, observed) == TextAssetPollResult::Unchanged);
    fixture.Write(valid);
    assert(tracker.Poll(.25, observed) == TextAssetPollResult::Changed);
    assert(ParseQuadrupedLocomotionJson(observed.GetSpan(), params, error));
    std::filesystem::remove(fixture.Root / "profile.json");
    const auto oldData = observed.GetData();
    assert(tracker.Poll(.25, observed) == TextAssetPollResult::ReadFailed && observed.GetData() == oldData);
    fixture.Write(valid);
    assert(tracker.Poll(.25, observed) == TextAssetPollResult::Unchanged);
    tracker.Reset();
    assert(tracker.Poll(0, observed) == TextAssetPollResult::Changed);
    assert(tracker.Poll(-1, observed) == TextAssetPollResult::InvalidArgument);
    assert(tracker.Poll(std::numeric_limits<double>::infinity(), observed) == TextAssetPollResult::InvalidArgument);
    assert(tracker.Poll(std::numeric_limits<double>::max(), observed) == TextAssetPollResult::Unchanged);
    std::cout << "LocomotionProfileReloadTest PASS\n";
    return 0;
}
