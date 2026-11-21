# 音源から聞き手までの伝搬と遮蔽

## 目的と置き場

AI の聴覚（GR24）と音声モジュール（G16）は、音源から聞き手までの経路を Core の `AcousticTransmissionEvaluator` に問い合わせる。距離、壁、草、地形による減衰と、雨や風が音の高域を覆う効果を同じ経路について求める。これにより、プレイヤーに聞こえる音と AI が気づく音の判定が同じ伝搬条件に基づき、音声出力装置がない環境でも同じ結果を得られる。

公開型は `Library/Core/Public/Scene/AcousticTransmission.h` にある。音声ドメイン `NorvesAudioDomain` の依存先は Core だけで、XAudio2 はバックエンドに閉じる。評価器は GameThread 専用で、`Engine::Engine` がメンバとして所有し、利用側は `GetAcoustics()` で参照する。

## 経路の照会と結果

`Evaluate(query, outResult)` に `AcousticPathQuery` を渡す。位置と距離の単位は m、レベル・利得・損失の単位は dB である。

| 入力 | 意味 |
|---|---|
| `Source` | 音源のワールド座標。 |
| `Listener` | 聞き手のワールド座標。 |
| `SoundClass` | 距離・高域・免除の設定を選ぶ分類。既定は `Voice`。 |
| `SourceLevelDb` | クラスの基準距離での音源レベル。騒音床と同じ尺度で、既定は 60 dB。 |
| `Filter` | 静的幾何に使う `PhysicsQueryFilter`。評価器はトリガーを常に除外する。 |

| 出力（`AcousticPathResult`） | 意味 |
|---|---|
| `DistanceM` | 音源と聞き手の間の 3 次元の直線距離。 |
| `GainDb` | 距離と適用対象の遮蔽を合わせた全帯域の利得。範囲は −120〜0 dB。 |
| `HighFrequencyLossDb` | 遮蔽とマスキングによって高域にだけ追加する損失。0 dB 以上。 |
| `StaticOcclusionDb` | 壁などの静的幾何を通る際の全帯域損失。免除前の値。 |
| `VegetationLossDb` | 草の中を通る区間の全帯域損失。免除前の値。 |
| `TerrainOcclusionDb` | 丘などの地形に遮られた経路の全帯域損失。免除前の値。 |
| `NoiseFloorDb` | 聞き手の位置での高域の騒音床。寄与がなければ −120 dB。 |
| `bLineOfSight` | 見通しを遮る寄与源がなければ真。遮蔽の免除中も経路の状態を示す。 |

戻り値は `EPhysicsSceneQueryResult`。所有スレッド以外からの呼び出しは `WrongThread`、座標成分か音源レベルが非有限、または分類が範囲外なら `InvalidArgument`、評価できれば `Success` を返す。エラー時は出力を既定値に戻す。

## 距離・遮蔽・マスキングの合成順

1. 直線距離とクラスの基準距離から距離利得を dB で求める。
2. `Masking` 以外の寄与源を `Order` の昇順、同順なら登録順に呼ぶ。`GetKind()` に従って全帯域損失を静的幾何・草・地形の各欄に足し、高域だけの追加損失は別に合計する。見通しを遮る寄与が一つでもあれば `bLineOfSight` を偽にする。
3. 3 欄の損失の和を使い、`GainDb = clamp(距離利得 − 遮蔽損失の和, −120, 0)` とする。高域の合計を `HighFrequencyLossDb` に入れる。遮蔽免除なら 3 欄の内訳を保ち、`GainDb` には距離利得だけを、高域の初期値には 0 を使う。
4. `Masking` の寄与源を同じ順序規則で呼ぶ。全員が遮蔽までの同じ途中結果（`AcousticContributionContext::Partial`）を読む。`NoiseFloorDb` は各寄与と初期値 −120 dB の最大値とし、マスキングによる高域損失の和を `HighFrequencyLossDb` に足す。マスキング免除時も騒音床は返すが、この加算は省く。マスキングで `GainDb` は変わらない。

照会、設定、寄与源の登録順と参照するシーンの状態が同じなら、結果はビット単位で一致する。評価時間は `Debug/Stats.h` の `Acoustics.Evaluate` に記録する。Release ではこの計測は空になる。

## 距離利得・可聴半径・カットオフの換算

`ComputeAcousticDistanceGainDb` は G16 の 3D 定位と同じ純関数 `EvaluateAcousticDistanceGain`（`Scene/AcousticDistance.h`）を呼ぶ。`Rolloff = InverseClamped`、`MinDistance` はクラスの基準距離、`MaxDistance` は 1,000,000 m とし、返った振幅利得を `20 × log10(振幅利得)` で dB に換算する。振幅利得が 0.000001 以下なら −120 dB。基準距離の内側は通常 0 dB で、減衰区間のうち最大距離から十分離れた範囲では、距離が 2 倍になると約 −6.02 dB 変化する（強度は約 1/4、振幅は約 1/2）。

