# Runtime Object / Resource / Reflection Design

## 目的

この文書は NorvesLib の Object、Resource、Property、Function、VariableContainer、GC、外部エディター連携を再設計するための実装指針です。

NorvesLib は単体でゲームエンジン基盤として成立することを優先します。外部エディター接続は重要な機能ですが、Object 設計そのものを通信都合に従属させません。

基本方針は次の通りです。

```text
Runtime Object / Resource model
        -> Reflection metadata
        -> Schema projection
        -> Editor / UDP / tools / scripting / debug
```

Object と Resource はまずエンジン本体の runtime entity として設計します。Reflection は GC、serialization、scripting、debugging、resource metadata、editor sync を支える engine schema として持ちます。外部プロトコルはその schema を投影して利用します。

## 目標

- C++ ベースの中規模エンジンとして、所有権と寿命規約を追いやすくする。
- 高レベル Object には GC を提供し、利用側が細かい delete/refcount 管理をしなくてよい形にする。
- Property / Function / Resource metadata をエンジン内部機能と外部ツールの両方で使える schema にする。
- CDO、全 Object Clone、巨大 macro magic など Unreal 由来の重い前提は避ける。
- RHI、GPU resource、frame temporary allocation など performance critical な領域は GC 対象にしない。

## 非目標

- Unreal Engine 互換の Object model を作ること。
- 全 C++ オブジェクトを Object 化すること。
- 全プロパティ値を VariableContainer に置くこと。
- UDP/editor protocol を runtime object layout の一次設計にすること。
- すべての Object が Clone 可能であるという前提を置くこと。

## Runtime Object Model

`Object` は GC 対象の高レベル runtime entity です。主な責務は identity、class metadata、flags、GC traversal、lifecycle hook です。

```cpp
class Object
{
public:
    ObjectId GetId() const;
    ObjectHandle GetHandle() const;
    const ClassInfo& GetClassInfo() const;

    void Destroy();
    bool IsPendingDestroy() const;

    virtual void AddReferencedObjects(ReferenceCollector& collector) {}
    virtual void OnDestroying() {}

private:
    ObjectId m_Id;
    uint32_t m_Generation;
    ObjectFlags m_Flags;
};
```

`World` や subsystem は gameplay 上の root/owner です。実際の Object memory は `ObjectHeap` が管理します。

- `ObjectHeap`: Object の生成、登録、GC mark/sweep、handle 解決を担当する。
- `World`: WorldObject の logical root。GC root として扱う。
- `WorldObject`: scene/gameplay object。
- `Component`: WorldObject に属する機能単位。
- `ObjectHandle`: 長期参照、外部参照、protocol 用の安定参照。
- `ObjectRef<T>`: GC が追跡する Object 参照 property。
- `WeakObjectRef<T>`: GC の mark root にならない弱参照。

## ObjectHandle

外部や長期保存には raw pointer を使わず、generation 付き handle を使います。

```cpp
struct ObjectHandle
{
    ObjectId Id = 0;
    uint32_t Generation = 0;
};
```

`ObjectHeap::Resolve(handle)` は generation が一致する場合だけ valid pointer を返します。これにより sweep 後の stale reference を検出できます。

## GC

初期実装は stable pointer の mark-sweep GC とします。moving GC や世代別 GC は後回しです。

GC 対象:

- `Object`
- `WorldObject`
- `Component`
- 必要なら Resource metadata object

GC 対象外:

- RHI buffer/texture/pipeline
- command buffer
- frame temporary allocation
- renderer internal cache
- STL/Container の通常メモリ

GC root:

- Engine subsystem
- loaded World / Scene
- pinned Object
- ResourceRegistry が保持する必要のある metadata object
- 明示的に登録された external root

GC traversal:

- `PROPERTY(ObjectRef<T>)` は自動で mark 対象にする。
- `PROPERTY(ResourceHandle)` は ResourceManager へ参照情報を渡せる。
- 複雑な container や custom graph は `AddReferencedObjects()` で明示走査する。

最初は GameThread only GC とし、後から incremental budget を追加します。

```cpp
GC.CollectIncremental(GCBudget{
    .MaxObjectsPerFrame = 1000,
    .MaxTimeMs = 0.5f
});
```

## Flags

単一の巨大な `ObjectFlags` に全意味を詰め込まず、用途ごとに分離します。

- `LifecycleFlags`: Initialized, PendingDestroy, Destroying, Marked, Rooted
- `RuntimeFlags`: Transient, RuntimeOnly, HiddenInGame
- `EditorFlags`: Editable, ReadOnly, HiddenInEditor
- `SchemaFlags`: Serializable, Inspectable, ScriptVisible

必要なら内部表現は bitset でよいですが、API とドキュメント上の意味は分けます。

## ClassInfo

`ClassInfo` は runtime reflection の中心です。エディター用 protocol schema ではなく、エンジン内部 schema として定義します。

```cpp
struct ClassInfo
{
    ClassId Id;
    String Name;
    ClassId ParentId;
    Span<PropertyDesc> Properties;
    Span<FunctionDesc> Functions;
    FactoryFn Factory;
    uint32_t SchemaVersion;
};
```

