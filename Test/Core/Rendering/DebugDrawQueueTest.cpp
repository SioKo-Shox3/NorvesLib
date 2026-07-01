#include "Rendering/DebugDrawQueue.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "DebugDrawQueueTest start\n";

    DebugDrawQueue queue;
    const NorvesLib::Math::Vector4 color(1.0f, 0.5f, 0.25f, 1.0f);

    queue.AddLine(
        NorvesLib::Math::Vector3(0.0f, 1.0f, 2.0f),
        NorvesLib::Math::Vector3(3.0f, 4.0f, 5.0f),
        color);
    assert(queue.GetVertexCount() == 2);
    assert(queue.GetVertices()[0].Position[0] == 0.0f);
    assert(queue.GetVertices()[0].Position[1] == 1.0f);
    assert(queue.GetVertices()[0].Position[2] == 2.0f);
    assert(queue.GetVertices()[0].Color[0] == color.x);
    assert(queue.GetVertices()[0].Color[1] == color.y);
    assert(queue.GetVertices()[0].Color[2] == color.z);
    assert(queue.GetVertices()[0].Color[3] == color.w);

    queue.Clear();
    assert(queue.GetVertexCount() == 0);

    queue.AddAABB(
        NorvesLib::Math::AABB(
            NorvesLib::Math::Vector3(-1.0f, -2.0f, -3.0f),
            NorvesLib::Math::Vector3(1.0f, 2.0f, 3.0f)),
        color);
    assert(queue.GetVertexCount() == 24);

    queue.Clear();
    constexpr uint32_t kExpectedSegments = 24;
    const NorvesLib::Math::Vector3 sphereCenter(2.0f, 3.0f, 4.0f);
    const float sphereRadius = 5.0f;
    queue.AddSphere(NorvesLib::Math::Sphere(sphereCenter, sphereRadius), color);
    assert(queue.GetVertexCount() == 6u * kExpectedSegments);
    assert(queue.GetVertices()[0].Color[0] == color.x);
    assert(queue.GetVertices()[0].Color[1] == color.y);
    assert(queue.GetVertices()[0].Color[2] == color.z);
    assert(queue.GetVertices()[0].Color[3] == color.w);

    queue.Clear();
    queue.AddSphere(NorvesLib::Math::Sphere(sphereCenter, 0.0f), color);
    assert(queue.GetVertexCount() == 0);
    queue.AddSphere(NorvesLib::Math::Sphere(sphereCenter, -1.0f), color);
    assert(queue.GetVertexCount() == 0);

    queue.Clear();
    queue.AddSphere(NorvesLib::Math::Sphere(sphereCenter, std::nanf("")), color);
    assert(queue.GetVertexCount() == 0);

    queue.Clear();
    queue.AddSphere(sphereCenter, 1.0e6f, color);
    assert(queue.GetVertexCount() <= DebugDrawQueue::kMaxDebugLineVertices);

    queue.Clear();
    const uint32_t maxVertices = DebugDrawQueue::kMaxDebugLineVertices;
    for (uint32_t lineIndex = 0; lineIndex <= maxVertices / 2u; ++lineIndex)
    {
        queue.AddLine(
            NorvesLib::Math::Vector3(static_cast<float>(lineIndex), 0.0f, 0.0f),
            NorvesLib::Math::Vector3(static_cast<float>(lineIndex), 1.0f, 0.0f),
            color);
    }
    assert(queue.GetVertexCount() <= maxVertices);
    assert(queue.GetVertexCount() == maxVertices);

    queue.Clear();
    assert(queue.GetVertexCount() == 0);

    std::cout << "DebugDrawQueueTest passed\n";
    return 0;
}
