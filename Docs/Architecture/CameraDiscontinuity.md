# カメラの不連続（カット）と描画履歴

カメラのカットを記録する唯一の印は `CameraProxy::CutSerial`。既定値は 0 で、カットのたびに 1 増える `uint32_t` の通し番号である。
前のカメラに頼る処理は、**`SourceCameraId` と `CutSerial` の両方が、使いたい履歴を書いたときのカメラと一致する場合に限り**、その履歴を使う。
`SourceCameraId` は GameThread 側のカメラの識別子で、これが変わるカメラの切り替えもカットと同じに扱う。
描画側の登録枠を表す `CameraId` は、別のカメラに切り替えても同じことがある。

## 通し番号を使う理由

RenderThread は未描画のパケットを新しいパケットで差し替える。たとえばフレーム 10 を描き、11 でカットし、次に 12 を描くとする。
11 だけに立つブール値では 12 からカットが見えず、10 の画像を 12 に再投影してしまう。その 1 フレームだけ筋が引くため、静止画を 1 枚見ても気づきにくい。
`CutSerial` は 12 にも引き継がれるので、10 の履歴に記録した番号と比べればカットが分かる。別のカットフラグは追加しない。

## ゲーム側でカットを起こすとき

- カット後のカメラをパケットに載せる前に、その `CameraComponent` の `MarkCameraCut()` をカットごとに 1 回呼ぶ。
  呼んだ時点で `GetCutSerial()` の値が 1 増え、次の `BuildCameraProxy` から新しい番号が写る。この値は実行中だけ保持し、保存対象の `PROPERTY` には含めない。
- 補間を切るテレポート、ファストトラベル、時間の飛ばしでは、呼ぶ側が `MarkCameraCut()` を呼ぶ。
- `CameraDirector` を実装するときも、`Cut` とカットシーンの開始・終了で同じ `MarkCameraCut()` を呼ぶ。
- ショットは手動露出を前提とする。カットを契機に自動露出を即座に合わせる処理は加えない。

## 描画側でカットをまたぐとき

- **速度・モーションブラー** — `RenderingCoordinator::GenerateDrawCommands` は、直前のゲームフレームに有効なメインカメラがあり、現在のメインカメラと識別子・通し番号が一致するときに `FramePacket::PreviousMainCamera` を渡す。
  カットのパケットでは前のカメラを渡さず、速度（velocity）のカメラ移動分、モーションブラー、TAA の再投影にカット前の視点を使わせない。次の連続したパケットではカット後のカメラを前の値として渡せる。
- **TAA の画像履歴** — `TemporalAAHistoryTracker::Evaluate` は、同じビューポートで履歴を書いたカメラと登録 ID・`SourceCameraId`・`CutSerial` のいずれかが違えば `CameraChanged` を返し、その履歴を再利用しない。
  描画がゲームフレームを飛ばしたとき、`SceneView` は `FindReprojectionCamera` で履歴を書いたカメラを求める。カットのパケット自体が捨てられても、番号が違えばこの関数は `nullptr` を返し、カット前のカメラで再投影しない。
- **パストレーシングの連番** — GameThread と RenderThread はそれぞれの `PathTracingSequenceCarry` に、最後に見たメインカメラの識別子と通し番号を記録する。
  `ApplyPathTracingSequenceCarry` はどちらかが変わると、同じ `SequenceFrame` の途中でも連番用の前の値を決め直し、前のカメラと全インスタンスの前の変換を捨てる。
- **シャッター区間** — `SamplePathTracingCamera` は登録 ID と投影方式に加えて `SourceCameraId` と `CutSerial` も照合する。カットをまたぐときは現在のカメラを使い、前のカメラとの間を補間しない。
- **物体の動き** — `ApplyPreviousObjectStates` は物体の前の変換をゲームフレームの連続性で扱う。カメラがカットしても、直前のフレームから追跡できている物体の変換は保持する。

## 環境も同時に変えるとき

空・天気・時刻の描画履歴を切る印は `EnvironmentState::DiscontinuityId`。`EnvironmentSystem` の `SetImmediate`、`SnapToTarget`、`LoadState`、`SkipTime`、`SkipTo` が環境を不連続に変更するときに進める。
この番号と `CutSerial` は独立しており、片方だけを進めても、もう片方の履歴は切れない。
時間の飛ばしやファストトラベルで環境の不連続とカメラのカットを伴う場合、呼ぶ側が該当する環境操作と `MarkCameraCut()` の両方を呼ぶ。

## 前のカメラに頼る描画を足すときの約束

- 前のカメラは `FramePacket` から受け取る。描画パスでは `ViewRenderContext::GetPreviousCamera()` を使い、`nullptr` のフレームは前のカメラを使う計算を省く。
- 自分でカメラや再投影用の履歴を保持するときは、履歴を書いたカメラの `SourceCameraId` と `CutSerial` を一緒に保存する。使う直前に現在のカメラと両方を比べ、一方でも違えば履歴を捨てる。
