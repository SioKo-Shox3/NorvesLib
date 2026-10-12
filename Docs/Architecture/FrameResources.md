# フレームごとのデータ（パスを書く人向けの約束）

描画を足す人が「前のフレームの GPU の仕事がまだこのデータを読んでいるか」「いつ消してよいか」を考えずに済むように、
フレームごとに書くデータは**フレームの持ち主**（`Library/Core/Public/Rendering/FrameResourceOwner.h`）から借りる。
飛行中のフレームの数・提出の完了・回収の時期は持ち主だけが知っていればよく、パスは知らなくてよい。

手本は `DebugDrawPass`（`Library/Core/Private/Rendering/DebugDrawPass.cpp`）。

## 約束

1. **フレームごとに書くデータは文脈から借り、メンバに持たない。**
   定数・頂点・インデックス・ストレージ（CPU が書いて GPU が読むもの）と記述子セットは、`ViewRenderContext::FrameResources`
   （`FrameResourceLane*`）から毎フレーム借りる。借りた物はそのフレームの中でだけ使い、メンバに残さない。
   飛行中のフレームの番号（`ViewRenderContext::FrameIndex`）でスロットを引くリングをパスごとに持たない。
2. **GPU だけが使う資源はメンバに持ってよい。**
   履歴（TAA の前のフレーム、HZB など）・描画先・パイプライン・render pass・framebuffer・サンプラ・共有の記述子セットの
   レイアウトのように、CPU がフレームごとに書き換えない資源はパスが持ち続けてよい。前のフレームの GPU の書き込みは、
   フレームの頭の全体のメモリバリアで次のフレームが待つ。
3. **RHI の資源は手放せばよく、破棄の時期は気にしない。**
   `BufferPtr`・`TexturePtr`・`PipelinePtr` などは参照を落とすだけでよい。デバイスが提出の完了まで破棄を遅らせる。
   作り直しも同じで、古い物を手放して新しい物を作ればよい。
4. **読み戻しは共通の口を使う。**
   GPU が書いて CPU が読むデータ（統計・フィードバック・撮影）は、フレームの持ち主の読み戻しの口から借り、結果は完了の後に
   RenderThread で受け取る。パスごとに「何フレーム後に読めば安全か」を数えない。
   読み戻しの口が入る前からある読み戻し（ShadowProbe・MegaGeometry の統計・照明の VSM の統計）は、各パスが提出の回数を数えて
   読む形のまま残っていて、口へ移す対象になっている。
5. **Parallel のパスでも同じ口を使える。**
   RenderGraph が Parallel の区間を記録するときは、区間（コマンドリスト）ごとに持ち主から別の口を借りて、写した文脈の
   `FrameResources` に入れる。パスは直列でも並列でも `context.FrameResources` から借りればよい。
   並列の記録の中で済ませてはいけない準備（テクスチャの作成・転送、render pass・パイプラインの作り直し）は
   `PrepareParallelRecord` で済ませる（`IRenderGraphPass.h` の約束）。

## 借り方

| 借りる物 | 呼ぶもの | 束縛 |
|---|---|---|
| 定数 | `AllocateConstants(value)`（`FrameUploadUsage::Constants`） | `BindConstantBuffer(binding, a.Buffer, a.GetOffset32(), a.GetSize32())` |
| ストレージ | `AllocateUploadData(data, size, FrameUploadUsage::Storage)` | `BindStorageBuffer(binding, a.Buffer, a.GetOffset32(), a.GetSize32())` |
| 頂点 | `AllocateUploadData(data, size, FrameUploadUsage::Vertex)` | `SetVertexBuffer(a.Buffer, a.Offset, slot)` |
| インデックス | `AllocateUploadData(data, size, FrameUploadUsage::Index)` | `SetIndexBuffer` に `a.Buffer` と `a.Offset` |
| 中身を自分で書く切れ端 | `AllocateUpload(size, usage)` の `CPUAddress` へ書く | 上と同じ |
| 記述子セット | `AllocateDescriptorSet(layout)`（`layout` は `IDevice::GetOrCreateDescriptorSetLayout` で引いた共有のレイアウト） | `Bind*` と `Update` の後、`SetDescriptorSet` |

- 切れ端は 1 つの大きなバッファの一部なので、**オフセットを束縛まで通す**。`FrameCommand` に積むときは `Buffer` と一緒に
  `Offset` も載せ、記録する側で `SetVertexBuffer` などに渡す（オフセットを 0 に固定しない）。
- 整列と記述子の範囲の上限は持ち主が守る。定数・ストレージの `GetOffset32`・`GetSize32` は切り詰めが起きない位置でだけ返る。
- 書いた中身はフラッシュせずに GPU から見える（常にマップしたままの HOST_COHERENT のページ）。`IBuffer::Update` は通さない。
- 記録中でない・大きさの上限を超えた・ページや pool を作れなかったときは `IsValid()` が false になる。そのフレームはその描画を
  飛ばす（エラーは持ち主がログに出す）。
- 借りた物を後の段（同じフレームの別の関数）へ渡して使うときは、`ValidateBorrowed(allocation, "名前")` で今のフレームの物か
  確かめられる（開発ビルドだけ検査し、前のフレームの物なら `FRAME_RESOURCE_STALE` を出して false を返す）。

DebugDrawPass の形（要点だけ）:

```cpp
FrameResourceLane* frameResources = context.FrameResources;
if (!frameResources)
{
    return;
}

const FrameUploadAllocation vertexAllocation =
    frameResources->AllocateUploadData(vertices->data(), uploadBytes, FrameUploadUsage::Vertex);
const FrameUploadAllocation uboAllocation = frameResources->AllocateConstants(uboData);
const FrameDescriptorAllocation descriptorAllocation =
    frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
if (!vertexAllocation.IsValid() || !uboAllocation.IsValid() || !descriptorAllocation.IsValid())
{
    return;
}

descriptorAllocation.Set->BindConstantBuffer(0, uboAllocation.Buffer, uboAllocation.GetOffset32(), uboAllocation.GetSize32());
descriptorAllocation.Set->BindTexture(1, sceneDepthTexture);
descriptorAllocation.Set->BindSampler(1, m_PointSampler);
descriptorAllocation.Set->Update();

context.EnqueueFrameCommand(FrameCommand::CreateDebugDrawLineList(
    m_RenderPass, m_Framebuffer, viewport, scissor, m_Pipeline,
    descriptorAllocation.Set, vertexAllocation.Buffer, vertexAllocation.Offset, vertexCount));
```

メンバに残るのは、シェーダ・パイプライン・render pass・framebuffer・サンプラ・共有のレイアウトだけ。

## 寿命（知らなくてよいが、知りたい人へ）

持ち主は飛行中のスロットごとに 1 つあり、`RenderingCoordinator` が段を進める。

1. 開始: スワップチェーンの `BeginFrame` の後。完了した提出の分を回収してから、このスロットの持ち主を新しい世代で記録中にする。
2. 提出または中止: 提出の成功で提出の通し番号を結び付ける。提出しなかったフレームは次の開始で回収する。
3. 回収: 結び付けた提出の完了で、そのフレームに借りた切れ端・記述子セットを全部まとめて戻す（ページは先頭へ戻し、pool は
   `vkResetDescriptorPool`）。回収の後の記述子セットは `IsExpired()` になり、書き込みも束縛も拒まれる。

## 開発ビルドの `FRAME_RESOURCE_*` の読み方

ログのカテゴリは `FrameResource`。Release では見張りも統計も無い（性能は RelWithDebInfo で測る）。

| 行 | 意味 | どうするか |
|---|---|---|
| `FRAME_RESOURCE_UNPROTECTED resource=<名前> pass=<パス> kind=<種類>` | 守られていない上書き。前のフレーム（飛行中のフレームを 2 と仮定）や同じフレームで記録済みのコマンドが参照しうるデータを CPU が書き換えた | `pass` のパスが `resource` をメンバに持ってフレームごとに書いている。借りる形へ移す |
| `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=<n>` | 終わりに 1 回。`count` は上の組の数 | 自分のパスの組が 0 になっていることを確かめる |
| `FRAME_RESOURCE_REWRITE resource= pass= frames=` | 一覧（合否に使わない）。フレームをまたいで持ち続ける資源を 2 つ以上のフレームで書き換えた | 移す候補の洗い出しに使う |
| `FRAME_RESOURCE_STALE resource=<名前>` | 前のフレームで借りた物を使おうとした（`ValidateBorrowed` が拒んだ） | 借りた物をメンバや静的な所に残していないか見る |
| `FRAME_RESOURCE_DESCRIPTOR_EXPIRED op=update`・`op=bind` | 回収の後の記述子セットへの書き込み・束縛を拒んだ | 同上 |
| `FRAME_RESOURCE_UPLOAD_NOT_RECORDING`・`FRAME_RESOURCE_DESCRIPTOR_NOT_RECORDING` | 記録中でないときに借りようとした | 借りるのは `Execute`（記録）の中だけにする。`Initialize` などでは借りない |
| `FRAME_RESOURCE_UPLOAD_TOO_LARGE`・`FRAME_RESOURCE_UPLOAD_PAGE_FAILED` | 範囲の上限（定数は `maxUniformBufferRange`）を超えた・ページを作れなかった | 定数は小さく分ける。大きなデータはストレージで借りる |
| `FRAME_RESOURCE_DESCRIPTOR_*_FAILED`・`FRAME_RESOURCE_DESCRIPTOR_UNSUPPORTED_LAYOUT`・`_INVALID_LAYOUT` | 記述子セットを借りられなかった | レイアウトを `GetOrCreateDescriptorSetLayout` で引いているか、デバイスに無い型を求めていないか見る |
| `FRAME_RESOURCE_LANE_LEAKED`・`FRAME_RESOURCE_OWNER_UNRETIRED` | 区間の口の返し忘れ・回収されないまま次の開始が来た（仕組みの側の誤り） | パスではなく RenderGraph・`RenderingCoordinator` を見る |
| `FrameResource.Upload …`・`FrameResource.Descriptor …`・`FrameResource.Lanes=` | 統計（最初のフレーム・600 フレームごと・ページや pool を足したフレーム・失敗のあったフレーム） | 1 フレームに借りた数・バイト数・足したページと pool の数を見る |
| `FRAME_RESOURCE_UPLOAD_CONFIG`・`FRAME_RESOURCE_DESCRIPTOR_CONFIG` | 起動時の設定（整列・範囲の上限・ページの大きさ・pool の容量） | — |

見張りが数える書き込み・数えない書き込み（`Map` のポインタへの任意の書き込みは捕まえられない）と「守られている」の登録の
仕方は `FrameResourceWriteGuard.md` に書いてある。借りた物は登録済みなので、借りる形へ移したパスの組は
`FRAME_RESOURCE_UNPROTECTED` に出なくなる。
