# BVH生データの解析（G2 / GR84）

## 接続範囲

BvhDecodeは入力byte列をBvhDocumentへ独立所有するpure parserである。ファイルI/O、骨格Resource、回転行列、リターゲット、軸や単位の変換、fps上書き、RootMotion、ループ化、NVSKELとAssetCookの接続は含めない。角度は元のdegree値、OFFSETと位置値は元の単位、Frame Timeは元の秒のまま保持する。

Jointは宣言順、親番号はrootだけUINT32_MAX。名前は厳密UTF8 byte列で、同名は拒否する。名前の正規化・大小文字統合・自動改名をしない。CHANNELSの0〜6個の既知成分を宣言順でglobal配列へ保持し、同一関節内の重複は拒否する。root以外の位置channelと部分回転も捨てない。変換可能性は後続の契約で判定する。

End Siteはjointごとに最大1個のOFFSETとして保存する。関節数やchannel数へ加算せず、End Siteが無い場合に仮の末端を補完しない。

## 入力profile

- HIERARCHY、1ROOT、MOTIONの順。ROOT/JOINTはOFFSETとCHANNELSを各1回、この順で持つ
- キーワードとchannel名はASCII大小文字を区別せず、joint名は区別する
- 名前は非空のsingle token。colonは保持する。空白/brace/二重引用符を含む名前、named End Site、comment、複数ROOT、未知directiveは対象外
- 空白はspace/tab/CR/LF。先頭UTF8 BOMだけ許す。UTF8不正、NUL、その他C0/C1制御文字、途中BOMは拒否する
- Frames: N / Frames : N、Frame Time: dt / Frame Time : dtを許す。Framesは1以上、全channel数も1以上
- MOTIONは非空行1本が1frame。行幅は全channel数と厳密一致し、折返しを数え合わせて救済しない。空行・末尾改行なしは許す
- 最終frame以後はASCII空白だけ。余分なframeや文字はTrailingInput

数値はlocale非依存のdecimal、小数、e/E指数をtoken全体で解析する。浮動値は先頭の+/-を許し、有限doubleだけを保持する。hex、NaN/Inf、overflow/underflow、部分変換を拒否する。Frame Timeは正で、(Frames-1)×Frame Timeも有限であることを要求する。

## 所有と失敗

DecodeBvhは全成功時だけoutを置換する。入力の寿命から独立し、copy/move後も値と名前を保持する。解析失敗と確保例外では以前のoutを保持し、確保例外は伝播する。返却値はstatusと入力byte位置。失敗位置のLine/Columnは1始まり、ColumnはUTF8 byte単位、CRLFは1改行。成功時Line/Columnは0。

## 安全上限

BvhDecodeLimitsの既定は入力64MiB、joint 1024、joint深さ256、frame 1,000,000、double値2^23個、名前512byte、数値token128byte。count/積/byte量を確保前に検査し、階層は明示stackで辿る。呼出側は上限を調整できるが、保存型の32bit上限とsize_tの積の検査は常に行う。NVSKELの128/256関節制限はここへ持ち込まず、骨格へ変換する境界で別に検証する。

