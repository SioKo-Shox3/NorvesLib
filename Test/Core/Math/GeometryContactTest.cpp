#include "Math/GeometryIntersection.h"
#include "Math/GeometryTypes.h"
#include <Windows.h>

#include <cassert>
#include <cmath>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Math;

namespace
{
    void ConfigureFailureReporting()
    {
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    }

    bool NearlyEqual(float a, float b, float tolerance = 1e-4f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    void AssertVector(const Vector3& actual, const Vector3& expected)
    {
        assert(NearlyEqual(actual.x, expected.x));
        assert(NearlyEqual(actual.y, expected.y));
        assert(NearlyEqual(actual.z, expected.z));
    }

    void AssertContact(
        const GeometryContact& actual,
        const Vector3& expectedNormal,
        float expectedDepth,
        const Vector3& expectedPoint)
    {
        AssertVector(actual.Normal, expectedNormal);
        assert(actual.Depth >= 0.0f);
        assert(NearlyEqual(actual.Depth, expectedDepth));
        AssertVector(actual.Point, expectedPoint);
    }

    OBB MakeBox(const Vector3& center, const Vector3& halfExtents)
    {
        return OBB(
            center,
            halfExtents,
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
    }
}

int main()
{
    ConfigureFailureReporting();
    std::cout << "GeometryContactTest start\n";

    {
        GeometryContact contact;
        assert(!ComputeContact(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(2.1f, 0.0f, 0.0f), 1.0f),
            contact));
        assert(ComputeContact(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(2.0f, 0.0f, 0.0f), 1.0f),
            contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 0.0f, Vector3(1.0f, 0.0f, 0.0f));
        assert(ComputeContact(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 2.0f),
            Sphere(Vector3(1.0f, 0.0f, 0.0f), 0.5f),
            contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(1.25f, 0.0f, 0.0f));

        GeometryContact reverseContact;
        assert(ComputeContact(
            Sphere(Vector3(1.0f, 0.0f, 0.0f), 0.5f),
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 2.0f),
            reverseContact));
        AssertContact(reverseContact, Vector3(-1.0f, 0.0f, 0.0f), 1.5f, Vector3(1.25f, 0.0f, 0.0f));
        assert(SphereIntersectsSphere(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(2.0f, 0.0f, 0.0f), 1.0f)));
        std::cout << "Sphere/Sphere passed\n";
    }

    {
        const OBB box = MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
        GeometryContact contact;
        assert(!ComputeContact(Sphere(Vector3(-2.1f, 0.0f, 0.0f), 1.0f), box, contact));
        assert(ComputeContact(Sphere(Vector3(-2.0f, 0.0f, 0.0f), 1.0f), box, contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 0.0f, Vector3(-1.0f, 0.0f, 0.0f));
        assert(ComputeContact(Sphere(Vector3(0.0f, 0.0f, 0.0f), 0.5f), box, contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(-0.25f, 0.0f, 0.0f));
        std::cout << "Sphere/OBB passed\n";
    }

    {
        const OBB boxA = MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
        GeometryContact contact;
        assert(!ComputeContact(boxA, MakeBox(Vector3(2.1f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f)), contact));
        assert(ComputeContact(boxA, MakeBox(Vector3(2.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f)), contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 0.0f, Vector3(1.0f, 0.0f, 0.0f));
        assert(ComputeContact(boxA, MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.5f, 0.5f, 0.5f)), contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(0.25f, 0.0f, 0.0f));

        GeometryContact reverseContact;
        assert(ComputeContact(
            MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.5f, 0.5f, 0.5f)),
            boxA,
            reverseContact));
        AssertContact(reverseContact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(-0.25f, 0.0f, 0.0f));
        assert(OBBIntersectsAABB(boxA, AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f))));
        std::cout << "OBB/OBB passed\n";
    }

    {
        const float axisX = 0.80473785f;
        const float axisY = 0.50587936f;
        const float axisZ = -0.31061722f;
        const OBB boxA = MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 0.1f, 0.1f));
        const OBB separatedBoxB(
            Vector3(0.0f, 0.20930009f, 0.34087163f),
            Vector3(2.0f, 0.1f, 0.1f),
            Vector3(axisX, axisY, axisZ),
            Vector3(axisZ, axisX, axisY),
            Vector3(axisY, axisZ, axisX));
        GeometryContact contact;
        assert(!ComputeContact(boxA, separatedBoxB, contact));

        const OBB contactBoxB(
            Vector3(0.0f, 0.10465005f, 0.17043582f),
            Vector3(2.0f, 0.1f, 0.1f),
            Vector3(axisX, axisY, axisZ),
            Vector3(axisZ, axisX, axisY),
            Vector3(axisY, axisZ, axisX));
        assert(ComputeContact(boxA, contactBoxB, contact));
        AssertContact(
            contact,
            Vector3(0.0f, 0.52325024f, 0.85217908f),
            0.07508586f,
            Vector3(-0.00976311f, 0.07761899f, 0.06968705f));

        GeometryContact reverseContact;
        assert(ComputeContact(contactBoxB, boxA, reverseContact));
        AssertContact(
            reverseContact,
            Vector3(0.0f, -0.52325024f, -0.85217908f),
            0.07508586f,
            Vector3(-0.00976311f, 0.07761899f, 0.06968705f));
        std::cout << "OBB/OBB edge cross-axis passed\n";
    }

    {
        const Capsule capsule(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.5f);
        GeometryContact contact;
        assert(!ComputeContact(capsule, Sphere(Vector3(0.0f, 1.6f, 0.0f), 1.0f), contact));
        assert(ComputeContact(capsule, Sphere(Vector3(0.0f, 1.5f, 0.0f), 1.0f), contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 0.0f, Vector3(0.0f, 0.5f, 0.0f));
        assert(ComputeContact(
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 0.5f),
            contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(0.25f, 0.0f, 0.0f));
        assert(CapsuleIntersectsSphere(capsule, Sphere(Vector3(0.0f, 1.5f, 0.0f), 1.0f)));
        std::cout << "Capsule/Sphere passed\n";
    }

    {
        const Capsule capsule(Vector3(-2.5f, 0.0f, 0.0f), Vector3(-1.5f, 0.0f, 0.0f), 0.5f);
        const OBB box = MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
        GeometryContact contact;
        assert(!ComputeContact(capsule, MakeBox(Vector3(2.0f, 0.0f, 0.0f), Vector3(0.4f, 0.4f, 0.4f)), contact));
        assert(ComputeContact(capsule, box, contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 0.0f, Vector3(-1.0f, 0.0f, 0.0f));
        assert(ComputeContact(
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 0.5f),
            box,
            contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(-0.25f, 0.0f, 0.0f));
        assert(CapsuleIntersectsAABB(capsule, AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f))));
        std::cout << "Capsule/OBB passed\n";
    }

    {
        const Capsule capsuleA(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.5f);
        GeometryContact contact;
        assert(!ComputeContact(capsuleA, Capsule(Vector3(0.0f, 1.1f, 0.0f), Vector3(0.0f, 2.1f, 0.0f), 0.5f), contact));
        assert(ComputeContact(capsuleA, Capsule(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 2.0f, 0.0f), 0.5f), contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 0.0f, Vector3(0.0f, 0.5f, 0.0f));
        assert(ComputeContact(
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 0.5f),
            contact));
        AssertContact(contact, Vector3(1.0f, 0.0f, 0.0f), 1.5f, Vector3(0.25f, 0.0f, 0.0f));

        GeometryContact reverseContact;
        assert(ComputeContact(
            Capsule(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 2.0f, 0.0f), 0.5f),
            capsuleA,
            reverseContact));
        AssertContact(reverseContact, Vector3(0.0f, -1.0f, 0.0f), 0.0f, Vector3(0.0f, 0.5f, 0.0f));
        std::cout << "Capsule/Capsule passed\n";
    }

    {
        const Capsule capsule(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.5f);
        GeometryContact contact;
        assert(!ComputeContact(
            capsule,
            Capsule(Vector3(0.0f, 1.1f, 0.0f), Vector3(0.0f, 1.1f, 0.0f), 0.5f),
            contact));
        assert(ComputeContact(
            capsule,
            Capsule(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.5f),
            contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 0.0f, Vector3(0.0f, 0.5f, 0.0f));
        assert(ComputeContact(
            capsule,
            Capsule(Vector3(0.0f, 0.5f, 0.0f), Vector3(0.0f, 0.5f, 0.0f), 0.5f),
            contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 0.5f, Vector3(0.0f, 0.25f, 0.0f));
        std::cout << "Capsule/Capsule b-degenerate passed\n";
    }

    {
        const Capsule capsule(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.5f);
        GeometryContact contact;
        assert(ComputeContact(capsule, Sphere(Vector3(0.0f, 0.0f, 0.0f), 0.5f), contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 1.0f, Vector3(0.0f, 0.0f, 0.0f));

        const Capsule crossing(Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.5f);
        assert(ComputeContact(capsule, crossing, contact));
        AssertContact(contact, Vector3(0.0f, 0.0f, 1.0f), 1.0f, Vector3(0.0f, 0.0f, 0.0f));

        GeometryContact reverseContact;
        assert(ComputeContact(crossing, capsule, reverseContact));
        AssertContact(reverseContact, Vector3(0.0f, 0.0f, -1.0f), 1.0f, Vector3(0.0f, 0.0f, 0.0f));

        const Capsule overlapping(Vector3(-0.5f, 0.0f, 0.0f), Vector3(1.5f, 0.0f, 0.0f), 0.5f);
        assert(ComputeContact(capsule, overlapping, contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 1.0f, Vector3(-0.5f, 0.0f, 0.0f));
        std::cout << "Capsule zero-distance normal passed\n";
    }

    {
        const OBB box = MakeBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
        GeometryContact contact;
        assert(ComputeContact(
            Capsule(Vector3(-0.5f, 0.0f, 0.0f), Vector3(0.5f, 0.0f, 0.0f), 0.1f),
            box,
            contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 1.1f, Vector3(-0.25f, -0.45f, 0.0f));
        assert(ComputeContact(
            Capsule(Vector3(-2.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), 0.1f),
            box,
            contact));
        AssertContact(contact, Vector3(0.0f, 1.0f, 0.0f), 1.1f, Vector3(-1.0f, -0.45f, 0.0f));

        const float axisComponent = 0.70710678f;
        const OBB rotatedBox(
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(1.0f, 2.0f, 3.0f),
            Vector3(axisComponent, axisComponent, 0.0f),
            Vector3(-axisComponent, axisComponent, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        assert(ComputeContact(
            Capsule(Vector3(-2.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), 0.1f),
            rotatedBox,
            contact));
        AssertContact(
            contact,
            Vector3(0.0f, -1.0f, 0.0f),
            2.22132034f,
            Vector3(-1.35355339f, 1.01066017f, 0.0f));
        std::cout << "Capsule/OBB SAT support passed\n";
    }

    std::cout << "GeometryContactTest passed\n";
    return 0;
}
