# M6 AngelScript 統合実装計画

```scope
Docs/Plans/m6-angelscript-integration-plan.md
```

## 0. ヘッダと着地規律

- **Goal**: シーン永続化された `ScriptComponent` が AngelScript の `Tick` を GameThread で実行し、失敗安全な hot reload と Bridge 経由の文字列プロパティ編集を備える。
- **承認と基準点**: 技術選定は **2026-07-20 にユーザー承認済み**。M6 の比較基準 commit は `41615f3710a90a8d90117822c5be37aa507be5bc`（M5 close）とし、最終レビューは `<base>..HEAD` に対して行う。
- **リスク分類**: 重量（ThirdParty、Core Public API、Object/Component 寿命、Application lifecycle、GameThread）。
- **着地**: feature branch / worktree は新設せず `main` 上で実施する。各フェーズはレビュー済み diff を `main` へ直接 commit / push してから次へ進む。クロスAIは本計画の計画レビューに1回、M6最終統合diffに1回のみ行う。
- **TDD 規律**: 各フェーズは test source と CMake 登録を先に commit 候補 diff へ置き、target が存在してから feature 欠落または期待挙動不一致の RED を実測する。単なる missing target を RED 証拠にしない。Phase 1 / 2 の compile RED は欠落 API symbol / header を期待理由として記録し、Phase 3 / 4 は既存 API に対する runtime assertion の RED を取る。

この tree に `Docs/agent-guide/architecture.md` と `Docs/agent-guide/build-and-verify.md` は存在しない。これらを読んだ、または準拠したとは主張しない。計画・実装の正式な照合一次情報は `AGENTS.md`、`Docs/Plans/EngineRoadmap.md`、`Docs/Architecture/ObjectModel.md`、`Docs/Architecture/RenderingFlow.md`、および実コードである。

## 1. 技術選定記録: version・取得・vendoring

AngelScript **2.38.0** を、公式配布 ZIP を一次 pin とする checked-in source snapshot として採用する。**公式 ZIP 自体はリポジトリに保持しない**。Phase 1 acquisition 時だけ temp `$Archive` を取得し、size / SHA-256 を検証してから必要な source と license を抽出する。`FetchContent`、submodule、configure 時ネットワーク取得を使わず、通常の configure / build は offline で完結させる。

| 項目 | 固定値 |
|---|---|
| 公式配布元（一次） | `https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip` |
| 配布 ZIP size | `2060096` bytes |
| 配布 ZIP SHA-256（一次 pin） | `B33B5DBCDA10317EF67D628353D83246984CE6FCAC102D4DC2AED121EBA52E6F` |
| SDK header version | `ANGELSCRIPT_VERSION = 23800` / `2.38.0` |
| upstream cross-reference | tag `v2.38.0` / commit `0601da029d846a658bf23f2888e953a45a94450a` |
| commit archive SHA-256（照合用、一次 pin ではない） | `0c2ed8bfa0bb3ace32efa842ac96ed605d6ad96bb35d8952a4e4c3acae8004bc` |
| license | zlib。原文を vendor source とともに保持する。 |

### 1.1 代替案と採否

| 選択肢 | 採否 | 理由 |
|---|---|---|
| 公式 ZIP 由来 source snapshot のチェックイン | **採用** | 公式配布物の hash を一次根拠にでき、offline configure、再現ビルド、将来の保守フォークを両立する。 |
| Git commit archive | 不採用（cross-reference のみ） | tag / commit 追跡には有用だが、公式 SDK 配布物を一次根拠にできない。 |
| Git submodule | 不採用 | 初期化に Git / network 状態を要求する。 |
| `FetchContent` | 不採用 | configure が network と上流 archive 可用性に依存する。 |

### 1.2 vendor layout と build 統合

既存の `add_subdirectory` 慣例に合わせる。Norves wrapper は `Library/ThirdParty/angelscript/CMakeLists.txt` とし、upstream selected subtree は次に限定する。

```text
Library/ThirdParty/angelscript/
  CMakeLists.txt                         # Norves wrapper、upstream source ではない
  UPSTREAM.json                          # URL、size、hash、tag/commit、選択 file manifest
  LICENSE                               # zlib 原文
  upstream/sdk/angelscript/
    include/                             # 選択 core header bytes は upstream 不変
    source/                              # 選択 core source bytes は upstream 不変
    source/as_callfunc_x64_msvc_asm.asm  # MSVC x64 で必須
```

