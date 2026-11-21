# GR73 3D 定位とリスナーの受入れ記録

策定日 2026-11-18

## 判定

**受入れの閾値を固定した。** 確かめはビルドと、作者の聴感（`G16-GR73-SPATIAL-CHECK`）による。このプロジェクトでは試験を作らないため、下の閾値は数値を照合する試験を置かず、式の定義として固定する。

## 範囲

モノラル音源の距離減衰、等パワーのパン、近距離の広がり、背後の減衰、ドップラーを扱う。リスナーの選択、Entity に追従する音源の位置、各係数を voice に渡す時点も対象とする。

休眠 voice（`G16-GR73-VIRTUAL-VOICE`）、バス（`G16-GR73-BUS-MIXER`）、リバーブ（`G16-GR73-REVERB-OCCLUSION`）、voice ごとのフィルタと遮蔽（`G16-GR73-ACOUSTICS-AI-WIRE`）は範囲外とする。

## 定義

### 3D で再生する条件

`PlayAt` は `SoundPlayParams::Spatialization == World3D` または定義の `spatial` が `3d` のときに 3D で再生する。`PlayGlobal` は常に 2D。World3D の `AudioComponent::Play` は持ち主のワールド位置で `PlayAt` を呼ぶ。3D 再生で選んだ clip がステレオなら `Rejected` を返す。バンクの読み込みでも、ステレオの clip を含む `3d` 定義を拒否する。

| `AudioComponent` の PROPERTY | 値・単位 | 既定 |
|---|---|---|
| `Spatialization` | `None`（2D）、`World3D` | `None` |
| `MinDistance` | 距離減衰を始める距離（m） | 1 m |
| `MaxDistance` | 距離利得が 0 になる距離（m） | 50 m |
| `Rolloff` | `InverseClamped`、`Linear`、`LogDb`、`Table` | `InverseClamped` |
| `DopplerScale` | 視線方向に投影した速度の倍率 | 1 |
| `OcclusionMode` | `None`、`Evaluator`。遮蔽への適用は `G16-GR73-ACOUSTICS-AI-WIRE` の範囲 | `None` |

### 距離の減衰

音の定位、G13 の音の評価器（GR116）、AI の聴覚（GR24）は、同じ `Core::Scene::EvaluateAcousticDistanceGain` を呼ぶ契約とする。距離の式を定義する場所は Core のこの関数だけである。

距離を d（m）、Min = max(MinDistance, `kAcousticMinDistanceFloor`)、Max = MaxDistance とする。下限 `kAcousticMinDistanceFloor` は 0.01 m。MinDistance が非有限なら Min は 1 m。負の距離は 0 m とし、NaN と正の無限大の利得は 0 とする。Max が有限なら、まず d ≥ Max で利得を 0 とする。残りは d ≤ Min なら 1、d > Min で Max が非有限なら 0 とする。

有限の Max ≤ Min では d < Max の利得は 1、d ≥ Max の利得は 0 になる。音源設定の検査 `IsValidEmitterSpatialSettings` は MaxDistance > max(MinDistance, 0.01 m) を求めるため、受理した設定では利得は Max で連続的に 0 に達する。Min < d < Max では t = (d − Min)/(Max − Min) とし、次の利得 g を [0, 1] に収める。

| 曲線 | 距離利得 g |
|---|---|
| `InverseClamped` | (Min/d − Min/Max)/(1 − Min/Max) |
| `Linear` | 1 − t |
| `LogDb` | a = clamp(LogDbRange, 1, 120)、f = 10^(−a/20) として (10^(−a·t/20) − f)/(1 − f)。LogDbRange の既定は 60 dB |
| `Table` | (0, 1) から (1, 0) まで、有効な中間点を t の昇順にたどり、隣り合う点を線形補間する。前の点より進んだ t < 1 の点だけを採り、その利得を [0, 前の利得] に収める。中間点がなければ 1 − t |

### パン・近距離の広がり・背後の減衰

