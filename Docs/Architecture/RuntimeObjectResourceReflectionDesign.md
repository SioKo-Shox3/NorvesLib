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

## 設計不変条件

この設計は将来計画であり、現行実装との差分は段階的に解消します。ただし、最終形で守る不変条件は先に固定します。

- Object memory を直接削除できる所有者は `ObjectHeap` だけにする。
- `ObjectHandle` は runtime-session 内で Object lifetime と generation を検証する参照であり、raw pointer の保存代替にする。
- 保存、外部 protocol、editor 再接続に使う永続参照は `StableObjectId` / `ObjectPath` / `SceneObjectId` として `ObjectHandle` から分ける。
- `ObjectRef<T>` は GC の strong reference、`WeakObjectRef<T>` は mark しない weak reference として扱う。
- `Outer` / `Inner` は gameplay hierarchy と名前空間を表す。所有権の実体は `ObjectHeap`、到達可能性は GC traversal が担当する。既定では `Outer -> Inner` を strong GC edge とする。
- 非所有の attachment、selection、grouping、debug view は `Outer` / `Inner` に混ぜない。必要な場合は `WeakInner`、`AttachmentRef`、または用途別 container として明示し、GC mark edge かどうかを型で分ける。
- Resource の asset lifetime は `ResourceManager` / `ResourceRegistry` が担当し、Object GC と混ぜない。
- GPU resource の lifetime は `RenderResourceManager` / RHI backend が担当し、Object/Resource metadata と混ぜない。
- Reflection schema は engine runtime schema であり、protocol/editor は projection として扱う。
- Runtime hot path は C++ member / handle / cached pointer を優先し、文字列 lookup や protocol object を持ち込まない。

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
- `ObjectHandle`: runtime-session 内の長期参照。
- `StableObjectRef`: save/load、protocol、editor 再接続用の永続参照。
- `ObjectRef<T>`: GC が追跡する Object 参照 property。
- `WeakObjectRef<T>`: GC の mark root にならない弱参照。

現行の `IUnknown::AddRef()` / `Release()` は互換層として段階的に縮小します。GC 導入後は refcount が Object memory を削除してはいけません。互換 API が残る場合も、最終的には handle pinning、external root、または Resource 用 shared ownership へ移譲します。

## ObjectHandle

runtime 内の長期保持には raw pointer を使わず、generation 付き handle を使います。

```cpp
struct ObjectHandle
{
    ObjectId Id = 0;
    uint32_t Generation = 0;
};
```

`ObjectHeap::Resolve(handle)` は generation が一致する場合だけ valid pointer を返します。これにより sweep 後の stale reference を検出できます。

`ObjectHandle` は所有権を持ちません。Object を GC root として保持したい場合は `ObjectRef<T>`、一時的に破棄を防ぎたい場合は明示的な `PinnedObject` / external root を使います。

`ObjectHandle` は process / session / heap lifetime を越えて安定しません。保存、外部 tool、editor 再接続では `StableObjectRef` を使い、runtime で必要になった時点で `ObjectResolver` が `ObjectHandle` へ解決します。

```cpp
struct StableObjectRef
{
    StableObjectId Id;
    ObjectPath Path;
    SceneObjectId SceneId;
};
```

`StableObjectRef` は単一 ID ではなく、永続参照を解決するための envelope です。各フィールドの責務は分けます。

- `SceneObjectId`: scene / prefab namespace 内で安定する局所 ID。scene object と component の永続参照ではこれを第一候補にする。
- `StableObjectId`: subsystem、project singleton、asset metadata object など、scene / prefab namespace に属さない persistable object の ID。
- `ObjectPath`: 人間が読める debug / migration / fallback key。保存済み ID が失われた場合の修復には使えるが、通常の保存形式で唯一の identity にしない。

`ObjectResolver::Resolve(ref, context)` は解決 context を必ず受け取ります。context は loaded World、Scene、Prefab、AssetRegistry、SubsystemRegistry など、どの namespace で解決するかを表します。

