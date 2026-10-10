#include "Component/Component.h"
#include "Ecs/EcsWorld.h"
#include "Object/ObjectPropertyOps.h"
#include "Object/Reflection.h"
#include "Object/SchemaProjection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Ecs;
namespace Component = NorvesLib::Core::Component;

// ECS_PROPERTY（StorageKind::EcsColumn）の契約を、テスト用の Component で確かめる。
// 値は「行が無い間は予備、行が付いたら列、行を消す前に予備」と移り、どの時点でも 1 か所にだけある。
// 列への書き込み（ApplyValue・CopyValueFrom・既定値の適用）は版を付け、可変の GetValuePtr は nullptr を返す。

namespace
{
    struct ProbeColumn
    {
        float Spin;
        int32_t Count;
    };

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ProbeBase = Component::Component;

    class EcsPropertyProbe : public ProbeBase
    {
        REFLECTION_CLASS(EcsPropertyProbe, ProbeBase)

    public:
        EcsPropertyProbe() = default;

        // ECS_PROPERTY が値を探す行
        EcsRowRef GetEcsRow() const { return m_Row; }

        // 行を付け、予備の値を列へ移す（BIND-ENTITY の結び付けと同じ手順）
        uint32_t BindRow(EcsWorld &world, EcsEntity entity)
        {
            m_Row = EcsRowRef{&world, entity};
            return MoveEcsPropertyValuesToColumns(this);
        }

        // 行を消す前に、列の値を予備へ戻して行を外す
        uint32_t UnbindRow()
        {
            const uint32_t moved = MoveEcsPropertyValuesToFallback(this);
            m_Row = EcsRowRef{};
            return moved;
        }

        // 値の移動なしで行だけを外す（予備に何が残っているかを調べる）
        void DetachRowWithoutMoving() { m_Row = EcsRowRef{}; }

        // 値の移動なしで行だけを付ける（列の値をそのまま読めるかを調べる）
        void AttachRowWithoutMoving(EcsWorld &world, EcsEntity entity) { m_Row = EcsRowRef{&world, entity}; }

        ECS_PROPERTY(ProbeColumn, Spin, float, ProbeSpin)
        ECS_PROPERTY(ProbeColumn, Count, int32_t, ProbeCount)

    private:
        EcsRowRef m_Row;
    };

    IMPLEMENT_CLASS(EcsPropertyProbe, ProbeBase)

    struct ProbeWorld
    {
        EcsWorld World;
        TypeId Column = 0;
        EcsConsumerId Consumer = kInvalidEcsConsumer;
        EcsArchetypeId Archetype = kInvalidEcsArchetype;

        ProbeWorld()
        {
            Column = EcsColumnInfoOf<ProbeColumn>().Type;
            assert(World.SetColumnTracked(Column));
            const EcsColumnInfo columns[] = {EcsColumnInfoOf<ProbeColumn>()};
            Archetype = World.GetOrCreateArchetype(Container::Span<const EcsColumnInfo>(columns, 1));
            assert(Archetype != kInvalidEcsArchetype);
            Consumer = World.RegisterChangeConsumer(Column);
            assert(Consumer != kInvalidEcsConsumer);
        }

        EcsEntity NewRow()
        {
            const EcsEntity entity = World.CreateEntity(Archetype);
            assert(!entity.IsNull());
            Drain();
            return entity;
        }

        // 溜まった変更を読み捨てる（以後の書き込みは新しい版になる）
        uint32_t Drain()
        {
            Container::VariableArray<EcsChangeEntry> entries;
            return World.ReadChanges(Consumer, entries);
        }

        // 直前の Drain 以降の変更が、entity の 1 回の書き込みだけで、新しい版であることを確かめる
        uint64_t ExpectOneLoggedWrite(EcsEntity entity, uint64_t previousVersion)
        {
            Container::VariableArray<EcsChangeEntry> entries;
            const uint32_t count = World.ReadChanges(Consumer, entries);
            assert(count == 1);
            assert(entries[0].Entity == entity);
            assert(entries[0].Version > previousVersion);
            assert(World.GetRowVersion(entity, Column) == entries[0].Version);
            return entries[0].Version;
        }

        const ProbeColumn &Value(EcsEntity entity)
        {
            const ProbeColumn *value = World.GetColumnValue<ProbeColumn>(entity);
            assert(value != nullptr);
            return *value;
        }
    };

    const ClassProperty *FindProperty(const char *name)
    {
        const ClassProperty *property = EcsPropertyProbe::StaticClass()->GetProperty(Identity(name));
        assert(property != nullptr);
        return property;
    }