`UPSTREAM.json` の per-file manifest は selected core file と license の relative path、byte size、SHA-256 を記録し、network を使わない `AngelScriptVendorContractTest` が vendored upstream files、header version 23800、license、add-on 不在、fetch 不在を検証する。SDK add-on（`scriptstdstring`、`scriptarray`、`scriptdictionary`、builder、ContextManager、serializer）は copy も build もしない。wrapper は `NorvesThirdParty_AngelScript` STATIC を作り、MSVC x64 では `ASM_MASM` を有効化して `as_callfunc_x64_msvc_asm.asm` を target に含める。`Library/CMakeLists.txt` は既存慣例どおり `add_subdirectory(ThirdParty/angelscript)` を追加し、`Core` がその target を `PRIVATE` link する。

ThirdParty source は vendor formatting・改名・patch をせず byte 不変に保つ。Norves 固有の CMake、manifest、解説は upstream subtree 外へ置く。

## 2. M6 v1 の設計固定

### 2.1 Engine 所有と API / ABI 境界

runtime の実体所有者は **`NorvesLib::Core::NorvesEngine GEngine` の member** である。AGENTS.md の非交渉規則に従い、新しい `*Manager` と singleton は作らない。`NorvesLib::Core::Engine::GEngine` pointer は World 所有だけを担い、script runtime を所有しない。Roadmap §10 の「World 紐付き機能は `Engine::Engine` 側へ」との緊張は、**World 実体と Component 所有は `Engine::Engine`、process-global AngelScript subsystem 所有は `NorvesEngine`、World は `Initialize(World&)` で明示借用注入**という分担で解消する。runtime は World を所有せず、shutdown 後に binding を残さない。`Engine.h` / `Engine.cpp` への runtime member / accessor 追加は禁止する。

実在 namespace に合わせ、Public 宣言は次で固定する。

```cpp
namespace NorvesLib::Core
{
    class World;
    namespace Component
    {
        class ScriptComponent;
    }

    enum class EScriptRuntimeResult : uint8
    {
        Success,
        AlreadyInitialized,
        NotInitialized,
        WrongThread,
        InvalidArgument,
        InvalidHandle,
        LoadFailed,
        CompileFailed,
        BindFailed,
        ExecutionFailed
    };

    struct ScriptBindingHandle
    {
        static constexpr uint32 InvalidSlotIndex = ~uint32{0};
        uint32 SlotIndex = InvalidSlotIndex;
        uint32 Generation = 0;
        bool IsValid() const;
        void Reset();
    };

    struct ScriptRuntimeDiagnostics
    {
        uint64 AllocationCount;
        uint64 FreeCount;
        uint64 LiveAllocationCount;
        uint64 GcStepCount;
        uint64 ReloadGeneration;
        uint32 ActiveBindingCount;
        EScriptRuntimeResult LastResult;
    };

    class ScriptRuntime final
    {
    public:
        ScriptRuntime();
        ~ScriptRuntime();
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        ScriptRuntime(ScriptRuntime&&) = delete;
        ScriptRuntime& operator=(ScriptRuntime&&) = delete;

        EScriptRuntimeResult Initialize(World& world);
        EScriptRuntimeResult Shutdown();
        EScriptRuntimeResult BeginFrameMaintenance(float deltaSeconds);
        EScriptRuntimeResult EndFrameMaintenance();
        EScriptRuntimeResult BindComponent(Component::ScriptComponent& component, ScriptBindingHandle& outHandle);
        EScriptRuntimeResult UnbindComponent(ScriptBindingHandle& handle);
        EScriptRuntimeResult TickComponent(const ScriptBindingHandle& handle, float deltaSeconds);
        bool IsInitialized() const;
        const ScriptRuntimeDiagnostics& GetDiagnostics() const;

    private:
        class Impl;
        TUniquePtr<Impl> m_Impl;
    };

    class NorvesEngine : public IEngine
    {
    public:
        ScriptRuntime& GetScriptRuntime();
        const ScriptRuntime& GetScriptRuntime() const;
    };
}
```

`WrongThread` は全 mutating entry が実行前に返し、state を不変にする。assert は補助である。