GR24 は `HearingRadiusToThresholdDb(soundClass, radiusM)` でクラスを指定できる。内部の `ComputeHearingRadiusThresholdDb` は、遮るもののない地面で可聴半径ちょうどに達したときの距離利得を返す。照会結果が `GainDb >= しきい値` なら聞こえる。例えば基準距離が 1 m、可聴半径が 2 m ならしきい値は約 −6.02 dB。同じ距離でも、途中に壁があれば遮蔽後の利得で判定する。

音声側は `ComputeHighFrequencyCutoffHz` で高域損失をローパスの遮断周波数に換算できる。損失が 0 dB 以下なら `maxHz`、`maxLossDb` 以上なら `minHz`、その間は周波数の対数で補間する。

## 寄与源の登録と寿命

`IAcousticMediumContributor` は、登録中に変わらない種類を `GetKind()` で返し、`Contribute(context, outContribution)` でその経路への寄与を書く。追加の寄与源は `BindContributor(contributor, order)` で借用登録する。`SceneQuery::BindPhysicsProvider` と同じく、登録した側が `UnbindContributor(contributor)` で外すまで生存させる。登録・解除は所有スレッドで行い、`Contribute` の中から登録を変更しない。

登録・解除の成功は `Success`、別スレッドは `WrongThread`。同じ寄与源の二重登録は `AlreadyBound`。解除時に登録簿が空なら `Unavailable`、登録済みの寄与源はあるが対象が違えば `ProviderMismatch` を返す。

## 既定の四つの寄与源

評価器は構築時に次の四つを登録する。`SetSources(AcousticSources{SceneQuery, Surfaces, Vegetation, Terrain, Environment})` でシーンの参照先を借りる。入力がない媒体からは損失を加えない。静的幾何・草・地形は、それぞれの全帯域損失に `occlusionHighFrequencyRatio` を掛けた値を高域の追加損失とする。

### 静的幾何（Order 100）

`SceneQuery::ExecuteQuery` の `RaycastAll` を音源から聞き手まで 1 回行い、トリガーを除いて距離順に当たりを最大 4 枚分取得する（`MaxHits = 4`）。これで当たりの少し先から撃ち直す処理を代え、5 枚目以降は数えない。地形の高さ場に対応する当たり（当たりの高さと地形高の差が 5 cm 以内）は地形側で扱い、壁に数えない。

残った当たりごとに、その表面から親をたどって最初の正の `transmissionLossDb` を加える。見つからなければ 1 枚につき `wallClassFallbackLossDb` を使う。1 枚でも数えれば見通しは遮られる。[表面の語彙と照会の契約](SurfaceContract.md)

### 草（Order 200）

経路の水平投影を 1.5 m 刻みで区切り、区間の中点で `GrassField` の草丈と密度を読む。24 点を超える間は刻みを倍にする。各中点で光線が地面以上、草の先端未満を通る区間だけ、`密度 × 区間長 × vegetationAbsorptionDbPerMeter` を加える。草より上を通る区間の損失は 0 で、草は見通しを遮らない。吸収係数はゲーム内での効き方を調整する仮の値で、実測による物理的な根拠はない。

### 地形（Order 300）

草と同じ刻み・中点で `TerrainSystem::SampleGround` を引き、地面が光線よりどれだけ高いかを求める。地面に入った最大の深さが `terrainOcclusion.minDepthM` を超えたとき、丘の陰として `min(maxLossDb, baseLossDb + 最大の深さ × lossPerMeterDb)` を 1 回加え、見通しを遮る。

### 雨と風によるマスキング（Order 1000）

`Masking` の寄与源は `AcousticNoiseFloorModel` から聞き手の高域の騒音床を得る。モデルは `EnvironmentSystem` の雨量と平均風速をそれぞれの曲線で dB に換算する。雨の分だけを `shelteredReductionDb × 覆いの度合い` だけ下げてから、雨と風のレベルを電力に換算して足し、dB に戻す。

聞き手での声の高域レベルは `SourceLevelDb + highFrequencyLevelOffsetDb + GainDb − HighFrequencyLossDb`。ここでは遮蔽までを合成した利得と高域損失を使う。マスキング免除のない声には、`max(0, 騒音床 − 声の高域レベル) × maskingCoefficient` を高域の追加損失として適用する。

## 雨を遮る覆い

`IRainShelterSource` は聞き手の頭上の覆いを 0（開けた空）から 1（完全に覆われている）で返す。G11 の雨の遮蔽（`G11-GR59-OVERHEAD-MAP`）は GPU の深度マップで、現在は CPU から屋根の下かを照会する手段がない。既定の `OpenSkyRainShelterSource` は常に 0 を返す。CPU の照会が使えるようになれば、`GetNoiseFloorModel().SetShelterSource()` で差し替える。

## 音源の分類と免除

