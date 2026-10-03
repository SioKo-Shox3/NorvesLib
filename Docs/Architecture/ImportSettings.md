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

## 共有geometry変換（GR78）

ImportTransformはinterleaved頂点の位置float3・法線float3・UV float2とuint32 indexを受けるprivateな無確保処理。
strideとfield offsetでlayoutを渡し、他のfieldやpaddingは変更しない。頂点/indexの全spanは非交差で、呼出中は不変入力とする。

- up/forwardをtargetの+Y/+Zへ写す右手直交基底を作り、mirrorXはtarget Xへ別に掛ける
- units.fitは軸変換後boundsのup=Y、forward=Z、longest=最大extentでmetersへ合わせる。選択軸のextentが0なら失敗
- bounds_centerはbounds中心、bounds_bottom_centerはXZ中心とminY、surface_centroidは三角形面積重みの中心を原点へ移す。零面積triangleは寄与せず、全て零面積なら失敗
- custom.offsetは軸/scale適用後に加える平行移動量。ソース内のpivot指定ではない
- 演算はdouble。P' = (R×P − Pivot) × Scale + Offset と評価し、巨大な中間平行移動行列を作らない。floatへは最後に一度だけ変換する
- 法線は正の一様scaleで逆転置のscale成分が正規化時に消えるため、直交rotation/mirrorを適用して単位化する。ゼロ/非有限法線は拒否
- UVはflipU/flipVに応じ1−u / 1−v。windingは既存glTF→engineの反転後を基準とし、keepは追加反転なし、flipは常に追加、autoはmirrorX時だけ追加する
- 全layout/index/位置/変換後のfloat表現可能性を検証してから書込passへ進む。失敗時は頂点もindexも非変更。位置の非ゼロ値が乗算またはfloat変換で0へunderflowする場合も拒否する
- 非整列byte storageはmemcpyで読み書きする

実ImportTransformTestは48の軸/鏡像組合せの体積符号、3種類fit、bounds/足元/面積重み原点、custom移動、
法線/UV、layout・index・NaN/inf/overflow/underflowと失敗時非変更、非整列storageと未指定field保持を確認する。
通常・最適化・ASan/UBSan（LeakSanitizer除外）を実行済み。
骨格のIBM/animation等の一様scaleと、実cooker/looseへの挿入は後続。現時点でロード結果はまだ変わらない。

SurfaceCentroidは辺の大きな数同士の減算を避け、float32座標の積をbinary64で保持して外積の6項を誤差展開で加算する。
三角形頂点の演算順も座標順へ正規化し、中心と面積重みmoment・総面積を補償付き和で保持する。
巨大な正負座標と小さい座標の混在、三角形の循環index、複数面の列挙順による重心消失を回帰試験へ追加した。

### 変換の保留範囲

SurfaceCentroidの極端値では面積と三角形中心の途中丸めによる相殺が見つかった。
最新修正は三角形中心へ一度丸めず、各頂点×面積の積とfma残差をmoment展開へ足し、最後に3×総面積で割る。
M=2^100、t=2^-100の等面積2面を含む追加回帰は通常・最適化・sanitizerで成功したが、この最終修正の独立確認はまだ完了していない。
変換APIは未接続のまま保留し、実ロード/cookerへ適用する前に確認を必要とする。
