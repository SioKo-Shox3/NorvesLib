#include "FileStream/FileStream.h"
#include "Resource/GLTFAnalyzer.h"
#include "Resource/SkeletalGltfData.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

namespace Container = NorvesLib::Core::Container;
namespace Gltf = NorvesLib::Core::Resource;
namespace Skeletal = NorvesLib::Core::Skeletal;
namespace FileStream = NorvesLib::FileStream;

namespace
{
    using ByteArray = Container::VariableArray<uint8_t>;

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

    void WriteFloat(ByteArray& bytes, size_t offset, float value)
    {
        WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
    }

    ByteArray BuildFixtureBuffer()
    {
        ByteArray bytes(416, 0);

        constexpr float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        constexpr float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr float texCoords[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        constexpr uint8_t jointsU8[12] = {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
        constexpr uint16_t jointsU16[12] = {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
        constexpr float weightsFloat[12] = {
            0.75f, 0.25f, 0.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f};
        constexpr uint8_t weightsU8[12] = {191, 64, 0, 0, 128, 127, 0, 0, 255, 0, 0, 0};
        constexpr uint16_t weightsU16[12] = {49151, 16384, 0, 0, 32768, 32767, 0, 0, 65535, 0, 0, 0};
        constexpr uint16_t indices[3] = {0, 1, 2};

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
            bytes[96 + index] = jointsU8[index];
            WriteLe16(bytes, 108 + index * sizeof(uint16_t), jointsU16[index]);
            WriteFloat(bytes, 132 + index * sizeof(float), weightsFloat[index]);
            bytes[180 + index] = weightsU8[index];
            WriteLe16(bytes, 192 + index * sizeof(uint16_t), weightsU16[index]);
        }
        for (size_t index = 0; index < 3; ++index)
        {
            WriteLe16(bytes, 216 + index * sizeof(uint16_t), indices[index]);
        }

        WriteFloat(bytes, 224 + 0 * sizeof(float), 1.0f);
        WriteFloat(bytes, 224 + 5 * sizeof(float), 1.0f);
        WriteFloat(bytes, 224 + 10 * sizeof(float), 1.0f);
        WriteFloat(bytes, 224 + 15 * sizeof(float), 1.0f);
        WriteFloat(bytes, 288 + 0 * sizeof(float), 1.0f);
        WriteFloat(bytes, 288 + 5 * sizeof(float), 1.0f);
        WriteFloat(bytes, 288 + 10 * sizeof(float), 1.0f);
        WriteFloat(bytes, 288 + 13 * sizeof(float), -1.0f);
        WriteFloat(bytes, 288 + 15 * sizeof(float), 1.0f);

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

    ByteArray BuildCubicSplineBuffer()
    {
        const ByteArray base = BuildFixtureBuffer();
        ByteArray bytes(488, 0);
        for (size_t index = 0; index < base.size(); ++index)
        {
            bytes[index] = base[index];
        }

        constexpr float cubicTranslations[18] = {
            0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f,
            0.0f, 3.0f, 0.0f,
            0.0f, 0.0f, 0.0f};
        for (size_t index = 0; index < 18; ++index)
        {
            WriteFloat(bytes, 416 + index * sizeof(float), cubicTranslations[index]);
        }
        return bytes;
    }

    ByteArray BuildOutOfRangeJointBuffer()
    {
        ByteArray bytes = BuildFixtureBuffer();
        bytes[96] = 2;
        return bytes;
    }

    ByteArray BuildNonFinitePositionBuffer()
    {
        ByteArray bytes = BuildFixtureBuffer();
        WriteLe32(bytes, 0, 0x7fc00000u);
        return bytes;
    }

    ByteArray BuildNegativeWeightBuffer()
    {
        ByteArray bytes = BuildFixtureBuffer();
        WriteFloat(bytes, 132, -0.25f);
        WriteFloat(bytes, 136, 1.25f);
        return bytes;
    }

    ByteArray BuildInvalidU16WeightSumBuffer()
    {
        ByteArray bytes = BuildFixtureBuffer();
        WriteLe16(bytes, 192, 49150);
        return bytes;
    }

    ByteArray BuildNonIncreasingTimeBuffer()
    {
        ByteArray bytes = BuildFixtureBuffer();
        WriteFloat(bytes, 356, 0.0f);
        return bytes;
    }

    ByteArray BuildTooManyJointsBuffer()
    {
        const ByteArray base = BuildFixtureBuffer();
        ByteArray bytes(8544, 0);
        for (size_t index = 0; index < 224; ++index)
        {
            bytes[index] = base[index];
        }
        for (size_t jointIndex = 0; jointIndex < 129; ++jointIndex)
        {
            const size_t matrixOffset = 224 + jointIndex * 16 * sizeof(float);
            WriteFloat(bytes, matrixOffset + 0 * sizeof(float), 1.0f);
            WriteFloat(bytes, matrixOffset + 5 * sizeof(float), 1.0f);
            WriteFloat(bytes, matrixOffset + 10 * sizeof(float), 1.0f);
            WriteFloat(bytes, matrixOffset + 15 * sizeof(float), 1.0f);
        }

        WriteFloat(bytes, 8480, 0.0f);
        WriteFloat(bytes, 8484, 2.0f);
        constexpr float translations[6] = {0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f};
        constexpr float rotations[8] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (size_t index = 0; index < 6; ++index)
        {
            WriteFloat(bytes, 8488 + index * sizeof(float), translations[index]);
        }
        for (size_t index = 0; index < 8; ++index)
        {
            WriteFloat(bytes, 8512 + index * sizeof(float), rotations[index]);
        }
        return bytes;
    }

    Container::String ReadText(const std::filesystem::path& path)
    {
        auto stream = FileStream::FileStream::Create(
            ToCorePath(path), FileStream::FileMode::Read, FileStream::FileAccess::Read, FileStream::FileShare::Read);
        assert(stream && stream->IsOpen());
        Container::String text = stream->ReadString();
        stream->Close();
        return text;
    }

    void WriteText(const std::filesystem::path& path, const Container::String& text)
    {
        auto stream = FileStream::FileStream::Create(
            ToCorePath(path), FileStream::FileMode::Write, FileStream::FileAccess::Write, FileStream::FileShare::None);
        assert(stream && stream->IsOpen());
        assert(stream->WriteString(text) == text.size());
        stream->Close();
    }

    void WriteBytes(const std::filesystem::path& path, const ByteArray& bytes)
    {
        auto stream = FileStream::FileStream::Create(
            ToCorePath(path), FileStream::FileMode::Write, FileStream::FileAccess::Write, FileStream::FileShare::None);
        assert(stream && stream->IsOpen());
        assert(stream->Write(bytes.data(), bytes.size()) == bytes.size());
        stream->Close();
    }

    void ReplaceRange(Container::String& text,
                      size_t position,
                      size_t count,
                      const Container::String& replacement)
    {
        text = text.substr(0, position) + replacement + text.substr(position + count);
    }

    void ReplaceOnce(Container::String& text, const char* from, const Container::String& to)
    {
        const size_t position = text.find(from);
        assert(position != Container::String::npos);
        ReplaceRange(text, position, Container::String(from).size(), to);
    }

    void ReplaceAfter(Container::String& text,
                      const char* anchor,
                      const char* from,
                      const Container::String& to)
    {
        const size_t anchorPosition = text.find(anchor);
        assert(anchorPosition != Container::String::npos);
        const size_t position = text.find(from, anchorPosition);
        assert(position != Container::String::npos);
        ReplaceRange(text, position, Container::String(from).size(), to);
    }

    void ReplaceSecondSection(Container::String& text,
                              const char* beginMarker,
                              const char* endMarker,
                              const Container::String& replacement)
    {
        const size_t first = text.find(beginMarker);
        assert(first != Container::String::npos);
        const size_t begin = text.find(beginMarker, first + Container::String(beginMarker).size());
        assert(begin != Container::String::npos);
        const size_t end = text.find(endMarker, begin);
        assert(end != Container::String::npos);
        ReplaceRange(text, begin, end - begin, replacement);
    }

    void ReplaceArrayAfter(Container::String& text,
                           const char* anchor,
                           const char* arrayField,
                           const Container::String& replacement)
    {
        const size_t anchorPosition = text.find(anchor);
        assert(anchorPosition != Container::String::npos);
        const size_t fieldPosition = text.find(arrayField, anchorPosition);
        assert(fieldPosition != Container::String::npos);
        const size_t closeBracket = text.find(']', fieldPosition);
        assert(closeBracket != Container::String::npos);
        ReplaceRange(text, fieldPosition, closeBracket + 1 - fieldPosition, replacement);
    }

    void AppendUInt(Container::String& text, uint32_t value)
    {
        char reversed[10] = {};
        size_t digitCount = 0;
        do
        {
            reversed[digitCount++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (digitCount > 0)
        {
            text += reversed[--digitCount];
        }
    }

    Container::String BuildTooManyNodesJson()
    {
        Container::String nodes = "\"nodes\":[{\"name\":\"Joint0\",\"children\":[";
        for (uint32_t nodeIndex = 1; nodeIndex < 129; ++nodeIndex)
        {
            if (nodeIndex != 1)
            {
                nodes += ',';
            }
            AppendUInt(nodes, nodeIndex);
        }
        nodes += "]}";
        for (uint32_t jointIndex = 1; jointIndex < 129; ++jointIndex)
        {
            nodes += ",{\"name\":\"Joint";
            AppendUInt(nodes, jointIndex);
            nodes += '"';
            if (jointIndex == 1)
            {
                nodes += ",\"translation\":[0,1,0]";
            }
            nodes += '}';
        }
        nodes += ",{\"name\":\"Mesh\",\"mesh\":0,\"skin\":0}],\r\n  ";
        return nodes;
    }

    Container::String BuildTooManyJointArray()
    {
        Container::String joints = "\"joints\":[";
        for (uint32_t jointIndex = 0; jointIndex < 129; ++jointIndex)
        {
            if (jointIndex != 0)
            {
                joints += ',';
            }
            AppendUInt(joints, jointIndex);
        }
        joints += ']';
        return joints;
    }

    class SkeletalFixtureDirectory final
    {
    public:
        SkeletalFixtureDirectory()
        {
            Root = std::filesystem::temp_directory_path() / "NorvesLibM9SkeletalAnalyzer";
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root);

            const std::filesystem::path sourceRoot = FindFixtureRoot();
            Copy(sourceRoot, "ValidU8Float.gltf");
            Copy(sourceRoot, "ValidU16Unorm8.gltf");
            Copy(sourceRoot, "ValidU8Unorm16.gltf");
            WriteBytes(Root / "fixture.bin", BuildFixtureBuffer());

            const Container::String valid = ReadText(Root / "ValidU8Float.gltf");
            const Container::String validU16Weights = ReadText(Root / "ValidU8Unorm16.gltf");

            Container::String malformed = valid;
            ReplaceAfter(malformed, "\"bufferView\": 3", "\"count\": 3", "\"count\": 4");
            WriteText(Root / "MalformedAccessor.gltf", malformed);

            Container::String fractionalAccessorCount = valid;
            ReplaceAfter(fractionalAccessorCount,
                         "\"bufferView\": 3",
                         "\"count\": 3",
                         "\"count\": 3.5");
            WriteText(Root / "FractionalAccessorCount.gltf", fractionalAccessorCount);

            Container::String sparseAccessor = valid;
            ReplaceAfter(sparseAccessor,
                         "\"bufferView\": 0",
                         "\"count\": 3",
                         "\"count\": 3, \"sparse\": {\"count\": 1, \"indices\": {\"bufferView\": 3, "
                         "\"componentType\": 5121}, \"values\": {\"bufferView\": 0}}");
            WriteText(Root / "SparseAccessor.gltf", sparseAccessor);

            Container::String outOfRangeJoint = valid;
            ReplaceOnce(outOfRangeJoint, "\"uri\": \"fixture.bin\"", "\"uri\": \"out_of_range_joint.bin\"");
            WriteText(Root / "OutOfRangeJoint.gltf", outOfRangeJoint);
            WriteBytes(Root / "out_of_range_joint.bin", BuildOutOfRangeJointBuffer());

            Container::String twoSkins = valid;
            ReplaceOnce(twoSkins,
                        "\"skins\": [",
                        "\"skins\": [{\"name\":\"Extra\",\"inverseBindMatrices\":9,"
                        "\"skeleton\":0,\"joints\":[0,1]},");
            WriteText(Root / "TwoSkins.gltf", twoSkins);

            Container::String twoPrimitives = valid;
            ReplaceOnce(twoPrimitives,
                        "\"primitives\": [",
                        "\"primitives\": [{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
                        "\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":5},\"indices\":8,\"mode\":4},");
            WriteText(Root / "TwoPrimitives.gltf", twoPrimitives);

            Container::String twoClips = valid;
            ReplaceOnce(twoClips,
                        "\"animations\": [",
                        "\"animations\": [{\"name\":\"Extra\",\"samplers\":["
                        "{\"input\":10,\"output\":11,\"interpolation\":\"LINEAR\"},"
                        "{\"input\":10,\"output\":12,\"interpolation\":\"STEP\"}],"
                        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
                        "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"rotation\"}}]},");
            WriteText(Root / "TwoClips.gltf", twoClips);

            Container::String cubicSpline = valid;
            ReplaceOnce(cubicSpline, "\"uri\": \"fixture.bin\"", "\"uri\": \"cubic.bin\"");
            ReplaceOnce(cubicSpline, "\"byteLength\": 416", "\"byteLength\": 488");
            ReplaceAfter(cubicSpline, "\"byteOffset\": 360", "\"byteLength\": 24", "\"byteLength\": 72");
            ReplaceOnce(cubicSpline, "\"byteOffset\": 360", "\"byteOffset\": 416");
            ReplaceAfter(cubicSpline, "\"bufferView\": 11", "\"count\": 2", "\"count\": 6");
            ReplaceOnce(cubicSpline,
                        "\"interpolation\": \"LINEAR\"",
                        "\"interpolation\": \"CUBICSPLINE\"");
            WriteText(Root / "CubicSpline.gltf", cubicSpline);
            WriteBytes(Root / "cubic.bin", BuildCubicSplineBuffer());

            Container::String morphTarget = valid;
            ReplaceOnce(morphTarget,
                        "\"attributes\": {",
                        "\"targets\":[{\"POSITION\":0}],\"attributes\":{");
            WriteText(Root / "MorphTarget.gltf", morphTarget);

            Container::String tooManyJoints = valid;
            ReplaceOnce(tooManyJoints, "\"uri\": \"fixture.bin\"", "\"uri\": \"too_many.bin\"");
            ReplaceOnce(tooManyJoints, "\"byteLength\": 416", "\"byteLength\": 8544");
            ReplaceAfter(tooManyJoints, "\"byteOffset\": 224", "\"byteLength\": 128", "\"byteLength\": 8256");
            ReplaceOnce(tooManyJoints, "\"byteOffset\": 352", "\"byteOffset\": 8480");
            ReplaceOnce(tooManyJoints, "\"byteOffset\": 360", "\"byteOffset\": 8488");
            ReplaceOnce(tooManyJoints, "\"byteOffset\": 384", "\"byteOffset\": 8512");
            ReplaceAfter(tooManyJoints, "\"bufferView\": 9", "\"count\": 2", "\"count\": 129");
            ReplaceSecondSection(tooManyJoints, "\"nodes\": [", "\"buffers\": [", BuildTooManyNodesJson());
            ReplaceArrayAfter(tooManyJoints, "\"scenes\": [", "\"nodes\": [", "\"nodes\":[0,129]");
            ReplaceArrayAfter(tooManyJoints, "\"skins\": [", "\"joints\": [", BuildTooManyJointArray());
            WriteText(Root / "TooManyJoints.gltf", tooManyJoints);
            WriteBytes(Root / "too_many.bin", BuildTooManyJointsBuffer());

            Container::String disconnectedJointForest = valid;
            ReplaceArrayAfter(disconnectedJointForest,
                              "\"name\": \"Root\"",
                              "\"children\": [",
                              "\"children\":[]");
            ReplaceAfter(disconnectedJointForest, "\"scenes\": [", "2", "2,\r\n        1");
            WriteText(Root / "DisconnectedJointForest.gltf", disconnectedJointForest);

            Container::String meshOutsideScene = valid;
            ReplaceAfter(meshOutsideScene, "\"scenes\": [", "2", "1");
            WriteText(Root / "MeshOutsideScene.gltf", meshOutsideScene);

            Container::String wrongSkinBinding = valid;
            ReplaceAfter(wrongSkinBinding, "\"name\": \"Mesh\"", "\"skin\": 0", "\"skin\": 1");
            WriteText(Root / "WrongSkinBinding.gltf", wrongSkinBinding);

            Container::String missingMeshBinding = valid;
            ReplaceAfter(missingMeshBinding, "\"name\": \"Mesh\"", "\"mesh\": 0", "\"mesh_missing\": 0");
            WriteText(Root / "MissingMeshBinding.gltf", missingMeshBinding);

            Container::String missingSkinBinding = valid;
            ReplaceAfter(missingSkinBinding, "\"name\": \"Mesh\"", "\"skin\": 0", "\"skin_missing\": 0");
            WriteText(Root / "MissingSkinBinding.gltf", missingSkinBinding);

            Container::String ambiguousBinding = valid;
            ReplaceAfter(ambiguousBinding, "\"scenes\": [", "2", "2,\r\n        3");
            ReplaceOnce(ambiguousBinding,
                        "\"skin\": 0\r\n    }\r\n  ],\r\n  \"buffers\"",
                        "\"skin\": 0\r\n    },\r\n    {\"name\":\"DuplicateMesh\",\"mesh\":0,\"skin\":0}\r\n  ],\r\n  \"buffers\"");
            WriteText(Root / "AmbiguousBinding.gltf", ambiguousBinding);

            Container::String singularMeshTransform = valid;
            ReplaceAfter(singularMeshTransform,
                         "\"name\": \"Mesh\"",
                         "\"mesh\": 0",
                         "\"scale\": [0, 1, 1], \"mesh\": 0");
            WriteText(Root / "SingularMeshTransform.gltf", singularMeshTransform);

            Container::String intermediateNode = valid;
            ReplaceAfter(intermediateNode, "\"name\": \"Root\"", "1", "2");
            ReplaceAfter(intermediateNode, "\"scenes\": [", "2", "3");
            ReplaceOnce(intermediateNode,
                        "\"name\": \"Mesh\",",
                        "\"name\": \"Intermediate\", \"children\": [1]\r\n    },\r\n    {\r\n      \"name\": \"Mesh\",");
            WriteText(Root / "IntermediateNode.gltf", intermediateNode);

            Container::String nodeCycle = valid;
            ReplaceOnce(nodeCycle,
                        "\"translation\": [\r\n        0,\r\n        1,\r\n        0\r\n      ]",
                        "\"translation\": [0, 1, 0], \"children\": [0]");
            WriteText(Root / "NodeCycle.gltf", nodeCycle);

            Container::String duplicateChannel = valid;
            ReplaceAfter(duplicateChannel,
                         "\"interpolation\": \"STEP\"",
                         "\"node\": 0,\r\n            \"path\": \"rotation\"",
                         "\"node\": 1,\r\n            \"path\": \"translation\"");
            WriteText(Root / "DuplicateChannel.gltf", duplicateChannel);

            Container::String nonTriangleIndices = valid;
            ReplaceAfter(nonTriangleIndices, "\"bufferView\": 8", "\"count\": 3", "\"count\": 2");
            WriteText(Root / "NonTriangleIndices.gltf", nonTriangleIndices);

            Container::String normalizedFloat = valid;
            ReplaceAfter(normalizedFloat,
                         "\"bufferView\": 0",
                         "\"componentType\": 5126,",
                         "\"componentType\": 5126, \"normalized\": true,");
            WriteText(Root / "NormalizedFloat.gltf", normalizedFloat);

            Container::String invalidStride = valid;
            ReplaceAfter(invalidStride,
                         "\"buffer\": 0,",
                         "\"byteOffset\": 0,",
                         "\"byteOffset\": 0, \"byteStride\": 14,");
            WriteText(Root / "InvalidStride.gltf", invalidStride);

            Container::String misalignedU8Accessor = valid;
            ReplaceAfter(misalignedU8Accessor, "\"byteOffset\": 96", "\"byteOffset\": 96", "\"byteOffset\": 95");
            ReplaceAfter(misalignedU8Accessor, "\"byteOffset\": 95", "\"byteLength\": 12", "\"byteLength\": 13");
            ReplaceAfter(misalignedU8Accessor,
                         "\"accessors\": [",
                         "\"bufferView\": 3,",
                         "\"bufferView\": 3, \"byteOffset\": 1,");
            WriteText(Root / "MisalignedU8Accessor.gltf", misalignedU8Accessor);

            Container::String misalignedU16Accessor = validU16Weights;
            ReplaceAfter(misalignedU16Accessor, "\"byteOffset\": 192", "\"byteOffset\": 192", "\"byteOffset\": 190");
            ReplaceAfter(misalignedU16Accessor, "\"byteOffset\": 190", "\"byteLength\": 24", "\"byteLength\": 26");
            ReplaceAfter(misalignedU16Accessor,
                         "\"accessors\": [",
                         "\"bufferView\": 7,",
                         "\"bufferView\": 7, \"byteOffset\": 2,");
            WriteText(Root / "MisalignedU16Accessor.gltf", misalignedU16Accessor);

            Container::String nonFinitePosition = valid;
            ReplaceOnce(nonFinitePosition, "\"uri\": \"fixture.bin\"", "\"uri\": \"non_finite.bin\"");
            WriteText(Root / "NonFinitePosition.gltf", nonFinitePosition);
            WriteBytes(Root / "non_finite.bin", BuildNonFinitePositionBuffer());

            Container::String negativeWeight = valid;
            ReplaceOnce(negativeWeight, "\"uri\": \"fixture.bin\"", "\"uri\": \"negative_weight.bin\"");
            WriteText(Root / "NegativeWeight.gltf", negativeWeight);
            WriteBytes(Root / "negative_weight.bin", BuildNegativeWeightBuffer());

            Container::String invalidU16WeightSum = validU16Weights;
            ReplaceOnce(invalidU16WeightSum, "\"uri\": \"fixture.bin\"", "\"uri\": \"invalid_u16_weight_sum.bin\"");
            WriteText(Root / "InvalidU16WeightSum.gltf", invalidU16WeightSum);
            WriteBytes(Root / "invalid_u16_weight_sum.bin", BuildInvalidU16WeightSumBuffer());

            Container::String nonIncreasingTime = valid;
            ReplaceOnce(nonIncreasingTime, "\"uri\": \"fixture.bin\"", "\"uri\": \"bad_time.bin\"");
            WriteText(Root / "NonIncreasingTime.gltf", nonIncreasingTime);
            WriteBytes(Root / "bad_time.bin", BuildNonIncreasingTimeBuffer());
        }

        ~SkeletalFixtureDirectory()
        {
            std::filesystem::remove_all(Root);
        }

        Container::String Path(const char* name) const
        {
            return ToCorePath(Root / name);
        }

    private:
        void Copy(const std::filesystem::path& sourceRoot, const char* name)
        {
            assert(std::filesystem::copy_file(sourceRoot / name, Root / name));
        }

    public:
        std::filesystem::path Root;
    };

    void AssertNear(float actual, float expected)
    {
        assert(std::fabs(actual - expected) <= 0.000001f);
    }

    void AssertCanonicalTriangle(const Skeletal::SkeletalGltfData& data)
    {
        assert(data.Vertices.size() == 3);
        assert(data.Indices.size() == 3);
        assert(data.Indices[0] == 0);
        assert(data.Indices[1] == 2);
        assert(data.Indices[2] == 1);
        assert(data.Vertices[1].Position.X == 1.0f);
        assert(data.Vertices[2].Position.Y == 1.0f);
        assert(data.Vertices[0].Normal.Z == 1.0f);
        assert(data.Vertices[2].TexCoord.V == 1.0f);
        assert(data.Vertices[0].JointIndices[0] == 0);
        assert(data.Vertices[0].JointIndices[1] == 1);
        assert(data.Vertices[2].JointIndices[0] == 1);

        assert(data.Joints.size() == 2);
        assert(data.Joints[0].Name == "Root");
        assert(data.Joints[0].ParentIndex == -1);
        assert(data.Joints[1].Name == "Child");
        assert(data.Joints[1].ParentIndex == 0);
        assert(data.Joints[0].InverseBindMatrix[0] == 1.0f);
        assert(data.Joints[0].InverseBindMatrix[15] == 1.0f);
        assert(data.Joints[1].InverseBindMatrix[13] == -1.0f);

        assert(data.Clips.size() == 1);
        assert(data.Clips[0].Name == "Wave");
        assert(data.Clips[0].DurationSeconds == 2.0f);
        assert(data.Clips[0].Channels.size() == 2);

        const Skeletal::SkeletalAnimationChannel& translation = data.Clips[0].Channels[0];
        assert(translation.JointIndex == 1);
        assert(translation.Path == Skeletal::SkeletalAnimationPath::Translation);
        assert(translation.Interpolation == Skeletal::SkeletalAnimationInterpolation::Linear);
        assert(translation.Samples.size() == 2);
        assert(translation.Samples[0].TimeSeconds == 0.0f);
        assert(translation.Samples[0].Value.Y == 1.0f);
        assert(translation.Samples[1].TimeSeconds == 2.0f);
        assert(translation.Samples[1].Value.Y == 3.0f);

        const Skeletal::SkeletalAnimationChannel& rotation = data.Clips[0].Channels[1];
        assert(rotation.JointIndex == 0);
        assert(rotation.Path == Skeletal::SkeletalAnimationPath::Rotation);
        assert(rotation.Interpolation == Skeletal::SkeletalAnimationInterpolation::Step);
        assert(rotation.Samples.size() == 2);
        assert(rotation.Samples[0].Value.W == 1.0f);
        assert(rotation.Samples[1].Value.Z == 1.0f);
        assert(rotation.Samples[1].Value.W == 0.0f);
    }

    void ExpectReject(const SkeletalFixtureDirectory& fixture,
                      const char* name,
                      Skeletal::SkeletalGltfDecodeStatus expectedStatus)
    {
        const Skeletal::SkeletalGltfDecodeResult result = Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path(name));
        assert(!result.Succeeded());
        assert(result.Status == expectedStatus);
        assert(result.Data.Vertices.empty());
        assert(result.Data.Indices.empty());
        assert(result.Data.Joints.empty());
        assert(result.Data.Clips.empty());
    }
} // namespace

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "GLTFSkeletalAnalyzerTest start\n";
    SkeletalFixtureDirectory fixture;

    {
        const Skeletal::SkeletalGltfDecodeResult result =
            Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("ValidU8Float.gltf"));
        assert(result.Succeeded());
        assert(result.Status == Skeletal::SkeletalGltfDecodeStatus::Success);
        AssertCanonicalTriangle(result.Data);
        AssertNear(result.Data.Vertices[0].JointWeights[0], 0.75f);
        AssertNear(result.Data.Vertices[0].JointWeights[1], 0.25f);
        AssertNear(result.Data.Vertices[1].JointWeights[0], 0.5f);
        AssertNear(result.Data.Vertices[1].JointWeights[1], 0.5f);
        assert(result.Data.MeshNodeGlobalTransform[12] == 5.0f);
        std::cout << "fixture ValidU8Float.gltf passed\n";
    }

