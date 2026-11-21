# AI モジュールと地上ナビゲーション

地上の移動経路は、地形に沿う均一な XZ 格子をタイル単位で保持し、整数コストの重み付き A* で求める。1 セルに地面の高さを一つだけ持つ 2.5D の表現である。障害物までの余裕を格子に記録し、照会する動物の半径と地形別コストを変えることで、同じタイルを複数の動物が使える。ベイク・距離変換・探索に外部のナビゲーションライブラリは使わない。

## 構成と所有権

`Library/Modules/AI/CMakeLists.txt` は C++23 の静的ライブラリ `NorvesModule_AI` を定義し、`Core` に `PRIVATE` リンクする。`Public/AI` にナビの型・照会・地形ポート、`Private/AI` に格子の構築、探索、描画の実装を置く。ソースは `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` で `Public/*.h`、`Private/*.h`、`Private/*.cpp` から集める。

モジュールとサービスの設計上の入口は `RegisterAIModule`、`FindAIModule`、`IAIModule::GetNavigation()` である。登録後はレジストリからモジュールを引き、そこから `INavigationService` を借りる。モジュールは `NavigationService` とその `NavGrid` を所有する。寿命の遷移、タイルの出し入れ、経路の要求・参照・解放はすべて GameThread で行う。[IModule.h](../../Library/Core/Public/Module/IModule.h)

| 寿命の段 | AI モジュールの契約 |
|---|---|
| `Install` | Engine の参照を借りる。Engine の所有権は呼び出し側にある。 |
| `Initialize` | 組み込みプロファイルで開始し、論理パス `AI/NavProfiles.json` を読む。ファイルが無い場合や検証に失敗した場合は組み込み値を使う。 |
| 実行中 | `PathHandle` は 32 ビットの `(Index, Generation)` で経路枠を指す値型。世代 0 は無効。`IsValid()` が調べるのは世代が非ゼロかだけで、現在の枠との一致はサービスが照合する。 |
| `Shutdown` | 全要求・経路枠・タイルを破棄する。借用したサービス、点列、タイル参照の利用を終了する。 |

操舵側で独立に経路を保持する型は Core の [Math/PolylinePath.h](../../Library/Core/Public/Math/PolylinePath.h) にある。`Math::PolylinePath` は点列をコピーし、3 次元の累積弧長を持つ。前回の弧長と許す移動幅を `FindClosest` に渡せば、折れ線が交差する場所でも最寄り点の探索区間を絞れる。`GetLookAhead` は先読み点、`GetRemainingLength` はその弧長位置からの残り距離を返す。

## 固定ステップと移動の受け渡し

| 1 歩の順序 | その歩で行うこと |
|---|---|
| 1. World の `FixedBegin` | Transform と補間履歴を準備し、`Physics.FixedPre` が Kinematic の歩の開始位置を控える。 |
| 2. モジュールの `PreFixedTick` | ナビにその歩の展開予算を配り、行動を決める。移動要求を準備し、Transform への書き戻しは物理に渡す。 |
| 3. World の `FixedTick` | Component の固定更新をこの段に登録する場合、同じ歩の要求を作る処理は `Physics.CharacterFixed` の前に置く。`Physics.CharacterFixed` が CharacterBody の要求を処理し、続く `Physics.FixedStep` が物理を進める。 |
| 4. モジュールの `FixedTick` | 物理はすでに一歩進んでいる。AI はここで位置を書き換えない。 |
| 5. World の `FixedEnd` | Transform を確定し、補間履歴を記録する。 |

Kinematic の速度は物理の一歩の中で `(現在位置 − 歩の開始位置) / dt` と求める。モジュールの `FixedTick` だけで移動すると、その変位は計算に入らず、次の歩では移動後の位置から記録が始まる。他に変位がなければ速度は 0 になる。AI の決定と物理による消費をこの順に置くと、AI と Physics のモジュール登録順に依存しない。[ApplicationProcessor.cpp](../../Library/Core/Private/Engine/ApplicationProcessor.cpp)・[PhysicsModule.cpp](../../Library/Modules/Physics/Private/Physics/PhysicsModule.cpp)

## 格子、通行条件、連結成分

`NavGridDesc` の既定は `(OriginX, OriginZ) = (0, 0)`、`CellSize = 1` m、`TileCells = 64`。セル幅は有限の正数、一辺は 1～1024 セル。各軸で `floor((位置 − 原点) / CellSize)` がセル座標になる。負の座標も負の無限大側に切り下げる。`NavTile` は同じ属性を一つの配列にまとめる構造（SoA）で、全配列が同じ添字 `localZ * TileCells + localX` を使う。`NavTile::Grid` はベイク時の格子設定（XZ 原点・セル幅・タイル一辺のセル数）を保持する。[NavTile.h](../../Library/Modules/AI/Public/AI/Navigation/NavTile.h)

