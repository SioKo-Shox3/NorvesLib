# 音声の構成と更新順序

音声は、バックエンドに依存しない論理サウンド層と voice の寿命管理を、
実際の出力に接続するアダプタに分けている。

## 層の構成

```text
Game / AudioComponent / AudioGameModeOwner
          ↓
ISoundSystem / SoundSystem ← SoundBank ← AudioClipLoader
          ├──→ AudioMixerDomain ← IAudioMixer ← IAudioModule::GetMixer()
          │          │
          ↓          ↓
         IAudioService
          ↓
IAudioBackend → XAudio2Backend または NullAudioBackend
```

| 層・要素 | 役割 |
|---|---|
| `NorvesAudioDomain` | バックエンド非依存の静的ライブラリ。`Library/Modules/Audio/CMakeLists.txt` に構成がある。 |
| `IAudioMixer` / `AudioMixerDomain` | 音声モジュールが所有するミキサーの公開窓口と実装。バスの構成・音量・ダッキングの状態を保持し、サービスに反映する。 |
| `IAudioService` | voice の生成・再生・停止・破棄、世代付きハンドル、PCM の保持とイベントを管理する。 |
| `ISoundSystem` / `SoundSystem` | 音の定義とバンク、フェード、同時発音の制限と奪取、cooldown・dedupe、持ち主のキーを扱う。 |
| `SoundBank` | JSON のバンクを解析し、音の定義とクリップの参照を検証する。 |
| `AudioClipLoader` | 解決された cook 済みの nvaud を読み、cook 資産を使わない解決が成功した場合はばらの PCM16 WAV を読む。 |
| `NullAudioBackend` | 音を出さずに voice を扱う。ワンショットの開始時とループの停止時に終端通知を発行する。 |
| `AudioComponent` | Entity に属する音の再生と持ち主の寿命を結び付ける。 |
| `AudioGameModeOwner` | GameMode を包み、退場後にそのモードのキーに属する音を停止する。 |
| `NorvesAudioBackend_XAudio2` | XAudio2 を使って voice を実出力するプラットフォーム側のアダプタ。 |
| `NorvesModule_Audio` | XAudio2 バックエンドとドメインをつなぎ、音声モジュールを登録する。 |

ばらの WAV を正常に読んだときは `AUDIO_COOKED_MISSING` を出す。
PCM16 の対応形式は 44.1 / 48 kHz、モノラル / ステレオ。

## Game の既定の組み立て

音声を有効にした通常起動では、`Game/GameApplicationHandler.cpp` の
`OnPreInitialize` が `AudioModuleOptions::bAllowNullBackendFallback` を有効にして登録する。
XAudio2 の生成または初期化に失敗すると `NullAudioBackend` へ切り替え、
`AUDIO_BACKEND fallback=null reason=...` を記録する。
`IAudioModule::GetBackendStatus()` から種類・フォールバックの有無・理由を取得できる。

`--m9-world-smoke` と `--audio-smoke` はフォールバックを許可しない。
`--audio-smoke` は雨と足音を鳴らし、終了時に次の印を出す。

```text
AUDIO_SMOKE stage=shutdown voices=0 callbacks=0 shutdown_complete=1
```

通常起動の `OnPostInitialize` は音声モジュールがあれば既定のバンク
`Assets/Audio/Banks/default.soundbank.json` を論理パス
`Audio/Banks/default.soundbank.json` で読み、結果を `AUDIO_BANK ...` に記録する。
読み込みは同期で行い、不正な定義については診断を返す。

既定バンクの `Ambience.RainLoop` と `Foley.Footstep` が参照する WAV は、
`Scripts/GenerateProvisionalAudio.py` で生成した `Assets/Audio/Provisional/` の仮の合成音である。
各定義の `source_ref` に「仮・合成」と記録されている。
音を差し替えるときは、バンクの `clips` と出所を表す `source_ref` を設定する。

## バスとミキサー

バス構成と音量の正本は、音声モジュールが所有する `AudioMixerDomain` にある。GameThread からは `IAudioModule::GetMixer()` でそのモジュールの `IAudioMixer` を参照する。バスの親子関係はコードで固定し、`Se`、`Ambient`、`Music`、`Ui` の親はすべて `Master` とする。

