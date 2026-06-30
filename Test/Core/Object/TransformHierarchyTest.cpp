#include "Component/MeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;

namespace
{
    constexpr float Epsilon = 0.0001f;
    constexpr float Pi = 3.14159265358979323846f;

    class TestMeshComponent : public MeshComponent
    {
    public:
        using MeshComponent::CalculateWorldMatrix;
    };

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    void ExpectVectorNear(const NorvesLib::Math::Vector3& lhs, const NorvesLib::Math::Vector3& rhs)
    {
        assert(Near(lhs.x, rhs.x));
        assert(Near(lhs.y, rhs.y));
        assert(Near(lhs.z, rhs.z));
    }

    void ExpectQuaternionNear(const NorvesLib::Math::Quaternion& lhs, const NorvesLib::Math::Quaternion& rhs)
    {
        assert(Near(lhs.x, rhs.x));
        assert(Near(lhs.y, rhs.y));
        assert(Near(lhs.z, rhs.z));
        assert(Near(lhs.w, rhs.w));
    }

    NorvesLib::Math::Vector3 ComponentDivide(
        const NorvesLib::Math::Vector3& lhs,
        const NorvesLib::Math::Vector3& rhs)
    {
        return NorvesLib::Math::Vector3(
            rhs.x != 0.0f ? lhs.x / rhs.x : 0.0f,
            rhs.y != 0.0f ? lhs.y / rhs.y : 0.0f,
            rhs.z != 0.0f ? lhs.z / rhs.z : 0.0f);
    }

    NorvesLib::Math::Quaternion InverseRotation(const NorvesLib::Math::Quaternion& rotation)
    {
        return NorvesLib::Math::Quaternion(-rotation.x, -rotation.y, -rotation.z, rotation.w);
    }

    NorvesLib::Math::Vector3 InverseTransformPosition(
        const NorvesLib::Math::Transform& parentWorld,
        const NorvesLib::Math::Vector3& worldPosition)
    {
        return ComponentDivide(
            InverseRotation(parentWorld.rotation) * (worldPosition - parentWorld.position),
            parentWorld.scale);
    }

    Entity* AddChild(World& world, Entity& parent)
    {
        Entity* child = world.SpawnEntity<Entity>(&parent);
        assert(child);
        return child;
    }

    void TestRootSetterVersionAuthority()
    {
        World world;
        world.Initialize();

        Entity* root = world.SpawnObject<Entity>();
        assert(root);

        const uint64_t initialVersion = root->GetTransformVersion();
        root->SetPosition(1.0f, 2.0f, 3.0f);
        ExpectVectorNear(root->GetPosition(), NorvesLib::Math::Vector3(1.0f, 2.0f, 3.0f));
        assert(root->GetTransformVersion() == initialVersion);

        world.UpdateWorldTransforms();
        assert(root->GetTransformVersion() == initialVersion + 1);
        ExpectVectorNear(root->GetWorldTransform().position, NorvesLib::Math::Vector3(1.0f, 2.0f, 3.0f));

        world.UpdateWorldTransforms();
        assert(root->GetTransformVersion() == initialVersion + 1);

        world.Finalize();
    }