| Public API | ABI / 型境界 | 責務・結果 |
|---|---|---|
| `ScriptRuntime` | PIMPL。AngelScript 型を header に含めない | runtime 状態を非公開に保持する。 |
| `ScriptBindingHandle { Slot, Generation }` | POD value 型 | Component が保持する transient binding。slot 再利用時も generation で ABA を防ぐ。 |
| diagnostic / result 型 | NorvesLib 型のみ | success、fault、wrong-thread、missing asset/class、compile / bind error を AS 型なしで報告する。 |
| `NorvesEngine::GetScriptRuntime()` | Core public accessor | process-global runtime の参照を返す。 |
| `Initialize` / `Shutdown` | result を返す mutating entry | engine / allocator の作成・破棄を transaction として実行する。 |
| `BeginFrameMaintenance` / `EndFrameMaintenance` | result を返す mutating entry | 前者は property差分 / reload、後者は GC。 |
| `Bind` / `Unbind` / `Tick` | `ScriptComponent` と `ScriptBindingHandle` のみ | component binding の生成・無効化・script lifecycle dispatch を行う。 |

NorvesLib Public header には `asIScriptEngine`、`asIScriptObject`、`asIScriptContext`、AngelScript include、SDK `std` 型を出さない。

### 2.2 private 所有権と release 順

| 対象 | 所有者 / 参照規則 |
|---|---|
| `ScriptRuntime` | `NorvesEngine GEngine` が実体所有し、World は明示的に borrowed する。 |
| AS engine | runtime が所有し `ShutDownAndRelease` する。 |
| module / type / function | engine が所有、runtime は borrowed pointer として generation 内だけ参照する。 |
| AS script object | runtime slot が refcount ownership を持ち `Release` する。 |
| AS context | invocation ごとに runtime が所有し必ず `Release` する。 |
| `ScriptComponent` | Entity Inner。AS object を持たず `ScriptBindingHandle` と PROPERTY 値だけを持つ。 |

release 順は **context / script object → old module `DiscardModule` → engine `ShutDownAndRelease` → allocator reset** とする。`EndPlay`、`Finalize`、destructor fallback の全経路で slot を invalidate する。

### 2.3 script class と EntityRef

script class は default constructor を必須とし、許可する declaration は厳密に以下だけとする。

```angelscript
void BeginPlay(EntityRef owner) // optional
void Tick(EntityRef owner, float deltaTime) // required
void EndPlay(EntityRef owner) // optional
```

登録 API は value type `Vector3` と value type `EntityRef` の `IsValid` / `GetPosition` / `SetPosition` に限定する。`Object*`、`Entity*`、`TSharedPtr`、AS handle を公開しない。Roadmap §179 の generic reflection bridge は、M6 v1 では **typed EntityRef API に縮小**する。generic `IFunction::Invoke` は実コード上 zero-arg 制約があり、受入基準達成に不要なので後続計画へ送る。`Vector3` は値型として直接登録し、hot path に `asCALL_GENERIC` と `IFunction` の二重ディスパッチを置かない。

EntityRef resolve は slot / generation、一意に結び付く component、`bBegunPlay` / initialized、nonnull owner、owner 非 pending destroy、同一 runtime World、なお owner の Inner child であることを全て確認する。無効時は `IsValid == false`、`GetPosition` は zero を返して AS context exception を設定、`SetPosition` は false と context exception を設定する。raw / heap-owned `RemoveComponent`、Entity destruction、`MarkForDestroy`、`World::Finalize`、slot reuse で新 Entity を誤参照しないことを test し、BeginPlay / Finalize / heap-deferred destruction の実経路も確認する。

### 2.4 Component と scene persistence

`ScriptComponent` は public default constructor を持ち、以下だけを永続化する。

```cpp
PROPERTY(Container::String, ScriptPath)
PROPERTY(Container::String, ScriptClassName)
```

runtime bootstrap は scene load 前に `ScriptComponent::StaticClass()` を呼び cold registration する。cold-load test は ScriptComponent を事前 construct せずに scene を読む。transient `ScriptBindingHandle` は保存しない。`SceneSerializer`、`World`、Reflection は無改変とし、Core CMake の `PRIVATE_SOURCES` と `PUBLIC_HEADERS` に ScriptComponent / Scripting ファイルを明示列挙する。

