# テストの段と回し方

テストは既定ではビルドしない。既定の構成（`cmake -S . -B build`）はエンジン（Core）・Game・Toolsだけを
含み、Visual Studioのソリューションにもテストのプロジェクトは入らない。

## テストを有効にする

```
cmake -S . -B build -DNORVES_BUILD_TESTS=ON
```

無効へ戻すときは `-DNORVES_BUILD_TESTS=OFF` で構成し直す（ビルドディレクトリからテストのプロジェクトが外れる）。
テストの実行ファイルを使うスクリプト（`Scripts/RenderR8Sequences.ps1`、`Scripts/RunRenderingValidation.ps1` など）も、
有効にした構成でビルドしてから使う。

## 段

| 段 | 中身 | 選び方 | 回すとき |
|---|---|---|---|
| 軽い | CPUだけのテスト（数秒） | `ctest --test-dir build -C Debug -LE "GPU|Reference"` | 変更に関係するものを、変更のたびに |
| GPU | Vulkanを使うテスト（数秒〜数分） | `ctest --test-dir build -C Debug -L GPU -LE Reference` | 描画に関係する変更のとき、関係するものだけ |
| 参照比較 | パストレーサーとの比較・連番の確認・受入れの記録（数分〜1時間以上） | `ctest --test-dir build -C Debug -L Reference` | 節目（受入れ）だけ、PCを使ってよいとき |

参照比較の段に入れるテストは `Test/Core/Rendering/CMakeLists.txt` の `NORVES_REFERENCE_TESTS` に並べる
（ほかのディレクトリのテストは、そのディレクトリで `Reference` のラベルを付ける）。

## 束ねた実行ファイル

小さなテストは、吸収先のテストの実行ファイルに束ねてある（`Test/TestBundle/CMakeLists.txt` の
`norves_add_test_bundle`）。ctest のテスト名は束ねる前と同じで、束ねた実行ファイルを `--test=<名前>` 付きで
起動する。引数を付けずに起動すると吸収先のテストが走る。新しいテストは実行ファイルを増やさず、
関係する束へ `MEMBER` として足すか、既存のテストにケースとして足す。

## 1つだけ回す

テスト名と実行ファイル（ビルドのターゲット）が違うことがあるので、先にコマンドを確かめる。

```
ctest --test-dir build -C Debug -N -V -R "^<テスト名>$"
cmake --build build --config Debug --target <実行ファイル名>
ctest --test-dir build -C Debug -R "^<テスト名>$"
```

GPUを使うテストは、Vulkanを使えない環境ではskip（終了コード125）になる。

## 技術的負債: コード上の確からしさを見るだけの試験

既存の試験には、契約を反証せず、実装をそのまま写した検査が多い。たとえば次のようなものである。
- dispatch や render pass の数、呼び出しの並び、バイト数を `== N` で決め打ちする。
- 偽のデバイスで場面を組み、記録されたコマンドを数える。

こうした検査は、正しい変更のたびに壊れて直す手間を生む。一方で、撮影の比較・検証レイヤー・開発ビルドの検査・コードの読み合わせで見つからない不具合は捕まえていない。

`RenderGraphCompileTest.cpp` などでは副作用のある呼び出しを `assert(...)` の中に置いている（例: `assert(graph.Initialize(nullptr));`、`assert(graph.Compile());`）。同ファイルで `assert(` の中に `Initialize`・`Create`・`Compile`・`Register` の呼び出しを含む行は 344 行ある。NDEBUG の構成（RelWithDebInfo・Release）では初期化ごと消え、0xC0000005 で落ちる。修正は別途、その周りを触るときに試験を消すか、副作用を assert の外へ出す。

規模（2026-10-13 時点）:
- `Test/` の追跡ファイルは約 580 個（.cpp は約 470 個）。`Test/Core/Rendering/CMakeLists.txt` の `add_test` は約 165 個ある。
- 例として、`RenderGraphCompileTest.cpp` は約 1.55 万行・assert は約 2,700 個ある。`== N` の形の数の決め打ちは約 160 個で、このファイルに触れたコミットは約 120 個。
- GitHub Actions のワークフローは 3 本（`animation-cpu.yml`・`texture-asset-set-baseline.yml`・`windows-assetcook.yml`）。

