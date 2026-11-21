# 音声の構成と更新順序

音声は、バックエンドに依存しない論理サウンド層と voice の寿命管理を、
実際の出力に接続するアダプタに分けている。

## 層の構成

```text
Game / AudioComponent / AudioGameModeOwner
          ↓
ISoundSystem / SoundSystem ← SoundBank ← AudioClipLoader
          ↓
IAudioService
          ↓
IAudioBackend → XAudio2Backend または NullAudioBackend
```

| 層・要素 | 役割 |
|---|---|
| `NorvesAudioDomain` | バックエンド非依存の静的ライブラリ。`Library/Modules/Audio/CMakeLists.txt` に構成がある。 |
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
6. 続く `SoundSystem::Update` は `Drained` の voice を破棄して論理インスタンスを回収し、残った音のフェードと音量、停止を進める。時間には上限付きの可変フレーム delta を使う。
7. 終了時は `ModuleRegistry::ShutdownAll` が登録の逆順で終了する。音声モジュールは先に `SoundSystem`、次にサービスを終了し、サービスは callback を静止させてから voice とバックエンドを破棄する。

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
3. 登録された `AudioComponent` のうち `Spatialization == World3D` のものは、`ISoundSystem::IsAudible` が真のインスタンスへ、持ち主の `Entity::GetWorldTransform()` の位置を `ISoundSystem::SetEmitterPosition` で渡す。有効なハンドルでは物理 voice に停止を発行するまで `IsAudible` が真なので、フェードアウト中も音源は持ち主に追従する。
4. `SoundSystem::Update(dt)` が各 3D インスタンスの音源速度を位置の差分/dt で求める。`ComputeAudioSpatial` にリスナーと音源の状態を渡し、下表の値を反映する。左右の利得は両方とも前回の適用値との差が ε = 1e-4 以下なら更新を省く。3D の周波数比（`SetPitch` による変更を含む）と 3D の voice の音量にも同じ ε を使い、音量が非ゼロから厳密に 0 になるときは送る。2D の voice の音量は完全一致のときだけ更新を省く。

| 係数 | 反映先 |
|---|---|
| 距離利得 × 等パワーの左右のパン係数 | `IAudioService::SetVoiceOutputGains` に左右の線形利得を渡す。近距離ほどパンは中央に寄る。3D の voice に `SetVoicePan` は使わない |
| ピッチの周波数比 × ドップラー比 | `IAudioService::SetVoiceFrequencyRatio` に渡す。最終ピッチを pitch（半音）とすると、3D の voice 作成時の上限は min(max(12, pitch) + 12, 120) 半音（`kAudioMaxVoicePitchSemitones` = 120）。pitch > 120 の再生は既存の voice を奪う前に `InvalidState` で拒否する |
| 背後の音量倍率 | 既存の voice 音量（10^((volume_db + 揺らぎ_db)/20) × 再生の音量 × フェード利得 × 区分の音量 × 全体の音量）に一度だけ掛ける |
| 背後の高域の係数 | 値を保持する。フィルタへの適用は `G16-GR73-ACOUSTICS-AI-WIRE` の範囲 |

真後ろでの音量減衰は 6 dB、高域の係数は 0.5。`MinDistance` の内側では、近距離の広がりと同じ係数 s = d/MinDistance で背後の効果も弱まる。式と受入れ条件は [GR73 3D 定位とリスナーの受入れ記録](../AudioValidation/GR73Acceptance.md)を参照。