`ScriptPath` は `NORVES_ASSET_DIR` 相対の normalized logical path で保存し、absolute path、`..`、asset root escape を reject する。Game CWD に依存しない。

### 2.5 thread、例外、allocator

`Initialize` が owner thread ID を捕捉する。全 build で全 mutating entry は owner-thread でない場合に **実行前に result = wrong-thread を返し state を不変**にする。assert は補助であり仕様の実装ではない。script 実行、load、reload、GC は GameThread 限定で、RenderThread は live World / runtime を読まない。

`asSetGlobalMemoryFunctions(Memory::Malloc, Memory::Free)` は最初の `asCreateScriptEngine` 前に設定し、最後の engine shutdown 後だけ reset する。C++ 例外と AS exception は境界内で diagnostic に変換して越境させず、faulted instance は successful rebind まで Tick しない。

### 2.6 lifecycle と frame safe point

`ApplicationProcessor::Initialize` は `World.Initialize` の直後かつ handler による scene load の前に、`Core::GEngine.GetScriptRuntime().Initialize(Engine::GEngine->GetWorld())` を呼ぶ。これは transaction / self-rollback とする。以後 handler / module の失敗も ApplicationProcessor が phase flags に従い逆順 unwind し、失敗時に `Shutdown` を呼ばない AppLauncher でも `GEngine`、World、runtime、allocator を残さない。通常 shutdown は `World.Finalize` 直後、`DestroyEngine` 前、MemorySystem shutdown 前に `Core::GEngine.GetScriptRuntime().Shutdown()` と allocator reset を行う。

frame safe point は厳密に以下とする。

1. `OnUpdate` 後、`ShouldAdvanceSimulation` 前に `ScriptRuntime::BeginFrameMaintenance`。property 差分検出と reload をここで行い、pause 中も実行する。
2. simulation が advance するときだけ World.Tick 内の ScriptComponent Tick を実行する。
3. `World.SyncToSceneView → SceneQuery.Rebuild` 後、`ModuleRegistry` / `OnPreRender` 前に `EndFrameMaintenance`。incremental GC はここで pause 中も実行する。

`RenderWorld` / `InputSystem` の BeginFrame / EndFrame は変更しない。reload は component iteration 中に実行しない。

### 2.7 reload transaction

Bridge の `ApplyValue` には通知がないため、BeginFrame に bound config と `PROPERTY` の差分を検出する。reload は unique candidate module を `asGM_ALWAYS_CREATE` で compile し、全 class / required method / staged object を validate してから frame safe point で全 slot / generation を一括 swap する。

成功時は old `EndPlay` → context / object `Release` → old module `DiscardModule` → new `BeginPlay` の順に進める。compile / validation 失敗時は candidate だけ discard し old generation は untouched のまま継続する。invalid property 文字列でも PROPERTY 表示値は維持し old generation を継続し、修正後に switch する。new `BeginPlay` failure は side effect を rollback できないため、当該 new component を Faulted にする。script state は移行せず C++ state を正とする。

## 3. スコープ

**許可**: `Docs/Plans/m6-angelscript-integration-plan.md`、`Docs/ThirdParty/AngelScript.md`、`Docs/Architecture/Scripting.md`、M6受入後の Roadmap、`Library/ThirdParty/angelscript/**`、`Library/CMakeLists.txt`、Core Scripting / ScriptComponent / `Library/Core/Public/Engine/NorvesEngine.h` / `Library/Core/Private/Engine/NorvesEngine.cpp` / ApplicationProcessor / Core CMake、Game handler / Scripting / Bridge、Assets Scripts / Scenes、hot reload PowerShell、Core Scripting tests と Bridge tests / CMake。

**禁止**: `AGENTS.md` / `CLAUDE.md`、root CMake、Modules、`Library/Core/Public/Engine/Engine.h` / `Library/Core/Private/Engine/Engine.cpp`、Object / SceneSerializer / World / Reflection の変更、RHI / Rendering / Thread、既存 ThirdParty、NorvesEditor repo、build 生成物、DAP、LSP、generic reflection bridge、script state serializer、SDK add-on、ScriptComponent 以外の gameplay API。

## 4. フェーズ1: vendor・build・bootstrap

