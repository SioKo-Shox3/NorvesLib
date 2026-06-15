// ObjectCastTest: Phase 1 (ancestor table O(1) IsChildOf) and Phase 2 (cast flags)
// の正しさを検証する。中心は「キャストフラグ経路(第1層) と 祖先テーブル経路(第2層) と
// IsChildOf が常に一致する」という不変条件のクロスバリデーション。
#include "Animal.h"
#include "Component/Component.h"
#include "Component/MeshComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/LightComponent.h"
#include "Component/PointLightComponent.h"
#include "Object/ClassCastFlags.h"
#include "Object/ObjectCast.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Object/World.h"
#include "Object/WorldObject.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

// ---------------------------------------------------------------------------
// フラグ持ちのテスト用クラス階層（予約上位ビットを使い、エンジンのビットと衝突させない）。
// DECLARE_CLASS_CAST_FLAG の特殊化は、TClass<...> がインスタンス化される
// IMPLEMENT_CLASS より前に可視でなければならない（さもなくば主テンプレートの
// None が使われ ODR 不整合になる）ので、クラス定義 → DECLARE → IMPLEMENT_CLASS の順に置く。
// ---------------------------------------------------------------------------
namespace NorvesLib::Test
{
    class CastFlagBase : public Core::Object
    {
        REFLECTION_CLASS(CastFlagBase, Core::Object)

    public:
        CastFlagBase() = default;
    };

    class CastFlagDerived : public CastFlagBase
    {
        REFLECTION_CLASS(CastFlagDerived, CastFlagBase)

    public:
        CastFlagDerived() = default;
    };

    // フラグ未割当の兄弟（フォールバック経路＝祖先テーブルを通ることを検証する）
    class CastPlain : public Core::Object
    {
        REFLECTION_CLASS(CastPlain, Core::Object)

    public:
        CastPlain() = default;
    };
} // namespace NorvesLib::Test

// 予約上位ビット（テスト専用。エンジンの EClassCastFlags とは別ビット帯）
DECLARE_CLASS_CAST_FLAG(NorvesLib::Test::CastFlagBase, static_cast<NorvesLib::Core::EClassCastFlags>(1ull << 60))
DECLARE_CLASS_CAST_FLAG(NorvesLib::Test::CastFlagDerived, static_cast<NorvesLib::Core::EClassCastFlags>(1ull << 61))

namespace NorvesLib::Test
{
    IMPLEMENT_CLASS(CastFlagBase, Core::Object)
    IMPLEMENT_CLASS(CastFlagDerived, CastFlagBase)
    IMPLEMENT_CLASS(CastPlain, Core::Object)
} // namespace NorvesLib::Test

namespace
{
    // クラスレベルの不変条件：フラグ持ち T について
    //   HasAnyFlags(c->GetCastFlags(), flag(T)) == c->IsChildOf(T::StaticClass())
    template <typename T>
    void CheckClassConsistency(const IClass *c)
    {
        constexpr EClassCastFlags flag = ClassCastFlagTraits<T>::Value;
        static_assert(flag != EClassCastFlags::None, "CheckClassConsistency requires a flagged type");
        const bool byFlag = HasAnyFlags(c->GetCastFlags(), flag);
        const bool byTree = c->IsChildOf(T::StaticClass());
        assert(byFlag == byTree);
    }

    // インスタンスレベルの不変条件：CastTo<T>(o)!=nullptr == o.GetClass()->IsChildOf(T::StaticClass())
    template <typename T, typename U>
    void CheckCastConsistency(U *o)
    {
        T *viaCast = CastTo<T>(o);
        const bool viaTree = o->GetClass()->IsChildOf(T::StaticClass());
        assert((viaCast != nullptr) == viaTree);
    }
}

