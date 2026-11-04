# GR72 論理サウンド層受入れ記録

策定日: 2026-11-04

## 判定

結果: **未測定**。`NorvesAudioDomain` に新設する `SoundSystem` の受入れ条件として、以下の範囲・定義・閾値・検証の手段を固定する。実装および動作の合格を示す記録ではない。

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
| fade-out 2.0 秒の到達 | dt = 1/60 で Update | 利得が 0 になるのは 2.0 秒 ± 1 フレーム（120 ± 1 回目の Update） | 未測定 |
| fade-out の単調性 | 各 Update の後の音量 | 前の値以下（単調に増えない） | 未測定 |
| fade-out の停止 | 同上 | `StopVoice` がちょうど 1 回。Drained の後に `DestroyVoice`、`ActiveVoiceCount` が 0 | 未測定 |
| fade-in 1.0 秒（線形） | dt = 1/60 で 60 回 | 終値 1.0 ± 1e-4、各ステップの増分 ≤ 1/60 + 1e-4、1 を超えない | 未測定 |
| fade-in 1.0 秒（等パワー） | 同上 | 音量² が時間に線形（各点で ± 1e-3） | 未測定 |
| 最短フェード | 0 < 指定 < 2/60 秒 | 適用値 = 2/60 秒、診断にクランプの回数と値 | 未測定 |
| 奪取の順 | 上限 4、優先度 [10,50,50,90,30,200] を同じフレームで | 奪われるのは 10 → 30 の順、生存は {50,50,90,200}、fake の（Create − Destroy）が常に 4 以下 | 未測定 |
| 最下位の新規 | 上限が満ちた後に最下位の優先度を要求 | `Rejected`、`RejectedPlayCount` が 1 増える | 未測定 |
| cooldown | cooldown_ms = 60、16.7 ms 間隔で 10 回 | 鳴る回数 = 60 ms 以上空いた回数（3 回）、`SuppressedByCooldownCount` = 残り（7） | 未測定 |
| max_instances = 1 | StopOldest / RejectNew | StopOldest は古い方が奪われ新しい方が鳴る、RejectNew は新規が `Rejected` | 未測定 |
| dedupe | 同じフレームに半径内 2 つと半径外 1 つ | 鳴るのは 2、dedupe の抑制が 1 | 未測定 |
| no-repeat | 3 clip、固定のシードで 10,000 回 | 同じ clip が連続しない、各 clip の出現数が 10000/3 の ± 5 %（相対） | 未測定 |
| 世代の折り返し | `SoundInstanceHandle` の世代が UINT32_MAX から戻る | 0 を飛ばし、古い handle は `StaleHandle` | 未測定 |
| フェード中の Shutdown | フェードアウト中に Shutdown | `Success`、fake の操作の末尾が Shutdown | 未測定 |
| バンクの拒否 | 存在しない clip・3d かつ stereo・未知のキー | その定義だけ拒否して診断に理由、ほかの定義は読める | 未測定 |

## 検証の手段

| 対象 | 手段と観測する値 | 結果 |
|---|---|---|
| 包絡と停止 | `AudioPlaybackTest` で dt を与え、各 `Update` の後に fake へ渡された音量を記録する。基礎の音量・再生の音量・全体と区分の音量を 1、揺らぎを 0 として利得を観測し、線形・等パワーの式と到達・単調性・増分を照合する。`StopVoice` の回数、Drained の取り込みと `DestroyVoice` の順、`ActiveVoiceCount` を照合する | 未測定 |
| 時間の契約 | `AudioPlaybackTest` で同じ経過時間を異なる dt の列に分け、包絡が dt の和で進むことを照合する。service の `Tick()` → `SoundSystem::Update(dt)` の順序と、シミュレーション停止中の更新を確認する | 未測定 |
| フェードの切り替えと最短値 | `AudioPlaybackTest` でフェードイン中に Stop し、切り替え時の利得が新しい g0 になることを照合する。短い正の指定、フェードインの 0、停止の 0、負の指定と既定値を照合し、クランプの回数・指定値・適用値を診断で確認する | 未測定 |
| 音量と可聴度 | `AudioPlaybackTest` で定義の dB、揺らぎ、`SoundPlayParams::Volume`、`SetVolume`、包絡、Effect・Ambience、全体の音量を積の式に照合する。可聴度の比較に全体・区分の音量が入らないことを確認する | 未測定 |
| 同時発音と奪取 | `AudioPlaybackTest` で戻り値・生存するインスタンス・`StolenVoiceCount`・`RejectedPlayCount`・奪われた handle の `StaleHandle` を照合する。優先度が同じ場合の可聴度・再生の通し番号による順序と、フェードアウト中も上限に数えることを確認する。fake の操作列の各時点で Create − Destroy を数え、AbortVoice による破棄が新規作成より先であることを確認する | 未測定 |
| 重複の抑制 | `AudioPlaybackTest` で cooldown と dedupe を個別に有効にし、鳴った回数・抑制数・`Rejected`・無効 handle を照合する。時刻と距離の境界、同じフレーム、`PlayGlobal` 同士も確認する | 未測定 |
| バリエーション | `AudioPlaybackTest` で固定のシードを注入し、clip の選択列の決定性、連続の不一致、出現数を照合する。音量とピッチの揺らぎが定義の範囲内であることを確認する | 未測定 |
| 世代と終了 | `AudioPlaybackTest` で世代の UINT32_MAX からの折り返し、0 の回避、古い handle の `StaleHandle` を照合する。フェードアウト中の Shutdown の戻り値と fake の操作の末尾を確認する | 未測定 |
| バンクの cook と解決 | `AssetCookSoundBankSmoke` で kind=raw の cook と論理パスの解決を確認する。`AudioPlaybackTest` で JSON の読み込みと clip の解決経路、不正な各定義の拒否理由、同じバンクの正常な定義の読み込みを確認する | 未測定 |

## 負の対照

通常の実装が閾値内であることに加え、次の変更を手元で個別に当てると、対応する受入れ試験が落ちることを確かめる。負の対照の変更はコミットせず、確認後に戻す。試験の結果と失敗した判定、変更を戻した後の結果は `PROGRESS.md` に記録する。

| 手元で当てる変更 | 検出する手段 | 結果 |
|---|---|---|
| 包絡の時間の積分が dt を無視し、毎回固定の量だけ進める | `AudioPlaybackTest` の時間の契約で、異なる dt の列を同じ経過時間まで進めて包絡を比較する。dt = 1/60 だけの測定では固定量の誤りを見逃すため、dt の分割を変えた比較でも失敗を確認する | 未測定 |
| 奪取の比較の向きを逆にする | `AudioPlaybackTest` の上限 4・優先度 [10,50,50,90,30,200] で、奪取順 10 → 30、生存 {50,50,90,200} の判定が失敗することを確認する | 未測定 |

## 記録と既知の制限

受入れ試験と負の対照の結果は未測定。fake の操作・音量・戻り値・診断による論理的な判定は、実機の波形や聴感の合格を示さない。