**scope**: vendor layout、wrapper CMake、`Library/CMakeLists.txt`、Core public / private ScriptRuntime PIMPL、`NorvesEngine.h/.cpp`、Core CMake、`AngelScriptVendorContractTest.cpp` / `AngelScriptBootstrapTest.cpp` と CMake、`Docs/ThirdParty/AngelScript.md`。

**RED → green**:

1. test source と CMake registration を先に入れ、missing `ScriptRuntime` public header / symbol を expected compile RED として記録する。
2. target 作成後、network-free `AngelScriptVendorContractTest` で `UPSTREAM.json`、header version 23800、selected source / license per-file manifest、add-on 不在、fetch 不在、Public AS isolation を behavior RED にする。official ZIP の `$Archive` size / SHA-256 検証は acquisition script のみで行い、BootstrapTest で ZIP 再計算を要求しない。
3. allocator 観測は cycle ごとに `AllocationCount > 0`、`AllocationCount == FreeCount`、shutdown 後 `LiveAllocationCount == 0`、context / object / module / engine active counts 0、shutdown後 probe hook の再設定 / 解除成功を確認する。連続2 cycle の initialize → module/context/object exercise → shutdown、forced partial-init failure 後の同条件と次cycle成功を behavior RED にする。
4. wrapper を実装し `NorvesThirdParty_AngelScript` STATIC、MSVC x64 ASM_MASM / `as_callfunc_x64_msvc_asm.asm`、Core PRIVATE link と `NorvesEngine` accessor を green にする。add-on の copy / build が無いことも test する。

**verification**:

```powershell
cmake --build build --config Debug --target AngelScriptVendorContractTest AngelScriptBootstrapTest Game
ctest --test-dir build -C Debug -R "^(AngelScriptVendorContractTest|AngelScriptBootstrapTest)$" --output-on-failure --no-tests=error
cmake -S . -B build-m6-offline -G "Visual Studio 17 2022"
cmake --build build-m6-offline --config Debug --target AngelScriptBootstrapTest
```

**rollback / commit condition**: vendor / wrapper / bootstrap は一括 revert 可能。hash / manifest / license / focused CTest / offline build / Game build を通過したら `AngelScript 2.38.0を再現可能な静的依存として固定する` を main へ commit / push する。

## 5. フェーズ2: binding・ScriptComponent・scene persistence

**scope**: Core Scripting / ScriptComponent / `NorvesEngine.h/.cpp`、Core CMake、`ScriptComponentTickTest`、`ScriptComponentSceneRoundTripTest`、`ScriptPathResolutionTest`、Scripting docs。

**RED → green**:

1. source / CMake を先に入れ、missing ScriptComponent / binding API symbol を expected compile RED として記録する。
2. public default ctor、cold `StaticClass` registration、PROPERTY String 2値、PIMPL runtime、`ScriptBindingHandle`、typed `EntityRef` / `Vector3`、strict lifecycle declarationsを実装する。
3. movement、disabled、stale ref、raw / heap destruction、pending destroy、World.Finalize、slot ABA、cold scene load、roundtrip、missing file / class fault isolation を green で検証する。
4. `ScriptPathResolutionTest` は process / test CWD を repo 外 temp に変えて logical path を解決し、absolute path、`../`、`..\\`、normalized asset-root escape を明示 reject する。拒否時は runtime generation / position が不変であることを検証する。root 外 reparse point / symlink は実用的なら作成して reject し、作成不能なら環境制約として記録する。

**verification**:

```powershell
cmake --build build --config Debug --target ScriptComponentTickTest ScriptComponentSceneRoundTripTest ScriptPathResolutionTest Game
ctest --test-dir build -C Debug -R "^(ScriptComponentTickTest|ScriptComponentSceneRoundTripTest|ScriptPathResolutionTest)$" --output-on-failure --no-tests=error
```

**rollback / commit condition**: SceneSerializer / World / Reflection を変えず Component / binding を単独 revert 可能にする。focused CTest と related regression / Game build 後、`ScriptComponentからAngelScript Tickを駆動する` を main へ commit / push する。

## 6. フェーズ3: Application lifecycle・thread・例外・GC

**scope**: ApplicationProcessor、Core Scripting / `NorvesEngine.h/.cpp`、`ScriptApplicationLifecycleContractTest`、`ScriptRuntimeSafetyTest`、Scripting docs。

**RED → green**:

1. test source / CMake を先に入れ、既存 runtime API に対する initialize failure unwind、wrong-thread state preservation、exception isolation、pause maintenance、GC / allocation cleanup assertion を RED で取得する。
2. phase flags の reverse unwind、owner-thread result、exact lifecycle / safe points、fault containment、`asGC_ONE_STEP` と `NORVES_STAT` を実装して green にする。

**verification**:

```powershell
cmake --build build --config Debug --target ScriptApplicationLifecycleContractTest ScriptRuntimeSafetyTest Game
ctest --test-dir build -C Debug -R "^(ScriptApplicationLifecycleContractTest|ScriptRuntimeSafetyTest)$" --output-on-failure --no-tests=error
```

**rollback / commit condition**: lifecycle hook を revert しても Phase 2 API は変えない。focused / related regression / Game build を通過後、`ScriptRuntimeをGameThreadライフサイクルへ統合する` を main へ commit / push する。

## 7. フェーズ4: failure-safe hot reload v1

**scope**: `ScriptSourceTracker`、Core Scripting / ScriptComponent / CMake、`ScriptHotReloadTest`、Scripting docs。

**RED → green**:

1. source / CMake registration 後、既存 runtime API を使って good edit generation change、bad edit old generation continuation、recovery、property diff、multi-component atomicity、pause reload の behavior mismatch RED を取る。temp file 更新は sleep に依存しない。
2. 250 ms GameThread poll + content hash、`asGM_ALWAYS_CREATE` candidate transaction、safe-point all-slot swap、candidate-only discard を実装し green にする。

**verification**:

```powershell
cmake --build build --config Debug --target ScriptHotReloadTest Game
ctest --test-dir build -C Debug -R "^ScriptHotReloadTest$" --output-on-failure --no-tests=error
```

**rollback / commit condition**: tracker / reload path だけを revert して Phase 1〜3 の fixed script を残す。focused / related regression / Game build 後、`AngelScriptファイルの失敗安全なホットリロードを追加する` を main へ commit / push する。

## 8. フェーズ5: Bridge component surface

**scope**: `Game/Bridge/NorvesLibBridgeAdapter.*` と Bridge tests のみ。component surface は net-new である。wire の `objectId` は既に opaque string であり protocol 変更ではない。

**RED → green**:

1. Bridge SDK cache preflight を行う。未 configure なら以下を実行して target を存在させ、`NorvesLibBridgeAdapterScriptComponentTest` source / CMake を先に置く。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DNORVES_BRIDGE_SDK_DIR=C:/Users/<user>/Documents/NorvesEditor/bridge/cpp
```

2. 全 numeric parse site（6 handlers / 8 `strtoull` sites、`sceneDuplicateObject` を含む）を central resolver に寄せ、実 `BridgeEngineServer::handleFrame` loopback で component snapshot / set、numeric Entity regression を RED にする。
3. component id は `component:<ownerId>:<componentId>`。snapshot / set だけを accept し、create / delete / reparent / duplicate は explicit reject とする。Bridge set → 次 BeginFrame rebind と、bad config で old generation 継続を green にする。

**verification**:

```powershell
cmake --build build --config Debug --target NorvesLibBridgeAdapterScriptComponentTest Game
ctest --test-dir build -C Debug -R "^NorvesLibBridgeAdapterScriptComponentTest$" --output-on-failure --no-tests=error
```

**rollback / commit condition**: Bridge adapter のみを revert して Core scripting を残す。focused / numeric regression / Game build 後、`BridgeからScriptComponentの文字列プロパティを編集可能にする` を main へ commit / push する。

## 9. フェーズ6: assets と実 Game acceptance

**scope**: `Assets/Scripts/M6Mover.as`、`Assets/Scenes/M6AngelScriptDemo.scene.json`、Game Scripting / handler、`Scripts/Test-M6AngelScriptHotReload.ps1`、`M6AngelScriptAcceptanceTest` と CMake。

**RED → green**:

1. test source / CMake registration を先に入れ、scene load Entity movement / reload marker / ScriptPath negative acceptance の behavior mismatch RED を取る。Phase 2 の `ScriptPathResolutionTest` と同じ repo 外 CWD / absolute / `../` / `..\\` / normalized escape 拒否を実 Game smoke でも確認し、rejection 中の generation / position 不変を記録する。
2. opt-in `--m6-script-smoke=<scene>` を実装する。PowerShell contract は negative fixture（bad script が old generation 継続）も実行する。
3. smoke は `Assets/.m6-acceptance-<guid>` に scene / script の一時コピーを作り、logical asset reference で起動する。`finally` で削除し、tracked asset pre / post hash が不変であることを検査する。

**same-PID acceptance marker**: structured log に `pid`、`generation=1`、`generation=2`、reload 前後 position X / Y、exit code、tracked asset hash、temporary directory cleanup を出す。同一 PID で gen 1 → 2 と X → Y movement、exit 0 を証明する。

**verification**:

```powershell
cmake --build build --config Debug --target M6AngelScriptAcceptanceTest Game
ctest --test-dir build -C Debug -R "^M6AngelScriptAcceptanceTest$" --output-on-failure --no-tests=error
Scripts/Test-M6AngelScriptHotReload.ps1 -BuildDirectory build
```

**rollback / commit condition**: demo / opt-in smoke は runtime と独立に revert 可能。focused acceptance、Debug / Release Game、related regression、same-PID smoke、negative fixture を通過後、`M6の実Gameスクリプト受け入れ経路を追加する` を main へ commit / push する。

## 10. 最終検証・閉鎖

最終統合diffの review 範囲は `41615f3710a90a8d90117822c5be37aa507be5bc..HEAD` とする。実装レビューと非メインAIのクロスAIレビューを各1回だけ行い、blocking 指摘だけを最大2周で直す。

```powershell
cmake --build build --config Debug
cmake --build build --config Release --target Game
ctest --test-dir build -C Debug --output-on-failure --timeout 180 --no-tests=error
Scripts/Test-M6AngelScriptHotReload.ps1 -BuildDirectory build

