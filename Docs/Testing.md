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

規模（2026-10-13 時点）:
- `Test/` の追跡ファイルは約 580 個（.cpp は約 470 個）。`Test/Core/Rendering/CMakeLists.txt` の `add_test` は約 165 個ある。
- 例として、`RenderGraphCompileTest.cpp` は約 1.55 万行・assert は約 2,700 個ある。`== N` の形の数の決め打ちは約 160 個で、このファイルに触れたコミットは約 120 個。
- GitHub Actions のワークフローは 3 本（`animation-cpu.yml`・`texture-asset-set-baseline.yml`・`windows-assetcook.yml`）。

扱い:
- 邪魔になった試験は、消す前に `set_tests_properties(<名前> PROPERTIES DISABLED TRUE)` で ctest の実行から外す（戻せる）。理由をコメントに書く。外したもの: `RenderGraphCompileTest`（2026-10-13）。
- 外した試験・写しの検査は、その周りを触るときに消す。期待値を書き直して生かし続けない。
- まとめての洗い出しと削除は、まだしていない（負債として残す）。

これから足す試験の約束:
- 足す・直す前に、次の 3 つを書く。書けない試験は足さない。
  - 何の契約を反証するか。
  - 壊れたときに、撮影・検証レイヤー・開発ビルドの検査・ログで気づけないか。
  - 失敗が黙って起きるか。
- 実装の写し（数・並び・バイト数の決め打ち）を書かない。可逆で影響の小さい変更には試験を足さない。
- 計画や評価で試験・CI の追加を求められても、上の 3 つで採否を決める。足すこと自体を完了の条件にしない。
