# M8 最小物理: SceneQuery façade と決定的SAP

`PhysicsSceneQueryFacade.cpp` だけを編集してください。`PhysicsSceneQueryFacadeTest.cpp`、
`CMakeLists.txt`、`test.ps1` は編集してはいけません。

自己完結した最小物理 fixture を実装してください。ゲーム、Vulkan、live repository の
`Library/`、`Game/`、`Scripts/` を参照してはいけません。物理エンジン全体を作る必要はありません。

- A→B の接触 normal は B 方向、逆順の問い合わせでは反転する。touching は contact で depth `0`。
- `BodyHandle` は generation 付きで、slot 再利用後の stale handle を拒否する。
- 専用SAP は touching pair を候補に残し、挿入順によらず候補の順序を決定的にする。既存
  SceneQuery BVH を broadphase として流用してはいけない。
- solid の `OnHit` は新しい overlap pair で一度だけ発行し、毎フレーム再発行しない。
  step の順序は `prepare/snapshot` → `integrate/solve` → `publish query snapshot` →
  `dispatch events`。publish は event より先である。
- `SetVelocity` はlive handleだけを受理し、step開始時に積分される。hit callback中に同じ
  `SceneQueryFacade::BoundsOf` から、すでに公開済みの積分後boundsを読めること。
- Dynamic body は root だけを受理する。child dynamic、negative scale、nonuniform scale は拒否する。
- Game/query consumer は `SceneQueryFacade` を通す。query result と event は raw pointer を含まない値型にする。

完了前に fixture 内で次を実行してください。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test.ps1
```