扱い:
- 邪魔になった試験は、消す前に `set_tests_properties(<名前> PROPERTIES DISABLED TRUE)` で ctest の実行から外す（戻せる）。理由をコメントに書く。外したもの: `RenderGraphCompileTest`（2026-10-13）、`GeometryPageRequestVulkanTest`（2026-10-14。シェーダーの束縛を手で写したレイアウト）、`RenderGraphTextureUsageContractTest`（2026-11-01。VisibilityResolve の assert で落ち、対話窓のまま終わらない）、`NorvesLibBridgeAdapterScriptComponentTest` の対象と `NorvesLibBridgeAdapterScriptComponentTest`・`NorvesLibBridgeAdapterNumericEntityRegressionTest`・`NorvesLibBridgeAdapterThumbnailTest`・`BridgeBase64Test` の4試験、`AssetManifestReloadBridgeLoopbackTest`（2026-11-01。Game のソースを組み込み、全体のビルドで Game をもう一度コンパイルするので、対象ごと `EXCLUDE_FROM_ALL` にして実行からも外した）、`FrameCaptureReadbackHelperTest`（2026-11-02。対応しない形式の一覧の決め打ち）。
- 外した試験・写しの検査は、その周りを触るときに消す。期待値を書き直して生かし続けない。
- まとめての洗い出しと削除は、まだしていない（負債として残す）。

これから足す試験の約束:
- 足す・直す前に、次の 3 つを書く。書けない試験は足さない。
  - 何の契約を反証するか。
  - 壊れたときに、撮影・検証レイヤー・開発ビルドの検査・ログで気づけないか。
  - 失敗が黙って起きるか。
- 実装の写し（数・並び・バイト数の決め打ち）を書かない。可逆で影響の小さい変更には試験を足さない。
- 計画や評価で試験・CI の追加を求められても、上の 3 つで採否を決める。足すこと自体を完了の条件にしない。

`TestAssertNoDialogTest` を足す理由:
- 反証する契約: `Test/` 以下のすべてのテスト実行ファイルに共通処理が付き、Debug で assert が失敗しても対話窓を出さず、すぐ非 0 で終了する。
- ほかの手段で気づけるか: 気づけない。assert が失敗したときだけ対話窓で止まり、ctest の打ち切りまで待つだけになり、ログにも出ない。
- 失敗が黙って起きるか: 起きる。一括リンクが外れても assert が失敗するまで何も変わらない。

`DecalPoolEvictionTest` を足す理由:
- 反証する契約: `DecalPool` が上限を超えて足されたとき、足した順で一番古いものから消え、消えたハンドルは無効になり、数が上限を超えない。
- ほかの手段で気づけるか: 気づきにくい。追い出しを誤ると、新しい跡が出ない・古い跡が残り続けるのが長い遊びの後にだけ出て、撮影では 1 枚の絵に出ない。
- 失敗が黙って起きるか: 起きる。エラーもログも出ず、跡の数と並びが変わるだけ。

`DecalGBufferVulkanTest` を足す理由:
- 反証する契約: 箱の範囲内の Albedo と Normal の RGB だけが変わり、範囲外の画素・Material・各 A は変わらず、既定で草に描かない。
- ほかの手段で気づけるか: 気づきにくい。範囲外の染み出しや A の書き換えは、撮影の 1 枚では照明に紛れる。
- 失敗が黙って起きるか: 起きる。エラーもログも出ない。

共通処理は `Test/TestSupport` に置き、`Test/CMakeLists.txt` で一括リンクするため、新しいテストに個別の設定は要らない。

