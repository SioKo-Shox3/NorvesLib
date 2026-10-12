# 守られていない上書きの見張り（開発ビルド）

2 フレームを重ねて描く前に、前のフレームの GPU の仕事が読みうるデータを CPU が書き換えている所を見つけるための見張り。
本体は `Library/Core/Public/RHI/FrameResourceWriteTracker.h`（ヘッダのみ）で、デバイス（`VulkanDevice`）が 1 つ持つ。
開発ビルド（`NORVES_BUILD_DEVELOPMENT`）だけで動き、Release ではフックも状態も消える。

## 出すもの

ログのカテゴリは `FrameResource`。

| 行 | いつ | 合否に使うか | 意味 |
|---|---|---|---|
| `FRAME_RESOURCE_UNPROTECTED resource=<名前> pass=<パス> kind=<種類> id=<ID> gen=<世代> range=<開始>+<大きさ> frame=<n>` | 名前・パス・種類の組を初めて見つけたとき（警告） | 使う | 守られていない上書き |
| `FRAME_RESOURCE_REWRITE resource=<名前> pass=<パス> frames=<n> instances=<n>` | 終わりに、名前とパスの組ごとに 1 回 | 使わない | フレームをまたいで持ち続ける資源を、2 つ以上のフレームで CPU から書き換えた（一覧） |
| `FRAME_RESOURCE_UNPROTECTED_TOTAL resource=… pass=… kind=… resources=<n> first_frame=<n>` | 終わりに、組ごとに 1 回 | 使わない | 組ごとの資源の数 |
| `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=<組の数> resources=<n> events=<n> rewrites=<n> frames=<n> reason=<理由>` | 終わりに 1 回（`RenderingCoordinator::ReleaseInitializedResources` の `WaitIdle` の後） | 使う | 集計 |

- `kind` の種類:
  - `buffer_previous_frame`: 飛行中のフレームを 2 と仮定し、前のフレームで記録したコマンドが参照した範囲へのバッファの書き込み。
  - `buffer_recorded_same_frame`: 同じフレームで記録済みのコマンドが参照する範囲へのバッファの書き込み（同じフレームの 2 回目の Execute が 1 回目を上書きする形など）。
  - `descriptor_bound_previous_frame`: 前のフレームで束縛した記述子セットの `Update`（飛行中のフレームを 2 と仮定）。
  - `descriptor_bound_same_frame`: 同じフレームで束縛した後の記述子セットの `Update`。
- `MAX_FRAMES_IN_FLIGHT=1` のままでも見つかる（フレームの番号で前のフレームの参照と比べるので、GPU の実際の重なりには頼らない）。
- `pass` は書き込みを起こしたパスのクラス名（`IRenderGraphPass::GetName`）。RenderGraph はパスの `Declare`・`PrepareParallelRecord`・`Execute` を、
  `RenderingCoordinator` は overlay のパスを、その名前の区間（`RHI::FrameResourceWriteScope`）で包む。パスの外は呼び出し元の名前
  （`RenderingCoordinator`・`PresentationPass`）で、どの区間にも入らない書き込みは `Unscoped`。LightingPass の中から呼ぶ `DDGIProbePass` は自分の名前の区間を持つ。
- `resource` はバッファの `DebugName`。名前が無ければ作ったパスの名前に `.Buffer`・`.DescriptorSet` を付ける。

## 数えるもの・数えないもの

- 識別は資源の ID（作るたびに増え、使い回さない）と世代（フレームの持ち主から借りた記述子セットは pool の世代、ほかは 1）。バッファは範囲ごとに比べる。
- 数える書き込みは次の 3 つだけ:
  - `IBuffer::Update`
  - `IBuffer::NoteExplicitWrite`（明示した書き込みの借り。フレームの持ち主の領域 `FrameUploadArena` は切れ端を返すたびに呼ぶ）
  - 書き込みのある `IDescriptorSet::Update`（束縛が変わっていない `Update` は書き込まないので数えない）
- **`Map` で得たポインタへの任意の書き込みは捕まえられない。** 常にマップしたままのバッファへ直接書く所は、`NoteExplicitWrite` を呼ばない限り見張りに見えない。
  読み戻しの `Map` は書き込みとみなさない。テクスチャの書き換え（`ITexture::Update`）は見ない。
- 作成の直後の 1 回は数えない。大きさの変更は作り直し（新しい ID）なので、その直後の 1 回も数えない。初期化で 1 回だけ書く物は、2 回目の書き込みが無いので出ない。
- 記録したコマンドの参照として数えるのは、記述子セットの束縛（セットが参照するバッファの範囲を含む）・頂点とインデックスのバッファの束縛・
  間接引数（`DrawIndexedIndirect`・`DrawIndexedIndirectCount`・`DispatchIndirect`）・`CopyBuffer` の両側・`FillBuffer`・`CopyBufferToTexture` の元。
  加速構造の構築の入力は数えない。
- バッファの参照の範囲は、書き換えを一度見たバッファだけ記録する（作成の直後に 1 回だけ書く頂点などの記録を省くため）。
  そのため書き換えの最初の 1 回はまだ参照を持たず、重なりは 2 回目の書き換えから見つかる。

## 「守られている」の登録

名前で除くのではなく、仕組みの側が資源を作るときに登録する。守られている資源は前のフレームとの重なりを見ず、一覧（`FRAME_RESOURCE_REWRITE`）にも載らないが、
同じフレームの中の重なり（`buffer_recorded_same_frame`・`descriptor_bound_same_frame`）は引き続き見る。

| 仕組み | 登録の仕方 |
|---|---|
| フレームの持ち主の領域のページ（`DeviceFrameUploadPageSource`） | ページを作る所を `RHI::FrameResourceProtectedCreationScope` で包む |
| フレームの持ち主から借りた記述子セット | 借りたセットを包む `VulkanDescriptorSet` の作成で登録する（世代は pool の世代） |
| `FrameUseRing`（飛行中のフレームの番号で引くリング） | `Acquire` が `RHI::MarkFrameUseProtectedCreation` を呼び、今のパスの区間の残りで作る資源（Use の資源は Acquire の後に作る）を登録する |
| `DynamicBufferRing`（呼び出し側の DebugDraw・ImGui が飛行中のフレームの番号でスロットを引く） | スロットのバッファを作る所を `RHI::FrameResourceProtectedCreationScope` で包む |
| `InstanceBufferRing`（飛行中のフレームの番号でスロットを引く） | 同上 |

`FrameUseRing` の窓は「Acquire から今のパスの区間の終わりまで」なので、その間に作ったパスのメンバの資源も守られている扱いになる（同じフレームの中の重なりは見る）。
パスの区間の外の Acquire は窓を開かない。

## 費用

- 書き換えを一度も見ていないバッファの束縛は、原子変数を 1 回読むだけ。書き換えを見たバッファの束縛と、フレームで初めての記述子セットの束縛は資源ごとの排他を取る。
- 報告は名前・パス・種類の組ごとに初めての 1 回だけログへ出す（資源ごと・種類ごとに 1 度だけ報告の排他を取る）。
- 性能を測る撮影の数字には、この見張りの分が入る（開発ビルドの RelWithDebInfo）。
