#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "FileStream/FileStream.h"
#include "FileStream/Package.h"
#include "Rendering/VertexLayout.h"
#include "Resource/GLTFAnalyzer.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/SkeletalCubicBake.h"
#include "Animation/SkeletalSamplingMath.h"
#include "Resource/GltfBufferSet.h"
#include "Resource/ImportSettingsFile.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Resource/SkinnedMeshResource.h"
#include <algorithm>
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/ModelCookCache.h"
#include <cstdio>
#include <chrono>
#include <charconv>

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <utility>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace FileStream = NorvesLib::FileStream;
namespace Rendering = NorvesLib::Core::Rendering;
namespace Gltf = NorvesLib::Core::Gltf;
namespace ResourceIO = NorvesLib::Core::ResourceIO;
namespace Skeletal = NorvesLib::Core::Skeletal;
namespace Math = NorvesLib::Math;
namespace Animation = NorvesLib::Core::Animation;
using NorvesLib::Core::SkeletonResource;
using NorvesLib::Core::AnimationClipResource;
using NorvesLib::Core::SkinnedMeshResource;

namespace
{
    using ByteArray = Container::VariableArray<uint8_t>;

    namespace GoldenWire
    {
        constexpr size_t HeaderSize = 256;
        constexpr size_t VertexOffset = 256;
        constexpr size_t VertexSize = 192;
        constexpr size_t IndexOffset = 448;
        constexpr size_t IndexSize = 12;
        constexpr size_t JointOffset = 464;
        constexpr size_t JointSize = 160;
        constexpr size_t ClipOffset = 624;
        constexpr size_t ClipSize = 32;
        constexpr size_t ChannelOffset = 656;
        constexpr size_t ChannelSize = 64;
        constexpr size_t SampleOffset = 720;
        constexpr size_t SampleSize = 128;
        constexpr size_t StringOffset = 848;
        constexpr size_t StringSize = 13;
        constexpr size_t FileSize = 861;
        constexpr uint8_t Magic[8] = {'N', 'V', 'S', 'K', 'E', 'L', 'v', '0'};
    } // namespace GoldenWire

    Container::String ToCorePath(const std::filesystem::path& path)
    {
#if defined(UNICODE)
        return Container::String(path.c_str());
#else
        return Container::String(path.generic_string().c_str());
#endif
    }

    std::filesystem::path FindFixtureRoot()
    {
        const std::filesystem::path sourceFile(__FILE__);
        if (sourceFile.is_absolute())
        {
            const std::filesystem::path candidate = sourceFile.parent_path()
                                                        .parent_path()
                                                        .parent_path()
                                                        .parent_path() /
                                                    "Assets" / "Models" / "M9Skinned";
            if (std::filesystem::exists(candidate / "ValidU8Float.gltf"))
            {
                return candidate;
            }
        }

        std::filesystem::path cursor = std::filesystem::current_path();
        for (size_t depth = 0; depth < 8; ++depth)
        {
            const std::filesystem::path candidate = cursor / "Assets" / "Models" / "M9Skinned";
            if (std::filesystem::exists(candidate / "ValidU8Float.gltf"))
            {
                return candidate;
            }
            cursor = cursor.parent_path();
        }
        assert(false && "M9Skinned fixture root must be discoverable");
        return {};
    }

    void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    void WriteFloat(ByteArray& bytes, size_t offset, float value)
    {
        WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
    }

    void WriteVertex(ByteArray& bytes,
                     size_t offset,
                     float x,
                     float y,
                     float u,
                     float v,
                     uint32_t joint0,
                     uint32_t joint1,
                     float weight0,
                     float weight1)
    {
        WriteFloat(bytes, offset + 0, x);
        WriteFloat(bytes, offset + 4, y);
        WriteFloat(bytes, offset + 8, 0.0f);
        WriteFloat(bytes, offset + 12, 0.0f);
        WriteFloat(bytes, offset + 16, 0.0f);
        WriteFloat(bytes, offset + 20, 1.0f);
        WriteFloat(bytes, offset + 24, u);
        WriteFloat(bytes, offset + 28, v);
        WriteLe32(bytes, offset + 32, joint0);
        WriteLe32(bytes, offset + 36, joint1);
        WriteLe32(bytes, offset + 40, 0);
        WriteLe32(bytes, offset + 44, 0);
        WriteFloat(bytes, offset + 48, weight0);
        WriteFloat(bytes, offset + 52, weight1);
        WriteFloat(bytes, offset + 56, 0.0f);
        WriteFloat(bytes, offset + 60, 0.0f);
    }

    void WriteMatrix(ByteArray& bytes, size_t offset, float inverseY)
    {
        WriteFloat(bytes, offset + 0 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 5 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 10 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 13 * sizeof(float), inverseY);
        WriteFloat(bytes, offset + 15 * sizeof(float), 1.0f);
    }

    void WriteSample(ByteArray& bytes, size_t offset, float time, float x, float y, float z, float w)
    {
        WriteFloat(bytes, offset + 0, time);
        WriteFloat(bytes, offset + 4, x);
        WriteFloat(bytes, offset + 8, y);
        WriteFloat(bytes, offset + 12, z);
        WriteFloat(bytes, offset + 16, w);
    }

    ByteArray BuildGoldenSkeletal()
    {
        ByteArray bytes(GoldenWire::FileSize, 0);
        std::memcpy(bytes.data(), GoldenWire::Magic, sizeof(GoldenWire::Magic));
        WriteLe32(bytes, 8, static_cast<uint32_t>(GoldenWire::HeaderSize));
        WriteLe16(bytes, 12, 0);
        WriteLe16(bytes, 14, 1);
        WriteLe32(bytes, 16, 0x01020304u);
        WriteLe32(bytes, 20, 64);
        WriteLe32(bytes, 24, 80);
        WriteLe32(bytes, 28, 32);
        WriteLe32(bytes, 32, 32);
        WriteLe32(bytes, 36, 32);
        WriteLe64(bytes, 40, GoldenWire::FileSize);
        WriteLe64(bytes, 48, GoldenWire::VertexOffset);
        WriteLe64(bytes, 56, GoldenWire::VertexSize);
        WriteLe64(bytes, 64, GoldenWire::IndexOffset);
        WriteLe64(bytes, 72, GoldenWire::IndexSize);
        WriteLe64(bytes, 80, GoldenWire::JointOffset);
        WriteLe64(bytes, 88, GoldenWire::JointSize);
        WriteLe64(bytes, 96, GoldenWire::ClipOffset);
        WriteLe64(bytes, 104, GoldenWire::ClipSize);
        WriteLe64(bytes, 112, GoldenWire::ChannelOffset);
        WriteLe64(bytes, 120, GoldenWire::ChannelSize);
        WriteLe64(bytes, 128, GoldenWire::SampleOffset);
        WriteLe64(bytes, 136, GoldenWire::SampleSize);
        WriteLe64(bytes, 144, GoldenWire::StringOffset);
        WriteLe64(bytes, 152, GoldenWire::StringSize);
        WriteLe32(bytes, 168, 3);
        WriteLe32(bytes, 172, 3);
        WriteLe32(bytes, 176, 2);
        WriteLe32(bytes, 180, 1);
        WriteLe32(bytes, 184, 2);
        WriteLe32(bytes, 188, 4);
        WriteMatrix(bytes, 192, 0.0f);
        WriteFloat(bytes, 192 + 12 * sizeof(float), 5.0f);

        WriteVertex(bytes, 256, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1, 0.75f, 0.25f);
        WriteVertex(bytes, 320, 1.0f, 0.0f, 1.0f, 0.0f, 0, 1, 0.5f, 0.5f);
        WriteVertex(bytes, 384, 0.0f, 1.0f, 0.0f, 1.0f, 1, 0, 1.0f, 0.0f);
        WriteLe32(bytes, 448, 0);
        WriteLe32(bytes, 452, 2);
        WriteLe32(bytes, 456, 1);

        WriteLe32(bytes, 464, 0xffffffffu);
        WriteLe32(bytes, 468, 0);
        WriteLe32(bytes, 472, 4);
        WriteMatrix(bytes, 480, 0.0f);
        WriteLe32(bytes, 544, 0);
        WriteLe32(bytes, 548, 4);
        WriteLe32(bytes, 552, 5);
        WriteMatrix(bytes, 560, -1.0f);

        WriteLe64(bytes, 624, 9);
        WriteLe32(bytes, 632, 4);
        WriteFloat(bytes, 636, 2.0f);
        WriteLe32(bytes, 640, 0);
        WriteLe32(bytes, 644, 2);
        WriteLe32(bytes, 656, 1);
        WriteLe32(bytes, 660, 0);
        WriteLe32(bytes, 664, 0);
        WriteLe32(bytes, 668, 0);
        WriteLe32(bytes, 672, 2);
        WriteLe32(bytes, 688, 0);
        WriteLe32(bytes, 692, 1);
        WriteLe32(bytes, 696, 1);
        WriteLe32(bytes, 700, 2);
        WriteLe32(bytes, 704, 2);
        WriteSample(bytes, 720, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        WriteSample(bytes, 752, 2.0f, 0.0f, 3.0f, 0.0f, 0.0f);
        WriteSample(bytes, 784, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        WriteSample(bytes, 816, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f);

        constexpr char names[] = "RootChildWave";
        std::memcpy(bytes.data() + 848, names, sizeof(names) - 1);
        WriteLe64(bytes,
                  160,
                  Asset::ComputeCookedSkeletalV01Hash(bytes.data() + 192, bytes.data() + 256, bytes.size() - 256));
        return bytes;
    }

    void RecomputeSkeletalHash(ByteArray& bytes)
    {
        const uint64_t hash = bytes[14] == 0
            ? Asset::ComputeCookedSkeletalPayloadHash(bytes.data() + 256, bytes.size() - 256)
            : bytes[14] == 2
                ? Asset::ComputeCookedSkeletalV02Hash(bytes.data() + 192, bytes.data() + 256, bytes.data() + 320, bytes.size() - 320)
                : Asset::ComputeCookedSkeletalV01Hash(bytes.data() + 192, bytes.data() + 256, bytes.size() - 256);
        WriteLe64(bytes, 160, hash);
    }

    Asset::AssetBlob MakeBlob(const ByteArray& bytes)
    {
        return Asset::AssetBlob::CopyBytes(Container::Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvskel");
    }

    // writerと独立した0.2配置。2clipは同joint/pathを別々に所有する。
    ByteArray BuildGoldenSkeletalV02()
    {
        const auto old = BuildGoldenSkeletal();
        ByteArray bytes(1436, 0);
        std::memcpy(bytes.data(), old.data(), 256);
        WriteLe32(bytes, 8, 320);
        WriteLe16(bytes, 14, 2);
        WriteLe64(bytes, 40, 1436);
        const uint64_t offsets[] = {320, 512, 544, 704, 768, 896, 1408};
        const uint64_t sizes[] = {192, 24, 160, 64, 128, 256, 28};
        for (size_t index = 0; index < 7; ++index)
        {
            WriteLe64(bytes, 48 + index * 16, offsets[index]);
            WriteLe64(bytes, 56 + index * 16, sizes[index]);
        }
        WriteLe32(bytes, 172, 6);
        WriteLe32(bytes, 180, 2);
        WriteLe32(bytes, 184, 4);
        WriteLe32(bytes, 188, 8);
        WriteLe32(bytes, 256, 64);
        WriteLe32(bytes, 260, 64);
        WriteLe64(bytes, 264, 1152);
        WriteLe64(bytes, 272, 128);
        WriteLe64(bytes, 280, 1280);
        WriteLe64(bytes, 288, 128);
        WriteLe32(bytes, 296, 2);
        WriteLe32(bytes, 300, 2);
        std::memcpy(bytes.data() + 320, old.data() + 256, 192);
        std::memcpy(bytes.data() + 512, old.data() + 448, 12);
        std::memcpy(bytes.data() + 524, old.data() + 448, 12);
        std::memcpy(bytes.data() + 544, old.data() + 464, 160);
        std::memcpy(bytes.data() + 704, old.data() + 624, 32);
        std::memcpy(bytes.data() + 736, old.data() + 624, 32);
        WriteLe64(bytes, 736, 13);
        WriteLe32(bytes, 744, 7);
        WriteFloat(bytes, 748, 1.0f);
        WriteLe32(bytes, 752, 2);
        std::memcpy(bytes.data() + 768, old.data() + 656, 64);
        std::memcpy(bytes.data() + 832, old.data() + 656, 64);
        WriteLe32(bytes, 844, 4);
        WriteLe32(bytes, 876, 6);
        std::memcpy(bytes.data() + 896, old.data() + 720, 128);
        std::memcpy(bytes.data() + 1024, old.data() + 720, 128);
        WriteFloat(bytes, 1056, 1.0f);
        WriteFloat(bytes, 1120, 1.0f);
        for (size_t index = 0; index < 2; ++index)
        {
            const size_t record = 1152 + index * 64;
            WriteLe32(bytes, record, static_cast<uint32_t>(index * 3));
            WriteLe32(bytes, record + 4, 3);
            WriteLe32(bytes, record + 12, 3);
            WriteLe32(bytes, record + 16, static_cast<uint32_t>(index));
            WriteLe32(bytes, record + 20, static_cast<uint32_t>(index));
            WriteFloat(bytes, record + 24, 0.5f);
            WriteFloat(bytes, record + 36, 2.0f);
            WriteLe64(bytes, 1280 + index * 64, 20 + index * 4);
            WriteLe32(bytes, 1288 + index * 64, 4);
        }
        const uint8_t names[] = {'R','o','o','t','C','h','i','l','d','W','a','v','e',
            0xe9,0xaa,0xa8,0xf0,0x9f,0x90,0xba,'B','o','d','y','E','y','e','s'};
        std::memcpy(bytes.data() + 1408, names, sizeof(names));
        RecomputeSkeletalHash(bytes);
        return bytes;
    }

    void RunV02ReaderContract()
    {
        const auto golden = BuildGoldenSkeletalV02();
        const auto parsed = Asset::ParseCookedSkeletal(MakeBlob(golden));
        assert(parsed.Succeeded() && parsed.Data.VersionMinor == 2);
        const auto& data = parsed.Data.Skeletal;
        assert(data.SubMeshes.size() == 2 && data.MaterialSlots.size() == 2 && data.Clips.size() == 2);
        assert(data.SubMeshes[1].IndexStart == 3 && data.SubMeshes[1].IndexCount == 3 &&
            data.SubMeshes[1].VertexCount == 3 && data.SubMeshes[1].bNoShadow &&
            data.SubMeshes[1].BoundsCenter[0] == 0.5f && data.SubMeshes[1].BoundsRadius == 2.0f);
        assert(data.MaterialSlots[0].Name == "Body" && data.MaterialSlots[1].Name == "Eyes");
        assert(data.Clips[0].DurationSeconds == 2.0f && data.Clips[1].DurationSeconds == 1.0f &&
            data.Clips[0].Channels.size() == 2 && data.Clips[1].Channels.size() == 2);
        assert(data.Clips[0].Channels[0].Samples.back().TimeSeconds == 2.0f &&
            data.Clips[1].Channels[0].Samples.back().TimeSeconds == 1.0f);
        const auto utf = Asset::MeasureSkeletalNameEncoding<Container::String::value_type>(2,
            {data.Clips[1].Name.data(), data.Clips[1].Name.size()});
        assert(utf.Succeeded() && utf.ByteCount == 7);
        uint8_t encodedName[7]{};
        const uint8_t expectedName[] = {0xe9, 0xaa, 0xa8, 0xf0, 0x9f, 0x90, 0xba};
        assert(Asset::EncodeSkeletalWireName<Container::String::value_type>(2,
            {data.Clips[1].Name.data(), data.Clips[1].Name.size()}, encodedName).Succeeded());
        assert(std::memcmp(encodedName, expectedName, sizeof(expectedName)) == 0);
        const auto reject = [](ByteArray bytes, size_t offset, uint32_t value)
        {
            WriteLe32(bytes, offset, value);
            RecomputeSkeletalHash(bytes);
            const auto result = Asset::ParseCookedSkeletal(MakeBlob(bytes));
            assert(!result.Succeeded() && result.Data.Skeletal.Vertices.empty() &&
                result.Data.Skeletal.Clips.empty() && result.Data.Skeletal.SubMeshes.empty() && !result.Data.SourceBlob.IsValid());
        };
        // range/予約/flags/bounds/所有/padding/UTF-8の不正をhash再計算後にも拒否する。
        reject(golden, 1216, 2);
        reject(golden, 1168, 2);
        reject(golden, 1160, 1);
        reject(golden, 1164, 2);
        reject(golden, 1172, 2);
        reject(golden, 1176, 0x7fc00000);
        reject(golden, 1188, 0xbf800000);
        reject(golden, 1192, 1);
        reject(golden, 1292, 1);
        reject(golden, 1296, 1);
        reject(golden, 1280, UINT32_MAX);
        reject(golden, 1288, UINT32_MAX);
        reject(golden, 752, 1); // clip所有が重複。
        reject(golden, 756, 1); // 最後のchannelが未所有。
        reject(golden, 716, 0x3f800000); // clip0のdurationだけ違う。
        reject(golden, 748, 0x40000000); // clip1は他clipの最大時刻を使えない。
        reject(golden, 844, 2); // sample所有が重複。
        auto duplicate = golden;
        WriteLe32(duplicate, 800, 1);
        reject(duplicate, 804, 0);
        reject(golden, 556, 1); // joint予約。
        reject(golden, 728, 1); // clip予約。
        reject(golden, 788, 1); // channel予約。
        reject(golden, 916, 1); // sample予約。
        reject(golden, 536, 1); // index後padding。
        reject(golden, 1421, 0); // 名前へNUL。
        reject(golden, 1421, 0x808080c0); // overlong。
        reject(golden, 304, 1); // extra vertex未対応。
        auto badHash = golden;
        badHash[160] ^= 1;
        assert(Asset::ParseCookedSkeletal(MakeBlob(badHash)).Status == Asset::CookedSkeletalParseStatus::PayloadHashMismatch);
        auto truncated = golden;
        truncated.resize(319);
        assert(Asset::ParseCookedSkeletal(MakeBlob(truncated)).Status == Asset::CookedSkeletalParseStatus::HeaderTooSmall);
        auto emptyNames = golden;
        WriteLe32(emptyNames, 744, 0);
        WriteLe32(emptyNames, 1288, 0);
        RecomputeSkeletalHash(emptyNames);
        assert(Asset::ParseCookedSkeletal(MakeBlob(emptyNames)).Succeeded());
        std::cout << "V02 reader contract passed\n";
    }

    ByteArray BuildLooseFixtureBuffer()
    {
        ByteArray bytes(416, 0);
        constexpr float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        constexpr float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr float texCoords[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr uint8_t joints[12] = {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
        constexpr float weights[12] = {
            0.75f, 0.25f, 0.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f};
        for (size_t index = 0; index < 9; ++index)
        {
            WriteFloat(bytes, index * sizeof(float), positions[index]);
            WriteFloat(bytes, 36 + index * sizeof(float), normals[index]);
        }
        for (size_t index = 0; index < 6; ++index)
        {
            WriteFloat(bytes, 72 + index * sizeof(float), texCoords[index]);
        }
        for (size_t index = 0; index < 12; ++index)
        {
            bytes[96 + index] = joints[index];
            WriteFloat(bytes, 132 + index * sizeof(float), weights[index]);
        }
        WriteLe16(bytes, 216, 0);
        WriteLe16(bytes, 218, 1);
        WriteLe16(bytes, 220, 2);
        WriteMatrix(bytes, 224, 0.0f);
        WriteMatrix(bytes, 288, -1.0f);
        WriteFloat(bytes, 352, 0.0f);
        WriteFloat(bytes, 356, 2.0f);
        constexpr float translations[6] = {0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f};
        constexpr float rotations[8] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (size_t index = 0; index < 6; ++index)
        {
            WriteFloat(bytes, 360 + index * sizeof(float), translations[index]);
        }
        for (size_t index = 0; index < 8; ++index)
        {
            WriteFloat(bytes, 384 + index * sizeof(float), rotations[index]);
        }
        return bytes;
    }

    class LooseFixture final
    {
    public:
        LooseFixture()
        {
            char number[64] = {};
            const auto converted = std::to_chars(number, number + sizeof(number),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(converted.ec == std::errc{});
            const Container::AnsiString name = Container::AnsiString("NorvesLibM9CookedSkeletal-") +
                Container::AnsiString(Container::AnsiStringView(number, static_cast<size_t>(converted.ptr - number)));
            Root = std::filesystem::temp_directory_path() / std::filesystem::path(name.begin(), name.end());
            assert(std::filesystem::create_directory(Root));
            const std::filesystem::path source = FindFixtureRoot() / "ValidU8Float.gltf";
            assert(std::filesystem::copy_file(source, Root / "ValidU8Float.gltf"));
            const ByteArray bytes = BuildLooseFixtureBuffer();
            auto stream = FileStream::FileStream::Create(
                ToCorePath(Root / "fixture.bin"),
                FileStream::FileMode::Write,
                FileStream::FileAccess::Write,
                FileStream::FileShare::None);
            assert(stream && stream->IsOpen());
            assert(stream->Write(bytes.data(), bytes.size()) == bytes.size());
            stream->Close();
        }

        ~LooseFixture()
        {
            std::filesystem::remove_all(Root);
        }

        Container::String Path() const
        {
            return ToCorePath(Root / "ValidU8Float.gltf");
        }

        std::filesystem::path Root;
    };

    ByteArray TextBytes(Container::AnsiStringView text)
    {
        ByteArray result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }
    Container::String CoreText(Container::AnsiStringView text)
    {
        Container::String result;
        for (const char value : text)
        {
            result.push_back(static_cast<Container::String::value_type>(static_cast<uint8_t>(value)));
        }
        return result;
    }
    Container::AnsiString ReadFixtureJson(const Container::String& path)
    {
        auto stream = FileStream::FileStream::Create(path, FileStream::FileMode::Read,
            FileStream::FileAccess::Read, FileStream::FileShare::Read);
        assert(stream && stream->IsOpen() && stream->GetSize() > 0);
        ByteArray bytes(static_cast<size_t>(stream->GetSize()));
        assert(stream->Read(bytes.data(), bytes.size()) == bytes.size());
        stream->Close();
        return Container::AnsiString(Container::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    Container::AnsiString ChangeBufferUri(Container::AnsiStringView text, Container::AnsiStringView replacement)
    {
        constexpr Container::AnsiStringView needle = "\"uri\": \"fixture.bin\",";
        const size_t offset = text.find(needle);
        assert(offset != Container::AnsiStringView::npos);
        return Container::AnsiString(text.substr(0, offset)) + Container::AnsiString(replacement) +
            Container::AnsiString(text.substr(offset + needle.size()));
    }
    Container::AnsiString EncodeFixtureBase64(const ByteArray& bytes)
    {
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        Container::AnsiString result;
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            const bool bSecond = i + 1 < bytes.size();
            const bool bThird = i + 2 < bytes.size();
            const uint32_t value = (uint32_t{bytes[i]} << 16) |
                (bSecond ? uint32_t{bytes[i + 1]} << 8 : 0) | (bThird ? bytes[i + 2] : 0);
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
            result.push_back(bSecond ? alphabet[(value >> 6) & 63] : '=');
            result.push_back(bThird ? alphabet[value & 63] : '=');
        }
        return result;
    }
    ByteArray MakeSkeletalGlb(Container::AnsiStringView json, const ByteArray& binary)
    {
        const size_t jsonSize = (json.size() + 3) & ~size_t{3};
        const size_t binarySize = (binary.size() + 3) & ~size_t{3};
        ByteArray result(28 + jsonSize + binarySize, 0);
        WriteLe32(result, 0, 0x46546c67);
        WriteLe32(result, 4, 2);
        WriteLe32(result, 8, static_cast<uint32_t>(result.size()));
        WriteLe32(result, 12, static_cast<uint32_t>(jsonSize));
        WriteLe32(result, 16, 0x4e4f534a);
        std::memcpy(result.data() + 20, json.data(), json.size());
        for (size_t i = json.size(); i < jsonSize; ++i)
        {
            result[20 + i] = ' ';
        }
        WriteLe32(result, 20 + jsonSize, static_cast<uint32_t>(binarySize));
        WriteLe32(result, 24 + jsonSize, 0x004e4942);
        std::memcpy(result.data() + 28 + jsonSize, binary.data(), binary.size());
        return result;
    }
    uint64_t HashSourcePart(uint64_t hash, Container::Span<const uint8_t> bytes)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            hash = (hash ^ ((static_cast<uint64_t>(bytes.size()) >> (8 * i)) & 255)) * 1099511628211ull;
        }
        for (const auto value : bytes)
        {
            hash = (hash ^ value) * 1099511628211ull;
        }
        return hash;
    }
    void WriteFixtureBytes(const std::filesystem::path& path, const ByteArray& bytes)
    {
        auto stream = FileStream::FileStream::Create(ToCorePath(path), FileStream::FileMode::Write,
            FileStream::FileAccess::Write, FileStream::FileShare::None);
        assert(stream && stream->IsOpen());
        assert(stream->Write(bytes.data(), bytes.size()) == bytes.size());
        stream->Close();
    }

    void RunSkeletalCacheVersionContract(const std::filesystem::path& root, const ByteArray& current)
    {
        using namespace NorvesLib::Tools::AssetCook;
        const auto packagePath = root / "version-cache.nvpkg";
        const auto manifestPath = root / "version-cache.json";
        ModelCookFingerprint fingerprint;
        fingerprint.SourceHash = 1;
        const auto check = [&](const ByteArray& payload, bool expected)
        {
            // 1entryの独立package配置。name10Bの後を8Bへ整列する。
            ByteArray package(176 + payload.size(), 0);
            const uint8_t magic[] = {'N','V','P','K','G','v','1',0};
            std::memcpy(package.data(),magic,8);
            WriteLe32(package,8,96); WriteLe16(package,12,1);
            WriteLe32(package,16,0x01020304); WriteLe32(package,20,64);
            WriteLe64(package,24,package.size()); WriteLe32(package,32,1);
            WriteLe64(package,40,96); WriteLe64(package,48,64);
            WriteLe64(package,56,160); WriteLe64(package,64,10);
            WriteLe64(package,72,176); WriteLe32(package,80,8);
            WriteLe64(package,96,160); WriteLe32(package,104,10);
            WriteLe32(package,108,Asset::MakeAssetPackageFourCC('S','k','l','0'));
            WriteLe64(package,120,176); WriteLe64(package,128,payload.size());
            WriteLe64(package,136,payload.size());
            const auto hash = Asset::ComputeAssetPackagePayloadHash(payload.data(),payload.size());
            WriteLe64(package,144,hash);
            std::memcpy(package.data()+160,"rig.nvskel",10);
            std::memcpy(package.data()+176,payload.data(),payload.size());
            FileStream::Package loaded;
            assert(loaded.LoadFromMemory({package.data(),package.size()}));
            assert(Asset::ParseCookedSkeletal(MakeBlob(payload)).Succeeded());
            char hashText[17]{};
            std::snprintf(hashText,sizeof(hashText),"%016llx",static_cast<unsigned long long>(hash));
            Container::AnsiString manifest = R"json({"version":1,"assets":[{"logical_path":"Models/rig.gltf","kind":"model","source_hash":"0000000000000001","variant":"default","format":"nvskel.v0.skinned.pnujiw.u32","cooked_package":"version-cache.nvpkg","entry_name":"rig.nvskel","entry_type":"Skl0","cooked_hash":")json";
            manifest += hashText;
            manifest += R"json(","cooked_version":0}]})json";
            WriteFixtureBytes(packagePath,package);
            WriteFixtureBytes(manifestPath,TextBytes(manifest));
            assert(IsModelCookCacheCurrent(manifestPath,packagePath,"Models/rig.gltf","default",
                "nvskel.v0.skinned.pnujiw.u32","rig.nvskel",fingerprint) == expected);
        };
        auto legacy = BuildGoldenSkeletal();
        check(legacy,false);
        WriteLe16(legacy,14,0);
        std::memset(legacy.data()+192,0,64);
        RecomputeSkeletalHash(legacy);
        check(legacy,false);
        check(current,true);
    }

