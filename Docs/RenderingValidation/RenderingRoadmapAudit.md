# RenderingRoadmap 整合監査

監査日: 2026-09-22

## 結論

実装と受入れ記録を正として照合すると、R0〜R5は完了済みであり、RoadMapが定める依存関係からの逸脱は確認されない。R4をR5の後に実施した順序も、R4がRT依存へ倒れた場合はR4/R5の順序交換を許容するRoadMapの注記に合致する。

一方、RoadMapのステータス表はR3〜R5が「未着手」のまま遅れている。完了トレーラーもR2〜R5には無く、完了状態を履歴から機械的に復元できる状態ではない。`Docs/Plans/` はリポジトリの無視対象なので、RoadMap本体を強制追跡せず、本監査と追跡対象の受入れ記録・進捗で差分を明示する。

## フェーズ照合

| フェーズ | RoadMap表 | 実装・受入れの証拠 | トレーラー | 判定 |
|---|---|---|---|---|
| R0 | 完了 / `052a7dcbcedfd21764b7b9c0c5151d5108b51343` | `Docs/RenderingValidation/R0Acceptance.md`、`052a7dc` | あり | 整合 |
| R1 | 完了 / `eedec7a681489768645eabf77355886ae8e755c0` | `Docs/RenderingValidation/R1Acceptance.md`、`eedec7a` | あり | 整合 |
| R2 | 完了 / `ca9204d` | `Docs/RenderingValidation/R2Acceptance.md`、R2-P8受入れ `5f520e80c2123a82d11f16c5ec7358ecc189560d` | なし | 実装は整合、履歴規約は不足 |
| R3 | 未着手 | `Docs/RenderingValidation/R3Acceptance.md`、`9dc360de07a6f9df895e1eb9ac6933593216925b` | なし | RoadMap表が遅延 |
| R4 | 未着手 | `Docs/RenderingValidation/R4Acceptance.md`、`75102b1f7924a84e0eb86b2849cb0dd99eea73cc` | なし | RoadMap表が遅延 |
| R5 | 未着手 | `Docs/RenderingValidation/R5Acceptance.md`、最終検証 `a743eeea98801b59aa209a29a84b9fb874453e0d` | なし | RoadMap表が遅延 |
| R6 | 未着手 | R5完了を依存条件としてR6-aを独立受入れ。R6本体のRTGI・テンポラル蓄積・デノイザは未着手 | R6-a完了トレーラーを最終受入れコミットへ付与 | R6本体未着手、R6-aは整合 |
| R7 | 未着手 | R7コアはR5+R1待ち。両方完了済みだが未着手 | — | 後続工程 |
| R8 | 未着手 | R7コアとR6-a待ち | — | 依存未充足 |

### 依存関係の判定

- R0 → R1 → R2 は受入れ済みで、R3もR2後の屋外系列として受入れ済みである。
- R1 → R4 は受入れ済みである。R4のDDGIはRTを使うため、R5後に実施したことはRoadMapの順序交換条件内である。
- R0 → R5 は受入れ済みである。R5のRHI/Vulkan・RT影・動的TLAS・RT無効fallbackは `R5Acceptance.md` と最終検証記録にまとまっている。
- R6はR5に依存し、R6-aは独立したvelocity工程として先に閉じる。R4は任意の比較基準だが、現時点で完了済みである。
- R7コアはR5+R1を満たしているため着手条件だけは満たす。RoadMapの推奨直列順に従い、R6-aを先に進める。
- R8はR7コアとR6-aに依存するため、現時点での着手対象ではない。R7屋外拡張はR2+R3も必要とする。
- R4/R5およびR0〜R5のGPU性能gate保留は、RoadMapが性能を別トラックへ分離しているため、フェーズ未完了とは判定しない。残課題は `NEXT_FINDINGS.md` に残っている。

## 起動経路とシーンの照合

起動経路は変更していない。`GameApplicationHandler::CreateGameModeStateMachine` が従来どおり `Rendering3DTest` を登録・開始し、`Rendering3DTestRoutine::Enter` が次を構成する。

- 球、地面、ライト球、方向ライト
- `Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf` の非同期ロードと岩オブジェクト
- `SceneView` の既定HDR環境 `Textures/Atmosphere/grasslands_sunset_4k.hdr`

`9afea11` ではこの経路を差し替えず、毎フレーム投入されていた検証用黄色AABBだけを外した。選択対象のAABB描画は残している。Debug Game build exit 0、関連CTest 3/3 passed、`Game.log` で球・地面・ライト・方向ライト・岩・HDR環境、`frames_rendered=1`、120フレーム終了を確認した。

現時点の保存済みシーン源については、`Assets/Scenes` にあるレンダリング用ファイルは確認できず、存在する `M6AngelScriptDemo.scene.json` はM6のスクリプト受入れ用である。`SceneSerializer` の汎用Save/Load APIはあるが、Rendering3DTestの起動時に読み込む接続はなく、Bridgeにもレンダリングシーンの保存・読込メソッドはない。このため、存在しないEditor設定ファイルを推測して別経路へ接続していない。Editorで設定したシーンを永続化して起動時に読む機能が必要なら、M系のシーン保存・読込タスクとして別途実装する必要がある。

## 次の着手点

RoadMapから外れていない次工程はR6-M1である。R6-aでカメラとオブジェクトのvelocityをFramePacket経由で独立出力し、解析値のreadbackまで受入れたため、次にR6のRTGIとテンポラルデノイズ方式を選定する。その後、R7コア、R7屋外拡張、R8の順に依存を満たす。

本監査では無視対象の `Docs/Plans/RenderingRoadmap.md` を変更・追跡化していない。