    void TestMultipleSettersCommitOnce()
    {
        World world;
        world.Initialize();

        Entity* root = world.SpawnObject<Entity>();
        assert(root);
        world.UpdateWorldTransforms();

        const uint64_t baseVersion = root->GetTransformVersion();
        root->SetPosition(5.0f, 6.0f, 7.0f);
        root->SetScale(2.0f, 3.0f, 4.0f);
        root->SetRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3::UnitY, Pi * 0.25f));
        assert(root->GetTransformVersion() == baseVersion);

        world.UpdateWorldTransforms();
        assert(root->GetTransformVersion() == baseVersion + 1);

        const NorvesLib::Math::Transform committed = root->GetWorldTransform();
        const uint64_t committedVersion = root->GetTransformVersion();
        root->SetPosition(100.0f, 200.0f, 300.0f);
        root->SetWorldTransform(committed);
        assert(root->GetTransformVersion() == committedVersion);

        world.UpdateWorldTransforms();
        assert(root->GetTransformVersion() == committedVersion);
        ExpectVectorNear(root->GetWorldTransform().position, committed.position);

        world.Finalize();
    }

    void TestChildFollowsParentUpdate()
    {
        World world;
        world.Initialize();

        Entity* parent = world.SpawnObject<Entity>();
        assert(parent);
        Entity* child = AddChild(world, *parent);
        child->SetLocalPosition(1.0f, 0.0f, 0.0f);

        parent->SetPosition(10.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        ExpectVectorNear(child->GetWorldTransform().position, NorvesLib::Math::Vector3(11.0f, 0.0f, 0.0f));

        const uint64_t childVersion = child->GetTransformVersion();
        parent->SetPosition(20.0f, 0.0f, 0.0f);
        assert(child->GetTransformVersion() == childVersion);

        world.UpdateWorldTransforms();
        ExpectVectorNear(child->GetWorldTransform().position, NorvesLib::Math::Vector3(21.0f, 0.0f, 0.0f));
        assert(child->GetTransformVersion() == childVersion + 1);

        world.Finalize();
    }

    void TestChildWorldSettersUnderScaledRotatedParent()
    {
        World world;
        world.Initialize();

        Entity* parent = world.SpawnObject<Entity>();
        assert(parent);
        Entity* child = AddChild(world, *parent);

        parent->SetLocalPosition(10.0f, 20.0f, 30.0f);
        parent->SetLocalRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3::UnitZ, Pi * 0.5f));
        parent->SetLocalScale(2.0f, 3.0f, 4.0f);
        world.UpdateWorldTransforms();

        const NorvesLib::Math::Transform parentWorld = parent->GetWorldTransform();
        const NorvesLib::Math::Vector3 desiredPosition(16.0f, 22.0f, 38.0f);
        child->SetPosition(desiredPosition);
        ExpectVectorNear(child->GetLocalTransform().position, InverseTransformPosition(parentWorld, desiredPosition));

        world.UpdateWorldTransforms();
        ExpectVectorNear(child->GetWorldTransform().position, desiredPosition);

        const NorvesLib::Math::Quaternion desiredRotation =
            parentWorld.rotation * NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3::UnitX, Pi * 0.25f);
        const NorvesLib::Math::Transform desiredWorld(
            NorvesLib::Math::Vector3(12.0f, 26.0f, 34.0f),
            desiredRotation,
            NorvesLib::Math::Vector3(4.0f, 9.0f, 8.0f));
        child->SetWorldTransform(desiredWorld);

        world.UpdateWorldTransforms();
        ExpectVectorNear(child->GetWorldTransform().position, desiredWorld.position);
        ExpectQuaternionNear(child->GetWorldTransform().rotation, desiredWorld.rotation);
        ExpectVectorNear(child->GetWorldTransform().scale, desiredWorld.scale);

        world.Finalize();
    }

    void TestMeshMatrixUsesEntityWorldTransform()
    {
        World world;
        world.Initialize();

        Entity* parent = world.SpawnObject<Entity>();
        assert(parent);
        Entity* child = AddChild(world, *parent);

        parent->SetLocalPosition(3.0f, 4.0f, 5.0f);
        parent->SetLocalRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3::UnitZ, Pi * 0.5f));
        parent->SetLocalScale(2.0f, 3.0f, 4.0f);
        child->SetLocalPosition(1.0f, 2.0f, 3.0f);
        child->SetLocalScale(0.5f, 2.0f, 1.5f);

        TestMeshComponent* mesh = new TestMeshComponent();
        assert(child->AddComponent(mesh));

        world.UpdateWorldTransforms();

        NorvesLib::Math::Matrix4x4 matrix;
        mesh->CalculateWorldMatrix(matrix);

        const NorvesLib::Math::Transform& worldTransform = child->GetWorldTransform();
        const NorvesLib::Math::Matrix4x4 transformMatrix = worldTransform.ToMatrix();

        ExpectVectorNear(matrix.GetTranslationRow(), worldTransform.position);

        const NorvesLib::Math::Vector4 matrixTranslationColumn = matrix.GetColumn(3);
        assert(Near(matrixTranslationColumn.x, 0.0f));
        assert(Near(matrixTranslationColumn.y, 0.0f));
        assert(Near(matrixTranslationColumn.z, 0.0f));

        const NorvesLib::Math::Vector4 transformTranslationColumn = transformMatrix.GetColumn(3);
        assert(Near(transformTranslationColumn.x, worldTransform.position.x));
        assert(Near(transformTranslationColumn.y, worldTransform.position.y));
        assert(Near(transformTranslationColumn.z, worldTransform.position.z));

        assert(NorvesLib::Math::MatrixUtils::ApproxEqualUpperLeft3x3(matrix, transformMatrix, Epsilon));

        world.Finalize();
    }
}

int main()
{
    std::cout << "TransformHierarchyTest start\n";

    TestRootSetterVersionAuthority();
    TestMultipleSettersCommitOnce();
    TestChildFollowsParentUpdate();
    TestChildWorldSettersUnderScaledRotatedParent();
    TestMeshMatrixUsesEntityWorldTransform();

    std::cout << "TransformHierarchyTest passed\n";
    return 0;
}