    {
        const Skeletal::SkeletalGltfDecodeResult result =
            Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("ValidU16Unorm8.gltf"));
        assert(result.Succeeded());
        AssertCanonicalTriangle(result.Data);
        AssertNear(result.Data.Vertices[0].JointWeights[0], 0.749019608f);
        AssertNear(result.Data.Vertices[0].JointWeights[1], 0.250980392f);
        AssertNear(result.Data.Vertices[1].JointWeights[0], 0.501960784f);
        AssertNear(result.Data.Vertices[1].JointWeights[1], 0.498039216f);
        std::cout << "fixture ValidU16Unorm8.gltf passed\n";
    }

    {
        const Skeletal::SkeletalGltfDecodeResult result =
            Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("ValidU8Unorm16.gltf"));
        assert(result.Succeeded());
        AssertCanonicalTriangle(result.Data);
        AssertNear(result.Data.Vertices[0].JointWeights[0], 0.749996185f);
        AssertNear(result.Data.Vertices[0].JointWeights[1], 0.250003815f);
        AssertNear(result.Data.Vertices[1].JointWeights[0], 0.500007630f);
        AssertNear(result.Data.Vertices[1].JointWeights[1], 0.499992370f);
        std::cout << "fixture ValidU8Unorm16.gltf passed\n";
    }

    // These mutations respectively remove bounds validation and each v0 feature gate.
    const Skeletal::SkeletalGltfDecodeResult invalidU16WeightSum =
        Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("InvalidU16WeightSum.gltf"));
    const Skeletal::SkeletalGltfDecodeResult disconnectedJointForest =
        Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("DisconnectedJointForest.gltf"));
    const Skeletal::SkeletalGltfDecodeResult misalignedU8Accessor =
        Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("MisalignedU8Accessor.gltf"));
    const Skeletal::SkeletalGltfDecodeResult misalignedU16Accessor =
        Gltf::GLTFAnalyzer::AnalyzeSkeletal(fixture.Path("MisalignedU16Accessor.gltf"));
    std::cout << "review blocker statuses: raw_u16=" << static_cast<int>(invalidU16WeightSum.Status)
              << " forest=" << static_cast<int>(disconnectedJointForest.Status)
              << " align_u8=" << static_cast<int>(misalignedU8Accessor.Status)
              << " align_u16=" << static_cast<int>(misalignedU16Accessor.Status) << "\n";
    assert(invalidU16WeightSum.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor &&
           disconnectedJointForest.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton &&
           misalignedU8Accessor.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor &&
           misalignedU16Accessor.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);

    ExpectReject(fixture, "MalformedAccessor.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "FractionalAccessorCount.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "SparseAccessor.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedSparseAccessor);
    ExpectReject(fixture, "OutOfRangeJoint.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "TwoSkins.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedSkinCount);
    ExpectReject(fixture, "TwoPrimitives.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedPrimitiveCount);
    ExpectReject(fixture, "TwoClips.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedClipCount);
    ExpectReject(fixture, "CubicSpline.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedInterpolation);
    ExpectReject(fixture, "MorphTarget.gltf", Skeletal::SkeletalGltfDecodeStatus::UnsupportedMorphTargets);
    ExpectReject(fixture, "TooManyJoints.gltf", Skeletal::SkeletalGltfDecodeStatus::JointLimitExceeded);
    ExpectReject(fixture, "MeshOutsideScene.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "WrongSkinBinding.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "MissingMeshBinding.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "MissingSkinBinding.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "AmbiguousBinding.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "SingularMeshTransform.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "IntermediateNode.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "NodeCycle.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidSkeleton);
    ExpectReject(fixture, "DuplicateChannel.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAnimation);
    ExpectReject(fixture, "NonTriangleIndices.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "NormalizedFloat.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "InvalidStride.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "NonFinitePosition.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "NegativeWeight.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAccessor);
    ExpectReject(fixture, "NonIncreasingTime.gltf", Skeletal::SkeletalGltfDecodeStatus::InvalidAnimation);

    std::cout << "GLTFSkeletalAnalyzerTest passed\n";
    return 0;
}
