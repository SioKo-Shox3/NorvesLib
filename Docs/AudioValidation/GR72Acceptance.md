# GR72 論理サウンド層受入れ記録

策定日: 2026-11-04

## 判定

結果: **受入れの閾値について合格**。`AudioPlaybackTest` で確認した範囲の結果を以下に記す。検証の手段の表に示す未試験の条件と、実機の波形・聴感は、この判定に含めない。

## 範囲

- 論理サウンド層は GameThread 専用とし、フェード、同時発音と奪取、重複の抑制、名前で呼ぶバンクを扱う。
- 物理の voice の寿命は既存の `IAudioService`（世代付き `VoiceHandle`・メールボックス・隔離）が持つ。論理層はその上で `SoundInstance`（世代付きの `SoundInstanceHandle`、包絡、物理の `VoiceHandle`、OwnerKey、年齢）を管理する。
- 検証は `FakeAudioBackend` だけをリンクする `AudioPlaybackTest`（`AudioBackendLifetimeTest` の束の中）と、cook の ctest `AssetCookSoundBankSmoke` を対象とする。
- 実機の XAudio2 の鳴り方・聴感（クリック・ループの継ぎ目・フェードの滑らかさ）はこの記録の範囲外とし、後の実機の確かめに回す。
- 3D 定位（GR73）・virtual voice（GR75）・voice のプール（GR109）は範囲外とする。`noise_radius` は値を持つだけとする。

## 定義

### 時間と更新順

SoundSystem の時刻は `Update(dt)` に渡された dt の和だけで進む（壁時計を読まない）。`AudioServiceModule::Tick(dt)` は service の `Tick()`（Drained の取り込み）の後に `SoundSystem::Update(dt)` を呼ぶ。`ModuleRegistry::TickAll` はシミュレーションの停止中も回るので、フェードは停止中も進む。1 フレーム = 1 回の `Update`。

### 音量と可聴度

物理の voice に渡す音量は次の積とする。

`10^((volume_db + 揺らぎ_db)/20) × 再生の音量 × フェードの利得 × 区分の音量 × 全体の音量`

再生の音量は `SoundPlayParams::Volume` とし、`SetVolume` で置き換える。区分は SE（Effect）と環境（Ambience）の 2 つ。全体・区分の音量は既定 1。

奪取の比較に使う可聴度は `10^((volume_db + 揺らぎ_db)/20) × 再生の音量 × フェードの利得` とし、全体と区分の音量は掛けない。

### フェードの包絡

フェードの利得を g(t)、t = 経過 / 長さ ∈ [0,1]、g0 = 開始時の利得とする。

| 曲線 | フェードイン | フェードアウト |
|---|---|---|
| 線形 | g = g0 + (1 − g0)·t | g = g0·(1 − t) |
| 等パワー | g = sqrt(g0² + (1 − g0²)·t) | g = g0·sqrt(1 − t) |

等パワーでは、どちらも g² が t に線形となる。途中で向きを変える（フェードイン中の Stop）ときは、そのときの g を g0 として新しい包絡を始める。

最短のフェードは 2 フレーム = 2/60 秒（約 33.3 ms）。0 < 指定 < 2/60 秒はこの値に引き上げ、引き上げた回数・指定値・適用値を診断に出す。フェードインの 0 は「フェード無し（最初から利得 1）」。停止（Stop・StopOwner）のフェードは常に 2/60 秒以上で、0 の指定も引き上げて数える。負の指定は定義の既定（`fade_in_ms` / `fade_out_ms`）を使う。

### 停止と自動の破棄

フェードアウトの利得が 0 に達した `Update` で `StopVoice` をちょうど 1 回呼ぶ。voice が Drained になった後の `Update` で `DestroyVoice` を呼び、インスタンスを解放する。ワンショットが鳴り終えて Drained になったときも同じ自動の破棄とする。

### 同時発音と奪取

全体の上限は既定 32（仮の値）とし、定義ごとに `max_instances`（0 は無制限）を持つ。フェードアウト中のインスタンスも数に入る。

- 定義ごとの超過: 方針 `StopOldest` はその定義の最も古いインスタンスを奪い、`RejectNew` は新規を `Rejected` で断る。
- 全体の超過: 奪う候補を（優先度の昇順, 可聴度の昇順, 古さ = 再生の通し番号の昇順）で並べ、新規も同じ比較に入れる（新規は最も新しい）。最下位が既存なら奪い、最下位が新規なら `Rejected` を返して `RejectedPlayCount` を 1 増やす。
- 奪うときは同じ呼び出しの中で既存の voice を `IAudioService::AbortVoice` で止めて破棄してから新しい voice を作る。したがって fake の（Create − Destroy）は上限を超えない。奪った回数は `StolenVoiceCount`。奪われたインスタンスの handle は以後 `StaleHandle`。

### 重複の抑制

