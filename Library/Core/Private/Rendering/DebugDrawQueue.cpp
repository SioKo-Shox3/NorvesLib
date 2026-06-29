#include "Rendering/DebugDrawQueue.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

namespace
{

DebugLineVertex MakeVertex(const Math::Vector3& position, const Math::Vector4& color)
{
    return DebugLineVertex{
        {position.x, position.y, position.z},
        {color.x, color.y, color.z, color.w}};
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
