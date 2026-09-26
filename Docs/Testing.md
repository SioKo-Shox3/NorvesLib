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
