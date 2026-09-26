#pragma once

#include "IBuffer.h"
#include "Container/VariableArray.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::RHI
{
    enum class AccelerationStructureType : uint8_t
    {
        BottomLevel,
        TopLevel
    };

    enum class AccelerationStructureGeometryType : uint8_t
    {
        Triangles,
        AABBs
    };

    enum class AccelerationStructureBuildMode : uint8_t
    {
        Build,
        Update
    };

    /**
     * @brief BLAS内のジオメトリ容量
     */
    struct AccelerationStructureGeometryCapacityDesc
    {
        AccelerationStructureGeometryType type = AccelerationStructureGeometryType::Triangles;
        uint32_t maxPrimitiveCount = 0;
        bool opaque = true;
    };

    /**
     * @brief 加速構造を作成するための容量と属性
     */
    struct AccelerationStructureDesc
    {
        AccelerationStructureType type = AccelerationStructureType::BottomLevel;
        NorvesLib::Core::Container::VariableArray<AccelerationStructureGeometryCapacityDesc> geometryCapacities;
        uint32_t maxInstanceCount = 0;
        bool allowUpdate = false;
        bool allowCompaction = false;
    };

    /**
     * @brief 三角形ジオメトリの入力バッファ
     */
    struct AccelerationStructureTriangleGeometryDesc
    {
        BufferPtr vertexBuffer;
        uint64_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t vertexStride = 0;
        Format vertexFormat = Format::R32G32B32_FLOAT;
        BufferPtr indexBuffer;
        uint64_t indexOffset = 0;
        uint32_t indexCount = 0;
        IndexType indexFormat = IndexType::Uint32;
    };

    /**
     * @brief 軸平行境界箱ジオメトリの入力バッファ
     */
    struct AccelerationStructureAabbGeometryDesc
    {
        BufferPtr buffer;
        uint64_t offset = 0;
        uint32_t primitiveCount = 0;
        uint32_t stride = 24;
    };

    /**
     * @brief BLASに登録するジオメトリ
     */
    struct AccelerationStructureGeometryDesc
    {
        AccelerationStructureGeometryType type = AccelerationStructureGeometryType::Triangles;
        bool opaque = true;
        AccelerationStructureTriangleGeometryDesc triangles;
        AccelerationStructureAabbGeometryDesc aabbs;
    };

    /**
     * @brief TLASに登録するBLASインスタンス
     * @details transformは行優先3x4で、並進は添字3・7・11に置く。
     * NorvesLib::Matrix4x4の行ベクトル規約では並進が行3にあるため、直接コピーせず変換する。
     */
    struct AccelerationStructureInstanceDesc
    {
        AccelerationStructurePtr bottomLevel;
        float transform[12] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f};
        uint32_t customIndex = 0;
        uint8_t mask = 0xFF;
        uint32_t shaderBindingTableRecordOffset = 0;
        bool disableTriangleFacingCull = false;
    };

    /**
     * @brief BLAS/TLASの構築または更新内容
     */
    struct AccelerationStructureBuildDesc
    {
        AccelerationStructureType type = AccelerationStructureType::BottomLevel;
        AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build;
        AccelerationStructurePtr destination;
        AccelerationStructurePtr source;
        NorvesLib::Core::Container::VariableArray<AccelerationStructureGeometryDesc> geometries;
        NorvesLib::Core::Container::VariableArray<AccelerationStructureInstanceDesc> instances;
    };

    /**
     * @brief GPU加速構造リソース
     */
    class IAccelerationStructure
    {
    public:
        virtual ~IAccelerationStructure() = default;

        /** @brief 作成時に指定した容量と属性を取得 */
        virtual const AccelerationStructureDesc& GetDesc() const = 0;
        /** @brief GPU上の加速構造領域のサイズを取得 */
        virtual uint64_t GetSize() const = 0;
        /** @brief GPUから参照する加速構造のデバイスアドレスを取得 */
        virtual uint64_t GetDeviceAddress() const = 0;
        /** @brief 記述子に従ってGPU上に加速構造を構築 */
        virtual bool Build(const AccelerationStructureBuildDesc& desc)
        {
            (void)desc;
            return false;
        }
    };

    namespace Detail
    {
        inline uint32_t GetAccelerationStructureVertexPositionSize(Format format)
        {
            switch (format)
            {
            case Format::R32G32B32_FLOAT:
                return 12;
            case Format::R32G32B32A32_FLOAT:
                return 16;
            default:
                return 0;
            }
        }

        inline uint32_t GetAccelerationStructureIndexSize(IndexType type)
        {
            switch (type)
            {
            case IndexType::Uint16:
                return 2;
            case IndexType::Uint32:
                return 4;
            default:
                return 0;
            }
        }

        inline bool IsValidAccelerationStructureInputRange(
            const BufferPtr& buffer,
            uint64_t offset,
            uint64_t size)
        {
            if (!buffer || size == 0 || buffer->GetDeviceAddress() == 0 ||
                (buffer->GetUsage() & ResourceUsage::BufferDeviceAddress) != ResourceUsage::BufferDeviceAddress)
            {
                return false;
            }

            const uint64_t bufferSize = buffer->GetSize();
            return offset <= bufferSize && size <= bufferSize - offset;
        }
    }

    inline bool IsValidAccelerationStructureDesc(const AccelerationStructureDesc& desc)
    {
        switch (desc.type)
        {
        case AccelerationStructureType::BottomLevel:
        {
            if (desc.geometryCapacities.empty() || desc.maxInstanceCount != 0)
            {
                return false;
            }

            const AccelerationStructureGeometryType geometryType = desc.geometryCapacities.front().type;
            if (geometryType != AccelerationStructureGeometryType::Triangles &&
                geometryType != AccelerationStructureGeometryType::AABBs)
            {
                return false;
            }

            // 一つのBLASに登録するジオメトリ種別は統一する。
            for (const auto& capacity : desc.geometryCapacities)
            {
                if (capacity.type != geometryType || capacity.maxPrimitiveCount == 0)
                {
                    return false;
                }
            }
            return true;
        }
        case AccelerationStructureType::TopLevel:
            return desc.geometryCapacities.empty() && desc.maxInstanceCount > 0;
        default:
            return false;
        }
    }

    inline bool IsValidAccelerationStructureGeometryDesc(const AccelerationStructureGeometryDesc& desc)
    {
        switch (desc.type)
        {
        case AccelerationStructureGeometryType::Triangles:
        {
            const auto& triangles = desc.triangles;
            const uint32_t positionSize = Detail::GetAccelerationStructureVertexPositionSize(triangles.vertexFormat);
            if (positionSize == 0 || triangles.vertexCount == 0 || triangles.vertexStride < positionSize ||
                triangles.vertexStride % 4 != 0 || triangles.vertexOffset % 4 != 0 || !triangles.vertexBuffer)
            {
                return false;
            }

            const uint64_t vertexBufferSize = triangles.vertexBuffer->GetSize();
            if (triangles.vertexOffset > vertexBufferSize || vertexBufferSize - triangles.vertexOffset < positionSize ||
                (triangles.vertexCount - 1u) >
                    (vertexBufferSize - triangles.vertexOffset - positionSize) / triangles.vertexStride ||
                triangles.vertexBuffer->GetDeviceAddress() == 0 || triangles.vertexBuffer->GetDeviceAddress() % 4 != 0 ||
                (triangles.vertexBuffer->GetUsage() & ResourceUsage::BufferDeviceAddress) != ResourceUsage::BufferDeviceAddress)
            {
                return false;
            }

            if (!triangles.indexBuffer)
            {
                return triangles.vertexCount >= 3 && triangles.vertexCount % 3 == 0 &&
                       triangles.indexCount == 0 && triangles.indexOffset == 0;
            }

            const uint32_t indexSize = Detail::GetAccelerationStructureIndexSize(triangles.indexFormat);
            if (indexSize == 0 || triangles.indexCount < 3 || triangles.indexCount % 3 != 0 ||
                triangles.indexOffset % indexSize != 0 || triangles.indexBuffer->GetDeviceAddress() % indexSize != 0)
            {
                return false;
            }

            const uint64_t indexBytes = static_cast<uint64_t>(triangles.indexCount) * indexSize;
            return Detail::IsValidAccelerationStructureInputRange(
                triangles.indexBuffer, triangles.indexOffset, indexBytes);
        }
        case AccelerationStructureGeometryType::AABBs:
        {
            const auto& aabbs = desc.aabbs;
            if (aabbs.primitiveCount == 0 || aabbs.stride < 24 || aabbs.stride % 8 != 0 || aabbs.offset % 8 != 0 ||
                !aabbs.buffer || aabbs.buffer->GetDeviceAddress() == 0 || aabbs.buffer->GetDeviceAddress() % 8 != 0 ||
                (aabbs.buffer->GetUsage() & ResourceUsage::BufferDeviceAddress) != ResourceUsage::BufferDeviceAddress)
            {
                return false;
            }

            const uint64_t bufferSize = aabbs.buffer->GetSize();
            if (aabbs.offset > bufferSize || bufferSize - aabbs.offset < 24)
            {
                return false;
            }

            return (aabbs.primitiveCount - 1u) <= (bufferSize - aabbs.offset - 24) / aabbs.stride;
        }
        default:
            return false;
        }
    }

    inline bool IsValidAccelerationStructureInstanceDesc(const AccelerationStructureInstanceDesc& desc)
    {
        constexpr uint32_t MaxInstanceFieldValue = 0x00FFFFFFu;
        if (!desc.bottomLevel || !IsValidAccelerationStructureDesc(desc.bottomLevel->GetDesc()) ||
            desc.bottomLevel->GetDesc().type != AccelerationStructureType::BottomLevel ||
            desc.bottomLevel->GetDeviceAddress() == 0 || desc.bottomLevel->GetSize() == 0 ||
            desc.customIndex > MaxInstanceFieldValue ||
            desc.shaderBindingTableRecordOffset > MaxInstanceFieldValue)
        {
            return false;
        }

        for (float value : desc.transform)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    }

    inline bool IsValidAccelerationStructureBuildDesc(const AccelerationStructureBuildDesc& desc)
    {
        if (!desc.destination || !IsValidAccelerationStructureDesc(desc.destination->GetDesc()) ||
            desc.destination->GetDesc().type != desc.type || desc.destination->GetDeviceAddress() == 0 ||
            desc.destination->GetSize() == 0)
        {
            return false;
        }

        if (desc.mode == AccelerationStructureBuildMode::Build)
        {
            if (desc.source)
            {
                return false;
            }
        }
        else if (desc.mode == AccelerationStructureBuildMode::Update)
        {
            if (!desc.source || !IsValidAccelerationStructureDesc(desc.source->GetDesc()) ||
                !desc.destination->GetDesc().allowUpdate || !desc.source->GetDesc().allowUpdate ||
                desc.source->GetDesc().type != desc.type || desc.source->GetDeviceAddress() == 0 ||
                desc.source->GetSize() == 0)
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        if (desc.type == AccelerationStructureType::BottomLevel)
        {
            const auto& resourceDesc = desc.destination->GetDesc();
            if (desc.geometries.empty() || !desc.instances.empty() ||
                desc.geometries.size() != resourceDesc.geometryCapacities.size() ||
                (desc.mode == AccelerationStructureBuildMode::Update &&
                 desc.geometries.size() != desc.source->GetDesc().geometryCapacities.size()))
            {
                return false;
            }

            uint32_t geometryIndex = 0;
            for (const auto& geometry : desc.geometries)
            {
                const auto& capacity = resourceDesc.geometryCapacities[geometryIndex];
                if (!IsValidAccelerationStructureGeometryDesc(geometry) || geometry.type != capacity.type ||
                    geometry.opaque != capacity.opaque)
                {
                    return false;
                }
                uint32_t primitiveCount = 0;
                if (geometry.type == AccelerationStructureGeometryType::Triangles)
                {
                    primitiveCount = geometry.triangles.indexBuffer
                        ? geometry.triangles.indexCount / 3u
                        : geometry.triangles.vertexCount / 3u;
                }
                else
                {
                    primitiveCount = geometry.aabbs.primitiveCount;
                }
                if (primitiveCount == 0 || primitiveCount > capacity.maxPrimitiveCount)
                {
                    return false;
                }
                if (desc.mode == AccelerationStructureBuildMode::Update)
                {
                    const auto& sourceCapacity = desc.source->GetDesc().geometryCapacities[geometryIndex];
                    if (geometry.type != sourceCapacity.type || geometry.opaque != sourceCapacity.opaque ||
                        primitiveCount > sourceCapacity.maxPrimitiveCount)
                    {
                        return false;
                    }
                }
                ++geometryIndex;
            }
            return true;
        }

        if (desc.type == AccelerationStructureType::TopLevel)
        {
            const auto& resourceDesc = desc.destination->GetDesc();
            if (!desc.geometries.empty() || desc.instances.empty() || desc.instances.size() > resourceDesc.maxInstanceCount ||
                (desc.mode == AccelerationStructureBuildMode::Update &&
                 desc.instances.size() > desc.source->GetDesc().maxInstanceCount))
            {
                return false;
            }
            for (const auto& instance : desc.instances)
            {
                if (!IsValidAccelerationStructureInstanceDesc(instance))
                {
                    return false;
                }
            }
            return true;
        }

        return false;
    }

    inline bool IsValidAccelerationStructureBuildDesc(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureBuildMode expectedMode)
    {
        return desc.mode == expectedMode && IsValidAccelerationStructureBuildDesc(desc);
    }

} // namespace NorvesLib::RHI
