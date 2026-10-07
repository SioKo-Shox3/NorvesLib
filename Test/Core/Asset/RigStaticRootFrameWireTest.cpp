// profile2の必須作者frameと三roleの境界。独立literal比較用の実bytesを保存する。
#include "RigStaticRootFrameFixture.h"
#include "RigSplitWireTestFixture.h"
#include <limits>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace H = NorvesLib::Tests::RigStaticRootFrameFixture;
namespace W = NorvesLib::Tests::RigSplitWireFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace
{
    F::Text JointCountSource(F::Fixture& f, uint32_t count)
    {
        const auto number = [](uint32_t value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%u", value);
            return F::Text(buffer);
        };
        F::Text joints = "0", children;
        F::Text nodes = "{\"name\":\"Root\",\"children\":[";
        for (uint32_t i = 1; i < count; ++i)
        {
            joints += "," + number(i);
            if (i > 1)
            {
                children += ",";
            }
            children += number(i);
        }
        nodes += children + "]}";
        for (uint32_t i = 1; i < count; ++i)
        {
            nodes += ",{\"name\":\"" + (i == 1 ? F::Text("Child") : "Bone" + number(i)) + "\",\"translation\":[0,1,0]}";
        }
        nodes += ",{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0}";
        auto json = F::Replace(
            f.Json,
            "{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Child\",\"translation\":[0,1,0]},{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0}",
            nodes);
        json = F::Replace(json, "\"nodes\":[0,2]", "\"nodes\":[0," + number(count) + "]");
        json = F::Replace(json, "\"joints\":[0,1]", "\"joints\":[" + joints + "]");
        json = F::Replace(json, "\"byteLength\":416", "\"byteLength\":" + number(416 + count * 64));
        json = F::Replace(json, "\"byteOffset\":224,\"byteLength\":128",
                          "\"byteOffset\":416,\"byteLength\":" + number(count * 64));
        json = F::Replace(json, "\"bufferView\":9,\"componentType\":5126,\"count\":2",
                          "\"bufferView\":9,\"componentType\":5126,\"count\":" + number(count));
        auto binary = f.Binary;
        binary.resize(416 + count * 64, 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t offset = 416 + size_t(i) * 64;
            for (size_t j = 0; j < 4; ++j)
            {
                F::Float(binary, offset + j * 20, 1);
            }
            F::Float(binary, offset + 48, 5);
            F::Float(binary, offset + 52, i == 0 ? 0.0f : -1.0f);
        }
        f.SetBuffer(binary);
        return json;
    }
    void JointCapacity(F::Fixture& f)
    {
        constexpr auto profile = S::RigImportProfile::StaticRootFrame256;
        auto request = H::CookRequest();
        request.Profile = profile;
        request.Limits.MaxJoints = 256;
        for (uint32_t count : {129u, 256u})
        {
            const auto json = JointCountSource(f, count);
            const auto cooked = H::X::CookSource(json, request);
            S::RigV1Report report;
            S::SkeletonV1 skeleton;
            S::SkinMeshV1 mesh;
            S::ClipBankV1 bank;
            RIG_CHECK(S::ParseSkeletonV1(cooked.Skeleton.Payload, skeleton, report, request.Limits, profile));
            RIG_CHECK(S::ParseSkinMeshV1(cooked.Mesh.Payload, mesh, report, request.Limits, profile));
            RIG_CHECK(S::ParseClipBankV1(cooked.Bank.Payload, bank, report, request.Limits, profile));
            RIG_CHECK(skeleton.GetData()->Topology.Joints.size() == count &&
                      mesh.GetData()->InverseBindMatrices.size() == count);
            S::CookedRigSplitCpuAsset cpu;
            S::RigSplitReport bind;
            RIG_CHECK(S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, {}, cpu, bind, request.Limits, profile));
            Core::ResourceRegistry registry;
            RIG_CHECK(registry.Initialize());
            C::TSharedPtr<Core::SkeletalAssetResource> asset;
            RIG_CHECK(
                S::AssembleRigSplitV1(cpu, {&registry, NorvesLib::Thread::Thread::GetCurrentThreadId()}, asset, bind));
            Core::Animation::SkeletalPoseSnapshot pose;
            RIG_CHECK(Core::Animation::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *asset->GetClip(0),
                                                                        *asset->GetMesh(), 1,
                                                                        F::MeshMatrix(*asset->GetMesh()), pose));
            RIG_CHECK(pose.BonePalette.size() == count);
            S::RigAuthoringCpu source;
            S::RigClipSourceSelection selection;
            RIG_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", profile,
                                                                 source, report, request.Limits, nullptr, nullptr,
                                                                 nullptr, &selection));
            RIG_CHECK(source.GetData()->LocalRest.size() == count && source.GetData()->Geometry.Vertices.empty());
            RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/rig.gltf", source, report));
            RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile,
                                                                  source, report, request.Limits));
            RIG_CHECK(!S::ParseSkeletonV1(cooked.Skeleton.Payload, skeleton, report, request.Limits, H::Profile));
        }
        const auto tooMany = JointCountSource(f, 257);
        S::RigAuthoringCpu source;
        S::RigV1Report report;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(tooMany), "RigV1Fixture/rig.gltf", profile,
                                                              source, report, request.Limits));
        f.SetBuffer(f.Binary);
    }
    void RootMotionRoundtrip(F::Fixture& f)
    {
        f.SetBuffer(f.Binary);
        const auto author = H::Import(f.Json);
        auto clip = author.GetData()->Geometry.Clips[0];
        clip.RootMotionJoint = 0;
        const float duration = clip.DurationSeconds;
        RIG_CHECK(duration > 0);
        clip.RootMotion = {{0, 0, 0, 0}, {duration * 0.5f, 1, 2, 0.4}, {duration, 3, 4, 0.8}};
        S::ClipBankV1 bank, parsed;
        S::RigV1Report report;
        RIG_CHECK(S::BuildClipBankV1({&author, 1}, bank, report, {}, H::Profile, nullptr, {&clip, 1}));
        F::Bytes bytes, again;
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report, {}, H::Profile));
        RIG_CHECK(F::U32(bytes, 28) == 9 && F::U32(bytes, 256 + 8 * 32) == 0x4e544d52);
        RIG_CHECK(S::ParseClipBankV1(bytes, parsed, report, {}, H::Profile));
        const auto& value = parsed.GetData()->Clips[0];
        RIG_CHECK(value.RootMotion.size() == 3 && value.RootMotion.back().TranslationX == 3 &&
                  value.RootMotion.back().TranslationZ == 4 && value.RootMotion.back().YawRadians == 0.8);
        RIG_CHECK(parsed.GetData()->Topology.Joints[value.RootMotionJoint].ParentIndex == -1);
        RIG_CHECK(S::WriteClipBankV1(parsed, again, report, {}, H::Profile) && again == bytes);
        S::RigClipAnalysisOptions options;
        options.TimeScale = 2;
        S::RigClipAnalysis analysis;
        S::SkeletalAnimationClip adjusted;
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, adjusted, analysis));
        RIG_CHECK(adjusted.RootMotion.back().TimeSeconds == duration * 2 && analysis.TranslationX == 3 &&
                  analysis.TotalYawRadians == 0.8);
        RIG_CHECK(S::BuildClipBankV1({&author, 1}, bank, report, {}, H::Profile, &options, {&clip, 1}));
        RIG_CHECK(S::WriteClipBankV1(bank, again, report, {}, H::Profile) && F::U32(again, 28) == 10);
        RIG_CHECK(S::ParseClipBankV1(again, parsed, report, {}, H::Profile) && parsed.GetData()->Analyses.size() == 1);
        const auto saved = parsed.GetData();
        const auto offset = F::SectionOffset(bytes, 8);
        for (size_t field : {size_t{4}, size_t{12}})
        {
            auto bad = bytes;
            F::Put32(bad, offset + field, UINT32_MAX);
            F::Reseal(bad);
            RIG_CHECK(!S::ParseClipBankV1(bad, parsed, report, {}, H::Profile) && parsed.GetData() == saved);
        }
        auto bad = bytes;
        F::Float(bad, offset + 40 + 8, 0);
        F::Reseal(bad);
        RIG_CHECK(!S::ParseClipBankV1(bad, parsed, report, {}, H::Profile) && parsed.GetData() == saved);
        bad = bytes;
        F::Put64(bad, offset + 16, std::bit_cast<uint64_t>(1.0));
        F::Reseal(bad);
        RIG_CHECK(!S::ParseClipBankV1(bad, parsed, report, {}, H::Profile) && parsed.GetData() == saved);
        auto pitchedJson =
            F::Replace(H::Armature(f.Json), R"("translation":[2,3,0],"rotation":[0,0,1,0],"scale":[2,2,2])",
                       R"("rotation":[0.7071067811865475,0,0,0.7071067811865475])");
        pitchedJson =
            F::Replace(pitchedJson, R"("translation":[-1,4,1],"rotation":[1,0,0,0])", R"("translation":[0,0,0])");
        const auto pitched = H::Import(pitchedJson);
        auto pitchedClip = clip;
        pitchedClip.Channels.clear();
        pitchedClip.Channels.push_back({0,
                                        S::SkeletalAnimationPath::Rotation,
                                        S::SkeletalAnimationInterpolation::Linear,
                                        {{0, {0, 0, 0, 1}}, {duration, {0, 0, 0, 1}}}});
        RIG_CHECK(S::AnalyzeRigClip(pitched, pitchedClip, {}, adjusted, analysis));
        RIG_CHECK(analysis.TotalYawRadians == 0.8 && analysis.TranslationX == 3);
        pitchedClip.RootMotion.clear();
        pitchedClip.RootMotionJoint = UINT32_MAX;
        RIG_CHECK(S::AnalyzeRigClip(pitched, pitchedClip, {}, adjusted, analysis) &&
                  std::abs(analysis.TotalYawRadians) < 1e-9);
        clip.RootMotionJoint = 1;
        RIG_CHECK(!S::BuildClipBankV1({&author, 1}, bank, report, {}, H::Profile, nullptr, {&clip, 1}));
    }
    void ClipAnalysis(F::Fixture& f)
    {
        const auto author = H::Import(f.Json);
        S::SkeletalAnimationClip clip;
        clip.Name = _T("Motion");
        clip.DurationSeconds = 1;
        S::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = S::SkeletalAnimationPath::Translation;
        channel.Interpolation = S::SkeletalAnimationInterpolation::Linear;
        channel.Samples.push_back({0, {0, 0, 0, 0}});
        channel.Samples.push_back({1, {2, 0, 0, 0}});
        clip.Channels.push_back(channel);
        S::RigClipAnalysisOptions options;
        options.RootJoint = 0;
        S::RigClipAnalysis analysis;
        S::SkeletalAnimationClip corrected;
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, corrected, analysis));
        RIG_CHECK(!analysis.bLoopCandidate && !analysis.bLoop && std::abs(analysis.TranslationX - 2) < 1e-6 &&
                  std::abs(analysis.AverageSpeedMetersPerSecond - 2) < 1e-6 &&
                  std::abs(analysis.TotalYawRadians) < 1e-6);
        clip.Channels[0].Samples.clear();
        for (uint32_t i = 0; i <= 4; ++i)
        {
            const float values[] = {0, 1, 0, -1, 0};
            clip.Channels[0].Samples.push_back({float(i) * 0.25f, {values[i], 0, 0, 0}});
        }
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, corrected, analysis) && analysis.bLoopCandidate &&
                  analysis.bLoop && analysis.LoopError < 1e-6);
        options.Loop = S::RigClipLoopMode::Once;
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, corrected, analysis) && analysis.bLoopCandidate &&
                  !analysis.bLoop);
        options.Loop = S::RigClipLoopMode::Auto;
        options.AuthoredFps = 30;
        options.SourceFps = 15;
        options.TimeScale = 2;
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, corrected, analysis) && corrected.DurationSeconds == 4 &&
                  corrected.Channels[0].Samples[1].TimeSeconds == 1 && analysis.SourceFps == 7.5);
        options = {};
        options.RootJoint = 0;
        clip.Channels[0].Path = S::SkeletalAnimationPath::Rotation;
        clip.Channels[0].Samples.clear();
        clip.Channels[0].Samples.push_back({0, {0, 0, 0, 1}});
        clip.Channels[0].Samples.push_back({1, {0, float(std::sqrt(0.5)), 0, float(std::sqrt(0.5))}});
        RIG_CHECK(S::AnalyzeRigClip(author, clip, options, corrected, analysis));
        RIG_CHECK(std::abs(analysis.TotalYawRadians - 1.5707963267948966) < 1e-5);
        const auto saved = corrected.DurationSeconds;
        options.MaximumSamples = 2;
        RIG_CHECK(!S::AnalyzeRigClip(author, clip, options, corrected, analysis) && corrected.DurationSeconds == saved);
        options = {};
        options.SourceFps = 16;
        RIG_CHECK(!S::AnalyzeRigClip(author, clip, options, corrected, analysis));

        options = {};
        options.RootJoint = 0;
        S::ClipBankV1 bank;
        S::RigV1Report report;
        RIG_CHECK(S::BuildClipBankV1({&author, 1}, bank, report, {}, H::Profile, &options));
        F::Bytes bytes;
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report, {}, H::Profile));
        RIG_CHECK(F::U32(bytes, 28) == 9 && F::U32(bytes, 256 + 8 * 32) == 0x594c4e41);
        RIG_CHECK(S::ParseClipBankV1(bytes, bank, report, {}, H::Profile));
        RIG_CHECK(bank.GetData()->Analyses.size() == 1 && bank.GetData()->Analyses[0].RootJoint == 1);
        F::Bytes rewritten;
        RIG_CHECK(S::WriteClipBankV1(bank, rewritten, report, {}, H::Profile) && rewritten == bytes);
        const size_t offset = F::SectionOffset(bytes, 8);
        for (unsigned error = 0; error < 3; ++error)
        {
            auto invalid = bytes;
            if (error == 0)
            {
                F::Put32(invalid, offset, UINT32_MAX);
            }
            if (error == 1)
            {
                F::Put32(invalid, offset + 4, 4);
            }
            if (error == 2)
            {
                invalid[offset + 72] = 1;
            }
            F::Reseal(invalid);
            RIG_CHECK(!S::ParseClipBankV1(invalid, bank, report, {}, H::Profile));
        }
        auto request = H::CookRequest();
        request.bAnalyzeClips = true;
        request.ClipAnalysis = options;
        const auto cooked = H::X::CookSource(f.Json, request);
        RIG_CHECK(S::ParseClipBankV1(cooked.Bank.Payload, bank, report, {}, H::Profile) &&
                  bank.GetData()->Analyses.size() == 1);
        RIG_CHECK(cooked.SourceHash != H::Cook(f.Json).SourceHash);
    }
    void Roundtrip(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        const auto cooked = H::Cook(json);
        const auto repeated = H::Cook(json);
        RIG_CHECK(cooked.Skeleton.Payload == repeated.Skeleton.Payload &&
                  cooked.Mesh.Payload == repeated.Mesh.Payload && cooked.Bank.Payload == repeated.Bank.Payload &&
                  cooked.SourceHash == repeated.SourceHash);
        S::RigV1Report report;
        S::SkeletonV1 skeleton;
        S::SkinMeshV1 mesh;
        S::ClipBankV1 bank;
        RIG_CHECK(S::ParseSkeletonV1(cooked.Skeleton.Payload, skeleton, report, {}, H::Profile));
        RIG_CHECK(S::ParseSkinMeshV1(cooked.Mesh.Payload, mesh, report, {}, H::Profile));
        RIG_CHECK(S::ParseClipBankV1(cooked.Bank.Payload, bank, report, {}, H::Profile));
        RIG_CHECK(S::SameRigRootFrame(skeleton.GetData()->RootTransform, H::ExpectedFrame()) &&
                  S::SameRigRootFrame(bank.GetData()->Snapshots[0].RootFrame, H::ExpectedFrame()));
        RIG_CHECK(skeleton.GetData()->Profile == H::Profile && mesh.GetData()->Profile == H::Profile &&
                  bank.GetData()->Profile == H::Profile);
        F::Bytes bytes;
        RIG_CHECK(S::WriteSkeletonV1(skeleton, bytes, report, {}, H::Profile) && bytes == cooked.Skeleton.Payload);
        RIG_CHECK(S::WriteSkinMeshV1(mesh, bytes, report, {}, H::Profile) && bytes == cooked.Mesh.Payload);
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report, {}, H::Profile) && bytes == cooked.Bank.Payload);
        const auto old = bytes;
        RIG_CHECK(!S::WriteClipBankV1(bank, bytes, report) && bytes == old);
        RIG_CHECK(!S::WriteSkeletonV1(skeleton, bytes, report) && bytes == old);
        RIG_CHECK(!S::WriteSkinMeshV1(mesh, bytes, report) && bytes == old);
        RIG_CHECK(!S::ParseSkeletonV1(cooked.Skeleton.Payload, skeleton, report));
        RIG_CHECK(!S::ParseSkinMeshV1(cooked.Mesh.Payload, mesh, report));
        RIG_CHECK(!S::ParseClipBankV1(cooked.Bank.Payload, bank, report));
        RIG_CHECK(cooked.Skeleton.Payload.size() == 704 && cooked.Mesh.Payload.size() == 1360 &&
                  cooked.Bank.Payload.size() == 1088);
        RIG_CHECK(F::U32(cooked.Bank.Payload, 28) == 8 && F::U32(cooked.Bank.Payload, 256 + 7 * 32) == 0x4d524641);
        if (const char* output = std::getenv("NORVES_ROOT_FRAME_V2_OUTPUT"))
        {
            std::filesystem::create_directories(output);
            F::WriteBytes(std::filesystem::path(output) / "skeleton-p2.nvskel", cooked.Skeleton.Payload);
            F::WriteBytes(std::filesystem::path(output) / "mesh-p2.nvskel", cooked.Mesh.Payload);
            F::WriteBytes(std::filesystem::path(output) / "bank-p2.nvclip", cooked.Bank.Payload);
        }
        std::printf(
            "ROOT_FRAME_WIRE_CASE result=pass three_roles_explicit_profile_afrm_roundtrip_determinism_native_bytes\n");
    }
    void Corruption(F::Fixture& f)
    {
        const auto cooked = H::Cook(H::Armature(f.Json));
        S::RigV1Report report;
        for (unsigned role = 1; role <= 3; ++role)
        {
            const auto& source = role == 1   ? cooked.Skeleton.Payload
                                 : role == 2 ? cooked.Mesh.Payload
                                             : cooked.Bank.Payload;
            S::SkeletonV1 skeleton;
            S::SkinMeshV1 mesh;
            S::ClipBankV1 bank;
            const auto parse = [&](const F::Bytes& b, const S::RigV1Limits& limits = S::RigV1Limits{})
            {
                if (role == 1)
                {
                    return S::ParseSkeletonV1(b, skeleton, report, limits, H::Profile);
                }
                if (role == 2)
                {
                    return S::ParseSkinMeshV1(b, mesh, report, limits, H::Profile);
                }
                return S::ParseClipBankV1(b, bank, report, limits, H::Profile);
            };
            RIG_CHECK(parse(source));
            const auto* sk = skeleton.GetData();
            const auto* sm = mesh.GetData();
            const auto* bk = bank.GetData();
            for (size_t n = 0; n < source.size(); ++n)
            {
                F::Bytes truncated(source.begin(), source.begin() + n);
                RIG_CHECK(!parse(truncated));
                RIG_CHECK(skeleton.GetData() == sk && mesh.GetData() == sm && bank.GetData() == bk);
            }
            auto optional = W::Optional(source);
            RIG_CHECK(parse(optional));
            const size_t entry = 256 + F::U32(source, 28) * 32;
            F::Put32(optional, entry + 4, 1);
            F::Reseal(optional);
            RIG_CHECK(!parse(optional));
            optional = W::Optional(source);
            F::Put32(optional, entry, role == 3 ? 0x544f4f52 : 0x4d524641);
            F::Reseal(optional);
            RIG_CHECK(!parse(optional));
            auto wrong = source;
            F::Put32(wrong, 64, 1);
            RIG_CHECK(!parse(wrong));
            S::RigV1Limits limit;
            limit.MaxWireBytes = source.size();
            RIG_CHECK(parse(source, limit));
            --limit.MaxWireBytes;
            RIG_CHECK(!parse(source, limit));
        }
        const auto& source = cooked.Bank.Payload;
        S::ClipBankV1 bank;
        RIG_CHECK(S::ParseClipBankV1(source, bank, report, {}, H::Profile));
        const auto* saved = bank.GetData();
        const auto rejected = [&](F::Bytes bytes)
        {
            F::Reseal(bytes);
            RIG_CHECK(!S::ParseClipBankV1(bytes, bank, report, {}, H::Profile) && bank.GetData() == saved);
        };
        const size_t frame = F::SectionOffset(source, 7);
        const size_t entry = 256 + 7 * 32;
        auto b = source;
        F::Put32(b, entry + 4, 0);
        rejected(b);
        b = source;
        F::Put32(b, entry, 0x54534554);
        F::Put32(b, entry + 4, 0);
        rejected(b);
        b = source;
        F::Put32(b, entry + 28, 2);
        rejected(b);
        b = source;
        F::Put32(b, entry + 24, 32);
        rejected(b);
        b = source;
        F::Put64(b, entry + 8, UINT64_MAX - 15);
        rejected(b);
        b = source;
        F::Put32(b, entry, 0x54535241);
        rejected(b);
        for (float value :
             {-0.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.1f})
        {
            b = source;
            F::Float(b, frame + 4, value);
            rejected(b);
        }
        b = source;
        F::Float(b, frame, 2);
        rejected(b);
        b = source;
        F::Float(b, frame + 60, 2);
        rejected(b);
        b = source;
        F::Put32(b, F::SectionOffset(b, 2) + 20, 1);
        rejected(b);
        S::SkeletonV1 skeleton;
        b = cooked.Skeleton.Payload;
        F::Float(b, F::SectionOffset(b, 4) + 4, -0.f);
        F::Reseal(b);
        RIG_CHECK(!S::ParseSkeletonV1(b, skeleton, report, {}, H::Profile));
        S::SkinMeshV1 mesh;
        b = cooked.Mesh.Payload;
        F::Put32(b, F::SectionOffset(b, 2) + 12, 1);
        F::Reseal(b);
        RIG_CHECK(!S::ParseSkinMeshV1(b, mesh, report, {}, H::Profile));
        std::printf(
            "ROOT_FRAME_WIRE_CASE result=pass truncated_optional_required_profile_afrm_counts_domain_hash_budget_atomicity\n");
    }
    void Binding(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        const auto cooked = H::Cook(json);
        S::RigV1Report report;
        S::SkeletonV1 skeleton;
        S::SkinMeshV1 mesh;
        RIG_CHECK(S::ParseSkeletonV1(cooked.Skeleton.Payload, skeleton, report, {}, H::Profile) &&
                  S::ParseSkinMeshV1(cooked.Mesh.Payload, mesh, report, {}, H::Profile));
        const auto author = H::Import(json);
        const auto other = H::Import(F::Replace(F::Replace(json, "\"name\":\"Wave\"", "\"name\":\"Walk\""),
                                                "\"translation\":[2,3,0]", "\"translation\":[3,3,0]"));
        C::FixedArray<S::RigAuthoringCpu, 2> sources{author, other};
        S::ClipBankV1 bank;
        RIG_CHECK(S::BuildClipBankV1(sources, bank, report, {}, H::Profile));
        F::Bytes bytes;
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report, {}, H::Profile));
        RIG_CHECK(S::ParseClipBankV1(bytes, bank, report, {}, H::Profile) && bank.GetData()->Snapshots.size() == 2);
        S::CookedRigSplitCpuAsset cpu;
        S::RigSplitReport bind;
        S::RigBindingPolicy policy;
        for (bool allow : {false, true})
        {
            policy.bAllowRestMismatch = allow;
            RIG_CHECK(!S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, policy, cpu, bind, {}, H::Profile) &&
                      !cpu.GetData());
            RIG_CHECK(bind.Status == S::RigV1Status::FrameMismatch && bind.Banks.size() == 1 &&
                      bind.Banks[0].bFrameComparisonComplete && !bind.Banks[0].bComparisonComplete &&
                      !bind.Banks[0].bOverrideUsed);
            RIG_CHECK(bind.Banks[0].FrameComparisons.size() == 2 && bind.Banks[0].FrameComparisons[0].bEqual &&
                      !bind.Banks[0].FrameComparisons[1].bEqual &&
                      bind.Banks[0].FrameComparisons[1].MaximumAbsoluteMatrixDifference == 1);
        }
        sources[1] = H::Import(F::Replace(json, "\"name\":\"Wave\"", "\"name\":\"Walk\""));
        RIG_CHECK(S::BuildClipBankV1(sources, bank, report, {}, H::Profile));
        RIG_CHECK(S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, {}, cpu, bind, {}, H::Profile) &&
                  cpu.GetData()->Clips.size() == 2);
        RIG_CHECK(bind.Banks[0].bFrameComparisonComplete && bind.Banks[0].bComparisonComplete &&
                  !bind.Banks[0].bOverrideUsed);
        const auto* saved = cpu.GetData();
        RIG_CHECK(!S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, {}, cpu, bind) && cpu.GetData() == saved);
        const auto old = f.Bank(f.Json);
        RIG_CHECK(!S::BindRigSplitV1(skeleton, mesh, {&old, 1}, policy, cpu, bind, {}, H::Profile) &&
                  cpu.GetData() == saved);
        S::RigV1Limits limits;
        limits.MaxSnapshots = 1;
        RIG_CHECK(!S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, {}, cpu, bind, limits, H::Profile) &&
                  cpu.GetData() == saved);
        limits = {};
        limits.MaxWireBytes = bytes.size() - 1;
        F::Bytes out = bytes;
        RIG_CHECK(!S::WriteClipBankV1(bank, out, report, limits, H::Profile) && out == bytes);
        // B1の旧binderへ新profileを渡して作者frame検査を迂回しない。
        S::BoundClipBank legacyBound;
        RIG_CHECK(!S::BindClipBankV1(bank, author, {}, legacyBound, report) &&
                  report.Status == S::RigV1Status::UnsupportedProfile);
        std::printf(
            "ROOT_FRAME_WIRE_CASE result=pass multisnapshot_frame_guard_override_refusal_same_frame_rest_bind_cross_profile_limits\n");
    }
} // namespace
int main()
{
    F::Fixture f;
    Roundtrip(f);
    Corruption(f);
    Binding(f);
    JointCapacity(f);
    ClipAnalysis(f);
    RootMotionRoundtrip(f);
    std::printf("ROOT_FRAME_WIRE result=pass mandatory_author_frames_three_roles_safe_binding\n");
    return 0;
}