| 配列 | セルに保持する値 |
|---|---|
| `Height` | セル中心の地面のワールド Y 座標（m）。経路点の高さに使う。 |
| `Flags` | `StaticBlocked`（静的な遮蔽）、`WaterDeep`（深水）、`Cliff`（段差）、`SteepSlope`（急斜面）は通行を阻む。`WaterShallow`（浅瀬）は進入コストを増やす。 |
| `SlopeQ` | 傾斜 0～90° を 0～255 に四捨五入した値。急斜面の判定には量子化前の角度を使う。 |
| `ClearanceQ` | 障害物までの余裕。`CellSize / 8` を単位に切り捨て、0～255 に収める。 |
| `GrassQ` | 草密度 0～255。0 は割増なし、255 はプロファイルで指定した草の割増全量。 |
| `Region` | タイル内の 4 連結成分を表す 16 ビット番号。遮蔽セルは 0、各成分は 1～`RegionCount`。 |

有限の正の半径 `r` の通行しきい値は `ceil(8 * r / CellSize)` で求める。これが 255 を超えると、しきい値は 256（`kNavClearanceThresholdUnreachable`）になる。どのセルもこの値を満たせないため、A* 探索・通行判定・最寄り点検索・視線判定・コスト照会のすべてで通行不能となる。読み込み済みセルに対する `IsNavCellPassable` は、遮蔽フラグがすべて 0 で、かつ `ClearanceQ >= thresholdQ` のとき真になる。例えば既定のセル幅で半径 0.5 m なら、必要な `ClearanceQ` は 4。未読み込みのセルは通れない。[NavTypes.h](../../Library/Modules/AI/Public/AI/Navigation/NavTypes.h)

局所の `Region` は半径によらず遮蔽フラグだけで決まる。角を切らない斜め移動は縦横の二歩でも到達できるため、4 連結とその 8 近傍の到達範囲は一致する。タイルの読み込み・置換・解放時には、境界の辺を共有する非ゼロの成分同士を併合し、格子全体で番号を振り直す。`GetConnectivityRegion` の 0 は未読み込みまたは遮蔽を表す。異なるタイルの局所番号を直接比較してはいけない。また、全体番号が同じでも、大きな半径では途中の狭い通路を通れないことがある。

## 静的タイルのベイク

`NavGridBaker::Bake(input, outTile)` は指定タイルを一枚生成し、成功時だけ出力を置き換える。`INavTerrainSource` の `SampleHeight`、`SampleWaterDepth`、`SampleGrassDensity` はワールド XZ で地面高・水深・草密度を返す。地形を省略すると `FlatNavTerrainSource` の平地（地面 Y = 0 m、水深 0 m、草密度 0）を使う。[NavGridBaker.h](../../Library/Modules/AI/Public/AI/Navigation/NavGridBaker.h)・[INavTerrainSource.h](../../Library/Modules/AI/Public/AI/Ports/INavTerrainSource.h)

1. タイルの四辺に最低 33 セルの余白を取る。これは量子化の上限 31.875 セル（255 / 8）に半セルを足し、整数に切り上げた幅である。`ClearanceApronCells` の既定値は 33 で、設定によって各辺 256 セルまで広げられる。余白を含む各セル中心で地形を採る。高さが非有限なら `StaticBlocked` を立てる。
2. X と Z の高さの中心差分から傾斜角を求める。片側しか使えない場合は片側差分を使う。角度が `MaxWalkableSlopeDegrees`（既定 40°）を超えれば急斜面、縦横の隣との高低差が `CliffStepHeight`（既定 1 m）を超えれば崖とする。水深は `DeepWaterDepth`（既定 0.6 m）以上なら深水、それ未満で `ShallowWaterDepth`（既定 0.05 m）以上なら浅瀬となる。
3. 静的障害物の XZ 足跡を押印する。`NavObstacleFootprint` は中心・二軸の半幅・Y 軸まわりの回転角で表す箱である。セルの正方形と回転した箱の重なりを分離軸（格子の 2 軸と箱の 2 軸）で調べ、境界で接するだけのセルも含めて `StaticBlocked` を立てる。セル中心の間を通る細い壁もこれで塞がる。
4. 余白を含む遮蔽セルを種として、行・列の順に厳密なユークリッド二乗距離変換を行う。中心間距離をセル単位で求め、半セルを引いて `ClearanceQ` に量子化する。計算範囲に種が一つもなければ 255 を格納する。この余白により、タイル内の `ClearanceQ` が表せる上限まで、余白の外の遮蔽セルを見落として値を過大にすることはない。
5. 余白を除いたタイルに属性を格納し、最後に 4 連結の `Region` を付ける。傾斜の量子化は地形分類時、クリアランスの量子化はタイルへの格納時に済む。[NavGridBaker.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavGridBaker.cpp)

