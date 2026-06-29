#include "Resource/MeshResource.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    struct Vertex
    {
        float Position[3];
        float Normal[3];
        float UV[2];
    };

    struct TestVector3
    {
        float X;
        float Y;
        float Z;
    };

    static_assert(sizeof(Vertex) == 32);

    constexpr float PrimitiveMinimumSize = 1e-4f;
    constexpr float Epsilon = 1e-4f;
    constexpr float NormalEpsilon = 1e-3f;

    float ClampPrimitiveSize(float value)
    {
        return value < PrimitiveMinimumSize ? PrimitiveMinimumSize : value;
    }

    bool Near(float actual, float expected, float epsilon = Epsilon)
    {
        return std::fabs(actual - expected) <= epsilon;
    }

    TestVector3 MakePosition(const Vertex& vertex)
    {
        return TestVector3{vertex.Position[0], vertex.Position[1], vertex.Position[2]};
    }

    TestVector3 MakeNormal(const Vertex& vertex)
    {
        return TestVector3{vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]};
    }

    TestVector3 Add(const TestVector3& left, const TestVector3& right)
    {
        return TestVector3{left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    TestVector3 Subtract(const TestVector3& left, const TestVector3& right)
    {
        return TestVector3{left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    TestVector3 Cross(const TestVector3& left, const TestVector3& right)
    {
        return TestVector3{
            left.Y * right.Z - left.Z * right.Y,
            left.Z * right.X - left.X * right.Z,
            left.X * right.Y - left.Y * right.X};
    }

    float Dot(const TestVector3& left, const TestVector3& right)
    {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    }

    void AssertStandardLayout(const NorvesLib::Core::Rendering::VertexLayout& layout)
    {
        using NorvesLib::Core::Rendering::VertexFormat;
        using NorvesLib::Core::Rendering::VertexSemantic;

        assert(layout.Stride == sizeof(Vertex));
        assert(layout.ElementCount == 3u);

        assert(layout.Elements[0].Semantic == VertexSemantic::Position);
        assert(layout.Elements[0].Format == VertexFormat::Float3);
        assert(layout.Elements[0].Offset == 0u);

        assert(layout.Elements[1].Semantic == VertexSemantic::Normal);
        assert(layout.Elements[1].Format == VertexFormat::Float3);
        assert(layout.Elements[1].Offset == 12u);

        assert(layout.Elements[2].Semantic == VertexSemantic::TexCoord0);
        assert(layout.Elements[2].Format == VertexFormat::Float2);
        assert(layout.Elements[2].Offset == 24u);
    }

    void AssertBounds(
        const NorvesLib::Core::Rendering::BoundingBox& bounds,
        const NorvesLib::Core::Rendering::BoundingSphere& sphere,
        float radius,
        float height)
    {
        const float halfH = height * 0.5f;

        assert(bounds.IsValid());
        assert(bounds.MaxX > bounds.MinX);
        assert(bounds.MaxY > bounds.MinY);
        assert(bounds.MaxZ > bounds.MinZ);

        assert(Near(bounds.MinX, -radius));
        assert(Near(bounds.MinY, -halfH));
        assert(Near(bounds.MinZ, -radius));
        assert(Near(bounds.MaxX, radius));
        assert(Near(bounds.MaxY, halfH));
        assert(Near(bounds.MaxZ, radius));

        assert(sphere.IsValid());
        assert(Near(sphere.CenterX, 0.0f));
        assert(Near(sphere.CenterY, 0.0f));
        assert(Near(sphere.CenterZ, 0.0f));
        assert(Near(sphere.Radius, std::sqrt(radius * radius + halfH * halfH)));
    }

    void AssertBounds(
        const NorvesLib::Core::Rendering::BoundingBox& bounds,
        const NorvesLib::Core::Rendering::BoundingSphere& sphere,
        float minX,
        float minY,
        float minZ,
        float maxX,
        float maxY,
        float maxZ,
        float sphereRadius)
    {
        assert(bounds.IsValid());
        assert(bounds.MaxX > bounds.MinX);
        assert(bounds.MaxY > bounds.MinY);
        assert(bounds.MaxZ > bounds.MinZ);

        assert(Near(bounds.MinX, minX));
        assert(Near(bounds.MinY, minY));
        assert(Near(bounds.MinZ, minZ));
        assert(Near(bounds.MaxX, maxX));
        assert(Near(bounds.MaxY, maxY));
        assert(Near(bounds.MaxZ, maxZ));

        assert(sphere.IsValid());
        assert(Near(sphere.CenterX, 0.0f));
        assert(Near(sphere.CenterY, 0.0f));
        assert(Near(sphere.CenterZ, 0.0f));
        assert(Near(sphere.Radius, sphereRadius));
    }

    void ReadVertex(
        const NorvesLib::Core::Container::VariableArray<uint8_t>& vertexData,
        uint32_t vertexIndex,
        Vertex& vertex)
    {
        const size_t offset = static_cast<size_t>(vertexIndex) * sizeof(Vertex);
        std::memcpy(&vertex, vertexData.data() + offset, sizeof(Vertex));
    }

    bool IsNormalNear(const float normal[3], float x, float y, float z)
    {
        return Near(normal[0], x, NormalEpsilon) &&
               Near(normal[1], y, NormalEpsilon) &&
               Near(normal[2], z, NormalEpsilon);
    }

    void AssertVertexPayload(
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh,
        bool bExpectTopCapNormal,
        bool bExpectBottomCapNormal)
    {
        const auto& vertexData = mesh->GetVertexData();
        const auto& bounds = mesh->GetBounds();

        assert(vertexData.size() == static_cast<size_t>(mesh->GetVertexCount()) * sizeof(Vertex));

        bool bHasTopCapNormal = false;
        bool bHasBottomCapNormal = false;

        for (uint32_t vertexIndex = 0; vertexIndex < mesh->GetVertexCount(); ++vertexIndex)
        {
            Vertex vertex;
            ReadVertex(vertexData, vertexIndex, vertex);

            assert(std::isfinite(vertex.Position[0]));
            assert(std::isfinite(vertex.Position[1]));
            assert(std::isfinite(vertex.Position[2]));
            assert(vertex.Position[0] >= bounds.MinX - Epsilon);
            assert(vertex.Position[0] <= bounds.MaxX + Epsilon);
            assert(vertex.Position[1] >= bounds.MinY - Epsilon);
            assert(vertex.Position[1] <= bounds.MaxY + Epsilon);
            assert(vertex.Position[2] >= bounds.MinZ - Epsilon);
            assert(vertex.Position[2] <= bounds.MaxZ + Epsilon);

            assert(std::isfinite(vertex.Normal[0]));
            assert(std::isfinite(vertex.Normal[1]));
            assert(std::isfinite(vertex.Normal[2]));

            const float normalLength = std::sqrt(
                vertex.Normal[0] * vertex.Normal[0] +
                vertex.Normal[1] * vertex.Normal[1] +
                vertex.Normal[2] * vertex.Normal[2]);
            assert(std::isfinite(normalLength));
            assert(Near(normalLength, 1.0f, NormalEpsilon));

            if (IsNormalNear(vertex.Normal, 0.0f, 1.0f, 0.0f))
            {
                bHasTopCapNormal = true;
            }
            if (IsNormalNear(vertex.Normal, 0.0f, -1.0f, 0.0f))
            {
                bHasBottomCapNormal = true;
            }
        }

        assert(!bExpectTopCapNormal || bHasTopCapNormal);
        assert(!bExpectBottomCapNormal || bHasBottomCapNormal);
    }

    void AssertIndices(const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh)
    {
        const auto& indices = mesh->GetIndexData();

        assert(mesh->GetIndexCount() > 0u);
        assert(mesh->GetIndexCount() == static_cast<uint32_t>(indices.size()));
        assert((mesh->GetIndexCount() % 3u) == 0u);

        for (uint32_t indexPosition = 0; indexPosition < mesh->GetIndexCount(); ++indexPosition)
        {
            assert(indices[indexPosition] < mesh->GetVertexCount());
        }
    }

    void AssertWinding(const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh)
    {
        const auto& vertexData = mesh->GetVertexData();
        const auto& indices = mesh->GetIndexData();

        uint32_t checkedTriangleCount = 0u;

        for (uint32_t indexPosition = 0; indexPosition < mesh->GetIndexCount(); indexPosition += 3u)
        {
            Vertex vertex0;
            Vertex vertex1;
            Vertex vertex2;
            ReadVertex(vertexData, indices[indexPosition], vertex0);
            ReadVertex(vertexData, indices[indexPosition + 1u], vertex1);
            ReadVertex(vertexData, indices[indexPosition + 2u], vertex2);

            const TestVector3 p0 = MakePosition(vertex0);
            const TestVector3 p1 = MakePosition(vertex1);
            const TestVector3 p2 = MakePosition(vertex2);
            const TestVector3 edge0 = Subtract(p1, p0);
            const TestVector3 edge1 = Subtract(p2, p0);
            const TestVector3 faceNormal = Cross(edge0, edge1);
            const float faceNormalLengthSquared = Dot(faceNormal, faceNormal);

            if (faceNormalLengthSquared <= 1e-12f)
            {
                continue;
            }

            ++checkedTriangleCount;

            const TestVector3 averageNormal = Add(Add(MakeNormal(vertex0), MakeNormal(vertex1)), MakeNormal(vertex2));
            assert(Dot(faceNormal, averageNormal) > 0.0f);
        }

        assert(checkedTriangleCount > 0u);
    }

    void AssertCapsuleJoinRings(
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh,
        float halfHeight,
        float radius)
    {
        const auto& vertexData = mesh->GetVertexData();

        bool bHasTopRing = false;
        bool bHasBottomRing = false;

        for (uint32_t vertexIndex = 0; vertexIndex < mesh->GetVertexCount(); ++vertexIndex)
        {
            Vertex vertex;
            ReadVertex(vertexData, vertexIndex, vertex);

            const float horizontalRadius = std::sqrt(
                vertex.Position[0] * vertex.Position[0] +
                vertex.Position[2] * vertex.Position[2]);

            if (Near(vertex.Position[1], halfHeight) && Near(horizontalRadius, radius))
            {
                bHasTopRing = true;
            }
            if (Near(vertex.Position[1], -halfHeight) && Near(horizontalRadius, radius))
            {
                bHasBottomRing = true;
            }
        }

        assert(bHasTopRing);
        assert(bHasBottomRing);
    }

    void AssertPrimitive(
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh,
        const char* expectedName,
        float minX,
        float minY,
        float minZ,
        float maxX,
        float maxY,
        float maxZ,
        float sphereRadius,
        bool bExpectTopCapNormal,
        bool bExpectBottomCapNormal)
    {
        using NorvesLib::Core::Identity;
        using NorvesLib::Core::ResourceState;

        assert(mesh);
        assert(mesh->GetResourceState() == ResourceState::Loaded);
        assert(mesh->GetResourceName() == Identity(expectedName));
        assert(mesh->GetVertexCount() > 0u);
        assert(mesh->GetIndexCount() > 0u);

        AssertIndices(mesh);
        AssertStandardLayout(mesh->GetVertexLayout());
        AssertBounds(mesh->GetBounds(), mesh->GetBoundingSphere(), minX, minY, minZ, maxX, maxY, maxZ, sphereRadius);
        AssertVertexPayload(mesh, bExpectTopCapNormal, bExpectBottomCapNormal);
    }

    void AssertPrimitive(
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::MeshResource>& mesh,
        const char* expectedName,
        float radius,
        float height,
        bool bExpectTopCapNormal,
        bool bExpectBottomCapNormal)
    {
        const float halfH = height * 0.5f;
        AssertPrimitive(
            mesh,
            expectedName,
            -radius,
            -halfH,
            -radius,
            radius,
            halfH,
            radius,
            std::sqrt(radius * radius + halfH * halfH),
            bExpectTopCapNormal,
            bExpectBottomCapNormal);
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    using NorvesLib::Core::MeshResource;

    std::cout << "MeshResourcesPrimitiveTest start\n";

    auto cylinder = MeshResource::CreateCylinder(1.0f, 2.0f, 16u);
    AssertPrimitive(cylinder, "Primitive_Cylinder", 1.0f, 2.0f, true, true);
    AssertWinding(cylinder);

    auto cone = MeshResource::CreateCone(1.5f, 3.0f, 24u);
    AssertPrimitive(cone, "Primitive_Cone", 1.5f, 3.0f, false, true);
    AssertWinding(cone);

    auto torus = MeshResource::CreateTorus(2.0f, 0.5f, 24u, 12u);
    AssertPrimitive(torus, "Primitive_Torus", -2.5f, -0.5f, -2.5f, 2.5f, 0.5f, 2.5f, 2.5f, false, false);
    AssertWinding(torus);

    auto capsule = MeshResource::CreateCapsule(1.0f, 2.0f, 16u, 8u);
    AssertPrimitive(capsule, "Primitive_Capsule", -1.0f, -2.0f, -1.0f, 1.0f, 2.0f, 1.0f, 2.0f, true, true);
    AssertWinding(capsule);
    AssertCapsuleJoinRings(capsule, 1.0f, 1.0f);

    const float clampedRadius = ClampPrimitiveSize(0.0f);
    const float clampedHeight = ClampPrimitiveSize(0.0f);

    auto clampedCylinder = MeshResource::CreateCylinder(0.0f, 0.0f, 2u);
    AssertPrimitive(clampedCylinder, "Primitive_Cylinder", clampedRadius, clampedHeight, true, true);
    assert(clampedCylinder->GetIndexCount() == 36u);

    auto clampedCone = MeshResource::CreateCone(-1.0f, 0.0f, 2u);
    AssertPrimitive(clampedCone, "Primitive_Cone", clampedRadius, clampedHeight, false, true);
    assert(clampedCone->GetIndexCount() == 18u);

    const float clampedTorusBoundsRadius = 2.0f + clampedRadius;
    auto clampedTorus = MeshResource::CreateTorus(2.0f, 0.0f, 2u, 2u);
    AssertPrimitive(
        clampedTorus,
        "Primitive_Torus",
        -clampedTorusBoundsRadius,
        -clampedRadius,
        -clampedTorusBoundsRadius,
        clampedTorusBoundsRadius,
        clampedRadius,
        clampedTorusBoundsRadius,
        clampedTorusBoundsRadius,
        false,
        false);
    assert(clampedTorus->GetIndexCount() == 54u);

    const float clampedCapsuleHalfHeight = clampedHeight * 0.5f;
    const float clampedCapsuleExtent = clampedCapsuleHalfHeight + clampedRadius;
    auto clampedCapsule = MeshResource::CreateCapsule(0.0f, 0.0f, 2u, 2u);
    AssertPrimitive(
        clampedCapsule,
        "Primitive_Capsule",
        -clampedRadius,
        -clampedCapsuleExtent,
        -clampedRadius,
        clampedRadius,
        clampedCapsuleExtent,
        clampedRadius,
        clampedCapsuleExtent,
        true,
        true);
    AssertCapsuleJoinRings(clampedCapsule, clampedCapsuleHalfHeight, clampedRadius);
    assert(clampedCapsule->GetIndexCount() == 108u);

    std::cout << "MeshResourcesPrimitiveTest passed\n";
    return 0;
}