`ClassId`、`PropertyId`、`FunctionId` は将来的に安定 ID が望ましいです。初期実装は runtime ID でもよいですが、外部 tool/protocol へ出す段階では class name + member name の hash、または明示 GUID を検討します。

## Property

`PROPERTY` は C++ member reflection ではなく、型付き property schema として扱います。

```cpp
struct PropertyDesc
{
    PropertyId Id;
    String Name;
    TypeId Type;
    PropertyFlags Flags;
    StorageKind Storage;
    GetterFn Get;
    SetterFn Set;
};
```

`StorageKind`:

- `Member`: C++ 実メンバを getter/setter で読む。
- `Bag`: `PropertyBag` に保存された動的値を読む。
- `Computed`: getter only の派生値。
- `ObjectRef`: GC が追跡する Object 参照。
- `ResourceRef`: ResourceHandle 参照。
- `Transient`: runtime only の値。

`PropertyFlags`:

- `Editable`
- `ReadOnly`
- `Serializable`
- `Transient`
- `RuntimeOnly`
- `EditorOnly`
- `GCReference`
- `ResourceReference`
- `Undoable`
- `Inspectable`

ホットな gameplay data は普通の C++ member に置きます。動的値、override、editor-only metadata、script value、remote adapter cache などは `PropertyBag` に置きます。

## VariableContainer / PropertyBag

現在の `VariableContainer` は、将来的には `PropertyBag` として再定義します。

役割:

- 動的 property の保存。
- prefab/scene override の保存。
- editor-only metadata の保存。
- script-defined value の保存。
- remote adapter value cache。
- serialize 差分の保持。
- runtime debug value の保持。

非役割:

- 全 C++ member の一次ストレージ。
- Object layout の代替。
- performance critical な gameplay state の強制保存先。

値は `TypeId + Value` で管理し、C++ offset には依存しない形へ寄せます。

```cpp
struct PropertyValue
{
    TypeId Type;
    VariantStorage Data;
};
```

## Function

`FUNCTION` は残します。ただし Unreal 風の万能関数反射ではなく、engine command / callable action として扱います。

```cpp
struct FunctionDesc
{
    FunctionId Id;
    String Name;
    Span<ParamDesc> Params;
    TypeId ReturnType;
    FunctionFlags Flags;
    ThreadPolicy Thread;
    InvokeFn Invoke;
};
```

`FunctionFlags`:

- `RuntimeCallable`
- `EditorCallable`
- `ConsoleCallable`
- `ReadOnly`
- `Mutating`
- `Undoable`
- `Async`
- `GameThreadOnly`
- `RenderThreadOnly`
- `RequiresAuthority`
- `Unsafe`

外部エディター、debug console、automation、script は同じ `FunctionDesc` を使えます。UDP などの transport は呼び出し経路であり、Function schema の所有者ではありません。

受信 thread で直接 C++ function を呼ばないこと。外部からの invocation は `InvokeRequest` として queue に積み、`ThreadPolicy` に従って実行します。

## Clone / Duplicate

`Clone()` は Object 基底 API から外す方向にします。

理由:

- ObjectId / ComponentId の扱いが型ごとに違う。
- Outer / World 所属の扱いが曖昧。
- Component deep copy の仕様が重い。
- runtime state、resource handle、GPU resource の扱いが統一できない。

必要になったら明示的なサービスとして設計します。

```cpp
WorldObject* DuplicateObject(const WorldObject& source, DuplicateOptions options);
void CopyEditableProperties(Object& dst, const Object& src);
```

`DuplicateObject` は editor/prefab 用の高レベル操作です。`CopyEditableProperties` は schema に基づく低レベル操作です。

## Resource Model

Resource は Object と似ていますが、同じ寿命管理にしすぎません。

```cpp
class Resource
{
public:
    ResourceId GetId() const;
    ResourceType GetType() const;
    const ResourceMetadata& GetMetadata() const;
    ResourceLoadState GetLoadState() const;
};
```

Resource の中心は metadata、URI、load state、dependency、version/hash、import settings です。

GPU resource は Resource そのものに直結させず、`RenderResourceManager` / RHI backend が RAII と render-thread deferred release で管理します。

Object は `ResourceHandle` を持ちます。

```cpp
struct ResourceHandle
{
    ResourceId Id = 0;
    uint32_t Generation = 0;
};
```

Resource schema には次を含めます。

- URI/path
- type
- load state
- version/hash
- dependencies
- editable import settings
- reload/reimport Function
- preview metadata

## Reflection Projection

外部エディターや tool には、runtime reflection から生成した projection を渡します。

```text
ClassSchemaSnapshot
ObjectList
ObjectSnapshot
PropertyChanged
SetPropertyRequest
InvokeFunctionRequest
ResourceList
ResourceSnapshot
ResourceReloadRequest
```

重要なのは、projection は view であって runtime model そのものではないことです。

- Runtime schema は engine 内部が所有する。
- Protocol schema は Runtime schema から生成する。
- UDP/editor の都合で Object memory layout を変えない。
- protocol message は raw pointer や C++ offset を持たない。