## 整数コストの経路探索

`NavPathfinder::Begin` は格子の版を控え、始点と目標を調べる。始点を通れない場合、または両端とも通れるのに格子全体の連結成分が違う場合は、展開数 0 で状態が `Failed` になる。通れる始点と目標が同じなら 1 点で成功する。目標が遮蔽されている場合や未読み込みの場合は探索を続ける。

探索は 8 近傍を使い、斜めには両脇の縦横セルがどちらも同じ半径で通れる場合だけ進む。次のセルに入る一歩のコストを以下の整数演算で求める。除算はその都度切り捨てる。

```text
base  = diagonal ? 181 : 128
water = WaterShallow ? ShallowWaterExtraPer128 : 0
extra = water + (GrassQ * GrassExtraPer128) / 255
step  = base + (base * extra) / 128
```

目標までの各軸のセル差の絶対値を `dx, dz`、小さい方を `d` とすると、オクタイル距離は `181*d + 128*(max(dx,dz)-d)`。重み ε は 1～4 に収め、`round(256 * ε)` で Q8 整数にする。既定の 1.2 は 307、非有限値も 307 になる。ヒープの比較順は `(f, h, セルの鍵)` の昇順で、`h = (octile * epsilonQ8) >> 8`、`f = 始点からの累積コスト + h`。鍵の上位 32 ビットが Z、下位 32 ビットが X のビット列である。

`Step(n)` は最大 n 個を展開し、予算切れならヒープ・親・累積コストを保持して次回に続ける。探索全体の `MaxExpansions`（0 は無制限）に達した場合、または候補を使い切った場合は `Partial` となる。終点には展開済み節点のうち重みなしのオクタイル距離が最小のものを採り、同距離なら累積コスト、さらに同じなら鍵の小さい方を採る。始点だけの部分経路もあり得る。格子と入力が同一なら二度の探索は同じセル列を返し、展開を複数の `Step` に分けても一括実行とビット単位で一致する。[NavPathfinder.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavPathfinder.cpp)

## サービスの公開契約

| `INavigationService` の操作 | 呼び出し側が得る結果 |
|---|---|
| `LoadTile` / `UnloadTile` / `GetGrid` | 格子が空なら最初に読み込むタイルの格子設定を採用する。タイルがある間は、原点・セル幅・一辺のセル数のいずれかが異なる読み込みや置換を `Mismatch` で拒む。座標で解放し、格子は読み取り専用で借りる。置換・解放したタイルへの参照は失効する。[NavGrid.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavGrid.cpp) |
| `RequestPath` | 始点・終点・プロファイル・ε を渡し、要求を識別する `PathHandle` を受け取る。始点を通れないときは 2 m 以内の通行可能な中心に寄せる。候補がなければ有効な枠の状態が `Failed` になる。 |
| `GetPathStatus` | `Pending`、`Succeeded`、`Partial`、`Failed`、`Cancelled` を区別する。無効または解放済みの枠は `Invalid`。 |
| `GetPath` / `BuildPathPolyline` | 成功または部分到達の平滑化済み点列を借りる。他の状態では点列は空。折れ線を手元に保持する場合は `BuildPathPolyline` でコピーする。 |
| `CancelPath` / `ReleasePath` | 前者は `Pending` を中止して `Cancelled` にする。後者は状態を問わず枠を返す。解放後はハンドルのコピーも借りた点列も使えない。 |
| `IsPathStale` | 経路のセル列に加え、返す折れ線の各線分で `Raycast` が通行を調べたセル（角の両脇や格子線の両側を含む）について、タイルの座標と世代を記録する。1 点の経路でもその点で照会する。記録したタイルが一枚でも置換または解放されていれば真。[NavigationService.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavigationService.cpp) |
| `FindNearestWalkable` / `IsWalkable` | 指定の探索半径内で XZ 距離が最小の通行可能セル中心を地面高で返す（同距離なら Z、X の昇順）。後者は指定位置のセルを通れるか返す。どちらもエージェントの半径を使う。 |
| `NavRaycast` / `SampleCost` | 線分が最初に当たるセル・位置・進行割合を返す。XZ の変位がゼロでも、始点が格子線上なら両側のセルが判定対象になる（格子点では 4 セル）。後者は指定セルに直進で入るコストを返し、通行不能なら `kNavCostImpassable` を返す。[NavGridQueries.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavGridQueries.cpp) |
| `FindProfile` / `SetExpansionBudget` / `GetExpansionBudget` | 名前でプロファイルを引く。予算は固定ステップ 1 歩で展開できる節点の合計として設定・取得する。 |

