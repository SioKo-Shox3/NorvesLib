# 取り込み設定 v1

GR78のソース隣設定は <source>.import.json（例: Dog.glb.import.json）を正本とし、cook/looseで共用する。
現段階は値・JSON解析・正規化bytesの基盤のみ。file探索、geometryへの適用、source_hash連結、CLIは後続であり、まだ設定fileを置くだけではロードへ反映されない。

## 共通の配置と責務

- Library/Core/Private/Resource/ImportSettings.h: 値・列挙・判定結果
- ImportSettings.cpp: 副作用の無い値検証と正規化bytes
- ImportSettingsJson.cpp: 実JsonDocumentによる共有解析
- AssetCookLibとCoreのlooseが共有するため、解析をTools専用には置かない。CoreからToolsへ逆依存しない
- 解析は成功時だけ出力を置換する。失敗時は前の設定を保持する

## JSON契約

rootはobject、version: 1が必須。省略時にv1と推測しない。
未知field・同名fieldの重複・型違いはエラー。JSONは既存JsonDocumentの厳密な文法に従い、JSON5やコメント構文は追加しない。
メモはmeta.noteへ書く。

- units: scale（有限で正、既定1）、またはfit: {axis: up/forward/longest, meters: 有限で正}
  - scaleとfitは同時指定不可。fitを指定したらaxis/metersは両方必須
  - units省略/空objectはscale=1
- axes: up/forwardは +X/-X/+Y/-Y/+Z/-Z、既定+Y/+Z
  - 同じ座標軸の組合せは符号が違っても不可
  - mirrorXはbool、既定false
- origin: modeはkeep/bounds_center/bounds_bottom_center/surface_centroid/custom、既定keep
  - offsetは有限number3要素、既定[0,0,0]
  - custom以外の非ゼロoffsetは拒否する
- mesh: windingはkeep/flip/auto、既定keep。flipU/flipVはbool、既定false
- meta: generator/noteはstring、seedは0〜2^53−1の整数。すべて任意、hashへ含めない
- repair/lod/material/collision/clip: 現在未実装。省略または空objectだけ受理し、非空指定/他の型はUnsupportedFeature
  - 実装を追加するGRで許可するfieldと既定値を定める。黙って無視しない

typed値ではfit未使用時のFitMetersは1、fit使用時のScaleは1に固定する。
正の極端な有限値の読み込みは許すが、geometry変換時の表現可能性は後続の変換層が検証する。

## 正規化bytes

現在の固定長は52byte。structのメモリ表現やpaddingを直接hashしない。
整数はlittle endian、doubleはIEEE754 binary64のbit列をlittle endianにする。全ての-0を+0へ正規化する。

| offset | 内容 | byte |
|---:|---|---:|
| 0 | schema version u32 = 1 | 4 |
| 4 | Scale binary64 | 8 |
| 12 | Fit: None=0 / Up=1 / Forward=2 / Longest=3 | 1 |
| 13 | FitMeters binary64 | 8 |
| 21 | Up: +X=0 / -X=1 / +Y=2 / -Y=3 / +Z=4 / -Z=5 | 1 |
| 22 | Forward（同じaxis番号） | 1 |
| 23 | mirrorX 0/1 | 1 |
| 24 | Origin: Keep=0 / BoundsCenter=1 / BoundsBottomCenter=2 / SurfaceCentroid=3 / Custom=4 | 1 |
| 25 | OriginOffset X/Y/Z binary64 | 24 |
| 49 | Winding: Keep=0 / Flip=1 / Auto=2 | 1 |
| 50 | flipU 0/1 | 1 |
| 51 | flipV 0/1 | 1 |

無効なtyped値は正規化結果Size=0。JSONの空白・キー順・meta変更はbytesへ影響しない。
sidecar無しと明示的な恒等設定は呼出側で区別する。無しなら従来source_hashへ何も連結しない。
cook algorithm versionと長さ前置きのhash連結は後続の接続層で行う。

## 検証範囲

ImportSettingsValueTestは固定bytesの既知値、全軸組合せ、非有限/非正値、列挙範囲外、矛盾、-0と各field変更を検査する。
通常・最適化・ASan/UBSan（LeakSanitizer除外）およびMEMBERのWerror compileを実行済み。
ImportSettingsJsonTestは43のJSON fixtureを登録。fixture自体のJSON文法は別途確認した。
実JsonDocumentを含むsource/testはWindows.h依存でcompile停止しており、native解析試験は未実行。
file探索・変換・cook/loose統合が通ったという意味ではない。
