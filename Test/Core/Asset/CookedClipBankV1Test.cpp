// 独立wire値と実import/writer/parserを往復する。旧fixtureを再生成しない。
#include "ClipBankV1Fixture.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Animation/SkeletalSamplingMath.h"
#include "Resource/GltfBufferFile.h"
#include "Asset/ClipQuantization.h"
#include <cmath>
#include <limits>
#include <random>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace S = NorvesLib::Core::Skeletal;
namespace C = NorvesLib::Core::Container;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace
{


    void RotationQuantizationRandom()
    {
        std::mt19937 random(0x51434c50u);
        std::normal_distribution<double> normal(0.0, 1.0);
        double maximumAngle = 0.0;
        for (uint32_t i = 0; i < 100000; ++i)
        {
            double components[4] = {normal(random), normal(random), normal(random), normal(random)};
            const double inverseLength = 1.0 / std::sqrt(
                components[0] * components[0] + components[1] * components[1] +
                components[2] * components[2] + components[3] * components[3]);
            const S::SkeletalValue original{float(components[0] * inverseLength), float(components[1] * inverseLength),
                                            float(components[2] * inverseLength), float(components[3] * inverseLength)};
            const auto decoded = S::DecodeRotationSmallest3_32(S::EncodeRotationSmallest3_32(original));
            // 返された float の長さの丸めも除き、4 次元の角度を独立に求める。
            const double a[4] = {original.X, original.Y, original.Z, original.W};
            const double b[4] = {decoded.X, decoded.Y, decoded.Z, decoded.W};
            double dot = 0.0, lengthA = 0.0, lengthB = 0.0;
            for (uint32_t component = 0; component < 4; ++component)
            {
                dot += a[component] * b[component];
                lengthA += a[component] * a[component];
                lengthB += b[component] * b[component];
            }
            dot = std::fabs(dot) / std::sqrt(lengthA * lengthB);
            const double angle = std::acos(std::fmin(1.0, dot)) * 57.295779513082320877;
            maximumAngle = std::fmax(maximumAngle, angle);
            RIG_CHECK(angle <= 0.1);
        }
        std::printf("CLIP_QUANTIZATION_RANDOM result=pass samples=100000 max_angle_4d_degrees=%.12f\n", maximumAngle);
    }

    void RotationQuantizationBoundaries()
    {
        const float halfRoot = float(1.0 / std::sqrt(2.0));
        const S::SkeletalValue rotations[] = {
            {0.5f, 0.5f, 0.5f, 0.5f},
            {halfRoot, halfRoot, 0.0f, 0.0f},
            {0.0f, 0.0f, halfRoot, -halfRoot},
            {0.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f, -1.0f},
            {-0.8f, 0.2f, -0.3f, 0.4f}};
        for (const auto& original : rotations)
        {
            const uint32_t bits32 = S::EncodeRotationSmallest3_32(original);
            const uint64_t bits48 = S::EncodeRotationSmallest3_48(original);
            RIG_CHECK(S::RotationAngleDifferenceDegrees(original, S::DecodeRotationSmallest3_32(bits32)) <= 0.2);
            RIG_CHECK(S::RotationAngleDifferenceDegrees(original, S::DecodeRotationSmallest3_48(bits48)) <= 0.01);
            RIG_CHECK(S::IsValidRotationSmallest3_48(bits48));
            RIG_CHECK(!S::IsValidRotationSmallest3_48(bits48 | (uint64_t{1} << 45)));
        }
        std::printf("CLIP_QUANTIZATION_BOUNDARIES result=pass smallest_three_32_48_reserved_bit\n");
    }