    void AssertLiteralCookedData(const Asset::CookedSkeletalData& cooked, uint64_t expectedPayloadHash = 0)
    {
        assert(cooked.SourceBlob.IsValid());
        if (expectedPayloadHash != 0)
        {
            assert(cooked.PayloadHash == expectedPayloadHash);
        }
        assert(cooked.Skeletal.Vertices.size() == 3);
        assert(cooked.Skeletal.Indices.size() == 3);
        assert(cooked.Skeletal.Joints.size() == 2);
        assert(cooked.Skeletal.Clips.size() == 1);
        assert(cooked.Skeletal.Vertices[1].Position.X == 1.0f);
        assert(cooked.Skeletal.Vertices[2].TexCoord.V == 1.0f);
        assert(cooked.Skeletal.Vertices[0].JointIndices[1] == 1);
        assert(cooked.Skeletal.Vertices[0].JointWeights[0] == 0.75f);
        assert(cooked.Skeletal.Indices[0] == 0);
        assert(cooked.Skeletal.Indices[1] == 2);
        assert(cooked.Skeletal.Indices[2] == 1);
        assert(cooked.Skeletal.MeshNodeGlobalTransform[12] == 5.0f);
        assert(cooked.Skeletal.Joints[0].Name == "Root");
        assert(cooked.Skeletal.Joints[0].ParentIndex == -1);
        assert(cooked.Skeletal.Joints[1].Name == "Child");
        assert(cooked.Skeletal.Joints[1].ParentIndex == 0);
        assert(cooked.Skeletal.Joints[1].InverseBindMatrix[13] == -1.0f);
        assert(cooked.Skeletal.Clips[0].Name == "Wave");
        assert(cooked.Skeletal.Clips[0].DurationSeconds == 2.0f);
        assert(cooked.Skeletal.Clips[0].Channels.size() == 2);
        assert(cooked.Skeletal.Clips[0].Channels[0].Interpolation ==
               Skeletal::SkeletalAnimationInterpolation::Linear);
        assert(cooked.Skeletal.Clips[0].Channels[0].Samples[1].Value.Y == 3.0f);
        assert(cooked.Skeletal.Clips[0].Channels[1].Interpolation ==
               Skeletal::SkeletalAnimationInterpolation::Step);
        assert(cooked.Skeletal.Clips[0].Channels[1].Samples[1].Value.Z == 1.0f);
    }

    void AssertEquivalent(const Skeletal::SkeletalGltfData& loose, const Skeletal::SkeletalGltfData& cooked)
    {
        for (size_t element = 0; element < 16; ++element)
        {
            assert(loose.MeshNodeGlobalTransform[element] == cooked.MeshNodeGlobalTransform[element]);
        }
        assert(loose.Vertices.size() == cooked.Vertices.size());
        assert(loose.Indices == cooked.Indices);
        assert(loose.Joints.size() == cooked.Joints.size());
        assert(loose.Clips.size() == cooked.Clips.size());
        for (size_t vertexIndex = 0; vertexIndex < loose.Vertices.size(); ++vertexIndex)
        {
            const Skeletal::SkeletalVertex& a = loose.Vertices[vertexIndex];
            const Skeletal::SkeletalVertex& b = cooked.Vertices[vertexIndex];
            assert(a.Position.X == b.Position.X);
            assert(a.Position.Y == b.Position.Y);
            assert(a.Position.Z == b.Position.Z);
            assert(a.Normal.X == b.Normal.X);
            assert(a.Normal.Y == b.Normal.Y);
            assert(a.Normal.Z == b.Normal.Z);
            assert(a.TexCoord.U == b.TexCoord.U);
            assert(a.TexCoord.V == b.TexCoord.V);
            for (size_t influence = 0; influence < 4; ++influence)
            {
                assert(a.JointIndices[influence] == b.JointIndices[influence]);
                assert(a.JointWeights[influence] == b.JointWeights[influence]);
            }
        }
        for (size_t jointIndex = 0; jointIndex < loose.Joints.size(); ++jointIndex)
        {
            assert(loose.Joints[jointIndex].Name == cooked.Joints[jointIndex].Name);
            assert(loose.Joints[jointIndex].ParentIndex == cooked.Joints[jointIndex].ParentIndex);
            for (size_t element = 0; element < 16; ++element)
            {
                assert(loose.Joints[jointIndex].InverseBindMatrix[element] ==
                       cooked.Joints[jointIndex].InverseBindMatrix[element]);
            }
        }
        for (size_t clipIndex=0;clipIndex<loose.Clips.size();++clipIndex)
        {
            assert(loose.Clips[clipIndex].Name == cooked.Clips[clipIndex].Name);
            assert(loose.Clips[clipIndex].DurationSeconds == cooked.Clips[clipIndex].DurationSeconds);
            assert(loose.Clips[clipIndex].Channels.size() == cooked.Clips[clipIndex].Channels.size());
            for (size_t channelIndex = 0; channelIndex < loose.Clips[clipIndex].Channels.size(); ++channelIndex)
            {
                const Skeletal::SkeletalAnimationChannel& a = loose.Clips[clipIndex].Channels[channelIndex];
                const Skeletal::SkeletalAnimationChannel& b = cooked.Clips[clipIndex].Channels[channelIndex];
                assert(a.JointIndex == b.JointIndex);
                assert(a.Path == b.Path);
                assert(a.Interpolation == b.Interpolation);
                assert(a.Samples.size() == b.Samples.size());
                for (size_t sampleIndex = 0; sampleIndex < a.Samples.size(); ++sampleIndex)
                {
                    assert(a.Samples[sampleIndex].TimeSeconds == b.Samples[sampleIndex].TimeSeconds);
                    assert(a.Samples[sampleIndex].Value.X == b.Samples[sampleIndex].Value.X);
                    assert(a.Samples[sampleIndex].Value.Y == b.Samples[sampleIndex].Value.Y);
                    assert(a.Samples[sampleIndex].Value.Z == b.Samples[sampleIndex].Value.Z);
                    assert(a.Samples[sampleIndex].Value.W == b.Samples[sampleIndex].Value.W);
                }
            }
        }
    }

    void ExpectStatus(ByteArray bytes, Asset::CookedSkeletalParseStatus expectedStatus)
    {
        const Asset::CookedSkeletalParseResult result = Asset::ParseCookedSkeletal(MakeBlob(bytes));
        assert(!result.Succeeded());
        assert(result.Status == expectedStatus);
        assert(!result.Data.SourceBlob.IsValid());
    }

    void AssertSkinnedVertexAbi()
    {
        const Rendering::VertexLayout layout = Rendering::VertexLayout::CreateSkinned();
        assert(layout.ElementCount == 5);
        assert(layout.Stride == 64);
        assert(layout.Elements[3].Semantic == Rendering::VertexSemantic::BoneIndices);
        assert(layout.Elements[3].Format == Rendering::VertexFormat::UInt4);
        assert(layout.Elements[3].Offset == 32);
        assert(layout.Elements[3].GetSize() == 16);
        assert(layout.Elements[4].Semantic == Rendering::VertexSemantic::BoneWeights);
        assert(layout.Elements[4].Format == Rendering::VertexFormat::Float4);
        assert(layout.Elements[4].Offset == 48);
        assert(layout.Elements[4].GetSize() == 16);
    }