- cooldown: 同じ定義の直前に鳴った時刻から `cooldown_ms` 未満の要求は鳴らさず `SuppressedByCooldownCount` を 1 増やす。
- dedupe: `dedupe_radius > 0` の定義だけ有効。直前に鳴った同じ定義の再生から `dedupe_window_ms` 以内（0 は同じフレームだけ）で、位置の距離が `dedupe_radius` 以下（`PlayGlobal` 同士は距離 0 とみなす）の要求は鳴らさず、dedupe の抑制数を 1 増やす。同じ clip の重ね鳴りは約 +6 dB になるのを避けるため。
- 抑制した要求の戻り値は `Rejected`（handle は無効のまま）。

### バリエーション

clips が 2 つ以上なら、直前に選んだ clip を除いた中から一様に選ぶ（no-repeat）。乱数は注入した `Random::Generator`（固定のシードで決定的）を使う。音量の揺らぎは [−volume_jitter_db, +volume_jitter_db]、ピッチの揺らぎは [−pitch_jitter_semitones, +pitch_jitter_semitones] の一様とする。

### バンク

`Assets/Audio/Banks/*.soundbank.json` を kind=raw で cook し、`AssetSystem::ResolveAsset` で解決して `JsonDocument::TryParse` で読む。clip は論理パスを `ResolveAsset(path, AssetKind::Audio)` → `ParseCookedAudio` → `TryCreateSubBlob` → `AudioClipResource` で解決する。

不正な定義（存在しない clip・spatial=3d かつ stereo の clip・未知のキー）は定義ごとに拒否して診断に出し、ほかの定義は読み込む。

## 受入れの閾値

| 項目 | 条件 | 閾値 | 結果 |
|---|---|---|---|
| fade-out 2.0 秒の到達 | dt = 1/60 で Update | 利得が 0 になるのは 2.0 秒 ± 1 フレーム（120 ± 1 回目の Update） | 合格: 120 回目 |
| fade-out の単調性 | 各 Update の後の音量 | 前の値以下（単調に増えない） | 合格: 122 回の更新で前値以下 |
| fade-out の停止 | 同上 | `StopVoice` がちょうど 1 回。Drained の後に `DestroyVoice`、`ActiveVoiceCount` が 0 | 合格: 停止 1 回、Drained 後に破棄 1 回、残り 0 |
| fade-in 1.0 秒（線形） | dt = 1/60 で 60 回 | 終値 1.0 ± 1e-4、各ステップの増分 ≤ 1/60 + 1e-4、1 を超えない | 合格: 終値 1.0、60 点の増分と上限を満たす |
| fade-in 1.0 秒（等パワー） | 同上 | 音量² が時間に線形（各点で ± 1e-3） | 合格: 60 点すべてで誤差 1e-3 以下 |
| 最短フェード | 0 < 指定 < 2/60 秒 | 適用値 = 2/60 秒、診断にクランプの回数と値 | 合格: 停止の指定 0.01 秒に対し約 0.033333 秒、回数 +1 |
| 奪取の順 | 上限 4、優先度 [10,50,50,90,30,200] を同じフレームで | 奪われるのは 10 → 30 の順、生存は {50,50,90,200}、fake の（Create − Destroy）が常に 4 以下 | 合格: 10 → 30、生存 {50,50,90,200}、最大 4 |
| 最下位の新規 | 上限が満ちた後に最下位の優先度を要求 | `Rejected`、`RejectedPlayCount` が 1 増える | 合格: `Rejected`、カウント +1 |
| cooldown | cooldown_ms = 60、16.7 ms 間隔で 10 回 | 鳴る回数 = 60 ms 以上空いた回数（3 回）、`SuppressedByCooldownCount` = 残り（7） | 合格: 再生 3 回、抑制 7 回 |
| max_instances = 1 | StopOldest / RejectNew | StopOldest は古い方が奪われ新しい方が鳴る、RejectNew は新規が `Rejected` | 合格: 古い方を奪取 / 新規を拒否 |
| dedupe | 同じフレームに半径内 2 つと半径外 1 つ | 鳴るのは 2、dedupe の抑制が 1 | 合格: 再生 2 回、抑制 1 回 |
| no-repeat | 3 clip、固定のシードで 10,000 回 | 同じ clip が連続しない、各 clip の出現数が 10000/3 の ± 5 %（相対） | 合格: 連続なし、出現数 3346 / 3341 / 3313 |
| 世代の折り返し | UINT32_MAX の世代を使い切ったスロットを解放し、次を再生する | 0 を飛ばし、古い handle は `StaleHandle` | 合格: 使い切ったスロットは再利用されず、世代 0 は使われず、古い handle は `StaleHandle` |
| フェード中の Shutdown | フェードアウト中に Shutdown | `Success`、fake の操作の末尾が Shutdown | 合格: `Success`、操作の末尾は Shutdown |
| バンクの拒否 | 存在しない clip・3d かつ stereo・未知のキー | その定義だけ拒否して診断に理由、ほかの定義は読める | 合格: 読込 1・拒否 3、各理由と定義名が各 1 件。正常な定義のみ再生でき、解除後は再生不可 |

