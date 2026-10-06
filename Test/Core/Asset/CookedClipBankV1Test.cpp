// 独立wire値と実import/writer/parserを往復する。旧fixtureを再生成しない。
#include "ClipBankV1Fixture.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Animation/SkeletalSamplingMath.h"
#include "Resource/GltfBufferFile.h"
#include <cmath>
#include <limits>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace S = NorvesLib::Core::Skeletal;
namespace C = NorvesLib::Core::Container;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace
{
    void Topology()
    {
        C::VariableArray<S::SkeletalJoint> joints(2);
        joints[0].Name = _T("Root");
        joints[1].Name = _T("Child");
        joints[1].ParentIndex = 0;
        S::RigTopology first;
        RIG_CHECK(S::BuildRigTopology({joints.data(), joints.size()}, {}, first) == S::RigV1Status::Success);
        RIG_CHECK(first.CanonicalBytes.size() == 49);
        RIG_CHECK(first.SkeletonId == 0x7498d74adc178547ull);
        RIG_CHECK(first.CanonicalToSource[0] == 1 && first.SourceToCanonical[0] == 1 &&
                  first.Joints[0].ParentIndex == 1);
        auto swapped = joints;
        std::swap(swapped[0], swapped[1]);
        swapped[0].ParentIndex = 1;
        swapped[1].ParentIndex = -1;
        S::RigTopology second;
        RIG_CHECK(S::BuildRigTopology({swapped.data(), swapped.size()}, {}, second) == S::RigV1Status::Success &&
                  S::SameRigTopology(first, second));
        swapped[0].InverseBindMatrix[0] = 999;
        RIG_CHECK(S::BuildRigTopology({swapped.data(), swapped.size()}, {}, second) == S::RigV1Status::Success &&
                  S::SameRigTopology(first, second));
        swapped[0].Name = _T("Different");
        RIG_CHECK(S::BuildRigTopology({swapped.data(), swapped.size()}, {}, second) == S::RigV1Status::Success);
        second.SkeletonId = first.SkeletonId;
        RIG_CHECK(!S::SameRigTopology(first, second)); // hash同値でも名前/親の全文が必要。
        for (int kind = 0; kind < 4; ++kind)
        {
            auto broken = joints;
            if (kind == 0)
            {
                broken[1].Name = broken[0].Name;
            }
            if (kind == 1)
            {
                broken[0].ParentIndex = 1;
            }
            if (kind == 2)
            {
                broken[1].ParentIndex = 9;
            }
            if (kind == 3)
            {
                broken[1].Name = {};
            }
            const auto old = first.CanonicalBytes;
            RIG_CHECK(S::BuildRigTopology({broken.data(), broken.size()}, {}, first) != S::RigV1Status::Success &&
                      first.CanonicalBytes == old);
        }
        S::RigV1Limits lowLimits;
        lowLimits.MaxJoints = 1;
        RIG_CHECK(S::BuildRigTopology({joints.data(), joints.size()}, lowLimits, second) == S::RigV1Status::LimitExceeded);
    }
    F::Bytes Optional(F::Bytes bytes)
    {
        F::Bytes result(bytes.size() + 32, 0);
        std::memcpy(result.data(), bytes.data(), 480);
        std::memcpy(result.data() + 512, bytes.data() + 480, bytes.size() - 480);
        F::Put32(result, 28, 8);
        F::Put64(result, 40, result.size());
        for (size_t i = 0; i < 7; ++i)
        {
            F::Put64(result, 256 + i * 32 + 8, F::U64(bytes, 256 + i * 32 + 8) + 32);
        }
        F::Put32(result, 480, 0x52545546);
        F::Put64(result, 488, result.size());
        F::Put32(result, 504, 1);
        F::Reseal(result);
        return result;
    }
    void Codec(F::Fixture& f)
    {
        F::Bytes bytes;
        auto bank = f.Bank(f.Json, &bytes);
        RIG_CHECK(bank.GetData() && bank.GetData()->Snapshots.size() == 1 && bank.GetData()->Clips.size() == 1);
        RIG_CHECK(bank.GetData()->Topology.SkeletonId == 0x7498d74adc178547ull);
        RIG_CHECK(bank.GetData()->Snapshots[0].Rest[0].Translation.Y == 1 &&
                  bank.GetData()->Snapshots[0].Rest[1].Rotation.W == 1);
        S::RigV1Report report;
        F::Bytes roundtrip;
        RIG_CHECK(S::WriteClipBankV1(bank, roundtrip, report) && roundtrip == bytes);
        Cook::RigClipBankCookResult again;
        RIG_CHECK(Cook::CookRigClipBankV1NativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", "nvskel.v1.clips", again,
                                                    report) &&
                  again.Bytes == bytes);
        const auto goodData = bank.GetData();
        const auto reject = [&](const F::Bytes& broken)
        { RIG_CHECK(!S::ParseClipBankV1(F::View(broken), bank, report) && bank.GetData() == goodData); };
        for (size_t cut : {size_t{0}, size_t{8}, size_t{255}, size_t{479}, bytes.size() - 1})
        {
            auto b = bytes;
            b.resize(cut);
            reject(b);
        }
        for (size_t at : {size_t{0}, size_t{12}, size_t{16}, size_t{20}, size_t{24}, size_t{32}, size_t{40}, size_t{48},
                          size_t{56}, size_t{64}, size_t{68}, size_t{72}})
        {
            auto b = bytes;
            b[at] ^= 0x40;
            reject(b);
        }
        const auto modify = [&](size_t at, uint32_t value)
        {
            auto b = bytes;
            F::Put32(b, at, value);
            F::Reseal(b);
            reject(b);
        };
        modify(256 + 32 + 4, 0);                                               // known必須節のoptional化。
        modify(256 + 32 + 24, 20);                                             // recordSize。
        modify(256 + 32 + 8, 256);                                             // header/directory overlap。
        modify(256 + 2 * 32, F::U32(bytes, 256));                              // duplicateFourCC。
        modify(256 + 6 * 32 + 28, 0xffffffffu);                                // count×stride。
        modify(F::SectionOffset(bytes, 1) + 12, 0);                            // self-parent。
        modify(F::SectionOffset(bytes, 2) + 4, 1);                             // snapshotのjoint欠落。
        modify(F::SectionOffset(bytes, 2) + 20, 2);                            // profile。
        modify(F::SectionOffset(bytes, 3) + 40, 1);                            // rest reserved。
        modify(F::SectionOffset(bytes, 4) + 12, 99);                           // snapshot参照。
        modify(F::SectionOffset(bytes, 4) + 20, 1);                            // channel所有範囲の穴。
        modify(F::SectionOffset(bytes, 5) + 28, 1);                            // 未知encoding。
        modify(F::SectionOffset(bytes, 5) + 32, 1);                            // sample所有範囲の穴。
        modify(F::SectionOffset(bytes, 6) + 20, 1);                            // sample reserved。
        modify(F::SectionOffset(bytes, 6) + 32, std::bit_cast<uint32_t>(0.f)); // 同時刻。
        modify(F::SectionOffset(bytes, 6) + 4, 0x7fc00000);                    // NaN。
        auto optional = Optional(bytes);
        S::ClipBankV1 withOptional;
        RIG_CHECK(S::ParseClipBankV1(F::View(optional), withOptional, report));
        F::Put32(optional, 484, 1);
        F::Reseal(optional);
        reject(optional);
        S::RigV1Limits lower;
        lower.MaxWireBytes = 480;
        F::Bytes sentinel{4, 5, 6};
        RIG_CHECK(!S::WriteClipBankV1(bank, sentinel, report, lower) && sentinel == F::Bytes({4, 5, 6}));
        lower = {};
        lower.MaxSamples = 1;
        RIG_CHECK(!S::ParseClipBankV1(F::View(bytes), bank, report, lower) && bank.GetData() == goodData);
        lower = {};
        lower.MaxChannels = 1;
        RIG_CHECK(!S::ParseClipBankV1(F::View(bytes), bank, report, lower));
        RIG_CHECK(!Cook::CookRigClipBankV1NativePath(F::View(f.Json), "RigV1Fixture/rig.gltf",
                                                     "nvskel.v0.skinned.pnujiw.u32", again, report) &&
                  again.Bytes == bytes);
        // 元rig・file bufferから独立したbank所有を確かめる。
        S::ClipBankV1 built;
        {
            auto author = f.Import(f.Json);
            RIG_CHECK(S::BuildClipBankV1({&author, 1}, built, report));
            author = {};
        }
        F::Bytes owned;
        RIG_CHECK(S::WriteClipBankV1(built, owned, report) && owned == bytes);
        const char* output = std::getenv("NORVES_CLIPBANK_V1_OUTPUT");
        if (output && *output)
        {
            const std::filesystem::path path(output);
            std::filesystem::create_directories(path);
            F::WriteBytes(path / "clipbank-v1.nvclip", bytes);
        }
    }
    void QuaternionAndScaleDomain(F::Fixture& f)
    {
        namespace D = NorvesLib::Core::Animation::Detail;
        const auto tiny = D::SkeletalRotationFromColumn(0, 0, 1e-4f, 1e-4f);
        const auto huge = D::SkeletalRotationFromColumn(0, 0, 1e20f, 1e20f);
        RIG_CHECK(tiny.z == 0 && tiny.w == 1 && huge.z == 0 && huge.w == 0);
        F::Bytes original;
        auto stable = f.Bank(f.Json, &original);
        const auto* before = stable.GetData();
        S::RigV1Report report;
        for (float q : {1e-4f, 1e20f})
        {
            auto binary = f.Binary;
            F::Float(binary, 392, q);
            F::Float(binary, 396, q);
            f.SetBuffer(binary);
            Cook::RigClipBankCookResult result;
            result.Bytes = {9, 8};
            RIG_CHECK(!Cook::CookRigClipBankV1NativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", "nvskel.v1.clips",
                                                         result, report));
            RIG_CHECK(result.Bytes == F::Bytes({9, 8}) && report.Status == S::RigV1Status::InvalidClip);
            auto wire = original;
            const auto samples = F::SectionOffset(wire, 6);
            F::Float(wire, samples + 64 + 12, q);
            F::Float(wire, samples + 64 + 16, q);
            F::Reseal(wire);
            RIG_CHECK(!S::ParseClipBankV1(F::View(wire), stable, report) &&
                      report.Status == S::RigV1Status::InvalidClip && stable.GetData() == before);
            wire = original;
            const auto rest = F::SectionOffset(wire, 3);
            F::Float(wire, rest + 48 + 20, q);
            F::Float(wire, rest + 48 + 24, q);
            F::Bytes values;
            values.insert(values.end(), wire.begin() + rest, wire.begin() + rest + 40);
            values.insert(values.end(), wire.begin() + rest + 48, wire.begin() + rest + 88);
            F::Put64(wire, F::SectionOffset(wire, 2) + 24, F::Hash(F::View(values)));
            F::Reseal(wire);
            RIG_CHECK(!S::ParseClipBankV1(F::View(wire), stable, report) &&
                      report.Status == S::RigV1Status::InvalidRest && stable.GetData() == before);
        }
        f.SetBuffer(f.Binary);
        const auto scaleJson = F::Replace(f.Json, "\"path\":\"translation\"", "\"path\":\"scale\"");
        for (float x : {0.f, -1.f})
        {
            auto binary = f.Binary;
            F::Float(binary, 360, x);
            F::Float(binary, 368, 1);
            F::Float(binary, 372, 1);
            F::Float(binary, 380, 1);
            f.SetBuffer(binary);
            Cook::RigClipBankCookResult result;
            RIG_CHECK(!Cook::CookRigClipBankV1NativePath(F::View(scaleJson), "RigV1Fixture/rig.gltf", "nvskel.v1.clips",
                                                         result, report) &&
                      report.Status == S::RigV1Status::InvalidClip);
            auto wire = original;
            F::Put32(wire, F::SectionOffset(wire, 5) + 12, 2);
            F::Float(wire, F::SectionOffset(wire, 6) + 4, x);
            F::Reseal(wire);
            RIG_CHECK(!S::ParseClipBankV1(F::View(wire), stable, report) &&
                      report.Status == S::RigV1Status::InvalidClip);
        }
        f.SetBuffer(f.Binary);
        std::printf("CLIPBANK_V1_CASE result=pass tiny_huge_quaternion_legacy_negative_control_positive_scale_keys\n");
    }
    void ImportBudgets(F::Fixture& f)
    {
        namespace G = NorvesLib::Core::Gltf;
        auto sentinel = f.Import(f.Json);
        const auto* before = sentinel.GetData();
        S::RigV1Report report;
        // fileを退避して、count budgetが外部readより前に拒否することを確かめる。
        std::filesystem::rename("RigV1Fixture/fixture.bin", "RigV1Fixture/held.bin");
        for (int kind = 0; kind < 7; ++kind)
        {
            S::RigV1Limits limit;
            if (kind == 0)
            {
                limit.MaxJoints = 1;
            }
            if (kind == 1)
            {
                limit.MaxChannels = 1;
            }
            if (kind == 2)
            {
                limit.MaxSamples = 1;
            }
            if (kind == 3)
            {
                limit.MaxVertices = 2;
            }
            if (kind == 4)
            {
                limit.MaxIndices = 2;
            }
            if (kind == 5)
            {
                limit.MaxNodes = 2;
            }
            if (kind == 6)
            {
                limit.MaxAccessors = 1;
            }
            RIG_CHECK(
                !S::DecodeRigAuthoringNativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", sentinel, report, limit));
            RIG_CHECK(report.Status == S::RigV1Status::LimitExceeded &&
                      report.DecodeStatus == S::SkeletalGltfDecodeStatus::ImportLimitExceeded &&
                      sentinel.GetData() == before);
        }
        S::RigV1Limits limit;
        limit.MaxSamples = 4;
        auto shared = F::AllTranslation(f.Json);
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(shared), "RigV1Fixture/rig.gltf", sentinel, report, limit) &&
                  report.Status == S::RigV1Status::LimitExceeded);
        const char* key = "\"animations\":[";
        const auto start = f.Json.find(key) + std::strlen(key);
        const auto clip = f.Json.substr(start, f.Json.size() - start - 2);
        F::Text many = f.Json.substr(0, start);
        for (size_t i = 0; i < 257; ++i)
        {
            if (i)
            {
                many += ',';
            }
            many += clip;
        }
        many += "]}";
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(many), "RigV1Fixture/rig.gltf", sentinel, report) &&
                  report.Status == S::RigV1Status::LimitExceeded);
        std::filesystem::rename("RigV1Fixture/held.bin", "RigV1Fixture/fixture.bin");
        auto extended = f.Binary;
        extended.resize(1024, 0);
        f.SetBuffer(extended);
        limit = {};
        limit.MaxBufferBytes = 512;
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", sentinel, report, limit) &&
                  report.Status == S::RigV1Status::LimitExceeded);
        G::BufferFileContext context{"RigV1Fixture/rig.gltf"};
        context.MaxReadBytes = 512;
        F::Bytes read{7};
        const auto capacity = read.capacity();
        RIG_CHECK(G::ReadBufferFile(F::View(F::Text("fixture.bin")), read, &context) ==
                      G::ExternalBufferReadResult::InvalidSize &&
                  context.bLimitExceeded && read.empty() && read.capacity() == capacity);
        // 宣言417bytesでも実file合計は932bytes。2本目の確保より先に残量で拒否する。
        extended = f.Binary;
        extended.resize(516, 0);
        f.SetBuffer(extended);
        F::WriteBytes("RigV1Fixture/other.bin", f.Binary);
        const auto two = F::Replace(
            f.Json, "\"buffers\":[{\"uri\":\"fixture.bin\",\"byteLength\":416}]",
            "\"buffers\":[{\"uri\":\"fixture.bin\",\"byteLength\":416},{\"uri\":\"other.bin\",\"byteLength\":1}]");
        limit.MaxBufferBytes = 700;
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(two), "RigV1Fixture/rig.gltf", sentinel, report, limit) &&
                  report.Status == S::RigV1Status::LimitExceeded);
        context = {"RigV1Fixture/rig.gltf"};
        context.MaxReadBytes = 700;
        RIG_CHECK(G::ReadBufferFile(F::View(F::Text("fixture.bin")), read, &context) ==
                      G::ExternalBufferReadResult::Success &&
                  context.ReadBytes == 516);
        const auto firstCapacity = read.capacity();
        RIG_CHECK(G::ReadBufferFile(F::View(F::Text("other.bin")), read, &context) ==
                      G::ExternalBufferReadResult::InvalidSize &&
                  read.empty() && read.capacity() == firstCapacity && context.ReadBytes == 516);
        F::Text encoded(1366, 'A');
        encoded += "==";
        auto embedded = F::Replace(two, "\"uri\":\"other.bin\"",
                                   F::Text("\"uri\":\"data:application/octet-stream;base64,") + encoded + "\"");
        limit.MaxBufferBytes = 800;
        RIG_CHECK(
            !S::DecodeRigAuthoringNativePath(F::View(embedded), "RigV1Fixture/rig.gltf", sentinel, report, limit) &&
            report.Status == S::RigV1Status::LimitExceeded);
        f.SetBuffer(f.Binary);
        limit = {};
        limit.MaxSamples = 4;
        RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", sentinel, report, limit));
        std::printf(
            "CLIPBANK_V1_CASE result=pass import_preallocation_counts_shared_accessor_external_tail_cumulative_bytes\n");
    }
    void BakeBudget(F::Fixture& f)
    {
        auto binary = f.Binary;
        binary.resize(656, 0);
        constexpr float t[] = {0, 0, 0, 0, 1, 0, 0, 6, 0, 0, 6, 0, 0, 3, 0, 0, 0, 0};
        constexpr float r[] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, .5f, -.5f, 0, 0, .5f, -.5f, 0, 0, 1, 0, 0, 0, 0, 0};
        constexpr float s[] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0};
        for (size_t i = 0; i < 18; ++i)
        {
            F::Float(binary, 416 + i * 4, t[i]);
            F::Float(binary, 584 + i * 4, s[i]);
        }
        for (size_t i = 0; i < 24; ++i)
        {
            F::Float(binary, 488 + i * 4, r[i]);
        }
        f.SetBuffer(binary);
        const auto json = F::BaseJson("CubicChannels.gltf");
        S::SkeletalGltfDecodeOptions options;
        options.CubicSplinePolicy = S::SkeletalCubicSplinePolicy::Bake;
        S::RigAuthoringCpu good;
        S::RigV1Report report;
        RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/cubic.gltf", good, report, {}, nullptr,
                                                  &options));
        size_t total = 0;
        for (const auto& channel : good.GetData()->Geometry.Clips[0].Channels)
        {
            total += channel.Samples.size();
        }
        RIG_CHECK(total > 6);
        auto sentinel = good;
        S::RigV1Limits lower;
        lower.MaxSamples = 6;
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/cubic.gltf", sentinel, report, lower,
                                                   nullptr, &options) &&
                  sentinel.GetData() == good.GetData());
        RIG_CHECK(report.DecodeStatus == S::SkeletalGltfDecodeStatus::CubicBakeFailed ||
                  report.DecodeStatus == S::SkeletalGltfDecodeStatus::ImportLimitExceeded);
        lower.MaxSamples = uint32_t(total - 1);
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/cubic.gltf", sentinel, report, lower,
                                                   nullptr, &options) &&
                  sentinel.GetData() == good.GetData());
        lower.MaxSamples = uint32_t(total);
        RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/cubic.gltf", sentinel, report, lower,
                                                  nullptr, &options));
        f.SetBuffer(f.Binary);
        std::printf("CLIPBANK_V1_CASE result=pass cubic_bake_cumulative_sample_budget_before_output_allocation\n");
    }
    void AuthorRest(F::Fixture& f)
    {
        const auto source = f.Import(f.Json);
        RIG_CHECK(source.GetData()->LocalRest[0].Translation.X == 0 &&
                  source.GetData()->LocalRest[1].Translation.Y == 1);
        // IBM由来のroot bind x=5ではなくnode default x=0を保持する。
        RIG_CHECK(source.GetData()->Geometry.MeshNodeGlobalTransform[12] == 5);
        S::RigV1Report report;
        auto sentinel = source;
        for (const F::Text field : {F::Text("\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,1,0,1]"),
                                    F::Text("\"scale\":[-1,1,1]"), F::Text("\"scale\":[0,1,1]")})
        {
            auto text = F::ChildTrs(f.Json, field);
            RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(text), "RigV1Fixture/rig.gltf", sentinel, report) &&
                      sentinel.GetData() == source.GetData());
        }
        auto nonuniform = f.Import(F::ChildTrs(f.Json, "\"translation\":[0,1,0],\"scale\":[2,3,4]"));
        RIG_CHECK(nonuniform.GetData()->LocalRest[1].Scale.Y == 3);
        NorvesLib::Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        auto scaled = f.Import(f.Json, "RigV1Fixture/scaled.gltf", &settings);
        RIG_CHECK(scaled.GetData()->ResolvedImportScale == 2 && scaled.GetData()->LocalRest[1].Translation.Y == 2 &&
                  scaled.GetData()->Geometry.Clips[0].Channels[0].Samples[1].Value.Y == 6);
        settings.Settings.Fit = NorvesLib::Core::AssetImport::FitAxis::Up;
        settings.Settings.FitMeters = 4;
        settings.Settings.Scale = 1;
        auto fit = f.Import(f.Json, "RigV1Fixture/fit.gltf", &settings);
        RIG_CHECK(fit.GetData()->ResolvedImportScale == 4 && fit.GetData()->LocalRest[1].Translation.Y == 4);
        const auto unicode = F::Replace(f.Json, "\"name\":\"Child\"", "\"name\":\"\\u9aa8\\ud83d\\udc3a\"");
        auto rig = f.Import(unicode);
        S::ClipBankV1 bank;
        RIG_CHECK(S::BuildClipBankV1({&rig, 1}, bank, report));
        F::Bytes bytes;
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report));
        RIG_CHECK(S::ParseClipBankV1(F::View(bytes), bank, report));
        const auto nul = F::Replace(f.Json, "\"name\":\"Child\"", "\"name\":\"a\\u0000b\"");
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(nul), "RigV1Fixture/rig.gltf", sentinel, report));
    }
} // namespace
int main()
{
    Topology();
    F::Fixture fixture;
    Codec(fixture);
    AuthorRest(fixture);
    QuaternionAndScaleDomain(fixture);
    ImportBudgets(fixture);
    BakeBudget(fixture);
    std::printf(
        "CLIPBANK_V1_WIRE result=pass actual_author_import_required_rest_deterministic_wire_strict_parse_canonical_topology_no_legacy_upgrade\n");
    return 0;
}
