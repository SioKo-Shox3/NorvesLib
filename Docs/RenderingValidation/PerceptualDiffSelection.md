# 技術選定記録: R0 S1 知覚差分方式

日付: 2026-08-10 ／ 状態: 決定済み ／ 決定者: ユーザー承認「じゃあCで。」

## 課題定義

- 解こうとしている問題: 【実測】`Docs/Plans/RenderingRoadmap.md` の R0/S1 は、tone-mapped golden image 回帰に知覚 diff を導入し、画像全体のプール指標と画素単位最大誤差を二段で判定するよう要求している。【実測】Task 2 は `Rgba8Image` と raw channel metrics を提供済みだが、知覚的重要度を表すプール指標はまだ持たない。
- 制約（技術・規約・予算）: 【実測】NorvesLib の通常コードは独自 container/string/pointer を使い、STL 型を公開境界へ露出しない。【推測】第三者実装を採用する場合は、license、固定 version、完全な include closure、更新手順を決める必要がある。【決定】R0 の比較対象は tone-mapped PNG とし、CUDA path と HDR-FLIP は build しない。
- 成功基準（何が満たされれば成功か）: 【決定】LDR-FLIP のプール指標と raw channel 最大誤差を独立した gate として併用する。【決定】公開 metrics は `MeanFlipError`（知覚 gate）、`MaxFlipError`（記録）、`MaxChannelDelta` とその座標（hard gate／記録）とする。【決定】Task 6 の低コスト prototype で identical image が `0`、人工差が `>0`、raw hard gate が維持され、upstream STL 型が NorvesLib header/Core に露出しなければ成功とする。

## 候補