## 検証の手段

| 対象 | 手段と観測する値 | 結果 |
|---|---|---|
| 包絡と停止 | `AudioPlaybackTest` は基礎・再生・全体・区分の音量を 1、揺らぎを 0 とし、線形 fade-out の到達・単調性、線形 fade-in の各値・増分、等パワー fade-in の音量²を確認する。停止回数、Drained 後の破棄、残りの voice 数も確認する。線形 fade-out の全点の式と等パワー fade-out の式は試験していない | 合格: fade-out は 120 回目に到達、線形 fade-in は 60 回目に 1.0、停止と破棄は各 1 回 |
| 時間の契約 | `AudioPlaybackTest` は fade-out を dt = 1/60 と 1/30 で進め、同じ経過時間の 3 点を比較する。`AudioServiceModule::Tick` の呼び出し順とシミュレーション停止中の更新は試験していない | 合格: 1/60 では 120 回目、1/30 では 60 回目に到達し、3 点の差は 1e-4 以下 |
| フェードの切り替えと最短値 | `AudioPlaybackTest` は停止に 0.01 秒を指定し、クランプの回数・指定値・適用値と 2 フレームでの停止を確認する。フェードイン中の Stop による切り替え、fade-in の短い指定と 0、停止の 0、負の指定と既定値は試験していない | 合格: クランプ +1、0.01 秒から約 0.033333 秒へ、2 フレームで停止 |
| 音量と可聴度 | 定義の dB・揺らぎ・再生音量・`SetVolume`・包絡・区分音量・全体音量を含む積の式と、可聴度の比較から全体・区分音量を除く条件は試験していない | 未試験 |
| 同時発音と奪取 | `AudioPlaybackTest` は優先度による奪取と新規の拒否、定義ごとの上限、戻り値・生存・診断・奪われた handle、fake の操作列の各時点の Create − Destroy を確認する。優先度が同じ場合の可聴度と古さによる順序、およびフェードアウト中の上限への算入は試験していない | 合格: 奪取 2 回、上限 4、最下位の新規は拒否 |
| 重複の抑制 | `AudioPlaybackTest` は cooldown の 10 要求と、同じフレーム内の半径内・半径外および次フレームの `PlayAt` を確認する。時刻と距離の境界値、`PlayGlobal` 同士は試験していない | 合格: cooldown は再生 3・抑制 7、dedupe は同一フレームで再生 2・抑制 1 |
| バリエーション | `AudioPlaybackTest` は固定シードで 10,000 回選び、連続の不一致と各 clip の出現数を確認する。同一シードによる選択列の再現性と音量・ピッチの揺らぎの範囲は試験していない | 合格: 連続なし、出現数 3346 / 3341 / 3313 |
| 世代と終了 | `AudioPlaybackTest` は UINT32_MAX を使い切ったスロットの解放後に、そのスロットを再利用せず、世代 0 を使わず、古い handle が `StaleHandle` になることを確認する。フェードアウト中の Shutdown の戻り値と fake の操作の末尾も確認する | 合格: 古い handle は `StaleHandle`、Shutdown は `Success` |
| バンクの cook と解決 | `AssetCookSoundBankSmoke` は kind=raw の cook と `AssetSystem` による論理パスの解決を確認する。`AudioPlaybackTest` は試験用の解決関数を使い、不正な各定義の拒否理由・名前と正常な定義の登録・再生・解除を確認する。本番の `SoundSystem::LoadBank` を通した clip の解決・デコード経路は試験していない | 合格: `AudioPlaybackTest` は読込 1・拒否 3、`AssetCookSoundBankSmoke` は raw の cook と解決に成功 |

## 負の対照

次の誤りを個別に加えた場合に、対応する受入れ試験が失敗することを確認した。

| 手元で当てる変更 | 検出する手段 | 結果 |
|---|---|---|
| 包絡の時間の積分が dt を無視し、毎回固定の量だけ進める | `AudioPlaybackTest` の時間の契約で、異なる dt の列を同じ経過時間まで進めて包絡を比較する。dt = 1/60 だけの測定では固定量の誤りを見逃すため、dt の分割を変えた比較でも失敗を確認する | `TestFadeOutDurationAndDrainedLifetime` の異なる dt による音量比較が失敗。復元後は合格 |
| 奪取の比較の向きを逆にする | `AudioPlaybackTest` の上限 4・優先度 [10,50,50,90,30,200] で、奪取順 10 → 30、生存 {50,50,90,200} の判定が失敗することを確認する | `TestVoiceStealingOrderAndStaleEvents` の優先度 10 の handle の失効判定が失敗。復元後は合格 |

## 記録と既知の制限

fake の操作・音量・戻り値・診断による論理的な判定は、実機の波形や聴感の合格を示さない。試験していない条件は検証の手段の表に記す。