`ESoundClass` の六つの分類は仮のもの。基準距離・高域のレベル差・遮蔽免除・マスキング免除を各クラスで独立に設定する。既定の基準距離は全クラス 1 m、両方の免除は全クラスで無効である。

| クラス | Cue | Ambient | Footstep | Voice | Rain | Creature |
|---|---:|---:|---:|---:|---:|---:|
| 高域のレベル差（dB） | −6 | −10 | −3 | −8 | 0 | −6 |

手がかりの音を壁越しにも届けるかは企画で決め、`classes.Cue.exemptFromOcclusion` で切り替える。雨などにかき消されるかは別に `classes.Cue.exemptFromMasking` で切り替えられる。どちらも距離減衰は保つ。

## 設定ファイル

`Assets/Data/Acoustics.json` のルートはオブジェクトで、最大 256 KiB。表のドットはオブジェクトの階層を表す。すべてのキーを省略でき、読み込みのたびに `AcousticSettings::MakeDefault()` に指定値を重ねる。未知のキーや分類、重複キー、型・範囲の誤りは失敗となる。全項目の検査が通ったときだけ設定を入れ替えるので、失敗時は直前の設定を保つ。調整値はすべて仮である。

| キー | 既定値 | 意味と受け付ける範囲 |
|---|---|---|
| `version` | 1 | 形式の版。整数リテラルの 1 のみ。 |
| `vegetationAbsorptionDbPerMeter` | 0.5 | 密度 1 の草の吸収係数（dB/m）。0 以上。 |
| `wallClassFallbackLossDb` | 12 | 表面から透過損失を取得できない壁 1 枚分の損失（dB）。0 以上。 |
| `occlusionHighFrequencyRatio` | 0.5 | 遮蔽損失に対する高域の追加損失の倍率。0〜4。 |
| `terrainOcclusion.baseLossDb` | 6 | 地形に遮られたときの基礎損失（dB）。0 以上。 |
| `terrainOcclusion.lossPerMeterDb` | 3 | 地面に入った深さ当たりの損失（dB/m）。0 以上。 |
| `terrainOcclusion.maxLossDb` | 24 | 地形損失の上限（dB）。0 以上。 |
| `terrainOcclusion.minDepthM` | 0.1 | 地形を遮蔽と判定する深さのしきい値（m）。0 以上。 |
| `hfLossToCutoff.minHz` | 800 | 最大損失時の遮断周波数（Hz）。正値。 |
| `hfLossToCutoff.maxHz` | 20000 | 損失がないときの遮断周波数（Hz）。`minHz` より大きい正値。 |
| `hfLossToCutoff.maxLossDb` | 30 | 遮断周波数が下限に達する損失（dB）。正値。 |
| `rainNoiseFloor.curve` | `[[0,-120],[0.1,30],[0.5,45],[1,55]]` | 雨量（0〜1）から高域の騒音床（dB）への曲線。 |
| `rainNoiseFloor.shelteredReductionDb` | 12 | 完全に覆われた位置で雨の成分を下げる量（dB）。0 以上。 |
| `windNoiseFloor.curve` | `[[0,-120],[5,25],[20,45]]` | 平均風速（m/s、0 以上）から高域の騒音床（dB）への曲線。 |
| `maskingCoefficient` | 0.5 | 騒音床が声の高域レベルを上回る分に掛ける倍率。0 以上。 |
| `updateHz` | 10 | 声の経路を更新する頻度（Hz）。正値。 |
| `voiceBudgetPerFrame` | 32 | 1 フレームで経路を更新する声の上限。1〜1024 の整数リテラル。 |
| `classes.<分類>.referenceDistanceM` | 1 | 音源レベルを定義する基準距離（m）。正値。 |
| `classes.<分類>.highFrequencyLevelOffsetDb` | 分類別（上表） | 音源レベルに加える高域の差（dB）。正負とも可。 |
| `classes.<分類>.exemptFromOcclusion` | `false` | 遮蔽を利得・高域損失への適用から外すか。真偽値。 |
| `classes.<分類>.exemptFromMasking` | `false` | マスキングを高域損失への適用から外すか。真偽値。 |

小数を受け付ける値はすべて有限の `float` の範囲とし、正値は `float` への変換後も正であること。曲線は 1〜64 個の `[入力, 騒音床dB]` の配列で、入力は `float` への変換後に狭義の昇順とする。騒音床の点は負値も指定できる。点の間は dB を線形補間し、定義域の外では近い端の値を使う。

## 聴覚と音声再生への接続

GR24 は同じ経路の `GainDb` と可聴半径のしきい値を比較する。G16（GR73）は 3D 定位とバスを担当し、遮蔽の入口からこの評価器を呼ぶ。声の音量とローパスを時間をかけて評価値になじませる処理は `G13-GR116-AUDIO-ACOUSTICS` が担い、`updateHz`、`voiceBudgetPerFrame`、`hfLossToCutoff` を使う。[音声の構成と更新順序](AudioFlow.md)
