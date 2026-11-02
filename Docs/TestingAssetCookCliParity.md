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
Windows では2026-11-02に現行 Debug の7ケースが LF checkout と CRLF 写しの両方で成功し、各91出力の完全な snapshot を取得した。凍結79出力との正式比較と原因の切り分けも実施済み。旧基準との差はクラスタ化20件、行末2件、その他0件で、Texture の追加12件は別集計した。旧基準との無条件な全 byte 一致を示す結果ではない。一括 cook の2 spec各10出力は直接 byte 比較で一致したが、標準の管理CLI検証スクリプト全体の完走には入力・実行環境の照合が残る。各ゲートの範囲と実測値は [ローカル実行受入れ](Architecture/ManagedTextureAssetSetCli.md#実行受入れ2026-11-02ローカル-windows) を参照する。

## クラスタ化の後の基準

凍結基準 A は run `37200047966`、artifact `11302304928`、commit `d1307c32ec86ec187c7cfb3959d5452760059543` の Release。取得した snapshot SHA-256 `09af3262f55ce75cf512cfb9086c0dbf3b81cd8344233b5f10ebda8e87f36c85` と2本の exe hash は `Docs/AssetCookCliFrozenBaseline.json` と一致した。

新基準は HEAD `b9d7468e3f0c739a1b51b8a42a0fdca329d1622d` の **Debug exe＋CRLF 写し（C）** から作った。保存先は `%LOCALAPPDATA%/NorvesLib/AssetCookCliBaseline/b9d7468e3f0c-crlf/snapshot`、`snapshot.json` の SHA-256 は `e58a03535cd8c52f0121d7acb1d616be019fba64e632592d971a5f71fdfe6a52`。91出力（56 package＋35 JSON）を元 C と同じ byte で保存し、保存後も全出力の size・SHA-256・一覧を検証した。

| 採取条件 | 値 |
|---|---|
| OS / shell | Windows 11 10.0.22631 / PowerShell 7.6.9 |
| Python / CMake | 3.14.3 / 3.31.6 |
| AssetCook.exe SHA-256 | `ba17bc55a585f9f209bdf9852e5d877008c0988396b53e1b7adf32f347fc67cc` |
| CookedMeshTest.exe SHA-256 | `98f3bbc8f3a1bf3f15406f033823624c5d96a56736661545d3891e8accb2aaff` |
| source | `%TEMP%/NorvesCliP2/src-crlf`（HEAD の archive、recipe のテキストを CRLF 化） |
| LF checkout の参考 snapshot（B）SHA-256 | `a7b76d2dd924e3d776375b5437af661ad654c742298da646ea7610d1fb6aa6ff` |

旧79出力は B で57一致、クラスタ化20差、行末2差、C で59一致、クラスタ化20差。その他 (c) は0件なので C を保存した。D/E（凍結 exe＋LF/CRLF）は `--usage albedo` が未対応で Texture 途中に停止し、snapshot は作られず、生成済み6出力を直接比較した。Mesh については凍結 smoke を TEMP へ取り出して別途実行し、CRLF source の26出力が旧基準とすべて一致した。現行の差20件は `13ba56a7` のクラスタ化変更による11 package と9 manifest の hash であり、今回の format は NVMESH v0.0 のまま。LOD 階層用の任意 smoke は capture では走らない。Texture の追加12件は `bb53b109`（用途別10件）と `e4a71feb`（タイル付き2件）による。旧出力の全パス・先頭差 byte・size・二重に寄与する行末差・recipe31項目は [詳細な比較表](Architecture/ManagedTextureAssetSetCli.md#単体-cli-の凍結基準との比較) にある。

CRLF 写しは次の PowerShell 手順で再現できる。作業ツリーの HEAD と2本の exe を上の値に揃え、凍結 artifact を `%TEMP%/NorvesCliP2/frozen` に取得・検証してから実行する。archive の対象は smoke が参照する Tools・Test・Models と Scripts を含める。変換するのは recipe の30ファイルだけで、属性による binary/明示 LF を除き、全体に NUL を含まないものを LF→CRLF にする（既存 CRLF は重ねない）。今回は属性がすべて未指定で、11ファイルの byte が変わり、残るバイナリと改行の無いテキストは変わらなかった。

```powershell
$repo = (Resolve-Path .).Path
$head = 'b9d7468e3f0c739a1b51b8a42a0fdca329d1622d'
if ((git -C $repo rev-parse HEAD) -ne $head) { throw 'HEAD mismatch' }
$root = Join-Path $env:TEMP ('NorvesCliReplay-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'src-crlf'
New-Item -ItemType Directory $source | Out-Null
git -C $repo archive --format=tar --output="$root/head.tar" $head Tools/AssetCook Test/Core/Asset Assets/Models Scripts
if ($LASTEXITCODE -ne 0) { throw 'archive failed' }
tar -xf "$root/head.tar" -C $source
if ($LASTEXITCODE -ne 0) { throw 'extract failed' }
@'
import hashlib, json, pathlib, subprocess, sys
repo, source, frozen = map(pathlib.Path, sys.argv[1:])
cases = ('Texture', 'Audio', 'Mesh', 'Glb', 'Import')
paths = [source/'Tools/AssetCook'/f'AssetCook{x}Smoke.cmake.in' for x in cases]
paths += [source/'Test/Core/Asset/CookedSkeletalAssetTest.cmake.in']
for folder in ('Test/Core/Asset/Fixtures/AssetCook',
               'Assets/Models/Rendering3DTestSilverGltf', 'Assets/Models/M9Skinned'):
    paths += [p for p in (source/folder).rglob('*') if p.is_file()]
changed = []
for path in sorted(set(paths)):
    name = path.relative_to(source).as_posix()
    attr = subprocess.check_output(['git', 'check-attr', 'text', 'eol', '--', name],
                                   cwd=repo, text=True)
    data = path.read_bytes()
    if ': text: unset' in attr or ': eol: lf' in attr or b'\0' in data:
        continue
    crlf = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    if data != crlf:
        path.write_bytes(crlf)
        changed.append(name)
recipe = json.loads((frozen/'snapshot/snapshot.json').read_text(encoding='utf-8'))['metadata']['recipe']
matched = []
for name, digest in recipe.items():
    if name == 'driver':
        continue
    old = subprocess.check_output(['git', 'rev-parse', 'd1307c32:'+name], cwd=repo)
    new = subprocess.check_output(['git', 'rev-parse', 'HEAD:'+name], cwd=repo)
    if old == new:
        assert hashlib.sha256((source/name).read_bytes()).hexdigest() == digest, name
        matched.append(name)
assert len(changed) == 11 and len(matched) == 26
print('CRLF converted:', *changed, sep='\n')
print('Frozen recipe matched:', *matched, sep='\n')
'@ | python - $repo $source "$env:TEMP/NorvesCliP2/frozen"
if ($LASTEXITCODE -ne 0) { throw 'CRLF recipe mismatch' }
Set-Location $root
python "$repo/Scripts/AssetCookCliParity.py" capture --source $source --exe "$repo/build/Tools/AssetCook/Debug/AssetCook.exe" --skeletal-test-exe "$repo/build/Test/Core/Asset/Debug/CookedMeshTest.exe" --output "$root/C"
if ($LASTEXITCODE -ne 0) { throw 'capture failed' }
Get-FileHash "$root/C/snapshot/snapshot.json" -Algorithm SHA256
```

旧コミットから git blob が変わっていない26項目が凍結 recipe と hash 一致することを確認する。残る4項目は Skeletal（`2707ab89`）、Glb（`619e16eb`）、Mesh（`13ba56a7`・`4bc840da`・`585ebd09`）、Texture（`bb53b109`・`38110fef`・`e4a71feb`）の smoke 変更、1項目は driver（`b9d7468e`）の変更である。driver の hash はコピー先ではなく呼び出した作業ツリーの `Scripts/AssetCookCliParity.py` から取る。

新基準へ切り替える条件は、追加を別集計したうえで旧出力の説明不能な差 (c) が0件であること。保存するときは新しい宛先だけを使い、既存ディレクトリがあれば停止する。`snapshot/` を丸ごとコピーし、`snapshot.json` と全出力の byte を変えない。この基準と比較する capture も同じ CRLF recipe を使う。LF checkout の B を比較すると Raw と5 Mesh manifest の行末由来の差も検出する。

CI の `windows-assetcook.yml` は旧 artifact `11302304928` を参照している。新基準はローカル保存のみで、GitHub artifact として公開しておらず、CI の切り替えも行っていない。