    void TranslationQuantization()
    {
        std::mt19937 random(0x54524e53u);
        std::uniform_real_distribution<float> uniform(-5.0f, 5.0f);
        S::SkeletalAnimationChannel channel;
        channel.Samples.resize(4096);
        for (auto& sample : channel.Samples)
        {
            sample.Value = {uniform(random), uniform(random), uniform(random), 0.0f};
        }
        for (uint32_t pass = 0; pass < 2; ++pass)
        {
            // 2 回目は 1 成分を固定し、ゼロ幅の範囲も同じ経路で確かめる。
            if (pass == 1)
            {
                for (auto& sample : channel.Samples)
                {
                    sample.Value.Y = 1.25f;
                }
            }
            const auto range = S::ComputeTranslationQuantRange({channel.Samples.data(), channel.Samples.size()});
            if (pass == 1)
            {
                RIG_CHECK(range.Extent[1] == 0.0f);
            }
            for (const auto& sample : channel.Samples)
            {
                uint16_t codes[3];
                S::EncodeTranslation16(sample.Value, range, codes);
                const auto decoded = S::DecodeTranslation16(codes, range);
                const float original[3] = {sample.Value.X, sample.Value.Y, sample.Value.Z};
                const float restored[3] = {decoded.X, decoded.Y, decoded.Z};
                for (uint32_t component = 0; component < 3; ++component)
                {
                    RIG_CHECK(std::fabs(double(original[component]) - restored[component]) <=
                              double(range.Extent[component]) / 65535.0);
                    if (range.Extent[component] == 0.0f)
                    {
                        RIG_CHECK(codes[component] == 0 && restored[component] == original[component]);
                    }
                }
                RIG_CHECK(decoded.W == 0.0f);
            }
        }
        std::printf("CLIP_QUANTIZATION_TRANSLATION result=pass quant16_error_bound_constant_component\n");
    }

    size_t CompressionDirectory(const F::Bytes& bytes)
    {
        for (uint32_t i = 0; i < F::U32(bytes, 28); ++i)
        {
            const size_t entry = 256 + size_t(i) * 32;
            if (F::U32(bytes, entry) == 0x52504d43)
            {
                return entry;
            }
        }
        return 0;
    }

    S::ClipBankV1 CompressionBank(F::Fixture& f)
    {
        const auto source = f.Import(f.Json);
        S::SkeletalAnimationClip clip;
        clip.Name = _T("Compression");
        clip.DurationSeconds = 1.0f;
        clip.Channels.resize(6);
        constexpr float irregular[] = {0.0f, 0.021f, 0.057f, 0.101f, 0.19f};
        for (uint32_t c = 0; c < 6; ++c)
        {
            auto& channel = clip.Channels[c];
            channel.JointIndex = c / 3;
            channel.Path = static_cast<S::SkeletalAnimationPath>(c % 3);
            channel.Interpolation = c >= 3 ? S::SkeletalAnimationInterpolation::Step :
                                            S::SkeletalAnimationInterpolation::Linear;
            for (uint32_t i = 0; i < 5; ++i)
            {
                const float t = float(i) / 30.0f;
                S::SkeletalValue value;
                switch (c)
                {
                case 0:
                    value = {-2.0f + float(i) * 0.71f, 1.25f, 3.0f - float(i) * 0.43f, 0.0f};
                    break;
                case 1:
                    value = {0.0f, std::sin(float(i) * 0.17f), 0.0f, std::cos(float(i) * 0.17f)};
                    break;
                case 2:
                case 5:
                    value = {1.0f + t, 2.0f - t, 0.5f + t, -0.0f};
                    break;
                case 3:
                    value = {1.25f, -2.5f, 0.0f, -0.0f};
                    break;
                case 4:
                    value = {0.0f, -0.0f, 0.0f, -1.0f};
                    break;
                }
                channel.Samples.push_back({c < 4 ? t : irregular[i], value});
            }
        }
        S::ClipBankV1 bank;
        S::RigV1Report report;
        RIG_CHECK(S::BuildClipBankV1({&source, 1}, bank, report, {}, S::RigImportProfile::DirectTrs128,
                                     nullptr, {&clip, 1}));
        return bank;
    }