## Serialization / Undo / Diff

Property schema は serialization と undo/redo にも使います。

- `Serializable` property だけを save/load 対象にする。
- `Transient` / `RuntimeOnly` は保存しない。
- `Undoable` property の変更は transaction log に積める。
- `PropertyBag` は override/delta として保存できる。
- Resource import settings も Property schema として扱う。

## Threading

初期方針:

- Object 生成/破棄/GC は GameThread。
- Property set は GameThread queue 経由。
- Function invoke は `ThreadPolicy` に従って dispatch。
- RenderThread は FramePacket snapshot と RHI resource のみを扱い、live Object を直接読まない。
- Resource loading は worker thread 可能。ただし Object/Resource metadata 反映は main thread gate を通す。

## Migration Plan

### Phase 1: 仕様固定と互換層

- この文書を基準に Object/Resource/Reflection の用語を統一する。
- 現在の `Clone()` を新規利用禁止にする。
- `VariableContainer` は一次ストレージではなく `PropertyBag` 候補として扱う。
- 現在の macro/API は可能な限り薄い互換層にする。

### Phase 2: ObjectHandle / ObjectRegistry

- `ObjectHandle { Id, Generation }` を追加する。
- ObjectRegistry/ObjectHeap の前段として handle 解決テーブルを作る。
- raw pointer 長期保持を減らす。
- stale handle test を追加する。

### Phase 3: ClassInfo / TypeInfo / Stable Member Id

- `ClassInfo`、`PropertyDesc`、`FunctionDesc` を明示的な schema 型へ整理する。
- `PropertyId` / `FunctionId` を導入する。
- TypeInfo / Value 表現を追加する。
- schema snapshot test を追加する。

### Phase 4: Property Storage Backend

- `MemberProperty`、`BagProperty`、`ComputedProperty`、`ObjectRefProperty`、`ResourceRefProperty` を分ける。
- `VariableContainer` を `PropertyBag` として再定義、または互換 alias を作る。
- property get/set/serialize test を追加する。

### Phase 5: Function Invocation Queue

- FunctionDesc に params、return type、flags、thread policy を持たせる。
- `InvokeRequest` queue を追加する。
- GameThreadOnly / ReadOnly / Mutating の basic validation を入れる。
- console/editor/automation は同じ invocation path を使う。

### Phase 6: ObjectHeap + Mark-Sweep GC

- ObjectHeap が Object memory を所有する。
- roots、ReferenceCollector、ObjectRef traversal を追加する。
- PendingDestroy -> sweep の破棄順序を定義する。
- incremental budget は後段で追加する。
- GC tests を追加する。

### Phase 7: Resource Metadata / ResourceHandle

- ResourceHandle generation を追加する。
- Resource metadata schema を ClassInfo/PropertyDesc と接続する。
- reload/reimport を FunctionDesc として公開する。
- GPU resource は RenderResourceManager/RHI 側に閉じ込める。

### Phase 8: Schema Projection / Editor Adapter

- runtime schema から protocol schema snapshot を生成する。
- ObjectSnapshot / ResourceSnapshot / PropertyDelta / InvokeFunctionRequest を定義する。
- UDP transport は adapter として追加し、runtime object model には入れない。

### Phase 9: Legacy Cleanup

- refcount と GC の混在を解消する。
- `Clone()` を基底 API から外す。
- CDO 的 default object の必要性を再評価し、不要なら削除する。
- `IUnknown` の責務を縮小、または `Object` / `IReflectable` / `IGCObject` へ分割する。

## 実装時の判断基準

- エンジン単体で必要な責務か。
- 外部エディターなしでも意味がある schema か。
- C++ member と PropertyBag のどちらに置くべき値か。
- GC が追跡すべき参照か、ResourceManager が追跡すべき参照か。
- Runtime path に余計な string/hash lookup を持ち込んでいないか。
- RenderThread/RHI が live Object を読んでいないか。
- transport/protocol の都合が runtime model に漏れていないか。

## Open Questions

- Stable ID は hash ベースにするか、明示 GUID にするか。
- Resource を `Object` 派生にするか、別 root system にするか。
- GC を最初から incremental にするか、まず full mark-sweep にするか。
- `ObjectRef<T>` を property wrapper にするか、handle wrapper にするか。
- `PropertyBag` の Value 表現を variant にするか、typed erased storage にするか。
- `Function` の return value を同期 response にするか、request id 付き async response に統一するか。

## 結論

NorvesLib の Object/Resource/Reflection は、Unreal 互換ではなく、engine runtime schema として設計します。

- Object は GC 対象の runtime entity。
- Resource は metadata と handle を中心にした asset/runtime resource entity。
- Property は型付き schema、serialization、GC traversal、debug/edit の基盤。
- Function は安全な command/action invocation。
- VariableContainer は PropertyBag として動的値/override/cache を担う。
- Editor/UDP は reflection schema の projection を使う adapter。

この形なら、ゲームエンジン単体として成立しつつ、将来の外部エディター接続にも自然に拡張できます。