解決順序:

1. context が scene / prefab namespace を持ち、`SceneObjectId` が有効なら `SceneObjectId` で解決する。
2. `StableObjectId` が有効なら global / registry scope で解決する。
3. 上記が失敗し、migration mode または debug command の場合だけ `ObjectPath` fallback を許可する。
4. 複数候補、class mismatch、namespace mismatch は任意の候補に落とさず、ambiguous / failed として返す。

保存データでは scene / prefab object は `SceneObjectId` を必須、global persistable object は `StableObjectId` を必須にします。runtime-only transient object は stable reference を持たず、外部 adapter が一時的に公開する場合も保存可能な参照として扱いません。

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
- `PROPERTY(ResourceRef<T>)` は ResourceManager へ参照情報を渡せる。
- `PROPERTY(ResourceHandle<T>)` は weak identifier として扱い、keep-alive を保証しない。
- `Outer -> Inner` は既定で strong edge として mark する。非所有階層は `WeakInner` / `AttachmentRef` など別型にし、既定 traversal では mark しない。
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
    StableClassId StableId;
    String Name;
    ClassId ParentId;
    Span<PropertyDesc> Properties;
    Span<FunctionDesc> Functions;
    FactoryFn Factory;
    ClassDefaults Defaults;
    uint32_t SchemaVersion;
};
```

`ClassId` は runtime-local の高速 ID、`StableClassId` は serialization / protocol / undo / schema diff 用の安定 ID として分けます。`PropertyId`、`FunctionId` も同じ方針です。

安定 ID は初期段階から導入します。最初の実装では `module + class name + member name` の hash でよいですが、将来 rename や plugin を扱うなら明示 GUID へ移行できる形にします。

default value は CDO ではなく `ClassDefaults` に置きます。`ClassDefaults` は constructor 実行済み Object を保持する仕組みではなく、Property schema に基づく default value descriptor です。これにより serialization、diff、prefab override、`CopyEditableProperties` が CDO に依存しません。

## TypeInfo

`TypeInfo` は value ABI と property/function marshalling の中心です。型ごとの copy / destroy / serialize / reference traversal は `PropertyDesc` ではなく `TypeInfo` が持ちます。

```cpp
struct TypeInfo
{
    TypeId Id;
    StableTypeId StableId;
    String Name;
    TypeKind Kind;
    TypeOps Ops;
};

