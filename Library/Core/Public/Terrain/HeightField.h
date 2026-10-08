#pragma once
#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/GeometryTypes.h"
namespace NorvesLib::Core::Terrain
{
    struct HeightFieldSample
    {
        float Height = 0;
        Math::Vector3 Normal = Math::Vector3::UnitY;
        uint64_t SurfaceTag = 0;
        uint32_t CellX = 0, CellZ = 0;
    };
    // XZの規則格子。公開後は不変で、描画と物理が同じ高さ・三角形分割を共有する。
    class HeightField final
    {
      public:
        static Container::TSharedPtr<const HeightField> Create(uint32_t width, uint32_t depth, float spacing,
                                                               Container::Span<const float> heights,
                                                               Container::Span<const uint64_t> cellSurfaces = {});
        uint32_t GetWidth() const
        {
            return m_Width;
        }
        uint32_t GetDepth() const
        {
            return m_Depth;
        }
        float GetSpacing() const
        {
            return m_Spacing;
        }
        const Math::AABB& GetBounds() const
        {
            return m_Bounds;
        }
        float GetHeight(uint32_t x, uint32_t z) const
        {
            return m_Heights[size_t(z) * m_Width + x];
        }
        uint64_t GetSurfaceTag(uint32_t x, uint32_t z) const;
        Math::Vector3 GetVertex(uint32_t x, uint32_t z) const;
        Math::Vector3 GetVertexNormal(uint32_t x, uint32_t z) const;
        // 線形三角形補間。描画面と一致させ、bilinearによる物理面とのずれを作らない。
        bool Sample(float x, float z, HeightFieldSample& out) const;
        // 法線が上を向く頂点順。描画用CWとは逆順になる。
        void GetTriangle(uint32_t x, uint32_t z, uint32_t triangle, Math::Vector3& a, Math::Vector3& b,
                         Math::Vector3& c) const;

      private:
        uint32_t m_Width = 0, m_Depth = 0;
        float m_Spacing = 1;
        Math::AABB m_Bounds;
        Container::VariableArray<float> m_Heights;
        Container::VariableArray<uint64_t> m_Surfaces;
    };
} // namespace NorvesLib::Core::Terrain