**探索中の版管理。** `PreFixedTick` では最も古い要求から 1 本ずつ進める。1 本が終われば残った展開数を次の要求に渡し、予算を使い切れば次の歩に持ち越す。既定の **1500 展開は未計測の仮値**。処理中に格子の版が変わった場合、サービスは新しい版で `Begin` し直す。`NavPathfinder::Step` 自体は版の不一致で追加展開せず `Failed` になる。完成後の `IsPathStale` では格子全体の版より細かい、通過タイルごとの世代を使う。

**平滑化。** `SmoothNavPath` は地面高のセル中心を順に調べ、現在の点から最初に見通しが遮られる直前の中心までを 1 線分にする。成功時は各線分が探索と同じ半径で `NavRaycast` 相当の `NavGridQueries::Raycast` を通る。角や格子線では両側のセルも調べる。1 セルなら 1 点を返す。これは通行可能性の保証で、草や浅瀬のコストは平滑化では再評価しない。[NavPathSmoothing.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavPathSmoothing.cpp)

## エージェントのプロファイル

ルートは `version`（整数リテラルの 1）と `profiles`（1 件以上）の 2 項目。各要素は下の例の 4 項目を持つ。`name` は空文字・ヌル文字・重複を許さず、大文字と小文字を区別する。`radius` は float に変換しても正である 8 m 以下の有限数。浅瀬と草の割増はそれぞれ 0～1024 の整数リテラルである。全件が検証を通ったときだけ定義を入れ替える。再読み込みが失敗すれば従来の定義を保持する。

```json
{
  "version": 1,
  "profiles": [
    { "name": "Small",  "radius": 0.25, "shallowWaterExtraPer128": 64,  "grassExtraPer128": 16 },
    { "name": "Medium", "radius": 0.5,  "shallowWaterExtraPer128": 96,  "grassExtraPer128": 8 },
    { "name": "Large",  "radius": 1.0,  "shallowWaterExtraPer128": 128, "grassExtraPer128": 0 }
  ]
}
```

この 3 件が配布ファイルと組み込みに共通する仮の値である。半径は通行照会、探索、平滑化に渡すため、動物の大きさを変えても静的タイルを焼き直す必要はない。[NavProfiles.json](../../Assets/AI/NavProfiles.json)・[NavAgentProfiles.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavAgentProfiles.cpp)

## デバッグ描画と対象範囲

格子の印はカメラ位置を中心とする XZ 円内だけを `DebugDrawQueue` に送る。既定半径は 24 m、地面からの高さは 0.05 m、1 回の上限は 4096 セル。複数のフラグがあれば表の上から最初に一致した色を使う。

| 印 | 意味 |
|---|---|
| 赤の × | 静的な遮蔽 |
| 橙の × | 崖 |
| 紫の × | 急斜面 |
| 濃い青の × | 深水 |
| 水色の斜線 | 浅瀬 |

`LateTick` は格子と経路の描画に同じカメラ位置と半径を渡す。`SubmitNavPathDebug` は経路の各線分をその XZ 円の内側に切り詰め、呼び出し側が指定した色で描く。経路点の縦線は円内と円周上の点にだけ立てる。描画コードは `NORVES_ENABLE_DEBUG_OUTPUT` で囲み、Debug と RelWithDebInfo にだけ含める。[NavDebugDraw.cpp](../../Library/Modules/AI/Private/AI/Navigation/NavDebugDraw.cpp)・[AIModule.cpp](../../Library/Modules/AI/Private/AI/AIModule.cpp)

この格子が扱う障害物は静的な XZ 足跡である。動く障害物の更新、実地形データと地形ポートの接続、ベイク済みナビ資産の生成・読み込みは対象外。HPA* による階層探索は実測で必要性が示された場合に限る。同じ XZ に複数の歩行面を持つ橋の上下や建物内部の階層は、この単層表現の対象外である。