XAudio2 は各バスに 2 ch・48 kHz の submix voice を作る。source voice は作成時の send で出力バスへ、子バスは Master の submix へ、Master の submix は mastering voice へ送る。`ProcessingStage` は `kAudioBusCount − 深さ` で決まり、深さ 1 の子（段 4）を深さ 0 の Master（段 5）より先に処理する。Null バックエンドはバスの作成状態と音量を保持する。

既存の 4 引数の `IAudioBackend::CreateVoice`（ハンドル、フォーマット、周波数比の上限、mailbox）は仮想関数のままで、既定の実装では送り先を Se に設定して記述構造体版へ渡す。

### voice の送り先と音量

`SoundSystem` は選んだクリップごとに、次の順で最初に決まったバスを `AudioVoiceDesc::OutputBus` に設定する。

1. バンクの音の定義に `bus` があれば、そのバスを使う。値は `master`、`se`、`ambient`、`music`、`ui` のいずれか。
2. NVAUD のカテゴリがあれば、`Sfx` と `Footstep` は Se、`Ambience` は Ambient、`Music` は Music、`Ui` は Ui に送る。
3. どの指定もなければ Se に送る。定義の `category` は送り先に使わない。

同梱の `Ambience.RainLoop` はバンクで `bus: ambient` を指定しているため、クリップのカテゴリに関係なく Ambient に送られ、GR76 の環境音の音量（Ambient のスライダー）で雨音を調整できる。

`GetBusVolume` が返すバス自身の線形ゲインは `10^(volume_db/20) × slider² × ダッキングの線形利得 × ポーズ用の利得`。スライダーは [0, 1] に収め、二乗の写像を仮に使う。ポーズ対象のバスには `SetPauseGain` の値、対象外には 1 を掛ける。`GetEffectiveBusGain` は、そのバスから Master までの各ゲインを一度ずつ掛けた値を返す。

GR76 の設定は `SoundSystem::SetMasterVolume` から Master のスライダーへ、`SetCategoryVolume` から Effect は Se、Ambience は Ambient のスライダーへ反映する。通常のモジュール構成では全体・区分の音量をバスで一度だけ掛ける。ミキサーを結合せずに `SoundSystem` を使う場合だけ、従来どおり voice 側で両方を掛ける。

### ダッキング

`SoundSystem::Update` は毎フレーム、インスタンスの回収と停止処理の後で、活動中の物理 voice を出力バス別に数え、定義の `duck_tag`（任意の空でない文字列）を集め直す。休眠中と物理 voice に停止を発行済みのものは除く。回収されたインスタンスは次の集計に残らない。

規則はトリガのバスに 1 件以上の活動があるか、指定タグが集計に含まれると作動する。活動中は目標を `duck_db` とし、保持時間を毎フレーム `hold_ms` に戻す。活動が消えた後は、保持時間が尽きるまで同じ目標を保つ。フレームの途中で保持時間が尽きたら、そのフレームの残り時間を release に使い、0 dB へ戻し始める。

減衰は dB 上の 1 次系で、1 フレームごとに `現在値 += (目標値 − 現在値) × (1 − exp(−dt/τ))` とする。時間の単位は ms。深くするときは τ = AttackMs / 5、戻すときは τ = ReleaseMs / 5 とし、τ = 0 なら即座に目標値にする。目標が 0 dB のときだけ、減衰の絶対値が 0.001 dB 未満になったところで 0 dB に丸める。同じバスに複数の規則が掛かるときは、そのフレームで最も深い減衰を線形利得に変換する。

`SoundSystem::Shutdown` が呼ぶ `ResetActivity()` とミキサーの `Shutdown()` は、全規則の減衰を 0 dB、残りの保持時間を 0 ms、全バスのダッキングの線形利得を 1 に即座に戻す。

### Mixer.json の書式と読み込み

最初の `SoundSystem::LoadBank` で、バンクと同じ AssetSystem から論理パス `Audio/Mixer.json`（実ファイル `Assets/Audio/Mixer.json`）を一度だけ読む。明示的に `IAudioMixer::LoadConfig` を試行済みなら自動読み込みは省く。UTF-8 の JSON オブジェクトで、`version` は数値の 1 が必須。`buses` と `ducking` は省略できる。リバーブの欄は `G16-GR73-REVERB-OCCLUSION` で追加する。

