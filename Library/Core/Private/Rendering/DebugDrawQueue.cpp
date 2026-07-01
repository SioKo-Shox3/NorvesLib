#include "Rendering/DebugDrawQueue.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core::Rendering
{

namespace
{

constexpr uint32_t kSphereSegments = 24;

DebugLineVertex MakeVertex(const Math::Vector3& position, const Math::Vector4& color)
{
    return DebugLineVertex{
        {position.x, position.y, position.z},
        {color.x, color.y, color.z, color.w}};
}

bool IsFinite(const Math::Vector3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // namespace

void DebugDrawQueue::AddLine(const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector4& color)
{
    if (m_Vertices.size() + 2u > kMaxDebugLineVertices)
    {
        if (!m_bCapacityWarned)
        {
            NORVES_LOG_WARNING(
                "DebugDraw",
                "Debug line vertex capacity exceeded. Dropping debug draw lines. current=%zu max=%u",
                m_Vertices.size(),
                kMaxDebugLineVertices);
            m_bCapacityWarned = true;
        }
        return;
    }

    m_Vertices.push_back(MakeVertex(p0, color));
    m_Vertices.push_back(MakeVertex(p1, color));
}

void DebugDrawQueue::AddAABB(const Math::AABB& box, const Math::Vector4& color)
{
    const Math::Vector3 corners[8] = {
        Math::Vector3(box.Min.x, box.Min.y, box.Min.z),
        Math::Vector3(box.Max.x, box.Min.y, box.Min.z),
        Math::Vector3(box.Max.x, box.Max.y, box.Min.z),
        Math::Vector3(box.Min.x, box.Max.y, box.Min.z),
        Math::Vector3(box.Min.x, box.Min.y, box.Max.z),
        Math::Vector3(box.Max.x, box.Min.y, box.Max.z),
        Math::Vector3(box.Max.x, box.Max.y, box.Max.z),
        Math::Vector3(box.Min.x, box.Max.y, box.Max.z)};

    AddLine(corners[0], corners[1], color);
    AddLine(corners[1], corners[2], color);
    AddLine(corners[2], corners[3], color);
    AddLine(corners[3], corners[0], color);

    AddLine(corners[4], corners[5], color);
    AddLine(corners[5], corners[6], color);
    AddLine(corners[6], corners[7], color);
    AddLine(corners[7], corners[4], color);

    AddLine(corners[0], corners[4], color);
    AddLine(corners[1], corners[5], color);
    AddLine(corners[2], corners[6], color);
    AddLine(corners[3], corners[7], color);
}

void DebugDrawQueue::AddSphere(const Math::Sphere& sphere, const Math::Vector4& color)
{
    AddSphere(sphere.Center, sphere.Radius, color);
}

void DebugDrawQueue::AddSphere(const Math::Vector3& center, float radius, const Math::Vector4& color)
{
    if (radius <= 0.0f
        || !std::isfinite(radius)
        || !IsFinite(center))
    {
        return;
    }

    constexpr float twoPi = 2.0f * Math::Constants::PI;
    for (uint32_t segmentIndex = 0; segmentIndex < kSphereSegments; ++segmentIndex)
    {
        const float angle0 = twoPi * static_cast<float>(segmentIndex) / static_cast<float>(kSphereSegments);
        const float angle1 = twoPi * static_cast<float>(segmentIndex + 1u) / static_cast<float>(kSphereSegments);

        const float cos0 = std::cos(angle0);
        const float sin0 = std::sin(angle0);
        const float cos1 = std::cos(angle1);
        const float sin1 = std::sin(angle1);

        const Math::Vector3 xy0(center.x + radius * cos0, center.y + radius * sin0, center.z);
        const Math::Vector3 xy1(center.x + radius * cos1, center.y + radius * sin1, center.z);
        AddLine(xy0, xy1, color);

        const Math::Vector3 yz0(center.x, center.y + radius * cos0, center.z + radius * sin0);
        const Math::Vector3 yz1(center.x, center.y + radius * cos1, center.z + radius * sin1);
        AddLine(yz0, yz1, color);

        const Math::Vector3 zx0(center.x + radius * sin0, center.y, center.z + radius * cos0);
        const Math::Vector3 zx1(center.x + radius * sin1, center.y, center.z + radius * cos1);
        AddLine(zx0, zx1, color);
    }
}

void DebugDrawQueue::Clear()
{
    m_Vertices.clear();
    m_bCapacityWarned = false;
}

const Container::VariableArray<DebugLineVertex>& DebugDrawQueue::GetVertices() const
{
    return m_Vertices;
}

size_t DebugDrawQueue::GetVertexCount() const
{
    return m_Vertices.size();
}

} // namespace NorvesLib::Core::Rendering
