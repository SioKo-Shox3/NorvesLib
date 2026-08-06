#pragma once
#include "Container/PointerTypes.h"

#include "Container/Containers.h"
#include "Rendering/RenderTypes.h"
#include "RHI/RHITypes.h"
#include "Thread/Atomic.h"
#include "Math/GeometryTypes.h"
#include "Math/Matrix4x4.h"
#include <utility>

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct SkinnedMeshHandle
    {
        uint64_t Id = 0;
        uint64_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Id != 0 && Generation != 0;
        }

        static constexpr SkinnedMeshHandle Invalid()
        {
            return {};
        }

        constexpr bool operator==(const SkinnedMeshHandle& other) const
        {
            return Id == other.Id && Generation == other.Generation;
        }

        constexpr bool operator!=(const SkinnedMeshHandle& other) const
        {
            return !(*this == other);
        }

        constexpr bool operator<(const SkinnedMeshHandle& other) const
        {
            return Id < other.Id || (Id == other.Id && Generation < other.Generation);
        }
    };

    struct SkinnedMeshVertex
    {
        float Position[3] = {};
        float Normal[3] = {};
        float TexCoord[2] = {};
        uint32_t BoneIndices[4] = {};
        float BoneWeights[4] = {};
    };

    static_assert(sizeof(SkinnedMeshVertex) == 64, "Skinned vertex ABI must remain 64 bytes");

    class SkinnedMeshAssetLease final
    {
    public:
        SkinnedMeshAssetLease(SkinnedMeshHandle handle,
                              Container::VariableArray<SkinnedMeshVertex>&& vertices,
                              Container::VariableArray<uint32_t>&& indices)
            : m_Handle(handle),
              m_Vertices(std::move(vertices)),
              m_Indices(std::move(indices))
        {
        }

        [[nodiscard]] SkinnedMeshHandle GetHandle() const
        {
            return m_Handle;
        }

        [[nodiscard]] const Container::VariableArray<SkinnedMeshVertex>& GetVertices() const
        {
            return m_Vertices;
        }

        [[nodiscard]] const Container::VariableArray<uint32_t>& GetIndices() const
        {
            return m_Indices;
        }

        [[nodiscard]] bool IsAssetLeaseActive() const
        {
            return m_bAssetLeaseActive.Load(std::memory_order_acquire);
        }

        void ReleaseAssetLease()
        {
            m_bAssetLeaseActive.Store(false, std::memory_order_release);
        }

    private:
        SkinnedMeshHandle m_Handle;
        Container::VariableArray<SkinnedMeshVertex> m_Vertices;
        Container::VariableArray<uint32_t> m_Indices;
        Thread::Atomic<bool> m_bAssetLeaseActive{true};
    };

    struct SkinnedMeshFrameLease
    {
        explicit SkinnedMeshFrameLease(Container::TSharedPtr<const SkinnedMeshAssetLease> assetLease)
            : AssetLease(std::move(assetLease))
        {
        }

        [[nodiscard]] bool IsValid() const
        {
            return AssetLease && AssetLease->GetHandle().IsValid() &&
                   !AssetLease->GetVertices().empty() && !AssetLease->GetIndices().empty();
        }

        Container::TSharedPtr<const SkinnedMeshAssetLease> AssetLease;
    };

    enum class SkinnedMeshPassKind : uint8_t
    {
        None,
        GBuffer,
        Shadow
    };

    struct SkinnedMeshPreparedDraw
    {
        SkinnedMeshHandle MeshHandle;
        RHI::BufferPtr VertexBuffer;
        RHI::BufferPtr IndexBuffer;
        RHI::BufferPtr PaletteBuffer;
        uint32_t IndexCount = 0;

        [[nodiscard]] bool IsValid() const
        {
            return MeshHandle.IsValid() && VertexBuffer && IndexBuffer && PaletteBuffer && IndexCount > 0;
        }
    };

    struct SkinnedMeshGpuLifetimeSnapshot
    {
        SkinnedMeshHandle MeshHandle;
        uint32_t FrameLeaseCount = 0;
        uint64_t LastSubmittedSerial = 0;
        uint64_t CompletedSubmissionSerial = 0;
        bool bAssetLeaseActive = false;
    };

    struct SkinnedMeshProxy
    {
        SkinnedMeshHandle MeshHandle;
        MaterialHandle Material;
        Container::TWeakPtr<const SkinnedMeshAssetLease> AssetLease;
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;
        Math::Matrix4x4 WorldTransform;
        Container::VariableArray<Math::Matrix4x4> BonePalette;
        Math::AABB AnimatedBounds;
        bool bCastShadow = true;
        bool bHasAnimatedBounds = false;
        bool bVisible = true;

        [[nodiscard]] bool IsValid() const
        {
            return ComponentId != 0 && MeshHandle.IsValid() && !AssetLease.expired() &&
                   !BonePalette.empty() && bHasAnimatedBounds && bVisible;
        }
    };
} // namespace NorvesLib::Core::Rendering