    StablePropertyId StableIdOf(const char *name)
    {
        return MakeStableSchemaId("NorvesLib", "Property",
                                  EcsPropertyProbe::StaticClass()->GetClassName().GetView(), Identity(name).GetView());
    }

    const PropertyDesc *FindDesc(const ClassInfo &info, const char *name)
    {
        for (const PropertyDesc &desc : info.Properties)
        {
            if (desc.Name == Container::String(name))
            {
                return &desc;
            }
        }
        return nullptr;
    }

    const ProjectedPropertyValue *FindProjected(const ObjectSnapshot &snapshot, StablePropertyId id)
    {
        for (const ProjectedPropertyValue &value : snapshot.Properties)
        {
            if (value.Property == id)
            {
                return &value;
            }
        }
        return nullptr;
    }

    void TestStorageKindAndDescriptors()
    {
        const IClass *cls = EcsPropertyProbe::StaticClass();
        assert(FindProperty("ProbeSpin")->GetStorageKind() == StorageKind::EcsColumn);
        assert(FindProperty("ProbeCount")->GetStorageKind() == StorageKind::EcsColumn);
        // 既存の PROPERTY の既定は Member のまま（継承した Component 側のプロパティで確かめる）
        bool bFoundMember = false;
        for (const ClassProperty *property : cls->GetAllProperties())
        {
            if (property && property->GetStorageKind() == StorageKind::Member)
            {
                bFoundMember = true;
            }
        }
        assert(bFoundMember);

        const ClassInfo info = BuildClassInfoSnapshot(*cls);
        const PropertyDesc *spin = FindDesc(info, "ProbeSpin");
        const PropertyDesc *count = FindDesc(info, "ProbeCount");
        assert(spin != nullptr && count != nullptr);
        assert(spin->Storage == StorageKind::EcsColumn && count->Storage == StorageKind::EcsColumn);
        assert(spin->StableId == StableIdOf("ProbeSpin") && count->StableId == StableIdOf("ProbeCount"));
        assert(spin->Get != nullptr && spin->Set != nullptr);
        for (const PropertyDesc &desc : info.Properties)
        {
            if (desc.Storage == StorageKind::Member)
            {
                assert(desc.Source != nullptr && desc.Get != nullptr && desc.Set != nullptr);
            }
        }

        // Get/Set は予備の値にも列の値にも同じように通る
        EcsPropertyProbe probe;
        PropertyValue written = PropertyValue::Create<float>(3.5f);
        assert(spin->Set(*spin, &probe, written));
        PropertyValue read;
        assert(spin->Get(*spin, &probe, read) && read.Get<float>() != nullptr && *read.Get<float>() == 3.5f);
        // 型が違う値は受けない
        assert(!spin->Set(*spin, &probe, PropertyValue::Create<int32_t>(1)));
        assert(probe.getProbeSpin().Get() == 3.5f);
    }

    void TestValueMovesFallbackToColumnToFallback()
    {
        ProbeWorld probeWorld;
        EcsWorld &world = probeWorld.World;
        const EcsEntity row = probeWorld.NewRow();

        EcsPropertyProbe probe;
        // 行が無い間は予備の値にある
        probe.getProbeSpin() = 2.5f;
        probe.getProbeCount() = -7;
        assert(probe.getProbeSpin().Get() == 2.5f && probe.getProbeCount().Get() == -7);
        assert(probeWorld.Value(row).Spin == 0.0f && probeWorld.Value(row).Count == 0);

        // 行を付けると、予備の値が列へ移る（予備は空になり、値は 1 か所）
        assert(probe.BindRow(world, row) == 2);
        assert(probeWorld.Value(row).Spin == 2.5f && probeWorld.Value(row).Count == -7);
        assert(probe.getProbeSpin().Get() == 2.5f && probe.getProbeCount().Get() == -7);
        probe.DetachRowWithoutMoving();
        assert(probe.getProbeSpin().Get() == 0.0f && probe.getProbeCount().Get() == 0); // 予備に値は残っていない
        probe.AttachRowWithoutMoving(world, row);                                       // 列の値を引き継いで読める
        assert(probe.getProbeSpin().Get() == 2.5f);

        // 行が付いている間の代入は列へ入り、予備には入らない
        probe.getProbeSpin() = 9.0f;
        assert(probeWorld.Value(row).Spin == 9.0f);
        probe.getProbeCount() = probe.getProbeCount().Get() + 1; // 取得と代入だけで読み書きする
        assert(probeWorld.Value(row).Count == -6);

        // 行を消す前に、列の値が予備へ戻る
        assert(probe.UnbindRow() == 2);
        assert(probe.getProbeSpin().Get() == 9.0f && probe.getProbeCount().Get() == -6);
        // 戻した後の書き込みは列に作用しない
        probe.getProbeSpin() = 1.0f;
        assert(probeWorld.Value(row).Spin == 9.0f);

        // 行が生きていない間は、行が付いているつもりでも予備に書く
        EcsPropertyProbe orphan;
        orphan.BindRow(world, row);
        assert(world.DestroyEntity(row));
        orphan.getProbeSpin() = 4.0f;
        assert(orphan.getProbeSpin().Get() == 4.0f);
        assert(orphan.UnbindRow() == 0);
    }