`ComputeAudioSpatial` はリスナーから音源への単位方向 n を使う。d < 0.0001 m では n をゼロとする。広がりの係数は s = clamp(d/MinDistance, 0, 1)。ここでは設定値の MinDistance をそのまま使い、0 m のときは s = 1 とする。

- 水平方向の成分を x = n·Right、f = n·Forward、その長さを h = √(x² + f²) とする。h < 1e-6 ならパンは p = 0、それ以外は p = s·clamp(x/h, −1, 1)。x/h は正面から右を正に測った水平の方位角 φ の sin φ に当たる。近くでは s が小さくなり、左右の差が縮まる。
- `ComputeEqualPowerPan` は入力 p を [−1, 1] に収め（NaN は 0）、L = cos((1 + p)π/4)、R = cos((1 − p)π/4) をそれぞれ [0, 1] に収めて返す。端点では (L, R) は p = +1 で厳密に (0, 1)、p = −1 で厳密に (1, 0) となり、全域で L(−p) = R(p) が厳密に成り立つ。
- 左右の出力利得は (g·L, g·R)。距離利得 g はここで一度だけ掛ける。
- 背後の度合いは r = s·max(0, −n·Forward)。voice の音量に掛ける倍率は 10^(−6r/20)、高域の係数は 1 − 0.5r。d ≥ MinDistance の真後ろでは音量が 6 dB 下がり、高域の係数は 0.5 になる。MinDistance の内側では s に応じて両方の効果が弱まる。高域の係数は値を保持し、フィルタへの適用は `G16-GR73-ACOUSTICS-AI-WIRE` の範囲とする。

### ドップラー

`ComputeDopplerRatio` の音速は既定で c = 343 m/s。c が非有限または 0 以下のとき、d < 0.0001 m のとき、または DopplerScale が 0 のときは 1 を返す。音源からリスナーへの単位方向を u とし、投影速度 vs = DopplerScale·(音源速度·u)、vl = DopplerScale·(リスナー速度·u) と、その比 (c − vl)/(c − vs) は倍精度で求める。

- 分母 c − vs が 0 以外で比が正なら、比を [0.5, 2.0] に収める。比が 0 以下なら、分子 c − vl が正のときは 2.0、それ以外は 0.5 を返す。
- 分母が 0 なら、分子も 0 のときは 1.0、正なら 2.0、負なら 0.5 を返す。

### リスナーの決め方

`AudioListenerState` は `Position`、`Right`、`Up`、`Forward`、`Velocity` を持つ。`AudioListenerComponent` の PROPERTY は `Mode` と `Priority` で、型は音声モジュールの `RegisterReflectedTypes` で登録する。複数あるときは最大の Priority を選び、同値なら持ち主の ObjectId が最小のものを選ぶ。Component がなければ MainCamera を使う。

| Mode | 位置 | 向き |
|---|---|---|
| `MainCamera` | メインカメラ | メインカメラ |
| `Entity` | 持ち主の Entity | 持ち主の Entity |
| `Hybrid` | 持ち主の Entity（頭のソケットに付けた子 Entity など） | メインカメラ |

`IAudioListenerSource` がリスナーの取得口となる。MainCamera 版は `Install` で保持した `Core::Engine::Engine*` から `GetRenderWorld().GetRenderingCoordinator().GetMainCamera()` を取得し、`IsValid()` を確かめて読む。`MainCamera` はカメラ、`Entity` は持ち主、`Hybrid` は両方を必要とする。必要な入力が一つでも欠けたら姿勢全体を既定値に戻す。位置は原点、基底は CameraProxy の既定（Forward = (0, 0, −1)、Right = (1, 0, 0)、Up = (0, 1, 0)）、速度は 0 とし、初回だけ `AUDIO_LISTENER fallback=origin reason=<no_camera|entity_missing>` を記録する。通常の速度は位置の差分/dt で求める。選んだリスナーが変わったフレームと、フォールバックに入るフレーム・復帰するフレームの速度は 0 とする。

### フレーム内の更新順と voice への反映

音声モジュールの `Tick(dt)` は通常の `ModuleRegistry::TickAll` で動き、Handler の `OnLateUpdate` が確定した同じフレームのカメラを読む。

