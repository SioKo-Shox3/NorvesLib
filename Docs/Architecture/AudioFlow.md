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
`Test/Modules/Audio/CMakeLists.txt` はドメインのリンク先を検査し、
指定された中立ソースへの Windows / XAudio2 依存の混入を検出する。
同じ CMake ファイルは `AudioBackendLifetimeTest` が fake のみでリンクされることも確かめる。

## Game の既定の組み立て

音声を有効にした通常起動では、`Game/GameApplicationHandler.cpp` の
`OnPreInitialize` が `AudioModuleOptions::bAllowNullBackendFallback` を有効にして登録する。
XAudio2 の生成または初期化に失敗すると `NullAudioBackend` へ切り替え、
`AUDIO_BACKEND fallback=null reason=...` を記録する。
`IAudioModule::GetBackendStatus()` から種類・フォールバックの有無・理由を取得できる。

`--m9-world-smoke` と `--audio-smoke` はフォールバックを許可しない。
実出力のスモーク試験は `--audio-smoke` と `Scripts/Test-AudioSmoke.ps1` を使い、
雨と足音を鳴らし、終了時に次の印を確かめる。

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

## 試験

`AudioBackendLifetimeTest` の束は fake バックエンドを使い、次の契約を確かめる。

- `AudioBackendLifetimeTest`: PCM の寿命、callback の保留・重複・古い通知の扱い、終了時の静止と破棄。
- `AudioModuleLifecycleTest`: voice の状態遷移、世代付きハンドル、初期化・作成・停止の失敗時の後始末と隔離。
- `AudioModuleRegistryTest`: ModuleRegistry 経由の初期化・Tick・終了と、初期化失敗時のロールバック。
- `AudioPlaybackTest`: ループ・一時停止・パン、フェード、発音制限と奪取、cooldown・dedupe、バンクと WAV の読み込み。
- `AudioComponentTest`: 持ち主ごとの停止と破棄、モジュール不在時の処理、Component の設定の保存・復元。

試験の理由は [Docs/Testing.md](../Testing.md) を参照。

## 現在の再生範囲

実際の再生は 2D のみで、Entity の位置への追従はまだない。
`PlayAt` の位置は dedupe の判定に使われるが、空間再生には使われない。