# AngelScript 型の Public 漏れは全 Public で zero match を成功条件にする。
$leaks = rg -n "#include.*angelscript|asIScript|asIScriptEngine|asIScriptObject|asIScriptContext" Library/Core/Public
if ($LASTEXITCODE -eq 0) { $leaks; throw 'AngelScript型がPublic APIへ漏れている' }
if ($LASTEXITCODE -ne 1) { throw "public leak scan failed: $LASTEXITCODE" }

# std は M6で新規作成した Public file だけを対象にし、SDK boundary を含めない。
$m6PublicFiles = git diff --name-only 41615f3710a90a8d90117822c5be37aa507be5bc..HEAD -- Library/Core/Public/Scripting Library/Core/Public/Component/ScriptComponent.h
if ($m6PublicFiles) { rg -n "std::" -- $m6PublicFiles; if ($LASTEXITCODE -eq 0) { throw '新規M6 Public APIにstd型がある' }; if ($LASTEXITCODE -ne 1) { throw "public std scan failed: $LASTEXITCODE" } }

git diff --check 41615f3710a90a8d90117822c5be37aa507be5bc..HEAD
git diff --numstat 41615f3710a90a8d90117822c5be37aa507be5bc..HEAD
git diff --ignore-cr-at-eol --numstat 41615f3710a90a8d90117822c5be37aa507be5bc..HEAD
git status --short
```

新規 C++ source / test は UTF-8 BOM + CRLF、文書 / script は UTF-8 + CRLF。既存編集の numstat と ignore-CR-at-EOL numstat が違えば commit 前に EOL を復旧する。stage は宣言スコープを明示列挙し、`git add -A` / `git add .` を使わない。

M6受入証拠、main push、生成物 cleanup、clean status を確認してからだけ Roadmap を M6完了・次工程M7へ更新し、docs commit / push を行う。`git branch --merged` で全量マージ済み branch のみを確認し、clean な役目済み worktree / M6生成物だけを削除する。未マージ、dirty、使用中の対象は削除しない。

## 11. 停止条件

- 同一 process の外部 consumer が別 allocator hook / engine を求めた場合、Phase 1 を止めて設計レビューに戻す。
- M1 persistence に乗せるため SceneSerializer / World / Reflection の変更が必要になった場合、Phase 2 を止めて計画レビューに戻す。
- GameThread 外の runtime access、RenderThread から live script state access、AS handle と Object 所有権の共有が必要になった場合は同期機構を後付けせず停止する。
- 同一症状への同一手法が2回失敗したら、証拠を添えて非メインAIに相談し、3回目を試さない。
