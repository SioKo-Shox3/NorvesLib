# G2 取り込み方針の決定記録

## 2026-10-03: G2-S4 / G2-S1 の作者承認

### 企画上の前提の訂正

オオカミ、シ者の作り分け、共通の土台の張り直しは、いずれも未定。
「採用済みの骨格共有の提案」をv1化の根拠として扱わない。
汎用エンジン機能の整備と、ゲームで特定の資産共有案を採用することは別である。
v1を進める理由には、G3のイベント・マーカー・ソケットの置き場を確保することが残る。

### G2-S4: 推奨Aを採用

- 頂点の影響は4本のABIを維持し、明示的なcook縮約で上位4本へまとめる
- Strict既定ではJOINTS_1等の追加セットを黙って無視しない。専用の拒否理由を追加し、既存status番号は維持する
- CUBICSPLINEは明示指定時に許容誤差付きLINEARキー列へ焼き込む。既定の拒否を維持する
- morphは既定拒否、明示drop時だけ警告と除去数を報告する。sparse拒否は維持する
- 関節上限は現行/0.2で128を維持し、Stage Bのv1と同時に256へ進める
- 誤差・脱落量の許容超過は失敗とし、縮約した事実/数量/差を報告する
- GR86前半（Strict/縮約/焼込/drop）と、GR82 Stage Bに連動する後半（256）を分ける

### G2-S1: 推奨Aを条件付き採用

GR32のサブメッシュ/材質slot表とGR82 Stage Aの複数クリップを、同じNVSKEL 0.2定義へ統一する。
0.2のHeaderSizeは320とする計画を基準に詳細を固定し、別内容のVersionMinor=2を並立させない。
Stage Bのv1は材質レコードの選定後に進め、イベント/マーカー/ソケット等の拡張の受け皿を維持する。

SkeletonIdからrest poseを除外することは承認済み。ただし、名前/階層の一致だけで古いclipの互換性を認めない。

## Stage B / v1 の必須rest pose安全契約

1. 各clipに、作成時の骨格のrest poseを保持する。物理的に共通テーブルへまとめる場合でも、各clipから当時のsnapshotを一意に参照でき、現在の骨格への参照で代用しない
2. 束縛時にsnapshotと現在の対象骨格を比較する。全関節にTranslationキーがあるclipも例外にしない
3. 許容差を超える場合は既定で拒否する。SkeletonId一致、チャンネル名一致、部分束縛の指定を、rest pose検査を省く理由にしない
4. 明示的なrest pose不一致許可があるときだけ束縛を通す。暗黙のfallbackや自動許可を設けない
5. 拒否時も明示許可時も、差の大きさを報告する。全体の最大差と超過した関節を特定でき、並進・回転・scaleの差と適用閾値を区別できる設計にする
6. 比較空間・単位・関節対応・quaternion符号同値・数値誤差の閾値はStage Bの仕様と試験で固定する。閾値はパラメータ化し、比較を無効化する無限大等と明示許可を混同しない
7. cache共有は資産identity/versionも確認し、SkeletonIdだけで異なるrestのresourceを同一視しない。reloadやrestデータ変更後の束縛は現在restに対して再検査する
8. 作成時snapshotが欠ける/不正で比較不能な入力は、互換と仮定しない。v1の必須情報として検証し、移行時には正しい作成元から再cookする

理由: Blenderからの書き出しは全関節に移動チャンネルを持ちうるため、骨格を合わせ直した後に
以前の移動値を持つclipを名前だけで束縛すると、黙って歪む危険がある。
GR85の比率補正が将来あることは、未補正clipを現在の既定経路で許可する根拠にならない。

### 必須の回帰試験

- 同じSkeletonId/同じrest poseでは通常束縛が成功する
- 同じ名前/階層でもrestの並進・回転・scaleの差が許容を超えれば既定で拒否する
- 全関節Translationキーを持つ古いclipでも拒否を維持する
- 明示許可時だけ成功し、差の大きさ・超過関節・閾値と許可した事実を報告する
- 許容内の数値差とq/-qの同値を誤拒否しない
- 欠落/不正snapshotや対応不能な関節は、比較不能を互換成功へ変換しない
- cook→parse→bindの往復でも作成時snapshotを失わない

## 他の選定との境界

S2とS8のGLB部分は先に承認済み。S3/S5/S6/S7、およびS8のBVH/FBX部分は今回の承認に含まれない。
特定の生物/造形/共通土台の採用を、このエンジン側の実装承認から推測しない。

## 2026-10-04: G2-S3(a) / G2-S7 の作者承認

- S3(a): NVMESH v1へ128B材質レコードを載せ、旧v0も読む。骨格側と共有する材質表現を採用する
- S7: 案Aの1mesh/Nprimitiveを採用する。mesh+skinのnodeがちょうど1つという契約を維持し、体/目等を分けた制作データは書出し前に1オブジェクトへ結合して材質slotで区別する
- GR32は上記のprofileに沿って進め、GR82 Stage Aの複数clipと統一NVSKEL0.2（320B）へ整合させる
- ARMの既定とemissive nitsの既定、S5/S6、BVH/FBXは今回の承認に含まれない
- この承認は造形/骨格共有/共通土台の制作案を採用した意味ではなく、既存のv1 clip作成時rest安全契約も変更しない

## GR32 パレット共有（2026-10-04承認）

1 component・1フレームあたりSkinnedPaletteの作成は1回。SkinnedPreviousPaletteはGBufferに描かれるcomponentだけ最大1回とする。影では前フレーム用を束縛せず、影先行・GBuffer先行の両方で現在パレットを共有する。サブメッシュ・パス・viewport数に比例して作成を増やさない。1本化のRHI改修は行わない。

## GR79 ARM・発光（2026-10-04追加承認）

- auto＋AO ignoreを既定とする。AI生成GLBではmetallicもignore（0）。金属素材だけ素材単位でtextureへ戻せる
- 定数化の判定はmax−minではなく1〜99パーセンタイル幅。従来案の許容4/255はパラメータとして保持。最終の採否は撮影の見た目で行う
- 作者の手元のTRELLIS.2犬はB192〜255でautoでは畳めずmetallic約0.93。Pixal3D犬G249〜255は外れ値が広げる幅への対処が必要。これらは作者の実測情報であり、この環境での実物再計測ではない
- emissiveFactor×KHR_materials_emissive_strength.emissiveStrengthが非0ならemissiveNitsPerUnitを必須とし、未設定は警告から理由付き拒否へ変更
- emissiveTextureだけでfactorが0の材質は通す。strength既定1・factor既定0を用い、textureの有無で発光を推測しない
- 診断は資産名・材質名・設定名emissiveNitsPerUnitを含める。換算値はasset-set単位でも指定可能とし、素材/資産側の明示指定を優先する
- asset-setでの換算指定は限定した例外であり、import設定全般の正本をsidecarから移す意味ではない

[公式emissive_strength schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/extensions/2.0/Khronos/KHR_materials_emissive_strength/schema/material.KHR_materials_emissive_strength.schema.json)もstrength既定1・非負を定義する。
