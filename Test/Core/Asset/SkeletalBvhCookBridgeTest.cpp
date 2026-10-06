// 実BVH→cook→package→AssetSystem→Resource→Samplerを有限fixtureで検証する。
#include "Tools/AssetCook/SkeletalBvhCook.h"
#include "Tools/AssetCook/SkeletalRoleProfileCook.h"
#include "Tools/AssetCook/AssetCookOutput.h"
#include "M9LooseFixture.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/GltfBufferSet.h"
#include "Resource/ImportSettingsFile.h"
#include "Asset/AssetSystem.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Object/ResourceRegistry.h"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "BVH cook check %s:%d %s\n", __FILE__, __LINE__, #x);                                 \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace A = Core::Animation;
namespace S = Core::Skeletal;
namespace Asset = Core::Asset;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace B = Core::Bvh;
namespace M = NorvesLib::Math;
using Bytes = C::VariableArray<uint8_t>;
using Text = C::AnsiString;
namespace
{
    constexpr C::AnsiStringView Format = "nvskel.v0.skinned.pnujiw.u32";
    C::Span<const uint8_t> View(const char* value)
    {
        return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
    }
    void U32(Bytes& bytes, size_t at, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i)
        {
            bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }
    void Float(Bytes& bytes, size_t at, float value)
    {
        U32(bytes, at, std::bit_cast<uint32_t>(value));
    }
    Bytes Buffer(bool morph = false)
    {
        auto bytes = NorvesLib::Tests::AssetFixtures::BuildM9LooseBuffer<Bytes>(
            Float,
            [](Bytes& b, size_t at, uint16_t v)
            {
                b[at] = static_cast<uint8_t>(v);
                b[at + 1] = static_cast<uint8_t>(v >> 8);
            },
            [](Bytes& b, size_t at, float y)
            {
                for (size_t i = 0; i < 16; ++i)
                {
                    Float(b, at + i * 4, i % 5 == 0 ? 1.f : 0.f);
                }
                Float(b, at + 13 * 4, y);
            });
        if (morph)
        {
            bytes.resize(596, 0);
            Float(bytes, 416, .2f);
            for (size_t i = 0; i < 3; ++i)
            {
                Float(bytes, 488 + i * 16, 1);
                Float(bytes, 500 + i * 16, 1);
            }
            const float weights[] = {0, .5f, 1, 0, .7f, 0};
            for (size_t i = 0; i < 6; ++i)
            {
                Float(bytes, 572 + i * 4, weights[i]);
            }
        }
        return bytes;
    }
    Text Read(const char* name)
    {
        auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        std::ifstream in(root / "Assets" / "Models" / "M9Skinned" / name, std::ios::binary | std::ios::ate);
        CHECK(in);
        const auto length = in.tellg();
        CHECK(length > 0);
        Bytes bytes(static_cast<size_t>(length));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        CHECK(in);
        return Text(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    void Replace(Text& text, C::AnsiStringView from, C::AnsiStringView to)
    {
        const auto at = text.find(from);
        CHECK(at != Text::npos);
        text = Text(text.substr(0, at)) + Text(to) + Text(text.substr(at + from.size()));
    }
    // 合成fixtureの配列値だけを置換する。引用符内のbracketは数えない。
    Text ArrayValue(Text& text, const char* key, C::AnsiStringView replacement, bool remove = false)
    {
        const Text needle = Text("\"") + key + "\"";
        const auto member = text.find(needle);
        CHECK(member != Text::npos);
        size_t start = text.find('[', member), end = start;
        CHECK(start != Text::npos);
        int depth = 0;
        bool quoted = false, escaped = false;
        for (; end < text.size(); ++end)
        {
            const char c = text[end];
            if (quoted)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == '"')
                {
                    quoted = false;
                }
                continue;
            }
            if (c == '"')
            {
                quoted = true;
            }
            else if (c == '[')
            {
                ++depth;
            }
            else if (c == ']' && --depth == 0)
            {
                ++end;
                break;
            }
        }
        CHECK(depth == 0);
        Text old(text.substr(start, end - start));
        if (remove)
        {
            size_t after = end;
            while (after < text.size() && (text[after] == ' ' || text[after] == '\r' || text[after] == '\n'))
            {
                ++after;
            }
            if (after < text.size() && text[after] == ',')
            {
                text = Text(text.substr(0, member)) + Text(text.substr(after + 1));
            }
            else
            {
                size_t before = member;
                while (before > 0 && text[before - 1] != ',')
                {
                    --before;
                }
                CHECK(before > 0);
                text = Text(text.substr(0, before - 1)) + Text(text.substr(end));
            }
        }
        else
        {
            text = Text(text.substr(0, start)) + Text(replacement) + Text(text.substr(end));
        }
        return old;
    }
    Bytes Glb(Text text, const Bytes& binary)
    {
        char buffer[80];
        std::snprintf(buffer, sizeof(buffer), "[{\"byteLength\":%zu}]", binary.size());
        ArrayValue(text, "buffers", buffer);
        const size_t jsonSize = (text.size() + 3) & ~size_t{3}, binSize = (binary.size() + 3) & ~size_t{3};
        Bytes bytes(12 + 8 + jsonSize + 8 + binSize, 0);
        U32(bytes, 0, 0x46546c67);
        U32(bytes, 4, 2);
        U32(bytes, 8, static_cast<uint32_t>(bytes.size()));
        U32(bytes, 12, static_cast<uint32_t>(jsonSize));
        U32(bytes, 16, 0x4e4f534a);
        std::memcpy(bytes.data() + 20, text.data(), text.size());
        for (size_t i = text.size(); i < jsonSize; ++i)
        {
            bytes[20 + i] = ' ';
        }
        U32(bytes, 20 + jsonSize, static_cast<uint32_t>(binSize));
        U32(bytes, 24 + jsonSize, 0x004e4942);
        std::memcpy(bytes.data() + 28 + jsonSize, binary.data(), binary.size());
        return bytes;
    }
    struct Request
    {
        Text Bvh =
            "HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } } MOTION Frames: 3 Frame Time: 0.5\n0 0 0\n45 0 0\n90 0 0\n";
        C::FixedArray<A::SkeletalJointMappingNameView, 1> Pairs{
            A::SkeletalJointMappingNameView{View("Source"), View("Root")}};
        C::FixedArray<B::Matrix3d, 1> Corrections{B::Matrix3d{}};
        Cook::SkeletalBvhCookRequest Value;
        Request()
        {
            Value.BvhBytes = {reinterpret_cast<const uint8_t*>(Bvh.data()), Bvh.size()};
            Value.Mappings = {Pairs.data(), Pairs.size()};
            Value.Root = Pairs[0];
            Value.Corrections = {Corrections.data(), Corrections.size()};
            Value.ClipName = "Bvh";
            Value.Operation = Cook::SkeletalBvhClipOperation::Add;
            auto& s = Value.Settings;
            s.Up = Core::AssetImport::SignedAxis::PositiveY;
            s.Forward = Core::AssetImport::SignedAxis::PositiveZ;
            s.Handedness = A::SkeletalSourceHandedness::Right;
            s.PositionScale = 1;
            s.Translation = B::TranslationConvention::OffsetPlusChannels;
            s.TimeMode = A::SkeletalBvhClipTimeMode::HeaderFrameTime;
            s.SourceReuse = A::SkeletalSourceReusePolicy::Reject;
            s.Rotation = A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations;
        }
    };
    Cook::SkeletalBvhCookResult CookIt(const Bytes& glb, const Cook::SkeletalBvhCookRequest& request,
                                       const S::SkeletalGltfDecodeOptions* options = nullptr)
    {
        Cook::SkeletalBvhCookResult result;
        Text error;
        static unsigned invocation = 0;
        ++invocation;
        const bool bCooked = Cook::CookGltfWithBvhToNvskelNativePath(glb.data(), glb.size(), Format, {}, request,
                                                                     result, error, nullptr, options);
        if (!bCooked)
        {
            B::BvhDocument diagnostic;
            const auto decoded = B::DecodeBvh(request.BvhBytes, request.DecodeLimits, diagnostic);
            std::fprintf(stderr, "BVH cook invocation=%u error=%s raw_status=%u raw_offset=%zu frames=%u\n", invocation,
                         error.c_str(), static_cast<unsigned>(decoded.Status), decoded.ByteOffset,
                         diagnostic.FrameCount);
        }
        CHECK(bCooked);
        CHECK(result.Report.bStoredKeysValidated && !result.Report.bContinuousCurveValidated);
        return result;
    }
    Core::Asset::CookedSkeletalParseResult Parse(const Bytes& bytes)
    {
        auto result =
            Asset::ParseCookedSkeletal(Asset::AssetBlob::CopyBytes({bytes.data(), bytes.size()}, "bridge test"));
        CHECK(result.Succeeded());
        return result;
    }
    void Reject(const Bytes& glb, const Cook::SkeletalBvhCookRequest& request,
                const S::SkeletalGltfDecodeOptions* options = nullptr)
    {
        Request good;
        auto old = CookIt(Glb(Read("ValidU8Float.gltf"), Buffer()), good.Value);
        const auto bytes = old.Cook.NvskelBytes;
        const auto hash = old.Cook.SourceHash;
        const auto frames = old.Report.SourceFrames;
        Text error;
        CHECK(!Cook::CookGltfWithBvhToNvskelNativePath(glb.data(), glb.size(), Format, {}, request, old, error, nullptr,
                                                       options));
        CHECK(!error.empty() && old.Cook.NvskelBytes == bytes && old.Cook.SourceHash == hash &&
              old.Report.SourceFrames == frames);
    }
    void Near(float a, double b)
    {
        CHECK(std::isfinite(a) && std::abs(a - b) < 2e-4);
    }
    void PackageAndSample(const Cook::SkeletalBvhCookResult& cooked)
    {
        // 既存package helperのstd::string診断境界だけを使う。
        std::string error;
        Bytes package, repeat;
        uint64_t hash = 0, again = 0;
        const auto fourcc = Asset::MakeAssetPackageFourCC('S', 'k', 'l', '0');
        CHECK(Cook::Detail::BuildSingleSkeletalEntryPackage("asset", fourcc, cooked.Cook.NvskelBytes, package, hash,
                                                            error));
        CHECK(Cook::Detail::BuildSingleSkeletalEntryPackage("asset", fourcc, cooked.Cook.NvskelBytes, repeat, again,
                                                            error));
        CHECK(package == repeat && hash == again);
        char dir[80];
        std::snprintf(dir, sizeof(dir), "norves-bvh-%lld",
                      static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto root = std::filesystem::temp_directory_path() / dir;
        CHECK(std::filesystem::create_directory(root));
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup()
            {
                std::error_code e;
                std::filesystem::remove_all(Path, e);
            }
        } cleanup{root};
        const auto write = [&](const Bytes& b)
        {
            std::ofstream f(root / "asset.nvpkg", std::ios::binary | std::ios::trunc);
            CHECK(f);
            f.write(reinterpret_cast<const char*>(b.data()), b.size());
            f.close();
            CHECK(!f.fail());
        };
        write(package);
        Cook::Detail::SkeletalManifestMetadata metadata{cooked.Cook.VertexCount,  cooked.Cook.IndexCount,
                                                        cooked.Cook.JointCount,   cooked.Cook.ClipCount,
                                                        cooked.Cook.SubmeshCount, cooked.Cook.MaterialSlotCount};
        Text manifest;
        CHECK(Cook::Detail::BuildSkeletalManifestJson("Models/bvh.glb", cooked.Cook.SourceHash, "default", Format,
                                                      "asset.nvpkg", "asset", metadata, hash, manifest, error));
        Asset::AssetSystem assets(Text(root.generic_string().c_str()));
        CHECK(assets.LoadManifestFromJsonText(
            Cook::Detail::ToCoreString(C::AnsiStringView(manifest.data(), manifest.size()))));
        const auto resolved = assets.ResolveAsset("Models/bvh.glb", Asset::AssetKind::Model);
        CHECK(resolved.Succeeded() && resolved.UsedCooked());
        CHECK(resolved.Blob.GetSize() == cooked.Cook.NvskelBytes.size());
        CHECK(std::memcmp(resolved.Blob.GetData(), cooked.Cook.NvskelBytes.data(), resolved.Blob.GetSize()) == 0);
        const auto expected = Parse(cooked.Cook.NvskelBytes);
        auto parsed = Asset::ParseCookedSkeletal(resolved.Blob);
        CHECK(parsed.Succeeded());
        auto& data = parsed.Data.Skeletal;
        CHECK(data.Clips.size() == expected.Data.Skeletal.Clips.size());
        for (size_t i = 0; i < data.Clips.size(); ++i)
        {
            CHECK(Cook::Detail::EqualBvhCookClip(expected.Data.Skeletal.Clips[i], data.Clips[i]));
        }
        Core::ResourceRegistry registry;
        CHECK(registry.Initialize());
        auto mesh = registry.CreateTransient<Core::SkinnedMeshResource>("BvhMesh");
        auto skeleton = registry.CreateTransient<Core::SkeletonResource>("BvhSkeleton");
        auto asset = registry.CreateTransient<Core::SkeletalAssetResource>("BvhAsset");
        CHECK(mesh && skeleton && asset);
        const auto& v = data.MeshNodeGlobalTransform;
        const M::Matrix4x4 matrix(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12],
                                  v[13], v[14], v[15]);
        mesh->SetVertices(std::move(data.Vertices));
        mesh->SetIndices(std::move(data.Indices));
        mesh->SetSubmeshTables(std::move(data.SubMeshes), std::move(data.MaterialSlots));
        mesh->SetMeshNodeGlobalTransform(v);
        skeleton->SetJoints(std::move(data.Joints));
        CHECK(mesh->Load() && skeleton->Load());
        C::VariableArray<C::TSharedPtr<Core::AnimationClipResource>> clips;
        for (size_t i = 0; i < data.Clips.size(); ++i)
        {
            auto clip = registry.CreateTransient<Core::AnimationClipResource>("BvhClip");
            CHECK(clip);
            clip->SetClip(std::move(data.Clips[i]));
            CHECK(clip->Load());
            CHECK(Cook::Detail::EqualBvhCookClip(expected.Data.Skeletal.Clips[i], clip->GetClip()));
            clips.push_back(clip);
        }
        asset->SetClipResources(mesh, skeleton, clips);
        CHECK(asset->Load());
        auto clip = asset->GetClip(C::StringView(_T("Bvh")));
        CHECK(clip && clip == clips[cooked.ClipIndex]);
        for (float time : {0.f, .25f, .5f, .75f, 1.f})
        {
            A::SkeletalPoseSnapshot pose;
            CHECK(A::SkeletalAnimationSampler::Sample(*skeleton, *clip, *mesh, time, matrix, pose));
            CHECK(pose.JointModelMatrices.size() == 2 && pose.BonePalette.size() == 2 && pose.bHasAnimatedBounds);
            const double angle = time * 1.5707963267948966;
            Near(pose.JointModelMatrices[0].m00, std::cos(angle));
            Near(pose.JointModelMatrices[0].m01, std::sin(angle));
            Near(pose.JointModelMatrices[1].m30, -std::sin(angle));
            Near(pose.JointModelMatrices[1].m31, std::cos(angle));
            // このfixtureでは全骨がrootを中心に剛体回転し、両paletteとも同じRになる。
            const double palette[16] = {std::cos(angle),
                                        std::sin(angle),
                                        0,
                                        0,
                                        -std::sin(angle),
                                        std::cos(angle),
                                        0,
                                        0,
                                        0,
                                        0,
                                        1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        1};
            for (const auto& bone : pose.BonePalette)
            {
                for (size_t i = 0; i < 16; ++i)
                {
                    Near(bone.values[i], palette[i]);
                }
            }
        }
        auto broken = package;
        broken.back() ^= 1;
        write(broken);
        CHECK(!assets.ResolveAsset("Models/bvh.glb", Asset::AssetKind::Model).Succeeded());
        auto badPayload = cooked.Cook.NvskelBytes;
        badPayload[0] ^= 1;
        CHECK(!Asset::ParseCookedSkeletal(Asset::AssetBlob::CopyBytes(badPayload, "bad")).Succeeded());
        CHECK(Cook::Detail::BuildSingleSkeletalEntryPackage("asset", fourcc, badPayload, package, hash, error));
        write(package);
        CHECK(Cook::Detail::BuildSkeletalManifestJson("Models/bvh.glb", cooked.Cook.SourceHash, "default", Format,
                                                      "asset.nvpkg", "asset", metadata, hash, manifest, error));
        Asset::AssetSystem corruptInner(Text(root.generic_string().c_str()));
        CHECK(corruptInner.LoadManifestFromJsonText(
            Cook::Detail::ToCoreString(C::AnsiStringView(manifest.data(), manifest.size()))));
        const auto inner = corruptInner.ResolveAsset("Models/bvh.glb", Asset::AssetKind::Model);
        CHECK(inner.Succeeded() && inner.UsedCooked());
        CHECK(!Asset::ParseCookedSkeletal(inner.Blob).Succeeded());
    }
    void BasicAndSelection()
    {
        Request request;
        const auto text = Read("ValidU8Float.gltf");
        const auto binary = Buffer();
        const auto original = Glb(text, binary);
        auto old = S::DecodeRigGltfNativePath(original, {});
        CHECK(old.Succeeded());
        const auto added = CookIt(original, request.Value);
        CHECK(added.Cook.ClipCount == 2 && added.ClipIndex == 1);
        const auto parsed = Parse(added.Cook.NvskelBytes);
        CHECK(Cook::Detail::EqualBvhCookClip(old.Data.Clips[0], parsed.Data.Skeletal.Clips[0]));
        const auto repeat = CookIt(original, request.Value);
        CHECK(repeat.Cook.NvskelBytes == added.Cook.NvskelBytes && repeat.Cook.SourceHash == added.Cook.SourceHash);
        PackageAndSample(added);
        for (bool missing : {false, true})
        {
            auto empty = text;
            ArrayValue(empty, "animations", "[]", missing);
            const auto glb = Glb(empty, binary);
            CHECK(!S::DecodeRigGltfNativePath(glb, {}).Succeeded());
            Cook::SkeletalCookResult legacy;
            Text error;
            CHECK(!Cook::CookGltfToNvskelNativePath(glb.data(), glb.size(), Format, {}, legacy, error));
            auto first = CookIt(glb, request.Value);
            CHECK(first.Cook.ClipCount == 1 && first.ClipIndex == 0);
            PackageAndSample(first);
        }
        for (const char* bad : {"null", "{}", "\"bad\""})
        {
            auto changed = text;
            ArrayValue(changed, "animations", bad);
            Reject(Glb(changed, binary), request.Value);
        }
        request.Value.ClipName = "Wave";
        Reject(original, request.Value);
        request.Value.Operation = Cook::SkeletalBvhClipOperation::Replace;
        auto replaced = CookIt(original, request.Value);
        CHECK(replaced.Cook.ClipCount == 1 && replaced.ClipIndex == 0);
        request.Value.ClipName = "unknown";
        Reject(original, request.Value);
        auto multi = text;
        Text temporary = text;
        const auto array = ArrayValue(temporary, "animations", "[]");
        Text first(array.substr(1, array.size() - 2)), middle = first, last = first;
        Replace(middle, "Wave", "Middle");
        Replace(last, "Wave", "Last");
        const Text values = Text("[") + first + "," + middle + "," + last + "]";
        ArrayValue(multi, "animations", values);
        const auto multiGlb = Glb(multi, binary);
        const auto before = S::DecodeRigGltfNativePath(multiGlb, {});
        CHECK(before.Succeeded());
        request.Value.ClipName = "Middle";
        const auto after = Parse(CookIt(multiGlb, request.Value).Cook.NvskelBytes);
        CHECK(Cook::Detail::EqualBvhCookClip(before.Data.Clips[0], after.Data.Skeletal.Clips[0]));
        CHECK(Cook::Detail::EqualBvhCookClip(before.Data.Clips[2], after.Data.Skeletal.Clips[2]));
        auto reordered = text;
        ArrayValue(reordered, "animations", Text("[") + middle + "," + last + "," + first + "]");
        CHECK(CookIt(Glb(reordered, binary), request.Value).ClipIndex == 0);
        auto duplicate = text;
        ArrayValue(duplicate, "animations", Text("[") + middle + "," + middle + "]");
        Reject(Glb(duplicate, binary), request.Value);
        request.Value.ClipName = "Bvh";
        request.Value.Operation = Cook::SkeletalBvhClipOperation::Add;
        CHECK(CookIt(Glb(duplicate, binary), request.Value).Cook.ClipCount == 3);
        auto changed = request.Value;
        changed.Settings.TimeMode = A::SkeletalBvhClipTimeMode::OverrideFps;
        changed.Settings.SourceFps = 4;
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        changed = request.Value;
        changed.MaxNvskelBytes = 1;
        Reject(original, changed);
        changed = request.Value;
        changed.ClipLimits.MaxOutputKeys = 1;
        Reject(original, changed);
        changed = request.Value;
        changed.Settings.SourceFps = 0;
        changed.Settings.TimeMode = A::SkeletalBvhClipTimeMode::OverrideFps;
        Reject(original, changed);
        changed = request.Value;
        changed.Root.TargetName = View("unknown");
        Reject(original, changed);
        changed = request.Value;
        changed.BvhBytes = View("invalid");
        Reject(original, changed);
        changed = request.Value;
        changed.ClipName = "Different";
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        changed = request.Value;
        changed.Settings.PositionScale = 2;
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        changed = request.Value;
        changed.Settings.Handedness = A::SkeletalSourceHandedness::Left;
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        changed = request.Value;
        changed.MaxNvskelBytes -= 1;
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        C::FixedArray<B::Matrix3d, 1> correction{B::Matrix3d{}};
        correction[0].Values = {1, 0, 0, 0, 0, -1, 0, 1, 0};
        changed = request.Value;
        changed.Corrections = correction;
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        correction[0].Values[0] = 2;
        Reject(original, changed);
        C::FixedArray<A::SkeletalJointMappingNameView, 1> badPair{
            A::SkeletalJointMappingNameView{View("Source"), View("unknown")}};
        changed = request.Value;
        changed.Mappings = badPair;
        Reject(original, changed);
        changed = request.Value;
        changed.ClipName = "";
        Reject(original, changed);
        changed = request.Value;
        changed.Operation = Cook::SkeletalBvhClipOperation::Unspecified;
        Reject(original, changed);
        changed = request.Value;
        changed.DecodeLimits.MaxFrames = 2;
        Reject(original, changed);
        // 同じ値を持つraw bytesの変更も元データidentityへ反映する。
        Text whitespace = request.Bvh + " ";
        changed = request.Value;
        changed.BvhBytes = {reinterpret_cast<const uint8_t*>(whitespace.data()), whitespace.size()};
        CHECK(CookIt(original, changed).Cook.SourceHash != added.Cook.SourceHash);
        const char* single =
            "HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Zrotation Xrotation Yrotation } MOTION Frames: 1 Frame Time: 0.5\n0 0 0\n";
        changed = request.Value;
        changed.BvhBytes = View(single);
        CHECK(CookIt(original, changed).Report.SourceFrames == 1);
        const char* positions =
            "HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Xposition Yposition Zposition } MOTION Frames: 2 Frame Time: 0.5\n1e300 0 0\n1e300 0 0\n";
        changed.BvhBytes = View(positions);
        CHECK(CookIt(original, changed).Report.SourceFrames == 2);
        const char* zero =
            "HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Zrotation Xrotation Yrotation } MOTION Frames: 0 Frame Time: 0.5";
        changed.BvhBytes = View(zero);
        Reject(original, changed);
        // writerのbyte境界と既存名前重複の範囲を固定する。
        changed = request.Value;
        changed.MaxNvskelBytes = added.Cook.NvskelBytes.size();
        CHECK(CookIt(original, changed).Cook.NvskelBytes == added.Cook.NvskelBytes);
        --changed.MaxNvskelBytes;
        Reject(original, changed);
    }
    void EmptyMorph()
    {
        auto text = Read("MorphDrop.gltf");
        ArrayValue(text, "animations", "[]");
        const auto binary = Buffer(true);
        const auto glb = Glb(text, binary);
        Request request;
        Reject(glb, request.Value);
        S::SkeletalGltfDecodeOptions options;
        options.MorphPolicy = S::SkeletalMorphPolicy::Drop;
        const auto result = CookIt(glb, request.Value, &options);
        CHECK(result.Cook.DecodeReport.bMorphScanComplete && result.Cook.DecodeReport.DroppedMorphTargetCount == 1 &&
              result.Cook.DecodeReport.DroppedMorphMeshWeightCount == 1 &&
              result.Cook.DecodeReport.DroppedMorphNodeWeightCount == 1 &&
              result.Cook.DecodeReport.DroppedMorphAnimationChannelCount == 0);
        auto bad = binary;
        Float(bad, 416, std::numeric_limits<float>::quiet_NaN());
        Reject(Glb(text, bad), request.Value, &options);
        auto wrongWeight = text;
        Replace(wrongWeight, "\"weights\":[0.25]", "\"weights\":[0,0]");
        Reject(Glb(wrongWeight, binary), request.Value, &options);
        auto badNode = text;
        Replace(badNode, "\"weights\":[0.5]", "\"weights\":[0,0]");
        Reject(Glb(badNode, binary), request.Value, &options);
        Core::Gltf::BufferSet buffers;
        const auto badGlb = Glb(text, bad);
        const auto failed = S::DecodeBvhTargetRigGltfNativePath(badGlb, {}, &buffers, nullptr, &options);
        CHECK(!failed.Succeeded() && buffers.GetCount() == 0 && failed.Data.Vertices.empty() &&
              !failed.Report.bMorphScanComplete);
        // clip無しでもscale/fitがgeometryとbindへ1回だけ適用される。
        Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        for (auto policy : {S::SkeletalCubicSplinePolicy::Reject, S::SkeletalCubicSplinePolicy::Bake})
        {
            options.CubicSplinePolicy = policy;
            options.InfluencePolicy = S::SkeletalInfluencePolicy::ReduceToFour;
            const auto scaled = S::DecodeBvhTargetRigGltfNativePath(glb, {}, nullptr, &settings, &options);
            CHECK(scaled.Succeeded() && scaled.Data.Clips.empty());
            Near(scaled.Data.Vertices[1].Position.X, 2);
            Near(scaled.Data.MeshNodeGlobalTransform[12], 10);
            Near(scaled.Data.Joints[1].InverseBindMatrix[13], -2);
        }
    }
    Text ProfileText()
    {
        return R"json({"version":1,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{"root":["Source"]},"target_roles":{"root":[{"joint":"Root","C":[1,0,0,0,1,0,0,0,1]}]}})json";
    }
    C::Span<const uint8_t> TextView(const Text& text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }
    Cook::SkeletalRoleProfileCookResult ProfileCook(
        const Bytes& glb, const Text& text, C::Span<const uint8_t> bvh,
        Cook::SkeletalBvhClipOperation operation = Cook::SkeletalBvhClipOperation::Add,
        const C::String& clipName = C::String("Bvh"))
    {
        Cook::SkeletalRoleProfileCookRequest request;
        request.BvhBytes = bvh;
        request.ProfileBytes = TextView(text);
        request.Operation = operation;
        request.ClipName = clipName;
        Cook::SkeletalRoleProfileCookResult out;
        Text error;
        const bool bCooked =
            Cook::CookGltfWithRoleProfileToNvskelNativePath(glb.data(), glb.size(), Format, {}, request, out, error);
        if (!bCooked)
        {
            std::fprintf(stderr, "Role Profile cook: %s\n", error.c_str());
        }
        CHECK(bCooked);
        return out;
    }
    void ProfileReject(const Bytes& glb, const Text& text, C::Span<const uint8_t> bvh)
    {
        Request base;
        auto out = ProfileCook(glb, ProfileText(), base.Value.BvhBytes);
        const auto bytes = out.Cooked.Cook.NvskelBytes;
        const auto hash = out.Cooked.Cook.SourceHash;
        const auto names = out.Profile.Source[0][0];
        const auto rawHash = out.RawProfileHash;
        Cook::SkeletalRoleProfileCookRequest request;
        request.BvhBytes = bvh;
        request.ProfileBytes = TextView(text);
        request.Operation = Cook::SkeletalBvhClipOperation::Add;
        request.ClipName = "Bvh";
        Text error;
        CHECK(
            !Cook::CookGltfWithRoleProfileToNvskelNativePath(glb.data(), glb.size(), Format, {}, request, out, error));
        CHECK(!error.empty() && out.Cooked.Cook.NvskelBytes == bytes && out.Cooked.Cook.SourceHash == hash &&
              out.Profile.Source[0][0] == names && out.RawProfileHash == rawHash);
    }
    void RoleParserContracts()
    {
        using Status = A::SkeletalRoleProfileStatus;
        const auto base = ProfileText();
        A::SkeletalRoleProfile original;
        A::SkeletalRoleProfileLimits limits;
        CHECK(A::ParseSkeletalRoleProfile(TextView(base), limits, original).Succeeded());
        CHECK(original.ExpandedMappings == 1 && original.NameBytes == 10 && original.RequiredMask == 0);
        CHECK(original.Settings.SourceReuse == A::SkeletalSourceReusePolicy::Reject);
        CHECK(std::strcmp(A::SkeletalRoleName(A::SkeletalRole::FrontLPaw), "front_L_paw") == 0);
        CHECK(std::strcmp(A::SkeletalRoleName(A::SkeletalRole::Count), "") == 0);
        const auto reject = [&](const Text& value, const A::SkeletalRoleProfileLimits& budget)
        {
            auto out = original;
            const auto result = A::ParseSkeletalRoleProfile(TextView(value), budget, out);
            CHECK(!result.Succeeded() && !result.Field.empty());
            CHECK(out.ExpandedMappings == original.ExpandedMappings && out.NameBytes == original.NameBytes &&
                  out.Source[0][0] == original.Source[0][0] && out.Target[0][0].Name == original.Target[0][0].Name);
        };
        const auto mutate = [&](const char* from, const char* to)
        {
            auto changed = base;
            Replace(changed, from, to);
            reject(changed, limits);
        };
        mutate("\"version\":1", "\"version\":2");
        mutate("\"version\":1", "\"version\":1.0");
        mutate("\"version\":1", "\"version\":1,\"version\":1");
        mutate("\"version\":1", "\"version\":1,\"vers\\u0069on\":1");
        mutate("\"version\":1", "\"version\":1,\"bone_map\":[]");
        mutate("quadruped_v1", "humanoid_v1");
        mutate("\"up\":\"+Y\"", "\"up\":\"+Z\"");
        mutate("\"up\":\"+Y\"", "\"up\":\"+Y\",\"auto\":true");
        mutate("\"handedness\":\"right\"", "\"handedness\":\"auto\"");
        mutate("\"position_scale\":1", "\"position_scale\":0");
        mutate("\"position_scale\":1", "\"position_scale\":1e9999");
        mutate("\"position_convention\":\"additive\"", "\"position_convention\":\"infer\"");
        mutate("\"mode\":\"header_frame_time\"", "\"mode\":\"header_frame_time\",\"source_fps\":30");
        mutate("\"mode\":\"header_frame_time\"", "\"mode\":\"override_fps\"");
        mutate("\"source_roles\":{\"root\":[\"Source\"]}", "\"source_roles\":{}");
        mutate("\"source_roles\":{\"root\":[\"Source\"]}", "\"source_roles\":{\"Root\":[\"Source\"]}");
        mutate("\"source_roles\":{\"root\":[\"Source\"]}",
               "\"source_roles\":{\"root\":[\"Source\"],\"root\":[\"Source\"]}");
        mutate("[\"Source\"]", "[]");
        mutate("[\"Source\"]", "[\"Source\",\"Other\"]");
        mutate("[\"Source\"]", "[\"\"]");
        mutate("[\"Source\"]", "[\"Source\\u0000tail\"]");
        mutate("\"C\":[1,0,0,0,1,0,0,0,1]", "\"C\":[1,0,0,0,1,0,0,0]");
        mutate("\"C\":[1,0,0,0,1,0,0,0,1]", "\"C\":[-1,0,0,0,1,0,0,0,1]");
        mutate("\"C\":[1,0,0,0,1,0,0,0,1]", "\"C\":[2,0,0,0,1,0,0,0,1]");
        mutate("\"C\":[1,0,0,0,1,0,0,0,1]", "\"auto_C\":true");
        auto required = base;
        Replace(required, "\"version\":1", "\"version\":1,\"required_roles\":[\"head\"]");
        reject(required, limits);
        required = base;
        Replace(required, "\"version\":1", "\"version\":1,\"required_roles\":[\"root\",\"root\"]");
        reject(required, limits);
        auto invalidUtf8 = base;
        invalidUtf8[invalidUtf8.find("Source")] = static_cast<char>(0xff);
        reject(invalidUtf8, limits);
        auto budget = limits;
        budget.MaxInputBytes = base.size() - 1;
        reject(base, budget);
        budget = limits;
        budget.MaxDepth = 3;
        reject(base, budget);
        budget = limits;
        budget.MaxDepth = 65;
        reject(base, budget);
        budget = limits;
        budget.MaxSyntaxTokens = 1;
        reject(base, budget);
        budget = limits;
        budget.MaxNameBytes = 5;
        reject(base, budget);
        budget = limits;
        budget.MaxTotalNameBytes = 9;
        reject(base, budget);
        budget = limits;
        budget.MaxSourceElements = 0;
        reject(base, budget);
        budget = limits;
        budget.MaxMappings = 0;
        reject(base, budget);
        budget = limits;
        budget.MaxInputBytes = base.size();
        budget.MaxTotalNameBytes = 10;
        A::SkeletalRoleProfile exact;
        CHECK(A::ParseSkeletalRoleProfile(TextView(base), budget, exact).Succeeded());
        // この固定JSONは最大深さ5、構造token45。直前の予算は拒否する。
        budget = limits;
        budget.MaxDepth = 5;
        budget.MaxSyntaxTokens = 45;
        budget.MaxSourceElements = 1;
        budget.MaxMappings = 1;
        CHECK(A::ParseSkeletalRoleProfile(TextView(base), budget, exact).Succeeded());
        budget.MaxSyntaxTokens = 44;
        reject(base, budget);
        budget = limits;
        budget.MaxDepth = 4;
        reject(base, budget);
        auto requiredRoot = base;
        Replace(requiredRoot, "\"version\":1", "\"version\":1,\"required_roles\":[\"root\"]");
        CHECK(A::ParseSkeletalRoleProfile(TextView(requiredRoot), limits, exact).Succeeded() &&
              exact.RequiredMask == 1);
        Text deeplyNested;
        for (size_t i = 0; i < 65; ++i)
        {
            deeplyNested += '[';
        }
        deeplyNested += '0';
        for (size_t i = 0; i < 65; ++i)
        {
            deeplyNested += ']';
        }
        CHECK(A::ParseSkeletalRoleProfile(TextView(deeplyNested), limits, exact).Status == Status::LimitExceeded);
        Text bom("\xef\xbb\xbf");
        bom += base;
        CHECK(A::ParseSkeletalRoleProfile(TextView(bom), limits, exact).Succeeded());
        auto overrideTime = base;
        Replace(overrideTime, "\"mode\":\"header_frame_time\"", "\"mode\":\"override_fps\",\"source_fps\":16");
        CHECK(A::ParseSkeletalRoleProfile(TextView(overrideTime), limits, exact).Succeeded() &&
              exact.Settings.SourceFps == 16);
        // 任意の追加必須集合とchain同長は構造契約。解剖学的な妥当性は主張しない。
        auto chain = base;
        Replace(chain, "\"source_roles\":{\"root\":[\"Source\"]}",
                "\"source_roles\":{\"root\":[\"Source\"],\"spine\":[\"A\",\"B\"]}");
        Replace(
            chain, "\"target_roles\":{",
            "\"target_roles\":{\"spine\":[{\"joint\":\"A\",\"C\":[1,0,0,0,1,0,0,0,1]},{\"joint\":\"B\",\"C\":[1,0,0,0,1,0,0,0,1]}],");
        CHECK(A::ParseSkeletalRoleProfile(TextView(chain), limits, exact).Succeeded() && exact.ExpandedMappings == 3);
        Replace(chain, "[\"A\",\"B\"]", "[\"A\"]");
        reject(chain, limits);
        auto ownedText = base;
        CHECK(A::ParseSkeletalRoleProfile(TextView(ownedText), limits, exact).Succeeded());
        ownedText = "destroyed";
        auto copy = exact;
        auto moved = std::move(copy);
        CHECK(moved.Source[0][0] == original.Source[0][0] && moved.Target[0][0].Name == original.Target[0][0].Name);
    }
    A::SkeletalPoseSnapshot SampleRoleCook(const Cook::SkeletalBvhCookResult& cooked, float time)
    {
        auto parsed = Parse(cooked.Cook.NvskelBytes);
        auto& data = parsed.Data.Skeletal;
        Core::ResourceRegistry registry;
        CHECK(registry.Initialize());
        auto skeleton = registry.CreateTransient<Core::SkeletonResource>("RoleSampleSkeleton");
        auto clip = registry.CreateTransient<Core::AnimationClipResource>("RoleSampleClip");
        auto mesh = registry.CreateTransient<Core::SkinnedMeshResource>("RoleSampleMesh");
        CHECK(skeleton && clip && mesh && mesh->GetResourceId() != 0);
        const auto& v = data.MeshNodeGlobalTransform;
        const M::Matrix4x4 matrix(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12],
                                  v[13], v[14], v[15]);
        mesh->SetVertices(std::move(data.Vertices));
        mesh->SetIndices(std::move(data.Indices));
        mesh->SetSubmeshTables(std::move(data.SubMeshes), std::move(data.MaterialSlots));
        mesh->SetMeshNodeGlobalTransform(v);
        skeleton->SetJoints(std::move(data.Joints));
        clip->SetClip(std::move(data.Clips[cooked.ClipIndex]));
        CHECK(skeleton->Load());
        CHECK(clip->Load());
        CHECK(mesh->Load());
        A::SkeletalPoseSnapshot pose;
        CHECK(A::SkeletalAnimationSampler::Sample(*skeleton, *clip, *mesh, time, matrix, pose));
        return pose;
    }
    void RoleCookContracts()
    {
        Request base;
        const auto text = ProfileText();
        const auto glb = Glb(Read("ValidU8Float.gltf"), Buffer());
        const auto result = ProfileCook(glb, text, base.Value.BvhBytes);
        const auto typed = CookIt(glb, base.Value);
        CHECK(result.Cooked.Cook.NvskelBytes == typed.Cook.NvskelBytes &&
              result.Cooked.Cook.SourceHash != typed.Cook.SourceHash);
        CHECK(result.RawProfileBytes == text.size() && result.ExpandedRoles.size() == 1 &&
              result.SourceOnlyRoles.empty());
        CHECK(result.ExpandedRoles[0].Role == A::SkeletalRole::Root && result.ExpandedRoles[0].Ordinal == 0);
        PackageAndSample(result.Cooked);
        Cook::SkeletalRoleProfileCookRequest changedLimits;
        changedLimits.BvhBytes = base.Value.BvhBytes;
        changedLimits.ProfileBytes = TextView(text);
        changedLimits.Operation = Cook::SkeletalBvhClipOperation::Add;
        changedLimits.ClipName = "Bvh";
        changedLimits.ProfileLimits.MaxNameBytes -= 1;
        Cook::SkeletalRoleProfileCookResult budgetResult;
        Text budgetError;
        CHECK(Cook::CookGltfWithRoleProfileToNvskelNativePath(glb.data(), glb.size(), Format, {}, changedLimits,
                                                              budgetResult, budgetError));
        CHECK(budgetResult.Cooked.Cook.NvskelBytes == result.Cooked.Cook.NvskelBytes &&
              budgetResult.Cooked.Cook.SourceHash != result.Cooked.Cook.SourceHash &&
              budgetResult.RawProfileHash == result.RawProfileHash);
        const auto repeated = ProfileCook(glb, text, base.Value.BvhBytes);
        CHECK(repeated.Cooked.Cook.NvskelBytes == result.Cooked.Cook.NvskelBytes &&
              repeated.Cooked.Cook.SourceHash == result.Cooked.Cook.SourceHash);
        auto whitespace = text + " ";
        const auto spaced = ProfileCook(glb, whitespace, base.Value.BvhBytes);
        CHECK(spaced.Cooked.Cook.NvskelBytes == result.Cooked.Cook.NvskelBytes &&
              spaced.Cooked.Cook.SourceHash != result.Cooked.Cook.SourceHash);
        auto reordered = text;
        Replace(reordered, "\"up\":\"+Y\",\"forward\":\"+Z\"", "\"forward\":\"+Z\",\"up\":\"+Y\"");
        const auto reorderedResult = ProfileCook(glb, reordered, base.Value.BvhBytes);
        CHECK(reorderedResult.Cooked.Cook.NvskelBytes == result.Cooked.Cook.NvskelBytes &&
              reorderedResult.Cooked.Cook.SourceHash != result.Cooked.Cook.SourceHash);
        auto empty = Read("ValidU8Float.gltf");
        ArrayValue(empty, "animations", "[]");
        const auto first = ProfileCook(Glb(empty, Buffer()), text, base.Value.BvhBytes);
        CHECK(first.Cooked.Cook.ClipCount == 1);
        PackageAndSample(first.Cooked);
        CHECK(ProfileCook(glb, text, base.Value.BvhBytes, Cook::SkeletalBvhClipOperation::Replace, "Wave")
                  .Cooked.Cook.ClipCount == 1);
        auto corrected = text;
        Replace(corrected, "[1,0,0,0,1,0,0,0,1]", "[1,0,0,0,0,-1,0,1,0]");
        const auto rotated = Parse(ProfileCook(glb, corrected, base.Value.BvhBytes).Cooked.Cook.NvskelBytes);
        const auto& keys = rotated.Data.Skeletal.Clips.back().Channels[0].Samples;
        Near(keys[0].Value.X, 0);
        Near(keys[0].Value.Y, 0);
        Near(keys[0].Value.Z, 0);
        Near(keys[0].Value.W, 1);
        Near(keys.back().Value.X, 0);
        Near(keys.back().Value.Y, -std::sqrt(.5));
        Near(keys.back().Value.Z, 0);
        Near(keys.back().Value.W, std::sqrt(.5));
        // source-only aliasは活動pairではない。未写像roleとして所有報告する。
        auto extra = text;
        Replace(extra, "\"source_roles\":{", "\"source_roles\":{\"head\":[\"Source\"],");
        const auto unused = ProfileCook(glb, extra, base.Value.BvhBytes);
        CHECK(unused.SourceOnlyRoles.size() == 1 && unused.SourceOnlyRoles[0] == A::SkeletalRole::Head);
        Replace(extra, "\"head\":[\"Source\"]", "\"head\":[\"Missing\"]");
        ProfileReject(glb, extra, base.Value.BvhBytes);
        extra = text;
        Replace(extra, "\"source_roles\":{", "\"source_roles\":{\"head\":[\"Source\"],");
        Replace(extra, "\"target_roles\":{",
                "\"target_roles\":{\"head\":[{\"joint\":\"Child\",\"C\":[1,0,0,0,1,0,0,0,1]}],");
        ProfileReject(glb, extra, base.Value.BvhBytes);
        auto wrongRoot = text;
        Replace(wrongRoot, "\"joint\":\"Root\"", "\"joint\":\"Child\"");
        ProfileReject(glb, wrongRoot, base.Value.BvhBytes);
        // 異なる実名の2jointを正準root/head順で展開し、object順で内部pair順を変えない。
        const Text twoBvh =
            "HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Zrotation Xrotation Yrotation JOINT SourceChild { OFFSET 0 1 0 CHANNELS 3 Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } } } MOTION Frames: 3 Frame Time: 0.5\n0 0 0 0 0 0\n45 0 0 0 0 0\n90 0 0 0 0 0\n";
        Replace(extra, "\"head\":[\"Source\"]", "\"head\":[\"SourceChild\"]");
        const auto two = ProfileCook(glb, extra, TextView(twoBvh));
        CHECK(two.ExpandedRoles.size() == 2 && two.ExpandedRoles[0].Role == A::SkeletalRole::Root &&
              two.ExpandedRoles[1].Role == A::SkeletalRole::Head);
        PackageAndSample(two.Cooked);
        auto distinctMotion = twoBvh;
        Replace(distinctMotion, "45 0 0 0 0 0", "45 0 0 0 45 0");
        Replace(distinctMotion, "90 0 0 0 0 0", "90 0 0 0 90 0");
        auto distinctC = extra;
        Replace(distinctC, "[1,0,0,0,1,0,0,0,1]", "[1,0,0,0,0,-1,0,1,0]");
        const auto distinct = ProfileCook(glb, distinctC, TextView(distinctMotion));
        const auto pose = SampleRoleCook(distinct.Cooked, 1);
        CHECK(pose.JointModelMatrices.size() == 2 && pose.BonePalette.size() == 2);
        // root=Rz90、head=Rx90*Rz90の列回転を、行行列の独立literalで検査する。
        const float rootExpected[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const float headExpected[16] = {0, 0, 1, 0, -1, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 1};
        for (size_t i = 0; i < 16; ++i)
        {
            Near(pose.JointModelMatrices[0].values[i], rootExpected[i]);
            Near(pose.JointModelMatrices[1].values[i], headExpected[i]);
            Near(pose.BonePalette[0].values[i], rootExpected[i]);
            Near(pose.BonePalette[1].values[i], i == 12 ? 0 : headExpected[i]);
        }

        auto roleOrder = text;
        Replace(roleOrder, "\"source_roles\":{\"root\":[\"Source\"]}",
                "\"source_roles\":{\"root\":[\"Source\"],\"head\":[\"SourceChild\"]}");
        Replace(
            roleOrder, "\"joint\":\"Root\",\"C\":[1,0,0,0,1,0,0,0,1]}]",
            "\"joint\":\"Root\",\"C\":[1,0,0,0,1,0,0,0,1]}],\"head\":[{\"joint\":\"Child\",\"C\":[1,0,0,0,1,0,0,0,1]}]");
        CHECK(ProfileCook(glb, roleOrder, TextView(twoBvh)).Cooked.Cook.NvskelBytes == two.Cooked.Cook.NvskelBytes);
        auto sourceRoot = text;
        Replace(sourceRoot, "[\"Source\"]", "[\"SourceChild\"]");
        ProfileReject(glb, sourceRoot, TextView(twoBvh));
        auto targetDuplicate = extra;
        Replace(targetDuplicate, "\"joint\":\"Child\"", "\"joint\":\"Root\"");
        ProfileReject(glb, targetDuplicate, TextView(twoBvh));
        ProfileReject(glb, text, View("invalid BVH"));
        auto wrongCase = text;
        Replace(wrongCase, "\"joint\":\"Root\"", "\"joint\":\"root\"");
        ProfileReject(glb, wrongCase, base.Value.BvhBytes);
        auto unicodeMismatch = text;
        Replace(unicodeMismatch, "\"joint\":\"Root\"", "\"joint\":\"Root\\u0301\"");
        ProfileReject(glb, unicodeMismatch, base.Value.BvhBytes);
        std::puts(
            "SKELETAL_ROLE_PROFILE result=pass bounded_owned_quadruped_roles_explicit_c_required_root_chain_hash_actual_cook_package_sampler_no_file_cache");
    }

} // namespace
int main()
{
    BasicAndSelection();
    EmptyMorph();
    RoleParserContracts();
    RoleCookContracts();
    std::puts(
        "SKELETAL_BVH_COOK_BRIDGE result=pass explicit_add_replace_zero_clip_rig_morph_hash_package_asset_system_sampler_atomic_no_cli");
    return 0;
}