| 候補 | 概要 | 主な根拠 | 主な懸念 |
|---|---|---|---|
| A. 何もしない | 【実測】Task 2 の raw channel strict 比較だけを維持する。 | 【実測】第三者 license と STL 例外を追加しない。【推測】1 px failure の raw 原因は直接説明しやすい。 | 【実測】Roadmap S1 の知覚 diff 要件を満たさず、採用には R0 scope waiver が必要になる。【推測】raw delta だけでは知覚的重要度を表せない。 |
| B. SSIM + raw max | 【外部】[SSIM 原論文の著者ページ](https://ece.uwaterloo.ca/~z70wang/publications/ssim.html)が提示する full-reference structural similarity と raw hard max を併用する。 | 【外部】原論文は structural similarity と subjective image quality の関係を評価している。【推測】NorvesLib 独自型で実装でき、large vendored header は不要にできる。 | 【外部】著者ページの代表的実験対象は JPEG/JPEG2000。【推測】rendered-image regression 用の window、色空間、multi-scale policy を別途設計・検証する必要がある。 |
| C. LDR-FLIP + raw max | 【外部】[NVIDIA Research FLIP](https://research.nvidia.com/publication/flip) と [pinned NVlabs repository](https://github.com/NVlabs/flip/tree/b475eb4bf394ab877c42166c9eb0a84a02cc5b14) の LDR-FLIP を raw hard max と併用する。 | 【外部】FLIP は rendered image と ground truth の human-perceived difference map を目的とし、単一集約値には mean FLIP を推奨する。【実測】pinned tree は C++ single-header と BSD-3-Clause license を提供する。 | 【実測】`FLIP.h` は `<vector>` などの STL を内部使用する。【決定】無改変 upstream を `Library/ThirdParty/flip` 内へ閉じる例外と、pin 更新時の再レビューを必要とする。 |

## 証拠等級付き比較

各主張は【実測】（当環境での実出力）、【外部】（公開一次資料）、【推測】（projectへの適用判断）、【決定】（本記録で固定する policy）のいずれかで示す。

| 比較軸 | A. 何もしない | B. SSIM + raw max | C. LDR-FLIP + raw max |
|---|---|---|---|
| S1 要件との一致 | 【実測】プール知覚指標が無いため不一致。 | 【推測】pooled SSIM と raw max で二段判定を構成できる。 | 【外部】mean FLIP と error map が提供される。【決定】mean FLIP と raw max で二段判定する。 |
| 対象領域の証拠 | 【推測】raw channel 差は知覚的重要度をモデル化しない。 | 【外部】SSIM は full-reference image quality を扱う。【外部】著者ページの代表例は JPEG/JPEG2000。 | 【外部】FLIP は rendered image と ground truth の知覚差を対象とする。【推測】rendered-image regression への直接性が3候補で最も高い。 |
| 依存と license | 【実測】追加依存なし。 | 【推測】独自実装なら追加第三者コードなしにできるが、paper interpretation の保守責任が生じる。 | 【実測】固定する2 vendor file は BSD-3-Clause。【実測】CPU required closure はその2 fileだけ。 |
| 型境界 | 【実測】既存の独自型規約内。 | 【推測】独自型だけで実装可能。 | 【実測】upstream header は STL を使用する。【決定】`.cpp` adapter 内だけに閉じ、NorvesLib header/CoreへSTL型を露出しない。 |
| R0 scope | 【実測】S1 waiverなしでは選択不可。 | 【推測】window、色空間、scale policy の追加選定が必要。 | 【推測】tone-mapped PNG と LDR mode の境界が一致し、HDR-FLIPを将来decisionへ分離できる。 |

調査の load-bearing identity は2026-08-10に当環境からGitHub API/raw contentを再取得して確認した。外部資料と実測値は次のとおり。

- 【外部】NVIDIA Research は FLIP を、交互表示される rendered image と ground truth の人間知覚差を近似する evaluator と説明し、単一値には mean FLIP を推奨している: [NVIDIA Research FLIP](https://research.nvidia.com/publication/flip)。
- 【外部】公式 repository は LDR-FLIP/HDR-FLIP と C++ single-header implementation を提供する: [NVlabs/flip pinned tree](https://github.com/NVlabs/flip/tree/b475eb4bf394ab877c42166c9eb0a84a02cc5b14)。pinned tree の README 表記は v1.7、artifact identity の正本は次の full commit SHA とする。
- 【実測】pin: `b475eb4bf394ab877c42166c9eb0a84a02cc5b14`。
- 【実測】`FLIP.h`: 127618 bytes／2491 logical lines。`#include <vector>` を含む。
- 【実測】`LICENSE`: 1648 bytes／31 logical lines。先頭は `BSD 3-Clause License`。一次資料: [pinned LICENSE](https://github.com/NVlabs/flip/blob/b475eb4bf394ab877c42166c9eb0a84a02cc5b14/LICENSE)。

### Vendor closure と build 境界

【決定】Task 6 が vendoringする required file は次の2件だけとする。

| vendor path | identity | license |
|---|---|---|
| `Library/ThirdParty/flip/FLIP.h` | 【実測】127618 bytes／2491 logical lines／commit `b475eb4bf394ab877c42166c9eb0a84a02cc5b14` | 【実測】BSD-3-Clause |
| `Library/ThirdParty/flip/LICENSE` | 【実測】1648 bytes／31 logical lines／同commit | 【実測】BSD-3-Clause |

【実測】pinned `FLIP.h` の angle include closure は次の10件である。

```text
algorithm
cstdlib
cstring
iostream
string
cmath
vector
sstream
fstream
limits
```

【実測】quoted include は次の2件だけで、いずれも `#ifdef FLIP_ENABLE_CUDA` 内にある。

```text
cuda_runtime.h
device_launch_parameters.h
```

【決定】R0のCPU targetでは `FLIP_ENABLE_CUDA` を未定義のままにする。`FLIP_ENABLE_CUDA=0` も `#ifdef` を有効にするため禁止する。【決定】CUDA SDK headersはCPU required closureに含めず、vendoringせず、license承認対象にも含めない。将来macroを有効化する場合は、別の依存・license承認へ戻る。【決定】CUDA pathとHDR-FLIPはR0でbuildしない。

### ThirdParty内STL例外

【決定】独自型規約の例外は、`Library/ThirdParty/flip` に無改変で置く上記upstream vendor fileの内部だけに限定する。【決定】`FLIP.h` の型とSTL containerはTask 6の`.cpp` adapter内だけで使用し、NorvesLibのpublic/private header、Core API、test fixture APIへ露出しない。NorvesLib側の公開入力はTask 2の`Rgba8Image`、公開結果は独自型だけで構成するmetricsとする。【決定】vendor更新時に都合のよいローカル改変を入れず、pin更新としてレビューする。

### LDR入力 transfer 契約

【決定】比較入力はtone-mapped PNGからdecodeしたRGBA8のRGB channelとする。pinned `FLIP::evaluate` のLDR入力契約である `[0,1]` linear RGBへ、各RGB byteを次のIEC 61966-2-1 sRGB EOTFで変換してから渡す。alphaはFLIP入力に使わない。

```text
encoded = byte / 255.0f
linear = encoded <= 0.04045
    ? encoded / 12.92
    : pow((encoded + 0.055) / 1.055, 2.4)
```

【決定】transfer関数はadapter内の単一実装とし、入力が既にlinearであるという暗黙仮定や、sRGB encoded値の直接入力を許さない。

### 公開 metrics と threshold 校正

【決定】公開metricsは次に固定する。

- `MeanFlipError`: 知覚diffのpooled gate値。
- `MaxFlipError`: error mapの最大値。診断記録用で、R0の単独gateにはしない。
- `MaxChannelDelta` とそのX/Y/channel座標: Task 2のraw hard gate値と診断記録。

【決定】mean FLIP thresholdとraw hard thresholdは独立して評価し、どちらか一方のfailureを他方の良好値で相殺しない。【決定】初回校正は固定条件のindoor/outdoor baseline、無変更の通常実行10回で得るnoise、選定指標で検出すべき人工差を収集し、「無変更で緑」と「人工差で赤」を分離できる値をテストコード内定数として固定する。実測分布、算出方法、採用値をTask 6の検証記録へ残す。分離できない場合はthresholdを都合よく緩めず、S1を再オープンする。

## プロトタイプコスト

- 本命候補を安く検証する方法と判定基準: 【決定】Task 6 Step 1–6を低コストprototypeとする。2 vendor fileのidentity検査、CPU-only adapter、sRGB EOTF、LDR-FLIP metrics、raw hard gate併用、最小unit testまでに限定する。
- 合格: 【決定】identical imageの`MeanFlipError`と`MaxFlipError`が`0`、人工差が`>0`、raw hard gateが同じ人工差を独立検出し、upstream STL型がNorvesLib header/Coreへ露出しない。
- 不合格: 【決定】上記のいずれかを満たさない、CPU buildでCUDA closureを要求する、pinned APIがlinear RGB境界へ安全にadapterできない場合。S1を再オープンし、A/B/Cを再提示する。

## Advisor往復

- 実施なし。理由: 2026-08-10のユーザー明示回答「じゃあCで。」で候補Cが確定しており、同日のread-only identity/closure再確認もbriefの期待値と完全一致したため、追加の設計裁定を要する不一致が無かった。架空のadvisor往復は記録しない。

## 決定記録

- 採用: **C. LDR-FLIP + hard raw max**。【外部】FLIPはrendered image comparisonを目的とする。【推測】tone-mapped PNGを扱うR0とLDR modeの境界が最も直接に一致する。【決定】`MeanFlipError`を知覚gate、`MaxChannelDelta`と座標をhard gateとし、`MaxFlipError`を診断記録する。
- ユーザー承認: 2026-08-10、原文「じゃあCで。」。この承認は、BSD-3-Clauseの2 vendor file、`Library/ThirdParty/flip` 内だけのupstream STL例外、`FLIP_ENABLE_CUDA` 未定義のCPU-only build、sRGB EOTFからlinear RGBへの入力契約を含む方式Cへの明示回答として記録する。
- Aを却下: 【実測】Roadmap S1の知覚diff要件を満たさず、waiverが必要になるため。【推測】raw channel strictだけでは画像全体の知覚的重要度を表すpooled gateにならないため。
- Bを却下: 【外部】SSIMはfull-reference image qualityの有力な一次根拠を持つが、代表的実験はJPEG/JPEG2000。【推測】rendered-image regression向けwindow、色空間、multi-scale policyの追加決定と独自実装検証が必要で、rendered imageを直接対象とするCよりR0導入不確実性が高いため。Bは次点候補として残す。
- version update条件: 【決定】pinを自動追従しない。更新候補ごとにfull commit SHA、bytes/logical lines、全include closure、license text、API/入力契約を再取得し、license、API、metric fixtureを再レビューする。差異があれば新しい承認記録を作り、現pinを勝手に進めない。
- **再評価トリガー**:
  - 【決定】知覚gateのfalse positiveが累計3件に達した時。
  - 【決定】知覚gateに起因するbaseline更新が1か月に2回を超えた時。
  - 【決定】HDR goldenを導入する時。HDR-FLIPを自動採用せず別decisionへ戻る。
  - 【決定】R1のpresentation gammaを修正した時。
  - 【決定】Task 6 Step 1–6のprototype合否基準を満たさない時。
  - 【決定】upstream pin、license、API、required/conditional include closureのいずれかを変更する時。
- **全再校正トリガー**: 【決定】sRGB transfer関数、PPD、FLIP pin、R1 presentation gammaのいずれかが変わった時は、indoor/outdoor baseline、無変更通常実行10回のnoise、人工差、mean/raw thresholdをすべて再校正する。部分的なthreshold流用は禁止する。

## R0 threshold 承認記録

- 承認日: 2026-08-10
- 承認原文: 「承認します」
- 承認対象 candidate SHA-256: `01755EC8E6091400B3589802411DCF1AD0AB9D3AF6EA5D9658390AC820DC414F`
- calibration HEAD: `2da856a3edd682807fe205baf884107af59f9198`
- Indoor baseline SHA-256: `545E745CE9958F310A551B0D71BEAB4DAD35743C6367E0DA0724F9930E6F49E7`
- Outdoor baseline SHA-256: `3676A470814C68841BF1C8E4BF8612802042AB936AAA77E1A9937C5EC642BA7E`
- 【実測】indoor: 通常10回の `noiseMax=0.000000000`、採用 `mean_flip_limit=0.000001`、選定人工差 `negativeMean=0.000001520`／`patch_size=1`／`channel_delta=2`。
- 【実測】outdoor: 通常10回の `noiseMax=0.000000000`、採用 `mean_flip_limit=0.000001`、選定人工差 `negativeMean=0.000002029`／`patch_size=1`／`channel_delta=1`。
- 【実測】両sceneで `noiseMax < mean_flip_limit < negativeMean` と6桁切上げ式が成立し、通常20行＋人工差40行の実測表が上記HEAD／baseline hashを保持することを確認した。
- 【決定】この承認は上記candidate hashのexact内容だけをsource thresholdへpublishする許可であり、別hash、再生成candidate、将来の再校正値には流用しない。

### upstream入力契約の証拠等級

- 【実測】vendored pinの `Library/ThirdParty/flip/FLIP.h` 2428〜2431行は、simplified LDR APIの入力を `[0,1]`、3 floats/pixel interleaved、linear RGBと明記する。これはadapterがIEC sRGB EOTFを適用するload-bearing根拠である。
- 【外部】同pinの公式 `README.md` はversion `1.7` を表示する。version表示は説明資料のidentity補助とし、build identityの正本はfull commit SHA `b475eb4bf394ab877c42166c9eb0a84a02cc5b14`、vendored byte hash、license textとする。
- 【決定】README version表示だけでpinや入力契約を更新しない。pin変更時はheaderのAPIコメント／実装、include closure、license、byte identityを再取得する。

### 規約例外（waiver）の分離

- 【規約例外】STL利用のwaiverは、byte-identical upstream `Library/ThirdParty/flip/FLIP.h` 内部だけに限定する。NorvesLib wrapper header、Core／Game、test fixture APIには拡張しない。
- 【規約例外ではない決定】`FLIP_ENABLE_CUDA`未定義、LDR-only、PPD 67.0、IEC sRGB EOTF、mean FLIP＋raw hard maxはR0の比較policyであり、STL waiverとは別にレビューする。
