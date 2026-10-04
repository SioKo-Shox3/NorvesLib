# texture spec v1 の旧出力基準（G2-S6 / GR96）

比較対象は固定した origin/main の Scripts/CookTextureAssetSet.ps1 と、Rendering3DTestSilverTextures / Rendering3DTestSilverGltfTextures の2spec。commitと11入力blobは AssetCookTextureLegacyRecipe.json を正本とする。CookAssets.ps1 と Rendering3DTestStartupMaterials は対象に含めない。

AssetCook本体は単体CLIの分割前基準 run37200047966 / artifact11302304928 の実行ファイルを使用し、実行前後にSHA256を照合する。上流全体を別途ビルドして差を混ぜない。新しいWindowsジョブではSDKを入れ直さず、固定exeと標準ランナーのC++ runtimeで実行する。

## PowerShell の固定

旧スクリプトは呼出元のPowerShellを継承する。既存CMake smokeのpowershell呼出しと揃え、Windows PowerShell 5.1を絶対パスで起動する。上位shellのpwsh版を子の版と取り違えず、実プロセスの版・実行file・SHA256を保存する。PSModulePathは子環境だけから除去する。

この選択は旧serializerの互換基準を1つに固定するためであり、全PowerShell版で同じJSONが出ると主張するものではない。ConvertTo-Jsonの空白・改行・property順は実結果を保存し、比較時に正規化しない。

## 受入条件

- 2specをそれぞれ独立した空ディレクトリへ2回ずつ実cookする
- 合計8packageと2aggregate manifestの一覧、byte数、SHA256を保存する
- 2回の結果は全byte一致する。余分なfile、欠落、asset数や参照の違いは失敗
- 入力blob/checkout byte、exe/PowerShell hashを実行前後で検証する
- 生byteのsnapshotは成功時だけartifactにし、実行ログは失敗時も残す

Scripts/CaptureTextureAssetSetBaseline.pyが採取器、Test/Tools/TextureAssetSetBaselineTest.pyが拒否契約の試験。実Windowsでの採取前に、純試験だけで基準取得を完了扱いにしない。

## 残る検証

この基準はtexture spec v1のみ。GltfTexturesも3枚の画像をcookするだけで、モデル自体の外部buffer・外部image・sidecarの増分印は別のmodel-capable batch試験で検証する。旧スクリプトが無視するusageは基準採取時に解釈し直さない。

2026-10-04受入: run37205025949 / feature SHA0893a213d8a278f65ca77895ea0cf8e681225f11。固定した2spec各2回の実cook成功、合計10出力296408686byteを保存した。Windows PowerShell5.1.20348.5622 Desktop、manifestはBOM無しCRLF・末尾改行無し（Silver3283byte、SilverGltf2160byte）。基準artifact/hashはAssetCookTextureFrozenBaseline.jsonに固定する。反復2の作業tree自体は保存せず、hash固定した実採取器内での全byte比較成功をsnapshotのreceiptとログで記録する。