struct TypeOps
{
    CopyFn Copy;
    MoveFn Move;
    DestroyFn Destroy;
    SerializeFn Serialize;
    DeserializeFn Deserialize;
    AddReferencesFn AddReferences;
};
```

`PropertyDesc` は `TypeId` を持ち、必要な操作は `TypeRegistry::GetOps(TypeId)` から取得します。property ごとに特殊な挙動が必要な場合だけ optional override を持たせます。

## Property

`PROPERTY` は C++ member reflection ではなく、型付き property schema として扱います。

```cpp
struct PropertyDesc
{
    PropertyId Id;
    StablePropertyId StableId;
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
- `ResourceRef`: ResourceManager に keep-alive を伝える Resource 参照。
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

`PropertyBag` は値の byte 列だけではなく、`TypeInfo` 経由で型ごとの操作を参照します。このため `PropertyBag` に入れられる型は、少なくとも copy / destroy / serialize policy を登録できる型に限定します。Object 参照、Resource 参照、配列、文字列、ユーザー定義 struct は `TypeOps` 経由で扱います。

## Function

`FUNCTION` は残します。ただし Unreal 風の万能関数反射ではなく、engine command / callable action として扱います。

```cpp
struct FunctionDesc
{
    FunctionId Id;
    StableFunctionId StableId;
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

Function invocation は次の ABI に寄せます。

```cpp
struct ObjectInvokeTarget
{
    ObjectHandle Object;
};

struct ResourceInvokeTarget
{
    ResourceHandle<Resource> Resource;
};

struct SubsystemInvokeTarget
{
    SubsystemId Subsystem;
};

struct GlobalInvokeTarget
{
};

using InvokeTarget = std::variant<
    ObjectInvokeTarget,
    ResourceInvokeTarget,
    SubsystemInvokeTarget,
    GlobalInvokeTarget>;

struct InvokeRequest
{
    InvokeTarget Target;
    StableFunctionId Function;
    Span<PropertyValue> Arguments;
    InvokeFlags Flags;
    uint64_t RequestId;
};

struct InvokeResult
{
    InvokeStatus Status;
    PropertyValue ReturnValue;
    String ErrorMessage;
    uint64_t RequestId;
};
```

`InvokeTarget` は同時に 1 種類の target だけを持つ tagged union とします。実装で `std::variant` を使わない場合も、`Kind + union` などの単一 active field 表現にして、`Object` と `Resource` が同時に有効な状態を表現できないようにします。これにより Object の member command、Resource の reload/reimport、Engine subsystem command、global debug command を同じ invocation path で扱えます。

同期呼び出しは engine 内部の最適化として許可できますが、外部 adapter、console、automation、script は request id 付き result を基本形にします。

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

Object は `ResourceHandle<T>` または `ResourceRef<T>` を持ちます。

```cpp
struct ResourceHandle
{
    ResourceId Id = 0;
    uint32_t Generation = 0;
};
```

`ResourceHandle<T>` は weak identifier です。handle だけでは Resource の keep-alive を保証しません。

`ResourceRef<T>` は ResourceManager に参照を登録する strong resource reference です。Object property として Resource を保持する場合はこちらを使います。

Resource の推奨 ownership は次の通りです。

- `ResourceRecord`: URI、type、hash、dependencies、import settings、load state を持つ registry-owned record。
- `ResourceHandle<T>`: `ResourceRecord` を指す weak/generation handle。
- `ResourceRef<T>`: ResourceManager に keep-alive を伝える strong reference。
- `ResourceMetadataObject`: 必要な場合だけ Object schema として投影する metadata view。通常 gameplay Object と同じ寿命管理にしない。
- `GpuResourceHandle`: RenderResourceManager が管理する buffer/texture/pipeline などの GPU handle。

これにより CPU asset、metadata schema、GPU resource の寿命を分離します。

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
StableObjectRef
PropertyChanged
SetPropertyRequest
InvokeFunctionRequest
InvokeResult
ResourceList
ResourceSnapshot
ResourceReloadRequest
```

重要なのは、projection は view であって runtime model そのものではないことです。

- Runtime schema は engine 内部が所有する。
- Protocol schema は Runtime schema から生成する。
- UDP/editor の都合で Object memory layout を変えない。
- protocol message は raw pointer、C++ offset、runtime-local `ObjectHandle` を持たない。
- 外部 object reference は `StableObjectRef` で渡し、adapter が必要に応じて runtime `ObjectHandle` へ解決する。

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

### Phase 0: 設計固定と禁止事項

目的は将来設計の前提を固定し、新しい依存を増やさないことです。

- この文書を基準に Object / Resource / Reflection / GC / Function の用語を統一する。
- 新規コードでは Object lifetime を `AddRef()` / `Release()` 前提にしない。
- 新規コードでは `Clone()` を汎用生成・複製 API として使わない。
- 新規コードでは `VariableContainer` を C++ member の一次ストレージとして使わない。
- 現在の macro/API は互換層として扱い、将来 schema API に寄せる。

完了条件:

- 設計不変条件がドキュメント化されている。
- 新規実装時の禁止事項がレビュー基準に含まれている。

### Phase 1: TypeInfo / Stable Schema Id / Value ABI

目的は Property、Function、serialization、projection の土台を先に固めることです。

- runtime-local ID と stable schema ID を分離する。
- `ClassInfo` / `PropertyDesc` / `FunctionDesc` の schema 型を導入する。
- `TypeInfo`、`TypeId`、`PropertyValue`、`TypeOps` を導入する。
- CDO に依存しない `ClassDefaults` を導入する。
- schema snapshot test を追加する。

完了条件:

- 起動順に依存しない stable class/property/function ID を取得できる。
- Property/Function が同じ `TypeId` / `PropertyValue` 表現を使える。
- default value、serialization、diff が `ClassDefaults` 経由で動く。

### Phase 2: ObjectHeap Skeleton / ObjectHandle

目的は GC の前に Object ownership と handle 検証を確定することです。

- `ObjectHeap` が allocation slot、generation、state、destroy queue、resolve table を持つ。
- `ObjectHandle { Id, Generation }` を追加する。
- `StableObjectRef` と `ObjectResolver` を追加し、scene / prefab / global registry の解決 context、解決優先順位、必須フィールド検証を実装する。
- `ObjectUtility::CreateObject` は互換 API として `ObjectHeap` に委譲する。
- `World::SpawnObject`、component creation、Resource metadata object creation など全 Object 派生生成経路を heap/factory へ集約する。
- raw pointer 長期保持を `ObjectHandle` / `ObjectRef<T>` へ移す準備をする。
- stale handle test、destroy queue test を追加する。

完了条件:

- Object memory を最終的に所有する場所が `ObjectHeap` に集約されている。
- `Release()` が直接 delete する経路を新規設計から切り離せている。

### Phase 3: Property Schema / PropertyBag / Reference Wrappers

目的は GC traversal、serialization、debug/edit を支える property 基盤を作ることです。

- `MemberProperty`、`BagProperty`、`ComputedProperty`、`ObjectRefProperty`、`ResourceRefProperty` を分ける。
- `ObjectRef<T>` / `WeakObjectRef<T>` を導入し、GC strong/weak を明確化する。
- `ResourceHandle<T>` と `ResourceRef<T>` を分け、weak identifier と keep-alive reference を明確化する。
- `VariableContainer` は `PropertyBag` 互換 alias または deprecated wrapper にする。
- property get/set/serialize/reference traversal test を追加する。

完了条件:

- C++ member、dynamic bag value、Object reference、Resource reference を同じ schema で列挙できる。
- `PropertyBag` の値が copy / destroy / serialize / AddReferences を正しく扱える。

### Phase 4: FunctionDesc / Invocation Queue

目的は `FUNCTION` を安全な command/action invocation として成立させることです。

- 既存の `ClassFunction` と `IFunction/TFunction` 系を `FunctionDesc` に統合する。
- params、return type、flags、thread policy、stable ID を持たせる。
- `InvokeRequest` / `InvokeResult` queue を追加する。
- `InvokeTarget` を Object / Resource / Subsystem / Global の tagged union として定義し、同時に複数 target を保持できない ABI にする。
- GameThreadOnly / RenderThreadOnly / ReadOnly / Mutating / RequiresAuthority の validation を入れる。
- console/editor/automation/script は同じ invocation path を使う。

完了条件:

- 引数付き Function が `PropertyValue` 経由で呼び出せる。
- 外部 thread から直接 C++ member function が呼ばれない。

### Phase 5: Resource Registry / Resource Metadata

目的は CPU asset、metadata schema、GPU resource の寿命を分離することです。

- `ResourceRecord` と `ResourceHandle<T>` generation を整理する。
- `ResourceHandle<T>` は weak、`ResourceRef<T>` は keep-alive として定義する。
- Resource metadata schema を `ClassInfo` / `PropertyDesc` と接続する。
- reload / reimport / unload / pin を `FunctionDesc` として公開する。
- GPU resource は `RenderResourceManager` / RHI 側に閉じ込め、asset resource と混ぜない。

完了条件:

- Object property が Resource を参照中に ResourceRegistry が意図せず回収しない。
- Resource metadata は editor なしでも engine 内で inspection / serialization できる。

### Phase 6: Mark-Sweep GC

目的は高レベル Object の明示 delete/refcount 管理を不要にすることです。

- roots、external root、pinned object、ReferenceCollector を追加する。
- `ObjectRef<T>`、container property、custom `AddReferencedObjects()` を traversal 対象にする。
- `Outer -> Inner` の既定 strong traversal と、`WeakInner` / `AttachmentRef` の非 mark traversal を分ける。
- PendingDestroy -> OnDestroying -> sweep -> generation increment の破棄順序を定義する。
- 最初は GameThread full mark-sweep とし、moving / generational GC は扱わない。
- GC tests を追加する。

完了条件:

- World root から到達可能な WorldObject / Component が保持される。
- 到達不能 Object が sweep され、stale handle が invalid になる。

### Phase 7: Incremental GC / Runtime Integration

目的は GC をフレーム実行に載せることです。

- `GCBudget` による incremental mark/sweep を追加する。
- Object mutation と GC の同期点を定義する。
- RenderThread は FramePacket snapshot のみを読み、live Object を読まないことを検証する。
- GC stats / debug dump を追加する。

完了条件:

- フレーム中に GC budget を切って実行できる。
- Object destroy と rendering snapshot が競合しない。

### Phase 8: Schema Projection / Tool Adapter

目的は runtime schema から外部連携を自然に派生させることです。

- runtime schema から protocol schema snapshot を生成する。
- ObjectSnapshot / ResourceSnapshot / PropertyDelta / InvokeFunctionRequest / InvokeResult を定義する。
- UDP transport は adapter として追加し、runtime object model には入れない。
- editor 以外の console / automation / debug view も同じ projection を使う。

完了条件:

- 外部 adapter が raw pointer、C++ offset、runtime-local ID に依存しない。
- engine 単体でも同じ schema を debug/serialization/function invocation に使える。

### Phase 9: Legacy Cleanup

目的は互換層を縮小し、最終設計に寄せることです。

- `IUnknown` の責務を縮小、または `Object` / `IReflectable` / `IGCObject` へ分割する。
- `AddRef()` / `Release()` を Object lifetime API から外す、または deprecated shim にする。
- `Clone()` を基底 API から外す。
- CDO 的 default object を public API と生成経路から削除する。
- `ResourceHandle` 名を asset handle と GPU handle で分離する。

完了条件:

- Object lifetime、Resource lifetime、GPU resource lifetime の所有者がコード上でも明確に分かれている。
- Unreal 由来の重い前提が public API から消えている。

## 実装時の判断基準

- エンジン単体で必要な責務か。
- 外部エディターなしでも意味がある schema か。
- C++ member と PropertyBag のどちらに置くべき値か。
- GC が追跡すべき参照か、ResourceManager が追跡すべき参照か。
- Runtime path に余計な string/hash lookup を持ち込んでいないか。
- RenderThread/RHI が live Object を読んでいないか。
- transport/protocol の都合が runtime model に漏れていないか。

## Open Questions

- Stable ID は初期 hash で開始し、rename/plugin 対応時に GUID へ移行するか。
- Resource metadata を必要時だけ `Object` projection にするか、常に別 root system として扱うか。
- GC を最初から incremental にするか、まず full mark-sweep にするか。
- `ObjectRef<T>` は内部に handle を持つ wrapper にするか、heap slot pointer cache も持つか。
- `PropertyBag` の `VariantStorage` は fixed variant にするか、typed erased storage にするか。
- Resource keep-alive を `ResourceRef<T>` で明示するか、ResourceManager の pin token として分離するか。
- Function の外部 invocation result は常に request id 付き async response に統一するか。

## 結論

NorvesLib の Object/Resource/Reflection は、Unreal 互換ではなく、engine runtime schema として設計します。

- Object は GC 対象の runtime entity。
- Resource は metadata と handle を中心にした asset/runtime resource entity。
- Property は型付き schema、serialization、GC traversal、debug/edit の基盤。
- Function は安全な command/action invocation。
- VariableContainer は PropertyBag として動的値/override/cache を担う。
- Editor/UDP は reflection schema の projection を使う adapter。

この形なら、ゲームエンジン単体として成立しつつ、将来の外部エディター接続にも自然に拡張できます。