    void TestWritesStampVersions()
    {
        ProbeWorld probeWorld;
        EcsWorld &world = probeWorld.World;
        const EcsEntity rowA = probeWorld.NewRow();
        const EcsEntity rowB = probeWorld.NewRow();
        EcsPropertyProbe a;
        EcsPropertyProbe b;
        a.BindRow(world, rowA);
        b.BindRow(world, rowB);
        probeWorld.Drain();

        const ClassProperty *spin = FindProperty("ProbeSpin");
        const ClassProperty *count = FindProperty("ProbeCount");
        uint64_t version = world.GetRowVersion(rowA, probeWorld.Column);

        // ApplyValue
        assert(spin->ApplyValue(&a, PropertyValue::Create<float>(1.25f)));
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);
        assert(probeWorld.Value(rowA).Spin == 1.25f);

        // 型が違う値は書かず、版も付けない
        assert(!spin->ApplyValue(&a, PropertyValue::Create<int32_t>(5)));
        assert(probeWorld.Drain() == 0 && probeWorld.Value(rowA).Spin == 1.25f);

        // 列の一部だけを書き、他のフィールドは保たれる
        assert(count->ApplyValue(&a, PropertyValue::Create<int32_t>(11)));
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);
        assert(probeWorld.Value(rowA).Spin == 1.25f && probeWorld.Value(rowA).Count == 11);

        // 代入（getName() = v）
        a.getProbeSpin() = 6.0f;
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);

        // CopyValueFrom: 別の行の Object から写す
        b.getProbeSpin() = 8.5f;
        probeWorld.Drain();
        assert(spin->CopyValueFrom(&a, &b));
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);
        assert(probeWorld.Value(rowA).Spin == 8.5f);
        assert(world.GetRowVersion(rowB, probeWorld.Column) < version); // 写し元の行は変わらない

        // 行が無い Object からも写せる（予備の値を読む）
        EcsPropertyProbe loose;
        loose.getProbeCount() = 33;
        assert(count->CopyValueFrom(&a, &loose));
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);
        assert(probeWorld.Value(rowA).Count == 33);
        // ObjectPropertyOps の写し（全プロパティ）も同じ口を通る
        assert(CopyEditableProperties(a, b) > 0);
        assert(probeWorld.Value(rowA).Spin == 8.5f && probeWorld.Value(rowA).Count == 0);
        probeWorld.Drain();

        // 既定値の適用
        a.getProbeSpin() = 5.0f;
        probeWorld.Drain();
        spin->ApplyDefaultValue(&a);
        version = probeWorld.ExpectOneLoggedWrite(rowA, version);
        assert(probeWorld.Value(rowA).Spin == 0.0f);
        a.getProbeSpin() = 5.0f;
        probeWorld.Drain();
        assert(ApplyDefaultValues(&a) > 0);
        assert(probeWorld.Value(rowA).Spin == 0.0f && probeWorld.Value(rowA).Count == 0);
        // 既定値の取得（CaptureDefaultValue）は列の値を読み、書かない
        a.getProbeSpin() = 2.0f;
        probeWorld.Drain();
        const_cast<ClassProperty *>(spin)->CaptureDefaultValue(&a);
        assert(probeWorld.Drain() == 0);
        spin->ApplyDefaultValue(&b);
        assert(probeWorld.Value(rowB).Spin == 2.0f);
        const_cast<ClassProperty *>(spin)->CaptureDefaultValue(&loose); // 既定値を元に戻す
        spin->ApplyDefaultValue(&b);
        assert(probeWorld.Value(rowB).Spin == 0.0f);
    }

    void TestMutableValuePtrIsNull()
    {
        ProbeWorld probeWorld;
        const EcsEntity row = probeWorld.NewRow();
        EcsPropertyProbe probe;
        probe.getProbeSpin() = 1.5f;

        const ClassProperty *spin = FindProperty("ProbeSpin");
        NorvesLib::Core::IUnknown *mutableObject = &probe;
        const NorvesLib::Core::IUnknown *constObject = &probe;

        // 行が無い間も付いている間も、可変の GetValuePtr は nullptr（書き込み用の生ポインタは出さない）
        assert(spin->GetValuePtr(mutableObject) == nullptr);
        assert(probe.GetPropertyValue(Identity("ProbeSpin")) == nullptr);
        const void *fallbackPtr = spin->GetValuePtr(constObject);
        assert(fallbackPtr != nullptr && *static_cast<const float *>(fallbackPtr) == 1.5f);

        probe.BindRow(probeWorld.World, row);
        assert(spin->GetValuePtr(mutableObject) == nullptr);
        assert(probe.GetPropertyValue(Identity("ProbeSpin")) == nullptr);
        const void *columnPtr = spin->GetValuePtr(constObject);
        assert(columnPtr != nullptr && *static_cast<const float *>(columnPtr) == 1.5f);
        assert(columnPtr == &probeWorld.Value(row).Spin); // const の読み口は列の中を指す
        assert(columnPtr != fallbackPtr);
    }

    // 並列の中で数えるだけのハンドラ（abort の代わり）
    void CountOnlyParallelAccess() {}

    // 並列の中の読み書きは、行あり（列）・行なし（予備）のどちらも拒否される（開発ビルドは NDEBUG に依らず報告する）
    void TestParallelAccessRejected()
    {
#if NORVES_BUILD_DEVELOPMENT
        ProbeWorld probeWorld;
        const EcsEntity row = probeWorld.NewRow();
        EcsPropertyProbe bound;
        EcsPropertyProbe loose;
        bound.BindRow(probeWorld.World, row);
        assert(bound.getProbeSpin().Get() == 0.0f);
        bound.getProbeSpin() = 42.0f;
        loose.getProbeSpin() = 3.0f;
        probeWorld.Drain();

        const ClassProperty *spin = FindProperty("ProbeSpin");
        const EcsParallelAccessHandler previous = EcsSetParallelAccessHandler(&CountOnlyParallelAccess);
        {
            // 列: 並列の中の書き込みは失敗し、列の値も版も変わらない
            const uint64_t version = probeWorld.World.GetRowVersion(row, probeWorld.Column);
            const uint32_t before = EcsGetParallelAccessReportCount();
            {
                EcsParallelScope scope;
                assert(!spin->ApplyValue(&bound, PropertyValue::Create<float>(9.0f)));
                assert(EcsGetParallelAccessReportCount() > before);
                // 列の読み取りも拒否され、列の値は読めない
                const void *columnPtr = spin->GetValuePtr(static_cast<const NorvesLib::Core::IUnknown *>(&bound));
                assert(columnPtr != &probeWorld.Value(row).Spin);
            }
            assert(probeWorld.Value(row).Spin == 42.0f);
            assert(probeWorld.World.GetRowVersion(row, probeWorld.Column) == version);
            assert(probeWorld.Drain() == 0);

            // 予備: 行が無くても並列の中の書き込みは失敗し、予備の値が変わらない
            const uint32_t beforeFallback = EcsGetParallelAccessReportCount();
            {
                EcsParallelScope scope;
                assert(!spin->ApplyValue(&loose, PropertyValue::Create<float>(9.0f)));
                assert(!spin->CopyValueFrom(&loose, &bound));
                spin->ApplyDefaultValue(&loose);
                assert(EcsGetParallelAccessReportCount() > beforeFallback);
            }
            assert(loose.getProbeSpin().Get() == 3.0f);

            // 並列の外に戻れば、どちらも書ける
            assert(spin->ApplyValue(&loose, PropertyValue::Create<float>(9.0f)));
            assert(loose.getProbeSpin().Get() == 9.0f);
            assert(spin->ApplyValue(&bound, PropertyValue::Create<float>(9.0f)));
            assert(probeWorld.Value(row).Spin == 9.0f);
        }
        EcsSetParallelAccessHandler(previous);
#endif
    }

    void TestSnapshotAndSerializedRoundTrip()
    {
        ProbeWorld probeWorld;
        EcsWorld &world = probeWorld.World;
        const EcsEntity rowSource = probeWorld.NewRow();
        const EcsEntity rowRestored = probeWorld.NewRow();

        const StablePropertyId spinId = StableIdOf("ProbeSpin");
        const StablePropertyId countId = StableIdOf("ProbeCount");
        const StableTypeId floatType = MakeStableSchemaId("NorvesLib", "Type", Container::StringView("float"));
        const StableTypeId intType = MakeStableSchemaId("NorvesLib", "Type", Container::StringView("int32"));

        // クラスのスキーマ投影: Stable ID は行の有無に依らず同じで、置き場が EcsColumn になる
        const ClassSchemaSnapshot schemaBefore = RuntimeSchemaProjector::BuildClassSchemaSnapshot();
        EcsPropertyProbe source;
        source.getProbeSpin() = 7.25f;
        source.getProbeCount() = -3;
        source.BindRow(world, rowSource);
        const ClassSchemaSnapshot schemaAfter = RuntimeSchemaProjector::BuildClassSchemaSnapshot();
        for (const ClassSchemaSnapshot *schema : {&schemaBefore, &schemaAfter})
        {
            bool bFound = false;
            for (const ClassSchemaProjection &cls : schema->Classes)
            {
                if (Identity(cls.Name) != Identity("EcsPropertyProbe"))
                {
                    continue;
                }
                for (const PropertySchemaProjection &property : cls.Properties)
                {
                    if (property.StableId == spinId)
                    {
                        assert(property.Storage == StorageKind::EcsColumn && property.Type == floatType);
                        bFound = true;
                    }
                    if (property.StableId == countId)
                    {
                        assert(property.Storage == StorageKind::EcsColumn && property.Type == intType);
                    }
                }
            }
            assert(bFound);
        }

        // 列に値がある Object のスナップショット（const の読み口で直列化される）
        StableObjectRef ref;
        ref.SceneId = 77;
        ref.Path = "Scene/Probe";
        const ObjectSnapshot snapshot = RuntimeSchemaProjector::BuildObjectSnapshot(source, ref);
        assert(snapshot.Ref.SceneId == 77 && snapshot.Path == "Scene/Probe");
        const ProjectedPropertyValue *spinValue = FindProjected(snapshot, spinId);
        const ProjectedPropertyValue *countValue = FindProjected(snapshot, countId);
        assert(spinValue != nullptr && countValue != nullptr);
        assert(spinValue->Type == floatType && countValue->Type == intType);
        assert(countValue->SerializedValue == "-3");

        // 保存の往復: 直列化した値を読み戻して、行が無い Object と、別の行の Object に適用する
        EcsPropertyProbe looseRestored;
        EcsPropertyProbe boundRestored;
        boundRestored.BindRow(world, rowRestored);
        for (EcsPropertyProbe *target : {&looseRestored, &boundRestored})
        {
            for (const ProjectedPropertyValue &stored : snapshot.Properties)
            {
                const ClassProperty *property = nullptr;
                for (const ClassProperty *candidate : EcsPropertyProbe::StaticClass()->GetAllProperties())
                {
                    if (candidate && StableIdOf(candidate->GetName().ToString().c_str()) == stored.Property)
                    {
                        property = candidate;
                    }
                }
                assert(property != nullptr);
                PropertyValue restored;
                assert(restored.DeserializeStable(stored.Type, stored.SerializedValue));
                assert(property->ApplyValue(target, restored));
            }
            assert(target->getProbeSpin().Get() == 7.25f && target->getProbeCount().Get() == -3);

            const ObjectSnapshot again = RuntimeSchemaProjector::BuildObjectSnapshot(*target, ref);
            assert(again.Properties.size() == snapshot.Properties.size());
            for (size_t i = 0; i < snapshot.Properties.size(); ++i)
            {
                assert(again.Properties[i].Property == snapshot.Properties[i].Property); // Stable ID が変わらない
                assert(again.Properties[i].Type == snapshot.Properties[i].Type);
                assert(again.Properties[i].SerializedValue == snapshot.Properties[i].SerializedValue);
            }
        }
        assert(probeWorld.Value(rowRestored).Spin == 7.25f && probeWorld.Value(rowRestored).Count == -3);

        // 行を外して予備へ戻した後も、スナップショットは同じ値になる
        source.UnbindRow();
        const ObjectSnapshot unbound = RuntimeSchemaProjector::BuildObjectSnapshot(source, ref);
        assert(unbound.Properties.size() == snapshot.Properties.size());
        assert(FindProjected(unbound, spinId)->SerializedValue == spinValue->SerializedValue);
        assert(FindProjected(unbound, countId)->SerializedValue == "-3");
    }
}

int main()
{
    TestStorageKindAndDescriptors();
    TestValueMovesFallbackToColumnToFallback();
    TestWritesStampVersions();
    TestMutableValuePtrIsNull();
    TestParallelAccessRejected();
    TestSnapshotAndSerializedRoundTrip();
    std::cout << "EcsPropertyStorageTest 合格\n";
    return 0;
}
