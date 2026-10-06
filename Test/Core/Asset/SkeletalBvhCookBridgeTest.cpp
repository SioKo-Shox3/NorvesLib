// 実BVH→cook→package→AssetSystem→Resource→Samplerを有限fixtureで検証する。
#include "Tools/AssetCook/SkeletalBvhCook.h"
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
        const bool bCooked = Cook::CookGltfWithBvhToNvskelNativePath(
            glb.data(), glb.size(), Format, {}, request, result, error, nullptr, options);
        if (!bCooked)
        {
            B::BvhDocument diagnostic;
            const auto decoded = B::DecodeBvh(request.BvhBytes, request.DecodeLimits, diagnostic);
            std::fprintf(stderr, "BVH cook invocation=%u error=%s raw_status=%u raw_offset=%zu frames=%u\n",
                invocation, error.c_str(), static_cast<unsigned>(decoded.Status), decoded.ByteOffset, diagnostic.FrameCount);
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
} // namespace
int main()
{
    BasicAndSelection();
    EmptyMorph();
    std::puts(
        "SKELETAL_BVH_COOK_BRIDGE result=pass explicit_add_replace_zero_clip_rig_morph_hash_package_asset_system_sampler_atomic_no_cli");
    return 0;
}