`VfxEffectSystemTest` を足す理由:
- 反証する契約: VFX のエフェクトのスキーマ(型・範囲・単位・既定値)、範囲外・型違いの値の拒否と前の値の保持、クックしたバイナリと元の形式の読み込みの一致(同じ種・固定の刻みで粒の位置の列が一致)、動作中の値の変更が次の刻みから効くこと、イベントで起動したエミッタの生成の位置と時刻、フリップブックの UV の矩形と再生の速さ。
- ほかの手段で気づけるか: 気づけない。起動画面の粒は乱数で散り、撮影ではクックの読み違い・値の拒否漏れ・刻みのずれを区別できない。ログにも出ない。
- 失敗が黙って起きるか: 起きる。範囲外の値を受けても、クックで値が化けても、粒は何かしら描かれ、落ちない。

`HitEffectEmissionTest` を足す理由:
- 反証する契約: 当たり 1 回で放出要求がちょうど 1 つ積まれ、位置と法線が当たりの値と一致すること。表面の名前を全バイトの一致で表から引き、表にない名前は既定の 1 種類になること。位置・法線の壊れた当たりを積まないこと。
- ほかの手段で気づけるか: 気づけない。当たりを出す側（GR16）がまだなく、起動画面に当たりは出ない。出るようになっても、要求の二重化や別の表面のエフェクトは粒の量・色の違いでしかなく、撮影やログでは区別できない。
- 失敗が黙って起きるか: 起きる。要求が 2 つ積まれても、既定へ落ちても、何かしらの粒が描かれて落ちない。

`MeshParticleInstanceTest` を足す理由:
- 反証する契約: 生きている粒の数だけメッシュのインスタンスの記録を積み、無効または破棄したインスタンスは積まない。記録の位置・拡大・ハンドル・色と縁の値が粒とインスタンスの値に一致し、溶けの閾値は時間とともに正規化年齢の式に従って変わる。動作中の値の変更が効き、画面の空間と描画モジュールの重複を拒否する。メッシュのエミッタは板に出ず、指定時は局所の +Z が速度を向く。
- ほかの手段で気づけるか: 気づけない。どのシーンにもメッシュの粒子を置かず、GPU の試験は閾値ごとの画素しか見ない。
- 失敗が黙って起きるか: 起きる。記録の数や閾値がずれても落ちず、ログにも出ない。

`MeshParticleDissolveVulkanTest` を足す理由:
- 反証する契約: 溶けの式 (`Assets/Shaders/Common/MeshParticleDissolve.glsl`) で描いた画素は、閾値 0 で全部覆われ、閾値を上げると覆う画素が単調に減り、閾値 1 で 0 になる。縁の帯は縁の色で出る。
- ほかの手段で気づけるか: 気づけない。シーンの描画経路にまだ載せておらず、GR27 の後に載せる。
- 失敗が黙って起きるか: 起きる。式やインスタンスの形が食い違っても、落ちずに全部描くか何も描かないだけになる。

`MaterialSelectionTest`・`MaterialImportDocumentTest`・`SkeletalMaterialBindingsTest` を拡充する理由: 反証する契約は、元材質（`SourceMaterial`）と生成 slot（`GeneratedSlot`）を名前の全バイトで照合し、正規化や推測をせず、設定値・`SurfaceName`・slot の割当てが選んだ 1 件にだけ届くこと。壊れても cook は成功し、別の材質へ設定や slot が入るだけでログに出ないため、撮影・検証レイヤー・開発ビルドの検査・ログでは気づけず、失敗は黙って起きる。

`CookCacheDecisionTest` を拡充する理由: 反証する契約は、静的 mesh・skinmesh・clips の入力側 sidecar にある材質設定の未一致・二重指定・未対応の `SurfaceName` を、増分の skip を許した場合も preflight で拒否し、記録の一致で検証を迂回させないこと。リターゲット先骨格の sidecar もこの契約の対象とし、`RigSplitCookTest` で BVH 入力からの増分 preflight を検証する。skip されると cook のログが出ず、前の出力がそのまま使われるため、ほかの手段では気づけず、失敗は黙って起きる。