1. `IAudioService::Tick()` が voice の通知を取り込む。
2. `AudioListenerResolver::Resolve` でリスナーを解決し、`SoundSystem::SetListener` に渡す。
3. 登録された `AudioComponent` のうち `Spatialization == World3D` のものについて、持ち主の `Entity::GetWorldTransform()` の位置を `ISoundSystem::SetEmitterPosition` で再生中のインスタンスへ渡す。
4. `SoundSystem::Update(dt)` は各 3D インスタンスの音源速度を位置の差分/dt で求め、`ComputeAudioSpatial` の結果を次のように適用する。

| 渡し先 | 渡す値 |
|---|---|
| `IAudioService::SetVoiceOutputGains` | 距離利得を含む左右の線形利得 (g·L, g·R)。3D の voice では `SetVoicePan` を使わない |
| `IAudioService::SetVoiceFrequencyRatio` | ピッチの周波数比 × ドップラー比。サービスは正の有限値を下限 1/1024 と voice 作成時の上限に収める。最終ピッチを pitch（半音）とすると、3D の voice 作成時の上限は min(max(12, pitch) + 12, 120) 半音（`kAudioMaxVoicePitchSemitones` = 120）。pitch > 120 の再生は既存の voice を奪う前に `InvalidState` で拒否する |
| voice の音量 | 10^((volume_db + 揺らぎ_db)/20) × 再生の音量 × フェード利得 × 区分の音量 × 全体の音量 × 背後の音量倍率 |

3D の左右の利得は、両方とも前回の適用値との差が ε = 1e-4 以下なら更新を省く。3D の周波数比は `SetPitch` による変更も含めて同じ ε を使う。3D の voice の音量も差が ε 以下なら更新を省くが、非ゼロから厳密に 0 へ変わるときは送る。2D の voice の音量は前回の適用値と完全に一致するときだけ省く。

## 受入れの閾値

基底は CameraProxy の既定とし、L、R は距離利得を掛ける前のパン係数とする。

| 項目 | 条件 | 閾値 |
|---|---|---|
| 正面 | 音源が Forward 方向 | L = R = 0.7071（±1e-4） |
| 等パワー | 水平面を 1° 刻みで全周 | L² + R² = 1（±1e-4） |
| 左右と鏡像 | 正面から右・左へ同じ角度 φ | 右 90° で R > L、左 90° で L > R。式は厳密に鏡像で L(+φ) = R(−φ)、R(+φ) = L(−φ) |
| 距離の端点と滑らかさ | 4 曲線とも Min = 2 m、Max = 50 m、0.1 m 刻み。LogDb は既定の 60 dB、Table は既定の中間点なし | gain(Min) = 1、gain(Max) = 0。単調非増加で、隣接する利得の差は 0.05 以下 |
| 回転の滑らかさ | 音源を 5 m に置き、水平面を 0.5° 刻みで 360° 回転 | 左右それぞれの 1 ステップの変化は 0.02 以下 |
| ドップラーの符号と量 | リスナーは静止、音源は 20 m/s で接近、c = 343 m/s、DopplerScale = 1 | 343/(343 − 20) = 1.0619（±1e-3） |
| ドップラーの上下限と無効化 | 接近・離脱の速度を変える | 比を [0.5, 2.0] に収め、DopplerScale = 0 で 1.0 |
| 距離利得の一致 | 同じ曲線・設定・標本距離 | 音の定位は `Core::Scene::EvaluateAcousticDistanceGain` を呼び、`DistanceGain` は各標本でその戻り値と一致 |

距離の標本で Min を 2 m とするのは、`InverseClamped` の既定の Min = 1 m、Max = 50 m では、1.0 m から 1.1 m の間に利得が約 0.093 下がるためである。

## 作者が聴くときの見どころ

`G16-GR73-SPATIAL-CHECK` では、World3D の `AudioComponent` を付けた Entity をカメラの周りで動かす。左右を横切ると音像が移り、同じ距離のまま背後に回ると音量が下がることを聴く。遠近による音量差と、接近時に音程が上がり、離脱時に下がる変化も聴き分ける。
