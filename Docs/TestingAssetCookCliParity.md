# AssetCook 単体 CLI の分割前後比較

Main.cpp の分割では、既存 smoke の成功と出力バイト一致をそれぞれ確認する。
比較器の単体試験だけでは CLI の成功や互換を証明しない。

## 分割前

Windows の通常ビルドで AssetCook と CookedMeshTest を生成する。
Python 3.10 以上、CMake、既存 smoke が使用する powershell が必要。
分割前のコミットとバイナリを保持し、リポジトリ外の新しい出力先へ記録する。

```powershell
python Scripts/AssetCookCliParity.py capture --source . --exe build/Tools/AssetCook/Debug/AssetCook.exe --skeletal-test-exe build/Test/Core/Asset/Debug/CookedMeshTest.exe --output C:/Temp/NorvesCliBefore
```

実行ファイルの配置はビルド設定に合わせて指定する。
raw、texture、audio、静的 glTF、GLB、取り込み設定、骨格の7種類を固定して実行する。
既存 CMake smoke をそのまま呼び、骨格ではバンドル member の選択を指定する。
各ケースのログを残し、全ケースで終了コード0とpackage/manifestの生成を確認して初めて snapshot を保存する。
失敗した出力先は再利用しない。既存ディレクトリは上書きしない。

## 分割後

同じ fixture と smoke のまま再ビルドし、別の新しい出力先へ記録する。

```powershell
python Scripts/AssetCookCliParity.py capture --source . --exe build/Tools/AssetCook/Debug/AssetCook.exe --skeletal-test-exe build/Test/Core/Asset/Debug/CookedMeshTest.exe --output C:/Temp/NorvesCliAfter
python Scripts/AssetCookCliParity.py compare C:/Temp/NorvesCliBefore/snapshot C:/Temp/NorvesCliAfter/snapshot
```

package は .nvpkg、manifest は名前に manifest を含む JSON または version/assets を持つ JSON として列挙する。
生成物の追加・欠落、最初に異なるバイト位置、長さを報告する。JSONの空白、改行、キー順を正規化しない。
smoke・入力fixture・比較driverのSHA-256とケース一覧も固定し、比較前に保存snapshotの改竄・欠落を検査する。
exeのSHA-256は出所として保存するが、分割で変わるため同一性条件にはしない。
CMakeListsのビルドsource列挙も分割で変わるため固定しない。rawの実行コマンドはdriverで固定する。
smoke実行中にfixtureまたは手順が変わった場合も記録を拒否する。
差が出た場合は原因を調べ、基準を無条件に更新しない。

## 一括 cook の別ゲート

texture spec v1 の受入れは origin/main の CookTextureAssetSet.ps1 経由で
Rendering3DTestSilverTextures と Rendering3DTestSilverGltfTextures を生成した結果に対し、
新 AssetCook --asset-set 経路の cooked と manifest がバイト一致すること。
単体 CLI の7ケースが通っても、この別ゲートを通過したことにはならない。
増分判定には glTF の外部ファイルとサイドカーを含める。

## 比較器だけの単体試験

```text
python Test/Tools/AssetCookCliParityTest.py -v
python -O Test/Tools/AssetCookCliParityTest.py -q
```

この試験は比較器の一致・差分・JSON空白・一覧・改竄・path等を検査する。
クラウドでは比較器の試験のみ実行済み。Windows実CLIの7ケースと分割前後・一括cookの実バイト比較は未実行。
実結果が揃うまで Main.cpp の分割ゲートは未達として扱う。