    void AssertScaledSkeletal(const Skeletal::SkeletalGltfData& before,
                              const Skeletal::SkeletalGltfData& after, float scale)
    {
        const auto isNear = [](float a, float b)
        {
            return std::abs(a - b) < 1e-4f;
        };
        assert(before.Vertices.size() == after.Vertices.size() && before.Joints.size() == after.Joints.size());
        assert(before.Indices == after.Indices && before.Clips.size() == after.Clips.size());
        for (size_t index = 0; index < before.Vertices.size(); ++index)
        {
            const auto& a = before.Vertices[index];
            const auto& b = after.Vertices[index];
            assert(isNear(b.Position.X, a.Position.X * scale) && isNear(b.Position.Y, a.Position.Y * scale) &&
                isNear(b.Position.Z, a.Position.Z * scale));
            assert(b.Normal.X == a.Normal.X && b.Normal.Y == a.Normal.Y && b.Normal.Z == a.Normal.Z);
            assert(b.TexCoord.U == a.TexCoord.U && b.TexCoord.V == a.TexCoord.V);
            assert(b.JointIndices == a.JointIndices && b.JointWeights == a.JointWeights);
        }
        for (size_t index = 0; index < before.Joints.size(); ++index)
        {
            const auto& a = before.Joints[index];
            const auto& b = after.Joints[index];
            assert(a.Name == b.Name && a.ParentIndex == b.ParentIndex);
            for (size_t element = 0; element < 16; ++element)
            {
                if (element >= 12 && element < 15)
                {
                    assert(isNear(b.InverseBindMatrix[element], a.InverseBindMatrix[element] * scale));
                }
                else
                {
                    assert(b.InverseBindMatrix[element] == a.InverseBindMatrix[element]);
                }
            }
        }
        for (size_t element = 0; element < 16; ++element)
        {
            if (element >= 12 && element < 15)
            {
                assert(isNear(after.MeshNodeGlobalTransform[element], before.MeshNodeGlobalTransform[element] * scale));
            }
            else
            {
                assert(after.MeshNodeGlobalTransform[element] == before.MeshNodeGlobalTransform[element]);
            }
        }
        for (size_t clip = 0; clip < before.Clips.size(); ++clip)
        {
            assert(before.Clips[clip].Name == after.Clips[clip].Name &&
                before.Clips[clip].DurationSeconds == after.Clips[clip].DurationSeconds);
            assert(before.Clips[clip].Channels.size() == after.Clips[clip].Channels.size());
            for (size_t channel = 0; channel < before.Clips[clip].Channels.size(); ++channel)
            {
                const auto& a = before.Clips[clip].Channels[channel];
                const auto& b = after.Clips[clip].Channels[channel];
                assert(a.Path == b.Path && a.JointIndex == b.JointIndex && a.Interpolation == b.Interpolation);
                assert(a.Samples.size() == b.Samples.size());
                const float factor = a.Path == Skeletal::SkeletalAnimationPath::Translation ? scale : 1.0f;
                for (size_t sample = 0; sample < a.Samples.size(); ++sample)
                {
                    assert(a.Samples[sample].TimeSeconds == b.Samples[sample].TimeSeconds);
                    const auto& x = a.Samples[sample].Value;
                    const auto& y = b.Samples[sample].Value;
                    assert(isNear(y.X,x.X*factor) && isNear(y.Y,x.Y*factor) && isNear(y.Z,x.Z*factor) && y.W == x.W);
                    if (a.Path != Skeletal::SkeletalAnimationPath::Translation)
                    {
                        assert(y.X==x.X && y.Y==x.Y && y.Z==x.Z);
                    }

                }
            }
        }
    }

    Math::Matrix4x4 ImportMatrix(const Container::FixedArray<float,16>& values)
    {
        return Math::Matrix4x4(values[0],values[1],values[2],values[3],
            values[4],values[5],values[6],values[7],values[8],values[9],values[10],values[11],
            values[12],values[13],values[14],values[15]);
    }

    void AssertScaledSample(const Skeletal::SkeletalGltfData& before,
                            const Skeletal::SkeletalGltfData& after, float scale)
    {
        SkeletonResource firstSkeleton, secondSkeleton;
        AnimationClipResource firstClip, secondClip;
        SkinnedMeshResource firstMesh, secondMesh;
        firstSkeleton.Initialize(); secondSkeleton.Initialize();
        firstClip.Initialize(); secondClip.Initialize(); firstMesh.Initialize(); secondMesh.Initialize();
        auto first = before;
        auto second = after;
        firstSkeleton.SetJoints(std::move(first.Joints)); secondSkeleton.SetJoints(std::move(second.Joints));
        firstClip.SetClip(std::move(first.Clips[0])); secondClip.SetClip(std::move(second.Clips[0]));
        firstMesh.SetVertices(std::move(first.Vertices)); secondMesh.SetVertices(std::move(second.Vertices));
        firstMesh.SetIndices(std::move(first.Indices)); secondMesh.SetIndices(std::move(second.Indices));
        firstMesh.SetMeshNodeGlobalTransform(before.MeshNodeGlobalTransform);
        secondMesh.SetMeshNodeGlobalTransform(after.MeshNodeGlobalTransform);
        assert(firstSkeleton.Load() && secondSkeleton.Load() && firstClip.Load() && secondClip.Load());
        for (float time : {0.0f,1.0f,2.0f})
        {
            Animation::SkeletalPoseSnapshot firstPose, secondPose;
            assert(Animation::SkeletalAnimationSampler::Sample(firstSkeleton,firstClip,firstMesh,time,
                ImportMatrix(before.MeshNodeGlobalTransform),firstPose));
            assert(Animation::SkeletalAnimationSampler::Sample(secondSkeleton,secondClip,secondMesh,time,
                ImportMatrix(after.MeshNodeGlobalTransform),secondPose));
            for (size_t vertex=0;vertex<before.Vertices.size();++vertex)
            {
                const auto a=Animation::SkeletalAnimationSampler::SkinVertex(firstMesh.GetVertices()[vertex],firstPose.BonePalette);
                const auto b=Animation::SkeletalAnimationSampler::SkinVertex(secondMesh.GetVertices()[vertex],secondPose.BonePalette);
                assert(std::abs(b.Position.x-a.Position.x*scale)<1e-4f);
                assert(std::abs(b.Position.y-a.Position.y*scale)<1e-4f);
                assert(std::abs(b.Position.z-a.Position.z*scale)<1e-4f);
                assert(std::abs(b.Normal.x-a.Normal.x)<1e-4f && std::abs(b.Normal.y-a.Normal.y)<1e-4f &&
                    std::abs(b.Normal.z-a.Normal.z)<1e-4f);
            }
        }
        firstMesh.Finalize(); secondMesh.Finalize(); firstClip.Finalize(); secondClip.Finalize();
        firstSkeleton.Finalize(); secondSkeleton.Finalize();
    }

