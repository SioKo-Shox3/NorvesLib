#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "FileStream/FileStream.h"
#include "FileStream/Package.h"
#include "Rendering/VertexLayout.h"
#include "Resource/GLTFAnalyzer.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/GltfBufferSet.h"
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
                return NorvesLib::Tools::AssetCook::CookGltfToNvskel(source.data(), source.size(),
                    "nvskel.v0.skinned.pnujiw.u32", sourcePath, out, error);
            };
            SkeletalCookResult externalCook;
            assert(cook(external, externalCook));
            assert(externalCook.SourceHash == HashSourcePart(HashSourcePart(14695981039346656037ull, external), binary));
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
