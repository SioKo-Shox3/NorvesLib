#include "MeshCooker.h"
#include "ModelInspection.h"

#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Container/FixedArray.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/SkeletalLimits.h"
#include "Resource/SkeletalInfluenceAttributes.h"
#include "Resource/SkeletalImportPolicy.h"
#include "Resource/GltfBufferFile.h"
#include "Resource/GltfBufferJson.h"
#include "Resource/GltfDocumentProfile.h"
#include "Resource/GltfImageSource.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/ImportSettingsHash.h"
#include "Resource/ImportTransform.h"
#include "Asset/AssetPackageFormat.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using NorvesLib::Core::JsonDocument;
        using NorvesLib::Core::JsonValue;
        using NorvesLib::Core::Asset::AssetBlob;
        using NorvesLib::Core::Asset::ComputeCookedMeshPayloadHash;
        using NorvesLib::Core::Asset::ParseCookedMesh;
        using NorvesLib::Core::Container::AnsiString;
        using NorvesLib::Core::Container::AnsiStringView;
        using NorvesLib::Core::Container::FixedArray;
        using NorvesLib::Core::Container::String;
        using NorvesLib::Core::Container::VariableArray;
        using NorvesLib::Core::Rendering::MegaGeometry::MeshCluster;
        using NorvesLib::Core::Rendering::MegaGeometry::MeshClusterizer;
        namespace Gltf = NorvesLib::Core::Gltf;
        namespace AssetImport = NorvesLib::Core::AssetImport;
        namespace ClusterRecordOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::ClusterRecordOffset;
        namespace Format = NorvesLib::Core::Asset::CookedMeshFormatV0;
        namespace HeaderOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::HeaderOffset;
        namespace MaterialRecordOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::MaterialRecordOffset;
        namespace StringRefRecordOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::StringRefRecordOffset;
        namespace SubmeshRecordOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::SubmeshRecordOffset;
        namespace VertexRecordOffset = NorvesLib::Core::Asset::CookedMeshFormatV0::VertexRecordOffset;

        constexpr uint32_t GltfFloatComponent = 5126;
        constexpr uint32_t GltfUInt16Component = 5123;
        constexpr uint32_t GltfUInt32Component = 5125;
        constexpr uint32_t GltfTrianglesMode = 4;
        constexpr uint64_t MaxExactJsonInteger = 9007199254740991ull;
        constexpr size_t GltfVertexAttributeAlignment = 4;
        constexpr size_t GltfMinimumByteStride = 4;
        constexpr size_t GltfMaximumByteStride = 252;
        constexpr AnsiStringView SupportedMeshFormat = "nvmesh.v0.mesh3d.pnt.u32.clustered";
        constexpr AnsiStringView SupportedSkeletalFormat = "nvskel.v0.skinned.pnujiw.u32";

        using MeshByteArray = NorvesLib::Core::Container::VariableArray<uint8_t>;

        // NVMESH v0 accepts strided vertex attributes but requires tightly packed index data,
        // so the layout rules differ per accessor usage.
        enum class AccessorUsage : uint8_t
        {
            VertexAttribute,
            Index,
        };

        struct MeshVertexPnt
        {
            float Position[3] = {};
            float Normal[3] = {};
            float TexCoord[2] = {};
        };

        static_assert(sizeof(MeshVertexPnt) == Format::VertexRecordSize);

        struct AccessorInfo
        {
            uint32_t BufferView = 0;
            size_t ByteOffset = 0;
            uint32_t ComponentType = 0;
            uint32_t Count = 0;
            AnsiString Type;
        };

        struct BufferViewInfo
        {
            uint32_t Buffer = 0;
            size_t ByteOffset = 0;
            size_t ByteLength = 0;
            size_t ByteStride = 0;
            bool bHasByteStride = false;
        };

        struct PrimitiveInfo
        {
            uint32_t PositionAccessor = 0;
            uint32_t NormalAccessor = 0;
            uint32_t TexCoordAccessor = 0;
            uint32_t IndexAccessor = 0;
            uint32_t MaterialIndex = 0;
            bool bHasMaterial = false;
        };

        struct AccessorLayout
        {
            const uint8_t* pData = nullptr;
            size_t Stride = 0;
        };

        struct MaterialReferences
        {
            AnsiString Albedo;
            AnsiString Normal;
            AnsiString Arm;
        };

        struct StringRefWire
        {
            uint64_t Offset = 0;
            uint32_t Length = 0;
        };

        struct BoundsSphere
        {
            float CenterX = 0.0f;
            float CenterY = 0.0f;
            float CenterZ = 0.0f;
            float Radius = 0.0f;
        };

        bool CheckedAdd(size_t lhs, size_t rhs, size_t& outValue)
        {
            if (lhs > std::numeric_limits<size_t>::max() - rhs)
            {
                return false;
            }

            outValue = lhs + rhs;
            return true;
        }

        bool CheckedMultiply(size_t lhs, size_t rhs, size_t& outValue)
        {
            if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
            {
                return false;
            }

            outValue = lhs * rhs;
            return true;
        }

        bool AlignUp(size_t value, size_t alignment, size_t& outValue)
        {
            if (alignment == 0)
            {
                return false;
            }

            size_t withPadding = 0;
            if (!CheckedAdd(value, alignment - 1, withPadding))
            {
                return false;
            }

            outValue = withPadding & ~(alignment - 1);
            return true;
        }

        void WriteLe16(MeshByteArray& bytes, size_t offset, uint16_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        }

        void WriteLe32(MeshByteArray& bytes, size_t offset, uint32_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
            bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
            bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
        }

        void WriteLe64(MeshByteArray& bytes, size_t offset, uint64_t value)
        {
            WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
            WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
        }

        void WriteFloat32(MeshByteArray& bytes, size_t offset, float value)
        {
            WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
        }

        uint64_t Fnv1a64Update(uint64_t hash, const uint8_t* pData, size_t size)
        {
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= static_cast<uint64_t>(pData[index]);
                hash *= Format::Fnv1a64Prime;
            }
            return hash;
        }

        uint64_t Fnv1a64UpdateLe64(uint64_t hash, uint64_t value)
        {
            uint8_t bytes[sizeof(uint64_t)] = {};
            for (size_t index = 0; index < sizeof(uint64_t); ++index)
            {
                bytes[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xffull);
            }
            return Fnv1a64Update(hash, bytes, sizeof(bytes));
        }

        // 埋込みbufferは元sourceに含まれる。外部bufferは余剰を含む全量をJSON順で加える。
        uint64_t ComputeGltfSourceHash(const uint8_t* sourceBytes, size_t sourceSize,
                                       const Gltf::BufferSet& buffers)
        {
            uint64_t hash = Format::Fnv1a64OffsetBasis;
            hash = Fnv1a64UpdateLe64(hash, static_cast<uint64_t>(sourceSize));
            hash = Fnv1a64Update(hash, sourceBytes, sourceSize);
            for (size_t index = 0; index < buffers.GetCount(); ++index)
            {
                if (buffers.GetSourceKind(index) == Gltf::BufferStorageKind::ExternalFile)
                {
                    const auto bytes = buffers.GetSourceBytes(index);
                    hash = Fnv1a64UpdateLe64(hash, static_cast<uint64_t>(bytes.size()));
                    hash = Fnv1a64Update(hash, bytes.data(), bytes.size());
                }
            }
            return hash;
        }

        float ReadFloat32(const uint8_t* pData)
        {
            const uint32_t bits = static_cast<uint32_t>(pData[0]) | (static_cast<uint32_t>(pData[1]) << 8) |
                                  (static_cast<uint32_t>(pData[2]) << 16) | (static_cast<uint32_t>(pData[3]) << 24);
            return std::bit_cast<float>(bits);
        }

        uint16_t ReadUInt16(const uint8_t* pData)
        {
            return static_cast<uint16_t>(pData[0]) | static_cast<uint16_t>(static_cast<uint16_t>(pData[1]) << 8);
        }

        uint32_t ReadUInt32(const uint8_t* pData)
        {
            return static_cast<uint32_t>(pData[0]) | (static_cast<uint32_t>(pData[1]) << 8) |
                   (static_cast<uint32_t>(pData[2]) << 16) | (static_cast<uint32_t>(pData[3]) << 24);
        }

        String ToCoreString(AnsiStringView value)
        {
            String result;
            result.reserve(value.size());
#if defined(UNICODE)
            for (const unsigned char character : value)
            {
                result.push_back(static_cast<wchar_t>(character));
            }
#else
            result.append(value.data(), value.size());
#endif
            return result;
        }

        template <typename T>
        AnsiString FormatInteger(T value)
        {
            char text[32] = {};
            const auto conversion = std::to_chars(text, text + sizeof(text), value);
            if (conversion.ec != std::errc())
            {
                return "?";
            }
            return AnsiString(AnsiStringView(text, static_cast<size_t>(conversion.ptr - text)));
        }

        bool TryReadUnsigned(const JsonValue& value, uint64_t maximum, uint64_t& outValue)
        {
            if (!value.IsNumber())
            {
                return false;
            }

            const double number = value.AsNumber(-1.0);
            const double exactMaximum = static_cast<double>(std::min(maximum, MaxExactJsonInteger));
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

        bool TryReadRequiredUInt32(const JsonValue& object, const char* name, uint32_t& outValue)
        {
            uint64_t value = 0;
            if (!object.IsObject() || !TryReadUnsigned(object.FindMember(name), UINT32_MAX, value))
            {
                return false;
            }

            outValue = static_cast<uint32_t>(value);
            return true;
        }

        bool TryReadOptionalUInt32(const JsonValue& object, const char* name, uint32_t defaultValue, uint32_t& outValue,
                                   bool* pOutPresent = nullptr)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsValid())
            {
                outValue = defaultValue;
                if (pOutPresent != nullptr)
                {
                    *pOutPresent = false;
                }
                return true;
            }

            if (pOutPresent != nullptr)
            {
                *pOutPresent = true;
            }
            return TryReadRequiredUInt32(object, name, outValue);
        }

        bool TryReadRequiredSize(const JsonValue& object, const char* name, size_t& outValue)
        {
            uint64_t value = 0;
            if (!object.IsObject() ||
                !TryReadUnsigned(object.FindMember(name), static_cast<uint64_t>(std::numeric_limits<size_t>::max()), value))
            {
                return false;
            }

            outValue = static_cast<size_t>(value);
            return true;
        }

        bool TryReadOptionalSize(const JsonValue& object, const char* name, size_t defaultValue, size_t& outValue,
                                 bool* pOutPresent = nullptr)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsValid())
            {
                outValue = defaultValue;
                if (pOutPresent != nullptr)
                {
                    *pOutPresent = false;
                }
                return true;
            }

            if (pOutPresent != nullptr)
            {
                *pOutPresent = true;
            }
            return TryReadRequiredSize(object, name, outValue);
        }

        bool TryConvertAsciiString(const JsonValue& value, AnsiString& outValue)
        {
            if (!value.IsString())
            {
                return false;
            }

            outValue.clear();
            const auto& source = value.AsString();
            outValue.reserve(source.size());
            for (const auto character : source)
            {
                const uint32_t codePoint = static_cast<uint32_t>(character);
                if (codePoint > 0x7fu)
                {
                    return false;
                }
                outValue.push_back(static_cast<char>(codePoint));
            }
            return true;
        }

        bool IsPrintableAscii(char value)
        {
            const unsigned char byte = static_cast<unsigned char>(value);
            return byte >= 0x20u && byte <= 0x7eu;
        }

        bool StartsWithDataUri(AnsiStringView value)
        {
            if (value.size() < 5)
            {
                return false;
            }

            return (value[0] == 'd' || value[0] == 'D') && (value[1] == 'a' || value[1] == 'A') &&
                   (value[2] == 't' || value[2] == 'T') && (value[3] == 'a' || value[3] == 'A') && value[4] == ':';
        }

        bool ValidateRelativePath(AnsiStringView value, const char* label, AnsiString& error)
        {
            if (value.empty())
            {
                error = AnsiString(label) + " must not be empty";
                return false;
            }

            if (StartsWithDataUri(value))
            {
                error = AnsiString(label) + " data URI is not supported";
                return false;
            }

            if (value.front() == '/' || value.find('\\') != AnsiStringView::npos || value.find(':') != AnsiStringView::npos)
            {
                error = AnsiString(label) + " must be a normalized relative path using / separators";
                return false;
            }

            size_t segmentStart = 0;
            while (segmentStart <= value.size())
            {
                const size_t separator = value.find('/', segmentStart);
                const size_t segmentEnd = separator == AnsiStringView::npos ? value.size() : separator;
                const AnsiStringView segment = value.substr(segmentStart, segmentEnd - segmentStart);
                if (segment.empty() || segment == AnsiStringView(".") || segment == AnsiStringView(".."))
                {
                    error = AnsiString(label) + " must not contain empty, . or .. path segments";
                    return false;
                }

                for (const char character : segment)
                {
                    if (!IsPrintableAscii(character))
                    {
                        error = AnsiString(label) + " must contain printable ASCII only";
                        return false;
                    }
                }

                if (separator == AnsiStringView::npos)
                {
                    break;
                }
                segmentStart = separator + 1;
            }

            const std::filesystem::path path(value.begin(), value.end());
            const AnsiString normalizedPath(path.lexically_normal().generic_string().c_str());
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
                AnsiStringView(normalizedPath) != value)
            {
                error = AnsiString(label) + " must be a normalized relative path";
                return false;
            }

            return true;
        }

        bool ParseAccessors(const JsonValue& root, VariableArray<AccessorInfo>& outAccessors, AnsiString& error)
        {
            const JsonValue accessors = root.FindMember("accessors");
            if (!accessors.IsArray())
            {
                error = "glTF accessors must be an array";
                return false;
            }

            outAccessors.clear();
            outAccessors.reserve(accessors.GetArraySize());
            for (size_t index = 0; index < accessors.GetArraySize(); ++index)
            {
                const JsonValue value = accessors.GetArrayElement(index);
                AccessorInfo accessor;
                if (!value.IsObject() || value.HasMember("sparse") ||
                    !TryReadRequiredUInt32(value, "bufferView", accessor.BufferView) ||
                    !TryReadOptionalSize(value, "byteOffset", 0, accessor.ByteOffset) ||
                    !TryReadRequiredUInt32(value, "componentType", accessor.ComponentType) ||
                    !TryReadRequiredUInt32(value, "count", accessor.Count) ||
                    !TryConvertAsciiString(value.FindMember("type"), accessor.Type))
                {
                    error = AnsiString("invalid or sparse glTF accessor at index ") + FormatInteger(index);
                    return false;
                }

                const JsonValue normalized = value.FindMember("normalized");
                if (normalized.IsValid() && (!normalized.IsBoolean() || normalized.AsBool()))
                {
                    error = "normalized glTF accessors are not supported";
                    return false;
                }
                outAccessors.push_back(std::move(accessor));
            }
            return true;
        }

        bool ParseBufferViews(const JsonValue& root, VariableArray<BufferViewInfo>& outBufferViews, AnsiString& error)
        {
            const JsonValue bufferViews = root.FindMember("bufferViews");
            if (!bufferViews.IsArray())
            {
                error = "glTF bufferViews must be an array";
                return false;
            }

            outBufferViews.clear();
            outBufferViews.reserve(bufferViews.GetArraySize());
            for (size_t index = 0; index < bufferViews.GetArraySize(); ++index)
            {
                const JsonValue value = bufferViews.GetArrayElement(index);
                BufferViewInfo bufferView;
                if (!value.IsObject() || !TryReadRequiredUInt32(value, "buffer", bufferView.Buffer) ||
                    !TryReadOptionalSize(value, "byteOffset", 0, bufferView.ByteOffset) ||
                    !TryReadRequiredSize(value, "byteLength", bufferView.ByteLength) ||
                    !TryReadOptionalSize(value, "byteStride", 0, bufferView.ByteStride, &bufferView.bHasByteStride))
                {
                    error = AnsiString("invalid glTF bufferView at index ") + FormatInteger(index);
                    return false;
                }
                outBufferViews.push_back(bufferView);
            }
            return true;
        }

        bool ParsePrimitive(const JsonValue& root, PrimitiveInfo& outPrimitive, AnsiString& error)
        {
            const JsonValue meshes = root.FindMember("meshes");
            if (!meshes.IsArray() || meshes.GetArraySize() != 1)
            {
                error = "NVMESH v0 requires exactly one glTF mesh";
                return false;
            }

            const JsonValue mesh = meshes.GetArrayElement(0);
            const JsonValue primitives = mesh.FindMember("primitives");
            if (!mesh.IsObject() || !primitives.IsArray() || primitives.GetArraySize() != 1)
            {
                error = "NVMESH v0 requires exactly one glTF primitive";
                return false;
            }

            const JsonValue primitive = primitives.GetArrayElement(0);
            const JsonValue attributes = primitive.FindMember("attributes");
            uint32_t mode = GltfTrianglesMode;
            if (!primitive.IsObject() || !attributes.IsObject() ||
                !TryReadRequiredUInt32(attributes, "POSITION", outPrimitive.PositionAccessor) ||
                !TryReadRequiredUInt32(attributes, "NORMAL", outPrimitive.NormalAccessor) ||
                !TryReadRequiredUInt32(attributes, "TEXCOORD_0", outPrimitive.TexCoordAccessor) ||
                !TryReadRequiredUInt32(primitive, "indices", outPrimitive.IndexAccessor) ||
                !TryReadOptionalUInt32(primitive, "mode", GltfTrianglesMode, mode) || mode != GltfTrianglesMode ||
                !TryReadOptionalUInt32(primitive, "material", 0, outPrimitive.MaterialIndex, &outPrimitive.bHasMaterial))
            {
                error = "glTF primitive must be indexed TRIANGLES with POSITION, NORMAL and TEXCOORD_0";
                return false;
            }
            return true;
        }

        bool ValidateRequiredExtensions(const JsonValue& root, AnsiString& error)
        {
            const auto status = Gltf::CheckRequiredExtensions(root);
            if (status != Gltf::RequiredExtensionsStatus::Success)
            {
                error = Gltf::RequiredExtensionsError(status);
                return false;
            }
            return true;
        }

        bool ResolveCookBuffers(const JsonValue& root, const Gltf::ContainerView& container,
                                AnsiStringView sourcePath, Gltf::BufferSet& buffers, AnsiString& error)
        {
            Gltf::BufferFileContext context{std::filesystem::path(sourcePath.begin(), sourcePath.end())};
            const auto outcome = Gltf::ResolveJsonBuffers(root, container, Gltf::ReadBufferFile, &context, buffers);
            if (outcome.Result == Gltf::BufferResolveResult::Success)
            {
                return true;
            }
            // 既存の相対path診断を維持し、percent復号後の違反は共有resolverの結果で拒否する。
            if (outcome.Result == Gltf::BufferResolveResult::InvalidUri)
            {
                AnsiString uri;
                const JsonValue descriptors = root.FindMember("buffers");
                if (outcome.BufferIndex < descriptors.GetArraySize() &&
                    TryConvertAsciiString(descriptors.GetArrayElement(outcome.BufferIndex).FindMember("uri"), uri) &&
                    !StartsWithDataUri(AnsiStringView(uri)) && !ValidateRelativePath(uri, "buffer URI", error))
                {
                    return false;
                }
                error = "invalid glTF buffer URI";
            }
            else if (outcome.Result == Gltf::BufferResolveResult::SourceTooShort)
            {
                error = "glTF buffer file is smaller than its declared byteLength";
            }
            else if (outcome.Result == Gltf::BufferResolveResult::ExternalReadFailure)
            {
                error = outcome.ReadError == Gltf::ExternalBufferReadResult::OutsideDirectory
                    ? "glTF buffer URI escapes the input directory" : "failed to read glTF buffer";
            }
            else if (outcome.Result == Gltf::BufferResolveResult::InvalidEmbeddedData ||
                     outcome.Result == Gltf::BufferResolveResult::UnsupportedDataMime)
            {
                error = "invalid glTF embedded buffer data";
            }
            else
            {
                error = "invalid glTF buffer descriptor";
            }
            return false;
        }

        bool ValidateAccessorLayout(const AccessorInfo& accessor, const VariableArray<BufferViewInfo>& bufferViews,
                                    const Gltf::BufferSet& buffers,
                                    uint32_t requiredComponentType, AnsiStringView requiredType, size_t componentCount,
                                    const char* label, AccessorUsage usage, AccessorLayout& outLayout, AnsiString& error)
        {
            if (accessor.ComponentType != requiredComponentType || AnsiStringView(accessor.Type) != requiredType)
            {
                error = AnsiString(label) + " accessor has unsupported componentType or type";
                return false;
            }

            if (accessor.BufferView >= bufferViews.size())
            {
                error = AnsiString(label) + " accessor bufferView is out of range";
                return false;
            }

            const BufferViewInfo& bufferView = bufferViews[accessor.BufferView];
            if (bufferView.Buffer >= buffers.GetCount())
            {
                error = AnsiString(label) + " bufferView buffer is out of range";
                return false;
            }

            const size_t componentSize = requiredComponentType == GltfUInt16Component ? 2 : 4;
            size_t elementSize = 0;
            if (!CheckedMultiply(componentSize, componentCount, elementSize))
            {
                error = AnsiString(label) + " accessor element size overflow";
                return false;
            }

            // Alignment is a property of the declared offsets alone, so it is diagnosed before any
            // range check can mask it. glTF requires accessor.byteOffset to be a multiple of the
            // component size, independently of the bufferView it is combined with.
            if (accessor.ByteOffset % componentSize != 0)
            {
                error = AnsiString(label) + " accessor byteOffset must be a multiple of the component size";
                return false;
            }

            if (usage == AccessorUsage::VertexAttribute && accessor.ByteOffset % GltfVertexAttributeAlignment != 0)
            {
                error = AnsiString(label) + " accessor byteOffset must be a multiple of 4";
                return false;
            }

            if (usage == AccessorUsage::Index && bufferView.bHasByteStride)
            {
                error = AnsiString(label) + " bufferView must not define byteStride";
                return false;
            }

            size_t stride = elementSize;
            if (bufferView.bHasByteStride)
            {
                stride = bufferView.ByteStride;
                if (stride < GltfMinimumByteStride || stride > GltfMaximumByteStride ||
                    stride % GltfVertexAttributeAlignment != 0)
                {
                    error = AnsiString(label) + " bufferView byteStride must be a multiple of 4 within 4..252";
                    return false;
                }
                if (stride < elementSize || stride % componentSize != 0)
                {
                    error = AnsiString(label) + " bufferView byteStride is invalid for the accessor element";
                    return false;
                }
            }

            size_t bufferViewEnd = 0;
            if (!CheckedAdd(bufferView.ByteOffset, bufferView.ByteLength, bufferViewEnd) ||
                bufferViewEnd > buffers.GetDeclaredByteLength(bufferView.Buffer) || bufferViewEnd > buffers.GetBytes(bufferView.Buffer).size())
            {
                error = AnsiString(label) + " bufferView range is invalid";
                return false;
            }

            size_t startOffset = 0;
            if (accessor.ByteOffset > bufferView.ByteLength ||
                !CheckedAdd(bufferView.ByteOffset, accessor.ByteOffset, startOffset))
            {
                error = AnsiString(label) + " accessor byteOffset is invalid";
                return false;
            }

            if (startOffset % componentSize != 0)
            {
                error = AnsiString(label) + " accessor absolute byteOffset must be a multiple of the component size";
                return false;
            }

            size_t requiredBytes = 0;
            if (accessor.Count > 0)
            {
                size_t stridedBytes = 0;
                if (!CheckedMultiply(static_cast<size_t>(accessor.Count) - 1, stride, stridedBytes) ||
                    !CheckedAdd(stridedBytes, elementSize, requiredBytes))
                {
                    error = AnsiString(label) + " accessor range overflow";
                    return false;
                }
            }

            if (requiredBytes > bufferView.ByteLength - accessor.ByteOffset)
            {
                error = AnsiString(label) + " accessor exceeds its bufferView";
                return false;
            }

            size_t absoluteEnd = 0;
            if (!CheckedAdd(startOffset, requiredBytes, absoluteEnd) || absoluteEnd > buffers.GetDeclaredByteLength(bufferView.Buffer) ||
                absoluteEnd > buffers.GetBytes(bufferView.Buffer).size())
            {
                error = AnsiString(label) + " accessor exceeds its buffer";
                return false;
            }

            outLayout.pData = buffers.GetBytes(bufferView.Buffer).data() + startOffset;
            outLayout.Stride = stride;
            return true;
        }

        bool ExtractMesh(const VariableArray<AccessorInfo>& accessors, const VariableArray<BufferViewInfo>& bufferViews,
                         const Gltf::BufferSet& buffers, const PrimitiveInfo& primitive,
                         VariableArray<MeshVertexPnt>& outVertices, VariableArray<uint32_t>& outIndices,
                         AnsiString& error)
        {
            if (primitive.PositionAccessor >= accessors.size() || primitive.NormalAccessor >= accessors.size() ||
                primitive.TexCoordAccessor >= accessors.size() || primitive.IndexAccessor >= accessors.size())
            {
                error = "glTF primitive accessor index is out of range";
                return false;
            }

            const AccessorInfo& positions = accessors[primitive.PositionAccessor];
            const AccessorInfo& normals = accessors[primitive.NormalAccessor];
            const AccessorInfo& texCoords = accessors[primitive.TexCoordAccessor];
            const AccessorInfo& indices = accessors[primitive.IndexAccessor];
            if (positions.Count == 0 || positions.Count != normals.Count || positions.Count != texCoords.Count)
            {
                error = "glTF vertex attribute counts must match and be non-zero";
                return false;
            }
            if (indices.Count == 0 || indices.Count % 3 != 0)
            {
                error = "glTF index count must be non-zero and divisible by three";
                return false;
            }

            AccessorLayout positionLayout;
            AccessorLayout normalLayout;
            AccessorLayout texCoordLayout;
            AccessorLayout indexLayout;
            if (!ValidateAccessorLayout(positions, bufferViews, buffers, GltfFloatComponent, "VEC3", 3, "POSITION",
                                        AccessorUsage::VertexAttribute, positionLayout, error) ||
                !ValidateAccessorLayout(normals, bufferViews, buffers, GltfFloatComponent, "VEC3", 3, "NORMAL",
                                        AccessorUsage::VertexAttribute, normalLayout, error) ||
                !ValidateAccessorLayout(texCoords, bufferViews, buffers, GltfFloatComponent, "VEC2", 2,
                                        "TEXCOORD_0", AccessorUsage::VertexAttribute, texCoordLayout, error))
            {
                return false;
            }

            if (indices.ComponentType != GltfUInt16Component && indices.ComponentType != GltfUInt32Component)
            {
                error = "indices accessor componentType must be uint16 or uint32";
                return false;
            }
            if (!ValidateAccessorLayout(indices, bufferViews, buffers, indices.ComponentType, "SCALAR", 1,
                                        "indices", AccessorUsage::Index, indexLayout, error))
            {
                return false;
            }

            outVertices.resize(positions.Count);
            for (uint32_t vertexIndex = 0; vertexIndex < positions.Count; ++vertexIndex)
            {
                const uint8_t* pPosition = positionLayout.pData + static_cast<size_t>(vertexIndex) * positionLayout.Stride;
                const uint8_t* pNormal = normalLayout.pData + static_cast<size_t>(vertexIndex) * normalLayout.Stride;
                const uint8_t* pTexCoord = texCoordLayout.pData + static_cast<size_t>(vertexIndex) * texCoordLayout.Stride;
                MeshVertexPnt& vertex = outVertices[vertexIndex];
                for (size_t component = 0; component < 3; ++component)
                {
                    vertex.Position[component] = ReadFloat32(pPosition + component * sizeof(float));
                    vertex.Normal[component] = ReadFloat32(pNormal + component * sizeof(float));
                }
                for (size_t component = 0; component < 2; ++component)
                {
                    vertex.TexCoord[component] = ReadFloat32(pTexCoord + component * sizeof(float));
                }

                for (const float value : vertex.Position)
                {
                    if (!std::isfinite(value))
                    {
                        error = "POSITION contains a non-finite float";
                        return false;
                    }
                }
                for (const float value : vertex.Normal)
                {
                    if (!std::isfinite(value))
                    {
                        error = "NORMAL contains a non-finite float";
                        return false;
                    }
                }
                for (const float value : vertex.TexCoord)
                {
                    if (!std::isfinite(value))
                    {
                        error = "TEXCOORD_0 contains a non-finite float";
                        return false;
                    }
                }
            }

            outIndices.resize(indices.Count);
            for (uint32_t index = 0; index < indices.Count; ++index)
            {
                const uint8_t* pIndex = indexLayout.pData + static_cast<size_t>(index) * indexLayout.Stride;
                const uint32_t value = indices.ComponentType == GltfUInt16Component ? static_cast<uint32_t>(ReadUInt16(pIndex))
                                                                                    : ReadUInt32(pIndex);
                if (value >= positions.Count)
                {
                    error = "glTF index is outside the vertex range";
                    return false;
                }
                outIndices[index] = value;
            }

            for (size_t index = 0; index < outIndices.size(); index += 3)
            {
                std::swap(outIndices[index + 1], outIndices[index + 2]);
            }
            return true;
        }

        bool BuildLogicalTextureReference(AnsiStringView logicalModelPath, AnsiStringView imageUri, const char* label,
                                          AnsiString& outReference, AnsiString& error)
        {
            if (!ValidateRelativePath(logicalModelPath, "model logical path", error) ||
                !ValidateRelativePath(imageUri, label, error))
            {
                return false;
            }

            const size_t separator = logicalModelPath.rfind('/');
            outReference.clear();
            if (separator != AnsiStringView::npos)
            {
                outReference = AnsiString(logicalModelPath.substr(0, separator + 1));
            }
            outReference.append(imageUri.data(), imageUri.size());
            return ValidateRelativePath(outReference, "material texture reference", error);
        }

        bool ResolveTextureReference(const JsonValue& root, const JsonValue& textureInfo, AnsiStringView logicalPath,
                                     const char* label, MeshImageRole role, const Gltf::BufferSet& buffers,
                                     VariableArray<MeshEmbeddedImage>& embeddedImages,
                                     AnsiString& outReference, AnsiString& error)
        {
            outReference.clear();
            if (!textureInfo.IsValid())
            {
                return true;
            }
            if (!textureInfo.IsObject())
            {
                error = AnsiString(label) + " texture info must be an object";
                return false;
            }

            // NVMESH v0のUV setは1つ。texCoord省略/明示0を許可し、他のUVへの誤結合を拒否する。
            uint32_t texCoord = 0;
            if (!TryReadOptionalUInt32(textureInfo, "texCoord", 0, texCoord))
            {
                error = AnsiString(label) + " texture info texCoord must be a non-negative integer";
                return false;
            }
            if (texCoord != 0)
            {
                error = AnsiString(label) + " texture info texCoord must be 0";
                return false;
            }

            // このcookerではUV変換を焼き込めないため、KHR_texture_transform等を拒否する。
            if (textureInfo.FindMember("extensions").IsValid())
            {
                error = AnsiString(label) + " texture info extensions are not supported";
                return false;
            }

            uint32_t textureIndex = 0;
            const JsonValue textures = root.FindMember("textures");
            if (!TryReadRequiredUInt32(textureInfo, "index", textureIndex) || !textures.IsArray() ||
                textureIndex >= textures.GetArraySize())
            {
                error = AnsiString(label) + " texture index is invalid";
                return false;
            }

            const JsonValue texture = textures.GetArrayElement(textureIndex);
            uint32_t imageIndex = 0;
            const JsonValue images = root.FindMember("images");
            if (!texture.IsObject() || !TryReadRequiredUInt32(texture, "source", imageIndex) || !images.IsArray() ||
                imageIndex >= images.GetArraySize())
            {
                error = AnsiString(label) + " texture source is invalid";
                return false;
            }

            Gltf::ImageSource image;
            const auto imageResult = Gltf::ImageSource::Resolve(root, imageIndex, buffers, image);
            if (imageResult != Gltf::ImageSourceResult::Success)
            {
                error = AnsiString(label) + " image source is invalid";
                return false;
            }
            if (image.GetKind() == Gltf::ImageSourceKind::ExternalFile)
            {
                const auto uri = image.GetExternalUri();
                return BuildLogicalTextureReference(logicalPath,
                    AnsiStringView(reinterpret_cast<const char*>(uri.data()), uri.size()), label, outReference, error);
            }

            const AnsiStringView textureFormat = role == MeshImageRole::Albedo
                ? "nvtex.v0.rgba8.srgb" : "nvtex.v0.rgba8.linear";
            for (auto& existing : embeddedImages)
            {
                if (existing.ImageIndex == imageIndex)
                {
                    if (AnsiStringView(existing.Format) != textureFormat)
                    {
                        error = "embedded image roles require conflicting texture formats";
                        return false;
                    }
                    existing.Roles |= static_cast<uint8_t>(role);
                    outReference = existing.LogicalPath;
                    return true;
                }
            }
            MeshEmbeddedImage entry;
            entry.ImageIndex = imageIndex;
            entry.Roles = static_cast<uint8_t>(role);
            entry.Format = AnsiString(textureFormat);
            entry.LogicalPath = AnsiString(logicalPath) + ".img" + FormatInteger(imageIndex) +
                (image.GetMime() == Gltf::DataUriMime::Png ? ".png" : ".jpg");
            if (!ValidateRelativePath(entry.LogicalPath, "embedded image logical path", error))
            {
                return false;
            }
            const auto bytes = image.GetBytes(buffers);
            if (!Gltf::MatchesEmbeddedImageMime(bytes, image.GetMime()))
            {
                error = Gltf::ProbeEmbeddedImageMime(bytes) == Gltf::DataUriMime::Unknown
                    ? "embedded image must contain PNG or JPEG bytes"
                    : "embedded image MIME does not match signature";
                return false;
            }
            const bool bBorrowGlb = image.GetKind() == Gltf::ImageSourceKind::BufferView &&
                buffers.GetSourceKind(image.GetBufferIndex()) == Gltf::BufferStorageKind::GlbBin;
            if (!entry.SetBytes(bytes, bBorrowGlb))
            {
                error = "embedded image bytes are empty or invalid";
                return false;
            }
            entry.SourceHash = Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
            outReference = entry.LogicalPath;
            embeddedImages.push_back(std::move(entry));
            return true;
        }

        bool ResolveMaterialReferences(const JsonValue& root, const PrimitiveInfo& primitive, AnsiStringView logicalPath,
                                       const Gltf::BufferSet& buffers, VariableArray<MeshEmbeddedImage>& embeddedImages,
                                       MaterialReferences& outReferences, AnsiString& error)
        {
            outReferences = {};
            if (!primitive.bHasMaterial)
            {
                return true;
            }

            const JsonValue materials = root.FindMember("materials");
            if (!materials.IsArray() || primitive.MaterialIndex >= materials.GetArraySize())
            {
                error = "glTF primitive material index is out of range";
                return false;
            }

            const JsonValue material = materials.GetArrayElement(primitive.MaterialIndex);
            if (!material.IsObject())
            {
                error = "glTF material must be an object";
                return false;
            }

            if (!ResolveTextureReference(root, material.FindMember("normalTexture"), logicalPath, "normal",
                                         MeshImageRole::Normal, buffers, embeddedImages, outReferences.Normal, error))
            {
                return false;
            }

            const JsonValue pbr = material.FindMember("pbrMetallicRoughness");
            if (!pbr.IsValid())
            {
                return true;
            }
            if (!pbr.IsObject())
            {
                error = "pbrMetallicRoughness must be an object";
                return false;
            }

            return ResolveTextureReference(root, pbr.FindMember("baseColorTexture"), logicalPath, "albedo",
                                           MeshImageRole::Albedo, buffers, embeddedImages, outReferences.Albedo, error) &&
                   ResolveTextureReference(root, pbr.FindMember("metallicRoughnessTexture"), logicalPath, "ARM",
                                           MeshImageRole::Arm, buffers, embeddedImages, outReferences.Arm, error);
        }

        BoundsSphere CalculateBounds(const VariableArray<MeshVertexPnt>& vertices)
        {
            BoundsSphere bounds;
            float minimumX = std::numeric_limits<float>::max();
            float minimumY = std::numeric_limits<float>::max();
            float minimumZ = std::numeric_limits<float>::max();
            float maximumX = std::numeric_limits<float>::lowest();
            float maximumY = std::numeric_limits<float>::lowest();
            float maximumZ = std::numeric_limits<float>::lowest();
            for (const MeshVertexPnt& vertex : vertices)
            {
                minimumX = std::min(minimumX, vertex.Position[0]);
                minimumY = std::min(minimumY, vertex.Position[1]);
                minimumZ = std::min(minimumZ, vertex.Position[2]);
                maximumX = std::max(maximumX, vertex.Position[0]);
                maximumY = std::max(maximumY, vertex.Position[1]);
                maximumZ = std::max(maximumZ, vertex.Position[2]);
            }

            bounds.CenterX = (minimumX + maximumX) * 0.5f;
            bounds.CenterY = (minimumY + maximumY) * 0.5f;
            bounds.CenterZ = (minimumZ + maximumZ) * 0.5f;
            float radiusSquared = 0.0f;
            for (const MeshVertexPnt& vertex : vertices)
            {
                const float deltaX = vertex.Position[0] - bounds.CenterX;
                const float deltaY = vertex.Position[1] - bounds.CenterY;
                const float deltaZ = vertex.Position[2] - bounds.CenterZ;
                radiusSquared = std::max(radiusSquared, deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            }
            bounds.Radius = std::sqrt(radiusSquared);
            return bounds;
        }

        bool AppendStringReference(const AnsiString& value, VariableArray<uint8_t>& stringTable,
                                   StringRefWire& outReference, AnsiString& error)
        {
            if (value.empty())
            {
                outReference = {};
                return true;
            }
            if (value.size() > static_cast<size_t>(UINT32_MAX))
            {
                error = "material texture reference is too long";
                return false;
            }

            outReference.Offset = static_cast<uint64_t>(stringTable.size());
            outReference.Length = static_cast<uint32_t>(value.size());
            if (value.size() > std::numeric_limits<size_t>::max() - stringTable.size())
            {
                error = "mesh string table size overflow";
                return false;
            }
            stringTable.insert(stringTable.end(), value.begin(), value.end());
            return true;
        }

        void WriteStringReference(MeshByteArray& bytes, size_t offset, const StringRefWire& reference)
        {
            WriteLe64(bytes, offset + StringRefRecordOffset::StringOffset, reference.Offset);
            WriteLe32(bytes, offset + StringRefRecordOffset::StringLength, reference.Length);
            WriteLe32(bytes, offset + StringRefRecordOffset::Reserved0, 0);
        }

        bool ValidateCoarseClusters(const VariableArray<uint32_t>& sourceIndices,
                                    const VariableArray<MeshCluster>& coarseClusters,
                                    const VariableArray<uint32_t>& coarseIndices, uint32_t vertexCount,
                                    AnsiString& error)
        {
            if (coarseClusters.empty() || coarseIndices.empty() || coarseIndices.size() % 3 != 0 ||
                coarseIndices.size() > UINT32_MAX || coarseIndices.size() != sourceIndices.size())
            {
                error = "MeshClusterizer returned invalid counts";
                return false;
            }

            size_t expectedIndexOffset = 0;
            for (const MeshCluster& cluster : coarseClusters)
            {
                if (cluster.IndexOffset != expectedIndexOffset || cluster.IndexCount == 0 || cluster.IndexCount % 3 != 0 ||
                    cluster.IndexCount / 3 > Format::ClusterMaxTriangles ||
                    cluster.VertexOffset != 0 || (cluster.VertexCount > 0 && cluster.VertexCount > vertexCount) ||
                    !std::isfinite(cluster.Bounds.CenterX) || !std::isfinite(cluster.Bounds.CenterY) ||
                    !std::isfinite(cluster.Bounds.CenterZ) || !std::isfinite(cluster.Bounds.Radius) ||
                    cluster.Bounds.Radius < 0.0f || !std::isfinite(cluster.ConeAxisX) || !std::isfinite(cluster.ConeAxisY) ||
                    !std::isfinite(cluster.ConeAxisZ) || !std::isfinite(cluster.ConeCutoff) || cluster.MaterialIndex != 0 ||
                    cluster.LODLevel != 0 || cluster.LODError != 0.0f || cluster.ParentStart != 0 || cluster.ParentCount != 0)
                {
                    error = "MeshClusterizer returned unsupported NVMESH v0 cluster data";
                    return false;
                }

                if (expectedIndexOffset > coarseIndices.size() ||
                    cluster.IndexCount > coarseIndices.size() - expectedIndexOffset)
                {
                    error = "MeshClusterizer cluster index range is invalid";
                    return false;
                }
                expectedIndexOffset += cluster.IndexCount;
            }

            if (expectedIndexOffset != coarseIndices.size())
            {
                error = "MeshClusterizer cluster ranges do not cover the index stream";
                return false;
            }
            for (const uint32_t index : coarseIndices)
            {
                if (index >= vertexCount)
                {
                    error = "MeshClusterizer returned an out-of-range index";
                    return false;
                }
            }
            using Triangle = FixedArray<uint32_t, 3>;
            VariableArray<Triangle> sourceTriangles;
            VariableArray<Triangle> clusteredTriangles;
            sourceTriangles.reserve(sourceIndices.size() / 3);
            clusteredTriangles.reserve(coarseIndices.size() / 3);
            for (size_t index = 0; index + 2 < sourceIndices.size(); index += 3)
            {
                sourceTriangles.push_back({sourceIndices[index], sourceIndices[index + 1], sourceIndices[index + 2]});
            }
            for (size_t index = 0; index + 2 < coarseIndices.size(); index += 3)
            {
                clusteredTriangles.push_back({coarseIndices[index], coarseIndices[index + 1], coarseIndices[index + 2]});
            }

            // Triangles are compared as ordered triplets so a winding flip is still a failure.
            std::sort(sourceTriangles.begin(), sourceTriangles.end());
            std::sort(clusteredTriangles.begin(), clusteredTriangles.end());
            if (sourceTriangles != clusteredTriangles)
            {
                error = "MeshClusterizer dropped, duplicated or reassigned a source triangle";
                return false;
            }

            return true;
        }

        bool RefineClusters(const VariableArray<MeshVertexPnt>& vertices,
                            const VariableArray<MeshCluster>& coarseClusters,
                            const VariableArray<uint32_t>& coarseIndices, VariableArray<MeshCluster>& finalClusters,
                            VariableArray<uint32_t>& finalIndices, AnsiString& error)
        {
            finalClusters.clear();
            finalIndices.clear();
            finalClusters.reserve(coarseClusters.size());
            finalIndices.reserve(coarseIndices.size());

            VariableArray<uint32_t> chunkVertices;
            chunkVertices.reserve(Format::ClusterMaxVertices);
            for (const MeshCluster& coarseCluster : coarseClusters)
            {
                const size_t coarseBegin = static_cast<size_t>(coarseCluster.IndexOffset);
                size_t coarseEnd = 0;
                if (!CheckedAdd(coarseBegin, static_cast<size_t>(coarseCluster.IndexCount), coarseEnd) ||
                    coarseEnd > coarseIndices.size())
                {
                    error = "MeshClusterizer coarse cluster range overflowed during refinement";
                    return false;
                }

                size_t chunkStart = finalIndices.size();
                chunkVertices.clear();
                for (size_t triangleOffset = coarseBegin; triangleOffset < coarseEnd; triangleOffset += 3)
                {
                    size_t additionalVertexCount = 0;
                    FixedArray<uint32_t, 3> triangle = {
                        coarseIndices[triangleOffset], coarseIndices[triangleOffset + 1], coarseIndices[triangleOffset + 2]};
                    for (size_t triangleVertex = 0; triangleVertex < triangle.size(); ++triangleVertex)
                    {
                        const bool bAlreadyInChunk =
                            std::find(chunkVertices.begin(), chunkVertices.end(), triangle[triangleVertex]) !=
                            chunkVertices.end();
                        bool bAlreadyInTriangle = false;
                        for (size_t previousVertex = 0; previousVertex < triangleVertex; ++previousVertex)
                        {
                            bAlreadyInTriangle = bAlreadyInTriangle || triangle[previousVertex] == triangle[triangleVertex];
                        }
                        if (!bAlreadyInChunk && !bAlreadyInTriangle)
                        {
                            ++additionalVertexCount;
                        }
                    }

                    const size_t chunkIndexCount = finalIndices.size() - chunkStart;
                    const size_t chunkTriangleCount = chunkIndexCount / 3;
                    if (chunkIndexCount > 0 &&
                        (chunkTriangleCount >= Format::ClusterMaxTriangles ||
                         additionalVertexCount > Format::ClusterMaxVertices - chunkVertices.size()))
                    {
                        if (chunkStart > UINT32_MAX || chunkIndexCount > UINT32_MAX)
                        {
                            error = "refined cluster range exceeds NVMESH v0 limits";
                            return false;
                        }
                        MeshCluster refinedCluster;
                        refinedCluster.IndexOffset = static_cast<uint32_t>(chunkStart);
                        refinedCluster.IndexCount = static_cast<uint32_t>(chunkIndexCount);
                        refinedCluster.VertexOffset = 0;
                        refinedCluster.VertexCount = 0;
                        finalClusters.push_back(refinedCluster);
                        chunkStart = finalIndices.size();
                        chunkVertices.clear();
                    }

                    for (const uint32_t index : triangle)
                    {
                        if (std::find(chunkVertices.begin(), chunkVertices.end(), index) == chunkVertices.end())
                        {
                            chunkVertices.push_back(index);
                        }
                        finalIndices.push_back(index);
                    }
                }

                const size_t chunkIndexCount = finalIndices.size() - chunkStart;
                if (chunkIndexCount == 0 || chunkStart > UINT32_MAX || chunkIndexCount > UINT32_MAX)
                {
                    error = "refined cluster range is invalid";
                    return false;
                }
                MeshCluster refinedCluster;
                refinedCluster.IndexOffset = static_cast<uint32_t>(chunkStart);
                refinedCluster.IndexCount = static_cast<uint32_t>(chunkIndexCount);
                refinedCluster.VertexOffset = 0;
                refinedCluster.VertexCount = 0;
                finalClusters.push_back(refinedCluster);
            }

            if (finalIndices.size() != coarseIndices.size() || finalIndices.size() > UINT32_MAX)
            {
                error = "cluster refinement changed the index count";
                return false;
            }

            const uint32_t* pFinalIndexData = finalIndices.data();
            for (MeshCluster& cluster : finalClusters)
            {
                MeshClusterizer::ComputeBoundingSphere(vertices.data(), static_cast<uint32_t>(sizeof(MeshVertexPnt)),
                                                       pFinalIndexData, cluster);
                MeshClusterizer::ComputeNormalCone(vertices.data(), static_cast<uint32_t>(sizeof(MeshVertexPnt)),
                                                   pFinalIndexData, cluster);
            }
            return true;
        }

        bool ValidateFinalClusters(const VariableArray<uint32_t>& coarseIndices,
                                   const VariableArray<MeshCluster>& finalClusters,
                                   const VariableArray<uint32_t>& finalIndices, uint32_t vertexCount,
                                   AnsiString& error)
        {
            if (finalClusters.empty() || finalIndices != coarseIndices)
            {
                error = "cluster refinement changed the ordered index stream";
                return false;
            }

            size_t expectedIndexOffset = 0;
            VariableArray<uint32_t> clusterVertices;
            clusterVertices.reserve(Format::ClusterMaxVertices);
            for (const MeshCluster& cluster : finalClusters)
            {
                if (cluster.IndexOffset != expectedIndexOffset || cluster.IndexCount == 0 || cluster.IndexCount % 3 != 0 ||
                    cluster.IndexCount / 3 > Format::ClusterMaxTriangles || cluster.VertexOffset != 0 ||
                    cluster.VertexCount != 0 || cluster.MaterialIndex != 0 || cluster.LODLevel != 0 ||
                    cluster.LODError != 0.0f || cluster.ParentStart != 0 || cluster.ParentCount != 0 ||
                    !std::isfinite(cluster.Bounds.CenterX) || !std::isfinite(cluster.Bounds.CenterY) ||
                    !std::isfinite(cluster.Bounds.CenterZ) || !std::isfinite(cluster.Bounds.Radius) ||
                    cluster.Bounds.Radius < 0.0f || !std::isfinite(cluster.ConeAxisX) ||
                    !std::isfinite(cluster.ConeAxisY) || !std::isfinite(cluster.ConeAxisZ) ||
                    !std::isfinite(cluster.ConeCutoff))
                {
                    error = "refined cluster contains unsupported NVMESH v0 data";
                    return false;
                }
                if (expectedIndexOffset > finalIndices.size() ||
                    cluster.IndexCount > finalIndices.size() - expectedIndexOffset)
                {
                    error = "refined cluster index range is invalid";
                    return false;
                }

                const size_t indexEnd = expectedIndexOffset + cluster.IndexCount;
                clusterVertices.assign(finalIndices.begin() + static_cast<std::ptrdiff_t>(expectedIndexOffset),
                                       finalIndices.begin() + static_cast<std::ptrdiff_t>(indexEnd));
                for (const uint32_t index : clusterVertices)
                {
                    if (index >= vertexCount)
                    {
                        error = "refined cluster contains an out-of-range index";
                        return false;
                    }
                }
                std::sort(clusterVertices.begin(), clusterVertices.end());
                clusterVertices.erase(std::unique(clusterVertices.begin(), clusterVertices.end()), clusterVertices.end());
                if (clusterVertices.size() > Format::ClusterMaxVertices)
                {
                    error = "refined cluster exceeds the NVMESH v0 vertex limit";
                    return false;
                }
                expectedIndexOffset = indexEnd;
            }

            if (expectedIndexOffset != finalIndices.size())
            {
                error = "refined cluster ranges do not cover the index stream";
                return false;
            }
            return true;
        }

        bool BuildNvmeshBytes(const VariableArray<MeshVertexPnt>& vertices,
                              const NorvesLib::Core::Container::VariableArray<MeshCluster>& clusters,
                              const NorvesLib::Core::Container::VariableArray<uint32_t>& indices,
                              const MaterialReferences& materialReferences, MeshByteArray& outBytes, AnsiString& error)
        {
            if (vertices.empty() || vertices.size() > UINT32_MAX || clusters.empty() || clusters.size() > UINT32_MAX ||
                indices.empty() || indices.size() > UINT32_MAX)
            {
                if (error.empty())
                {
                    error = "mesh counts exceed NVMESH v0 limits";
                }
                return false;
            }

            VariableArray<uint8_t> stringTable;
            StringRefWire albedoReference;
            StringRefWire normalReference;
            StringRefWire armReference;
            if (!AppendStringReference(materialReferences.Albedo, stringTable, albedoReference, error) ||
                !AppendStringReference(materialReferences.Normal, stringTable, normalReference, error) ||
                !AppendStringReference(materialReferences.Arm, stringTable, armReference, error))
            {
                return false;
            }

            if (stringTable.size() > UINT32_MAX)
            {
                error = "mesh string table exceeds the NVMESH v0 32-bit size limit";
                return false;
            }

            size_t clusterTableSize = 0;
            size_t vertexPayloadSize = 0;
            size_t indexPayloadSize = 0;
            if (!CheckedMultiply(clusters.size(), Format::ClusterRecordSize, clusterTableSize) ||
                !CheckedMultiply(vertices.size(), Format::VertexRecordSize, vertexPayloadSize) ||
                !CheckedMultiply(indices.size(), sizeof(uint32_t), indexPayloadSize))
            {
                error = "mesh section size overflow";
                return false;
            }

            const size_t submeshTableOffset = Format::HeaderSize;
            size_t materialTableOffset = 0;
            size_t clusterTableOffset = 0;
            size_t stringTableOffset = 0;
            size_t vertexPayloadOffset = 0;
            size_t indexPayloadOffset = 0;
            size_t fileSize = 0;
            size_t sectionEnd = 0;
            if (!CheckedAdd(submeshTableOffset, Format::SubmeshRecordSize, sectionEnd) ||
                !AlignUp(sectionEnd, Format::SectionAlignment, materialTableOffset) ||
                !CheckedAdd(materialTableOffset, Format::MaterialRecordSize, sectionEnd) ||
                !AlignUp(sectionEnd, Format::SectionAlignment, clusterTableOffset) ||
                !CheckedAdd(clusterTableOffset, clusterTableSize, sectionEnd) ||
                !AlignUp(sectionEnd, Format::SectionAlignment, stringTableOffset) ||
                !CheckedAdd(stringTableOffset, stringTable.size(), sectionEnd) ||
                !AlignUp(sectionEnd, Format::SectionAlignment, vertexPayloadOffset) ||
                !CheckedAdd(vertexPayloadOffset, vertexPayloadSize, sectionEnd) ||
                !AlignUp(sectionEnd, Format::SectionAlignment, indexPayloadOffset) ||
                !CheckedAdd(indexPayloadOffset, indexPayloadSize, fileSize))
            {
                error = "mesh section offset overflow";
                return false;
            }

            const BoundsSphere totalBounds = CalculateBounds(vertices);
            if (!std::isfinite(totalBounds.CenterX) || !std::isfinite(totalBounds.CenterY) ||
                !std::isfinite(totalBounds.CenterZ) || !std::isfinite(totalBounds.Radius) || totalBounds.Radius < 0.0f)
            {
                error = "mesh total bounds are invalid";
                return false;
            }

            outBytes.assign(fileSize, 0);
            std::memcpy(outBytes.data() + HeaderOffset::Magic, Format::Magic, Format::MagicSize);
            WriteLe32(outBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(Format::HeaderSize));
            WriteLe16(outBytes, HeaderOffset::VersionMajor, Format::VersionMajor);
            WriteLe16(outBytes, HeaderOffset::VersionMinor, Format::VersionMinor);
            WriteLe32(outBytes, HeaderOffset::EndianMarker, Format::EndianMarker);
            WriteLe32(outBytes, HeaderOffset::VertexRecordSize, static_cast<uint32_t>(Format::VertexRecordSize));
            WriteLe32(outBytes, HeaderOffset::SubmeshRecordSize, static_cast<uint32_t>(Format::SubmeshRecordSize));
            WriteLe32(outBytes, HeaderOffset::MaterialRecordSize, static_cast<uint32_t>(Format::MaterialRecordSize));
            WriteLe32(outBytes, HeaderOffset::ClusterRecordSize, static_cast<uint32_t>(Format::ClusterRecordSize));
            WriteLe32(outBytes, HeaderOffset::StringRefRecordSize, static_cast<uint32_t>(Format::StringRefRecordSize));
            WriteLe64(outBytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
            WriteLe64(outBytes, HeaderOffset::SubmeshTableOffset, static_cast<uint64_t>(submeshTableOffset));
            WriteLe64(outBytes, HeaderOffset::SubmeshTableSize, Format::SubmeshRecordSize);
            WriteLe64(outBytes, HeaderOffset::MaterialTableOffset, static_cast<uint64_t>(materialTableOffset));
            WriteLe64(outBytes, HeaderOffset::MaterialTableSize, Format::MaterialRecordSize);
            WriteLe64(outBytes, HeaderOffset::ClusterTableOffset, static_cast<uint64_t>(clusterTableOffset));
            WriteLe64(outBytes, HeaderOffset::ClusterTableSize, static_cast<uint64_t>(clusterTableSize));
            WriteLe64(outBytes, HeaderOffset::StringTableOffset, static_cast<uint64_t>(stringTableOffset));
            WriteLe64(outBytes, HeaderOffset::StringTableSize, static_cast<uint64_t>(stringTable.size()));
            WriteLe64(outBytes, HeaderOffset::VertexPayloadOffset, static_cast<uint64_t>(vertexPayloadOffset));
            WriteLe64(outBytes, HeaderOffset::VertexPayloadSize, static_cast<uint64_t>(vertexPayloadSize));
            WriteLe64(outBytes, HeaderOffset::IndexPayloadOffset, static_cast<uint64_t>(indexPayloadOffset));
            WriteLe64(outBytes, HeaderOffset::IndexPayloadSize, static_cast<uint64_t>(indexPayloadSize));
            WriteLe32(outBytes, HeaderOffset::VertexCount, static_cast<uint32_t>(vertices.size()));
            WriteLe32(outBytes, HeaderOffset::IndexCount, static_cast<uint32_t>(indices.size()));
            WriteLe32(outBytes, HeaderOffset::SubmeshCount, 1);
            WriteLe32(outBytes, HeaderOffset::MaterialCount, 1);
            WriteLe32(outBytes, HeaderOffset::ClusterCount, static_cast<uint32_t>(clusters.size()));
            WriteLe32(outBytes, HeaderOffset::StringByteCount, static_cast<uint32_t>(stringTable.size()));
            WriteFloat32(outBytes, HeaderOffset::TotalBoundsCenterX, totalBounds.CenterX);
            WriteFloat32(outBytes, HeaderOffset::TotalBoundsCenterY, totalBounds.CenterY);
            WriteFloat32(outBytes, HeaderOffset::TotalBoundsCenterZ, totalBounds.CenterZ);
            WriteFloat32(outBytes, HeaderOffset::TotalBoundsRadius, totalBounds.Radius);
            WriteLe32(outBytes, HeaderOffset::ClusterAlgorithmId, Format::ClusterAlgorithmId);
            WriteLe32(outBytes, HeaderOffset::ClusterAlgorithmVersion, Format::ClusterAlgorithmVersion);
            WriteLe32(outBytes, HeaderOffset::ClusterMaxTriangles, Format::ClusterMaxTriangles);
            WriteLe32(outBytes, HeaderOffset::ClusterMaxVertices, Format::ClusterMaxVertices);
            WriteLe32(outBytes, HeaderOffset::ClusterSettingsFlags, Format::ClusterSettingsFlags);

            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::IndexOffset, 0);
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::IndexCount, static_cast<uint32_t>(indices.size()));
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::VertexOffset, 0);
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::VertexCount, static_cast<uint32_t>(vertices.size()));
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::MaterialIndex, 0);
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::ClusterOffset, 0);
            WriteLe32(outBytes, submeshTableOffset + SubmeshRecordOffset::ClusterCount, static_cast<uint32_t>(clusters.size()));
            WriteFloat32(outBytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX, totalBounds.CenterX);
            WriteFloat32(outBytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterY, totalBounds.CenterY);
            WriteFloat32(outBytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterZ, totalBounds.CenterZ);
            WriteFloat32(outBytes, submeshTableOffset + SubmeshRecordOffset::BoundsRadius, totalBounds.Radius);

            WriteStringReference(outBytes, materialTableOffset + MaterialRecordOffset::AlbedoTexture, albedoReference);
            WriteStringReference(outBytes, materialTableOffset + MaterialRecordOffset::NormalTexture, normalReference);
            WriteStringReference(outBytes, materialTableOffset + MaterialRecordOffset::ArmTexture, armReference);

            for (size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
            {
                const MeshCluster& cluster = clusters[clusterIndex];
                const size_t recordOffset = clusterTableOffset + clusterIndex * Format::ClusterRecordSize;
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::BoundsCenterX, cluster.Bounds.CenterX);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::BoundsCenterY, cluster.Bounds.CenterY);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::BoundsCenterZ, cluster.Bounds.CenterZ);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::BoundsRadius, cluster.Bounds.Radius);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::ConeAxisX, cluster.ConeAxisX);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::ConeAxisY, cluster.ConeAxisY);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::ConeAxisZ, cluster.ConeAxisZ);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::ConeCutoff, cluster.ConeCutoff);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::IndexOffset, cluster.IndexOffset);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::IndexCount, cluster.IndexCount);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::VertexOffset, 0);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::VertexCount, cluster.VertexCount);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::MaterialIndex, cluster.MaterialIndex);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::LODLevel, cluster.LODLevel);
                WriteFloat32(outBytes, recordOffset + ClusterRecordOffset::LODError, cluster.LODError);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::ParentStart, cluster.ParentStart);
                WriteLe32(outBytes, recordOffset + ClusterRecordOffset::ParentCount, cluster.ParentCount);
            }

            if (!stringTable.empty())
            {
                std::memcpy(outBytes.data() + stringTableOffset, stringTable.data(), stringTable.size());
            }

            for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
            {
                const MeshVertexPnt& vertex = vertices[vertexIndex];
                const size_t recordOffset = vertexPayloadOffset + vertexIndex * Format::VertexRecordSize;
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::PositionX, vertex.Position[0]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::PositionY, vertex.Position[1]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::PositionZ, vertex.Position[2]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::NormalX, vertex.Normal[0]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::NormalY, vertex.Normal[1]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::NormalZ, vertex.Normal[2]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::TexCoordU, vertex.TexCoord[0]);
                WriteFloat32(outBytes, recordOffset + VertexRecordOffset::TexCoordV, vertex.TexCoord[1]);
            }

            for (size_t index = 0; index < indices.size(); ++index)
            {
                WriteLe32(outBytes, indexPayloadOffset + index * sizeof(uint32_t), indices[index]);
            }

            const uint64_t payloadHash =
                ComputeCookedMeshPayloadHash(outBytes.data() + submeshTableOffset, fileSize - submeshTableOffset);
            WriteLe64(outBytes, HeaderOffset::PayloadHash, payloadHash);
            return true;
        }
        bool InspectOptionalNumber(const JsonValue& object, const char* key, double minimum,
            double maximum, double& out)
        {
            const auto value=object.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsNumber())
            {
                return false;
            }
            const double number=value.AsNumber();
            if (!std::isfinite(number) || number<minimum || number>maximum)
            {
                return false;
            }
            out=number;
            return true;
        }
        template<size_t N>
        bool InspectOptionalFactor(const JsonValue& object,const char* key,double (&out)[N])
        {
            const auto value=object.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsArray() || value.GetArraySize()!=N)
            {
                return false;
            }
            for (size_t index=0;index<N;++index)
            {
                const auto item=value.GetArrayElement(index);
                if (!item.IsNumber() || !std::isfinite(item.AsNumber()) || item.AsNumber()<0 || item.AsNumber()>1)
                {
                    return false;
                }
                out[index]=item.AsNumber();
            }
            return true;
        }
        bool InspectMaterial(const JsonValue& material,MaterialInspection& out)
        {
            if (!material.IsObject())
            {
                return false;
            }
            const auto pbr=material.FindMember("pbrMetallicRoughness");
            if (pbr.IsValid() && (!pbr.IsObject() || !InspectOptionalFactor(pbr,"baseColorFactor",out.BaseColor) ||
                !InspectOptionalNumber(pbr,"metallicFactor",0,1,out.Metallic) ||
                !InspectOptionalNumber(pbr,"roughnessFactor",0,1,out.Roughness)))
            {
                return false;
            }
            if (!InspectOptionalFactor(material,"emissiveFactor",out.Emissive) ||
                !InspectOptionalNumber(material,"alphaCutoff",0,std::numeric_limits<double>::max(),out.AlphaCutoff))
            {
                return false;
            }
            const auto alpha=material.FindMember("alphaMode");
            if (alpha.IsValid())
            {
                AnsiString mode;
                if (!TryConvertAsciiString(alpha,mode))
                {
                    return false;
                }
                if (mode=="OPAQUE")
                {
                    out.AlphaMode=InspectionAlphaMode::Opaque;
                }
                else if (mode=="MASK")
                {
                    out.AlphaMode=InspectionAlphaMode::Mask;
                }
                else if (mode=="BLEND")
                {
                    out.AlphaMode=InspectionAlphaMode::Blend;
                }
                else
                {
                    return false;
                }
            }
            const auto doubleSided=material.FindMember("doubleSided");
            if (doubleSided.IsValid())
            {
                if (!doubleSided.IsBoolean())
                {
                    return false;
                }
                out.bDoubleSided=doubleSided.AsBool();
            }
            const auto normal=material.FindMember("normalTexture");
            const auto occlusion=material.FindMember("occlusionTexture");
            if ((normal.IsValid() && (!normal.IsObject() || !InspectOptionalNumber(normal,"scale",
                    -std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),out.NormalScale))) ||
                (occlusion.IsValid() && (!occlusion.IsObject() || !InspectOptionalNumber(occlusion,"strength",0,1,out.OcclusionStrength))))
            {
                return false;
            }
            const auto extensions=material.FindMember("extensions");
            if (extensions.IsValid())
            {
                if (!extensions.IsObject())
                {
                    return false;
                }
                const auto emissive=extensions.FindMember("KHR_materials_emissive_strength");
                if (emissive.IsValid() && (!emissive.IsObject() || !InspectOptionalNumber(emissive,"emissiveStrength",
                    0,std::numeric_limits<double>::max(),out.EmissiveStrength)))
                {
                    return false;
                }
            }
            return true;
        }

        bool InspectGltfModelInternal(const uint8_t* sourceBytes,size_t sourceSize,AnsiStringView sourcePath,
            ModelInspection& outInspection,AnsiString& error)
        {
            if (!sourceBytes || sourceSize==0)
            {
                error="inspect input is empty";
                return false;
            }
            Gltf::ContainerView container;
            const auto parsed=Gltf::ParseContainer({sourceBytes,sourceSize},container);
            if ((parsed!=Gltf::ContainerParseResult::Success && parsed!=Gltf::ContainerParseResult::NotGlb) ||
                container.Json.empty() || std::find(container.Json.begin(),container.Json.end(),uint8_t{0})!=container.Json.end())
            {
                error="invalid inspect glTF container";
                return false;
            }
            JsonDocument document;
            const AnsiStringView json(reinterpret_cast<const char*>(container.Json.data()),container.Json.size());
            if (!JsonDocument::TryParse(ToCoreString(json),document) || !ValidateRequiredExtensions(document.GetRoot(),error))
            {
                error="invalid inspect glTF document";
                return false;
            }
            const auto root=document.GetRoot();
            VariableArray<AccessorInfo> accessors;
            VariableArray<BufferViewInfo> views;
            PrimitiveInfo primitive;
            Gltf::BufferSet buffers;
            VariableArray<MeshVertexPnt> vertices;
            VariableArray<uint32_t> indices;
            if (!ParseAccessors(root,accessors,error) || !ParseBufferViews(root,views,error) ||
                !ParsePrimitive(root,primitive,error) || !ResolveCookBuffers(root,container,sourcePath,buffers,error) ||
                !ExtractMesh(accessors,views,buffers,primitive,vertices,indices,error))
            {
                return false;
            }
            ModelInspection result;
            VariableArray<InspectionVertex> inspectionVertices(vertices.size());
            VariableArray<uint32_t> order(vertices.size()),representatives(vertices.size()),parents(vertices.size());
            for (size_t index=0;index<vertices.size();++index)
            {
                for (size_t axis=0;axis<3;++axis)
                {
                    inspectionVertices[index].Position[axis]=vertices[index].Position[axis];
                    inspectionVertices[index].Normal[axis]=vertices[index].Normal[axis];
                }
            }
            if (!InspectGeometry(inspectionVertices,indices,order,representatives,parents,result.Geometry))
            {
                error="inspect geometry failed";
                return false;
            }
            const auto images=root.FindMember("images");
            if (images.IsValid() && !images.IsArray())
            {
                error="inspect images must be an array";
                return false;
            }
            Gltf::BufferFileContext imageContext{std::filesystem::path(sourcePath.begin(),sourcePath.end())};
            for (size_t index=0;index<images.GetArraySize();++index)
            {
                Gltf::ImageSource image;
                if (Gltf::ImageSource::Resolve(root,index,buffers,image)!=Gltf::ImageSourceResult::Success)
                {
                    error="inspect image source is invalid: "+FormatInteger(index);
                    return false;
                }
                VariableArray<uint8_t> fileBytes;
                auto bytes=image.GetBytes(buffers);
                if (image.GetKind()==Gltf::ImageSourceKind::ExternalFile)
                {
                    if (Gltf::ReadBufferFile(image.GetExternalUri(),fileBytes,&imageContext)!=Gltf::ExternalBufferReadResult::Success)
                    {
                        error="inspect image file could not be read: "+FormatInteger(index);
                        return false;
                    }
                    bytes={fileBytes.data(),fileBytes.size()};
                }
                if (image.GetMime()!=Gltf::DataUriMime::Unknown && !Gltf::MatchesEmbeddedImageMime(bytes,image.GetMime()))
                {
                    error="inspect image MIME does not match signature: "+FormatInteger(index);
                    return false;
                }
                ImageInspection inspection;
                const auto status=InspectImage(bytes,inspection);
                if (status!=ImageInspectionStatus::Success)
                {
                    error="inspect image decode failed: image="+FormatInteger(index)+" status="+FormatInteger(static_cast<int>(status));
                    return false;
                }
                result.Images.push_back(inspection);
            }
            const auto materials=root.FindMember("materials");
            if (materials.IsValid() && !materials.IsArray())
            {
                error="inspect materials must be an array";
                return false;
            }
            if (primitive.bHasMaterial && primitive.MaterialIndex>=materials.GetArraySize())
            {
                error="inspect primitive material is out of range";
                return false;
            }
            result.bHasMaterial=primitive.bHasMaterial;
            result.MaterialIndex=primitive.MaterialIndex;
            for (size_t index=0;index<materials.GetArraySize();++index)
            {
                MaterialInspection material;
                if (!InspectMaterial(materials.GetArrayElement(index),material))
                {
                    error="inspect material coefficients are invalid: "+FormatInteger(index);
                    return false;
                }
                result.Materials.push_back(material);
            }
            outInspection=std::move(result);
            return true;
        }

        bool LoadCookImportSettings(AnsiStringView sourcePath,
            const AssetImport::ImportSettingsFileOptions* importOptions,
            AssetImport::LoadedImportSettings& loadedImport, AnsiString& error)
        {
            const AssetImport::ImportSettingsFileOptions automaticImport;
            const auto& effectiveImport = importOptions != nullptr ? *importOptions : automaticImport;
            AssetImport::SettingsFileOutcome outcome;
            // source locator無しの自己完結入力はauto探索だけ省略する。
            if (!sourcePath.empty() || effectiveImport.bRequired || !effectiveImport.OverridePath.empty())
            {
                outcome = AssetImport::LoadImportSettingsFile(
                    std::filesystem::path(sourcePath.begin(), sourcePath.end()), effectiveImport, loadedImport);
            }
            if (outcome.Result != AssetImport::SettingsFileResult::Success)
            {
                error = AnsiString("import settings rejected: file=") +
                    FormatInteger(static_cast<int>(outcome.Result)) + " validation=" +
                    FormatInteger(static_cast<int>(outcome.Validation));
                return false;
            }
            return true;
        }

        bool FingerprintModelCookSourceInternal(const uint8_t* sourceBytes, size_t sourceSize,
            AnsiStringView format, AnsiStringView sourcePath, AnsiStringView logicalPath,
            ModelCookFingerprint& outResult, AnsiString& error,
            const AssetImport::ImportSettingsFileOptions* importOptions,
            const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions)
        {
            if (format != SupportedMeshFormat && format != SupportedSkeletalFormat)
            {
                error = "unsupported model fingerprint format";
                return false;
            }
            const Core::Skeletal::SkeletalGltfDecodeOptions options = decodeOptions ? *decodeOptions : Core::Skeletal::SkeletalGltfDecodeOptions{};
            if ((format != SupportedSkeletalFormat && decodeOptions != nullptr) ||
                !Core::Skeletal::IsValidSkeletalGltfDecodeOptions(options))
            {
                error = "骨格import指定が不正、または静的meshには対応していません";
                return false;
            }
            if (!sourceBytes || sourceSize==0)
            {
                error = "glTF source input is empty";
                return false;
            }
            Gltf::ContainerView container;
            const auto parsed = Gltf::ParseContainer({sourceBytes,sourceSize},container);
            if ((parsed != Gltf::ContainerParseResult::Success && parsed != Gltf::ContainerParseResult::NotGlb) ||
                container.Json.empty() || std::find(container.Json.begin(),container.Json.end(),uint8_t{0})!=container.Json.end())
            {
                error = "invalid glTF container for fingerprint";
                return false;
            }
            if (!ValidateRelativePath(logicalPath,"model logical path",error))
            {
                return false;
            }
            JsonDocument document;
            const AnsiStringView json(reinterpret_cast<const char*>(container.Json.data()),container.Json.size());
            if (!JsonDocument::TryParse(ToCoreString(json),document) || !ValidateRequiredExtensions(document.GetRoot(),error))
            {
                error = "invalid glTF document for fingerprint";
                return false;
            }
            const auto root = document.GetRoot();
            Gltf::BufferSet buffers;
            AssetImport::LoadedImportSettings settings;
            if (!ResolveCookBuffers(root,container,sourcePath,buffers,error) ||
                !LoadCookImportSettings(sourcePath,importOptions,settings,error))
            {
                return false;
            }
            if (format==SupportedSkeletalFormat && settings.bPresent &&
                !AssetImport::SupportsSkeletalScaleImport(settings.Settings))
            {
                error = "skeletal import supports only uniform scale/fit; axes, mirror, origin and mesh changes are unsupported";
                return false;
            }
            if (format==SupportedSkeletalFormat)
            {
                const auto attributes=root.FindMember("meshes").GetArrayElement(0).FindMember("primitives").GetArrayElement(0).FindMember("attributes");
                if (options.InfluencePolicy == Core::Skeletal::SkeletalInfluencePolicy::Strict)
                {
                    const auto strict = Core::Skeletal::ValidateStrictInfluenceAttributes(attributes);
                    if (strict != Core::Skeletal::StrictInfluenceStatus::Success)
                    {
                        error = "skeletal strict influences rejected: status=" + FormatInteger(static_cast<int>(strict));
                        return false;
                    }
                }
                else
                {
                    VariableArray<Core::Skeletal::SkeletalInfluenceSet> sets;
                    if (Core::Skeletal::CollectSkeletalInfluenceSets(attributes, sets) != Core::Skeletal::InfluenceSetCollectionStatus::Success)
                    {
                        error = "骨格のjoint/weightセット記述が不正です";
                        return false;
                    }
                }
            }

            ModelCookFingerprint result;
            if (format==SupportedMeshFormat)
            {
                PrimitiveInfo primitive;
                MaterialReferences references;
                VariableArray<MeshEmbeddedImage> images;
                if (!ParsePrimitive(root,primitive,error) ||
                    !ResolveMaterialReferences(root,primitive,logicalPath,buffers,images,references,error))
                {
                    return false;
                }
                for (auto& image : images)
                {
                    ModelImageFingerprint metadata;
                    metadata.ImageIndex=image.ImageIndex;
                    metadata.LogicalPath=std::move(image.LogicalPath);
                    metadata.Format=std::move(image.Format);
                    metadata.SourceHash=image.SourceHash;
                    result.EmbeddedImages.push_back(std::move(metadata));
                }
                std::sort(result.EmbeddedImages.begin(),result.EmbeddedImages.end(),[](const auto& a,const auto& b)
                {
                    return a.ImageIndex<b.ImageIndex;
                });
            }
            const auto hash = AssetImport::AppendImportSettingsHash(
                ComputeGltfSourceHash(sourceBytes,sourceSize,buffers),settings.bPresent,settings.Settings);
            if (!hash.bValid)
            {
                error = "invalid import settings hash";
                return false;
            }
            const auto policyHash = Core::Skeletal::AppendSkeletalImportPolicyHash(hash.Value, options);
            if (!policyHash.bValid)
            {
                error = "骨格importのpolicy hashを生成できません";
                return false;
            }
            result.SourceHash = policyHash.Value;
            result.bHasImportSettings=settings.bPresent;
            result.ImportSettingsPath=AnsiString(settings.Path.generic_string().c_str());
            if (settings.bPresent)
            {
                result.ImportSettingsHash=AssetImport::AppendImportSettingsHash(
                    Format::Fnv1a64OffsetBasis,true,settings.Settings).Value;
            }
            outResult=std::move(result);
            return true;
        }

        bool CookGltfToNvmeshInternal(const uint8_t* sourceBytes, size_t sourceSize, AnsiStringView format,
                                      AnsiStringView sourcePath, AnsiStringView logicalPath, MeshCookResult& outResult,
                                      AnsiString& error, const AssetImport::ImportSettingsFileOptions* importOptions)
        {
            if (format != SupportedMeshFormat)
            {
                error = AnsiString("unsupported mesh format: ") + AnsiString(format);
                return false;
            }
            if (sourceBytes == nullptr || sourceSize == 0)
            {
                error = "glTF JSON input is empty";
                return false;
            }
            Gltf::ContainerView container;
            const auto containerResult = Gltf::ParseContainer({sourceBytes, sourceSize}, container);
            if (containerResult != Gltf::ContainerParseResult::Success &&
                containerResult != Gltf::ContainerParseResult::NotGlb)
            {
                error = "invalid GLB container";
                return false;
            }
            if (container.Json.empty())
            {
                error = "glTF JSON input is empty";
                return false;
            }
            if (std::find(container.Json.begin(), container.Json.end(), uint8_t{0}) != container.Json.end())
            {
                error = "glTF JSON input contains an embedded NUL byte";
                return false;
            }
            if (!ValidateRelativePath(logicalPath, "model logical path", error))
            {
                return false;
            }

            // JSONだけを文字列にし、BINは元sourceの寿命内で借用する。hashにはBOMも残す。
            const AnsiString jsonText(AnsiStringView(reinterpret_cast<const char*>(container.Json.data()),
                                                     container.Json.size()));
            JsonDocument document;
            NorvesLib::Core::Container::String parseError;
            if (!JsonDocument::TryParse(ToCoreString(AnsiStringView(jsonText)), document, &parseError))
            {
                error = "failed to parse glTF JSON";
                return false;
            }

            const JsonValue root = document.GetRoot();
            if (!root.IsObject())
            {
                error = "glTF root must be an object";
                return false;
            }
            if (!ValidateRequiredExtensions(root, error))
            {
                return false;
            }

            VariableArray<AccessorInfo> accessors;
            VariableArray<BufferViewInfo> bufferViews;
            PrimitiveInfo primitive;
            if (!ParseAccessors(root, accessors, error) || !ParseBufferViews(root, bufferViews, error) ||
                !ParsePrimitive(root, primitive, error))
            {
                return false;
            }

            Gltf::BufferSet buffers;
            if (!ResolveCookBuffers(root, container, sourcePath, buffers, error))
            {
                return false;
            }

            VariableArray<MeshVertexPnt> vertices;
            VariableArray<uint32_t> indices;
            if (!ExtractMesh(accessors, bufferViews, buffers, primitive, vertices, indices, error))
            {
                return false;
            }

            AssetImport::LoadedImportSettings loadedImport;
            if (!LoadCookImportSettings(sourcePath, importOptions, loadedImport, error))
            {
                return false;
            }
            if (loadedImport.bPresent)
            {
                if (vertices.size() > std::numeric_limits<size_t>::max() / sizeof(MeshVertexPnt))
                {
                    error = "import vertex byte size overflow";
                    return false;
                }
                const AssetImport::ImportVertexLayout layout{sizeof(MeshVertexPnt),
                    offsetof(MeshVertexPnt, Position), offsetof(MeshVertexPnt, Normal), offsetof(MeshVertexPnt, TexCoord)};
                const auto transformed = AssetImport::ApplyImportTransform(
                    {reinterpret_cast<uint8_t*>(vertices.data()), vertices.size() * sizeof(MeshVertexPnt)},
                    vertices.size(), layout, indices, loadedImport.Settings);
                if (transformed.Result != AssetImport::TransformResult::Success)
                {
                    error = AnsiString("import transform rejected: status=") + FormatInteger(static_cast<int>(transformed.Result));
                    return false;
                }
            }

            MeshCookResult result;
            MaterialReferences materialReferences;
            if (!ResolveMaterialReferences(root, primitive, logicalPath, buffers, result.EmbeddedImages, materialReferences, error))
            {
                return false;
            }

            std::sort(result.EmbeddedImages.begin(), result.EmbeddedImages.end(), [](const auto& left, const auto& right)
            {
                return left.ImageIndex < right.ImageIndex;
            });

            VariableArray<MeshCluster> coarseClusters;
            VariableArray<uint32_t> coarseIndices;
            MeshClusterizer::Clusterize(vertices.data(), static_cast<uint32_t>(vertices.size()),
                                        static_cast<uint32_t>(sizeof(MeshVertexPnt)), indices.data(),
                                        static_cast<uint32_t>(indices.size()), coarseClusters, coarseIndices);

            if (!ValidateCoarseClusters(indices, coarseClusters, coarseIndices, static_cast<uint32_t>(vertices.size()),
                                        error))
            {
                return false;
            }

            VariableArray<MeshCluster> finalClusters;
            VariableArray<uint32_t> finalIndices;
            if (!RefineClusters(vertices, coarseClusters, coarseIndices, finalClusters, finalIndices, error) ||
                !ValidateFinalClusters(coarseIndices, finalClusters, finalIndices,
                                       static_cast<uint32_t>(vertices.size()), error))
            {
                return false;
            }

            if (!BuildNvmeshBytes(vertices, finalClusters, finalIndices, materialReferences, result.NvmeshBytes, error))
            {
                return false;
            }

            const NorvesLib::Core::Container::Span<const uint8_t> meshSpan(result.NvmeshBytes.data(),
                                                                           result.NvmeshBytes.size());
            const auto parseResult = ParseCookedMesh(AssetBlob::CopyBytes(meshSpan, "AssetCook mesh self-validation"));
            if (!parseResult.Succeeded())
            {
                error = AnsiString("generated NVMESH failed self-validation: status=") +
                        FormatInteger(static_cast<int>(parseResult.Status));
                return false;
            }

            const auto sourceHash = AssetImport::AppendImportSettingsHash(
                ComputeGltfSourceHash(sourceBytes, sourceSize, buffers), loadedImport.bPresent, loadedImport.Settings);
            if (!sourceHash.bValid)
            {
                error = "invalid import settings hash";
                return false;
            }
            result.SourceHash = sourceHash.Value;
            result.bHasImportSettings = loadedImport.bPresent;
            result.ImportSettingsPath = AnsiString(loadedImport.Path.generic_string().c_str());
            if (loadedImport.bPresent)
            {
                result.ImportSettingsHash = AssetImport::AppendImportSettingsHash(
                    Format::Fnv1a64OffsetBasis, true, loadedImport.Settings).Value;
            }
            result.VertexCount = static_cast<uint32_t>(vertices.size());
            result.IndexCount = static_cast<uint32_t>(finalIndices.size());
            result.ClusterCount = static_cast<uint32_t>(finalClusters.size());
            outResult = std::move(result);
            return true;
        }

        struct SkeletalStringReference
        {
            uint64_t Offset = 0;
            uint32_t Length = 0;
        };

        bool AppendSkeletalString(const String& value,
                                  VariableArray<uint8_t>& stringTable,
                                  SkeletalStringReference& outReference,
                                  AnsiString& error)
        {
            if (value.size() > UINT32_MAX || stringTable.size() > UINT32_MAX - value.size())
            {
                error = "skeletal string table exceeds the NVSKEL v0 32-bit limit";
                return false;
            }
            outReference.Offset = stringTable.size();
            outReference.Length = static_cast<uint32_t>(value.size());
            for (const auto character : value)
            {
                const uint32_t codePoint = static_cast<uint32_t>(character);
                if (codePoint < 0x20u || codePoint > 0x7eu)
                {
                    error = "NVSKEL v0 names must contain printable ASCII only";
                    return false;
                }
                stringTable.push_back(static_cast<uint8_t>(codePoint));
            }
            return true;
        }

        bool BuildNvskelBytes(const NorvesLib::Core::Skeletal::SkeletalGltfData& skeletal,
                              MeshByteArray& outBytes,
                              AnsiString& error)
        {
            namespace SkeletalFormat = NorvesLib::Core::Asset::CookedSkeletalFormatV0;
            namespace SkeletalHeader = SkeletalFormat::HeaderOffset;
            if (skeletal.Vertices.empty() || skeletal.Indices.empty() || skeletal.Joints.empty() ||
                skeletal.Clips.size() != 1 || skeletal.Joints.size() > Core::Skeletal::LegacyMaximumJointCount ||
                skeletal.Vertices.size() > UINT32_MAX || skeletal.Indices.size() > UINT32_MAX)
            {
                error = "skeletal data exceeds the NVSKEL v0 count contract";
                return false;
            }

            size_t channelCount = 0;
            size_t sampleCount = 0;
            for (const NorvesLib::Core::Skeletal::SkeletalAnimationClip& clip : skeletal.Clips)
            {
                if (!CheckedAdd(channelCount, clip.Channels.size(), channelCount))
                {
                    error = "skeletal channel count overflow";
                    return false;
                }
                for (const NorvesLib::Core::Skeletal::SkeletalAnimationChannel& channel : clip.Channels)
                {
                    if (!CheckedAdd(sampleCount, channel.Samples.size(), sampleCount))
                    {
                        error = "skeletal sample count overflow";
                        return false;
                    }
                }
            }
            if (channelCount > UINT32_MAX || sampleCount > UINT32_MAX)
            {
                error = "skeletal animation counts exceed the NVSKEL v0 32-bit limit";
                return false;
            }

            VariableArray<uint8_t> stringTable;
            VariableArray<SkeletalStringReference> jointNames(skeletal.Joints.size());
            VariableArray<SkeletalStringReference> clipNames(skeletal.Clips.size());
            for (size_t jointIndex = 0; jointIndex < skeletal.Joints.size(); ++jointIndex)
            {
                if (!AppendSkeletalString(skeletal.Joints[jointIndex].Name, stringTable, jointNames[jointIndex], error))
                {
                    return false;
                }
            }
            for (size_t clipIndex = 0; clipIndex < skeletal.Clips.size(); ++clipIndex)
            {
                if (!AppendSkeletalString(skeletal.Clips[clipIndex].Name, stringTable, clipNames[clipIndex], error))
                {
                    return false;
                }
            }

            size_t vertexSize = 0;
            size_t indexSize = 0;
            size_t jointSize = 0;
            size_t clipSize = 0;
            size_t channelSize = 0;
            size_t sampleSize = 0;
            if (!CheckedMultiply(skeletal.Vertices.size(), SkeletalFormat::VertexRecordSize, vertexSize) ||
                !CheckedMultiply(skeletal.Indices.size(), sizeof(uint32_t), indexSize) ||
                !CheckedMultiply(skeletal.Joints.size(), SkeletalFormat::JointRecordSize, jointSize) ||
                !CheckedMultiply(skeletal.Clips.size(), SkeletalFormat::ClipRecordSize, clipSize) ||
                !CheckedMultiply(channelCount, SkeletalFormat::ChannelRecordSize, channelSize) ||
                !CheckedMultiply(sampleCount, SkeletalFormat::SampleRecordSize, sampleSize))
            {
                error = "skeletal section size overflow";
                return false;
            }

            const size_t vertexOffset = SkeletalFormat::HeaderSize;
            size_t sectionEnd = 0;
            size_t indexOffset = 0;
            size_t jointOffset = 0;
            size_t clipOffset = 0;
            size_t channelOffset = 0;
            size_t sampleOffset = 0;
            size_t stringOffset = 0;
            size_t fileSize = 0;
            if (!CheckedAdd(vertexOffset, vertexSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, indexOffset) ||
                !CheckedAdd(indexOffset, indexSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, jointOffset) ||
                !CheckedAdd(jointOffset, jointSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, clipOffset) ||
                !CheckedAdd(clipOffset, clipSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, channelOffset) ||
                !CheckedAdd(channelOffset, channelSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, sampleOffset) ||
                !CheckedAdd(sampleOffset, sampleSize, sectionEnd) ||
                !AlignUp(sectionEnd, SkeletalFormat::SectionAlignment, stringOffset) ||
                !CheckedAdd(stringOffset, stringTable.size(), fileSize))
            {
                error = "skeletal section offset overflow";
                return false;
            }

            outBytes.assign(fileSize, 0);
            std::memcpy(outBytes.data() + SkeletalHeader::Magic, SkeletalFormat::Magic, SkeletalFormat::MagicSize);
            WriteLe32(outBytes, SkeletalHeader::HeaderSize, static_cast<uint32_t>(SkeletalFormat::HeaderSize));
            WriteLe16(outBytes, SkeletalHeader::VersionMajor, SkeletalFormat::VersionMajor);
            WriteLe16(outBytes, SkeletalHeader::VersionMinor, SkeletalFormat::VersionMinor);
            WriteLe32(outBytes, SkeletalHeader::EndianMarker, SkeletalFormat::EndianMarker);
            WriteLe32(outBytes, SkeletalHeader::VertexRecordSize,
                      static_cast<uint32_t>(SkeletalFormat::VertexRecordSize));
            WriteLe32(outBytes, SkeletalHeader::JointRecordSize,
                      static_cast<uint32_t>(SkeletalFormat::JointRecordSize));
            WriteLe32(outBytes, SkeletalHeader::ClipRecordSize,
                      static_cast<uint32_t>(SkeletalFormat::ClipRecordSize));
            WriteLe32(outBytes, SkeletalHeader::ChannelRecordSize,
                      static_cast<uint32_t>(SkeletalFormat::ChannelRecordSize));
            WriteLe32(outBytes, SkeletalHeader::SampleRecordSize,
                      static_cast<uint32_t>(SkeletalFormat::SampleRecordSize));
            WriteLe64(outBytes, SkeletalHeader::FileSize, fileSize);
            WriteLe64(outBytes, SkeletalHeader::VertexOffset, vertexOffset);
            WriteLe64(outBytes, SkeletalHeader::VertexSize, vertexSize);
            WriteLe64(outBytes, SkeletalHeader::IndexOffset, indexOffset);
            WriteLe64(outBytes, SkeletalHeader::IndexSize, indexSize);
            WriteLe64(outBytes, SkeletalHeader::JointOffset, jointOffset);
            WriteLe64(outBytes, SkeletalHeader::JointSize, jointSize);
            WriteLe64(outBytes, SkeletalHeader::ClipOffset, clipOffset);
            WriteLe64(outBytes, SkeletalHeader::ClipSize, clipSize);
            WriteLe64(outBytes, SkeletalHeader::ChannelOffset, channelOffset);
            WriteLe64(outBytes, SkeletalHeader::ChannelSize, channelSize);
            WriteLe64(outBytes, SkeletalHeader::SampleOffset, sampleOffset);
            WriteLe64(outBytes, SkeletalHeader::SampleSize, sampleSize);
            WriteLe64(outBytes, SkeletalHeader::StringOffset, stringOffset);
            WriteLe64(outBytes, SkeletalHeader::StringSize, stringTable.size());
            WriteLe32(outBytes, SkeletalHeader::VertexCount, static_cast<uint32_t>(skeletal.Vertices.size()));
            WriteLe32(outBytes, SkeletalHeader::IndexCount, static_cast<uint32_t>(skeletal.Indices.size()));
            WriteLe32(outBytes, SkeletalHeader::JointCount, static_cast<uint32_t>(skeletal.Joints.size()));
            WriteLe32(outBytes, SkeletalHeader::ClipCount, static_cast<uint32_t>(skeletal.Clips.size()));
            WriteLe32(outBytes, SkeletalHeader::ChannelCount, static_cast<uint32_t>(channelCount));
            WriteLe32(outBytes, SkeletalHeader::SampleCount, static_cast<uint32_t>(sampleCount));
            for (size_t element = 0; element < 16; ++element)
            {
                WriteFloat32(outBytes,
                             SkeletalHeader::MeshNodeGlobalTransform + element * sizeof(float),
                             skeletal.MeshNodeGlobalTransform[element]);
            }

            for (size_t vertexIndex = 0; vertexIndex < skeletal.Vertices.size(); ++vertexIndex)
            {
                const auto& vertex = skeletal.Vertices[vertexIndex];
                const size_t record = vertexOffset + vertexIndex * SkeletalFormat::VertexRecordSize;
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Position + 0, vertex.Position.X);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Position + 4, vertex.Position.Y);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Position + 8, vertex.Position.Z);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Normal + 0, vertex.Normal.X);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Normal + 4, vertex.Normal.Y);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::Normal + 8, vertex.Normal.Z);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::TexCoord + 0, vertex.TexCoord.U);
                WriteFloat32(outBytes, record + SkeletalFormat::VertexOffset::TexCoord + 4, vertex.TexCoord.V);
                for (size_t influence = 0; influence < 4; ++influence)
                {
                    WriteLe32(outBytes,
                              record + SkeletalFormat::VertexOffset::JointIndices + influence * sizeof(uint32_t),
                              vertex.JointIndices[influence]);
                    WriteFloat32(outBytes,
                                 record + SkeletalFormat::VertexOffset::JointWeights + influence * sizeof(float),
                                 vertex.JointWeights[influence]);
                }
            }
            for (size_t index = 0; index < skeletal.Indices.size(); ++index)
            {
                WriteLe32(outBytes, indexOffset + index * sizeof(uint32_t), skeletal.Indices[index]);
            }
            for (size_t jointIndex = 0; jointIndex < skeletal.Joints.size(); ++jointIndex)
            {
                const auto& joint = skeletal.Joints[jointIndex];
                const size_t record = jointOffset + jointIndex * SkeletalFormat::JointRecordSize;
                WriteLe32(outBytes, record + SkeletalFormat::JointOffset::ParentIndex,
                          static_cast<uint32_t>(joint.ParentIndex));
                WriteLe32(outBytes, record + SkeletalFormat::JointOffset::NameOffset,
                          static_cast<uint32_t>(jointNames[jointIndex].Offset));
                WriteLe32(outBytes, record + SkeletalFormat::JointOffset::NameSize, jointNames[jointIndex].Length);
                for (size_t element = 0; element < 16; ++element)
                {
                    WriteFloat32(outBytes,
                                 record + SkeletalFormat::JointOffset::InverseBindMatrix + element * sizeof(float),
                                 joint.InverseBindMatrix[element]);
                }
            }

            size_t channelCursor = 0;
            size_t sampleCursor = 0;
            for (size_t clipIndex = 0; clipIndex < skeletal.Clips.size(); ++clipIndex)
            {
                const auto& clip = skeletal.Clips[clipIndex];
                const size_t clipRecord = clipOffset + clipIndex * SkeletalFormat::ClipRecordSize;
                WriteLe64(outBytes, clipRecord + SkeletalFormat::ClipOffset::NameOffset,
                          clipNames[clipIndex].Offset);
                WriteLe32(outBytes, clipRecord + SkeletalFormat::ClipOffset::NameSize, clipNames[clipIndex].Length);
                WriteFloat32(outBytes, clipRecord + SkeletalFormat::ClipOffset::Duration, clip.DurationSeconds);
                WriteLe32(outBytes, clipRecord + SkeletalFormat::ClipOffset::ChannelOffset,
                          static_cast<uint32_t>(channelCursor));
                WriteLe32(outBytes, clipRecord + SkeletalFormat::ClipOffset::ChannelCount,
                          static_cast<uint32_t>(clip.Channels.size()));
                for (const auto& channel : clip.Channels)
                {
                    const size_t channelRecord = channelOffset + channelCursor * SkeletalFormat::ChannelRecordSize;
                    WriteLe32(outBytes, channelRecord + SkeletalFormat::ChannelOffset::JointIndex, channel.JointIndex);
                    WriteLe32(outBytes, channelRecord + SkeletalFormat::ChannelOffset::Path,
                              static_cast<uint32_t>(channel.Path));
                    WriteLe32(outBytes, channelRecord + SkeletalFormat::ChannelOffset::Interpolation,
                              static_cast<uint32_t>(channel.Interpolation));
                    WriteLe32(outBytes, channelRecord + SkeletalFormat::ChannelOffset::SampleOffset,
                              static_cast<uint32_t>(sampleCursor));
                    WriteLe32(outBytes, channelRecord + SkeletalFormat::ChannelOffset::SampleCount,
                              static_cast<uint32_t>(channel.Samples.size()));
                    for (const auto& sample : channel.Samples)
                    {
                        const size_t sampleRecord = sampleOffset + sampleCursor * SkeletalFormat::SampleRecordSize;
                        WriteFloat32(outBytes, sampleRecord + SkeletalFormat::SampleOffset::Time, sample.TimeSeconds);
                        WriteFloat32(outBytes, sampleRecord + SkeletalFormat::SampleOffset::Value + 0, sample.Value.X);
                        WriteFloat32(outBytes, sampleRecord + SkeletalFormat::SampleOffset::Value + 4, sample.Value.Y);
                        WriteFloat32(outBytes, sampleRecord + SkeletalFormat::SampleOffset::Value + 8, sample.Value.Z);
                        WriteFloat32(outBytes, sampleRecord + SkeletalFormat::SampleOffset::Value + 12, sample.Value.W);
                        ++sampleCursor;
                    }
                    ++channelCursor;
                }
            }
            if (!stringTable.empty())
            {
                std::memcpy(outBytes.data() + stringOffset, stringTable.data(), stringTable.size());
            }

            const uint64_t payloadHash = NorvesLib::Core::Asset::ComputeCookedSkeletalV01Hash(
                outBytes.data() + SkeletalHeader::MeshNodeGlobalTransform,
                outBytes.data() + SkeletalFormat::HeaderSize,
                outBytes.size() - SkeletalFormat::HeaderSize);
            WriteLe64(outBytes, SkeletalHeader::PayloadHash, payloadHash);
            return true;
        }

        bool CookGltfToNvskelInternal(const uint8_t* sourceBytes,
                                      size_t sourceSize,
                                      AnsiStringView format,
                                      AnsiStringView sourcePath,
                                      SkeletalCookResult& outResult,
                                      AnsiString& error,
                          const Core::AssetImport::ImportSettingsFileOptions* importOptions,
                          const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions)
        {
            if (format != SupportedSkeletalFormat)
            {
                error = AnsiString("unsupported skeletal format: ") + AnsiString(format);
                return false;
            }
            if (sourceBytes == nullptr || sourceSize == 0)
            {
                error = "glTF source input is empty";
                return false;
            }
            const Core::Skeletal::SkeletalGltfDecodeOptions options = decodeOptions ? *decodeOptions : Core::Skeletal::SkeletalGltfDecodeOptions{};
            if (!Core::Skeletal::IsValidSkeletalGltfDecodeOptions(options))
            {
                error = "骨格import指定が不正です";
                return false;
            }
            AssetImport::LoadedImportSettings loadedImport;
            if (!LoadCookImportSettings(sourcePath, importOptions, loadedImport, error))
            {
                return false;
            }
            if (loadedImport.bPresent && !AssetImport::SupportsSkeletalScaleImport(loadedImport.Settings))
            {
                error = "skeletal import supports only uniform scale/fit; axes, mirror, origin and mesh changes are unsupported";
                return false;
            }
            Gltf::BufferSet sourceBuffers;
            const auto decoded = NorvesLib::Core::Skeletal::DecodeSkeletalGltf(
                {sourceBytes, sourceSize}, ToCoreString(sourcePath), &sourceBuffers, &loadedImport, &options);
            if (!decoded.Succeeded())
            {
                error = AnsiString("skeletal glTF decode failed: status=") +
                        FormatInteger(static_cast<int>(decoded.Status));
                if (options.InfluencePolicy == Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour)
                {
                    error += " processed_vertices=" + FormatInteger(decoded.Report.ProcessedVertexCount);
                    if (decoded.Report.FailedVertexIndex != UINT64_MAX)
                        error += " failed_vertex=" + FormatInteger(decoded.Report.FailedVertexIndex);
                    if (decoded.Report.bHasFailedVertexDroppedWeight)
                    {
                        char number[64] = {};
                        const auto converted = std::to_chars(number, number + sizeof(number),
                            decoded.Report.FailedVertexDroppedWeight, std::chars_format::general,
                            std::numeric_limits<double>::max_digits10);
                        if (converted.ec == std::errc{})
                            error += " dropped_weight=" + AnsiString(AnsiStringView(number, static_cast<size_t>(converted.ptr - number)));
                    }
                }
                return false;
            }

            SkeletalCookResult result;
            if (!BuildNvskelBytes(decoded.Data, result.NvskelBytes, error))
            {
                return false;
            }
            const NorvesLib::Core::Container::Span<const uint8_t> span(result.NvskelBytes.data(),
                                                                        result.NvskelBytes.size());
            const auto parsed = NorvesLib::Core::Asset::ParseCookedSkeletal(
                AssetBlob::CopyBytes(span, "AssetCook skeletal self-validation"));
            if (!parsed.Succeeded())
            {
                error = AnsiString("generated NVSKEL failed self-validation: status=") +
                        FormatInteger(static_cast<int>(parsed.Status));
                return false;
            }

            const auto sourceHash = AssetImport::AppendImportSettingsHash(
                ComputeGltfSourceHash(sourceBytes, sourceSize, sourceBuffers), loadedImport.bPresent, loadedImport.Settings);
            if (!sourceHash.bValid)
            {
                error = "invalid import settings hash";
                return false;
            }
            const auto policyHash = Core::Skeletal::AppendSkeletalImportPolicyHash(sourceHash.Value, options);
            if (!policyHash.bValid)
            {
                error = "骨格importのpolicy hashを生成できません";
                return false;
            }
            result.SourceHash = policyHash.Value;
            result.DecodeReport = decoded.Report;
            result.bHasImportSettings = loadedImport.bPresent;
            result.ImportSettingsPath = AnsiString(loadedImport.Path.generic_string().c_str());
            if (loadedImport.bPresent)
            {
                result.ImportSettingsHash = AssetImport::AppendImportSettingsHash(
                    Format::Fnv1a64OffsetBasis, true, loadedImport.Settings).Value;
            }
            result.VertexCount = static_cast<uint32_t>(decoded.Data.Vertices.size());
            result.IndexCount = static_cast<uint32_t>(decoded.Data.Indices.size());
            result.JointCount = static_cast<uint32_t>(decoded.Data.Joints.size());
            result.ClipCount = static_cast<uint32_t>(decoded.Data.Clips.size());
            outResult = std::move(result);
            return true;
        }
    } // namespace

    MeshEmbeddedImage& MeshEmbeddedImage::operator=(const MeshEmbeddedImage& other)
    {
        if (this != &other)
        {
            MeshEmbeddedImage candidate(other);
            Swap(candidate);
        }
        return *this;
    }
    void MeshEmbeddedImage::Swap(MeshEmbeddedImage& other) noexcept
    {
        std::swap(ImageIndex, other.ImageIndex);
        std::swap(Roles, other.Roles);
        std::swap(LogicalPath, other.LogicalPath);
        std::swap(Format, other.Format);
        std::swap(SourceHash, other.SourceHash);
        m_OwnedBytes.swap(other.m_OwnedBytes);
        std::swap(m_BorrowedBytes, other.m_BorrowedBytes);
    }
    bool MeshEmbeddedImage::SetBytes(Core::Container::Span<const uint8_t> bytes, bool bBorrow)
    {
        if (bytes.empty() || bytes.data() == nullptr)
        {
            return false;
        }
        if (bBorrow)
        {
            // 自己所有bytesを借用へ切り替えると解放後にdanglingになるため拒否する。
            const auto begin = reinterpret_cast<uintptr_t>(bytes.data());
            const auto owned = reinterpret_cast<uintptr_t>(m_OwnedBytes.data());
            if (!m_OwnedBytes.empty() &&
                (begin >= owned ? begin - owned < m_OwnedBytes.size() : owned - begin < bytes.size()))
            {
                return false;
            }
            Core::Container::VariableArray<uint8_t> empty;
            m_OwnedBytes.swap(empty);
            m_BorrowedBytes = bytes;
        }
        else
        {
            Core::Container::VariableArray<uint8_t> candidate(bytes.begin(), bytes.end());
            m_OwnedBytes.swap(candidate);
            m_BorrowedBytes = {};
        }
        return true;
    }
    Core::Container::Span<const uint8_t> MeshEmbeddedImage::GetBytes() const noexcept
    {
        return IsBorrowed() ? m_BorrowedBytes : Core::Container::Span<const uint8_t>(m_OwnedBytes.data(), m_OwnedBytes.size());
    }
    bool MeshEmbeddedImage::IsBorrowed() const noexcept
    {
        return !m_BorrowedBytes.empty();
    }

    bool InspectGltfModel(const uint8_t* sourceBytes,size_t sourceSize,Core::Container::AnsiStringView sourcePath,
        ModelInspection& outInspection,Core::Container::AnsiString& error)
    {
        return InspectGltfModelInternal(sourceBytes,sourceSize,sourcePath,outInspection,error);
    }

    bool FingerprintModelCookSource(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, Core::Container::AnsiStringView sourcePath,
        Core::Container::AnsiStringView logicalPath, ModelCookFingerprint& outResult,
        Core::Container::AnsiString& error, const Core::AssetImport::ImportSettingsFileOptions* importOptions,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions)
    {
        return FingerprintModelCookSourceInternal(sourceBytes,sourceSize,format,sourcePath,logicalPath,
            outResult,error,importOptions,decodeOptions);
    }

    bool IsSupportedMeshCookFormat(NorvesLib::Core::Container::AnsiStringView format) noexcept
    {
        return format == SupportedMeshFormat;
    }

    bool CookGltfToNvmesh(const uint8_t* sourceBytes, size_t sourceSize,
                          NorvesLib::Core::Container::AnsiStringView format,
                          NorvesLib::Core::Container::AnsiStringView sourcePath,
                          NorvesLib::Core::Container::AnsiStringView logicalPath, MeshCookResult& outResult,
                          NorvesLib::Core::Container::AnsiString& error,
                          const Core::AssetImport::ImportSettingsFileOptions* importOptions)
    {
        AnsiString internalError;
        if (!CookGltfToNvmeshInternal(sourceBytes, sourceSize, format, sourcePath, logicalPath, outResult, internalError, importOptions))
        {
            error = internalError.c_str();
            return false;
        }
        return true;
    }

    bool IsSupportedSkeletalCookFormat(NorvesLib::Core::Container::AnsiStringView format) noexcept
    {
        return format == SupportedSkeletalFormat;
    }

    bool CookGltfToNvskel(const uint8_t* sourceBytes,
                          size_t sourceSize,
                          NorvesLib::Core::Container::AnsiStringView format,
                          NorvesLib::Core::Container::AnsiStringView sourcePath,
                          SkeletalCookResult& outResult,
                          NorvesLib::Core::Container::AnsiString& error,
                          const Core::AssetImport::ImportSettingsFileOptions* importOptions,
                          const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions)
    {
        AnsiString internalError;
        if (!CookGltfToNvskelInternal(sourceBytes, sourceSize, format, sourcePath, outResult, internalError, importOptions, decodeOptions))
        {
            error = internalError;
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
