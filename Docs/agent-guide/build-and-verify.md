# ビルドとテスト

> `CLAUDE.md` / `AGENTS.md`（working agreement）から移設した詳細規約。本体には索引だけを残し、全文はここが正本。

---

**Vulkan SDK** が必須（`shaderc_combined` を含む）。Slang 系ニューラルシェーダーを触るときだけ configure 時に `SLANG_SDK_DIR` を指定する。

```powershell
cmake -B build -S . -G "Visual Studio 17 2022"            # 構成（初回）
cmake --build build --config Debug --target Game            # ゲームをビルド
cmake --build build --config Debug --target FramePacketManagerTest  # 単一テストをビルド
ctest --test-dir build -C Debug                            # 全テスト
ctest --test-dir build -C Debug -R FramePacketManagerTest  # 単一テストを実行
```

- テストは CTest 登録の単体実行ファイル（`*Test.cpp`、`assert` + `std::cout` で進捗表示）。追加時は該当 `Test/<Area>/CMakeLists.txt` に `add_executable` + `add_test` し `PRIVATE Core` をリンクする。
- コンテナ・スレッド・メモリ・パース・レンダリング調整・ファイル/アセットロードの挙動を変えたらテストを追加/更新する。
- `build/` 配下の生成物（ログ、VS プロジェクト等）は編集しない。CMake のソースリスト・ソース・テスト・ドキュメント・アセットを直接更新する。