    // 5影響は先頭の1頂点のみ。他の頂点は元データと同じ影響を維持する。
    void RunInfluenceReductionContract()
    {
        using namespace Skeletal;
        LooseFixture fixture;
        const auto source = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "FiveInfluences.gltf"));
        const auto sourceBytes = TextBytes(source);
        const auto sourcePath = ToCorePath(fixture.Root / "FiveInfluences.gltf");
        WriteFixtureBytes(fixture.Root / "FiveInfluences.gltf", sourceBytes);
        const auto base = BuildLooseFixtureBuffer();
        ByteArray binary(668, 0);
        std::memcpy(binary.data(), base.data(), 224);
        for (size_t joint = 0; joint < 5; ++joint) WriteMatrix(binary, 224 + joint * 64, -float(joint));
        std::memcpy(binary.data() + 544, base.data() + 352, 64);
        constexpr float firstWeights[4] = {0.4f, 0.3f, 0.15f, 0.1f};
        for (size_t slot = 0; slot < 4; ++slot)
        {
            binary[96 + slot] = static_cast<uint8_t>(slot);
            WriteFloat(binary, 132 + slot * 4, firstWeights[slot]);
        }
        binary[608] = 4;
        WriteFloat(binary, 620, 0.05f);
        WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        SkeletalGltfDecodeOptions options;
        options.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
        Gltf::BufferSet buffers;
        const auto decoded = DecodeSkeletalGltf({sourceBytes.data(), sourceBytes.size()}, sourcePath, &buffers, nullptr, &options);
        assert(decoded.Succeeded() && decoded.Data.Vertices.size() == 3 && decoded.Data.Joints.size() == 5);
        assert(buffers.GetCount() == 1);
        const auto checkReport = [](const SkeletalGltfDecodeReport& report)
        {
            assert(report.TotalVertexCount == 3 && report.ProcessedVertexCount == 3);
            assert(report.ReducedVertexCount == 1 && report.WarningVertexCount == 1 && report.RenormalizedVertexCount == 1);
            assert(report.MergedJointVertexCount == 0 && report.bInfluenceScanComplete);
            assert(std::abs(report.MaximumDroppedWeight - 0.05) < 1e-7);
            assert(std::abs(report.MeanDroppedWeight - 0.05 / 3) < 1e-7);
            assert(report.FailedVertexIndex == UINT64_MAX && !report.bHasFailedVertexDroppedWeight);
        };
        checkReport(decoded.Report);
        for (size_t slot = 0; slot < 4; ++slot)
        {
            assert(decoded.Data.Vertices[0].JointIndices[slot] == slot);
            assert(std::abs(decoded.Data.Vertices[0].JointWeights[slot] - firstWeights[slot] / 0.95f) < 1e-6f);
        }
        SkeletalGltfSourceBuffers legacyBuffers;
        const auto legacy = DecodeSkeletalGltf(CoreText(source), sourcePath, &legacyBuffers, nullptr, &options);
        assert(legacy.Succeeded() && legacyBuffers.size() == 1);
        AssertEquivalent(decoded.Data, legacy.Data); checkReport(legacy.Report);
        const auto file = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(sourcePath, &options);
        assert(file.Succeeded()); AssertEquivalent(decoded.Data, file.Data); checkReport(file.Report);
        const auto glb = MakeSkeletalGlb(ChangeBufferUri(source, ""), binary);
        const auto embedded = DecodeSkeletalGltf({glb.data(), glb.size()}, sourcePath, nullptr, nullptr, &options);
        assert(embedded.Succeeded()); AssertEquivalent(decoded.Data, embedded.Data); checkReport(embedded.Report);
        assert(DecodeSkeletalGltf({sourceBytes.data(), sourceBytes.size()}, sourcePath).Status == SkeletalGltfDecodeStatus::InfluenceLimitExceeded);
        const auto checkEmpty = [](const SkeletalGltfDecodeResult& result)
        {
            assert(result.Data.Vertices.empty() && result.Data.Indices.empty() && result.Data.Joints.empty() && result.Data.Clips.empty());
        };
        // cook/preflightは同じsource/設定/policyを同じ順でhash化する。
        using namespace NorvesLib::Tools::AssetCook;
        const Container::AnsiString cookPath((fixture.Root / "FiveInfluences.gltf").generic_string().c_str());
        constexpr Container::AnsiStringView format = "nvskel.v0.skinned.pnujiw.u32";
        Container::AnsiString cookError;
        SkeletalCookDiagnostics diagnostics;
        auto cookWith = [&](const ByteArray& bytes, const SkeletalGltfDecodeOptions* policy, SkeletalCookResult& out)
        {
            return CookGltfToNvskel(bytes.data(), bytes.size(), format, cookPath, out, cookError, nullptr, policy, &diagnostics);
        };
        auto fingerprintWith = [&](const ByteArray& bytes, const SkeletalGltfDecodeOptions* policy, ModelCookFingerprint& out)
        {
            return FingerprintModelCookSource(bytes.data(), bytes.size(), format, cookPath, "Models/rig.gltf", out, cookError, nullptr, policy);
        };
        SkeletalCookResult cooked;
        ModelCookFingerprint fingerprint;
        assert(cookWith(sourceBytes, &options, cooked) && fingerprintWith(sourceBytes, &options, fingerprint));
        assert(cooked.SourceHash == fingerprint.SourceHash && !cooked.bHasImportSettings);
        checkReport(cooked.DecodeReport);
        assert(diagnostics.bDecodeAttempted && diagnostics.DecodeStatus == 0);
        checkReport(diagnostics.Report);
        auto parsedCook = Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsedCook.Succeeded()); AssertEquivalent(decoded.Data, parsedCook.Data.Skeletal);
        SkeletalCookResult cookedGlb;
        ModelCookFingerprint glbFingerprint;
        assert(cookWith(glb, &options, cookedGlb) && fingerprintWith(glb, &options, glbFingerprint));
        assert(cookedGlb.SourceHash == glbFingerprint.SourceHash && cookedGlb.NvskelBytes == cooked.NvskelBytes);
        auto warningOnly = options; warningOnly.WarnDroppedWeight = 0.06;
        SkeletalCookResult changedWarning;
        ModelCookFingerprint changedFingerprint;
        assert(cookWith(sourceBytes, &warningOnly, changedWarning) && fingerprintWith(sourceBytes, &warningOnly, changedFingerprint));
        assert(changedWarning.SourceHash == changedFingerprint.SourceHash && changedWarning.SourceHash != cooked.SourceHash);
        assert(changedWarning.DecodeReport.WarningVertexCount == 0 && changedWarning.NvskelBytes == cooked.NvskelBytes);
        auto changedLimit = options; changedLimit.FailDroppedWeight = 0.2;
        assert(fingerprintWith(sourceBytes, &changedLimit, changedFingerprint) && changedFingerprint.SourceHash != cooked.SourceHash);
        const auto savedCook = cooked;
        const auto savedFingerprint = fingerprint;
        assert(!cookWith(sourceBytes, nullptr, cooked) && !fingerprintWith(sourceBytes, nullptr, fingerprint));
        assert(cooked.SourceHash == savedCook.SourceHash && cooked.NvskelBytes == savedCook.NvskelBytes);
        assert(fingerprint.SourceHash == savedFingerprint.SourceHash);
        auto cookLimit = options; cookLimit.FailDroppedWeight = 0.04;
        // fingerprint成功はcook可能性の証明ではない。値を読み縮約する本cookで拒否する。
        assert(fingerprintWith(sourceBytes, &cookLimit, changedFingerprint));
        assert(!cookWith(sourceBytes, &cookLimit, cooked));
        assert(diagnostics.bDecodeAttempted && diagnostics.DecodeStatus == 17 && diagnostics.Report.FailedVertexIndex == 0);
        assert(diagnostics.Report.bHasFailedVertexDroppedWeight && std::abs(diagnostics.Report.FailedVertexDroppedWeight - 0.05) < 1e-7);
        assert(cookError.find("failed_vertex=0") != Container::AnsiString::npos &&
            cookError.find("dropped_weight=") != Container::AnsiString::npos);
        assert(cooked.SourceHash == savedCook.SourceHash && cooked.NvskelBytes == savedCook.NvskelBytes);
        checkReport(cooked.DecodeReport);
        auto badPolicy = options; badPolicy.FailDroppedWeight = -1;
        assert(!cookWith(sourceBytes, &badPolicy, cooked) && !fingerprintWith(sourceBytes, &badPolicy, fingerprint));
        assert(!diagnostics.bDecodeAttempted && diagnostics.Report.ProcessedVertexCount == 0);
        assert(cooked.SourceHash == savedCook.SourceHash && fingerprint.SourceHash == savedFingerprint.SourceHash);
        assert(!FingerprintModelCookSource(sourceBytes.data(), sourceBytes.size(), "nvmesh.v0.mesh3d.pnt.u32.clustered",
            cookPath, "Models/rig.gltf", fingerprint, cookError, nullptr, &options));
        // sidecarが有る場合もcook前照合と同じ順でpolicyを追加する。
        WriteFixtureBytes(fixture.Root / "FiveInfluences.gltf.import.json", TextBytes("{\"version\":1,\"units\":{\"scale\":2}}"));
        SkeletalCookResult scaledCook;
        assert(cookWith(sourceBytes, &options, scaledCook) && fingerprintWith(sourceBytes, &options, changedFingerprint));
        assert(scaledCook.SourceHash == changedFingerprint.SourceHash && scaledCook.SourceHash != cooked.SourceHash);
        assert(scaledCook.bHasImportSettings && scaledCook.ImportSettingsHash == changedFingerprint.ImportSettingsHash);
        const auto scaledParsed = Asset::ParseCookedSkeletal(MakeBlob(scaledCook.NvskelBytes));
        assert(scaledParsed.Succeeded()); AssertScaledSkeletal(decoded.Data, scaledParsed.Data.Skeletal, 2);
        checkReport(scaledCook.DecodeReport);
        assert(std::filesystem::remove(fixture.Root / "FiveInfluences.gltf.import.json"));
        auto limited = options; limited.FailDroppedWeight = 0.04;
        const auto failed = DecodeSkeletalGltf({sourceBytes.data(), sourceBytes.size()}, sourcePath, &buffers, nullptr, &limited);
        assert(failed.Status == SkeletalGltfDecodeStatus::InfluenceReductionExceeded && buffers.GetCount() == 0);
        checkEmpty(failed);
        assert(failed.Report.ProcessedVertexCount == 0 && failed.Report.FailedVertexIndex == 0 && !failed.Report.bInfluenceScanComplete);
        assert(failed.Report.MaximumDroppedWeight == 0 && failed.Report.MeanDroppedWeight == 0);
        assert(failed.Report.bHasFailedVertexDroppedWeight && std::abs(failed.Report.FailedVertexDroppedWeight - 0.05) < 1e-7);
        const auto failedLegacy = DecodeSkeletalGltf(CoreText(source), sourcePath, &legacyBuffers, nullptr, &limited);
        assert(failedLegacy.Status == failed.Status && legacyBuffers.empty()); checkEmpty(failedLegacy);
        const auto failedGlb = DecodeSkeletalGltf({glb.data(), glb.size()}, sourcePath, nullptr, nullptr, &limited);
        assert(failedGlb.Status == failed.Status); checkEmpty(failedGlb);
        assert(ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(sourcePath, &limited).Status == failed.Status);
        auto invalidOptions = options; invalidOptions.WarnDroppedWeight = 0.5;
        assert(DecodeSkeletalGltf({sourceBytes.data(), sourceBytes.size()}, sourcePath, nullptr, nullptr, &invalidOptions).Status == SkeletalGltfDecodeStatus::InvalidImportOptions);
        // 成功prefixの統計から失敗頂点を除外する。0番は通常成功、1番だけ脱落量超過。
        auto laterFailure = binary;
        std::memcpy(laterFailure.data() + 100, binary.data() + 96, 4);
        std::memcpy(laterFailure.data() + 148, binary.data() + 132, 16);
        std::memcpy(laterFailure.data() + 612, binary.data() + 608, 4);
        std::memcpy(laterFailure.data() + 636, binary.data() + 620, 16);
        std::memcpy(laterFailure.data() + 96, base.data() + 96, 4);
        std::memcpy(laterFailure.data() + 132, base.data() + 132, 16);
        WriteFloat(laterFailure, 620, 0);
        WriteFixtureBytes(fixture.Root / "fixture.bin", laterFailure);
        const auto prefixFailure = DecodeSkeletalGltf(CoreText(source), sourcePath, nullptr, nullptr, &limited);
        assert(prefixFailure.Status == SkeletalGltfDecodeStatus::InfluenceReductionExceeded);
        assert(prefixFailure.Report.ProcessedVertexCount == 1 && prefixFailure.Report.FailedVertexIndex == 1);
        assert(prefixFailure.Report.ReducedVertexCount == 0 && prefixFailure.Report.WarningVertexCount == 0);
        assert(prefixFailure.Report.MaximumDroppedWeight == 0 && prefixFailure.Report.MeanDroppedWeight == 0);
        assert(prefixFailure.Report.bHasFailedVertexDroppedWeight && std::abs(prefixFailure.Report.FailedVertexDroppedWeight - 0.05) < 1e-7);
        checkEmpty(prefixFailure);
        auto mixed = binary;
        // U8 set0の総和241、U16 set1は3598。241*257+3598=65535。
        mixed[180] = 102; mixed[181] = 76; mixed[182] = 38; mixed[183] = 25;
        mixed[184] = 255; mixed[188] = 255; WriteLe16(mixed, 192, 3598);
        auto mixedSource = source;
        const auto replaceOnce = [](Container::AnsiString& text, Container::AnsiStringView from, Container::AnsiStringView to)
        {
            const size_t position = text.find(from);
            assert(position != Container::AnsiString::npos && text.find(from, position + from.size()) == Container::AnsiString::npos);
            text = Container::AnsiString(text.substr(0, position)) + Container::AnsiString(to) + Container::AnsiString(text.substr(position + from.size()));
        };
        replaceOnce(mixedSource, "\"WEIGHTS_0\": 5", "\"WEIGHTS_0\": 6");
        replaceOnce(mixedSource, "\"WEIGHTS_1\": 14", "\"WEIGHTS_1\": 7");
        WriteFixtureBytes(fixture.Root / "fixture.bin", mixed);
        const auto mixedDecoded = DecodeSkeletalGltf(CoreText(mixedSource), sourcePath, nullptr, nullptr, &options);
        assert(mixedDecoded.Succeeded() && std::abs(mixedDecoded.Report.MaximumDroppedWeight - 3598.0 / 65535) < 1e-12);
        WriteLe16(mixed, 192, 3597); WriteFixtureBytes(fixture.Root / "fixture.bin", mixed);
        const auto mixedRejected = DecodeSkeletalGltf(CoreText(mixedSource), sourcePath, nullptr, nullptr, &options);
        assert(mixedRejected.Status == SkeletalGltfDecodeStatus::InvalidAccessor && mixedRejected.Report.ProcessedVertexCount == 0);
        checkEmpty(mixedRejected);
        // 捨てられる5本目もjoint範囲、負値、ゼロweight時のjointを検査する。
        binary[608] = 5; WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        const auto badJoint = DecodeSkeletalGltf(CoreText(source), sourcePath, nullptr, nullptr, &options);
        assert(badJoint.Status == SkeletalGltfDecodeStatus::InvalidSkeleton); checkEmpty(badJoint);
        WriteFloat(binary, 620, 0); WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        assert(DecodeSkeletalGltf(CoreText(source), sourcePath, nullptr, nullptr, &options).Status == SkeletalGltfDecodeStatus::InvalidSkeleton);
        binary[608] = 4; WriteFloat(binary, 620, -0.05f); WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        assert(DecodeSkeletalGltf(CoreText(source), sourcePath, nullptr, nullptr, &options).Status == SkeletalGltfDecodeStatus::InvalidAccessor);
        // UNORM16の1不足はfloat許容内でも拒否する。Strict/Reduceとも同じ元入力契約。
        auto quantized = base;
        for (size_t vertex = 0; vertex < 3; ++vertex) WriteLe16(quantized, 192 + vertex * 8, UINT16_MAX);
        const auto quantizedSource = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "ValidU8Unorm16.gltf"));
        WriteFixtureBytes(fixture.Root / "fixture.bin", quantized);
        assert(DecodeSkeletalGltf(CoreText(quantizedSource), sourcePath, nullptr, nullptr, &options).Succeeded());
        WriteLe16(quantized, 192, UINT16_MAX - 1); WriteFixtureBytes(fixture.Root / "fixture.bin", quantized);
        const auto badSum = DecodeSkeletalGltf(CoreText(quantizedSource), sourcePath, nullptr, nullptr, &options);
        assert(badSum.Status == SkeletalGltfDecodeStatus::InvalidAccessor); checkEmpty(badSum);
        assert(badSum.Report.ProcessedVertexCount == 0 && badSum.Report.FailedVertexIndex == 0);
    }

    ByteArray BuildCubicFixtureBuffer()
    {
        ByteArray bytes=BuildLooseFixtureBuffer(); bytes.resize(656,0);
        constexpr float translation[18]={0,0,0, 0,1,0, 0,6,0, 0,6,0, 0,3,0, 0,0,0};
        constexpr float rotation[24]={0,0,0,0, 0,0,0,1, 0,0,0.5f,-0.5f, 0,0,0.5f,-0.5f, 0,0,1,0, 0,0,0,0};
        constexpr float scale[18]={0,0,0, 1,1,1, 0,0,0, 0,0,0, 2,2,2, 0,0,0};
        for(size_t index=0;index<18;++index)
        {
            WriteFloat(bytes,416+index*4,translation[index]);
            WriteFloat(bytes,584+index*4,scale[index]);
        }
        for(size_t index=0;index<24;++index)
        {
            WriteFloat(bytes,488+index*4,rotation[index]);
        }
        return bytes;
    }

    Skeletal::SkeletalValue SampleBakedChannel(const Skeletal::SkeletalAnimationChannel& channel,float time)
    {
        const auto& samples=channel.Samples;
        assert(!samples.empty());
        if(time<=samples.front().TimeSeconds)
        {
            return samples.front().Value;
        }
        if(time>=samples.back().TimeSeconds)
        {
            return samples.back().Value;
        }
        for(size_t index=1;index<samples.size();++index)
        {
            if(time>samples[index].TimeSeconds)
            {
                continue;
            }
            const auto& a=samples[index-1]; const auto& z=samples[index];
            const float alpha=Animation::Detail::ComputeLinearAlpha(a.TimeSeconds,z.TimeSeconds,time);
            if(channel.Path==Skeletal::SkeletalAnimationPath::Rotation)
            {
                const auto value=Animation::Detail::Slerp({a.Value.X,a.Value.Y,a.Value.Z,a.Value.W},
                    {z.Value.X,z.Value.Y,z.Value.Z,z.Value.W},alpha);
                return {value.x,value.y,value.z,value.w};
            }
            return {a.Value.X+(z.Value.X-a.Value.X)*alpha,a.Value.Y+(z.Value.Y-a.Value.Y)*alpha,
                a.Value.Z+(z.Value.Z-a.Value.Z)*alpha,a.Value.W+(z.Value.W-a.Value.W)*alpha};
        }
        assert(false); return {};
    }

    void AssertCubicFixture(const Skeletal::SkeletalGltfData& data,const Skeletal::SkeletalGltfDecodeReport& report,
        const Skeletal::SkeletalGltfDecodeOptions& options,double factor)
    {
        using namespace Skeletal;
        assert(data.Clips.size()==1 && data.Clips[0].Channels.size()==3 && data.Clips[0].DurationSeconds==2);
        assert(report.bCubicScanStarted && report.bCubicScanComplete && report.TotalAnimationChannelCount==3 &&
            report.ProcessedAnimationChannelCount==3 && report.BakedCubicChannelCount==3 && report.CubicInputKeyCount==6);
        assert(report.FailedAnimationChannelIndex==UINT64_MAX && !report.bHasCubicBakeFailure);
        assert(report.BakedCubicTranslationChannelCount==1 && report.BakedCubicRotationChannelCount==1 && report.BakedCubicScaleChannelCount==1);
        assert(report.MaximumCubicTranslationErrorMeters<=options.CubicTranslationToleranceMeters &&
            report.MaximumCubicRotationErrorRadians<=options.CubicRotationToleranceRadians &&
            report.MaximumCubicScaleError<=options.CubicScaleTolerance);
        assert(std::abs(data.Vertices[1].Position.X-factor)<1e-6 &&
            std::abs(data.Joints[1].InverseBindMatrix[13]+factor)<1e-6 &&
            std::abs(data.MeshNodeGlobalTransform[12]-5*factor)<1e-6);
        size_t keyCount=0;
        for(const auto& channel:data.Clips[0].Channels)
        {
            assert(channel.Interpolation==SkeletalAnimationInterpolation::Linear && channel.Samples.size()>2);
            assert(channel.Samples.front().TimeSeconds==0 && channel.Samples.back().TimeSeconds==2);
            keyCount+=channel.Samples.size();
            for(size_t index=1;index<channel.Samples.size();++index)
            {
                assert(channel.Samples[index].TimeSeconds>channel.Samples[index-1].TimeSeconds);
            }
            for(size_t step=0;step<=128;++step)
            {
                const float time=static_cast<float>(step)/64;
                const long double u=static_cast<long double>(time)/2;
                const auto value=SampleBakedChannel(channel,time);
                if(channel.Path==SkeletalAnimationPath::Translation)
                {
                    const long double expected=((2*u*u*u-3*u*u+1)+3*(-2*u*u*u+3*u*u)+12*(2*u*u*u-3*u*u+u))*factor;
                    const long double squared=value.X*value.X+(expected-value.Y)*(expected-value.Y)+value.Z*value.Z;
                    assert(std::sqrt(squared)<=options.CubicTranslationToleranceMeters);
                }
                else if(channel.Path==SkeletalAnimationPath::Rotation)
                {
                    const long double idealNorm=std::sqrt(u*u+(1-u)*(1-u));
                    const long double actualNorm=std::sqrt(static_cast<long double>(value.X)*value.X+static_cast<long double>(value.Y)*value.Y+
                        static_cast<long double>(value.Z)*value.Z+static_cast<long double>(value.W)*value.W);
                    const long double dot=(u*value.Z+(1-u)*value.W)/(idealNorm*actualNorm);
                    assert(2*std::acos(std::clamp(std::abs(dot),0.0L,1.0L))<=options.CubicRotationToleranceRadians);
                }
                else
                {
                    assert(channel.Path==SkeletalAnimationPath::Scale);
                    const long double expected=1+3*u*u-2*u*u*u;
                    assert(std::sqrt((expected-value.X)*(expected-value.X)+(expected-value.Y)*(expected-value.Y)+
                        (expected-value.Z)*(expected-value.Z))<=options.CubicScaleTolerance);
                }
            }
        }
        assert(keyCount==report.CubicOutputKeyCount && keyCount<=options.CubicMaximumSamplesPerAsset);
    }

    void RunCubicBakeContract()
    {
        using namespace Skeletal;
        using namespace NorvesLib::Tools::AssetCook;
        LooseFixture fixture;
        const auto text=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"CubicChannels.gltf"));
        const auto source=TextBytes(text);
        const auto path=ToCorePath(fixture.Root/"CubicChannels.gltf");
        auto binary=BuildCubicFixtureBuffer();
        WriteFixtureBytes(fixture.Root/"CubicChannels.gltf",source);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        const auto glb=MakeSkeletalGlb(ChangeBufferUri(text,""),binary);
        SkeletalGltfDecodeOptions options; options.CubicSplinePolicy=SkeletalCubicSplinePolicy::Bake;
        assert(DecodeSkeletalGltf(source,path).Status==SkeletalGltfDecodeStatus::UnsupportedInterpolation);
        Gltf::BufferSet sources;
        const auto decoded=DecodeSkeletalGltf(source,path,&sources,nullptr,&options);
        assert(decoded.Succeeded() && sources.GetCount()==1);
        AssertCubicFixture(decoded.Data,decoded.Report,options,1);
        SkeletalGltfSourceBuffers legacySources;
        const auto legacy=DecodeSkeletalGltf(CoreText(text),path,&legacySources,nullptr,&options);
        const auto embedded=DecodeSkeletalGltf(glb,path,nullptr,nullptr,&options);
        const auto file=ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(path,&options);
        assert(legacy.Succeeded() && embedded.Succeeded() && file.Succeeded() && legacySources.size()==1);
        AssertEquivalent(decoded.Data,legacy.Data); AssertEquivalent(decoded.Data,embedded.Data); AssertEquivalent(decoded.Data,file.Data);
        AssertCubicFixture(legacy.Data,legacy.Report,options,1); AssertCubicFixture(embedded.Data,embedded.Report,options,1);
        AssertCubicFixture(file.Data,file.Report,options,1);
        const Container::AnsiString cookPath((fixture.Root/"CubicChannels.gltf").generic_string().c_str());
        constexpr Container::AnsiStringView format="nvskel.v0.skinned.pnujiw.u32";
        Container::AnsiString error;
        SkeletalCookResult cooked;
        SkeletalCookDiagnostics diagnostics;
        ModelCookFingerprint fingerprint;
        const auto cook=[&](const ByteArray& bytes,const SkeletalGltfDecodeOptions& selected,SkeletalCookResult& out)
        {
            return CookGltfToNvskel(bytes.data(),bytes.size(),format,cookPath,out,error,nullptr,&selected,&diagnostics);
        };
        assert(cook(source,options,cooked));
        assert(FingerprintModelCookSource(source.data(),source.size(),format,cookPath,"Models/rig.gltf",fingerprint,error,nullptr,&options));
        assert(fingerprint.SourceHash==cooked.SourceHash && diagnostics.bDecodeAttempted && diagnostics.DecodeStatus==0);
        const auto parsed=Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsed.Succeeded()); AssertEquivalent(decoded.Data,parsed.Data.Skeletal);
        AssertCubicFixture(parsed.Data.Skeletal,cooked.DecodeReport,options,1);
        SkeletalCookResult embeddedCook; assert(cook(glb,options,embeddedCook) && embeddedCook.NvskelBytes==cooked.NvskelBytes);
        for(bool fit:{false,true})
        {
            WriteFixtureBytes(fixture.Root/"CubicChannels.gltf.import.json",TextBytes(fit ?
                "{\"version\":1,\"units\":{\"fit\":{\"axis\":\"up\",\"meters\":0.6}}}" : "{\"version\":1,\"units\":{\"scale\":2}}"));
            const double factor=fit ? 0.6 : 2;
            const auto scaled=DecodeSkeletalGltf(source,path,nullptr,nullptr,&options);
            assert(scaled.Succeeded()); AssertCubicFixture(scaled.Data,scaled.Report,options,factor);
            SkeletalCookResult scaledCook; assert(cook(source,options,scaledCook));
            const auto scaledParsed=Asset::ParseCookedSkeletal(MakeBlob(scaledCook.NvskelBytes));
            assert(scaledParsed.Succeeded()); AssertEquivalent(scaled.Data,scaledParsed.Data.Skeletal);
            AssertCubicFixture(scaledParsed.Data.Skeletal,scaledCook.DecodeReport,options,factor);
        }
        assert(std::filesystem::remove(fixture.Root/"CubicChannels.gltf.import.json"));
        const auto retained=cooked;
        auto limited=options; limited.CubicMaximumSamplesPerChannel=2;
        const auto failure=DecodeSkeletalGltf(source,path,&sources,nullptr,&limited);
        assert(failure.Status==SkeletalGltfDecodeStatus::CubicBakeFailed && sources.GetCount()==0 && failure.Data.Vertices.empty());
        assert(failure.Report.bCubicScanStarted && !failure.Report.bCubicScanComplete && failure.Report.ProcessedAnimationChannelCount==0 &&
            failure.Report.FailedAnimationChannelIndex==0 && failure.Report.bHasCubicBakeFailure &&
            failure.Report.FailedCubicBakeStatus==static_cast<uint32_t>(CubicBakeStatus::SampleLimitExceeded));
        assert(DecodeSkeletalGltf(CoreText(text),path,&legacySources,nullptr,&limited).Status==failure.Status && legacySources.empty());
        assert(ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(path,&limited).Status==failure.Status);
        assert(!cook(source,limited,cooked) && diagnostics.DecodeStatus==static_cast<uint32_t>(failure.Status));
        assert(cooked.NvskelBytes==retained.NvskelBytes && cooked.SourceHash==retained.SourceHash);
        limited=options; limited.CubicMaximumSamplesPerAsset=2;
        assert(DecodeSkeletalGltf(source,path,nullptr,nullptr,&limited).Status==SkeletalGltfDecodeStatus::CubicBakeFailed);
        limited=options;
        limited.CubicMaximumSamplesPerAsset=static_cast<uint32_t>(decoded.Data.Clips[0].Channels[0].Samples.size()+1);
        const auto exhausted=DecodeSkeletalGltf(source,path,nullptr,nullptr,&limited);
        assert(exhausted.Status==SkeletalGltfDecodeStatus::CubicBakeFailed && exhausted.Report.ProcessedAnimationChannelCount==1 &&
            exhausted.Report.FailedAnimationChannelIndex==1 && exhausted.Report.BakedCubicTranslationChannelCount==1 &&
            exhausted.Report.BakedCubicRotationChannelCount==0 && exhausted.Report.BakedCubicScaleChannelCount==0 &&
            exhausted.Report.CubicOutputKeyCount==decoded.Data.Clips[0].Channels[0].Samples.size());
        // 通常LINEAR/STEPをBake modeで読む場合も、scale/fitは既存経路と同値。
        for(bool fit:{false,true})
        {
            WriteFixtureBytes(fixture.Root/"ValidU8Float.gltf.import.json",TextBytes(fit ?
                "{\"version\":1,\"units\":{\"fit\":{\"axis\":\"up\",\"meters\":0.6}}}" : "{\"version\":1,\"units\":{\"scale\":2}}"));
            const auto oldLinear=ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path());
            const auto bakedLinear=ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path(),&options);
            assert(oldLinear.Succeeded() && bakedLinear.Succeeded());
            AssertEquivalent(oldLinear.Data,bakedLinear.Data);
            assert(bakedLinear.Report.bCubicScanComplete && bakedLinear.Report.BakedCubicChannelCount==0);
        }
        assert(std::filesystem::remove(fixture.Root/"ValidU8Float.gltf.import.json"));
        // Translation完了後、Rotationの内部zeroで失敗した場合も正常prefixだけを診断する。
        for(size_t index=0;index<24;++index)
        {
            WriteFloat(binary,488+index*4,0);
        }
        WriteFloat(binary,488+7*4,1); WriteFloat(binary,488+11*4,-2);
        WriteFloat(binary,488+15*4,2); WriteFloat(binary,488+19*4,1);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        const auto zero=DecodeSkeletalGltf(source,path,nullptr,nullptr,&options);
        assert(zero.Status==SkeletalGltfDecodeStatus::CubicBakeFailed && zero.Data.Clips.empty());
        assert(zero.Report.ProcessedAnimationChannelCount==1 && zero.Report.BakedCubicChannelCount==1 &&
            zero.Report.CubicInputKeyCount==2 && zero.Report.FailedAnimationChannelIndex==1);
        binary=BuildCubicFixtureBuffer(); WriteFloat(binary,444,std::numeric_limits<float>::quiet_NaN());
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        const auto nonfinite=DecodeSkeletalGltf(source,path,nullptr,nullptr,&options);
        assert(nonfinite.Status==SkeletalGltfDecodeStatus::CubicBakeFailed && nonfinite.Report.FailedCubicBakeStatus==static_cast<uint32_t>(CubicBakeStatus::InvalidInput));
        WriteFixtureBytes(fixture.Root/"fixture.bin",BuildCubicFixtureBuffer());
        auto malformed=text;
        const Container::AnsiStringView countNeedle="\"count\": 6";
        const auto countPosition=malformed.find(countNeedle); assert(countPosition!=Container::AnsiString::npos);
        malformed=Container::AnsiString(malformed.substr(0,countPosition))+"\"count\": 5"+Container::AnsiString(malformed.substr(countPosition+countNeedle.size()));
        const auto badCount=DecodeSkeletalGltf(CoreText(malformed),path,nullptr,nullptr,&options);
        assert(badCount.Status==SkeletalGltfDecodeStatus::InvalidAnimation && !badCount.Report.bHasCubicBakeFailure);
    }

    ByteArray BuildMorphFixtureBuffer()
    {
        ByteArray binary(596, 0);
        const auto base = BuildLooseFixtureBuffer();
        std::memcpy(binary.data(), base.data(), base.size());
        WriteFloat(binary, 416, .2f);
        for (size_t vertex = 0; vertex < 3; ++vertex)
        {
            WriteFloat(binary, 488 + vertex * 16, 1);
            WriteFloat(binary, 500 + vertex * 16, 1);
        }
        constexpr float weightKeys[6] = {0,.5f,1,0,.7f,0};
        for (size_t index = 0; index < 6; ++index)
        {
            WriteFloat(binary, 572 + index * 4, weightKeys[index]);
        }
        return binary;
    }

    void RunMorphDropContract()
    {
        using namespace Skeletal;
        using namespace NorvesLib::Tools::AssetCook;
        LooseFixture fixture;
        const auto source = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "MorphDrop.gltf"));
        const auto replaceOnce = [](Container::AnsiString& text, Container::AnsiStringView from, Container::AnsiStringView to)
        {
            const size_t offset = text.find(from);
            assert(offset != Container::AnsiString::npos && text.find(from, offset + from.size()) == Container::AnsiString::npos);
            text = Container::AnsiString(text.substr(0, offset)) + Container::AnsiString(to) + Container::AnsiString(text.substr(offset + from.size()));
        };
        const auto bytes = TextBytes(source);
        const auto path = ToCorePath(fixture.Root / "MorphDrop.gltf");
        auto binary = BuildMorphFixtureBuffer();
        WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        WriteFixtureBytes(fixture.Root / "MorphDrop.gltf", bytes);
        auto embeddedText = source;
        replaceOnce(embeddedText, "\"uri\":\"fixture.bin\",", "");
        const auto glb = MakeSkeletalGlb(embeddedText, binary);
        assert(DecodeSkeletalGltf(bytes, path).Status == SkeletalGltfDecodeStatus::UnsupportedMorphTargets);
        SkeletalGltfDecodeOptions drop;
        drop.MorphPolicy = SkeletalMorphPolicy::Drop;
        Gltf::BufferSet sources;
        const auto decoded = DecodeSkeletalGltf(bytes, path, &sources, nullptr, &drop);
        assert(decoded.Succeeded() && sources.GetCount() == 1);
        const auto checkReport = [](const SkeletalGltfDecodeReport& report)
        {
            assert(report.bMorphScanComplete && report.DroppedMorphTargetCount == 1 &&
                report.DroppedMorphMeshWeightCount == 1 && report.DroppedMorphNodeWeightCount == 1 &&
                report.DroppedMorphAnimationChannelCount == 1);
        };
        checkReport(decoded.Report);
        assert(!decoded.Report.bCubicScanStarted); // weight専用Cubicは焼き込まず除去する。
        const auto baselineText = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "ValidU8Float.gltf"));
        const auto baseline = DecodeSkeletalGltf(TextBytes(baselineText), path);
        assert(baseline.Succeeded());
        AssertEquivalent(baseline.Data, decoded.Data);
        const auto noMorph = DecodeSkeletalGltf(TextBytes(baselineText), path, nullptr, nullptr, &drop);
        assert(noMorph.Succeeded() && noMorph.Report.bMorphScanComplete && noMorph.Report.DroppedMorphTargetCount == 0);
        SkeletalGltfSourceBuffers legacySources;
        const auto legacy = DecodeSkeletalGltf(CoreText(source), path, &legacySources, nullptr, &drop);
        const auto embedded = DecodeSkeletalGltf(glb, path, nullptr, nullptr, &drop);
        const auto file = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(path, &drop);
        assert(legacy.Succeeded() && embedded.Succeeded() && file.Succeeded() && legacySources.size() == 1);
        AssertEquivalent(decoded.Data, legacy.Data); AssertEquivalent(decoded.Data, embedded.Data); AssertEquivalent(decoded.Data, file.Data);
        checkReport(legacy.Report); checkReport(embedded.Report); checkReport(file.Report);
        const Container::AnsiString cookPath((fixture.Root / "MorphDrop.gltf").generic_string().c_str());
        constexpr Container::AnsiStringView format = "nvskel.v0.skinned.pnujiw.u32";
        Container::AnsiString error;
        SkeletalCookDiagnostics diagnostics;
        const auto cook = [&](const ByteArray& selected, const SkeletalGltfDecodeOptions& options, SkeletalCookResult& out)
        {
            return CookGltfToNvskel(selected.data(), selected.size(), format, cookPath, out, error, nullptr, &options, &diagnostics);
        };
        SkeletalCookResult cooked;
        assert(cook(bytes, drop, cooked));
        checkReport(cooked.DecodeReport);
        const auto parsed = Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsed.Succeeded()); AssertEquivalent(decoded.Data, parsed.Data.Skeletal);
        ModelCookFingerprint fingerprint;
        assert(FingerprintModelCookSource(bytes.data(), bytes.size(), format, cookPath, "MorphDrop", fingerprint, error, nullptr, &drop));
        assert(fingerprint.SourceHash == cooked.SourceHash);
        fingerprint.SourceHash = 123;
        assert(!FingerprintModelCookSource(bytes.data(), bytes.size(), format, cookPath, "MorphDrop", fingerprint, error) && fingerprint.SourceHash == 123);
        SkeletalCookResult embeddedCook;
        assert(cook(glb, drop, embeddedCook) && embeddedCook.NvskelBytes == cooked.NvskelBytes);
        auto combined = drop;
        combined.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
        combined.CubicSplinePolicy = SkeletalCubicSplinePolicy::Bake;
        const auto allPolicies = DecodeSkeletalGltf(bytes, path, nullptr, nullptr, &combined);
        assert(allPolicies.Succeeded()); AssertEquivalent(decoded.Data, allPolicies.Data); checkReport(allPolicies.Report);
        assert(allPolicies.Report.bCubicScanComplete && allPolicies.Report.TotalAnimationChannelCount == 3 &&
            allPolicies.Report.ProcessedAnimationChannelCount == 3 && allPolicies.Report.BakedCubicChannelCount == 0);
        const auto reportJson = [&](bool success, const SkeletalGltfDecodeResult& result, const SkeletalGltfDecodeOptions& options)
        {
            SkeletalImportReportInput input;
            input.Outcome = success ? SkeletalImportOutcome::PayloadReady : SkeletalImportOutcome::Failed;
            input.Options = options;
            input.Diagnostics.bDecodeAttempted = true;
            input.Diagnostics.DecodeStatus = static_cast<uint32_t>(result.Status);
            input.Diagnostics.Report = result.Report;
            const auto json = BuildSkeletalImportReport(input);
            assert(json.bValid && std::strstr(json.Bytes, "\"morph_scan\":{"));
        };
        reportJson(true, decoded, drop);
        reportJson(true, allPolicies, combined);
        for (const char* interpolation : {"LINEAR", "STEP"})
        {
            auto text = source;
            replaceOnce(text, "\"output\":17,\"interpolation\":\"CUBICSPLINE\"",
                Container::AnsiString("\"output\":17,\"interpolation\":\"") + interpolation + "\"");
            replaceOnce(text, "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":6",
                "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":2");
            const auto result = DecodeSkeletalGltf(TextBytes(text), path, nullptr, nullptr, &drop);
            assert(result.Succeeded()); AssertEquivalent(decoded.Data, result.Data); checkReport(result.Report); reportJson(true, result, drop);
        }
        auto twoTargets = source;
        replaceOnce(twoTargets, "\"targets\":[{\"POSITION\":13,\"NORMAL\":14,\"TANGENT\":16}]",
            "\"targets\":[{\"POSITION\":13,\"NORMAL\":14,\"TANGENT\":16},{\"POSITION\":13}]");
        replaceOnce(twoTargets, "\"weights\":[0.25]", "\"weights\":[0.25,0.3]");
        replaceOnce(twoTargets, "\"weights\":[0.5]", "\"weights\":[0.5,0.6]");
        replaceOnce(twoTargets, "\"byteLength\":596", "\"byteLength\":620");
        replaceOnce(twoTargets, "\"byteOffset\":572,\"byteLength\":24", "\"byteOffset\":572,\"byteLength\":48");
        replaceOnce(twoTargets, "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":6",
            "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":12");
        auto twoBinary = binary; twoBinary.resize(620, 0);
        WriteFixtureBytes(fixture.Root / "fixture.bin", twoBinary);
        const auto two = DecodeSkeletalGltf(TextBytes(twoTargets), path, nullptr, nullptr, &drop);
        assert(two.Succeeded() && two.Report.DroppedMorphTargetCount == 2 && two.Report.DroppedMorphMeshWeightCount == 2 &&
            two.Report.DroppedMorphNodeWeightCount == 2 && two.Report.DroppedMorphAnimationChannelCount == 1);
        AssertEquivalent(decoded.Data, two.Data); reportJson(true, two, drop);
        WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        auto onlyWeights = source;
        replaceOnce(onlyWeights, "{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"rotation\"}},", "");
        const auto emptyClip = DecodeSkeletalGltf(TextBytes(onlyWeights), path, nullptr, nullptr, &drop);
        assert(emptyClip.Status == SkeletalGltfDecodeStatus::InvalidAnimation && emptyClip.Data.Vertices.empty());
        checkReport(emptyClip.Report); reportJson(false, emptyClip, drop);
        auto invalidTrs = source;
        replaceOnce(invalidTrs, "\"node\":1,\"path\":\"translation\"", "\"node\":2,\"path\":\"translation\"");
        const auto trsFailure = DecodeSkeletalGltf(TextBytes(invalidTrs), path, nullptr, nullptr, &drop);
        assert(trsFailure.Status == SkeletalGltfDecodeStatus::InvalidAnimation && trsFailure.Data.Vertices.empty());
        checkReport(trsFailure.Report); reportJson(false, trsFailure, drop);
        const auto retained = cooked;
        const auto reject = [&](const Container::AnsiString& text, const ByteArray& data)
        {
            WriteFixtureBytes(fixture.Root / "fixture.bin", data);
            const auto selected = TextBytes(text);
            const auto result = DecodeSkeletalGltf(selected, path, &sources, nullptr, &drop);
            assert(!result.Succeeded() && result.Data.Vertices.empty() && sources.GetCount() == 0 && !result.Report.bMorphScanComplete);
            assert(!cook(selected, drop, cooked) && cooked.NvskelBytes == retained.NvskelBytes && cooked.SourceHash == retained.SourceHash);
        };
        for (int invalid = 0; invalid < 12; ++invalid)
        {
            auto text = source;
            switch (invalid)
            {
            case 0: replaceOnce(text, "\"targets\":[{\"POSITION\":13,\"NORMAL\":14,\"TANGENT\":16}]", "\"targets\":null"); break;
            case 1: replaceOnce(text, "\"POSITION\":13", "\"POSITION\":999"); break;
            case 2: replaceOnce(text, "\"NORMAL\":14", "\"NORMAL\":14,\"NORMAL\":14"); break;
            case 3: replaceOnce(text, "\"weights\":[0.25]", "\"weights\":[0.25,0.5]"); break;
            case 4: replaceOnce(text, "\"weights\":[0.5]", "\"weights\":null"); break;
            case 5: replaceOnce(text, "\"node\":2,\"path\":\"weights\"", "\"node\":0,\"path\":\"weights\""); break;
            case 6: replaceOnce(text, "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":6", "\"name\":\"MorphWeights\",\"bufferView\":17,\"componentType\":5126,\"count\":5"); break;
            case 7: replaceOnce(text, "\"name\":\"MorphPosition\",\"bufferView\":13", "\"name\":\"MorphPosition\",\"bufferView\":13,\"sparse\":{}"); break;
            case 8: replaceOnce(text, "\"TANGENT\":15", "\"_TANGENT\":15"); break;
            case 9: replaceOnce(text, "\"TANGENT\":16", "\"COLOR_0\":16"); break;
            case 10: replaceOnce(text, "\"weights\":[0.25]", "\"weights\":[0.25],\"weights\":[0.3]"); break;
            case 11: replaceOnce(text, "\"max\":[0.2,0,0]", "\"max\":[-1,0,0]"); break;
            }
            reject(text, binary);
        }
        for (size_t offset : {size_t{416}, size_t{488}, size_t{576}})
        {
            auto invalid = binary;
            WriteFloat(invalid, offset, std::numeric_limits<float>::quiet_NaN());
            reject(source, invalid);
        }
        auto invalidTime = binary;
        WriteFloat(invalidTime, 356, 0);
        reject(source, invalidTime);
        WriteFixtureBytes(fixture.Root / "fixture.bin", binary);
        // mesh初期weightだけでもReject/cacheが黙認しない。
        auto weightsOnly = baselineText;
        replaceOnce(weightsOnly, "\"name\": \"Triangle\"", "\"name\": \"Triangle\", \"weights\": [0.5]");
        assert(DecodeSkeletalGltf(TextBytes(weightsOnly), path).Status == SkeletalGltfDecodeStatus::UnsupportedMorphTargets);
        const auto weightsBytes = TextBytes(weightsOnly);
        assert(!FingerprintModelCookSource(weightsBytes.data(), weightsBytes.size(), format, cookPath, "MorphDrop", fingerprint, error));
    }

    void RunMultiPrimitiveContract()
    {
        using namespace Skeletal;
        using namespace NorvesLib::Tools::AssetCook;
        LooseFixture fixture;
        const auto source = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "TwoMaterials.gltf"));
        const auto path = ToCorePath(fixture.Root / "TwoMaterials.gltf");
        const auto replaceOnce = [](Container::AnsiString& text, Container::AnsiStringView from, Container::AnsiStringView to)
        {
            const size_t offset = text.find(from);
            assert(offset != Container::AnsiString::npos && text.find(from, offset + from.size()) == Container::AnsiString::npos);
            text = Container::AnsiString(text.substr(0, offset)) + Container::AnsiString(to) + Container::AnsiString(text.substr(offset + from.size()));
        };
        ByteArray binary(508,0);
        const auto base = BuildLooseFixtureBuffer();
        std::memcpy(binary.data(),base.data(),base.size());
        constexpr float positions[] = {2,0,0, 3,0,0, 2,1,0};
        for (size_t index=0; index<9; ++index)
        {
            WriteFloat(binary,416+index*4,positions[index]);
        }
        WriteLe16(binary,452,0); WriteLe16(binary,454,1); WriteLe16(binary,456,2);
        std::memcpy(binary.data()+460,base.data()+132,48);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        WriteFixtureBytes(fixture.Root/"TwoMaterials.gltf",TextBytes(source));
        Gltf::BufferSet buffers;
        const auto decoded = DecodeSkeletalGltf(TextBytes(source),path,&buffers);
        assert(decoded.Succeeded() && buffers.GetCount()==1 && decoded.Data.Vertices.size()==6 && decoded.Data.Indices.size()==6);
        constexpr uint32_t indices[] = {0,2,1,3,5,4};
        for (size_t index=0;index<6;++index)
        {
            assert(decoded.Data.Indices[index]==indices[index]);
        }
        assert(decoded.Data.Vertices[3].Position.X==2 && decoded.Data.Vertices[4].Position.X==3);
        const auto checkTables = [](const SkeletalGltfData& data)
        {
            assert(data.SubMeshes.size()==2 && data.MaterialSlots.size()==2);
            assert(data.SubMeshes[0].IndexStart==0 && data.SubMeshes[0].IndexCount==3 && data.SubMeshes[0].MaterialSlot==0);
            assert(data.SubMeshes[1].IndexStart==3 && data.SubMeshes[1].IndexCount==3 && data.SubMeshes[1].MaterialSlot==1);
            assert(data.MaterialSlots[0].Name=="Body" && data.MaterialSlots[1].Name=="Eyes");
        };
        checkTables(decoded.Data);
        SkeletalGltfSourceBuffers legacyBuffers;
        const auto legacy = DecodeSkeletalGltf(CoreText(source),path,&legacyBuffers);
        auto embeddedText=source; replaceOnce(embeddedText,"\"uri\":\"fixture.bin\",","");
        const auto glb=MakeSkeletalGlb(embeddedText,binary);
        const auto embedded=DecodeSkeletalGltf(glb,path);
        const auto file=ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(path);
        assert(legacy.Succeeded() && embedded.Succeeded() && file.Succeeded() && legacyBuffers.size()==1);
        AssertEquivalent(decoded.Data,legacy.Data); AssertEquivalent(decoded.Data,embedded.Data); AssertEquivalent(decoded.Data,file.Data);
        checkTables(legacy.Data); checkTables(embedded.Data); checkTables(file.Data);
        const Container::AnsiString cookPath((fixture.Root/"TwoMaterials.gltf").generic_string().c_str());
        constexpr Container::AnsiStringView format="nvskel.v0.skinned.pnujiw.u32";
        SkeletalCookResult retained; retained.SourceHash=123; retained.VertexCount=456;
        Container::AnsiString error;
        SkeletalCookDiagnostics diagnostics;
        const auto sourceBytes=TextBytes(source);
        assert(CookGltfToNvskel(sourceBytes.data(),sourceBytes.size(),format,cookPath,retained,error,nullptr,nullptr,&diagnostics));
        assert(retained.VertexCount==6 && diagnostics.bDecodeAttempted && diagnostics.DecodeStatus==0);
        assert(retained.SubmeshCount == 2 && retained.MaterialSlotCount == 2);
        const auto cookedTables = Asset::ParseCookedSkeletal(MakeBlob(retained.NvskelBytes));
        assert(cookedTables.Succeeded() && cookedTables.Data.VersionMinor == 2);
        checkTables(cookedTables.Data.Skeletal);
        AssertEquivalent(decoded.Data, cookedTables.Data.Skeletal);
        assert(cookedTables.Data.Skeletal.SubMeshes[0].BoundsRadius > 0 &&
            cookedTables.Data.Skeletal.SubMeshes[1].BoundsCenter[0] == 2.5f);
        SkeletalCookResult glbCook;
        assert(CookGltfToNvskel(glb.data(),glb.size(),format,cookPath,glbCook,error));
        assert(glbCook.NvskelBytes == retained.NvskelBytes);
        RunSkeletalCacheVersionContract(fixture.Root, retained.NvskelBytes);
        ModelCookFingerprint fingerprint;
        assert(FingerprintModelCookSource(sourceBytes.data(),sourceBytes.size(),format,cookPath,"Models/two.gltf",fingerprint,error));
        const Container::AnsiString primitiveA=R"json({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5},"indices":8,"mode":4,"material":0})json";
        const Container::AnsiString primitiveB=R"json({"attributes":{"POSITION":13,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":15},"indices":14,"mode":4,"material":1})json";
        const Container::AnsiString originalList=R"json("primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5},"indices":8,"mode":4,"material":0},{"attributes":{"POSITION":13,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":15},"indices":14,"mode":4,"material":1}])json";
        for (size_t count : {size_t{8},size_t{9}})
        {
            Container::AnsiString list="\"primitives\":[";
            for (size_t index=0;index<count;++index)
            {
                if(index!=0)
                {
                    list+=",";
                }
                list += index%2==0 ? primitiveA : primitiveB;
            }
            list+="]";
            auto text=source; replaceOnce(text,originalList,list);
            const auto result=DecodeSkeletalGltf(TextBytes(text),path);
            if(count==9)
            {
                assert(result.Status==SkeletalGltfDecodeStatus::SubmeshLimitExceeded && result.Data.Vertices.empty());
            }
            else
            {
                assert(result.Succeeded() && result.Data.SubMeshes.size()==8 && result.Data.Vertices.size()==24 && result.Data.Indices.size()==24);
                const auto bytes = TextBytes(text);
                SkeletalCookResult eightCook;
                assert(CookGltfToNvskel(bytes.data(),bytes.size(),format,cookPath,eightCook,error));
                assert(eightCook.SubmeshCount == 8 && eightCook.MaterialSlotCount == 2);
                for(size_t index=0;index<8;++index)
                {
                    assert(result.Data.SubMeshes[index].IndexStart==index*3 && result.Data.SubMeshes[index].MaterialSlot==index%2);
                    assert(result.Data.Indices[index*3]==index*3 && result.Data.Indices[index*3+1]==index*3+2);
                }
            }
        }
        auto single=source; replaceOnce(single,originalList,Container::AnsiString("\"primitives\":[")+primitiveA+"]");
        const auto singleDecoded=DecodeSkeletalGltf(TextBytes(single),path);
        assert(singleDecoded.Succeeded() && singleDecoded.Data.SubMeshes.size() == 1 &&
            singleDecoded.Data.MaterialSlots.size() == 1 && singleDecoded.Data.MaterialSlots[0].Name == "Body");
        const auto singleBytes = TextBytes(single);
        SkeletalCookResult singleCook;
        assert(CookGltfToNvskel(singleBytes.data(),singleBytes.size(),format,cookPath,singleCook,error));
        assert(singleCook.SubmeshCount == 1 && singleCook.MaterialSlotCount == 1);
        const auto singleParsed = Asset::ParseCookedSkeletal(MakeBlob(singleCook.NvskelBytes));
        assert(singleParsed.Succeeded() && singleParsed.Data.VersionMinor == 2 &&
            singleParsed.Data.Skeletal.MaterialSlots[0].Name == "Body");
        auto unicodeSingle = single;
        replaceOnce(unicodeSingle, R"json("name":"Body")json", R"json("name":"\u9aa8\ud83d\udc3a")json");
        const auto unicodeBytes = TextBytes(unicodeSingle);
        SkeletalCookResult unicodeCook;
        assert(CookGltfToNvskel(unicodeBytes.data(),unicodeBytes.size(),format,cookPath,unicodeCook,error));
        const auto unicodeParsed = Asset::ParseCookedSkeletal(MakeBlob(unicodeCook.NvskelBytes));
        assert(unicodeParsed.Succeeded());
        const auto& unicodeName = unicodeParsed.Data.Skeletal.MaterialSlots[0].Name;
        uint8_t nameBytes[7]{};
        const uint8_t expectedName[] = {0xe9,0xaa,0xa8,0xf0,0x9f,0x90,0xba};
        assert(Asset::EncodeSkeletalWireName<Container::String::value_type>(2,
            {unicodeName.data(),unicodeName.size()},nameBytes).Succeeded());
        assert(std::memcmp(nameBytes,expectedName,7) == 0);
        const auto savedCook = unicodeCook;
        auto invalidSingle = single;
        replaceOnce(invalidSingle, R"json("material":0)json", R"json("material":99)json");
        const auto invalidBytes = TextBytes(invalidSingle);
        assert(!CookGltfToNvskel(invalidBytes.data(),invalidBytes.size(),format,cookPath,unicodeCook,error));
        assert(unicodeCook.NvskelBytes == savedCook.NvskelBytes && unicodeCook.SourceHash == savedCook.SourceHash);
        auto duplicateNames=source; replaceOnce(duplicateNames,"\"name\":\"Eyes\"","\"name\":\"Body\"");
        auto duplicate=DecodeSkeletalGltf(TextBytes(duplicateNames),path);
        assert(duplicate.Succeeded() && duplicate.Data.MaterialSlots[0].Name=="Body [0]" && duplicate.Data.MaterialSlots[1].Name=="Body [1]");
        replaceOnce(duplicateNames,originalList,Container::AnsiString("\"primitives\":[")+primitiveB+","+primitiveA+"]");
        const auto reversed=DecodeSkeletalGltf(TextBytes(duplicateNames),path);
        assert(reversed.Succeeded() && reversed.Data.MaterialSlots[0].Name=="Body [1]" && reversed.Data.MaterialSlots[1].Name=="Body [0]");
        auto noMaterial=source; replaceOnce(noMaterial,",\"material\":1","");
        const auto defaultSlot=DecodeSkeletalGltf(TextBytes(noMaterial),path);
        assert(defaultSlot.Succeeded() && defaultSlot.Data.MaterialSlots.size()==2 && defaultSlot.Data.MaterialSlots[1].Name=="Default");
        auto sameMaterial=source; replaceOnce(sameMaterial,"\"material\":1","\"material\":0");
        const auto shared=DecodeSkeletalGltf(TextBytes(sameMaterial),path);
        assert(shared.Succeeded() && shared.Data.MaterialSlots.size()==1 && shared.Data.SubMeshes[1].MaterialSlot==0);
        auto collision=source;
        replaceOnce(collision,"\"materials\":[{\"name\":\"Body\"},{\"name\":\"Eyes\"}]","\"materials\":[{\"name\":\"Body\"},{\"name\":\"Body\"},{\"name\":\"Body [0]\"}]");
        auto third=primitiveA; replaceOnce(third,"\"material\":0","\"material\":2");
        replaceOnce(collision,originalList,Container::AnsiString("\"primitives\":[")+primitiveA+","+primitiveB+","+third+"]");
        const auto disambiguated=DecodeSkeletalGltf(TextBytes(collision),path);
        assert(disambiguated.Succeeded() && disambiguated.Data.MaterialSlots[0].Name=="Body [0]_1" && disambiguated.Data.MaterialSlots[1].Name=="Body [1]" && disambiguated.Data.MaterialSlots[2].Name=="Body [0]");
        for(const char* invalid : {"99","-1","0.5"})
        {
            auto text=source; replaceOnce(text,"\"material\":1",Container::AnsiString("\"material\":")+invalid);
            assert(DecodeSkeletalGltf(TextBytes(text),path).Status==SkeletalGltfDecodeStatus::InvalidSubMesh);
        }
        auto wrongName=source; replaceOnce(wrongName,"\"name\":\"Eyes\"","\"name\":42");
        assert(DecodeSkeletalGltf(TextBytes(wrongName),path).Status==SkeletalGltfDecodeStatus::InvalidSubMesh);
        auto outOfPrimitive=binary; WriteLe16(outOfPrimitive,456,3); WriteFixtureBytes(fixture.Root/"fixture.bin",outOfPrimitive);
        assert(DecodeSkeletalGltf(sourceBytes,path).Status==SkeletalGltfDecodeStatus::InvalidAccessor);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        SkeletalGltfDecodeOptions options; options.InfluencePolicy=SkeletalInfluencePolicy::ReduceToFour;
        options.CubicSplinePolicy=SkeletalCubicSplinePolicy::Bake; options.MorphPolicy=SkeletalMorphPolicy::Drop;
        const auto reduced=DecodeSkeletalGltf(sourceBytes,path,nullptr,nullptr,&options);
        assert(reduced.Succeeded() && reduced.Report.TotalVertexCount==6 && reduced.Report.ProcessedVertexCount==6 && reduced.Report.bInfluenceScanComplete);
        AssertEquivalent(decoded.Data,reduced.Data); checkTables(reduced.Data);
        auto invalidWeight=binary; WriteFloat(invalidWeight,476,-.5f); WriteFixtureBytes(fixture.Root/"fixture.bin",invalidWeight);
        const auto prefix=DecodeSkeletalGltf(sourceBytes,path,&buffers,nullptr,&options);
        assert(prefix.Status==SkeletalGltfDecodeStatus::InvalidAccessor && prefix.Data.Vertices.empty() && buffers.GetCount()==0);
        assert(prefix.Report.TotalVertexCount==6 && prefix.Report.ProcessedVertexCount==4 && prefix.Report.FailedVertexIndex==4 && !prefix.Report.bInfluenceScanComplete);
        SkeletalImportReportInput report;
        report.Options=options; report.Diagnostics.bDecodeAttempted=true; report.Diagnostics.DecodeStatus=static_cast<uint32_t>(prefix.Status); report.Diagnostics.Report=prefix.Report;
        assert(BuildSkeletalImportReport(report).bValid);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        WriteFixtureBytes(fixture.Root/"TwoMaterials.gltf.import.json",TextBytes("{\"version\":1,\"units\":{\"fit\":{\"axis\":\"longest\",\"meters\":0.6}}}"));
        const auto fitted=DecodeSkeletalGltf(sourceBytes,path,nullptr,nullptr,&options);
        assert(fitted.Succeeded() && std::abs(fitted.Data.Vertices[4].Position.X-.6f)<1e-6f);
        assert(std::abs(fitted.Data.Clips[0].Channels[0].Samples[0].Value.Y-.2f)<1e-6f && std::abs(fitted.Data.MeshNodeGlobalTransform[12]-1.0f)<1e-6f);
        assert(std::filesystem::remove(fixture.Root/"TwoMaterials.gltf.import.json"));
        auto extraInfluence=source; replaceOnce(extraInfluence,"\"WEIGHTS_0\":15","\"WEIGHTS_0\":15,\"JOINTS_1\":3");
        const auto extraBytes=TextBytes(extraInfluence);
        assert(DecodeSkeletalGltf(extraBytes,path).Status==SkeletalGltfDecodeStatus::InfluenceLimitExceeded);
        assert(!FingerprintModelCookSource(extraBytes.data(),extraBytes.size(),format,cookPath,"Models/two.gltf",fingerprint,error));
        // 2primitiveのdelta総数2とmesh-level target幅1を区別する。
        auto morphSource=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"MorphDrop.gltf"));
        const Container::AnsiString morphPrimitive=R"json({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5,"TANGENT":15},"indices":8,"mode":4,"targets":[{"POSITION":13,"NORMAL":14,"TANGENT":16}]})json";
        replaceOnce(morphSource,Container::AnsiString("\"primitives\":[")+morphPrimitive+"]",Container::AnsiString("\"primitives\":[")+morphPrimitive+","+morphPrimitive+"]");
        WriteFixtureBytes(fixture.Root/"fixture.bin",BuildMorphFixtureBuffer());
        SkeletalGltfDecodeOptions drop; drop.MorphPolicy=SkeletalMorphPolicy::Drop;
        const auto morph=DecodeSkeletalGltf(TextBytes(morphSource),path,nullptr,nullptr,&drop);
        assert(morph.Succeeded() && morph.Data.Vertices.size()==6 && morph.Report.bMorphScanComplete && morph.Report.DroppedMorphTargetCount==2 && morph.Report.MorphTargetWidth==1);
        assert(morph.Report.DroppedMorphMeshWeightCount==1 && morph.Report.DroppedMorphNodeWeightCount==1 && morph.Report.DroppedMorphAnimationChannelCount==1);
        report={}; report.Outcome=SkeletalImportOutcome::PayloadReady; report.Options=drop; report.Diagnostics.bDecodeAttempted=true; report.Diagnostics.Report=morph.Report;
        const auto json=BuildSkeletalImportReport(report);
        assert(json.bValid && std::strstr(json.Bytes,"\"mesh_target_width\":1") && std::strstr(json.Bytes,"\"dropped_targets\":2"));
        const Container::AnsiString targetList="\"targets\":[{\"POSITION\":13,\"NORMAL\":14,\"TANGENT\":16}]";
        auto noTargets=morphPrimitive; replaceOnce(noTargets,Container::AnsiString(",")+targetList,"");
        auto optionalMorph=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"MorphDrop.gltf"));
        replaceOnce(optionalMorph,Container::AnsiString("\"primitives\":[")+morphPrimitive+"]",Container::AnsiString("\"primitives\":[")+morphPrimitive+","+noTargets+"]");
        const auto partialMorph=DecodeSkeletalGltf(TextBytes(optionalMorph),path,nullptr,nullptr,&drop);
        assert(partialMorph.Succeeded() && partialMorph.Report.DroppedMorphTargetCount==1 && partialMorph.Report.MorphTargetWidth==1);
        auto mismatchedPrimitive=morphPrimitive;
        replaceOnce(mismatchedPrimitive,targetList,"\"targets\":[{\"POSITION\":13},{\"NORMAL\":14}]");
        auto mismatchedMorph=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"MorphDrop.gltf"));
        replaceOnce(mismatchedMorph,Container::AnsiString("\"primitives\":[")+morphPrimitive+"]",Container::AnsiString("\"primitives\":[")+morphPrimitive+","+mismatchedPrimitive+"]");
        const auto wrongWidth=DecodeSkeletalGltf(TextBytes(mismatchedMorph),path,nullptr,nullptr,&drop);
        assert(!wrongWidth.Succeeded() && !wrongWidth.Report.bMorphScanComplete && wrongWidth.Data.Vertices.empty());

    }

    void RunMultiClipContract()
    {
        using namespace Skeletal;
        using namespace NorvesLib::Tools::AssetCook;
        LooseFixture fixture;
        const auto source = ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ThreeClips.gltf"));
        const auto bytes = TextBytes(source);
        const auto path = ToCorePath(fixture.Root/"ThreeClips.gltf");
        const Container::AnsiString cookPath((fixture.Root/"ThreeClips.gltf").generic_string().c_str());
        constexpr Container::AnsiStringView format = "nvskel.v0.skinned.pnujiw.u32";
        ByteArray binary(524,0);
        const auto base = BuildLooseFixtureBuffer();
        std::memcpy(binary.data(),base.data(),base.size());
        constexpr float positions[] = {2,0,0,3,0,0,2,1,0};
        for (size_t index=0;index<9;++index)
        {
            WriteFloat(binary,416+index*4,positions[index]);
        }
        WriteLe16(binary,452,0); WriteLe16(binary,454,1); WriteLe16(binary,456,2);
        std::memcpy(binary.data()+460,base.data()+132,48);
        WriteFloat(binary,508,0); WriteFloat(binary,512,3);
        WriteFloat(binary,516,0); WriteFloat(binary,520,4);
        WriteFixtureBytes(fixture.Root/"fixture.bin",binary);
        WriteFixtureBytes(fixture.Root/"ThreeClips.gltf",bytes);
        assert(DecodeSkeletalGltf(bytes,path).Status==SkeletalGltfDecodeStatus::UnsupportedClipCount);
        assert(ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(path).Status==SkeletalGltfDecodeStatus::UnsupportedClipCount);
        Gltf::BufferSet sources;
        const auto decoded = DecodeRigGltf(bytes,path,&sources);
        assert(decoded.Succeeded() && sources.GetCount()==1 && decoded.Data.Clips.size()==3 && decoded.Data.SubMeshes.size()==2);
        constexpr const char* names[] = {"Wave","Run","Idle"};
        constexpr size_t channels[] = {2,1,2};
        for (size_t index=0;index<3;++index)
        {
            assert(decoded.Data.Clips[index].Name==names[index]);
            assert(decoded.Data.Clips[index].DurationSeconds==float(index+2));
            assert(decoded.Data.Clips[index].Channels.size()==channels[index]);
        }
        const auto glb = MakeSkeletalGlb(ChangeBufferUri(source,""),binary);
        const auto embedded = DecodeRigGltf(glb,path);
        assert(embedded.Succeeded()); AssertEquivalent(decoded.Data,embedded.Data);
        Container::AnsiString error;
        SkeletalCookDiagnostics diagnostics;
        SkeletalCookResult cooked,embeddedCook;
        assert(CookGltfToNvskel(bytes.data(),bytes.size(),format,cookPath,cooked,error,nullptr,nullptr,&diagnostics));
        assert(cooked.ClipCount==3);
        const auto parsed = Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsed.Succeeded() && parsed.Data.VersionMinor==2); AssertEquivalent(decoded.Data,parsed.Data.Skeletal);
        assert(CookGltfToNvskel(glb.data(),glb.size(),format,cookPath,embeddedCook,error));
        assert(embeddedCook.NvskelBytes==cooked.NvskelBytes);
        const auto invalid = TextBytes(ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ThreeClipsInvalid.gltf")));
        SkeletalGltfDecodeOptions bake; bake.CubicSplinePolicy=SkeletalCubicSplinePolicy::Bake;
        const auto failed = DecodeRigGltf(invalid,path,&sources,nullptr,&bake);
        assert(!failed.Succeeded() && failed.Data.Vertices.empty() && failed.Data.Clips.empty() && sources.GetCount()==0);
        assert(failed.Report.bCubicScanStarted && !failed.Report.bCubicScanComplete &&
            failed.Report.TotalAnimationChannelCount==5 && failed.Report.ProcessedAnimationChannelCount==3 &&
            failed.Report.FailedAnimationChannelIndex==3);
        SkeletalCookResult held; held.NvskelBytes={77};
        assert(!CookGltfToNvskel(invalid.data(),invalid.size(),format,cookPath,held,error,nullptr,&bake,&diagnostics));
        assert(held.NvskelBytes.size()==1 && held.NvskelBytes[0]==77);
        WriteFixtureBytes(fixture.Root/"ThreeClips.gltf.import.json",TextBytes("{\"version\":1,\"units\":{\"scale\":2}}"));
        const auto scaled = DecodeRigGltf(bytes,path);
        assert(scaled.Succeeded());
        for (size_t index=0;index<3;++index)
        {
            const auto& a=decoded.Data.Clips[index].Channels[0].Samples;
            const auto& b=scaled.Data.Clips[index].Channels[0].Samples;
            assert(a.size()==b.size());
            for (size_t key=0;key<a.size();++key)
            {
                assert(b[key].Value.Y==a[key].Value.Y*2);
            }
        }
        std::filesystem::remove(fixture.Root/"ThreeClips.gltf.import.json");
        const auto cubicText=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ThreeCubicClips.gltf"));
        const auto cubic=TextBytes(cubicText);
        const auto cubicBinary=BuildCubicFixtureBuffer();
        WriteFixtureBytes(fixture.Root/"fixture.bin",cubicBinary);
        const auto single=DecodeSkeletalGltf(TextBytes(ReadFixtureJson(ToCorePath(FindFixtureRoot()/"CubicChannels.gltf"))),path,nullptr,nullptr,&bake);
        const auto baked=DecodeRigGltf(cubic,path,nullptr,nullptr,&bake);
        assert(single.Succeeded() && baked.Succeeded() && baked.Data.Clips.size()==3);
        assert(baked.Report.bCubicScanComplete && baked.Report.TotalAnimationChannelCount==9 &&
            baked.Report.ProcessedAnimationChannelCount==9 && baked.Report.BakedCubicChannelCount==9 &&
            baked.Report.CubicOutputKeyCount==single.Report.CubicOutputKeyCount*3);
        WriteFixtureBytes(fixture.Root/"ThreeClips.gltf.import.json",TextBytes("{\"version\":1,\"units\":{\"scale\":2}}"));
        const auto scaledBaked=DecodeRigGltf(cubic,path,nullptr,nullptr,&bake);
        assert(scaledBaked.Succeeded());
        for (const auto& clip : scaledBaked.Data.Clips)
        {
            assert(clip.Channels[0].Samples.front().Value.Y==2 && clip.Channels[0].Samples.back().Value.Y==6);
        }
        std::filesystem::remove(fixture.Root/"ThreeClips.gltf.import.json");
        auto limited=bake; limited.CubicMaximumSamplesPerAsset=static_cast<uint32_t>(single.Report.CubicOutputKeyCount*2);
        const auto over=DecodeRigGltf(cubic,path,nullptr,nullptr,&limited);
        assert(!over.Succeeded() && over.Data.Clips.empty() && over.Report.bHasCubicBakeFailure &&
            !over.Report.bCubicScanComplete && over.Report.ProcessedAnimationChannelCount==6 &&
            over.Report.FailedAnimationChannelIndex==6 && over.Report.CubicOutputKeyCount==limited.CubicMaximumSamplesPerAsset);
        const auto cubicGlb=MakeSkeletalGlb(ChangeBufferUri(cubicText,""),cubicBinary);
        const auto bakedGlb=DecodeRigGltf(cubicGlb,path,nullptr,nullptr,&bake);
        assert(bakedGlb.Succeeded()); AssertEquivalent(baked.Data,bakedGlb.Data);
        assert(CookGltfToNvskel(cubic.data(),cubic.size(),format,cookPath,cooked,error,nullptr,&bake));
        const auto parsedCubic=Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsedCubic.Succeeded()); AssertEquivalent(baked.Data,parsedCubic.Data.Skeletal);
        const auto morphText=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ThreeMorphClips.gltf"));
        const auto morph=TextBytes(morphText);
        const auto morphBinary=BuildMorphFixtureBuffer();
        WriteFixtureBytes(fixture.Root/"fixture.bin",morphBinary);
        auto drop=bake; drop.MorphPolicy=SkeletalMorphPolicy::Drop;
        const auto dropped=DecodeRigGltf(morph,path,nullptr,nullptr,&drop);
        assert(dropped.Succeeded() && dropped.Data.Clips.size()==3 && dropped.Report.bMorphScanComplete &&
            dropped.Report.DroppedMorphTargetCount==1 && dropped.Report.MorphTargetWidth==1 &&
            dropped.Report.DroppedMorphMeshWeightCount==1 && dropped.Report.DroppedMorphNodeWeightCount==1 &&
            dropped.Report.DroppedMorphAnimationChannelCount==3 && dropped.Report.ProcessedAnimationChannelCount==9 &&
            dropped.Report.TotalAnimationChannelCount==9 && dropped.Report.BakedCubicChannelCount==0);
        assert(CookGltfToNvskel(morph.data(),morph.size(),format,cookPath,cooked,error,nullptr,&drop,&diagnostics));
        const auto parsedMorph=Asset::ParseCookedSkeletal(MakeBlob(cooked.NvskelBytes));
        assert(parsedMorph.Succeeded()); AssertEquivalent(dropped.Data,parsedMorph.Data.Skeletal);
        const auto morphGlb=MakeSkeletalGlb(ChangeBufferUri(morphText,""),morphBinary);
        const auto droppedGlb=DecodeRigGltf(morphGlb,path,nullptr,nullptr,&drop);
        assert(droppedGlb.Succeeded()); AssertEquivalent(dropped.Data,droppedGlb.Data);
        const auto badMorph=TextBytes(ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ThreeMorphClipsInvalid.gltf")));
        const auto morphFailure=DecodeRigGltf(badMorph,path,&sources,nullptr,&drop);
        assert(!morphFailure.Succeeded() && morphFailure.Data.Clips.empty() && sources.GetCount()==0 &&
            !morphFailure.Report.bMorphScanComplete && morphFailure.Report.DroppedMorphAnimationChannelCount==0 &&
            morphFailure.Report.DroppedMorphTargetCount==0);
        SkeletalImportReportInput reportInput;
        reportInput.Options=drop; reportInput.Diagnostics=diagnostics; reportInput.Outcome=SkeletalImportOutcome::PayloadReady;
        assert(BuildSkeletalImportReport(reportInput).bValid);
    }

    void RunUnitContract()
    {
        AssertSkinnedVertexAbi();
        assert(Asset::CookedSkeletalFormatV0::EntryType == Asset::MakeAssetPackageFourCC('S', 'k', 'l', '0'));
        assert(Container::AnsiStringView(Asset::CookedSkeletalFormatV0::FormatName) ==
               Container::AnsiStringView("nvskel.v0.skinned.pnujiw.u32"));

        const ByteArray goldenBytes = BuildGoldenSkeletal();
        assert(goldenBytes.size() == 861);
        const uint64_t goldenHash =
            Asset::ComputeCookedSkeletalV01Hash(goldenBytes.data() + 192, goldenBytes.data() + 256,
                                                goldenBytes.size() - 256);

        Asset::CookedSkeletalParseResult retainedResult;
        {
            Asset::AssetBlob source = MakeBlob(goldenBytes);
            retainedResult = Asset::ParseCookedSkeletal(source);
            source = Asset::AssetBlob::Invalid();
        }
        assert(retainedResult.Succeeded());
        assert(retainedResult.Status == Asset::CookedSkeletalParseStatus::Success);
        AssertLiteralCookedData(retainedResult.Data, goldenHash);

        {
            ByteArray legacyBytes = goldenBytes;
            WriteLe16(legacyBytes, 14, 0);
            for (size_t byteIndex = 192; byteIndex < 256; ++byteIndex)
            {
                legacyBytes[byteIndex] = 0;
            }
            RecomputeSkeletalHash(legacyBytes);
            const Asset::CookedSkeletalParseResult legacy = Asset::ParseCookedSkeletal(MakeBlob(legacyBytes));
            assert(legacy.Succeeded());
            assert(legacy.Data.Skeletal.MeshNodeGlobalTransform[0] == 1.0f);
            assert(legacy.Data.Skeletal.MeshNodeGlobalTransform[5] == 1.0f);
            assert(legacy.Data.Skeletal.MeshNodeGlobalTransform[10] == 1.0f);
            assert(legacy.Data.Skeletal.MeshNodeGlobalTransform[15] == 1.0f);
            assert(legacy.Data.Skeletal.MeshNodeGlobalTransform[12] == 0.0f);
        }

        {
            LooseFixture fixture;
            const Skeletal::SkeletalGltfDecodeResult loose = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path());
            assert(loose.Succeeded());
            AssertEquivalent(loose.Data, retainedResult.Data.Skeletal);
            const Container::AnsiString text = ReadFixtureJson(fixture.Path());
            const ByteArray external = TextBytes(text);
            // Bake指定でもCUBICSPLINEが無い既存LINEAR/STEP資産の値は変わらない。
            Skeletal::SkeletalGltfDecodeOptions noCubicBake;
            noCubicBake.CubicSplinePolicy=Skeletal::SkeletalCubicSplinePolicy::Bake;
            const auto noCubic=Skeletal::DecodeSkeletalGltf(external,fixture.Path(),nullptr,nullptr,&noCubicBake);
            assert(noCubic.Succeeded() && noCubic.Report.bCubicScanComplete && noCubic.Report.BakedCubicChannelCount==0);
            AssertEquivalent(loose.Data,noCubic.Data);
            const ByteArray binary = BuildLooseFixtureBuffer();
            const Container::AnsiString withoutUri = ChangeBufferUri(text, "");
            const ByteArray glb = MakeSkeletalGlb(withoutUri, binary);
            const Container::AnsiString embeddedUri = Container::AnsiString("\"uri\":\"data:application/octet-stream;base64,") +
                EncodeFixtureBase64(binary) + "\",";
            const ByteArray dataUri = TextBytes(ChangeBufferUri(text, embeddedUri));
            NorvesLib::Core::Gltf::BufferSet sources;
            for (const ByteArray* source : {&external, &glb, &dataUri})
            {
                const auto decoded = Skeletal::DecodeSkeletalGltf({source->data(), source->size()}, fixture.Path(), &sources);
                assert(decoded.Succeeded());
                AssertEquivalent(decoded.Data, loose.Data);
                assert(sources.GetCount() == 1 && sources.GetBytes(0).size() == binary.size());
                assert(std::memcmp(sources.GetBytes(0).data(), binary.data(), binary.size()) == 0);
                if (source == &glb)
                {
                    NorvesLib::Core::Gltf::ContainerView view;
                    assert(NorvesLib::Core::Gltf::ParseContainer(glb, view) == NorvesLib::Core::Gltf::ContainerParseResult::Success);
                    assert(sources.GetBytes(0).data() == view.Bin.data());
                }
            }
            // 正常な骨格fixtureへ必須拡張だけを加え、ガード欠落なら成功する入力で反証する。
            const auto objectStart = text.find('{');
            assert(objectStart != Container::AnsiString::npos);
            for (const char* declaration : {"\"extensionsRequired\":[],",
                "\"extensionsUsed\":[\"KHR_draco_mesh_compression\"],"})
            {
                const Container::AnsiString optional = Container::AnsiString("{") + declaration + text.substr(objectStart + 1);
                const auto decoded = Skeletal::DecodeSkeletalGltf(TextBytes(optional), fixture.Path());
                assert(decoded.Succeeded());
                AssertEquivalent(decoded.Data, loose.Data);
            }
            for (const char* extension : {"KHR_draco_mesh_compression", "EXT_meshopt_compression",
                "KHR_mesh_quantization", "KHR_texture_transform"})
            {
                Container::AnsiString required = "{\"extensionsRequired\":[\"";
                required += extension;
                required += "\"],";
                required += text.substr(objectStart + 1);
                assert(Skeletal::DecodeSkeletalGltf(glb, fixture.Path(), &sources).Succeeded());
                const auto rejected = Skeletal::DecodeSkeletalGltf(TextBytes(required), fixture.Path(), &sources);
                assert(rejected.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidDocument && sources.GetCount() == 0);
                assert(Skeletal::DecodeSkeletalGltf(CoreText(required), fixture.Path()).Status ==
                    Skeletal::SkeletalGltfDecodeStatus::InvalidDocument);
            }
            const auto glbPath = fixture.Root / "embedded.glb";
            WriteFixtureBytes(glbPath, glb);
            const auto looseGlb = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(glbPath));
            assert(looseGlb.Succeeded());
            AssertEquivalent(looseGlb.Data, loose.Data);

            Skeletal::SkeletalGltfSourceBuffers legacySources;
            const auto legacy = Skeletal::DecodeSkeletalGltf(CoreText(text), fixture.Path(), &legacySources);
            assert(legacy.Succeeded() && legacySources.size() == 1 && legacySources[0].size() == binary.size());
            AssertEquivalent(legacy.Data, loose.Data);

            using NorvesLib::Tools::AssetCook::SkeletalCookResult;
            const Container::AnsiString sourcePath((fixture.Root / "ValidU8Float.gltf").generic_string().c_str());
            auto cook = [&](const ByteArray& source, SkeletalCookResult& out)
            {
                Container::AnsiString error;
                NorvesLib::Tools::AssetCook::ModelCookFingerprint fingerprint;
                const bool identified=NorvesLib::Tools::AssetCook::FingerprintModelCookSource(source.data(),source.size(),
                    "nvskel.v0.skinned.pnujiw.u32",sourcePath,"Models/rig.gltf",fingerprint,error);
                const bool cooked=NorvesLib::Tools::AssetCook::CookGltfToNvskel(source.data(), source.size(),
                    "nvskel.v0.skinned.pnujiw.u32", sourcePath, out, error);
                if (cooked)
                {
                    assert(identified && fingerprint.SourceHash==out.SourceHash && fingerprint.EmbeddedImages.empty());
                    assert(fingerprint.ImportSettingsHash==out.ImportSettingsHash &&
                        fingerprint.bHasImportSettings==out.bHasImportSettings && fingerprint.ImportSettingsPath==out.ImportSettingsPath);
                }
                return cooked;
            };
            SkeletalCookResult externalCook;
            assert(cook(external, externalCook));
            assert(externalCook.SourceHash == HashSourcePart(HashSourcePart(14695981039346656037ull, external), binary));
            SkeletalCookResult explicitStrict;
            Skeletal::SkeletalGltfDecodeOptions strictOptions;
            Container::AnsiString strictError;
            assert(NorvesLib::Tools::AssetCook::CookGltfToNvskel(external.data(), external.size(),
                "nvskel.v0.skinned.pnujiw.u32", sourcePath, explicitStrict, strictError, nullptr, &strictOptions));
            assert(explicitStrict.SourceHash == externalCook.SourceHash && explicitStrict.NvskelBytes == externalCook.NvskelBytes);

            for (const ByteArray* source : {&glb, &dataUri})
            {
                SkeletalCookResult result;
                assert(cook(*source, result));
                assert(result.NvskelBytes.size() == externalCook.NvskelBytes.size());
                assert(std::memcmp(result.NvskelBytes.data(), externalCook.NvskelBytes.data(), result.NvskelBytes.size()) == 0);
                assert(result.SourceHash == HashSourcePart(14695981039346656037ull, *source));
                assert(result.VertexCount == externalCook.VertexCount && result.IndexCount == externalCook.IndexCount &&
                    result.JointCount == externalCook.JointCount && result.ClipCount == externalCook.ClipCount);
            }
            Container::AnsiString errorForExtra;
            // 旧版が黙認した追加セットをraw/legacy/cook前照合/本cookの全入口で拒否する。
            const size_t attributesStart=text.find("\"attributes\": {");
            assert(attributesStart!=Container::AnsiString::npos);
            const size_t insert=attributesStart+std::strlen("\"attributes\": {");
            const auto retainedExtraPayload=externalCook.NvskelBytes;
            const auto retainedExtraHash=externalCook.SourceHash;
            for (const char* extra : {"\"JOINTS_1\":3,", "\"WEIGHTS_7\":5,", "\"JOINTS_1\":null,",
                "\"JOINTS_4294967295\":3,", "\"JOINTS_1\":3,\"WEIGHTS_1\":5,"})
            {
                const Container::AnsiString extraText=Container::AnsiString(text.substr(0,insert))+extra+Container::AnsiString(text.substr(insert));
                const auto extraBytes=TextBytes(extraText);
                assert(Skeletal::DecodeSkeletalGltf(extraBytes,fixture.Path(),&sources).Status==
                    Skeletal::SkeletalGltfDecodeStatus::InfluenceLimitExceeded && sources.GetCount()==0);
                assert(Skeletal::DecodeSkeletalGltf(CoreText(extraText),fixture.Path(),&legacySources).Status==
                    Skeletal::SkeletalGltfDecodeStatus::InfluenceLimitExceeded && legacySources.empty());
                NorvesLib::Tools::AssetCook::ModelCookFingerprint fingerprint;
                fingerprint.SourceHash=123;
                assert(!NorvesLib::Tools::AssetCook::FingerprintModelCookSource(extraBytes.data(),extraBytes.size(),
                    "nvskel.v0.skinned.pnujiw.u32",sourcePath,"Models/rig.gltf",fingerprint,errorForExtra));
                assert(fingerprint.SourceHash==123);
                assert(!cook(extraBytes,externalCook) && externalCook.SourceHash==retainedExtraHash && externalCook.NvskelBytes==retainedExtraPayload);
            }
            const Container::AnsiString customText=Container::AnsiString(text.substr(0,insert))+"\"_JOINTS_1\":3,"+Container::AnsiString(text.substr(insert));
            const auto custom=Skeletal::DecodeSkeletalGltf(TextBytes(customText),fixture.Path());
            assert(custom.Succeeded()); AssertEquivalent(custom.Data,loose.Data);
            const auto zeroExtraText=ReadFixtureJson(ToCorePath(FindFixtureRoot()/"ExtraZeroInfluences.gltf"));
            ByteArray zeroExtraBinary=binary; zeroExtraBinary.resize(464,0);
            WriteFixtureBytes(fixture.Root/"extra_zero.bin",zeroExtraBinary);
            const auto zeroExtraGlb=MakeSkeletalGlb(ChangeBufferUri(zeroExtraText,""),zeroExtraBinary);
            assert(Skeletal::DecodeSkeletalGltf(zeroExtraGlb,fixture.Path()).Status==
                Skeletal::SkeletalGltfDecodeStatus::InfluenceLimitExceeded);
            assert(!cook(zeroExtraGlb,externalCook) && externalCook.SourceHash==retainedExtraHash && externalCook.NvskelBytes==retainedExtraPayload);

            Container::AnsiString errorForImport;
            // source隣sidecarをraw/legacy/loose/cookerで同じsnapshotとして適用する。
            auto sidecar = fixture.Root / "ValidU8Float.gltf.import.json";
            WriteFixtureBytes(sidecar,TextBytes("{\"version\":1}"));
            SkeletalCookResult identityImport;
            assert(cook(external,identityImport) && identityImport.bHasImportSettings);
            assert(identityImport.SourceHash!=externalCook.SourceHash && identityImport.NvskelBytes==externalCook.NvskelBytes);
            WriteFixtureBytes(sidecar,TextBytes("{\"version\":1,\"units\":{\"scale\":2}}"));
            const auto scaled = Skeletal::DecodeSkeletalGltf(external,fixture.Path());
            assert(scaled.Succeeded());
            AssertScaledSkeletal(loose.Data,scaled.Data,2.0f);
            AssertScaledSample(loose.Data,scaled.Data,2.0f);
            const auto scaledLegacy = Skeletal::DecodeSkeletalGltf(CoreText(text),fixture.Path());
            assert(scaledLegacy.Succeeded()); AssertEquivalent(scaled.Data,scaledLegacy.Data);
            const auto scaledLoose = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path());
            assert(scaledLoose.Succeeded()); AssertEquivalent(scaled.Data,scaledLoose.Data);
            SkeletalCookResult scaledCook;
            assert(cook(external,scaledCook) && scaledCook.bHasImportSettings && scaledCook.SourceHash!=externalCook.SourceHash);
            const auto scaledParsed = Asset::ParseCookedSkeletal(MakeBlob(scaledCook.NvskelBytes));
            assert(scaledParsed.Succeeded()); AssertEquivalent(scaled.Data,scaledParsed.Data.Skeletal);
            const auto scaledHash=scaledCook.SourceHash;
            const auto settingsHash=scaledCook.ImportSettingsHash;
            WriteFixtureBytes(sidecar,TextBytes("{ \"meta\":{\"note\":\"format only\"},\"units\":{\"scale\":2},\"version\":1 }"));
            assert(cook(external,scaledCook) && scaledCook.SourceHash==scaledHash && scaledCook.ImportSettingsHash==settingsHash);
            // .glbも同じデータ/設定を適用し、BINを別途hashしない。
            auto glbSidecar=glbPath; glbSidecar+=".import.json";
            WriteFixtureBytes(glbSidecar,TextBytes("{\"version\":1,\"units\":{\"scale\":2}}"));
            const auto scaledGlb=ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(glbPath));
            assert(scaledGlb.Succeeded()); AssertEquivalent(scaled.Data,scaledGlb.Data);

            NorvesLib::Core::AssetImport::ImportSettingsFileOptions disabledImport;
            disabledImport.bDisabled=true;
            SkeletalCookResult unscaledCook;
            assert(NorvesLib::Tools::AssetCook::CookGltfToNvskel(external.data(),external.size(),
                "nvskel.v0.skinned.pnujiw.u32",sourcePath,unscaledCook,errorForImport,&disabledImport));
            assert(unscaledCook.SourceHash==externalCook.SourceHash && !unscaledCook.bHasImportSettings);
            assert(unscaledCook.NvskelBytes==externalCook.NvskelBytes);
            NorvesLib::Core::AssetImport::LoadedImportSettings noImport;
            const auto unscaled= Skeletal::DecodeSkeletalGltf(external,fixture.Path(),nullptr,&noImport);
            assert(unscaled.Succeeded()); AssertEquivalent(loose.Data,unscaled.Data);

            WriteFixtureBytes(sidecar,TextBytes("{\"version\":1,\"units\":{\"fit\":{\"axis\":\"up\",\"meters\":0.6}}}"));
            const auto fitted=Skeletal::DecodeSkeletalGltf(external,fixture.Path());
            assert(fitted.Succeeded());
            float minY=loose.Data.Vertices[0].Position.Y,maxY=minY;
            for(const auto& vertex:loose.Data.Vertices)
            {
                minY=std::min(minY,vertex.Position.Y); maxY=std::max(maxY,vertex.Position.Y);
            }
            const float fitScale=0.6f/(maxY-minY);
            AssertScaledSkeletal(loose.Data,fitted.Data,fitScale);
            AssertScaledSample(loose.Data,fitted.Data,fitScale);
            const size_t meshName=text.find("\"name\": \"Mesh\"");
            assert(meshName!=Container::AnsiString::npos);
            const Container::AnsiString nodeScaled = Container::AnsiString(text.substr(0,meshName)) +
                "\"scale\":[1,2,1]," + Container::AnsiString(text.substr(meshName));
            const auto nodeBaseline=Skeletal::DecodeSkeletalGltf(CoreText(nodeScaled),fixture.Path(),nullptr,&noImport);
            const auto nodeFitted=Skeletal::DecodeSkeletalGltf(CoreText(nodeScaled),fixture.Path());
            assert(nodeBaseline.Succeeded() && nodeFitted.Succeeded());
            AssertScaledSkeletal(nodeBaseline.Data,nodeFitted.Data,fitScale/2.0f);
            AssertScaledSample(nodeBaseline.Data,nodeFitted.Data,fitScale/2.0f);

            // 非identityのScaleチャンネルを持つ入力でも回転/scaleは変更しない。
            const auto scaleText = ReadFixtureJson(ToCorePath(FindFixtureRoot() / "ValidImportScale.gltf"));
            ByteArray scaleBinary = binary;
            scaleBinary.resize(440);
            constexpr float scaleKeys[] = {1.0f,2.0f,1.0f,2.0f,3.0f,0.5f};
            for (size_t index = 0; index < 6; ++index)
            {
                WriteFloat(scaleBinary,416 + index * sizeof(float),scaleKeys[index]);
            }
            const auto scaleGlb = MakeSkeletalGlb(ChangeBufferUri(scaleText,""),scaleBinary);
            const auto scaleBaseline = Skeletal::DecodeSkeletalGltf(scaleGlb,fixture.Path(),nullptr,&noImport);
            assert(scaleBaseline.Succeeded() && scaleBaseline.Data.Clips[0].Channels.size()==3);
            const auto& scaleChannel = scaleBaseline.Data.Clips[0].Channels[2];
            assert(scaleChannel.Path==Skeletal::SkeletalAnimationPath::Scale && scaleChannel.Samples.size()==2);
            assert(scaleChannel.Samples[0].Value.Y==2.0f && scaleChannel.Samples[1].Value.X==2.0f &&
                scaleChannel.Samples[1].Value.Y==3.0f && scaleChannel.Samples[1].Value.Z==0.5f);
            for (bool fit : {false,true})
            {
                WriteFixtureBytes(sidecar,TextBytes(fit ?
                    "{\"version\":1,\"units\":{\"fit\":{\"axis\":\"up\",\"meters\":0.6}}}" :
                    "{\"version\":1,\"units\":{\"scale\":2}}"));
                const auto imported = Skeletal::DecodeSkeletalGltf(scaleGlb,fixture.Path());
                assert(imported.Succeeded());
                const float factor = fit ? fitScale : 2.0f;
                AssertScaledSkeletal(scaleBaseline.Data,imported.Data,factor);
                AssertScaledSample(scaleBaseline.Data,imported.Data,factor);
                SkeletalCookResult importedCook;
                assert(cook(scaleGlb,importedCook));
                const auto importedParsed = Asset::ParseCookedSkeletal(MakeBlob(importedCook.NvskelBytes));
                assert(importedParsed.Succeeded());
                AssertEquivalent(imported.Data,importedParsed.Data.Skeletal);
                AssertScaledSkeletal(scaleBaseline.Data,importedParsed.Data.Skeletal,factor);
                AssertScaledSample(scaleBaseline.Data,importedParsed.Data.Skeletal,factor);
            }

            const auto retainedHash=scaledCook.SourceHash;
            const ByteArray retainedImportPayload=scaledCook.NvskelBytes;
            for(const char* invalid:{
                "{\"version\":1,\"axes\":{\"up\":\"+Z\",\"forward\":\"+X\"}}",
                "{\"version\":1,\"axes\":{\"mirrorX\":true}}",
                "{\"version\":1,\"origin\":{\"mode\":\"bounds_center\"}}",
                "{\"version\":1,\"origin\":{\"mode\":\"surface_centroid\"}}",
                "{\"version\":1,\"mesh\":{\"flipV\":true}}",
                "{\"version\":1,\"mesh\":{\"winding\":\"flip\"}}",
                "{\"version\":1,\"units\":{\"scale\":1e308}}"})
            {
                WriteFixtureBytes(sidecar,TextBytes(invalid));
                assert(!Skeletal::DecodeSkeletalGltf(external,fixture.Path(),&sources).Succeeded() && sources.GetCount()==0);
                assert(!cook(external,scaledCook) && scaledCook.SourceHash==retainedHash);
                assert(scaledCook.NvskelBytes==retainedImportPayload);
            }
            assert(std::filesystem::remove(sidecar));
            assert(std::filesystem::remove(glbSidecar));
            assert(cook(external,unscaledCook) && unscaledCook.SourceHash==externalCook.SourceHash);
            assert(unscaledCook.NvskelBytes==externalCook.NvskelBytes);
            NorvesLib::Core::AssetImport::ImportSettingsFileOptions requiredImport; requiredImport.bRequired=true;
            assert(!NorvesLib::Tools::AssetCook::CookGltfToNvskel(glb.data(),glb.size(),
                "nvskel.v0.skinned.pnujiw.u32",{},scaledCook,errorForImport,&requiredImport));
            assert(NorvesLib::Tools::AssetCook::CookGltfToNvskel(glb.data(),glb.size(),
                "nvskel.v0.skinned.pnujiw.u32",{},unscaledCook,errorForImport));
            assert(unscaledCook.NvskelBytes==externalCook.NvskelBytes);

            ByteArray withBom{0xef, 0xbb, 0xbf};
            withBom.insert(withBom.end(), external.begin(), external.end());
            ByteArray withExtra = binary;
            withExtra.push_back(77);
            WriteFixtureBytes(fixture.Root / "fixture.bin", withExtra);
            SkeletalCookResult extraCook;
            assert(cook(withBom, extraCook));
            assert(extraCook.SourceHash == HashSourcePart(HashSourcePart(14695981039346656037ull, withBom), withExtra));
            assert(extraCook.NvskelBytes.size() == externalCook.NvskelBytes.size());
            assert(std::memcmp(extraCook.NvskelBytes.data(), externalCook.NvskelBytes.data(), extraCook.NvskelBytes.size()) == 0);
            assert(Skeletal::DecodeSkeletalGltf(CoreText(text), fixture.Path(), &legacySources).Succeeded());
            assert(legacySources[0].size() == withExtra.size() && legacySources[0].back() == 77);

            ByteArray invalidGlb = glb;
            invalidGlb[8] ^= 1;
            const ByteArray missingBin = TextBytes(withoutUri);
            const ByteArray shortData = TextBytes(ChangeBufferUri(text, "\"uri\":\"data:application/octet-stream;base64,AA==\","));
            const ByteArray* invalidSources[] = {&invalidGlb, &missingBin, &shortData};
            for (const ByteArray* invalid : invalidSources)
            {
                assert(Skeletal::DecodeSkeletalGltf(glb, fixture.Path(), &sources).Succeeded() && sources.GetCount() == 1);
                assert(!Skeletal::DecodeSkeletalGltf(*invalid, fixture.Path(), &sources).Succeeded() && sources.GetCount() == 0);
            }
            assert(!Skeletal::DecodeSkeletalGltf(Container::Span<const uint8_t>{}, fixture.Path(), &sources).Succeeded());
            const auto previousHash = extraCook.SourceHash;
            assert(!cook(invalidGlb, extraCook) && extraCook.SourceHash == previousHash);
            assert(extraCook.NvskelBytes.size() == externalCook.NvskelBytes.size());
            assert(std::memcmp(extraCook.NvskelBytes.data(), externalCook.NvskelBytes.data(), extraCook.NvskelBytes.size()) == 0);
        }

        assert(Asset::ParseCookedSkeletal(Asset::AssetBlob::Invalid()).Status ==
               Asset::CookedSkeletalParseStatus::InvalidBlob);
        const ByteArray empty;
        assert(Asset::ParseCookedSkeletal(MakeBlob(empty)).Status == Asset::CookedSkeletalParseStatus::EmptyBlob);
        const ByteArray shortHeader(GoldenWire::HeaderSize - 1, 0);
        assert(Asset::ParseCookedSkeletal(MakeBlob(shortHeader)).Status ==
               Asset::CookedSkeletalParseStatus::HeaderTooSmall);

        {
            ByteArray bytes = goldenBytes;
            bytes[0] = 'X';
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::BadMagic);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe16(bytes, 12, 1);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::UnsupportedVersion);
        }
        {
            ByteArray bytes = goldenBytes;
            bytes[GoldenWire::VertexOffset] ^= 1u;
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::PayloadHashMismatch);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe64(bytes, 48, static_cast<uint64_t>(GoldenWire::FileSize + 16));
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::SectionOutOfRange);
        }
        {
            ByteArray bytes = goldenBytes;
            bytes[GoldenWire::IndexOffset + GoldenWire::IndexSize] = 1;
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            bytes.pop_back();
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::FileSizeMismatch);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, GoldenWire::VertexOffset, 0x7fc00000u);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteFloat(bytes, GoldenWire::VertexOffset + 48, -0.25f);
            WriteFloat(bytes, GoldenWire::VertexOffset + 52, 1.25f);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, GoldenWire::JointOffset + 0, 1);
            WriteLe32(bytes, GoldenWire::JointOffset + GoldenWire::JointSize / 2, 0);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, GoldenWire::JointOffset + GoldenWire::JointSize / 2, 0xffffffffu);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, GoldenWire::ChannelOffset + GoldenWire::ChannelSize / 2 + 0, 1);
            WriteLe32(bytes, GoldenWire::ChannelOffset + GoldenWire::ChannelSize / 2 + 4, 0);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteFloat(bytes, GoldenWire::SampleOffset + 32, 0.0f);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteFloat(bytes, GoldenWire::ClipOffset + 12, 3.0f);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, GoldenWire::ChannelOffset + GoldenWire::ChannelSize / 2 + 12, 1);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteLe32(bytes, 192, 0x7fc00000u);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
        {
            ByteArray bytes = goldenBytes;
            WriteFloat(bytes, 192, 0.0f);
            RecomputeSkeletalHash(bytes);
            ExpectStatus(std::move(bytes), Asset::CookedSkeletalParseStatus::InvalidRecord);
        }
    }

    void RunAssetCookPackageContract(const char* packagePath, const char* gltfPath, const char* entryName)
    {
        FileStream::Package package;
        assert(package.Load(ToCorePath(std::filesystem::path(packagePath))));
        assert(package.IsLoaded());
        assert(package.GetFormat() == FileStream::PackageFormat::V1);
        assert(package.GetEntryCount() == 1);

        FileStream::PackageEntry entry;
        assert(package.FindEntry(Container::AnsiString(entryName), Asset::CookedSkeletalFormatV0::EntryType, entry));
        assert(entry.Name == Container::AnsiString(entryName));
        assert(entry.Type == Asset::MakeAssetPackageFourCC('S', 'k', 'l', '0'));

        Asset::AssetBlob payload = package.OpenEntry(entry);
        assert(payload.IsValid());
        assert(entry.PayloadHash == Asset::ComputeAssetPackagePayloadHash(payload.GetData(), payload.GetSize()));
        const Asset::CookedSkeletalParseResult cooked = Asset::ParseCookedSkeletal(payload);
        assert(cooked.Succeeded());
        assert(cooked.Status == Asset::CookedSkeletalParseStatus::Success);
        payload = Asset::AssetBlob::Invalid();
        package.Unload();
        AssertLiteralCookedData(cooked.Data);

        const Skeletal::SkeletalGltfDecodeResult loose =
            ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(std::filesystem::path(gltfPath)));
        assert(loose.Succeeded());
        AssertEquivalent(loose.Data, cooked.Data.Skeletal);
    }
} // namespace

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "CookedSkeletalAssetTest start\n";
    if (argc == 1)
    {
        RunInfluenceReductionContract();
        RunCubicBakeContract();
        RunMorphDropContract();
        RunMultiPrimitiveContract();
        RunMultiClipContract();
        RunUnitContract();
        RunV02ReaderContract();
    }
    else
    {
        assert(argc == 7);
        assert(std::strcmp(argv[1], "--package") == 0);
        assert(std::strcmp(argv[3], "--gltf") == 0);
        assert(std::strcmp(argv[5], "--entry") == 0);
        RunAssetCookPackageContract(argv[2], argv[4], argv[6]);
    }

    std::cout << "CookedSkeletalAssetTest passed\n";
    return 0;
}