| 欄 | 値と省略時の扱い |
|---|---|
| `buses` | 小文字のバス名をキーとするオブジェクト。指定のないバス・項目は既定値を使う |
| `volume_db` / `pause` | 各バスの基準音量（−96 ～ +12 dB、既定 0 dB）とポーズ対象かどうか（真偽値）。既定の対象は Se・Ambient・Music |
| `ducking` | 規則の配列。既定は空で、同梱の設定も 0 件。規則の採否と値は聴感で決める |
| `trigger_bus` / `trigger_tag` | 規則ごとにいずれか一方が必須。小文字のバス名、または定義の `duck_tag` に照合する空でない文字列 |
| `target` / `duck_db` | 下げるバスの小文字名と、目標の減衰量（−96 ～ 0 dB）。両方必須 |
| `attack_ms` / `hold_ms` / `release_ms` | 各 0 ～ 60000 の整数。省略時は順に 100、0、500 ms |

ファイルが見つからない場合や、未知のキー・重複・値の範囲外など不正な項目が一つでもある場合は、設定全体に既定値を適用して再生を続ける。診断ごとに `AUDIO_MIXER config=default reason=... detail=...` を記録する。

`ReapplyBackendState()` は Master から順に全バスの作成を試み、続いて全バスの現在の音量を再送する。音声モジュールの初期化は、サービスの初期化後に全バスの作成と音量の反映が成功したときだけ完了する。バスの設定に失敗すると `AUDIO_MIXER bus_setup=failed result=<n>` を記録する。使用中のバックエンドが Null 以外で、切替が許可されていれば、`bus_setup_failed` を理由に Null バックエンドへ切り替え、全バスの作成と音量の反映をやり直す。切替できない場合や再試行にも失敗した場合は、初期化済みのサービスを停止し、モジュールの初期化は失敗を返す。

G19 の GR118 はバックエンド再初期化後に `ReapplyBackendState()` でバスを復元する。voice の作り直しは GR118、ゲームのポーズ状態に応じたポーズ用ゲインとダッキングの駆動は G15 の GR119 が担う。

## 持ち主のキー

`SoundPlayParams::OwnerKey` の 0 は持ち主なしを表す。

- `AudioComponent` は `Initialize` で最上位ビットが 0 の連番を取得し、使用済みのキーは再利用しない。キーを使い切ると再生できない。
- `AudioComponent::Play` は自分のキーを設定して `PlayGlobal` を呼ぶ。`StopAll` は自分のキーの音を停止する。
- `AudioComponent::Finalize` は `ReleaseOwner` を呼ぶ。ループは定義の既定値でフェード停止し、停止要求のないワンショットは持ち主を外して鳴り切る。
- GameMode のキーは `GetGameModeOwnerKey(id)` で求める。最上位ビットは 1、残りはモード ID のハッシュなので Component のキーと重ならない。
- モード内の再生側が `SoundPlayParams::OwnerKey` にそのキーを指定する。`AudioGameModeOwner` は包んだモードの `Leave` 後に `StopOwner` を呼び、ループもワンショットもフェード停止する。

キーの採番と退場処理は `Library/Modules/Audio/Private/Audio/AudioComponent.cpp` と
`Library/Modules/Audio/Private/Audio/AudioGameModeOwner.cpp` にある。

## 初期化・毎フレームの更新・終了

音声の公開操作とモジュールの駆動は GameThread 専用である。
XAudio2 の callback は別スレッドから届きうるが、voice の状態はそこで更新しない。

1. `ModuleRegistry::InstallAll` は、全モジュールに登録順で `Install` と `RegisterReflectedTypes` を行い、その後に登録順で `Initialize` する。音声は `AudioComponent` の型を登録し、サービスを初期化する。
2. `ApplicationProcessor` は `OnPostInitialize` より前に `InstallAll` を呼ぶ。失敗した場合は到達した段階に応じて逆順にロールバックする。
3. 毎フレームの `PostUpdate.ModuleTick` は `ModuleRegistry::TickAll` を呼ぶ。登録は `GameThreadOnly` で、描画用の World 同期とスクリプト後処理の後、`OnPreRender` と描画の前に実行する。
4. この通常の Module Tick はシミュレーションの進行 gate の外にある。音声の `Tick` は `IAudioService::Tick()`、次いで `SoundSystem::Update(deltaSeconds)` を呼ぶ。
5. サービスの `Tick` は callback が各 voice のメールボックスに入れた `Drained` / `VoiceError` を消費して状態を進め、古い世代や終了後の通知を扱う。
6. 続く `SoundSystem::Update` は `Drained` の voice を破棄して論理インスタンスを回収し、残った音のフェードと音量、停止を進める。休眠への移行と復帰もここで行う。時間には上限付きの可変フレーム delta を使う。
7. 終了時は `ModuleRegistry::ShutdownAll` が登録の逆順で終了する。音声モジュールは `SoundSystem` を終了し、ミキサーとサービスの結合を外してからサービスを終了する。サービスは callback を静止させてから voice とバックエンドを破棄する。

