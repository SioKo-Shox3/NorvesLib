#pragma once

#include "Container/VariableArray.h"
#include "Math/GeometryTypes.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

struct DebugLineVertex
{
    float Position[3];
    float Color[4];
};

class DebugDrawQueue
{
public:
    static constexpr uint32_t kMaxDebugLineVertices = 1u << 16;

    void AddLine(const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector4& color);
    void AddAABB(const Math::AABB& box, const Math::Vector4& color);
    void AddSphere(const Math::Sphere& sphere, const Math::Vector4& color);
    void AddSphere(const Math::Vector3& center, float radius, const Math::Vector4& color);
    void Clear();

    const Container::VariableArray<DebugLineVertex>& GetVertices() const;
    size_t GetVertexCount() const;

private:
    Container::VariableArray<DebugLineVertex> m_Vertices;
    bool m_bCapacityWarned = false;
};

} // namespace NorvesLib::Core::Rendering