参考の[公式Blender importer](https://raw.githubusercontent.com/blender/blender/main/scripts/addons_core/io_anim_bvh/import_bvh.py)でchannelの宣言順、frame行、秒、degree、End Siteを確認した。実装コードは再利用せず、寛容な自動改名や末端補完も採用しない。ライセンス制限付きの実BVHはリポジトリへ含めない。

確保故障の注入は未実施。解析中の確保は局所documentだけに行い、成功時の置換がnothrow moveであることをstatic_assertで固定する。

## 実データの計測と FK の書き出し

### 実データの計測記録

計測時の入力は `Dog#V01full/Dog_Dog_rot6d_pred.bvh`。関節 53、End Site 11、1 フレームの値 162（全値 10854）、フレーム 67、`Frame Time` 0.033333 秒。ヘッダの時間とは別に `override_fps` の `source_fps: 16` を適用し、ループ処理前の clip の `Duration` は `(67 - 1) / 16 = 4.125` 秒だった。

計測記録の `REAL_BVH` 行のキーは次の意味を持つ。誤差・周期・速度は観測値の報告であり、これらの値に閾値を設けた合否判定はしていない。

| キー | 意味 |
|---|---|
| `joints`, `end_sites`, `values_per_frame`, `frames` | 関節数、End Site 数、1 フレームの値の数、入力フレーム数 |
| `frame_time_seconds` | BVH のヘッダに記載された秒数 |
| `source_fps`, `duration_seconds` | 上書きした fps と、ループ処理前の入力 clip の長さ（秒） |
| `rest_error_before_max_degrees`, `rest_error_after_max_degrees`, `rest_error_after_mean_degrees` | rest の対応する骨の方向差。補正前の最大、補正後の最大・平均（度） |
| `rest_error_corrections` | 補正の報告が 0 件のときだけ出る件数。その場合、rest の誤差は未計測の `nan` |
| `rest_direction_missing` | rest の骨の方向を求められなかった対応の数 |
| `rotation_error_measured`, `rotation_error_max_degrees` | 出力姿勢と写像時の double 精度の目標回転との差を計測したか（1/0）と、その最大値（度） |
| `loop_detected`, `period_seconds`, `period_frames` | ループを検出したか（1/0）、検出した周期（秒）、その周期を 16 fps のフレーム数にした値 |
| `seam_before_degrees`, `seam_after_degrees` | 処理前後のループの継ぎ目の回転差（度） |
| `planar_distance_m`, `estimated_speed_m_s` | 処理で求めた平面移動距離（m）と推定の平均速度（m/s） |
| `source_root_distance_m` | 写像の誤差計測で標本化した入力ルートの累積移動距離（m） |
| `unmapped_source`, `unmapped_target` | 対応付けられなかった入力・出力関節の数 |

2026-11-12 の実測（上記の実物を指定して Passed）では、`duration_seconds=4.125`、rest の誤差は `rest_error_before_max_degrees=1.70754729e-06`、`rest_error_after_max_degrees=1.70754729e-06`、`rest_error_after_mean_degrees=3.30799745e-07`、`rest_direction_missing=0` だった。ループは `loop_detected=1`、`period_seconds=0.78102891`、`period_frames=12.4964626`、継ぎ目は処理前 `3.02008852` 度、処理後 `0` 度。`planar_distance_m=0`、`estimated_speed_m_s=0`、`source_root_distance_m=0`、未対応関節は入力・出力とも 0 だった。

この計測の target は BVH の OFFSET から作った恒等写像であり、rest の方向差は別の骨格への写像の補正の精度を示す値ではない。実物のルートの位置は全 67 フレームで一定なので推定速度も 0 になる。出力の回転差は `rotation_error_measured=1`、`rotation_error_max_degrees=15.3357514` と報告された。rest の方向差とは別の観測値であり、受け入れ試験の Passed は回転誤差が小さいことを保証しない。

### FK の書き出しと位置の規約

`AssetCook --bvh <file> --dump-fk <frames>` は指定フレームの関節のワールド位置を JSON として stdout に書く。フレームは 0 始まりの整数をカンマで区切る（例: `0,17,33`）。重複・範囲外は拒否する。`--bvh` は必須で、`--out`・`--manifest`・`--clip-report` と併用すると `role_cli: dump_fk_output_conflict` となる。ファイルへの保存は stdout を使う。

JSON の形は次のとおり（位置は一件だけを示す）。`positions` は指定フレーム順、その中で BVH の関節宣言順に並び、`joint` は関節名、`frame` は BVH のフレーム番号、`x`・`y`・`z` は元の軸・単位での位置を表す。End Site は出さない。

```json
{"format":"norves-bvh-fk","version":1,"producer":"AssetCook","position_convention":"absolute","frames":[0],"positions":[{"joint":"Hips","frame":0,"x":0,"y":0,"z":0}]}
```

`--position-convention absolute|additive` は省略時 `absolute`。`absolute` は Blender の公式 importer と同じく、位置チャンネルが対応する OFFSET 成分を置き換える解釈である。`additive` は role profile の `position_convention: additive` と同じく OFFSET に位置チャンネルを加える。Blender と比べるときは既定の `absolute` のまま使う。上記の計測に用いた role profile は `additive` のため、その clip の位置とこの書き出しを混同しない。
