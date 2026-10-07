#include "Resource/SkeletalGltfDecode.h"
#include "Resource/RigGltfImportCapture.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include "Animation/RigV1Types.h"
#include "Animation/RigRootFrame.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Resource/GltfNativePath.h"
#include "Resource/SkeletalLimits.h"
#include "Resource/SkeletalInfluenceAttributes.h"
#include "Resource/SkeletalInfluenceReduction.h"
#include "Resource/SkeletalImportPolicy.h"
#include "Resource/SkeletalCubicBake.h"
#include "Resource/SkeletalSubmeshLayout.h"

#include "Resource/GltfBufferFile.h"
#include "Resource/GltfBufferJson.h"
#include "Resource/GltfDocumentProfile.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/ImportTransform.h"
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
            Container::VariableArray<SkeletalInfluenceSet> InfluenceSets;
            uint32_t MaterialSlot = 0;
            uint32_t VertexCount = 0;
        };

        using MatrixValues = Container::FixedArray<float, 16>;

        struct NodeContract
        {
            Container::VariableArray<int32_t> Parents;
            Container::VariableArray<MatrixValues> Globals;
            Container::VariableArray<uint8_t> Reachable;
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

        bool ParseNodeContract(const JsonValue& root, NodeContract& outContract, bool bRetainFrames = false,
                               bool bClipOnly = false)
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

            if (bClipOnly)
            {
                // clipはmesh-node変換/IBMに依存しない。作者frameだけを同じsceneから得る。
                outContract.Globals = std::move(globals);
                outContract.Reachable = std::move(reachable);
                return true;
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
            if (bRetainFrames)
            {
                outContract.Globals = std::move(globals);
                outContract.Reachable = std::move(reachable);
            }
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

        bool BuildAccessorLayout(const AccessorInfo& accessor,
                                 const Container::VariableArray<BufferViewInfo>& bufferViews,
                                 const Gltf::BufferSet& buffers,
                                 AccessorLayout& outLayout)
        {
            if (accessor.BufferView >= bufferViews.size())
            {
                return false;
            }

            const BufferViewInfo& view = bufferViews[accessor.BufferView];
            if (view.Buffer >= buffers.GetCount())
            {
                return false;
            }

            size_t viewEnd = 0;
            if (!CheckedAdd(view.ByteOffset, view.ByteLength, viewEnd) ||
                viewEnd > buffers.GetDeclaredByteLength(view.Buffer) || viewEnd > buffers.GetBytes(view.Buffer).size())
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
                dataOffset > buffers.GetBytes(view.Buffer).size())
            {
                return false;
            }

            outLayout.Data = buffers.GetBytes(view.Buffer).data() + dataOffset;
            outLayout.Stride = stride;
            outLayout.ElementSize = elementSize;
            return true;
        }

        bool GetAccessor(const Container::VariableArray<AccessorInfo>& accessors,
                         uint32_t index,
                         const Container::String& type,
                         uint32_t componentType,
                         const Container::VariableArray<BufferViewInfo>& bufferViews,
                         const Gltf::BufferSet& buffers,
                         const AccessorInfo*& outAccessor,
                         AccessorLayout& outLayout)
        {
            if (index >= accessors.size())
            {
                return false;
            }
            const AccessorInfo& accessor = accessors[index];
            if (accessor.Type != type || accessor.ComponentType != componentType ||
                !BuildAccessorLayout(accessor, bufferViews, buffers, outLayout))
            {
                return false;
            }
            outAccessor = &accessor;
            return true;
        }

        bool ParsePrimitive(const JsonValue& primitive, PrimitiveInfo& outPrimitive, SkeletalGltfDecodeStatus& outStatus,
            const SkeletalGltfDecodeOptions& options)
        {
            if (!primitive.IsObject())
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidDocument;
                return false;
            }
            if (primitive.HasMember("targets") && options.MorphPolicy == SkeletalMorphPolicy::Reject)
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
            if (options.InfluencePolicy == SkeletalInfluencePolicy::Strict)
            {
                const auto influences = ValidateStrictInfluenceAttributes(attributes);
                if (influences != StrictInfluenceStatus::Success)
                {
                    outStatus = influences == StrictInfluenceStatus::AdditionalSet ?
                        SkeletalGltfDecodeStatus::InfluenceLimitExceeded : SkeletalGltfDecodeStatus::InvalidAccessor;
                    return false;
                }
            }
            else if (CollectSkeletalInfluenceSets(attributes, outPrimitive.InfluenceSets) !=
                InfluenceSetCollectionStatus::Success)
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidAccessor;
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

        Container::String DecimalSlotIndex(uint64_t value)
        {
            char digits[20];
            size_t count = 0;
            do
            {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);
            Container::String result;
            while (count != 0)
            {
                result.push_back(static_cast<Container::String::value_type>(digits[--count]));
            }
            return result;
        }

        bool AssignMaterialSlots(const JsonValue& root, const JsonValue& primitives,
                                 Container::VariableArray<PrimitiveInfo>& descriptions,
                                 Container::VariableArray<SkeletalMaterialSlot>& slots,
                                 Container::VariableArray<uint64_t>* sourceIndices, const RigV1Limits* nameLimits,
                                 bool& bNameLimitExceeded)
        {
            const auto materials = root.FindMember("materials");
            if (root.HasMember("materials") && !materials.IsArray())
            {
                return false;
            }
            Container::VariableArray<uint64_t> sources;
            Container::VariableArray<Container::String> bases;
            uint64_t remainingSlotNames = nameLimits ? nameLimits->MaxStringBytes : UINT64_MAX;
            const auto nameBytes = [](const Container::String& name) -> uint64_t
            {
                const auto measured = Asset::MeasureSkeletalNameEncoding(
                    2, Container::Span<const Container::String::value_type>{name.data(), name.size()});
                return measured.Succeeded() ? measured.ByteCount : UINT64_MAX;
            };
            const auto fits = [&](uint64_t bytes)
            {
                if (nameLimits && (bytes > nameLimits->MaxNameBytes || bytes > remainingSlotNames))
                {
                    bNameLimitExceeded = true;
                    return false;
                }
                return true;
            };
            for (size_t index = 0; index < descriptions.size(); ++index)
            {
                const auto primitive = primitives.GetArrayElement(index);
                uint64_t source = UINT64_MAX; // material省略と、実material[0]を混同しない。
                if (primitive.HasMember("material"))
                {
                    uint32_t material = 0;
                    if (!TryReadUInt32(primitive.FindMember("material"), material) || material >= materials.GetArraySize())
                    {
                        return false;
                    }
                    source = material;
                }
                size_t slot = 0;
                for (; slot < sources.size(); ++slot)
                {
                    if (sources[slot] == source)
                    {
                        break;
                    }
                }
                if (slot == sources.size())
                {
                    if (slots.size() >= MaximumMaterialSlotCount)
                    {
                        return false;
                    }
                    if (nameLimits)
                    {
                        Detail::ObserveSplitAllocation("material_base_copy");
                    }
                    Container::String name;
                    if (source == UINT64_MAX)
                    {
                        name = "Default";
                    }
                    else
                    {
                        const auto material = materials.GetArrayElement(static_cast<size_t>(source));
                        if (!material.IsObject() || (material.HasMember("name") && !material.FindMember("name").IsString()))
                        {
                            return false;
                        }
                        name = material.FindMember("name").AsString();
                        for (const auto character : name)
                        {
                            if (character == 0)
                            {
                                return false;
                            }
                        }
                        if (name.empty())
                        {
                            name = "Material_";
                            name += DecimalSlotIndex(source);
                        }
                    }
                    sources.push_back(source);
                    bases.push_back(std::move(name));
                    slots.push_back({});
                }
                descriptions[index].MaterialSlot = static_cast<uint32_t>(slot);
            }
            // 名前の解決順はsource identity順。primitive順を変えても同名材質の名前を取り違えない。
            Container::FixedArray<size_t, MaximumMaterialSlotCount> order{};
            for (size_t slot = 0; slot < slots.size(); ++slot)
            {
                order[slot] = slot;
            }
            std::sort(order.begin(), order.begin() + slots.size(), [&](size_t a, size_t b) { return sources[a] < sources[b]; });
            for (size_t position = 0; position < slots.size(); ++position)
            {
                const size_t slot = order[position];
                size_t matches = 0;
                for (const auto& name : bases)
                {
                    matches += name == bases[slot];
                }
                if (matches == 1)
                {
                    const auto bytes = nameBytes(bases[slot]);
                    if (!fits(bytes))
                    {
                        return false;
                    }
                    if (nameLimits)
                    {
                        Detail::ObserveSplitAllocation("material_slot_copy");
                        remainingSlotNames -= bytes;
                    }
                    slots[slot].Name = bases[slot];
                    continue;
                }
                const auto sourceSuffix =
                    sources[slot] == UINT64_MAX ? Container::String("default") : DecimalSlotIndex(sources[slot]);
                const uint64_t stemBytes = nameBytes(bases[slot]) + 3 + sourceSuffix.size();
                if (!fits(stemBytes))
                {
                    return false;
                }
                Container::String stem = bases[slot];
                stem += " [";
                stem += sourceSuffix;
                stem += "]";
                bool bAssigned = false;
                for (uint32_t suffix = 0; suffix <= MaximumMaterialSlotCount * 2; ++suffix)
                {
                    const auto suffixText = suffix ? DecimalSlotIndex(suffix) : Container::String{};
                    const uint64_t candidateBytes = stemBytes + (suffix ? 1 + suffixText.size() : 0);
                    if (!fits(candidateBytes))
                    {
                        return false;
                    }
                    auto candidate = stem;
                    if (suffix != 0)
                    {
                        candidate += "_";
                        candidate += suffixText;
                    }
                    bool bConflict = false;
                    for (const auto& name : bases)
                    {
                        bConflict = bConflict || name == candidate;
                    }
                    for (const auto& existing : slots)
                    {
                        bConflict = bConflict || existing.Name == candidate;
                    }
                    if (!bConflict)
                    {
                        if (nameLimits)
                        {
                            Detail::ObserveSplitAllocation("material_slot_copy");
                            remainingSlotNames -= candidateBytes;
                        }
                        slots[slot].Name = std::move(candidate);
                        bAssigned = true;
                        break;
                    }
                }
                if (!bAssigned)
                {
                    return false;
                }
            }
            if (sourceIndices)
            {
                *sourceIndices = std::move(sources);
            }
            return true;
        }

        bool ParsePrimitives(const JsonValue& root, Container::VariableArray<PrimitiveInfo>& outPrimitives,
                             Container::VariableArray<SkeletalMaterialSlot>& outSlots, SkeletalGltfDecodeStatus& status,
                             const SkeletalGltfDecodeOptions& options,
                             Container::VariableArray<uint64_t>* sourceIndices, const RigV1Limits* nameLimits)
        {
            const auto meshes = root.FindMember("meshes");
            if (!meshes.IsArray() || meshes.GetArraySize() != 1)
            {
                status = SkeletalGltfDecodeStatus::UnsupportedMeshCount;
                return false;
            }
            const auto primitives = meshes.GetArrayElement(0).FindMember("primitives");
            if (!primitives.IsArray() || primitives.GetArraySize() == 0)
            {
                status = SkeletalGltfDecodeStatus::UnsupportedPrimitiveCount;
                return false;
            }
            if (primitives.GetArraySize() > MaximumSubmeshCount)
            {
                status = SkeletalGltfDecodeStatus::SubmeshLimitExceeded;
                return false;
            }
            outPrimitives.resize(primitives.GetArraySize());
            for (size_t index = 0; index < outPrimitives.size(); ++index)
            {
                if (!ParsePrimitive(primitives.GetArrayElement(index), outPrimitives[index], status, options))
                {
                    return false;
                }
            }
            // 単一primitiveもsource材質名を保持し、0.2の明示表へ保存する。
            bool bNameLimitExceeded = false;
            if (!AssignMaterialSlots(root, primitives, outPrimitives, outSlots, sourceIndices, nameLimits,
                                     bNameLimitExceeded))
            {
                status = bNameLimitExceeded ? SkeletalGltfDecodeStatus::ImportLimitExceeded
                                            : SkeletalGltfDecodeStatus::InvalidSubMesh;
                return false;
            }
            return true;
        }

        bool ParseSkinContract(const JsonValue& root, JsonValue& outSkin, SkeletalGltfDecodeStatus& outStatus,
                               uint32_t maximumJoints = LegacyMaximumJointCount)
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
            if (joints.GetArraySize() > maximumJoints)
            {
                outStatus = SkeletalGltfDecodeStatus::JointLimitExceeded;
                return false;
            }
            return true;
        }

        bool ParseAnimationContract(const JsonValue& outAnimation, SkeletalGltfDecodeStatus& outStatus,
            const SkeletalGltfDecodeOptions& options)
        {
            const JsonValue samplers = outAnimation.FindMember("samplers");
            const JsonValue channels = outAnimation.FindMember("channels");
            if (!outAnimation.IsObject() || !samplers.IsArray() || samplers.GetArraySize() == 0 ||
                !channels.IsArray() || channels.GetArraySize() == 0)
            {
                outStatus = SkeletalGltfDecodeStatus::InvalidAnimation;
                return false;
            }
            const bool bDrop = options.MorphPolicy == SkeletalMorphPolicy::Drop;
            Container::VariableArray<uint8_t> samplerUsage;
            if (bDrop)
            {
                samplerUsage.assign(samplers.GetArraySize(), 0);
                for (size_t channelIndex = 0; channelIndex < channels.GetArraySize(); ++channelIndex)
                {
                    const auto channel = channels.GetArrayElement(channelIndex);
                    uint32_t samplerIndex = InvalidIndex;
                    if (!TryReadRequiredUInt32(channel, "sampler", samplerIndex) || samplerIndex >= samplers.GetArraySize())
                    {
                        outStatus = SkeletalGltfDecodeStatus::InvalidAnimation;
                        return false;
                    }
                    samplerUsage[samplerIndex] |= channel.FindMember("target").FindMember("path").AsString() == "weights" ? 1 : 2;
                }
            }
            for (size_t index = 0; index < samplers.GetArraySize(); ++index)
            {
                const Container::String& interpolation =
                    samplers.GetArrayElement(index).FindMember("interpolation").AsString();
                const auto sampler = samplers.GetArrayElement(index);
                const bool bBake = options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake;
                const bool bOnlyDroppedWeights = bDrop && samplerUsage[index] == 1;
                if ((bBake || bDrop) && sampler.HasMember("interpolation") &&
                    (!sampler.FindMember("interpolation").IsString() || interpolation.empty()))
                {
                    outStatus = SkeletalGltfDecodeStatus::InvalidAnimation;
                    return false;
                }
                if (!interpolation.empty() && interpolation != "LINEAR" && interpolation != "STEP" &&
                    !((bBake || bOnlyDroppedWeights) && interpolation == "CUBICSPLINE"))
                {
                    outStatus = SkeletalGltfDecodeStatus::UnsupportedInterpolation;
                    return false;
                }
            }
            return true;
        }

        struct InfluenceSetLayout
        {
            const AccessorInfo* Joints = nullptr;
            const AccessorInfo* Weights = nullptr;
            AccessorLayout JointLayout;
            AccessorLayout WeightLayout;
        };

        bool PrepareInfluenceLayouts(const PrimitiveInfo& primitive,
            const Container::VariableArray<AccessorInfo>& accessors,
            const Container::VariableArray<BufferViewInfo>& bufferViews,
            const Gltf::BufferSet& buffers, uint32_t vertexCount,
            Container::VariableArray<InfluenceSetLayout>& layouts)
        {
            const size_t count = primitive.InfluenceSets.size();
            if (count == 0 || count > UINT32_MAX / 4 ||
                count > std::numeric_limits<size_t>::max() / (4 * sizeof(SkinInfluence))) return false;
            layouts.resize(count);
            for (size_t index = 0; index < count; ++index)
            {
                const auto& set = primitive.InfluenceSets[index];
                if (set.JointsAccessor >= accessors.size() || set.WeightsAccessor >= accessors.size()) return false;
                auto& layout = layouts[index];
                layout.Joints = &accessors[set.JointsAccessor];
                layout.Weights = &accessors[set.WeightsAccessor];
                const auto& joints = *layout.Joints;
                const auto& weights = *layout.Weights;
                if (joints.Type != "VEC4" || joints.bNormalized ||
                    (joints.ComponentType != ByteComponent && joints.ComponentType != UnsignedShortComponent) ||
                    weights.Type != "VEC4" ||
                    !((weights.ComponentType == FloatComponent && !weights.bNormalized) ||
                        ((weights.ComponentType == ByteComponent || weights.ComponentType == UnsignedShortComponent) && weights.bNormalized)) ||
                    joints.Count != vertexCount || weights.Count != vertexCount ||
                    joints.ByteOffset % 4 != 0 || weights.ByteOffset % 4 != 0 ||
                    !BuildAccessorLayout(joints, bufferViews, buffers, layout.JointLayout) ||
                    !BuildAccessorLayout(weights, bufferViews, buffers, layout.WeightLayout)) return false;
            }
            return true;
        }

        bool ReduceVertexInfluences(size_t vertexIndex, uint32_t jointCount,
            const Container::VariableArray<InfluenceSetLayout>& layouts,
            const SkeletalGltfDecodeOptions& options, Container::VariableArray<SkinInfluence>& input,
            Container::VariableArray<SkinInfluence>& workspace, SkeletalVertex& vertex,
            SkeletalGltfDecodeReport& report, SkeletalGltfDecodeStatus& status, uint64_t reportVertexOffset)
        {
            report.FailedVertexIndex = reportVertexOffset + vertexIndex;
            uint64_t rawSum65535 = 0;
            bool allInteger = true;
            for (size_t setIndex = 0; setIndex < layouts.size(); ++setIndex)
            {
                const auto& set = layouts[setIndex];
                const uint8_t* joints = set.JointLayout.Data + vertexIndex * set.JointLayout.Stride;
                const uint8_t* weights = set.WeightLayout.Data + vertexIndex * set.WeightLayout.Stride;
                for (size_t slot = 0; slot < 4; ++slot)
                {
                    auto& influence = input[setIndex * 4 + slot];
                    influence.JointIndex = ReadUnsignedComponent(joints + slot * GetComponentSize(set.Joints->ComponentType), set.Joints->ComponentType);
                    // ゼロweightや後で落とす影響も、元データのjoint範囲は必ず検査する。
                    if (influence.JointIndex >= jointCount)
                    {
                        status = SkeletalGltfDecodeStatus::InvalidSkeleton;
                        return false;
                    }
                    const uint8_t* value = weights + slot * GetComponentSize(set.Weights->ComponentType);
                    if (set.Weights->ComponentType == FloatComponent)
                    {
                        allInteger = false;
                        influence.Weight = ReadFloat(value);
                    }
                    else
                    {
                        const uint32_t raw = ReadUnsignedComponent(value, set.Weights->ComponentType);
                        const uint32_t factor = set.Weights->ComponentType == ByteComponent ? 257 : 1;
                        rawSum65535 += uint64_t(raw) * factor;
                        influence.Weight = double(raw) / (set.Weights->ComponentType == ByteComponent ? 255.0 : 65535.0);
                    }
                }
            }
            // 全て整数なら共通分母で厳密検査。FLOATを含む場合はkernelの総和許容を使う。
            if (allInteger && rawSum65535 != 65535) return false;
            InfluenceReductionOptions reduction;
            reduction.WarnDroppedWeight = options.WarnDroppedWeight;
            reduction.FailDroppedWeight = options.FailDroppedWeight;
            ReducedSkinInfluences reduced;
            const auto outcome = ReduceSkinInfluences({input.data(), input.size()}, jointCount, reduction,
                {workspace.data(), workspace.size()}, reduced);
            if (outcome.Status != InfluenceReductionStatus::Success)
            {
                if (outcome.Status == InfluenceReductionStatus::DroppedWeightExceeded)
                {
                    status = SkeletalGltfDecodeStatus::InfluenceReductionExceeded;
                    report.FailedVertexDroppedWeight = outcome.DroppedWeight;
                    report.bHasFailedVertexDroppedWeight = true;
                }
                return false;
            }
            for (size_t slot = 0; slot < 4; ++slot)
            {
                vertex.JointIndices[slot] = reduced.Joints[slot];
                vertex.JointWeights[slot] = reduced.Weights[slot];
            }
            ++report.ProcessedVertexCount;
            report.ReducedVertexCount += outcome.bReduced;
            report.MergedJointVertexCount += outcome.OriginalNonzeroCount > outcome.UniqueNonzeroCount;
            report.RenormalizedVertexCount += outcome.bRenormalized;
            report.WarningVertexCount += outcome.bWarning;
            report.MaximumDroppedWeight = std::max(report.MaximumDroppedWeight, outcome.DroppedWeight);
            report.MeanDroppedWeight += (outcome.DroppedWeight - report.MeanDroppedWeight) / double(report.ProcessedVertexCount);
            report.FailedVertexIndex = std::numeric_limits<uint64_t>::max();
            return true;
        }

        bool ExtractMesh(const PrimitiveInfo& primitive,
                         const Container::VariableArray<AccessorInfo>& accessors,
                         const Container::VariableArray<BufferViewInfo>& bufferViews,
                         const Gltf::BufferSet& buffers,
                         SkeletalGltfData& outData, uint32_t jointCount,
                         const SkeletalGltfDecodeOptions& options, SkeletalGltfDecodeReport& report,
                         SkeletalGltfDecodeStatus& status)
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

            if (!GetAccessor(accessors, primitive.Position, "VEC3", FloatComponent, bufferViews, buffers,
                             position, positionLayout) ||
                !GetAccessor(accessors, primitive.Normal, "VEC3", FloatComponent, bufferViews, buffers,
                             normal, normalLayout) ||
                !GetAccessor(accessors, primitive.TexCoord, "VEC2", FloatComponent, bufferViews, buffers,
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
                !BuildAccessorLayout(*joints, bufferViews, buffers, jointsLayout) ||
                !BuildAccessorLayout(*weights, bufferViews, buffers, weightsLayout) ||
                !BuildAccessorLayout(*indices, bufferViews, buffers, indexLayout) ||
                position->Count == 0 || normal->Count != position->Count || texCoord->Count != position->Count ||
                joints->Count != position->Count || weights->Count != position->Count ||
                indices->Count == 0 || indices->Count % 3 != 0)
            {
                return false;
            }

            const bool reduce = options.InfluencePolicy == SkeletalInfluencePolicy::ReduceToFour;
            Container::VariableArray<InfluenceSetLayout> influenceLayouts;
            Container::VariableArray<SkinInfluence> inputInfluences;
            Container::VariableArray<SkinInfluence> influenceWorkspace;
            if (reduce)
            {
                if (!PrepareInfluenceLayouts(primitive, accessors, bufferViews, buffers, position->Count, influenceLayouts)) return false;
                inputInfluences.resize(influenceLayouts.size() * 4);
                influenceWorkspace.resize(inputInfluences.size());
            }
            const size_t baseVertex = outData.Vertices.size();
            const size_t baseIndex = outData.Indices.size();
            if (position->Count > UINT32_MAX - uint64_t(baseVertex) || indices->Count > UINT32_MAX - uint64_t(baseIndex) ||
                uint64_t(baseVertex) + position->Count > std::numeric_limits<size_t>::max() / sizeof(SkeletalVertex) ||
                uint64_t(baseIndex) + indices->Count > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
            {
                status = SkeletalGltfDecodeStatus::InvalidSubMesh;
                return false;
            }
            outData.Vertices.resize(baseVertex + position->Count);
            for (size_t vertexIndex = 0; vertexIndex < position->Count; ++vertexIndex)
            {
                SkeletalVertex& vertex = outData.Vertices[baseVertex + vertexIndex];
                const uint8_t* positionData = positionLayout.Data + vertexIndex * positionLayout.Stride;
                const uint8_t* normalData = normalLayout.Data + vertexIndex * normalLayout.Stride;
                const uint8_t* texCoordData = texCoordLayout.Data + vertexIndex * texCoordLayout.Stride;
                const uint8_t* jointData = jointsLayout.Data + vertexIndex * jointsLayout.Stride;
                const uint8_t* weightData = weightsLayout.Data + vertexIndex * weightsLayout.Stride;
                vertex.Position = {ReadFloat(positionData), ReadFloat(positionData + 4), ReadFloat(positionData + 8)};
                vertex.Normal = {ReadFloat(normalData), ReadFloat(normalData + 4), ReadFloat(normalData + 8)};
                vertex.TexCoord = {ReadFloat(texCoordData), ReadFloat(texCoordData + 4)};
                if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                    !std::isfinite(vertex.Position.Z) || !std::isfinite(vertex.Normal.X) ||
                    !std::isfinite(vertex.Normal.Y) || !std::isfinite(vertex.Normal.Z) ||
                    !std::isfinite(vertex.TexCoord.U) || !std::isfinite(vertex.TexCoord.V))
                {
                    return false;
                }
                if (reduce)
                {
                    if (!ReduceVertexInfluences(vertexIndex, jointCount, influenceLayouts, options,
                        inputInfluences, influenceWorkspace, vertex, report, status, baseVertex)) return false;
                }
                else
                {
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
            }

            if (reduce) report.bInfluenceScanComplete = report.ProcessedVertexCount == report.TotalVertexCount;
            outData.Indices.resize(baseIndex + indices->Count);
            const size_t indexComponentSize = GetComponentSize(indices->ComponentType);
            for (size_t index = 0; index < indices->Count; ++index)
            {
                const uint32_t localIndex = ReadUnsignedComponent(indexLayout.Data + index * indexLayout.Stride, indices->ComponentType);
                if (localIndex >= position->Count)
                {
                    return false;
                }
                outData.Indices[baseIndex + index] = static_cast<uint32_t>(baseVertex + localIndex);
            }
            for (size_t index = baseIndex; index < outData.Indices.size(); index += 3)
            {
                std::swap(outData.Indices[index + 1], outData.Indices[index + 2]);
            }
            return indexComponentSize != 0;
        }

        // joint間をつなぐ非jointは許さず、唯一rootの上だけ静的similarityを保持する。
        bool ExtractStaticRootFrame(const JsonValue& root, const JsonValue& skin, const NodeContract& contract,
                                    const Container::VariableArray<int32_t>& nodeToJoint, SkeletalGltfData& data,
                                    RigRootFrame& outFrame)
        {
            const auto nodes = root.FindMember("nodes");
            const size_t count = nodes.GetArraySize();
            if (contract.Globals.size() != count || contract.Reachable.size() != count)
            {
                return false;
            }
            uint32_t rootNode = InvalidIndex;
            size_t roots = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (nodeToJoint[i] < 0)
                {
                    continue;
                }
                if (!contract.Reachable[i])
                {
                    return false;
                }
                const auto parent = contract.Parents[i];
                const auto parentJoint = parent < 0 ? -1 : nodeToJoint[size_t(parent)];
                data.Joints[size_t(nodeToJoint[i])].ParentIndex = parentJoint;
                if (parentJoint < 0)
                {
                    ++roots;
                    rootNode = uint32_t(i);
                }
            }
            if (roots != 1 || rootNode == InvalidIndex)
            {
                return false;
            }
            const auto rootJoint = nodeToJoint[rootNode];
            for (size_t i = 0; i < data.Joints.size(); ++i)
            {
                int32_t ancestor = int32_t(i);
                size_t depth = 0;
                while (ancestor != rootJoint)
                {
                    if (ancestor < 0 || depth++ >= data.Joints.size())
                    {
                        return false;
                    }
                    ancestor = data.Joints[size_t(ancestor)].ParentIndex;
                }
            }
            if (skin.HasMember("skeleton"))
            {
                uint32_t hint = InvalidIndex;
                if (!TryReadRequiredUInt32(skin, "skeleton", hint) || hint >= count)
                {
                    return false;
                }
                int32_t current = int32_t(rootNode);
                size_t depth = 0;
                while (current >= 0 && uint32_t(current) != hint && depth++ < count)
                {
                    current = contract.Parents[size_t(current)];
                }
                if (current < 0 || uint32_t(current) != hint)
                {
                    return false;
                }
            }
            Container::VariableArray<uint8_t> ancestors(count, 0);
            int32_t parent = contract.Parents[rootNode];
            size_t depth = 0;
            while (parent >= 0)
            {
                const size_t i = size_t(parent);
                if (i >= count || depth++ >= count || nodeToJoint[i] >= 0 || !contract.Reachable[i])
                {
                    return false;
                }
                ancestors[i] = 1;
                const auto node = nodes.GetArrayElement(i);
                if (node.HasMember("matrix"))
                {
                    return false;
                }
                float t[3]{0, 0, 0}, q[4]{0, 0, 0, 1}, scale[3]{1, 1, 1};
                if ((node.HasMember("translation") && !ReadFloatArray(node.FindMember("translation"), 3, t)) ||
                    (node.HasMember("rotation") && !ReadFloatArray(node.FindMember("rotation"), 4, q)) ||
                    (node.HasMember("scale") && !ReadFloatArray(node.FindMember("scale"), 3, scale)))
                {
                    return false;
                }
                const SkeletalRestTransform transform{
                    {t[0], t[1], t[2]}, {q[0], q[1], q[2], q[3]}, {scale[0], scale[1], scale[2]}};
                if (!IsValidSkeletalRestTransform(transform) || scale[0] != scale[1] || scale[0] != scale[2])
                {
                    return false;
                }
                parent = contract.Parents[i];
            }
            const auto animations = root.FindMember("animations");
            for (size_t a = 0; a < animations.GetArraySize(); ++a)
            {
                const auto channels = animations.GetArrayElement(a).FindMember("channels");
                if (!channels.IsArray())
                {
                    return false;
                }
                for (size_t c = 0; c < channels.GetArraySize(); ++c)
                {
                    uint32_t target = InvalidIndex;
                    if (!TryReadRequiredUInt32(channels.GetArrayElement(c).FindMember("target"), "node", target) ||
                        target >= count || ancestors[target])
                    {
                        return false;
                    }
                }
            }
            const int32_t rootParent = contract.Parents[rootNode];
            auto frame = rootParent < 0 ? IdentityRigRootFrame() : contract.Globals[size_t(rootParent)];
            CanonicalizeRigRootFrameZero(frame);
            if (!IsValidRigRootFrame(frame, RigImportProfile::StaticRootFrame128))
            {
                return false;
            }
            outFrame = frame;
            return true;
        }

        bool ExtractSkeleton(const JsonValue& root, const JsonValue& skin, const NodeContract& nodeContract,
                             const Container::VariableArray<AccessorInfo>& accessors,
                             const Container::VariableArray<BufferViewInfo>& bufferViews,
                             const Gltf::BufferSet& buffers, SkeletalGltfData& outData,
                             Container::VariableArray<int32_t>& outNodeToJoint,
                             RigImportProfile profile = RigImportProfile::DirectTrs128,
                             RigRootFrame* outFrame = nullptr, Container::Span<const uint32_t> clipJoints = {})
        {
            const JsonValue nodes = root.FindMember("nodes");
            const JsonValue jointValues = skin.FindMember("joints");
            if (!nodes.IsArray())
            {
                return false;
            }

            if (!clipJoints.empty())
            {
                // clip専用ではIBMを作らない。名前・親子関係・作者restだけを保持する。
                outNodeToJoint.assign(nodes.GetArraySize(), -1);
                outData.Joints.resize(clipJoints.size());
                for (size_t i = 0; i < clipJoints.size(); ++i)
                {
                    const uint32_t node = clipJoints[i];
                    if (node >= nodes.GetArraySize() || outNodeToJoint[node] >= 0)
                    {
                        return false;
                    }
                    outNodeToJoint[node] = static_cast<int32_t>(i);
                    outData.Joints[i].Name = nodes.GetArrayElement(node).FindMember("name").AsString();
                }
                return IsStaticRootFrameProfile(profile) && outFrame &&
                       ExtractStaticRootFrame(root, skin, nodeContract, outNodeToJoint, outData, *outFrame);
            }

            uint32_t inverseBindAccessorIndex = InvalidIndex;
            const AccessorInfo* inverseBind = nullptr;
            AccessorLayout inverseBindLayout;
            if (!TryReadRequiredUInt32(skin, "inverseBindMatrices", inverseBindAccessorIndex) ||
                !GetAccessor(accessors, inverseBindAccessorIndex, "MAT4", FloatComponent, bufferViews, buffers, inverseBind, inverseBindLayout) ||
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

            if (IsStaticRootFrameProfile(profile))
            {
                return outFrame && ExtractStaticRootFrame(root, skin, nodeContract, outNodeToJoint, outData, *outFrame);
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

        bool FailCubicBake(CubicBakeStatus reason, SkeletalGltfDecodeReport& report, SkeletalGltfDecodeStatus& status)
        {
            report.bHasCubicBakeFailure = true;
            report.FailedCubicBakeStatus = static_cast<uint32_t>(reason);
            status = SkeletalGltfDecodeStatus::CubicBakeFailed;
            return false;
        }

        bool ExtractCubicChannel(const AccessorInfo& input, const AccessorLayout& inputLayout,
            const AccessorInfo& output, const AccessorLayout& outputLayout, size_t components,
            const SkeletalGltfDecodeOptions& options, double translationScale,
            SkeletalAnimationChannel& channel, SkeletalGltfDecodeReport& report, SkeletalGltfDecodeStatus& status)
        {
            if (input.Count < 2 || input.Count > UINT32_MAX / 3 || output.Count != input.Count * 3)
            {
                return false;
            }
            if (report.CubicOutputKeyCount > options.CubicMaximumSamplesPerAsset)
            {
                return FailCubicBake(CubicBakeStatus::SampleLimitExceeded, report, status);
            }
            const uint64_t remaining = options.CubicMaximumSamplesPerAsset - report.CubicOutputKeyCount;
            const uint32_t maximum = static_cast<uint32_t>(std::min(uint64_t(options.CubicMaximumSamplesPerChannel), remaining));
            if (input.Count > maximum || maximum < 2)
            {
                return FailCubicBake(CubicBakeStatus::SampleLimitExceeded, report, status);
            }
            Container::VariableArray<CubicBakeInputKey> keys(input.Count);
            for (size_t index = 0; index < input.Count; ++index)
            {
                auto& key = keys[index];
                key.Time = ReadFloat(inputLayout.Data + index * inputLayout.Stride);
                CubicFloatPoint* triplet[] = {&key.Incoming, &key.Value, &key.Outgoing};
                for (size_t part = 0; part < 3; ++part)
                {
                    const uint8_t* bytes = outputLayout.Data + (index * 3 + part) * outputLayout.Stride;
                    for (size_t component = 0; component < components; ++component)
                    {
                        triplet[part]->Values[component] = ReadFloat(bytes + component * sizeof(float));
                    }
                }
            }
            CubicBakeOptions bake;
            bake.Kind = channel.Path == SkeletalAnimationPath::Rotation ? CubicBakeKind::Rotation : CubicBakeKind::Vector3;
            bake.Tolerance = channel.Path == SkeletalAnimationPath::Translation ? options.CubicTranslationToleranceMeters :
                channel.Path == SkeletalAnimationPath::Rotation ? options.CubicRotationToleranceRadians : options.CubicScaleTolerance;
            bake.ValueScale = channel.Path == SkeletalAnimationPath::Translation ? translationScale : 1;
            bake.MaximumDepth = options.CubicMaximumDepth;
            bake.MaximumSamples = maximum;
            Container::VariableArray<CubicBakeKey> workspace(maximum), baked(maximum);
            const auto result = BakeCubicChannel({keys.data(), keys.size()}, bake,
                {workspace.data(), workspace.size()}, {baked.data(), baked.size()});
            if (result.Status != CubicBakeStatus::Success)
            {
                return FailCubicBake(result.Status, report, status);
            }
            channel.Samples.resize(result.SampleCount);
            for (size_t index = 0; index < result.SampleCount; ++index)
            {
                const auto& key = baked[index];
                channel.Samples[index].TimeSeconds = key.Time;
                channel.Samples[index].Value = {key.Value.Values[0], key.Value.Values[1], key.Value.Values[2], key.Value.Values[3]};
            }
            ++report.BakedCubicChannelCount;
            uint64_t* kindCount = channel.Path == SkeletalAnimationPath::Translation ? &report.BakedCubicTranslationChannelCount :
                channel.Path == SkeletalAnimationPath::Rotation ? &report.BakedCubicRotationChannelCount : &report.BakedCubicScaleChannelCount;
            ++*kindCount;

            report.CubicInputKeyCount += input.Count;
            report.CubicOutputKeyCount += result.SampleCount;
            double* maximumError = channel.Path == SkeletalAnimationPath::Translation ? &report.MaximumCubicTranslationErrorMeters :
                channel.Path == SkeletalAnimationPath::Rotation ? &report.MaximumCubicRotationErrorRadians : &report.MaximumCubicScaleError;
            *maximumError = std::max(*maximumError, result.MaximumAcceptedErrorUpper);
            return true;
        }

        bool HasDuplicateMember(const JsonValue& object, const char* name)
        {
            bool bFound = false;
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                if (object.GetMemberName(index) == name)
                {
                    if (bFound)
                    {
                        return true;
                    }
                    bFound = true;
                }
            }
            return false;
        }

        bool ValidateMorphFloatValues(const AccessorInfo& accessor, const AccessorLayout& layout)
        {
            if (accessor.bNormalized || accessor.ComponentType != FloatComponent)
            {
                return false;
            }
            const size_t components = GetComponentCount(accessor.Type);
            for (size_t index = 0; index < accessor.Count; ++index)
            {
                for (size_t component = 0; component < components; ++component)
                {
                    if (!std::isfinite(ReadFloat(layout.Data + index * layout.Stride + component * sizeof(float))))
                    {
                        return false;
                    }
                }
            }
            return components != 0;
        }

        bool ValidateMorphWeights(const JsonValue& owner, uint32_t targetCount, uint64_t& count)
        {
            if (!owner.HasMember("weights"))
            {
                return true;
            }
            const auto weights = owner.FindMember("weights");
            if (HasDuplicateMember(owner, "weights") || targetCount == 0 || !weights.IsArray() || weights.GetArraySize() != targetCount)
            {
                return false;
            }
            for (size_t index = 0; index < weights.GetArraySize(); ++index)
            {
                const auto weight = weights.GetArrayElement(index);
                if (!weight.IsNumber() || !std::isfinite(weight.AsNumber()) || std::abs(weight.AsNumber()) > std::numeric_limits<float>::max())
                {
                    return false;
                }
            }
            count = weights.GetArraySize();
            return true;
        }

        bool ValidatePrimitiveMorphTargets(const JsonValue& root, const JsonValue& primitive, uint32_t vertexCount,
            const Container::VariableArray<AccessorInfo>& accessors, const Container::VariableArray<BufferViewInfo>& views,
            const Gltf::BufferSet& buffers, uint32_t& targetWidth, uint64_t& totalTargets)
        {
            const auto attributes = primitive.FindMember("attributes");
            const auto targets = primitive.FindMember("targets");
            uint32_t targetCount = 0;
            if (primitive.HasMember("targets"))
            {
                if (HasDuplicateMember(primitive, "targets") || !targets.IsArray() || targets.GetArraySize() == 0 || targets.GetArraySize() > UINT32_MAX)
                {
                    return false;
                }
                targetCount = static_cast<uint32_t>(targets.GetArraySize());
                if (targetWidth != 0 && targetWidth != targetCount)
                {
                    return false;
                }
                targetWidth = targetCount;
            }
            for (size_t index = 0; index < targetCount; ++index)
            {
                const auto target = targets.GetArrayElement(index);
                if (!target.IsObject() || target.GetObjectSize() == 0)
                {
                    return false;
                }
                uint32_t seen = 0;
                for (size_t member = 0; member < target.GetObjectSize(); ++member)
                {
                    const auto& name = target.GetMemberName(member);
                    const char* semantic = nullptr;
                    const char* baseType = "VEC3";
                    uint32_t bit = 0;
                    if (name == "POSITION")
                    {
                        semantic = "POSITION"; bit = 1;
                    }
                    else if (name == "NORMAL")
                    {
                        semantic = "NORMAL"; bit = 2;
                    }
                    else if (name == "TANGENT")
                    {
                        semantic = "TANGENT"; baseType = "VEC4"; bit = 4;
                    }
                    else
                    {
                        // 現profileでは拡張/色/UV等のmorphは未対応として拒否する。
                        return false;
                    }
                    uint32_t targetIndex = InvalidIndex, baseIndex = InvalidIndex;
                    const AccessorInfo* delta = nullptr;
                    const AccessorInfo* base = nullptr;
                    AccessorLayout deltaLayout, baseLayout;
                    if ((seen & bit) != 0 || !TryReadUInt32(target.GetMemberValue(member), targetIndex) ||
                        !TryReadRequiredUInt32(attributes, semantic, baseIndex) ||
                        !GetAccessor(accessors, targetIndex, "VEC3", FloatComponent, views, buffers, delta, deltaLayout) ||
                        !GetAccessor(accessors, baseIndex, baseType, FloatComponent, views, buffers, base, baseLayout) ||
                        delta->Count != vertexCount || base->Count != vertexCount ||
                        !ValidateMorphFloatValues(*delta, deltaLayout) || !ValidateMorphFloatValues(*base, baseLayout))
                    {
                        return false;
                    }
                    seen |= bit;
                    if (bit == 1)
                    {
                        const auto raw = root.FindMember("accessors").GetArrayElement(targetIndex);
                        const auto minimum = raw.FindMember("min"), maximum = raw.FindMember("max");
                        if (!minimum.IsArray() || !maximum.IsArray() || minimum.GetArraySize() != 3 || maximum.GetArraySize() != 3)
                        {
                            return false;
                        }
                        for (size_t component = 0; component < 3; ++component)
                        {
                            const auto lo = minimum.GetArrayElement(component), hi = maximum.GetArrayElement(component);
                            if (!lo.IsNumber() || !hi.IsNumber() || !std::isfinite(lo.AsNumber()) || !std::isfinite(hi.AsNumber()) || lo.AsNumber() > hi.AsNumber())
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            totalTargets += targetCount;
            return true;
        }

        bool ValidateMorphDropMesh(const JsonValue& root, const NodeContract& nodes,
            const Container::VariableArray<AccessorInfo>& accessors, const Container::VariableArray<BufferViewInfo>& views,
            const Gltf::BufferSet& buffers, Container::Span<const PrimitiveInfo> descriptions, SkeletalGltfDecodeReport& report)
        {
            uint32_t targetCount = 0;
            const auto mesh = root.FindMember("meshes").GetArrayElement(0);
            const auto primitiveValues = mesh.FindMember("primitives");
            uint64_t totalTargets = 0;
            for (size_t index = 0; index < descriptions.size(); ++index)
            {
                if (!ValidatePrimitiveMorphTargets(root, primitiveValues.GetArrayElement(index), descriptions[index].VertexCount,
                    accessors, views, buffers, targetCount, totalTargets))
                {
                    return false;
                }
            }
            uint64_t meshWeights = 0, nodeWeights = 0;
            if (!ValidateMorphWeights(mesh, targetCount, meshWeights))
            {
                return false;
            }
            const auto allNodes = root.FindMember("nodes");
            for (size_t index = 0; index < allNodes.GetArraySize(); ++index)
            {
                const auto node = allNodes.GetArrayElement(index);
                if (node.HasMember("weights"))
                {
                    if (index != nodes.MeshNodeIndex || !ValidateMorphWeights(node, targetCount, nodeWeights))
                    {
                        return false;
                    }
                }
            }
            report.DroppedMorphTargetCount = totalTargets;
            report.MorphTargetWidth = targetCount;
            report.DroppedMorphMeshWeightCount = meshWeights;
            report.DroppedMorphNodeWeightCount = nodeWeights;
            return true;
        }

        bool ValidateMorphDrop(const JsonValue& root, const JsonValue& animation, const NodeContract& nodes,
            const Container::VariableArray<AccessorInfo>& accessors, const Container::VariableArray<BufferViewInfo>& views,
            const Gltf::BufferSet& buffers, Container::Span<const PrimitiveInfo> descriptions, SkeletalGltfDecodeReport& report, bool validateMesh)
        {
            uint32_t targetCount = static_cast<uint32_t>(report.MorphTargetWidth);
            if (validateMesh)
            {
                if (!ValidateMorphDropMesh(root, nodes, accessors, views, buffers, descriptions, report))
                {
                    return false;
                }
                targetCount = static_cast<uint32_t>(report.MorphTargetWidth);
            }
            const auto channels = animation.FindMember("channels"), samplers = animation.FindMember("samplers");
            uint64_t weightChannels = 0;
            for (size_t index = 0; index < channels.GetArraySize(); ++index)
            {
                const auto channel = channels.GetArrayElement(index);
                const auto target = channel.FindMember("target");
                if (target.FindMember("path").AsString() != "weights")
                {
                    continue;
                }
                uint32_t nodeIndex = InvalidIndex, samplerIndex = InvalidIndex, inputIndex = InvalidIndex, outputIndex = InvalidIndex;
                if (weightChannels != 0 || targetCount == 0 || !TryReadRequiredUInt32(target, "node", nodeIndex) || nodeIndex != nodes.MeshNodeIndex ||
                    !TryReadRequiredUInt32(channel, "sampler", samplerIndex) || samplerIndex >= samplers.GetArraySize())
                {
                    return false;
                }
                const auto sampler = samplers.GetArrayElement(samplerIndex);
                const auto interpolation = sampler.FindMember("interpolation").AsString();
                const bool bCubic = interpolation == "CUBICSPLINE";
                const AccessorInfo* input = nullptr;
                const AccessorInfo* output = nullptr;
                AccessorLayout inputLayout, outputLayout;
                if (!TryReadRequiredUInt32(sampler, "input", inputIndex) || !TryReadRequiredUInt32(sampler, "output", outputIndex) ||
                    !GetAccessor(accessors, inputIndex, "SCALAR", FloatComponent, views, buffers, input, inputLayout) ||
                    !GetAccessor(accessors, outputIndex, "SCALAR", FloatComponent, views, buffers, output, outputLayout) ||
                    input->Count < (bCubic ? 2u : 1u) || !ValidateMorphFloatValues(*input, inputLayout) || !ValidateMorphFloatValues(*output, outputLayout))
                {
                    return false;
                }
                // u32同士の積をまずu64へ拡張し、triplet乗算前に出力count上限を確認する。
                const uint64_t values = uint64_t(input->Count) * targetCount;
                const uint32_t factor = bCubic ? 3 : 1;
                if (values > UINT32_MAX / factor || output->Count != values * factor)
                {
                    return false;
                }
                float previous = -1;
                for (size_t key = 0; key < input->Count; ++key)
                {
                    const float time = ReadFloat(inputLayout.Data + key * inputLayout.Stride);
                    if (time < 0 || time <= previous)
                    {
                        return false;
                    }
                    previous = time;
                }
                ++weightChannels;
            }
            report.DroppedMorphAnimationChannelCount += weightChannels;
            return true;
        }

        bool ExtractAnimation(const JsonValue& animation, const Container::VariableArray<int32_t>& nodeToJoint,
                              const Container::VariableArray<AccessorInfo>& accessors,
                              const Container::VariableArray<BufferViewInfo>& bufferViews,
                              const Gltf::BufferSet& buffers, SkeletalGltfData& outData,
                              const SkeletalGltfDecodeOptions& options, double translationScale,
                              SkeletalGltfDecodeReport& report, SkeletalGltfDecodeStatus& status,
                              uint64_t* remainingSamples = nullptr)
        {
            const JsonValue samplers = animation.FindMember("samplers");
            const JsonValue channels = animation.FindMember("channels");
            const bool bBake = options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake;
            SkeletalAnimationClip clip;
            clip.Name = animation.FindMember("name").AsString();
            clip.Channels.reserve(channels.GetArraySize());
            Container::VariableArray<uint8_t> animatedPaths(nodeToJoint.size() * 3, 0);

            for (size_t channelIndex = 0; channelIndex < channels.GetArraySize(); ++channelIndex)
            {
                if (bBake)
                {
                    report.FailedAnimationChannelIndex = report.ProcessedAnimationChannelCount;
                }
                const JsonValue channelValue = channels.GetArrayElement(channelIndex);
                const JsonValue target = channelValue.FindMember("target");
                if (options.MorphPolicy == SkeletalMorphPolicy::Drop && target.FindMember("path").AsString() == "weights")
                {
                    if (bBake)
                    {
                        ++report.ProcessedAnimationChannelCount;
                        report.FailedAnimationChannelIndex = UINT64_MAX;
                    }
                    continue;
                }
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
                    !GetAccessor(accessors, inputIndex, "SCALAR", FloatComponent, bufferViews, buffers,
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
                if (!GetAccessor(accessors, outputIndex, outputType, FloatComponent, bufferViews, buffers,
                                 output, outputLayout) ||
                    input->Count == 0)
                {
                    return false;
                }

                if (remainingSamples && input->Count > *remainingSamples)
                {
                    status = SkeletalGltfDecodeStatus::ImportLimitExceeded;
                    return false;
                }
                if (interpolation == "CUBICSPLINE")
                {
                    auto boundedOptions = options;
                    if (remainingSamples)
                    {
                        boundedOptions.CubicMaximumSamplesPerChannel = static_cast<uint32_t>(
                            std::min(uint64_t(options.CubicMaximumSamplesPerChannel), *remainingSamples));
                        boundedOptions.CubicMaximumSamplesPerAsset =
                            static_cast<uint32_t>(std::min(uint64_t(options.CubicMaximumSamplesPerAsset),
                                                           report.CubicOutputKeyCount + *remainingSamples));
                    }
                    if (!bBake || !ExtractCubicChannel(*input, inputLayout, *output, outputLayout, valueComponentCount,
                                                       boundedOptions, translationScale, channel, report, status))
                    {
                        return false;
                    }
                    clip.DurationSeconds = std::max(clip.DurationSeconds, channel.Samples.back().TimeSeconds);
                }
                else
                {
                    if (output->Count != input->Count)
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
                        if (bBake && channel.Path == SkeletalAnimationPath::Translation &&
                            (!AssetImport::TryScaleImportValue(sample.Value.X, translationScale, sample.Value.X) ||
                             !AssetImport::TryScaleImportValue(sample.Value.Y, translationScale, sample.Value.Y) ||
                             !AssetImport::TryScaleImportValue(sample.Value.Z, translationScale, sample.Value.Z)))
                        {
                            status = SkeletalGltfDecodeStatus::InvalidDocument;
                            return false;
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
                }
                if (remainingSamples)
                {
                    if (channel.Samples.size() > *remainingSamples)
                    {
                        status = SkeletalGltfDecodeStatus::ImportLimitExceeded;
                        return false;
                    }
                    *remainingSamples -= channel.Samples.size();
                }
                clip.Channels.push_back(std::move(channel));
                if (bBake)
                {
                    ++report.ProcessedAnimationChannelCount;
                    report.FailedAnimationChannelIndex = UINT64_MAX;
                }
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

    namespace
    {
        bool ResolveSkeletalImportScale(const SkeletalGltfData& data, const AssetImport::ImportSettings& settings,
                                        double& outScale, bool bClipOnly = false)
        {
            using namespace AssetImport;
            if (!SupportsSkeletalScaleImport(settings) ||
                (data.Vertices.empty() && (!bClipOnly || settings.Fit != FitAxis::None)))
            {
                return false;
            }
            double minimum[3] = {}, maximum[3] = {};
            if (settings.Fit != FitAxis::None)
            {
                // fitはasset内のmesh-node線形変換後の長さを使う。平行移動はextentに影響しない。
                const auto& matrix = data.MeshNodeGlobalTransform;
                for (size_t index = 0; index < data.Vertices.size(); ++index)
                {
                    const auto& position = data.Vertices[index].Position;
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const double value = static_cast<double>(matrix[axis]) * position.X +
                            static_cast<double>(matrix[4 + axis]) * position.Y +
                            static_cast<double>(matrix[8 + axis]) * position.Z;
                        if (!std::isfinite(value))
                        {
                            return false;
                        }
                        if (index == 0)
                        {
                            minimum[axis] = maximum[axis] = value;
                        }
                        else
                        {
                            minimum[axis] = std::min(minimum[axis], value);
                            maximum[axis] = std::max(maximum[axis], value);
                        }
                    }
                }
            }
            const auto resolved = ResolveUniformImportScale(settings, minimum, maximum);
            if (resolved.Result != TransformResult::Success)
            {
                return false;
            }
            outScale = resolved.Value;
            return true;
        }

        bool ApplyResolvedSkeletalImport(SkeletalGltfData& data, double factor, bool bScaleAnimation)
        {
            using namespace AssetImport;
            const auto scale = [&](float& value)
            {
                return TryScaleImportValue(value, factor, value);
            };
            // dataはdecoder内の未公開candidate。途中失敗時も外部へ部分適用を返さない。
            for (auto& vertex : data.Vertices)
            {
                if (!scale(vertex.Position.X) || !scale(vertex.Position.Y) || !scale(vertex.Position.Z))
                {
                    return false;
                }
            }
            for (auto& joint : data.Joints)
            {
                for (size_t axis = 12; axis < 15; ++axis)
                {
                    if (!scale(joint.InverseBindMatrix[axis]))
                    {
                        return false;
                    }
                }
            }
            if (bScaleAnimation)
            {
                for (auto& clip : data.Clips)
                {
                    for (auto& channel : clip.Channels)
                    {
                        if (channel.Path != SkeletalAnimationPath::Translation)
                        {
                            continue;
                        }
                        for (auto& sample : channel.Samples)
                        {
                            if (!scale(sample.Value.X) || !scale(sample.Value.Y) || !scale(sample.Value.Z))
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            for (size_t axis = 12; axis < 15; ++axis)
            {
                if (!scale(data.MeshNodeGlobalTransform[axis]))
                {
                    return false;
                }
            }
            return true;
        }

        // 新v1だけの割当前budget。legacyの受理・拒否とreader既定は変更しない。
        bool CheckRigInputBudget(const JsonValue& root, const RigV1Limits& limits, uint64_t& reservedBufferBytes,
                                 SkeletalGltfDecodeStatus& status, bool bClipOnly = false)
        {
            status = SkeletalGltfDecodeStatus::InvalidDocument;
            const auto exceed = [&]()
            {
                status = SkeletalGltfDecodeStatus::ImportLimitExceeded;
                return false;
            };
            const auto nodes = root.FindMember("nodes"), accessors = root.FindMember("accessors");
            const auto views = root.FindMember("bufferViews"), buffers = root.FindMember("buffers");
            const auto animations = root.FindMember("animations");
            if (nodes.GetArraySize() > limits.MaxNodes || accessors.GetArraySize() > limits.MaxAccessors ||
                views.GetArraySize() > limits.MaxAccessors || buffers.GetArraySize() > limits.MaxBuffers ||
                animations.GetArraySize() > limits.MaxClips)
            {
                return exceed();
            }
            const auto skins = root.FindMember("skins");
            if (skins.IsArray() && skins.GetArraySize() == 1 &&
                skins.GetArrayElement(0).FindMember("joints").GetArraySize() > limits.MaxJoints)
            {
                return exceed();
            }
            const auto accessorCount = [&](const JsonValue& index, uint32_t& count)
            {
                uint32_t i = 0;
                return TryReadUInt32(index, i) && i < accessors.GetArraySize() &&
                       TryReadUInt32(accessors.GetArrayElement(i).FindMember("count"), count);
            };
            uint64_t retainedNameBytes = 0;
            const auto retainName = [&](const JsonValue& value)
            {
                if (!value.IsString())
                {
                    return true;
                } // 必須/非空の意味検査は後続の名前契約で行う。
                const auto& name = value.AsString();
                const auto measured =
                    Asset::MeasureSkeletalNameEncoding<Container::String::value_type>(2, {name.data(), name.size()});
                if (!measured.Succeeded())
                {
                    return false;
                }
                if (measured.ByteCount > limits.MaxNameBytes ||
                    measured.ByteCount > limits.MaxStringBytes - retainedNameBytes)
                {
                    return exceed();
                }
                retainedNameBytes += measured.ByteCount;
                return true;
            };
            if (skins.IsArray() && skins.GetArraySize() == 1)
            {
                const auto joints = skins.GetArrayElement(0).FindMember("joints");
                for (size_t i = 0; i < joints.GetArraySize(); ++i)
                {
                    uint32_t node = 0;
                    if (!TryReadUInt32(joints.GetArrayElement(i), node) || node >= nodes.GetArraySize() ||
                        !retainName(nodes.GetArrayElement(node).FindMember("name")))
                    {
                        return false;
                    }
                }
            }
            uint64_t channelsTotal = 0, samplesTotal = 0, samplersTotal = 0;
            for (size_t i = 0; i < animations.GetArraySize(); ++i)
            {
                const auto animation = animations.GetArrayElement(i);
                if (!retainName(animation.FindMember("name")))
                {
                    return false;
                }
                const auto channels = animation.FindMember("channels"), samplers = animation.FindMember("samplers");
                if (channels.GetArraySize() > limits.MaxChannels - channelsTotal ||
                    samplers.GetArraySize() > limits.MaxChannels - samplersTotal)
                {
                    return exceed();
                }
                channelsTotal += channels.GetArraySize();
                samplersTotal += samplers.GetArraySize();
                for (size_t n = 0; n < channels.GetArraySize(); ++n)
                {
                    uint32_t sampler = 0, count = 0;
                    if (!TryReadUInt32(channels.GetArrayElement(n).FindMember("sampler"), sampler) ||
                        sampler >= samplers.GetArraySize() ||
                        !accessorCount(samplers.GetArrayElement(sampler).FindMember("input"), count))
                    {
                        return false;
                    }
                    // 共有accessorでもchannelごとの所有copyを数える。Bakeの追加分は生成前に別途制限する。
                    if (count > limits.MaxSamples - samplesTotal)
                    {
                        return exceed();
                    }
                    samplesTotal += count;
                }
            }
            uint64_t vertices = 0, indices = 0;
            const auto meshes = root.FindMember("meshes");
            for (size_t i = 0; !bClipOnly && i < meshes.GetArraySize(); ++i)
            {
                const auto primitives = meshes.GetArrayElement(i).FindMember("primitives");
                if (primitives.GetArraySize() > MaximumSubmeshCount)
                {
                    return exceed();
                }
                for (size_t n = 0; n < primitives.GetArraySize(); ++n)
                {
                    const auto primitive = primitives.GetArrayElement(n),
                               attributes = primitive.FindMember("attributes");
                    if (attributes.GetObjectSize() > 128)
                    {
                        return exceed();
                    }
                    uint32_t v = 0, k = 0;
                    if (!accessorCount(attributes.FindMember("POSITION"), v) ||
                        !accessorCount(primitive.FindMember("indices"), k))
                    {
                        return false;
                    }
                    if (v > limits.MaxVertices - vertices || k > limits.MaxIndices - indices)
                    {
                        return exceed();
                    }
                    vertices += v;
                    indices += k;
                }
            }
            for (size_t i = 0; i < nodes.GetArraySize(); ++i)
            {
                const auto value = nodes.GetArrayElement(i).FindMember("name");
                if (value.IsString())
                {
                    const auto& name = value.AsString();
                    const auto measured = Asset::MeasureSkeletalNameEncoding<Container::String::value_type>(
                        2, {name.data(), name.size()});
                    if (!measured.Succeeded())
                    {
                        return false;
                    }
                    if (measured.ByteCount > limits.MaxNameBytes)
                    {
                        return exceed();
                    }
                }
            }
            uint64_t declaredBytes = 0;
            reservedBufferBytes = 0;
            for (size_t i = 0; i < buffers.GetArraySize(); ++i)
            {
                const auto buffer = buffers.GetArrayElement(i);
                const auto length = buffer.FindMember("byteLength");
                if (!length.IsNumber())
                {
                    return false;
                }
                const auto parsedLength = Gltf::ParseBufferByteLength(length.AsNumber());
                if (!parsedLength.bValid)
                {
                    return false;
                }
                if (parsedLength.Value > limits.MaxBufferBytes - declaredBytes)
                {
                    return exceed();
                }
                declaredBytes += parsedLength.Value;
                const auto uri = buffer.FindMember("uri");
                uint64_t reserve = 0;
                if (!buffer.HasMember("uri"))
                {
                    reserve = parsedLength.Value;
                }
                else
                {
                    if (!uri.IsString())
                    {
                        return false;
                    }
                    const auto& text = uri.AsString();
                    if (text.size() > limits.MaxSourceBytes)
                    {
                        return exceed();
                    }
                    Container::VariableArray<uint8_t> raw;
                    raw.reserve(text.size());
                    for (auto c : text)
                    {
                        if (static_cast<uint32_t>(c) > 127)
                        {
                            return false;
                        }
                        raw.push_back(static_cast<uint8_t>(c));
                    }
                    const auto data = Gltf::ParseDataUri({raw.data(), raw.size()});
                    if (data.Result == Gltf::BufferSourceResult::Success)
                    {
                        if (data.View.Mime != Gltf::DataUriMime::OctetStream &&
                            data.View.Mime != Gltf::DataUriMime::GltfBuffer)
                        {
                            return false;
                        }
                        if (data.View.PercentDecodedSize > limits.MaxSourceBytes)
                        {
                            return exceed();
                        }
                        Container::VariableArray<uint8_t> encoded(data.View.PercentDecodedSize);
                        if (Gltf::DecodePercentBytes(data.View.EncodedPayload, {encoded.data(), encoded.size()})
                                .Result != Gltf::BufferSourceResult::Success)
                        {
                            return false;
                        }
                        const auto decoded = Text::GetBase64DecodedSize({encoded.data(), encoded.size()});
                        if (decoded.Result != Text::Base64DecodeResult::Success)
                        {
                            return false;
                        }
                        reserve = decoded.Size;
                    }
                    else if (data.Result == Gltf::BufferSourceResult::NotDataUri)
                    {
                        if (text.size() > limits.MaxNameBytes)
                        {
                            return exceed();
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                if (reserve > limits.MaxBufferBytes - reservedBufferBytes)
                {
                    return exceed();
                }
                reservedBufferBytes += reserve;
            }
            return true;
        }

        bool CheckSplitMaterialBudget(const JsonValue& root, const RigV1Limits& limits,
                                      SkeletalGltfDecodeStatus& status)
        {
            const auto materials = root.FindMember("materials");
            if (materials.IsValid() && !materials.IsArray())
            {
                return false;
            }
            if (materials.GetArraySize() > 256)
            {
                status = SkeletalGltfDecodeStatus::ImportLimitExceeded;
                return false;
            }
            uint64_t remaining = limits.MaxStringBytes;
            for (size_t i = 0; i < materials.GetArraySize(); ++i)
            {
                const auto material = materials.GetArrayElement(i);
                const auto field = material.FindMember("name");
                if (!material.IsObject() || (field.IsValid() && !field.IsString()))
                {
                    return false;
                }
                const auto& name = field.AsString();
                if (name.empty())
                {
                    continue;
                }
                const auto measured = Asset::MeasureSkeletalNameEncoding(
                    2, Container::Span<const Container::String::value_type>{name.data(), name.size()});
                if (!measured.Succeeded())
                {
                    return false;
                }
                if (measured.ByteCount > limits.MaxNameBytes || measured.ByteCount > remaining)
                {
                    status = SkeletalGltfDecodeStatus::ImportLimitExceeded;
                    return false;
                }
                remaining -= measured.ByteCount;
            }
            return true;
        }
        struct CapturedBufferReadContext
        {
            Gltf::BufferFileContext* File = nullptr;
            Container::VariableArray<std::filesystem::path>* Paths = nullptr;
        };
        Gltf::ExternalBufferReadResult ReadCapturedBuffer(Container::Span<const uint8_t> uri,
                                                          Container::VariableArray<uint8_t>& bytes, void* opaque)
        {
            auto& context = *static_cast<CapturedBufferReadContext*>(opaque);
            std::filesystem::path acquired;
            const auto status = Gltf::ReadBufferFileWithPath(uri, bytes, context.File, &acquired);
            if (status == Gltf::ExternalBufferReadResult::Success)
            {
                context.Paths->push_back(std::move(acquired));
            }
            return status;
        }
        SkeletalGltfDecodeResult DecodeResolvedDocument(
            const JsonValue& root, const Gltf::ContainerView& container, const std::filesystem::path& sourcePath,
            Gltf::BufferSet* outSourceBuffers, const AssetImport::LoadedImportSettings* importSettings,
            const SkeletalGltfDecodeOptions* decodeOptions, bool allowMultipleClips, bool bAllowEmptyClips = false,
            Container::VariableArray<SkeletalRestTransform>* outRest = nullptr, double* outResolvedScale = nullptr,
            const RigV1Limits* rigLimits = nullptr, RigGltfImportCapture* capture = nullptr,
            RigImportProfile profile = RigImportProfile::DirectTrs128, RigRootFrame* outRootFrame = nullptr,
            const RigClipSourceSelection* clipSource = nullptr)
        {
            if (!Gltf::IsValidNativeSourcePath(sourcePath))
            {
                return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
            }
            const SkeletalGltfDecodeOptions options = decodeOptions != nullptr ? *decodeOptions : SkeletalGltfDecodeOptions{};
            if (!IsValidSkeletalGltfDecodeOptions(options)) return Fail(SkeletalGltfDecodeStatus::InvalidImportOptions);
            if (!root.IsObject())
            {
                return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
            }

            uint64_t reservedBufferBytes = 0;
            SkeletalGltfDecodeStatus budgetStatus = SkeletalGltfDecodeStatus::InvalidDocument;
            if (rigLimits &&
                !CheckRigInputBudget(root, *rigLimits, reservedBufferBytes, budgetStatus, clipSource != nullptr))
            {
                return Fail(budgetStatus);
            }
            if (!clipSource && capture && rigLimits && !CheckSplitMaterialBudget(root, *rigLimits, budgetStatus))
            {
                return Fail(budgetStatus);
            }
            uint64_t remainingSamples = rigLimits ? rigLimits->MaxSamples : 0;

            if (Gltf::CheckRequiredExtensions(root) != Gltf::RequiredExtensionsStatus::Success)
            {
                return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
            }

            if ((clipSource || options.MorphPolicy == SkeletalMorphPolicy::Reject) && Gltf::HasMorphData(root))
            {
                return Fail(SkeletalGltfDecodeStatus::UnsupportedMorphTargets);
            }
            AssetImport::LoadedImportSettings discoveredImport;
            const auto* selectedImport = importSettings;
            if (selectedImport == nullptr)
            {
                if (!sourcePath.empty() &&
                    AssetImport::LoadImportSettingsFile(sourcePath, {}, discoveredImport).Result !=
                        AssetImport::SettingsFileResult::Success)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
                }
                selectedImport = &discoveredImport;
            }

            Container::VariableArray<PrimitiveInfo> primitives;
            Container::VariableArray<SkeletalMaterialSlot> materialSlots;
            SkeletalGltfDecodeStatus status = SkeletalGltfDecodeStatus::InvalidDocument;
            Container::VariableArray<uint64_t> materialSources;
            if (!clipSource && !ParsePrimitives(root, primitives, materialSlots, status, options,
                                                capture ? &materialSources : nullptr, capture ? rigLimits : nullptr))
            {
                return Fail(status);
            }

            JsonValue skin;
            Container::VariableArray<uint32_t> clipJoints;
            if (clipSource && !clipSource->JointNodes.empty())
            {
                // skinと明示選択を混ぜない。曖昧な骨格選択はcook段階で拒否する。
                if (root.HasMember("skins") || !clipSource->JointNodes.data() ||
                    clipSource->JointNodes.size() > rigLimits->MaxJoints)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
                }
                clipJoints.assign(clipSource->JointNodes.begin(), clipSource->JointNodes.end());
            }
            else
            {
                if (!ParseSkinContract(root, skin, status, RigProfileMaximumJoints(profile)))
                {
                    return Fail(status);
                }
                if (clipSource)
                {
                    const auto joints = skin.FindMember("joints");
                    clipJoints.resize(joints.GetArraySize());
                    for (size_t i = 0; i < clipJoints.size(); ++i)
                    {
                        if (!TryReadUInt32(joints.GetArrayElement(i), clipJoints[i]))
                        {
                            return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
                        }
                    }
                }
            }

            const JsonValue animations = root.FindMember("animations");
            const bool bMissingAnimations = !root.HasMember("animations");
            if ((!animations.IsArray() && !(bAllowEmptyClips && bMissingAnimations)) ||
                (!bAllowEmptyClips && animations.GetArraySize() == 0) || animations.GetArraySize() > UINT32_MAX ||
                (!allowMultipleClips && animations.GetArraySize() != 1))
            {
                return Fail(SkeletalGltfDecodeStatus::UnsupportedClipCount);
            }
            uint64_t totalChannels = 0;
            for (size_t index=0;index<animations.GetArraySize();++index)
            {
                const auto animation = animations.GetArrayElement(index);
                if (!ParseAnimationContract(animation,status,options))
                {
                    return Fail(status);
                }
                const size_t count = animation.FindMember("channels").GetArraySize();
                if (count > UINT32_MAX - totalChannels)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidAnimation);
                }
                totalChannels += count;
            }

            Container::VariableArray<AccessorInfo> accessors;
            Container::VariableArray<BufferViewInfo> bufferViews;
            Gltf::BufferSet buffers;
            Gltf::BufferFileContext fileContext{sourcePath};
            Container::VariableArray<std::filesystem::path> acquiredPaths, sourceFiles;
            CapturedBufferReadContext readContext{&fileContext, &acquiredPaths};
            if (capture)
            {
                acquiredPaths.reserve(root.FindMember("buffers").GetArraySize());
            }
            if (rigLimits)
            {
                fileContext.MaxReadBytes = rigLimits->MaxBufferBytes - reservedBufferBytes;
            }
            status = SkeletalGltfDecodeStatus::InvalidAccessor;
            if (!ParseAccessors(root, accessors, status))
            {
                return Fail(status);
            }
            if (!ParseBufferViews(root, bufferViews) ||
                Gltf::ResolveJsonBuffers(root, container, capture ? ReadCapturedBuffer : Gltf::ReadBufferFile,
                                         capture ? static_cast<void*>(&readContext) : static_cast<void*>(&fileContext),
                                         buffers)
                        .Result != Gltf::BufferResolveResult::Success)
            {
                return Fail(fileContext.bLimitExceeded ? SkeletalGltfDecodeStatus::ImportLimitExceeded
                                                       : SkeletalGltfDecodeStatus::InvalidAccessor);
            }

            if (capture)
            {
                sourceFiles.resize(buffers.GetCount());
                size_t next = 0;
                for (size_t i = 0; i < buffers.GetCount(); ++i)
                {
                    if (buffers.GetSourceKind(i) == Gltf::BufferStorageKind::ExternalFile)
                    {
                        if (next >= acquiredPaths.size())
                        {
                            return Fail(SkeletalGltfDecodeStatus::InvalidAccessor);
                        }
                        sourceFiles[i] = std::move(acquiredPaths[next++]);
                    }
                }
                if (next != acquiredPaths.size())
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidAccessor);
                }
            }
            SkeletalGltfData data;
            data.MaterialSlots = std::move(materialSlots);
            Container::VariableArray<int32_t> nodeToJoint;
            NodeContract nodeContract;
            if (!ParseNodeContract(root, nodeContract, IsStaticRootFrameProfile(profile), clipSource != nullptr))
            {
                return Fail(SkeletalGltfDecodeStatus::InvalidSkeleton);
            }
            data.MeshNodeGlobalTransform = nodeContract.MeshNodeGlobal;
            const size_t skinJointCount = clipSource ? clipJoints.size() : skin.FindMember("joints").GetArraySize();
            SkeletalGltfDecodeReport report;
            uint64_t totalVertices = 0;
            for (auto& primitive : primitives)
            {
                const AccessorInfo* position = nullptr;
                AccessorLayout positionLayout;
                if (!GetAccessor(accessors, primitive.Position, "VEC3", FloatComponent, bufferViews, buffers, position, positionLayout) || position->Count == 0)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidAccessor);
                }
                primitive.VertexCount = position->Count;
                totalVertices += primitive.VertexCount;
                if (totalVertices > UINT32_MAX)
                {
                    return Fail(SkeletalGltfDecodeStatus::InvalidSubMesh);
                }
            }
            if (options.InfluencePolicy == SkeletalInfluencePolicy::ReduceToFour)
            {
                report.TotalVertexCount = totalVertices;
            }
            status = SkeletalGltfDecodeStatus::InvalidAccessor;
            for (const auto& primitive : primitives)
            {
                const size_t baseIndex = data.Indices.size();
                if (!ExtractMesh(primitive, accessors, bufferViews, buffers, data,
                    static_cast<uint32_t>(skinJointCount), options, report, status))
                {
                    auto failure = Fail(status);
                    failure.Report = report;
                    return failure;
                }
                data.SubMeshes.push_back({static_cast<uint32_t>(baseIndex), static_cast<uint32_t>(data.Indices.size() - baseIndex), primitive.MaterialSlot});
            }
            if (!clipSource && !ResolveSkeletalSubmeshLayout({data.SubMeshes.data(), data.SubMeshes.size()},
                                                             data.Indices.size(), data.MaterialSlots.size())
                                    .Succeeded())
            {
                auto failure = Fail(SkeletalGltfDecodeStatus::InvalidSubMesh);
                failure.Report = report;
                return failure;
            }
            const auto failWithReport = [&report](SkeletalGltfDecodeStatus failureStatus)
            {
                auto failure = Fail(failureStatus);
                failure.Report = report;
                return failure;
            };
            for (const SkeletalVertex& vertex : data.Vertices)
            {
                for (const uint32_t jointIndex : vertex.JointIndices)
                {
                    if (jointIndex >= skinJointCount)
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::InvalidSkeleton);
                    }
                }
            }
            RigRootFrame rootFrame = IdentityRigRootFrame();
            if (!ExtractSkeleton(root, skin, nodeContract, accessors, bufferViews, buffers, data, nodeToJoint, profile,
                                 &rootFrame, {clipJoints.data(), clipJoints.size()}))
            {
                return failWithReport(SkeletalGltfDecodeStatus::InvalidSkeleton);
            }
            if (!clipSource && options.MorphPolicy == SkeletalMorphPolicy::Drop)
            {
                // 全clipの検査完了まで除去数は未確定。失敗時に途中の数を完了済み扱いしない。
                auto morphReport = report;
                // BVH用の0clip rigでもmesh/nodeのmorphは省略せず検証する。
                if (animations.GetArraySize() == 0 &&
                    !ValidateMorphDropMesh(root, nodeContract, accessors, bufferViews, buffers,
                        {primitives.data(), primitives.size()}, morphReport))
                {
                    return failWithReport(SkeletalGltfDecodeStatus::InvalidAccessor);
                }
                for (size_t index=0;index<animations.GetArraySize();++index)
                {
                    if (!ValidateMorphDrop(root,animations.GetArrayElement(index),nodeContract,accessors,bufferViews,buffers,
                        {primitives.data(),primitives.size()},morphReport,index==0))
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::InvalidAccessor);
                    }
                }
                morphReport.bMorphScanComplete = true;
                report = morphReport;
            }
            const bool bBake = options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake;
            double translationScale = 1;
            if (bBake && selectedImport->bPresent &&
                !ResolveSkeletalImportScale(data, selectedImport->Settings, translationScale, clipSource != nullptr))
            {
                return failWithReport(SkeletalGltfDecodeStatus::InvalidDocument);
            }
            status = SkeletalGltfDecodeStatus::InvalidAnimation;
            if (bBake)
            {
                report.TotalAnimationChannelCount = totalChannels;
                report.bCubicScanStarted = true;
            }
            for (size_t index=0;index<animations.GetArraySize();++index)
            {
                if (!ExtractAnimation(animations.GetArrayElement(index), nodeToJoint, accessors, bufferViews, buffers,
                                      data, options, translationScale, report, status,
                                      rigLimits ? &remainingSamples : nullptr))
                {
                    return failWithReport(status);
                }
            }
            if (bBake)
            {
                report.bCubicScanComplete = true;
            }

            if (selectedImport->bPresent)
            {
                if ((!bBake && !ResolveSkeletalImportScale(data, selectedImport->Settings, translationScale,
                                                           clipSource != nullptr)) ||
                    !ApplyResolvedSkeletalImport(data, translationScale, !bBake))
                {
                    return failWithReport(SkeletalGltfDecodeStatus::InvalidDocument);
                }
            }

            Container::VariableArray<SkeletalRestTransform> restCandidate;
            if (outRest)
            {
                const auto nodes = root.FindMember("nodes");
                const auto jointValues = skin.FindMember("joints");
                restCandidate.reserve(data.Joints.size());
                for (size_t i = 0; i < data.Joints.size(); ++i)
                {
                    uint32_t nodeIndex = InvalidIndex;
                    if (clipSource)
                    {
                        nodeIndex = clipJoints[i];
                    }
                    if ((!clipSource && !TryReadUInt32(jointValues.GetArrayElement(i), nodeIndex)) ||
                        nodeIndex >= nodes.GetArraySize())
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                    }
                    const auto node = nodes.GetArrayElement(nodeIndex);
                    if (node.HasMember("matrix"))
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                    }
                    float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
                    if ((node.HasMember("translation") && !ReadFloatArray(node.FindMember("translation"), 3, t)) ||
                        (node.HasMember("rotation") && !ReadFloatArray(node.FindMember("rotation"), 4, q)) ||
                        (node.HasMember("scale") && !ReadFloatArray(node.FindMember("scale"), 3, s)))
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                    }
                    SkeletalRestTransform rest;
                    rest.Translation = {t[0], t[1], t[2]};
                    rest.Rotation = {q[0], q[1], q[2], q[3]};
                    rest.Scale = {s[0], s[1], s[2]};
                    if (!AssetImport::TryScaleImportValue(rest.Translation.X, translationScale, rest.Translation.X) ||
                        !AssetImport::TryScaleImportValue(rest.Translation.Y, translationScale, rest.Translation.Y) ||
                        !AssetImport::TryScaleImportValue(rest.Translation.Z, translationScale, rest.Translation.Z) ||
                        !IsValidSkeletalRestTransform(rest))
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                    }
                    restCandidate.push_back(rest);
                }
            }
            if (outRootFrame)
            {
                for (size_t i = 12; i < 15; ++i)
                {
                    if (!AssetImport::TryScaleImportValue(rootFrame[i], translationScale, rootFrame[i]))
                    {
                        return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                    }
                }
                CanonicalizeRigRootFrameZero(rootFrame);
                if (!IsValidRigRootFrame(rootFrame, profile))
                {
                    return failWithReport(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
                }
            }
            SkeletalGltfDecodeResult result;
            result.Status = SkeletalGltfDecodeStatus::Success;
            result.Data = std::move(data);
            result.Report = report;
            if (outRest)
            {
                *outRest = std::move(restCandidate);
            }
            if (outResolvedScale)
            {
                *outResolvedScale = translationScale;
            }
            if (outRootFrame)
            {
                *outRootFrame = rootFrame;
            }
            if (capture)
            {
                capture->Buffers.Swap(buffers);
                capture->SourceCanonicalFiles = std::move(sourceFiles);
                capture->SlotSourceMaterialIndices = std::move(materialSources);
            }
            else if (outSourceBuffers != nullptr)
            {
                outSourceBuffers->Swap(buffers);
            }
            return result;
        }
    } // namespace

    static SkeletalGltfDecodeResult DecodeGltfBytes(
        Container::Span<const uint8_t> sourceBytes, const std::filesystem::path& sourcePath,
        Gltf::BufferSet* outSourceBuffers, const AssetImport::LoadedImportSettings* importSettings,
        const SkeletalGltfDecodeOptions* decodeOptions, bool allowMultipleClips, bool bAllowEmptyClips = false,
        Container::VariableArray<SkeletalRestTransform>* outRest = nullptr, double* outResolvedScale = nullptr,
        const RigV1Limits* rigLimits = nullptr, RigGltfImportCapture* capture = nullptr,
        RigImportProfile profile = RigImportProfile::DirectTrs128, RigRootFrame* outRootFrame = nullptr,
        const RigClipSourceSelection* clipSource = nullptr)
    {
        if (outSourceBuffers != nullptr)
        {
            outSourceBuffers->Reset();
        }
        if (!Gltf::IsValidNativeSourcePath(sourcePath))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
        }
        if (!IsSupportedRigImportProfile(profile) || (clipSource && !IsStaticRootFrameProfile(profile)) ||
            (IsStaticRootFrameProfile(profile) && (!rigLimits || !outRest || !outRootFrame)))
        {
            return Fail(SkeletalGltfDecodeStatus::UnsupportedAuthorRest);
        }
        if (rigLimits &&
            (!IsValidRigProfileLimits(profile, *rigLimits) || sourceBytes.size() > rigLimits->MaxSourceBytes))
        {
            return Fail(SkeletalGltfDecodeStatus::ImportLimitExceeded);
        }
        Gltf::ContainerView container;
        const auto parsed = Gltf::ParseContainer(sourceBytes, container);
        if (parsed != Gltf::ContainerParseResult::Success && parsed != Gltf::ContainerParseResult::NotGlb)
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidDocument);
        }
        if (container.Json.empty() ||
            std::find(container.Json.begin(), container.Json.end(), uint8_t{0}) != container.Json.end())
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidJson);
        }
        JsonDocument document;
        Container::String error;
        if (!JsonDocument::TryParseUtf8(container.Json, document, &error))
        {
            return Fail(SkeletalGltfDecodeStatus::InvalidJson);
        }
        auto result =
            DecodeResolvedDocument(document.GetRoot(), container, sourcePath, outSourceBuffers, importSettings,
                                   decodeOptions, allowMultipleClips, bAllowEmptyClips, outRest, outResolvedScale,
                                   rigLimits, capture, profile, outRootFrame, clipSource);
        if (result.Succeeded() && capture)
        {
            capture->SourceContainer = container;
            capture->Document = std::move(document);
        }
        return result;
    }

    SkeletalGltfDecodeResult DecodeSkeletalGltf(Container::Span<const uint8_t> sourceBytes,
                                                const Container::String& sourcePath, Gltf::BufferSet* outSourceBuffers,
                                                const AssetImport::LoadedImportSettings* importSettings,
                                                const SkeletalGltfDecodeOptions* decodeOptions)
    {
        return DecodeGltfBytes(sourceBytes,
                               (sourcePath.empty() ? std::filesystem::path{}
                                                   : std::filesystem::path(sourcePath.begin(), sourcePath.end())),
                               outSourceBuffers, importSettings, decodeOptions, false);
    }

    SkeletalGltfDecodeResult DecodeRigGltf(Container::Span<const uint8_t> sourceBytes,
                                           const Container::String& sourcePath, Gltf::BufferSet* outSourceBuffers,
                                           const AssetImport::LoadedImportSettings* importSettings,
                                           const SkeletalGltfDecodeOptions* decodeOptions)
    {
        return DecodeGltfBytes(sourceBytes,
                               (sourcePath.empty() ? std::filesystem::path{}
                                                   : std::filesystem::path(sourcePath.begin(), sourcePath.end())),
                               outSourceBuffers, importSettings, decodeOptions, true);
    }

    SkeletalGltfDecodeResult DecodeRigGltfNativePath(Container::Span<const uint8_t> sourceBytes,
                                                     const std::filesystem::path& sourcePath,
                                                     Gltf::BufferSet* outSourceBuffers,
                                                     const AssetImport::LoadedImportSettings* importSettings,
                                                     const SkeletalGltfDecodeOptions* decodeOptions)
    {
        return DecodeGltfBytes(sourceBytes, sourcePath, outSourceBuffers, importSettings, decodeOptions, true);
    }

    SkeletalGltfDecodeResult DecodeBvhTargetRigGltfNativePath(Container::Span<const uint8_t> sourceBytes,
        const std::filesystem::path& sourcePath, Gltf::BufferSet* outSourceBuffers,
        const AssetImport::LoadedImportSettings* importSettings, const SkeletalGltfDecodeOptions* decodeOptions)
    {
        return DecodeGltfBytes(sourceBytes, sourcePath, outSourceBuffers, importSettings, decodeOptions, true, true);
    }

    SkeletalGltfDecodeResult DecodeRigAuthorRestGltfNativePath(Container::Span<const uint8_t> sourceBytes,
                                                               const std::filesystem::path& sourcePath,
                                                               Container::VariableArray<SkeletalRestTransform>& outRest,
                                                               double& outResolvedScale, const RigV1Limits& limits,
                                                               const AssetImport::LoadedImportSettings* importSettings,
                                                               const SkeletalGltfDecodeOptions* decodeOptions)
    {
        return DecodeGltfBytes(sourceBytes, sourcePath, nullptr, importSettings, decodeOptions, true, true, &outRest,
                               &outResolvedScale, &limits);
    }

    SkeletalGltfDecodeResult DecodeRigAuthorRestGltfCapturedNativePath(
        Container::Span<const uint8_t> sourceBytes, const std::filesystem::path& sourcePath,
        Container::VariableArray<SkeletalRestTransform>& outRest, double& outResolvedScale,
        RigGltfImportCapture& outCapture, const RigV1Limits& limits, const AssetImport::LoadedImportSettings* settings,
        const SkeletalGltfDecodeOptions* options)
    {
        RigGltfImportCapture capture;
        Container::VariableArray<SkeletalRestTransform> rest;
        double scale = 1;
        auto result = DecodeGltfBytes(sourceBytes, sourcePath, nullptr, settings, options, true, true, &rest, &scale,
                                      &limits, &capture);
        if (result.Succeeded())
        {
            outCapture = std::move(capture);
            outRest = std::move(rest);
            outResolvedScale = scale;
        }
        return result;
    }

    SkeletalGltfDecodeResult DecodeRigAuthorFrameGltfNativePath(
        Container::Span<const uint8_t> source, const std::filesystem::path& path, RigImportProfile profile,
        Container::VariableArray<SkeletalRestTransform>& outRest, double& outScale, RigRootFrame& outFrame,
        RigGltfImportCapture* outCapture, const RigV1Limits& limits, const AssetImport::LoadedImportSettings* settings,
        const SkeletalGltfDecodeOptions* options, const RigClipSourceSelection* clipSource)
    {
        RigGltfImportCapture capture;
        Container::VariableArray<SkeletalRestTransform> rest;
        double scale = 1;
        RigRootFrame frame = IdentityRigRootFrame();
        auto result = DecodeGltfBytes(source, path, nullptr, settings, options, true,
                                      clipSource == nullptr || clipSource->bAllowEmptyAnimations, &rest, &scale,
                                      &limits, outCapture ? &capture : nullptr, profile, &frame, clipSource);
        if (result.Succeeded())
        {
            if (outCapture)
            {
                *outCapture = std::move(capture);
            }
            outRest = std::move(rest);
            outScale = scale;
            outFrame = frame;
        }
        return result;
    }

    SkeletalGltfDecodeResult DecodeSkeletalGltf(const Container::String& jsonText, const Container::String& sourcePath,
                                                SkeletalGltfSourceBuffers* outSourceBuffers,
                                                const AssetImport::LoadedImportSettings* importSettings,
                                                const SkeletalGltfDecodeOptions* decodeOptions)
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
        Gltf::BufferSet buffers;
        auto result = DecodeResolvedDocument(
            document.GetRoot(), {},
            (sourcePath.empty() ? std::filesystem::path{}
                                : std::filesystem::path(sourcePath.begin(), sourcePath.end())),
            outSourceBuffers != nullptr ? &buffers : nullptr, importSettings, decodeOptions, false);
        if (result.Succeeded() && outSourceBuffers != nullptr)
        {
            SkeletalGltfSourceBuffers owned(buffers.GetCount());
            for (size_t index = 0; index < buffers.GetCount(); ++index)
            {
                const auto bytes = buffers.GetSourceBytes(index);
                owned[index].assign(bytes.begin(), bytes.end());
            }
            outSourceBuffers->swap(owned);
        }
        return result;
    }
} // namespace NorvesLib::Core::Skeletal
