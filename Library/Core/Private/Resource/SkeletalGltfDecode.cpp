#include "Resource/SkeletalGltfDecode.h"

#include "FileStream/FileStream.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Skeletal
{
    namespace
    {
        constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
        constexpr uint32_t ByteComponent = 5121;
        constexpr uint32_t UnsignedShortComponent = 5123;
        constexpr uint32_t UnsignedIntComponent = 5125;
        constexpr uint32_t FloatComponent = 5126;
        constexpr size_t MaximumJointCount = 128;
        constexpr uint64_t MaximumExactJsonInteger = 9007199254740991ull;

        struct AccessorInfo
        {
            uint32_t BufferView = InvalidIndex;
            size_t ByteOffset = 0;
            uint32_t ComponentType = 0;
            uint32_t Count = 0;
            Container::String Type;
            bool bNormalized = false;
        };

        struct BufferViewInfo
        {
            uint32_t Buffer = InvalidIndex;
            size_t ByteOffset = 0;
            size_t ByteLength = 0;
            size_t ByteStride = 0;
        };

        struct BufferInfo
        {
            Container::String Uri;
            size_t ByteLength = 0;
        };

        struct AccessorLayout
        {
            const uint8_t* Data = nullptr;
            size_t Stride = 0;
            size_t ElementSize = 0;
        };

        struct PrimitiveInfo
        {
            uint32_t Position = InvalidIndex;
            uint32_t Normal = InvalidIndex;
            uint32_t TexCoord = InvalidIndex;
            uint32_t Joints = InvalidIndex;
            uint32_t Weights = InvalidIndex;
            uint32_t Indices = InvalidIndex;
        };

        using MatrixValues = Container::FixedArray<float, 16>;

        struct NodeContract
        {
            Container::VariableArray<int32_t> Parents;
            MatrixValues MeshNodeGlobal{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
            uint32_t MeshNodeIndex = InvalidIndex;
        };

        MatrixValues IdentityMatrix()
        {
            return {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        bool IsFiniteMatrix(const MatrixValues& matrix)
        {
            for (float value : matrix)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsInvertibleMatrix(const MatrixValues& matrix)
        {
            MatrixValues reduced = matrix;
            for (size_t column = 0; column < 4; ++column)
            {
                size_t pivotRow = column;
                float pivotMagnitude = std::fabs(reduced[pivotRow * 4 + column]);
                for (size_t row = column + 1; row < 4; ++row)
                {
                    const float magnitude = std::fabs(reduced[row * 4 + column]);
                    if (magnitude > pivotMagnitude)
                    {
                        pivotRow = row;
                        pivotMagnitude = magnitude;
                    }
                }
                if (!std::isfinite(pivotMagnitude) || pivotMagnitude <= 0.000001f)
                {
                    return false;
                }
                if (pivotRow != column)
                {
                    for (size_t element = 0; element < 4; ++element)
                    {
                        std::swap(reduced[column * 4 + element], reduced[pivotRow * 4 + element]);
                    }
                }
                const float pivot = reduced[column * 4 + column];
                for (size_t row = column + 1; row < 4; ++row)
                {
                    const float factor = reduced[row * 4 + column] / pivot;
                    for (size_t element = column; element < 4; ++element)
                    {
                        reduced[row * 4 + element] -= factor * reduced[column * 4 + element];
                    }
                }
            }
            return true;
        }

        MatrixValues MultiplyMatrix(const MatrixValues& left, const MatrixValues& right)
        {
            MatrixValues result{};
            for (size_t row = 0; row < 4; ++row)
            {
                for (size_t column = 0; column < 4; ++column)
                {
                    for (size_t inner = 0; inner < 4; ++inner)
                    {
                        result[row * 4 + column] +=
                            left[row * 4 + inner] * right[inner * 4 + column];
                    }
                }
            }
            return result;
        }

        bool ReadFloatArray(const JsonValue& value, size_t expectedCount, float* outValues)
        {
            if (!value.IsArray() || value.GetArraySize() != expectedCount)
            {
                return false;
            }
            for (size_t index = 0; index < expectedCount; ++index)
            {
                const JsonValue element = value.GetArrayElement(index);
                if (!element.IsNumber())
                {
                    return false;
                }
                const double number = element.AsNumber(0.0);
                if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
                    number > std::numeric_limits<float>::max())
                {
                    return false;
                }
                outValues[index] = static_cast<float>(number);
            }
            return true;
        }

        bool ParseNodeLocalTransform(const JsonValue& node, MatrixValues& outMatrix)
        {
            if (!node.IsObject())
            {
                return false;
            }
            const JsonValue matrix = node.FindMember("matrix");
            const bool bHasTrs = node.HasMember("translation") || node.HasMember("rotation") || node.HasMember("scale");
            if (matrix.IsValid())
            {
                return !bHasTrs && ReadFloatArray(matrix, 16, outMatrix.data()) && IsFiniteMatrix(outMatrix);
            }

            float translation[3] = {0.0f, 0.0f, 0.0f};
            float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float scale[3] = {1.0f, 1.0f, 1.0f};
            const JsonValue translationValue = node.FindMember("translation");
            const JsonValue rotationValue = node.FindMember("rotation");
            const JsonValue scaleValue = node.FindMember("scale");
            if ((translationValue.IsValid() && !ReadFloatArray(translationValue, 3, translation)) ||
                (rotationValue.IsValid() && !ReadFloatArray(rotationValue, 4, rotation)) ||
                (scaleValue.IsValid() && !ReadFloatArray(scaleValue, 3, scale)))
            {
                return false;
            }

            const float quaternionLengthSquared = rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                                  rotation[2] * rotation[2] + rotation[3] * rotation[3];
            if (!std::isfinite(quaternionLengthSquared) || quaternionLengthSquared <= 0.000001f)
            {
                return false;
            }
            const float inverseLength = 1.0f / std::sqrt(quaternionLengthSquared);
            const float x = rotation[0] * inverseLength;
            const float y = rotation[1] * inverseLength;
            const float z = rotation[2] * inverseLength;
            const float w = rotation[3] * inverseLength;

            outMatrix = IdentityMatrix();
            outMatrix[0] = scale[0] * (1.0f - 2.0f * (y * y + z * z));
            outMatrix[1] = scale[0] * (2.0f * (x * y + z * w));
            outMatrix[2] = scale[0] * (2.0f * (x * z - y * w));
            outMatrix[4] = scale[1] * (2.0f * (x * y - z * w));
            outMatrix[5] = scale[1] * (1.0f - 2.0f * (x * x + z * z));
            outMatrix[6] = scale[1] * (2.0f * (y * z + x * w));
            outMatrix[8] = scale[2] * (2.0f * (x * z + y * w));
            outMatrix[9] = scale[2] * (2.0f * (y * z - x * w));
            outMatrix[10] = scale[2] * (1.0f - 2.0f * (x * x + y * y));
            outMatrix[12] = translation[0];
            outMatrix[13] = translation[1];
            outMatrix[14] = translation[2];
            return IsFiniteMatrix(outMatrix);
        }

        bool CheckedAdd(size_t a, size_t b, size_t& out)
        {
            if (a > std::numeric_limits<size_t>::max() - b)
            {
                return false;
            }
            out = a + b;
            return true;
        }

        bool CheckedMultiply(size_t a, size_t b, size_t& out)
        {
            if (a != 0 && b > std::numeric_limits<size_t>::max() / a)
            {
                return false;
            }
            out = a * b;
            return true;
        }

        bool TryReadUnsigned(const JsonValue& value, uint64_t maximum, uint64_t& outValue)
        {
            if (!value.IsNumber())
            {
                return false;
            }

            const double number = value.AsNumber(-1.0);
            const double exactMaximum = static_cast<double>(std::min(maximum, MaximumExactJsonInteger));
            if (!std::isfinite(number) || number < 0.0 || number > exactMaximum || std::floor(number) != number)
            {
                return false;
            }

            const uint64_t converted = static_cast<uint64_t>(number);
            if (static_cast<double>(converted) != number)
            {
                return false;
            }
            outValue = converted;
            return true;
        }

        bool TryReadUInt32(const JsonValue& value, uint32_t& outValue)
        {
            uint64_t converted = 0;
            if (!TryReadUnsigned(value, UINT32_MAX, converted))
            {
                return false;
            }
            outValue = static_cast<uint32_t>(converted);
            return true;
        }

        bool TryReadRequiredUInt32(const JsonValue& object, const char* name, uint32_t& outValue)
        {
            return object.IsObject() && TryReadUInt32(object.FindMember(name), outValue);
        }

        bool TryReadOptionalUInt32(const JsonValue& object,
                                   const char* name,
                                   uint32_t defaultValue,
                                   uint32_t& outValue)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsValid())
            {
                outValue = defaultValue;
                return true;
            }
            return TryReadUInt32(value, outValue);
        }

        bool TryReadRequiredSize(const JsonValue& object, const char* name, size_t& outValue)
        {
            uint64_t converted = 0;
            if (!object.IsObject() ||
                !TryReadUnsigned(object.FindMember(name),
                                 static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
                                 converted))
            {
                return false;
            }
            outValue = static_cast<size_t>(converted);
            return true;
        }

        bool TryReadOptionalSize(const JsonValue& object,
                                 const char* name,
                                 size_t defaultValue,
                                 size_t& outValue)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsValid())
            {
                outValue = defaultValue;
                return true;
            }

            uint64_t converted = 0;
            if (!TryReadUnsigned(value, static_cast<uint64_t>(std::numeric_limits<size_t>::max()), converted))
            {
                return false;
            }
            outValue = static_cast<size_t>(converted);
            return true;
        }

        bool BuildNodeGlobal(size_t nodeIndex,
                             const Container::VariableArray<int32_t>& parents,
                             const Container::VariableArray<MatrixValues>& locals,
                             Container::VariableArray<MatrixValues>& globals,
                             Container::VariableArray<uint8_t>& states)
        {
            if (states[nodeIndex] == 2)
            {
                return true;
            }
            if (states[nodeIndex] == 1)
            {
                return false;
            }
            states[nodeIndex] = 1;
            const int32_t parent = parents[nodeIndex];
            if (parent >= 0)
            {
                if (!BuildNodeGlobal(static_cast<size_t>(parent), parents, locals, globals, states))
                {
                    return false;
                }
                globals[nodeIndex] = MultiplyMatrix(locals[nodeIndex], globals[static_cast<size_t>(parent)]);
            }
            else
            {
                globals[nodeIndex] = locals[nodeIndex];
            }
            states[nodeIndex] = 2;
            return IsFiniteMatrix(globals[nodeIndex]);
        }

        bool MarkReachableNode(uint32_t nodeIndex,
                               const JsonValue& nodes,
                               Container::VariableArray<uint8_t>& reachable)
        {
            if (nodeIndex >= nodes.GetArraySize())
            {
                return false;
            }
            if (reachable[nodeIndex] != 0)
            {
                return true;
            }
            reachable[nodeIndex] = 1;
            const JsonValue children = nodes.GetArrayElement(nodeIndex).FindMember("children");
            if (!children.IsValid())
            {
                return true;
            }
            if (!children.IsArray())
            {
                return false;
            }
            for (size_t childIndex = 0; childIndex < children.GetArraySize(); ++childIndex)
            {
                uint32_t childNodeIndex = InvalidIndex;
                if (!TryReadUInt32(children.GetArrayElement(childIndex), childNodeIndex) ||
                    !MarkReachableNode(childNodeIndex, nodes, reachable))
                {
                    return false;
                }
            }
            return true;
        }

        bool ParseNodeContract(const JsonValue& root, NodeContract& outContract)
        {
            const JsonValue nodes = root.FindMember("nodes");
            const JsonValue scenes = root.FindMember("scenes");
            uint32_t sceneIndex = InvalidIndex;
            if (!nodes.IsArray() || nodes.GetArraySize() == 0 || !scenes.IsArray() ||
                !TryReadRequiredUInt32(root, "scene", sceneIndex) || sceneIndex >= scenes.GetArraySize())
            {
                return false;
            }
            const JsonValue sceneRoots = scenes.GetArrayElement(sceneIndex).FindMember("nodes");
            if (!sceneRoots.IsArray() || sceneRoots.GetArraySize() == 0)
            {
                return false;
            }

            const size_t nodeCount = nodes.GetArraySize();
            outContract.Parents.assign(nodeCount, -1);
            Container::VariableArray<MatrixValues> locals(nodeCount);
            Container::VariableArray<MatrixValues> globals(nodeCount);
            for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const JsonValue node = nodes.GetArrayElement(nodeIndex);
                if (!ParseNodeLocalTransform(node, locals[nodeIndex]))
                {
                    return false;
                }
                const JsonValue children = node.FindMember("children");
                if (!children.IsValid())
                {
                    continue;
                }
                if (!children.IsArray())
                {
                    return false;
                }
                for (size_t childIndex = 0; childIndex < children.GetArraySize(); ++childIndex)
                {
                    uint32_t childNodeIndex = InvalidIndex;
                    if (!TryReadUInt32(children.GetArrayElement(childIndex), childNodeIndex) ||
                        childNodeIndex >= nodeCount || outContract.Parents[childNodeIndex] >= 0)
                    {
                        return false;
                    }
                    outContract.Parents[childNodeIndex] = static_cast<int32_t>(nodeIndex);
                }
            }

            Container::VariableArray<uint8_t> globalStates(nodeCount, 0);
            for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                if (!BuildNodeGlobal(nodeIndex, outContract.Parents, locals, globals, globalStates))
                {
                    return false;
                }
            }

            Container::VariableArray<uint8_t> reachable(nodeCount, 0);
            for (size_t rootIndex = 0; rootIndex < sceneRoots.GetArraySize(); ++rootIndex)
            {
                uint32_t rootNodeIndex = InvalidIndex;
                if (!TryReadUInt32(sceneRoots.GetArrayElement(rootIndex), rootNodeIndex) ||
                    rootNodeIndex >= nodeCount || outContract.Parents[rootNodeIndex] >= 0 ||
                    !MarkReachableNode(rootNodeIndex, nodes, reachable))
                {
                    return false;
                }
            }

            size_t bindingCount = 0;
            for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const JsonValue node = nodes.GetArrayElement(nodeIndex);
                const JsonValue meshValue = node.FindMember("mesh");
                const JsonValue skinValue = node.FindMember("skin");
                if (!meshValue.IsValid() && !skinValue.IsValid())
                {
                    continue;
                }
                uint32_t meshIndex = InvalidIndex;
                uint32_t skinIndex = InvalidIndex;
                if (!TryReadUInt32(meshValue, meshIndex) || !TryReadUInt32(skinValue, skinIndex) ||
                    meshIndex != 0 || skinIndex != 0)
                {
                    return false;
                }
                ++bindingCount;
                outContract.MeshNodeIndex = static_cast<uint32_t>(nodeIndex);
            }
            if (bindingCount != 1 || outContract.MeshNodeIndex == InvalidIndex ||
                reachable[outContract.MeshNodeIndex] == 0)
            {
                return false;
            }
            outContract.MeshNodeGlobal = globals[outContract.MeshNodeIndex];
            return IsInvertibleMatrix(outContract.MeshNodeGlobal);
        }

        size_t GetComponentSize(uint32_t componentType)
        {
            switch (componentType)
            {
            case ByteComponent:
                return 1;
            case UnsignedShortComponent:
                return 2;
            case UnsignedIntComponent:
            case FloatComponent:
                return 4;
            default:
                return 0;
            }
        }

        size_t GetComponentCount(const Container::String& type)
        {
            if (type == "SCALAR")
            {
                return 1;
            }
            if (type == "VEC2")
            {
                return 2;
            }
            if (type == "VEC3")
            {
                return 3;
            }
            if (type == "VEC4")
            {
                return 4;
            }
            if (type == "MAT4")
            {
                return 16;
            }
            return 0;
        }

        uint16_t ReadUInt16(const uint8_t* data)
        {
            uint16_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        uint32_t ReadUInt32(const uint8_t* data)
        {
            uint32_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        float ReadFloat(const uint8_t* data)
        {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        uint32_t ReadUnsignedComponent(const uint8_t* data, uint32_t componentType)
        {
            switch (componentType)
            {
            case ByteComponent:
                return *data;
            case UnsignedShortComponent:
                return ReadUInt16(data);
            case UnsignedIntComponent:
                return ReadUInt32(data);
            default:
                return 0;
            }
        }

        float ReadWeightComponent(const uint8_t* data, uint32_t componentType, bool bNormalized)
        {
            if (componentType == FloatComponent)
            {
                return ReadFloat(data);
            }
            if (componentType == ByteComponent && bNormalized)
            {
                return static_cast<float>(*data) / 255.0f;
            }
            if (componentType == UnsignedShortComponent && bNormalized)
            {
                return static_cast<float>(ReadUInt16(data)) / 65535.0f;
            }
            return 0.0f;
        }

        bool ReadBinaryFile(const Container::String& path, Container::VariableArray<uint8_t>& outBytes)
        {
            auto stream = NorvesLib::FileStream::FileStream::Create(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);
            if (!stream || !stream->IsOpen())
            {
                return false;
            }

            const int64_t fileSize = stream->GetSize();
            if (fileSize < 0 || static_cast<uint64_t>(fileSize) > std::numeric_limits<size_t>::max())
            {
                stream->Close();
                return false;
            }

            outBytes.resize(static_cast<size_t>(fileSize));
            const size_t readSize = outBytes.empty() ? 0 : stream->Read(outBytes.data(), outBytes.size());
            stream->Close();
            return readSize == outBytes.size();
        }

        Container::String MakeBufferPath(const Container::String& sourcePath, const Container::String& uri)
        {
            const std::filesystem::path path =
                (std::filesystem::path(sourcePath.c_str()).parent_path() / uri.c_str()).lexically_normal();
            return Container::String(path.string().c_str());
        }

        bool ParseAccessors(const JsonValue& root,
                            Container::VariableArray<AccessorInfo>& outAccessors,
                            SkeletalGltfDecodeStatus& outStatus)
        {
            const JsonValue values = root.FindMember("accessors");
            if (!values.IsArray())
            {
                return false;
            }

            outAccessors.reserve(values.GetArraySize());
            for (size_t index = 0; index < values.GetArraySize(); ++index)
            {
                const JsonValue value = values.GetArrayElement(index);
                if (!value.IsObject())
                {
                    outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
                    return false;
                }

                if (value.HasMember("sparse"))
                {
                    outStatus = SkeletalGltfDecodeStatus::UnsupportedSparseAccessor;
                    return false;
                }

                AccessorInfo accessor;
                if (!TryReadRequiredUInt32(value, "bufferView", accessor.BufferView) ||
                    !TryReadOptionalSize(value, "byteOffset", 0, accessor.ByteOffset) ||
                    !TryReadRequiredUInt32(value, "componentType", accessor.ComponentType) ||
                    !TryReadRequiredUInt32(value, "count", accessor.Count))
                {
                    outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
                    return false;
                }
                accessor.Type = value.FindMember("type").AsString();
                const JsonValue normalized = value.FindMember("normalized");
                if (normalized.IsValid() && !normalized.IsBoolean())
                {
                    outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
                    return false;
                }
                accessor.bNormalized = normalized.AsBool(false);
                if (accessor.ComponentType == FloatComponent && accessor.bNormalized)
                {
                    outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
                    return false;
                }
                outAccessors.push_back(std::move(accessor));
            }
            return true;
        }

        bool ParseBufferViews(const JsonValue& root, Container::VariableArray<BufferViewInfo>& outBufferViews)
        {
            const JsonValue values = root.FindMember("bufferViews");
            if (!values.IsArray())
            {
                return false;
            }

            outBufferViews.reserve(values.GetArraySize());
            for (size_t index = 0; index < values.GetArraySize(); ++index)
            {
                const JsonValue value = values.GetArrayElement(index);
                if (!value.IsObject())
                {
                    return false;
                }

                BufferViewInfo bufferView;
                if (!TryReadRequiredUInt32(value, "buffer", bufferView.Buffer) ||
                    !TryReadOptionalSize(value, "byteOffset", 0, bufferView.ByteOffset) ||
                    !TryReadRequiredSize(value, "byteLength", bufferView.ByteLength) ||
                    !TryReadOptionalSize(value, "byteStride", 0, bufferView.ByteStride))
                {
                    return false;
                }
                if (bufferView.ByteStride != 0 &&
                    (bufferView.ByteStride < 4 || bufferView.ByteStride > 252 || bufferView.ByteStride % 4 != 0))
                {
                    return false;
                }
                outBufferViews.push_back(bufferView);
            }
            return true;
        }

        bool ParseBuffers(const JsonValue& root, Container::VariableArray<BufferInfo>& outBuffers)
        {
            const JsonValue values = root.FindMember("buffers");
            if (!values.IsArray() || values.GetArraySize() == 0)
            {
                return false;
            }

            outBuffers.reserve(values.GetArraySize());
            for (size_t index = 0; index < values.GetArraySize(); ++index)
            {
                const JsonValue value = values.GetArrayElement(index);
                if (!value.IsObject())
                {
                    return false;
                }

                BufferInfo buffer;
                buffer.Uri = value.FindMember("uri").AsString();
                if (!TryReadRequiredSize(value, "byteLength", buffer.ByteLength) || buffer.Uri.empty() ||
                    buffer.Uri.find("data:") == 0)
                {
                    return false;
                }
                outBuffers.push_back(std::move(buffer));
            }
            return true;
        }

        bool LoadBuffers(const Container::VariableArray<BufferInfo>& buffers,
                         const Container::String& sourcePath,
                         Container::VariableArray<Container::VariableArray<uint8_t>>& outBytes)
        {
            outBytes.resize(buffers.size());
            for (size_t index = 0; index < buffers.size(); ++index)
            {
                if (!ReadBinaryFile(MakeBufferPath(sourcePath, buffers[index].Uri), outBytes[index]) ||
                    outBytes[index].size() < buffers[index].ByteLength)
                {
                    return false;
                }
            }
            return true;
        }

        bool BuildAccessorLayout(const AccessorInfo& accessor,
                                 const Container::VariableArray<BufferViewInfo>& bufferViews,
                                 const Container::VariableArray<BufferInfo>& buffers,
                                 const Container::VariableArray<Container::VariableArray<uint8_t>>& bufferBytes,
                                 AccessorLayout& outLayout)
        {
            if (accessor.BufferView >= bufferViews.size())
            {
                return false;
            }

            const BufferViewInfo& view = bufferViews[accessor.BufferView];
            if (view.Buffer >= buffers.size() || view.Buffer >= bufferBytes.size())
            {
                return false;
            }

            size_t viewEnd = 0;
            if (!CheckedAdd(view.ByteOffset, view.ByteLength, viewEnd) ||
                viewEnd > buffers[view.Buffer].ByteLength || viewEnd > bufferBytes[view.Buffer].size())
            {
                return false;
            }

            size_t elementSize = 0;
            if (!CheckedMultiply(GetComponentSize(accessor.ComponentType), GetComponentCount(accessor.Type), elementSize) ||
                elementSize == 0)
            {
                return false;
            }

            const size_t stride = view.ByteStride == 0 ? elementSize : view.ByteStride;
            const size_t componentSize = GetComponentSize(accessor.ComponentType);
            if (stride < elementSize || componentSize == 0 || stride % componentSize != 0 ||
                accessor.ByteOffset % componentSize != 0 || view.ByteOffset % componentSize != 0)
            {
                return false;
            }

            size_t localEnd = accessor.ByteOffset;
            if (accessor.Count > 0)
            {
                size_t precedingSize = 0;
                if (!CheckedMultiply(static_cast<size_t>(accessor.Count) - 1, stride, precedingSize) ||
                    !CheckedAdd(localEnd, precedingSize, localEnd) || !CheckedAdd(localEnd, elementSize, localEnd))
                {
                    return false;
                }
            }
            if (localEnd > view.ByteLength)
            {
                return false;
            }

            size_t dataOffset = 0;
            if (!CheckedAdd(view.ByteOffset, accessor.ByteOffset, dataOffset) ||
                dataOffset > bufferBytes[view.Buffer].size())
            {
                return false;
            }

            outLayout.Data = bufferBytes[view.Buffer].data() + dataOffset;
            outLayout.Stride = stride;
            outLayout.ElementSize = elementSize;
            return true;
        }

        bool GetAccessor(const Container::VariableArray<AccessorInfo>& accessors,
                         uint32_t index,
                         const Container::String& type,
                         uint32_t componentType,
                         const Container::VariableArray<BufferViewInfo>& bufferViews,
                         const Container::VariableArray<BufferInfo>& buffers,
                         const Container::VariableArray<Container::VariableArray<uint8_t>>& bufferBytes,
                         const AccessorInfo*& outAccessor,
                         AccessorLayout& outLayout)
        {
            if (index >= accessors.size())
            {
                return false;
            }
            const AccessorInfo& accessor = accessors[index];
            if (accessor.Type != type || accessor.ComponentType != componentType ||
                !BuildAccessorLayout(accessor, bufferViews, buffers, bufferBytes, outLayout))
            {
                return false;
            }
            outAccessor = &accessor;
            return true;
        }

        bool ParsePrimitive(const JsonValue& root, PrimitiveInfo& outPrimitive, SkeletalGltfDecodeStatus& outStatus)
        {
            const JsonValue meshes = root.FindMember("meshes");
            if (!meshes.IsArray() || meshes.GetArraySize() != 1)
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedMeshCount;
                return false;
            }
            const JsonValue primitives = meshes.GetArrayElement(0).FindMember("primitives");
            if (!primitives.IsArray() || primitives.GetArraySize() != 1)
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedPrimitiveCount;
                return false;
            }

            const JsonValue primitive = primitives.GetArrayElement(0);
            if (!primitive.IsObject())
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidDocument;
                return false;
            }
            if (primitive.HasMember("targets"))
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedMorphTargets;
                return false;
            }
            uint32_t mode = 4;
            if (!TryReadOptionalUInt32(primitive, "mode", 4, mode) || mode != 4)
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedPrimitiveCount;
                return false;
            }

            const JsonValue attributes = primitive.FindMember("attributes");
            if (!attributes.IsObject())
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidDocument;
                return false;
            }
            if (!TryReadRequiredUInt32(attributes, "POSITION", outPrimitive.Position) ||
                !TryReadRequiredUInt32(attributes, "NORMAL", outPrimitive.Normal) ||
                !TryReadRequiredUInt32(attributes, "TEXCOORD_0", outPrimitive.TexCoord) ||
                !TryReadRequiredUInt32(attributes, "JOINTS_0", outPrimitive.Joints) ||
                !TryReadRequiredUInt32(attributes, "WEIGHTS_0", outPrimitive.Weights) ||
                !TryReadRequiredUInt32(primitive, "indices", outPrimitive.Indices))
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
                return false;
            }
            return true;
        }

        bool ParseSkinContract(const JsonValue& root, JsonValue& outSkin, SkeletalGltfDecodeStatus& outStatus)
        {
            const JsonValue skins = root.FindMember("skins");
            if (!skins.IsArray() || skins.GetArraySize() != 1)
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedSkinCount;
                return false;
            }
            outSkin = skins.GetArrayElement(0);
            const JsonValue joints = outSkin.FindMember("joints");
            if (!outSkin.IsObject() || !joints.IsArray() || joints.GetArraySize() == 0)
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidSkeleton;
                return false;
            }
            if (joints.GetArraySize() > MaximumJointCount)
            {
                outStatus = SkeletalGltfDecodeStatus::JointLimitExceeded;
                return false;
            }
            return true;
        }

        bool ParseAnimationContract(const JsonValue& root, JsonValue& outAnimation, SkeletalGltfDecodeStatus& outStatus)
        {
            const JsonValue animations = root.FindMember("animations");
            if (!animations.IsArray() || animations.GetArraySize() != 1)
            {
                outStatus = SkeletalGltfDecodeStatus::UnsupportedClipCount;
                return false;
            }
            outAnimation = animations.GetArrayElement(0);
            const JsonValue samplers = outAnimation.FindMember("samplers");
            const JsonValue channels = outAnimation.FindMember("channels");
            if (!outAnimation.IsObject() || !samplers.IsArray() || samplers.GetArraySize() == 0 ||
                !channels.IsArray() || channels.GetArraySize() == 0)
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidAnimation;
                return false;
            }
            for (size_t index = 0; index < samplers.GetArraySize(); ++index)
            {
                const Container::String& interpolation =
                    samplers.GetArrayElement(index).FindMember("interpolation").AsString();
                if (!interpolation.empty() && interpolation != "LINEAR" && interpolation != "STEP")
                {
                    outStatus = SkeletalGltfDecodeStatus::UnsupportedInterpolation;
                    return false;
                }
            }
            return true;
        }

        bool ExtractMesh(const PrimitiveInfo& primitive,
                         const Container::VariableArray<AccessorInfo>& accessors,
                         const Container::VariableArray<BufferViewInfo>& bufferViews,
                         const Container::VariableArray<BufferInfo>& buffers,
                         const Container::VariableArray<Container::VariableArray<uint8_t>>& bufferBytes,
                         SkeletalGltfData& outData)
        {
            const AccessorInfo* position = nullptr;
            const AccessorInfo* normal = nullptr;
            const AccessorInfo* texCoord = nullptr;
            const AccessorInfo* joints = nullptr;
            const AccessorInfo* weights = nullptr;
            const AccessorInfo* indices = nullptr;
            AccessorLayout positionLayout;
            AccessorLayout normalLayout;
            AccessorLayout texCoordLayout;
            AccessorLayout jointsLayout;
            AccessorLayout weightsLayout;
            AccessorLayout indexLayout;

            if (!GetAccessor(accessors, primitive.Position, "VEC3", FloatComponent, bufferViews, buffers, bufferBytes,
                             position, positionLayout) ||
                !GetAccessor(accessors, primitive.Normal, "VEC3", FloatComponent, bufferViews, buffers, bufferBytes,
                             normal, normalLayout) ||
                !GetAccessor(accessors, primitive.TexCoord, "VEC2", FloatComponent, bufferViews, buffers, bufferBytes,
                             texCoord, texCoordLayout))
            {
                return false;
            }
            if (primitive.Joints >= accessors.size() || primitive.Weights >= accessors.size() ||
                primitive.Indices >= accessors.size())
            {
                return false;
            }

            joints = &accessors[primitive.Joints];
            weights = &accessors[primitive.Weights];
            indices = &accessors[primitive.Indices];
            const bool bValidJointType = joints->Type == "VEC4" && !joints->bNormalized &&
                                         (joints->ComponentType == ByteComponent ||
                                          joints->ComponentType == UnsignedShortComponent);
            const bool bValidWeightType = weights->Type == "VEC4" &&
                                          ((weights->ComponentType == FloatComponent && !weights->bNormalized) ||
                                           ((weights->ComponentType == ByteComponent ||
                                             weights->ComponentType == UnsignedShortComponent) &&
                                            weights->bNormalized));
            const bool bValidIndexType = indices->Type == "SCALAR" && !indices->bNormalized &&
                                         (indices->ComponentType == UnsignedShortComponent ||
                                          indices->ComponentType == UnsignedIntComponent);
            if (!bValidJointType || !bValidWeightType || !bValidIndexType || position->ByteOffset % 4 != 0 ||
                normal->ByteOffset % 4 != 0 || texCoord->ByteOffset % 4 != 0 || joints->ByteOffset % 4 != 0 ||
                weights->ByteOffset % 4 != 0 ||
                !BuildAccessorLayout(*joints, bufferViews, buffers, bufferBytes, jointsLayout) ||
                !BuildAccessorLayout(*weights, bufferViews, buffers, bufferBytes, weightsLayout) ||
                !BuildAccessorLayout(*indices, bufferViews, buffers, bufferBytes, indexLayout) ||
                position->Count == 0 || normal->Count != position->Count || texCoord->Count != position->Count ||
                joints->Count != position->Count || weights->Count != position->Count ||
                indices->Count == 0 || indices->Count % 3 != 0)
            {
                return false;
            }

            outData.Vertices.resize(position->Count);
            for (size_t vertexIndex = 0; vertexIndex < position->Count; ++vertexIndex)
            {
                SkeletalVertex& vertex = outData.Vertices[vertexIndex];
                const uint8_t* positionData = positionLayout.Data + vertexIndex * positionLayout.Stride;
                const uint8_t* normalData = normalLayout.Data + vertexIndex * normalLayout.Stride;
                const uint8_t* texCoordData = texCoordLayout.Data + vertexIndex * texCoordLayout.Stride;
                const uint8_t* jointData = jointsLayout.Data + vertexIndex * jointsLayout.Stride;
                const uint8_t* weightData = weightsLayout.Data + vertexIndex * weightsLayout.Stride;
                vertex.Position = {ReadFloat(positionData), ReadFloat(positionData + 4), ReadFloat(positionData + 8)};
                vertex.Normal = {ReadFloat(normalData), ReadFloat(normalData + 4), ReadFloat(normalData + 8)};
                vertex.TexCoord = {ReadFloat(texCoordData), ReadFloat(texCoordData + 4)};
                const size_t jointComponentSize = GetComponentSize(joints->ComponentType);
                const size_t weightComponentSize = GetComponentSize(weights->ComponentType);
                uint32_t rawWeightSum = 0;
                for (size_t influence = 0; influence < 4; ++influence)
                {
                    vertex.JointIndices[influence] =
                        ReadUnsignedComponent(jointData + influence * jointComponentSize, joints->ComponentType);
                    vertex.JointWeights[influence] = ReadWeightComponent(
                        weightData + influence * weightComponentSize, weights->ComponentType, weights->bNormalized);
                    if (weights->ComponentType != FloatComponent)
                    {
                        rawWeightSum += ReadUnsignedComponent(
                            weightData + influence * weightComponentSize, weights->ComponentType);
                    }
                }
                float weightSum = 0.0f;
                if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                    !std::isfinite(vertex.Position.Z) || !std::isfinite(vertex.Normal.X) ||
                    !std::isfinite(vertex.Normal.Y) || !std::isfinite(vertex.Normal.Z) ||
                    !std::isfinite(vertex.TexCoord.U) || !std::isfinite(vertex.TexCoord.V))
                {
                    return false;
                }
                for (float weight : vertex.JointWeights)
                {
                    if (!std::isfinite(weight) || weight < 0.0f)
                    {
                        return false;
                    }
                    weightSum += weight;
                }
                if (!std::isfinite(weightSum) || std::fabs(weightSum - 1.0f) > 0.001f)
                {
                    return false;
                }
                if ((weights->ComponentType == ByteComponent && rawWeightSum != UINT8_MAX) ||
                    (weights->ComponentType == UnsignedShortComponent && rawWeightSum != UINT16_MAX))
                {
                    return false;
                }
            }

            outData.Indices.resize(indices->Count);
            const size_t indexComponentSize = GetComponentSize(indices->ComponentType);
            for (size_t index = 0; index < indices->Count; ++index)
            {
                outData.Indices[index] = ReadUnsignedComponent(
                    indexLayout.Data + index * indexLayout.Stride, indices->ComponentType);
                if (outData.Indices[index] >= outData.Vertices.size())
                {
                    return false;
                }
            }
            for (size_t index = 0; index < outData.Indices.size(); index += 3)
            {
                std::swap(outData.Indices[index + 1], outData.Indices[index + 2]);
            }
            return indexComponentSize != 0;
        }

        bool ExtractSkeleton(const JsonValue& root,
                             const JsonValue& skin,
                             const NodeContract& nodeContract,
                             const Container::VariableArray<AccessorInfo>& accessors,
                             const Container::VariableArray<BufferViewInfo>& bufferViews,
                             const Container::VariableArray<BufferInfo>& buffers,
                             const Container::VariableArray<Container::VariableArray<uint8_t>>& bufferBytes,
                             SkeletalGltfData& outData,
                             Container::VariableArray<int32_t>& outNodeToJoint)
        {
            const JsonValue nodes = root.FindMember("nodes");
            const JsonValue jointValues = skin.FindMember("joints");
            if (!nodes.IsArray())
            {
                return false;
            }

            uint32_t inverseBindAccessorIndex = InvalidIndex;
            const AccessorInfo* inverseBind = nullptr;
            AccessorLayout inverseBindLayout;
            if (!TryReadRequiredUInt32(skin, "inverseBindMatrices", inverseBindAccessorIndex) ||
                !GetAccessor(accessors, inverseBindAccessorIndex, "MAT4", FloatComponent, bufferViews, buffers,
                             bufferBytes, inverseBind, inverseBindLayout) ||
                inverseBind->Count != jointValues.GetArraySize())
            {
                return false;
            }

            outNodeToJoint.assign(nodes.GetArraySize(), -1);
            outData.Joints.resize(jointValues.GetArraySize());
            for (size_t jointIndex = 0; jointIndex < jointValues.GetArraySize(); ++jointIndex)
            {
                uint32_t nodeIndex = InvalidIndex;
                if (!TryReadUInt32(jointValues.GetArrayElement(jointIndex), nodeIndex) ||
                    nodeIndex >= nodes.GetArraySize() || outNodeToJoint[nodeIndex] >= 0)
                {
                    return false;
                }
                outNodeToJoint[nodeIndex] = static_cast<int32_t>(jointIndex);
                SkeletalJoint& joint = outData.Joints[jointIndex];
                joint.Name = nodes.GetArrayElement(nodeIndex).FindMember("name").AsString();
                const uint8_t* matrixData = inverseBindLayout.Data + jointIndex * inverseBindLayout.Stride;
                for (size_t element = 0; element < 16; ++element)
                {
                    joint.InverseBindMatrix[element] = ReadFloat(matrixData + element * sizeof(float));
                    if (!std::isfinite(joint.InverseBindMatrix[element]))
                    {
                        return false;
                    }
                }
            }

            uint32_t skeletonNodeIndex = InvalidIndex;
            if (!TryReadRequiredUInt32(skin, "skeleton", skeletonNodeIndex) ||
                skeletonNodeIndex >= nodes.GetArraySize() || outNodeToJoint[skeletonNodeIndex] < 0)
            {
                return false;
            }
            for (size_t nodeIndex = 0; nodeIndex < nodes.GetArraySize(); ++nodeIndex)
            {
                if (outNodeToJoint[nodeIndex] < 0)
                {
                    continue;
                }
                const int32_t parentNodeIndex = nodeContract.Parents[nodeIndex];
                if (parentNodeIndex >= 0)
                {
                    const int32_t parentJointIndex = outNodeToJoint[static_cast<size_t>(parentNodeIndex)];
                    if (parentJointIndex < 0)
                    {
                        return false;
                    }
                    else
                    {
                        outData.Joints[static_cast<size_t>(outNodeToJoint[nodeIndex])].ParentIndex = parentJointIndex;
                    }
                }
            }
            if (outData.Joints[static_cast<size_t>(outNodeToJoint[skeletonNodeIndex])].ParentIndex >= 0)
            {
                return false;
            }
            const int32_t skeletonJointIndex = outNodeToJoint[skeletonNodeIndex];
            size_t rootCount = 0;
            for (size_t jointIndex = 0; jointIndex < outData.Joints.size(); ++jointIndex)
            {
                if (outData.Joints[jointIndex].ParentIndex < 0)
                {
                    ++rootCount;
                }

                int32_t ancestor = static_cast<int32_t>(jointIndex);
                size_t depth = 0;
                while (ancestor != skeletonJointIndex)
                {
                    if (ancestor < 0 || depth++ >= outData.Joints.size())
                    {
                        return false;
                    }
                    ancestor = outData.Joints[static_cast<size_t>(ancestor)].ParentIndex;
                }
            }
            return rootCount == 1;
        }

        bool ExtractAnimation(const JsonValue& animation,
                              const Container::VariableArray<int32_t>& nodeToJoint,
                              const Container::VariableArray<AccessorInfo>& accessors,
                              const Container::VariableArray<BufferViewInfo>& bufferViews,
                              const Container::VariableArray<BufferInfo>& buffers,
                              const Container::VariableArray<Container::VariableArray<uint8_t>>& bufferBytes,
                              SkeletalGltfData& outData)
        {
            const JsonValue samplers = animation.FindMember("samplers");
            const JsonValue channels = animation.FindMember("channels");
            SkeletalAnimationClip clip;
            clip.Name = animation.FindMember("name").AsString();
            clip.Channels.reserve(channels.GetArraySize());
            Container::VariableArray<uint8_t> animatedPaths(nodeToJoint.size() * 3, 0);

            for (size_t channelIndex = 0; channelIndex < channels.GetArraySize(); ++channelIndex)
            {
                const JsonValue channelValue = channels.GetArrayElement(channelIndex);
                const JsonValue target = channelValue.FindMember("target");
                uint32_t samplerIndex = InvalidIndex;
                uint32_t nodeIndex = InvalidIndex;
                if (!TryReadRequiredUInt32(channelValue, "sampler", samplerIndex) ||
                    !TryReadRequiredUInt32(target, "node", nodeIndex) || samplerIndex >= samplers.GetArraySize() ||
                    nodeIndex >= nodeToJoint.size() ||
                    nodeToJoint[nodeIndex] < 0)
                {
                    return false;
                }

                const JsonValue sampler = samplers.GetArrayElement(samplerIndex);
                uint32_t inputIndex = InvalidIndex;
                uint32_t outputIndex = InvalidIndex;
                const AccessorInfo* input = nullptr;
                AccessorLayout inputLayout;
                if (!TryReadRequiredUInt32(sampler, "input", inputIndex) ||
                    !TryReadRequiredUInt32(sampler, "output", outputIndex) ||
                    !GetAccessor(accessors, inputIndex, "SCALAR", FloatComponent, bufferViews, buffers, bufferBytes,
                                 input, inputLayout) ||
                    outputIndex >= accessors.size())
                {
                    return false;
                }

                SkeletalAnimationChannel channel;
                channel.JointIndex = static_cast<uint32_t>(nodeToJoint[nodeIndex]);
                const Container::String& path = target.FindMember("path").AsString();
                Container::String outputType;
                size_t valueComponentCount = 0;
                if (path == "translation")
                {
                    channel.Path = SkeletalAnimationPath::Translation;
                    outputType = "VEC3";
                    valueComponentCount = 3;
                }
                else if (path == "rotation")
                {
                    channel.Path = SkeletalAnimationPath::Rotation;
                    outputType = "VEC4";
                    valueComponentCount = 4;
                }
                else if (path == "scale")
                {
                    channel.Path = SkeletalAnimationPath::Scale;
                    outputType = "VEC3";
                    valueComponentCount = 3;
                }
                else
                {
                    return false;
                }

                const size_t uniquePathIndex = static_cast<size_t>(channel.JointIndex) * 3 +
                                               static_cast<size_t>(channel.Path);
                if (uniquePathIndex >= animatedPaths.size() || animatedPaths[uniquePathIndex] != 0)
                {
                    return false;
                }
                animatedPaths[uniquePathIndex] = 1;

                const Container::String& interpolation = sampler.FindMember("interpolation").AsString();
                channel.Interpolation = interpolation == "STEP" ? SkeletalAnimationInterpolation::Step
                                                                : SkeletalAnimationInterpolation::Linear;
                const AccessorInfo* output = nullptr;
                AccessorLayout outputLayout;
                if (!GetAccessor(accessors, outputIndex, outputType, FloatComponent, bufferViews, buffers, bufferBytes,
                                 output, outputLayout) ||
                    input->Count == 0 || output->Count != input->Count)
                {
                    return false;
                }

                channel.Samples.resize(input->Count);
                for (size_t sampleIndex = 0; sampleIndex < input->Count; ++sampleIndex)
                {
                    SkeletalAnimationSample& sample = channel.Samples[sampleIndex];
                    sample.TimeSeconds = ReadFloat(inputLayout.Data + sampleIndex * inputLayout.Stride);
                    const uint8_t* valueData = outputLayout.Data + sampleIndex * outputLayout.Stride;
                    sample.Value.X = ReadFloat(valueData);
                    if (valueComponentCount > 1)
                    {
                        sample.Value.Y = ReadFloat(valueData + 4);
                    }
                    if (valueComponentCount > 2)
                    {
                        sample.Value.Z = ReadFloat(valueData + 8);
                    }
                    if (valueComponentCount > 3)
                    {
                        sample.Value.W = ReadFloat(valueData + 12);
                    }
                    if (!std::isfinite(sample.TimeSeconds) || sample.TimeSeconds < 0.0f ||
                        (sampleIndex > 0 && sample.TimeSeconds <= channel.Samples[sampleIndex - 1].TimeSeconds) ||
                        !std::isfinite(sample.Value.X) || !std::isfinite(sample.Value.Y) ||
                        !std::isfinite(sample.Value.Z) || !std::isfinite(sample.Value.W))
                    {
                        return false;
                    }
                    clip.DurationSeconds = std::max(clip.DurationSeconds, sample.TimeSeconds);
                }
                clip.Channels.push_back(std::move(channel));
            }

            if (clip.Channels.empty() || !std::isfinite(clip.DurationSeconds))
            {
                return false;
            }
            outData.Clips.push_back(std::move(clip));
            return true;
        }

        SkeletalGltfDecodeResult Fail(SkeletalGltfDecodeStatus status)
        {
            SkeletalGltfDecodeResult result;
            result.Status = status;
            return result;
        }
    } // namespace

    SkeletalGltfDecodeResult DecodeSkeletalGltf(const Container::String& jsonText,
                                                const Container::String& sourcePath,
                                                SkeletalGltfSourceBuffers* outSourceBuffers)
    {
        if (outSourceBuffers != nullptr)
        {
            outSourceBuffers->clear();
        }
        JsonDocument document;
        Container::String error;
        if (!JsonDocument::TryParse(jsonText, document, &error))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidJson);
        }

        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
        }

        PrimitiveInfo primitive;
        SkeletalGltfDecodeStatus status = SkeletalGltfDecodeStatus::InvalidDocument;
        if (!ParsePrimitive(root, primitive, status))
        {
            return Fail(status);
        }

        JsonValue skin;
        if (!ParseSkinContract(root, skin, status))
        {
            return Fail(status);
        }

        JsonValue animation;
        if (!ParseAnimationContract(root, animation, status))
        {
            return Fail(status);
        }

        Container::VariableArray<AccessorInfo> accessors;
        Container::VariableArray<BufferViewInfo> bufferViews;
        Container::VariableArray<BufferInfo> buffers;
        Container::VariableArray<Container::VariableArray<uint8_t>> bufferBytes;
        status = SkeletalGltfDecodeStatus::InvalidAccessor;
        if (!ParseAccessors(root, accessors, status))
        {
            return Fail(status);
        }
        if (!ParseBufferViews(root, bufferViews) || !ParseBuffers(root, buffers) ||
            !LoadBuffers(buffers, sourcePath, bufferBytes))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidAccessor);
        }

        SkeletalGltfData data;
        Container::VariableArray<int32_t> nodeToJoint;
        NodeContract nodeContract;
        if (!ParseNodeContract(root, nodeContract))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
        }
        data.MeshNodeGlobalTransform = nodeContract.MeshNodeGlobal;
        if (!ExtractMesh(primitive, accessors, bufferViews, buffers, bufferBytes, data))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidAccessor);
        }
        const size_t skinJointCount = skin.FindMember("joints").GetArraySize();
        for (const SkeletalVertex& vertex : data.Vertices)
        {
            for (const uint32_t jointIndex : vertex.JointIndices)
            {
                if (jointIndex >= skinJointCount)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
                }
            }
        }
        if (!ExtractSkeleton(root, skin, nodeContract, accessors, bufferViews, buffers, bufferBytes, data, nodeToJoint))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
        }
        if (!ExtractAnimation(animation, nodeToJoint, accessors, bufferViews, buffers, bufferBytes, data))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidAnimation);
        }

        SkeletalGltfDecodeResult result;
        result.Status = SkeletalGltfDecodeStatus::Success;
        result.Data = std::move(data);
        if (outSourceBuffers != nullptr)
        {
            *outSourceBuffers = std::move(bufferBytes);
        }
        return result;
    }
} // namespace NorvesLib::Core::Skeletal
