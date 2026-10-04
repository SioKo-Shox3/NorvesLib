# Windows AssetCook CI（G2-S6）

`.github/workflows/windows-assetcook.yml` は `feature/game-asset-import` の関連コード更新にだけ反応する。標準 `windows-2022` で実 Core・AssetCook・CookedMeshTest を Release ビルドし、既存の単体 CLI 7 smoke の出力を保存する。main、描画/GPU試験、全CTestを実行するものではない。

## 権限と導入

- checkout / upload-artifact は確認した commit SHA に固定。checkout 認証は永続化しない
- job 権限は contents: read。新しい credential、repository 設定、課金設定を追加しない
- 2026-10-04 作者承認の公式 Vulkan SDK 1.4.309.0 を、公式の version/platform/SHA256 receipt と実ダウンロードの一致確認後に実行する
- `com.lunarg.vulkan.debug` を含める。Release でも現在の Core CMake は両構成の shaderc を検査する
- `copy_only=1` で SDK ファイルのみ配置する。VULKAN_SDK/PATH はこの job の後続 step に限って渡す。GPU driver の導入・GPU動作の証明ではない
- FreeType submodule を取得し、既定の Core 構成は維持する

公式の [Windows SDK導入手順](https://vulkan.lunarg.com/doc/view/1.4.309.0/windows/getting_started.html)、[download/checksum API](https://vulkan.lunarg.com/content/view/latest-sdk-version-api)、[ライセンス登録簿](https://vulkan.lunarg.com/license) を参照。

## 成功条件

1. 比較器の通常・Python最適化試験が成功
2. 実 Core と2 target のビルドが成功
3. 指定した CPU contract が成功
4. `Scripts/AssetCookCliParity.py capture` の Raw/Texture/Audio/Mesh/Glb/Import/Skeletal 全件が成功し、package/manifest を含む snapshot が作成される
5. snapshot に記録した SHA256 と、保存する2 exe の byte が一致する

失敗を警告だけに落とさない。成功した capture だけを before artifact とし、失敗途中の snapshot を基準に使わない。実行結果の確認前に、YAML構文検査や比較器単体試験を実CLI成功と報告しない。

## 証跡

- 一般ログ artifact: 14日。configure/build/CTest/capture/SDKのstdout/stderr、個別smokeログ、CMakeの構成情報、commit・image・ツール版・SDK receipt
- before artifact: 90日。full commit SHA/run id/attempt/configuration を名前に含め、snapshot、2 exe、provenance、SDK receipt を保存
- .git、SDK全体、全環境変数、MSBuild binary logは upload しない
- 基準を採用したら期限前にダウンロードし、snapshot 内の hash と照合して検証用ストレージへ保存する
- 同じ branch の実行は直列化し、後続pushで先行基準採取をキャンセルしない

## 残るゲート

この基準採取だけでは G2-S6 は完了しない。Main.cpp 分割後に同じ手順・fixtureで再採取し、出力を生byteで比較する。`--asset-set` は別途実装し、origin/main の CookTextureAssetSet.ps1 と Rendering3DTestSilverTextures / Rendering3DTestSilverGltfTextures の出力にも一致させる。比較時の main commit と PowerShell版を固定し、manifest の空白・キー順・BOM・改行を正規化しない。

glTF外部ファイルと sidecar の増分依存、未一致材質設定のpreflight拒否も別の受入れ条件として保持する。

## 初回取得URLの修正

初回 run 37194998794 は汎用名 `vulkan_sdk.exe.json` が404となり、SDK実行前に停止した。公式downloadページに掲載された実ファイル名 `VulkanSDK-1.4.309.0-Installer.exe` を使う。公式 `vulkan.lunarg.com/sdk/sha/1.4.309.0/windows/VulkanSDK-1.4.309.0-Installer.exe.json` のHTTP200と、同ページのSHA256 `48b132169b64fe65cdb0f20970195335a65354e73f1ea5373032c2a8bbad4297` の一致を確認した。receiptの版・platform・file・固定hashを照合してからダウンロード実体を検証する。SDKの版や導入componentは変更しない。