    void CompressionRoundtrip(F::Fixture& f)
    {
        const auto original = CompressionBank(f);
        S::RigV1Report report;
        F::Bytes bytes;
        RIG_CHECK(S::WriteClipBankV1(original, bytes, report, {}, S::RigImportProfile::DirectTrs128,
                                     S::ClipBankV1WriteOptions{.bQuantize = true}));
        RIG_CHECK(bytes[14] == 1 && bytes[15] == 0);
        const size_t directory = CompressionDirectory(bytes);
        RIG_CHECK(directory && F::U32(bytes, directory + 4) == 1 && F::U32(bytes, directory + 24) == 4);
        const size_t compression = size_t(F::U64(bytes, directory + 8));
        RIG_CHECK(F::U32(bytes, compression) == 1 && F::U32(bytes, compression + 4) == 6);
        const size_t dataStart = compression + 16 + 6 * 32;
        const size_t dataEnd = compression + size_t(F::U64(bytes, directory + 16));
        RIG_CHECK(F::U64(bytes, compression + 8) == F::Hash({bytes.data() + dataStart, dataEnd - dataStart}));
        const auto channels = F::SectionOffset(bytes, 5);
        constexpr uint32_t encodings[] = {3, 1, 0, 4, 4, 0};
        S::ClipBankV1 restored;
        RIG_CHECK(S::ParseClipBankV1(F::View(bytes), restored, report));
        RIG_CHECK(restored.GetData()->Clips.size() == original.GetData()->Clips.size());
        const auto& expectedClip = original.GetData()->Clips[0];
        const auto& actualClip = restored.GetData()->Clips[0];
        RIG_CHECK(actualClip.Name == expectedClip.Name && actualClip.DurationSeconds == expectedClip.DurationSeconds &&
                  actualClip.Channels.size() == expectedClip.Channels.size());
        for (size_t c = 0; c < expectedClip.Channels.size(); ++c)
        {
            const auto& expected = expectedClip.Channels[c];
            const auto& actual = actualClip.Channels[c];
            const uint32_t encoding = F::U32(bytes, channels + c * 48 + 28);
            RIG_CHECK(c == 1 ? (encoding == 1 || encoding == 2) : encoding == encodings[c]);
            RIG_CHECK(F::U32(bytes, compression + 16 + c * 32) == (c < 4 ? 1u : 0u));
            RIG_CHECK(actual.JointIndex == expected.JointIndex && actual.Path == expected.Path &&
                      actual.Interpolation == expected.Interpolation && actual.Samples.size() == expected.Samples.size());
            RIG_CHECK(c < 4 ? actual.UniformStepSeconds > 0 : actual.UniformStepSeconds == 0);
            const auto range = S::ComputeTranslationQuantRange({expected.Samples.data(), expected.Samples.size()});
            for (size_t k = 0; k < expected.Samples.size(); ++k)
            {
                const auto& a = expected.Samples[k];
                const auto& b = actual.Samples[k];
                if (c < 4)
                {
                    RIG_CHECK(std::fabs(double(a.TimeSeconds) - b.TimeSeconds) <= 1.0 / 30.0 * 1e-3);
                }
                else
                {
                    RIG_CHECK(std::bit_cast<uint32_t>(a.TimeSeconds) == std::bit_cast<uint32_t>(b.TimeSeconds));
                }
                if (c == 1)
                {
                    RIG_CHECK(S::RotationAngleDifferenceDegrees(a.Value, b.Value) <= 0.2);
                }
                else if (c == 0)
                {
                    const float first[] = {a.Value.X, a.Value.Y, a.Value.Z};
                    const float second[] = {b.Value.X, b.Value.Y, b.Value.Z};
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        RIG_CHECK(std::fabs(double(first[axis]) - second[axis]) <= double(range.Extent[axis]) / 65535.0);
                    }
                    RIG_CHECK(std::bit_cast<uint32_t>(a.Value.W) == std::bit_cast<uint32_t>(b.Value.W));
                }
                else
                {
                    // 定数と scale は符号付きゼロを含む全成分をそのまま保つ。
                    RIG_CHECK(std::bit_cast<uint32_t>(a.Value.X) == std::bit_cast<uint32_t>(b.Value.X) &&
                              std::bit_cast<uint32_t>(a.Value.Y) == std::bit_cast<uint32_t>(b.Value.Y) &&
                              std::bit_cast<uint32_t>(a.Value.Z) == std::bit_cast<uint32_t>(b.Value.Z) &&
                              std::bit_cast<uint32_t>(a.Value.W) == std::bit_cast<uint32_t>(b.Value.W));
                }
            }
        }
        F::Bytes plain;
        RIG_CHECK(S::WriteClipBankV1(restored, plain, report));
        RIG_CHECK(plain[14] == 0 && plain[15] == 0 && !CompressionDirectory(plain));
        std::printf("CLIPBANK_V1_CASE result=pass compressed_roundtrip_error_bounds_uniform_times_plain_rewrite\n");
    }

    void CompressionLegacyBytes(F::Fixture& f)
    {
        F::Bytes cooked;
        const auto bank = f.Bank(f.Json, &cooked);
        S::RigV1Report report;
        F::Bytes defaults, explicitOptions;
        RIG_CHECK(S::WriteClipBankV1(bank, defaults, report));
        RIG_CHECK(S::WriteClipBankV1(bank, explicitOptions, report, {}, S::RigImportProfile::DirectTrs128,
                                     S::ClipBankV1WriteOptions{}));
        RIG_CHECK(defaults == explicitOptions && defaults == cooked);
        RIG_CHECK(defaults[14] == 0 && defaults[15] == 0 && !CompressionDirectory(defaults));
        RIG_CHECK(F::U32(defaults, 28) == 7);
        RIG_CHECK(F::U64(defaults, 48) == F::Hash({defaults.data() + 256, defaults.size() - 256}));
        std::printf("CLIPBANK_V1_CASE result=pass unquantized_default_explicit_options_legacy_bytes\n");
    }

    void CompressionReject(F::Fixture& f)
    {
        auto bank = CompressionBank(f);
        S::RigV1Report report;
        F::Bytes bytes;
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report, {}, S::RigImportProfile::DirectTrs128,
                                     S::ClipBankV1WriteOptions{.bQuantize = true}));
        const auto* goodData = bank.GetData();
        const size_t directory = CompressionDirectory(bytes);
        RIG_CHECK(directory);
        const size_t compression = size_t(F::U64(bytes, directory + 8));
        const size_t channels = F::SectionOffset(bytes, 5);
        const size_t count = bank.GetData()->Clips[0].Channels.size();
        const auto reseal = [&](F::Bytes& broken, bool bDataHash = true)
        {
            if (bDataHash)
            {
                // 表を壊した場合も、データ域の境界は元のチャンネル数を使う。
                const size_t start = compression + 16 + count * 32;
                const size_t end = compression + size_t(F::U64(broken, directory + 16));
                F::Put64(broken, compression + 8, F::Hash({broken.data() + start, end - start}));
            }
            F::Reseal(broken);
        };
        const auto reject = [&](F::Bytes broken, S::RigV1Status status = S::RigV1Status::BadWire,
                                bool bDataHash = true)
        {
            reseal(broken, bDataHash);
            RIG_CHECK(F::U64(broken, 48) == F::Hash({broken.data() + 256, broken.size() - 256}));
            RIG_CHECK(!S::ParseClipBankV1(F::View(broken), bank, report) && bank.GetData() == goodData);
            RIG_CHECK(report.Status == status);
        };
        auto broken = bytes;
        broken[compression + 8] ^= 1;
        reject(broken, S::RigV1Status::HashMismatch, false);
        const auto modify = [&](size_t at, uint32_t value)
        {
            auto broken = bytes;
            F::Put32(broken, at, value);
            reject(broken);
        };
        modify(compression, 2);                          // 未対応の CMPR version。
        modify(compression + 4, uint32_t(count + 1));     // CHAN と異なる channelCount。
        modify(compression + 16 + 16, F::U32(bytes, compression + 16 + 16) + 4);
        modify(compression + 16 + 4, 4);                 // CHAN と異なる sampleCount。
        modify(compression + 16 + 24, 1);                // エントリ予約領域。
        modify(compression + 16, 2);                     // 未対応の timeForm。
        modify(channels + 28, 1);                       // Translation に回転 encoding。
        broken = bytes;
        broken[14] = 0;
        broken[15] = 0;
        reject(broken);
        broken = bytes;
        broken[14] = 2;
        broken[15] = 0;
        reject(broken, S::RigV1Status::UnsupportedVersion);

        // 有効な 48bit 回転ブロックを作り、予約 bit 以外の理由で拒否されないことを先に確認する。
        constexpr size_t rotation = 1;
        const size_t entry = compression + 16 + rotation * 32;
        const size_t block = size_t(F::U32(bytes, entry + 16));
        const size_t oldSize = size_t(F::U32(bytes, entry + 20));
        const auto& samples = bank.GetData()->Clips[0].Channels[rotation].Samples;
        const size_t newSize = samples.size() * 6;
        const size_t oldAligned = (oldSize + 3) & ~size_t{3};
        const size_t newAligned = (newSize + 3) & ~size_t{3};
        RIG_CHECK(F::U32(bytes, entry) == 1 && (F::U32(bytes, channels + rotation * 48 + 28) == 1 ||
                   F::U32(bytes, channels + rotation * 48 + 28) == 2) &&
                  compression + F::U64(bytes, directory + 16) == bytes.size() && newAligned >= oldAligned);
        const size_t growth = newAligned - oldAligned;
        const size_t tail = compression + block + oldAligned;
        F::Bytes wide(bytes.size() + growth, 0);
        std::memcpy(wide.data(), bytes.data(), tail);
        std::memcpy(wide.data() + tail + growth, bytes.data() + tail, bytes.size() - tail);
        F::Put64(wide, 40, wide.size());
        const uint64_t sectionSize = F::U64(bytes, directory + 16) + growth;
        F::Put64(wide, directory + 16, sectionSize);
        F::Put32(wide, directory + 28, uint32_t(sectionSize / 4));
        F::Put32(wide, channels + rotation * 48 + 28, 2);
        F::Put32(wide, entry + 20, uint32_t(newSize));
        for (size_t c = rotation + 1; c < count; ++c)
        {
            const size_t offset = compression + 16 + c * 32 + 16;
            F::Put32(wide, offset, F::U32(bytes, offset) + uint32_t(growth));
        }
        for (size_t k = 0; k < samples.size(); ++k)
        {
            const uint64_t bits = S::EncodeRotationSmallest3_48(samples[k].Value);
            for (size_t byte = 0; byte < 6; ++byte)
            {
                wide[compression + block + k * 6 + byte] = uint8_t(bits >> (byte * 8));
            }
        }
        reseal(wide);
        S::ClipBankV1 valid48;
        RIG_CHECK(S::ParseClipBankV1(F::View(wide), valid48, report));
        wide[compression + block + 5] |= 0x20;
        reject(wide);

        F::Bytes plain;
        RIG_CHECK(S::WriteClipBankV1(bank, plain, report) && !CompressionDirectory(plain));
        F::Put32(plain, F::SectionOffset(plain, 5) + 28, 1);
        F::Reseal(plain);
        RIG_CHECK(!S::ParseClipBankV1(F::View(plain), bank, report) && bank.GetData() == goodData &&
                  report.Status == S::RigV1Status::BadWire);
        std::printf("CLIPBANK_V1_CASE result=pass compressed_reject_12_corruptions_preserve_output\n");
    }

    void MetadataCodec(F::Fixture& f)
    {
        namespace A = NorvesLib::Core::Animation;
        const auto json = F::Replace(
            f.Json, "\"name\":\"Wave\"",
            R"("name":"Wave","extras":{"norves":{"events":[{"name":"step","t":0.2,"value":0.5,"intValue":-7},{"name":"hit","t":0.3,"end":0.8}],"markers":[{"name":"left","t":0.1},{"name":"right","t":0.6}],"rootMotion":{"joint":0,"mode":"extract","nominalSpeed":2},"loop":{"start":0.1,"end":0.9},"groundOffset":0.03}})");
        auto rig = f.Import(json);
        const auto& base = rig.GetData()->Geometry.Clips[0].Metadata;
        RIG_CHECK(base.Events.size() == 2 && base.Markers.size() == 2 && base.Root.Joint == 0);
        S::ClipBankV1 bank;
        S::RigV1Report report;
        F::Bytes bytes;
        RIG_CHECK(S::BuildClipBankV1({&rig, 1}, bank, report));
        auto expected = base;
        expected.Root.Joint = rig.GetData()->Topology.SourceToCanonical[0];
        RIG_CHECK(A::SameClipMetadata(expected, bank.GetData()->Clips[0].Metadata));
        RIG_CHECK(S::WriteClipBankV1(bank, bytes, report));
        RIG_CHECK(S::ParseClipBankV1(F::View(bytes), bank, report));
        RIG_CHECK(A::SameClipMetadata(expected, bank.GetData()->Clips[0].Metadata));
        F::Bytes again;
        RIG_CHECK(S::WriteClipBankV1(bank, again, report) && again == bytes);
        const auto section = [&](uint32_t code) {
            for (uint32_t i = 0; i < F::U32(bytes, 28); ++i)
                if (F::U32(bytes, 256 + i * 32) == code)
                    return F::SectionOffset(bytes, i);
            return size_t(0);
        };
        const auto event = section(0x544e5645), marker = section(0x4b52414d), meta = section(0x4154454d);
        RIG_CHECK(event && marker && meta && F::U32(bytes, event + 28) == uint32_t(-7));
        const auto good = bank.GetData();
        const auto reject = [&](size_t at, uint32_t value) {
            auto bad = bytes;
            F::Put32(bad, at, value);
            F::Reseal(bad);
            RIG_CHECK(!S::ParseClipBankV1(F::View(bad), bank, report) && bank.GetData() == good);
        };
        reject(meta, 1);
        reject(meta + 4, 4097);
        reject(meta + 16, 64);
        reject(meta + 20, 999);
        reject(meta + 40, 1);
        reject(event + 12, 0x7fc00000);
        reject(event + 8, 0xffffffff);
        reject(marker + 12, 0x7f800000);
        NorvesLib::Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        const auto scaled = f.Import(json, "RigV1Fixture/metadata-scaled.gltf", &settings);
        RIG_CHECK(std::fabs(scaled.GetData()->Geometry.Clips[0].Metadata.GroundOffset - .06f) < 1e-6f &&
                  scaled.GetData()->Geometry.Clips[0].Metadata.Root.NominalSpeed == 4);
        S::RigAuthoringCpu ignored;
        for (const char* joint : {"999", "1"})
        {
            const auto broken = F::Replace(json, "\"joint\":0", F::Text("\"joint\":") + joint);
            RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(broken), "RigV1Fixture/rig.gltf", ignored, report));
        }
        S::RigV1Limits limits;
        limits.MaxNameBytes = 8;
        const auto longName = F::Replace(json, "\"name\":\"step\"", "\"name\":\"step_name_too_long\"");
        RIG_CHECK(
            !S::DecodeRigAuthoringNativePath(F::View(longName), "RigV1Fixture/rig.gltf", ignored, report, limits));
        for (const char* metadata :
             {R"({"rootMotion":{"mode":"extract"}})", R"({"markers":[{"name":"left","t":0.2}]})"})
        {
            const auto one = F::Replace(f.Json, "\"name\":\"Wave\"",
                                        F::Text("\"name\":\"Wave\",\"extras\":{\"norves\":") + metadata + "}");
            auto oneRig = f.Import(one);
            S::ClipBankV1 oneBank;
            F::Bytes oneBytes;
            RIG_CHECK(S::BuildClipBankV1({&oneRig, 1}, oneBank, report) &&
                      S::WriteClipBankV1(oneBank, oneBytes, report) &&
                      S::ParseClipBankV1(F::View(oneBytes), oneBank, report));
            RIG_CHECK(A::SameClipMetadata(oneRig.GetData()->Geometry.Clips[0].Metadata,
                                          oneBank.GetData()->Clips[0].Metadata));
        }
        const auto invalid = F::Replace(json, "\"t\":0.2", "\"t\":999");
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(invalid), "RigV1Fixture/rig.gltf", ignored, report));
    }
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
    void MeshIndependentClips(F::Fixture& f)
    {
        constexpr auto profile = S::RigImportProfile::StaticRootFrame128;
        auto text = F::Replace(f.Json, "\"nodes\":[0,2]", "\"nodes\":[0]");
        text = F::Replace(text, ",{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0}", "");
        text = F::Replace(
            text,
            "\"meshes\":[{\"name\":\"Triangle\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":5},\"indices\":8,\"mode\":4}]}],",
            "");
        text = F::Replace(text, "\"inverseBindMatrices\":9,", "");
        text = F::ChildTrs(text, "\"translation\":[0,7,0]");
        S::RigClipSourceSelection selection;
        S::RigAuthoringCpu source;
        S::RigV1Report report;
        const auto import = [&](const F::Text& json, const S::RigClipSourceSelection& selected,
                                const NorvesLib::Core::AssetImport::LoadedImportSettings* settings = nullptr)
        {
            return S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/clip.gltf", profile, source,
                                                              report, {}, settings, nullptr, nullptr, &selected);
        };
        // 元ファイルに非indexedのmeshが残っていても、clip抽出はその形状を読まない。
        const auto nonIndexed = F::Replace(f.Json, "\"indices\":8,", "");
        RIG_CHECK(import(nonIndexed, selection));
        RIG_CHECK(import(text, selection));
        RIG_CHECK(source.GetData()->Geometry.Vertices.empty() && source.GetData()->Geometry.Indices.empty());
        RIG_CHECK(source.GetData()->LocalRest[1].Translation.Y == 7 &&
                  source.GetData()->Geometry.Clips[0].Channels[0].Samples[1].Value.Y == 3);
        S::ClipBankV1 bank;
        RIG_CHECK(S::BuildClipBankV1({&source, 1}, bank, report, {}, profile));
        RIG_CHECK(bank.GetData()->Snapshots[0].Rest[0].Translation.Y == 7);
        F::Bytes skinBytes;
        RIG_CHECK(S::WriteClipBankV1(bank, skinBytes, report, {}, profile));
        RIG_CHECK(S::ParseClipBankV1(F::View(skinBytes), bank, report, {}, profile));

        // skinの無いアニメーションにも、動いていないjointを含む作者骨格を明示する。
        const auto noSkin = F::Replace(text, "\"skins\":[{\"name\":\"Rig\",\"skeleton\":0,\"joints\":[0,1]}],", "");
        const uint32_t nodes[] = {0, 1}, reversed[] = {1, 0}, duplicate[] = {0, 0}, invalid[] = {0, 9};
        selection.JointNodes = {nodes, 2};
        RIG_CHECK(import(noSkin, selection));
        Cook::RigClipBankCookResult cooked;
        RIG_CHECK(Cook::CookRigClipBankV1NativePath(F::View(noSkin), "RigV1Fixture/clip.gltf", "nvskel.v1.clips",
                                                    cooked, report, {}, nullptr, nullptr, profile, &selection));
        RIG_CHECK(cooked.Bytes == skinBytes);
        selection.JointNodes = {reversed, 2};
        RIG_CHECK(import(noSkin, selection));
        RIG_CHECK(source.GetData()->LocalRest[0].Translation.Y == 7);
        RIG_CHECK(Cook::CookRigClipBankV1NativePath(F::View(noSkin), "RigV1Fixture/clip.gltf", "nvskel.v1.clips",
                                                    cooked, report, {}, nullptr, nullptr, profile, &selection));
        RIG_CHECK(cooked.Bytes == skinBytes);
        const auto sentinel = source.GetData();
        selection.JointNodes = {duplicate, 2};
        RIG_CHECK(!import(noSkin, selection) && source.GetData() == sentinel);
        selection.JointNodes = {invalid, 2};
        RIG_CHECK(!import(noSkin, selection) && source.GetData() == sentinel);
        selection.JointNodes = {};
        RIG_CHECK(!import(noSkin, selection) && source.GetData() == sentinel);
        selection.JointNodes = {nodes, 2};
        RIG_CHECK(!import(text, selection) && source.GetData() == sentinel);
        NorvesLib::Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        RIG_CHECK(import(noSkin, selection, &settings));
        RIG_CHECK(source.GetData()->LocalRest[1].Translation.Y == 14 &&
                  source.GetData()->Geometry.Clips[0].Channels[0].Samples[1].Value.Y == 6);
        settings.Settings.Fit = NorvesLib::Core::AssetImport::FitAxis::Up;
        settings.Settings.FitMeters = 4;
        RIG_CHECK(!import(noSkin, selection, &settings));
        // 旧入口はmesh無しを受けず、既存契約は変わらない。
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(text), "RigV1Fixture/clip.gltf", source, report));
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
    RotationQuantizationRandom();
    RotationQuantizationBoundaries();
    TranslationQuantization();
    Topology();
    F::Fixture fixture;
    CompressionRoundtrip(fixture);
    CompressionLegacyBytes(fixture);
    CompressionReject(fixture);
    Codec(fixture);
    MetadataCodec(fixture);
    AuthorRest(fixture);
    MeshIndependentClips(fixture);
    QuaternionAndScaleDomain(fixture);
    ImportBudgets(fixture);
    BakeBudget(fixture);
    std::printf(
        "CLIPBANK_V1_WIRE result=pass actual_author_import_required_rest_deterministic_wire_strict_parse_canonical_topology_no_legacy_upgrade\n");
    return 0;
}