int main()
{
    std::cout << "ObjectCastTest start\n";

    // -----------------------------------------------------------------------
    // 1. エンジンのホット型：フラグ蓄積（自分＋全祖先のOR）の検証
    // -----------------------------------------------------------------------
    const IClass *objectClass = Object::StaticClass();
    const IClass *worldObjectClass = WorldObject::StaticClass();
    const IClass *worldClass = World::StaticClass();
    const IClass *resourceClass = Resource::StaticClass();
    const IClass *componentClass = Component::Component::StaticClass();
    const IClass *meshClass = Component::MeshComponent::StaticClass();
    const IClass *megaClass = Component::MegaGeometryComponent::StaticClass();
    const IClass *lightClass = Component::LightComponent::StaticClass();
    const IClass *pointLightClass = Component::PointLightComponent::StaticClass();

    // Object はルートで自分のビットのみ
    assert(objectClass->GetCastFlags() == EClassCastFlags::Object);

    // MeshComponent は MeshComponent|Component|Object を含む
    assert(HasAnyFlags(meshClass->GetCastFlags(), EClassCastFlags::MeshComponent));
    assert(HasAnyFlags(meshClass->GetCastFlags(), EClassCastFlags::Component));
    assert(HasAnyFlags(meshClass->GetCastFlags(), EClassCastFlags::Object));
    // ただし MeshComponent は LightComponent ではない
    assert(!HasAnyFlags(meshClass->GetCastFlags(), EClassCastFlags::LightComponent));

    // MegaGeometryComponent は Mesh の派生
    assert(HasAnyFlags(megaClass->GetCastFlags(), EClassCastFlags::MegaGeometryComponent));
    assert(HasAnyFlags(megaClass->GetCastFlags(), EClassCastFlags::MeshComponent));
    assert(HasAnyFlags(megaClass->GetCastFlags(), EClassCastFlags::Component));

    // PointLightComponent は Light の派生（Mesh ではない）
    assert(HasAnyFlags(pointLightClass->GetCastFlags(), EClassCastFlags::PointLightComponent));
    assert(HasAnyFlags(pointLightClass->GetCastFlags(), EClassCastFlags::LightComponent));
    assert(HasAnyFlags(pointLightClass->GetCastFlags(), EClassCastFlags::Component));
    assert(!HasAnyFlags(pointLightClass->GetCastFlags(), EClassCastFlags::MeshComponent));

    // -----------------------------------------------------------------------
    // 2. クロスバリデーション：全フラグ持ち型 × 全エンジンクラスで flag経路==木経路
    // -----------------------------------------------------------------------
    const IClass *engineClasses[] = {
        objectClass, worldObjectClass, worldClass, resourceClass,
        componentClass, meshClass, megaClass, lightClass, pointLightClass,
    };
    for (const IClass *c : engineClasses)
    {
        CheckClassConsistency<Object>(c);
        CheckClassConsistency<WorldObject>(c);
        CheckClassConsistency<World>(c);
        CheckClassConsistency<Resource>(c);
        CheckClassConsistency<Component::Component>(c);
        CheckClassConsistency<Component::MeshComponent>(c);
        CheckClassConsistency<Component::MegaGeometryComponent>(c);
        CheckClassConsistency<Component::LightComponent>(c);
        CheckClassConsistency<Component::PointLightComponent>(c);
    }

    // -----------------------------------------------------------------------
    // 3. インスタンスレベル：フラグ持ちテスト階層（第1層＝単一ANDが走る）
    // -----------------------------------------------------------------------
    assert(ClassCastFlagTraits<CastFlagBase>::Value != EClassCastFlags::None);
    assert(ClassCastFlagTraits<CastFlagDerived>::Value != EClassCastFlags::None);

    CastFlagBase base;
    CastFlagDerived derived;
    CastPlain plain;

    // 派生→基底は成功、基底→派生は失敗
    assert(CastTo<CastFlagBase>(&derived) != nullptr);
    assert(CastTo<CastFlagDerived>(&base) == nullptr);
    assert(CastTo<CastFlagBase>(&base) != nullptr);
    assert(CastTo<Object>(&derived) != nullptr); // Object もフラグ持ち
    // 無関係（フラグ持ち同士でないが）：CastPlain は CastFlagBase ではない
    assert(CastTo<CastFlagBase>(&plain) == nullptr);
    assert(CastTo<CastFlagDerived>(&plain) == nullptr);

    // CastTo と IsChildOf の一致（フラグ経路）
    CheckCastConsistency<CastFlagBase>(&derived);
    CheckCastConsistency<CastFlagDerived>(&derived);
    CheckCastConsistency<CastFlagBase>(&base);
    CheckCastConsistency<CastFlagDerived>(&base);
    CheckCastConsistency<Object>(&derived);
    CheckCastConsistency<CastFlagBase>(&plain);

    // -----------------------------------------------------------------------
    // 4. フォールバック経路（フラグ未割当の Animal/Dog は祖先テーブルを通る）
    // -----------------------------------------------------------------------
    assert(ClassCastFlagTraits<Animal>::Value == EClassCastFlags::None);
    assert(ClassCastFlagTraits<Dog>::Value == EClassCastFlags::None);

    Dog dog;
    Animal animal;
    assert(CastTo<Animal>(&dog) != nullptr); // 派生→基底（フォールバック）
    assert(CastTo<Dog>(&animal) == nullptr); // 基底→派生
    assert(CastTo<Object>(&dog) != nullptr); // Object はフラグ持ちだが Dog 経由でも一致
    assert(CastTo<Dog>(&dog) != nullptr);
    CheckCastConsistency<Animal>(&dog);
    CheckCastConsistency<Dog>(&animal);
    CheckCastConsistency<Object>(&dog);

    // -----------------------------------------------------------------------
    // 5. null / IsA / TryCast の整合
    // -----------------------------------------------------------------------
    assert(CastTo<CastFlagBase>(static_cast<NorvesLib::Core::IUnknown *>(nullptr)) == nullptr);
    assert(CastTo<Animal>(static_cast<NorvesLib::Core::IUnknown *>(nullptr)) == nullptr);
    assert(IsA<CastFlagBase>(&derived));
    assert(!IsA<CastFlagDerived>(&base));
    assert(IsA<Animal>(&dog));
    assert(TryCast<CastFlagBase>(&derived) == CastTo<CastFlagBase>(&derived));

    // const 版
    const CastFlagDerived &cderived = derived;
    assert(CastTo<CastFlagBase>(static_cast<const NorvesLib::Core::IUnknown *>(&cderived)) != nullptr);
    assert(CastTo<CastFlagDerived>(static_cast<const NorvesLib::Core::IUnknown *>(&base)) == nullptr);

    std::cout << "ObjectCastTest passed\n";
    return 0;
}
