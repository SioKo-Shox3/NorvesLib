# 物理クエリ基盤（GR08）

## コライダーのmetadataと公開snapshot
- ColliderComponentがCollisionLayer/CollisionMask（32bit集合）、UserData（uint64値）を所有する。既定はLayer=1、Mask=全bit、UserData=0。ゲームが名前とbit割当を決め、Core/Physicsにゲーム固有名を置かない。Layer=0、複数bit、Mask=0も有効
- setterは既存SetTriggerと同じModule経由で、owner threadと登録世代/instanceを先に確認する。失敗時は旧値を保持。getterはGameThread専用の値取得。UserDataはopaqueな数値で、pointerの寿命や所有権を表さない
- BuildBroadphaseでLayer/Mask/UserData/Triggerを値コピーする。setter直後に公開済みsnapshotを書き換えず、次のpublish時に反映する。公開hitはsnapshotの時点の識別値を持つ
- 新PhysicsQueryHitに加えて旧PhysicsRaycastHit/PhysicsOverlapHitの末尾にUserDataを追加する。既存の法線方向、最近接の同距離順序、triggerを含む既定のqueryを維持する
- metadata供給とqueryの統合・solverのmask適用は段階を分ける。P2D時点ではmetadataの格納/コピー/旧hit伝搬までで、maskによるsolver制御は後続

## 検証範囲
- query値型と実proxy集約コードはクラウド上で直接g++により通常/最適化/sanitizerを検証する。独自allocatorやOSをmockに差し替えない
- 実Collider/Module/snapshot/旧hitの既定・高bit/0/複数bit・全uint64・全形状・反映時点・wrong thread/未登録拒否をPhysicsBroadphaseQueryTestに追加する。既存Windows.h依存の統合compile/実行は未確認として残す
