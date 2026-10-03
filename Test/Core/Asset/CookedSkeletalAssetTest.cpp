#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "FileStream/FileStream.h"
#include "FileStream/Package.h"
#include "Rendering/VertexLayout.h"
#include "Resource/GLTFAnalyzer.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/GltfBufferSet.h"
#include "Resource/ImportSettingsFile.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Resource/SkinnedMeshResource.h"
#include <algorithm>
#include "Tools/AssetCook/MeshCooker.h"
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
namespace Gltf = NorvesLib::Core::Resource;
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
            : Asset::ComputeCookedSkeletalV01Hash(bytes.data() + 192, bytes.data() + 256, bytes.size() - 256);
        WriteLe64(bytes, 160, hash);
    }

    Asset::AssetBlob MakeBlob(const ByteArray& bytes)
    {
        return Asset::AssetBlob::CopyBytes(Container::Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvskel");
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
        assert(loose.Clips[0].Name == cooked.Clips[0].Name);
        assert(loose.Clips[0].DurationSeconds == cooked.Clips[0].DurationSeconds);
        assert(loose.Clips[0].Channels.size() == cooked.Clips[0].Channels.size());
        for (size_t channelIndex = 0; channelIndex < loose.Clips[0].Channels.size(); ++channelIndex)
        {
            const Skeletal::SkeletalAnimationChannel& a = loose.Clips[0].Channels[channelIndex];
            const Skeletal::SkeletalAnimationChannel& b = cooked.Clips[0].Channels[channelIndex];
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
        const auto near = [](float a, float b) { return std::abs(a - b) < 1e-4f; };
        assert(before.Vertices.size() == after.Vertices.size() && before.Joints.size() == after.Joints.size());
        assert(before.Indices == after.Indices && before.Clips.size() == after.Clips.size());
        for (size_t index = 0; index < before.Vertices.size(); ++index)
        {
            const auto& a = before.Vertices[index];
            const auto& b = after.Vertices[index];
            assert(near(b.Position.X, a.Position.X * scale) && near(b.Position.Y, a.Position.Y * scale) &&
                near(b.Position.Z, a.Position.Z * scale));
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
                    assert(near(b.InverseBindMatrix[element], a.InverseBindMatrix[element] * scale));
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
                assert(near(after.MeshNodeGlobalTransform[element], before.MeshNodeGlobalTransform[element] * scale));
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
                    assert(near(y.X,x.X*factor) && near(y.Y,x.Y*factor) && near(y.Z,x.Z*factor) && y.W == x.W);
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
        const auto file = Resource::GLTFAnalyzer::AnalyzeSkeletal(sourcePath, &options);
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
        auto cookWith = [&](const ByteArray& bytes, const SkeletalGltfDecodeOptions* policy, SkeletalCookResult& out)
        {
            return CookGltfToNvskel(bytes.data(), bytes.size(), format, cookPath, out, cookError, nullptr, policy);
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
        assert(cookError.find("failed_vertex=0") != Container::AnsiString::npos &&
            cookError.find("dropped_weight=") != Container::AnsiString::npos);
        assert(cooked.SourceHash == savedCook.SourceHash && cooked.NvskelBytes == savedCook.NvskelBytes);
        checkReport(cooked.DecodeReport);
        auto badPolicy = options; badPolicy.FailDroppedWeight = -1;
        assert(!cookWith(sourceBytes, &badPolicy, cooked) && !fingerprintWith(sourceBytes, &badPolicy, fingerprint));
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
        assert(Resource::GLTFAnalyzer::AnalyzeSkeletal(sourcePath, &limited).Status == failed.Status);
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
            const Skeletal::SkeletalGltfDecodeResult loose = Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path());
            assert(loose.Succeeded());
            AssertEquivalent(loose.Data, retainedResult.Data.Skeletal);
            const Container::AnsiString text = ReadFixtureJson(fixture.Path());
            const ByteArray external = TextBytes(text);
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
            const auto looseGlb = Gltf::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(glbPath));
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
            const auto scaledLoose = Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path());
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
            const auto scaledGlb=Gltf::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(glbPath));
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
            Gltf::GLTFAnalyzer::AnalyzeSkeletal(ToCorePath(std::filesystem::path(gltfPath)));
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
        RunUnitContract();
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