呼び出し元は `Library/Core/Private/Module/ModuleRegistry.cpp` と
`Library/Core/Private/Engine/ApplicationProcessor.cpp`、音声の更新は
`Library/Modules/Audio/Private/Audio/AudioServiceModule.cpp`、
イベント処理は `Library/Modules/Audio/Private/Audio/AudioDomain.cpp` にある。
`AudioComponent` 自身は毎フレームの Tick を登録しない。
[Docs/Architecture/TickStages.md の AudioComponent の行](TickStages.md#component-の割り当て表)も参照。

## 現在の再生範囲

`PlayGlobal` と、`Spatialization` が `None`(既定)の `AudioComponent` は 2D で鳴り、位置を使わない。
`PlayAt` の位置は dedupe の判定に使い、3D の条件を満たすとき(次の節)は定位にも使う。3D の音源は持ち主の Entity の位置に毎 Tick 追従する。

## GR73 の 3D 再生

`PlayGlobal` は常に 2D で再生する。`PlayAt` は、`SoundPlayParams::Spatialization == World3D` または定義の `spatial` が `3d` のときに 3D になる。`AudioComponent::Play` は既定の `None` では従来どおり `PlayGlobal` を呼び、`World3D` では持ち主の位置で `PlayAt` を呼ぶ。3D 再生で選んだ clip がステレオなら `Rejected` を返す。バンクの読み込み時も、ステレオの clip を含む `3d` 定義を拒否する。

`AudioComponent` の距離設定は `MinDistance`（既定 1 m）、`MaxDistance`（50 m）、`Rolloff`（`InverseClamped`、`Linear`、`LogDb`、`Table`。既定は `InverseClamped`）。`DopplerScale` の既定は 1。`OcclusionMode` は `None`（既定）または `Evaluator` で、遮蔽の適用は `G16-GR73-ACOUSTICS-AI-WIRE` の範囲とする。

### リスナーの選択

音声モジュールは `RegisterReflectedTypes` で `AudioListenerComponent` の型を登録する。この Component は `Initialize` で音声モジュールに登録し、`Finalize` で解除する。`Priority` が最大のものを選び、同値なら持ち主の ObjectId が最小のものを使う。Component がなければメインカメラを使う。

| `Mode` | リスナーの位置と向き |
|---|---|
| `MainCamera` | メインカメラの位置と向き |
| `Entity` | 持ち主の Entity の位置と向き |
| `Hybrid` | 位置は持ち主の Entity（頭のソケットに付けた子 Entity など）、向きはメインカメラ |

リスナーの取得口は `IAudioListenerSource`。MainCamera 版は `Install` で保持した `Core::Engine::Engine*` から `GetRenderWorld().GetRenderingCoordinator().GetMainCamera()` を取得し、`IsValid()` を確かめて読む。`MainCamera` はカメラ、`Entity` は持ち主、`Hybrid` は両方を必要とする。必要な入力が一つでも欠けたら、`AudioListenerState` 全体を原点、CameraProxy の既定の基底（右 +X、上 +Y、前 −Z）、速度 0 に戻す。初回だけ `AUDIO_LISTENER fallback=origin reason=<no_camera|entity_missing>` を記録する。通常の速度は位置の差分/dt で求め、リスナーの選択が変わったフレームと、フォールバックに入るフレーム・復帰するフレームでは 0 にする。

### 1 フレームの更新

音声モジュールの `Tick(dt)` は通常の `ModuleRegistry::TickAll` で実行する。最終カメラを読むのは Handler の `OnLateUpdate` の後で、同じフレームの位置と向きが反映される。

1. `IAudioService::Tick()` が voice の終了・エラーの通知を取り込む。
2. `AudioListenerResolver::Resolve` でリスナーを決め、`SoundSystem::SetListener` で渡す。
3. 登録された `AudioComponent` のうち `Spatialization == World3D` のものは、`ISoundSystem::IsAudible` が真のインスタンスへ、持ち主の `Entity::GetWorldTransform()` の位置を `ISoundSystem::SetEmitterPosition` で渡す。有効なハンドルでは物理 voice に停止を発行するまで `IsAudible` が真なので、フェードアウト中も音源は持ち主に追従する。休眠中も `IsAudible` は真で、インスタンスは復帰の判定に使う位置を受け取り続ける。
4. `SoundSystem::Update(dt)` が各 3D インスタンスの音源速度を位置の差分/dt で求める。`ComputeAudioSpatial` にリスナーと音源の状態を渡し、下表の値を反映する。左右の利得は両方とも前回の適用値との差が ε = 1e-4 以下なら更新を省く。3D の周波数比は再生開始時から丸めて送り、送信に成功した比を保存する。毎フレームの更新と `SetPitch` では丸めた比をこの保存値と同じ ε で比較する。3D の voice の音量にも同じ ε を使い、音量が非ゼロから厳密に 0 になるときは送る。2D の voice の音量は完全一致のときだけ更新を省く。

| 係数 | 反映先 |
|---|---|
| 距離利得 × 等パワーの左右のパン係数 | `IAudioService::SetVoiceOutputGains` に左右の線形利得を渡す。近距離ほどパンは中央に寄る。3D の voice に `SetVoicePan` は使わない |
| ピッチの周波数比 × ドップラー比 | `SoundSystem` が 1/1024（`kAudioMinVoiceFrequencyRatio`）から voice 作成時の上限までに丸め、`IAudioService::SetVoiceFrequencyRatio` に渡す。有限の低いピッチで比が 0 になっても下限で鳴る。最終ピッチを pitch（半音）とすると、3D の voice 作成時の上限は min(max(12, pitch) + 12, 120) 半音（`kAudioMaxVoicePitchSemitones` = 120）。pitch > 120 の再生は既存の voice を奪う前に `InvalidState` で拒否する |
| 背後の音量倍率 | 既存の voice 音量（10^((volume_db + 揺らぎ_db)/20) × 再生の音量 × フェード利得）に一度だけ掛ける |
| 背後の高域の係数 | 値を保持する。フィルタへの適用は `G16-GR73-ACOUSTICS-AI-WIRE` の範囲 |

真後ろでの音量減衰は 6 dB、高域の係数は 0.5。`MinDistance` の内側では、近距離の広がりと同じ係数 s = d/MinDistance で背後の効果も弱まる。式と受入れ条件は [GR73 3D 定位とリスナーの受入れ記録](../AudioValidation/GR73Acceptance.md)を参照。

### 休眠(virtual voice)

1 voice に渡せる PCM は 2 GiB（`kAudioMaxVoicePcmBytes`、XAudio2 の単一バッファの上限）までとする。上限を超えると `IsPlayableVoiceDesc` の検査により開始時の距離にかかわらず `InvalidState` を返し、休眠のハンドルも発行しない。
休眠中も論理インスタンスと `SoundInstanceHandle` は有効である。`SoundSystem::Instance` は定義から写した値、選んだ clip（`TSharedPtr<AudioClipResource> Clip`）、音量・ピッチ・位置を保持する。物理の voice は手放し、`Voice` は無効の値になる。
再生開始時に PCM の所有権を共有し、形式とフレーム数を控える。元の clip が `Unload()` された後も、この控えで再生位置を進め、復帰時には同じ PCM から clip を作り直す。

| `SoundSystem::Instance` の欄 | 意味と更新条件 |
|---|---|
| `bVirtual` | 現在休眠中なら真。距離と呼び出し側のどちらか一方でも理由があれば休眠を望み、両方が外れたら復帰を試みる |
| `bVirtualByDistance` | 3D のみ。`Update` の空間計算で得た距離 d（`AudioSpatialResult::Distance`）が MaxDistance × 1.05 を超えたら立て、d ≤ MaxDistance で外す。その間の帯では前の値を保ち、距離による切替は交差ごとに最大 1 回とする。倍率 1.05 は `SoundSystem.cpp` の定数 `kSoundVirtualEnterDistanceScale` の仮の値 |
| `bVirtualRequested` | `ISoundSystem::SetInstanceAudible(SoundInstanceHandle handle, bool bAudible)` の `false` で立て、`true` で外す。物理の voice の切替は次の `Update` で行う |

`SetInstanceAudible` は古いハンドルに `StaleHandle`、停止要求後に `InvalidState`、呼び出し側の指定が今と同じなら `AlreadyInState`、変更を受け付けたら `Success` を返す。GR75 は −55 dB 未満で `false`、−52 dB を超えたら `true` を渡す。再生中は休眠しても `IsPlaying` と `IsAudible` は真である。

`Update` は休眠を望む物理の voice に停止要求がなければ、`IAudioService::AbortVoice` でバックエンドの `StopVoice` と `DestroyVoice` を同期で順に行う。停止済みの voice の破棄やバッファの解放に失敗した場合はサービスが隔離する。破棄済みまたは隔離済みなら、インスタンスは `Voice` を空にして `bVirtual` を立てる。停止自体に失敗して voice がまだ鳴っている場合は、物理の voice を保持し、次の `Update` で休眠を再び試す。停止要求を受けてフェードアウト中の音は、そのまま停止まで進める。`PlayAt` の時点で d > MaxDistance × 1.05 の 3D 音源は、物理の voice を作らず休眠状態で開始する。

再生位置 `PlayFrame` は clip のフレーム単位の `double` である。毎 `Update` で dt（秒）× サンプルレート × 周波数比だけ進める。周波数比は 2D ではピッチの比、3D ではピッチの比 × ドップラー比を、いずれも [1/1024, voice 作成時の上限] に収めた値とする。物理の voice がある間も同じ時計で再生位置を近似する。

ループは `LoopEnd` に達したら [LoopBegin, LoopEnd) に折り返す。`LoopLength` が 0 なら `LoopEnd` は clip の末尾である。休眠中のワンショットは末尾に達した `Update` で完了として回収し、ハンドルは古くなる。休眠中もフェード利得は進める。音量・ピッチ・位置の変更は値だけを保持し、サービスは呼ばない。

休眠中に `Stop`、`StopOwner`、ループに対する `ReleaseOwner`、定義の登録解除、バンクの解放で停止要求を受けたインスタンスは、次の `Update` で voice を作らずに回収する。`Shutdown` は休眠中を含めてすべてのインスタンスを回収する。

復帰には、物理の voice の数（`ActiveInstanceCount − VirtualVoiceCount`）が `MaxConcurrentVoices` 未満で、同じ定義の物理の voice の数も `MaxInstances` 未満であることを要する。`MaxInstances` が 0、または定義が登録解除済みなら定義ごとの制限はない。休眠中のインスタンスはどちらの上限にも数えず、定義の `StopOldest` と優先度による奪取の対象からも外す。復帰のためにほかの voice は奪わず、空きがなければ休眠のまま次の `Update` で再び試す。

`StartInstanceVoice(instance, 再開の位置)` は、位置を渡して物理の voice を作り直す口である。再開の位置は `floor(PlayFrame)` をループなら `LoopEnd` の手前、ワンショットなら clip の末尾の手前に収めたフレームとする。これを `AudioVoiceDesc::StartFrame` に指定して voice を作り、現在の音量と、3D では左右の利得と周波数比、2D ではピッチを送って再生を開始する。失敗時は休眠を保ち、次の `Update` で再び試す。ストリームの voice（`G16-GR74-STREAM-VOICE`）も同じ `StartInstanceVoice` で作り直し、再開の位置をデコーダの `Seek` に渡す。

| `AudioDiagnostics` の欄 | 型 | 数え方 |
|---|---|---|
| `VirtualVoiceCount` | `uint32_t` | 現在休眠しているインスタンス数。休眠状態で開始した音も含む |
| `VirtualizeCount` | `uint64_t` | 物理の voice を手放して休眠に移るたびに 1 増える。休眠状態での開始は数えない |
| `DevirtualizeCount` | `uint64_t` | 休眠から物理の voice への復帰に成功するたびに 1 増える |

この 3 欄は `StolenVoiceCount` と同じく論理サウンド層が数え、ボイス層では 0 のままとする。`SoundSystemDiagnostics::ActiveInstanceCount` は休眠中を含む論理インスタンス数である。

休眠・復帰の切替にフェードは掛けない。距離による切替点では距離利得が 0 であり、呼び出し側の指定では GR75 が −55 dB 未満で休眠させるため、切替の段差は聞こえない前提である。受入れの閾値は [GR73 3D 定位とリスナーの受入れ記録](../AudioValidation/GR73Acceptance.md#受入れの閾値)を参照。
