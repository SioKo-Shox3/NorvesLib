# AI モジュールと地上動物の移動

地上の移動経路は、地形に沿う均一な XZ 格子をタイル単位で保持し、整数コストの重み付き A* で求める。1 セルに地面の高さを一つだけ持つ 2.5D の表現である。障害物までの余裕を格子に記録し、照会する動物の半径と地形別コストを変えることで、同じタイルを複数の動物が使える。ベイク・距離変換・探索に外部のナビゲーションライブラリは使わない。

## 構成と所有権

`Library/Modules/AI/CMakeLists.txt` は C++23 の静的ライブラリ `NorvesModule_AI` を定義し、`Core` と `NorvesModule_Physics` に `PRIVATE` リンクする。CharacterBody への依存は AI → Physics の一方向である。`Public/AI` にナビの型・照会・地形ポートと操舵の Component、`Private/AI` に格子の構築、探索、操舵、四足運動への接続、描画の実装を置く。ソースは `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` で `Public/*.h`、`Private/*.h`、`Private/*.cpp` から集める。

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
| 2. `FixedModulePre.PreFixedTick` | 全モジュールの前段を実行する。AI は `SteeringSystem::BeginStep` で要求の変更と経路の再要求を受け付け、`NavigationService::RunBudget` で探索を進め、`SteeringSystem::Compute` でその歩の操舵速度を確定する。 |
| 3. World の `FixedTick` | `Physics.CharacterFixed` が固定駆動の CharacterBody に要求を出す。要求内の `BeforeSimulation` でモーターがその歩の操舵速度を変位に写し、続く `Physics.FixedStep` が物理を進める。 |
| 4. モジュールの `FixedTick` | 物理はすでに一歩進んでいる。AI はここで位置を書き換えない。 |
| 5. World の `FixedEnd` | Transform を確定し、補間履歴を記録する。 |

Kinematic の速度は物理の一歩の中で `(現在位置 − 歩の開始位置) / dt` と求める。モジュールの `FixedTick` だけで移動すると、その変位は計算に入らず、次の歩では移動後の位置から記録が始まる。他に変位がなければ速度は 0 になる。全モジュールの前段が終わってから World の固定更新に入るため、固定駆動では AI と Physics の登録順によらず同じ歩の操舵が間に合う。可変駆動の CharacterBody は固定ステップより前の Movement または PoseFinalize で要求するため、直近に完了した固定歩の操舵速度を使う。[ApplicationProcessor.cpp](../../Library/Core/Private/Engine/ApplicationProcessor.cpp)・[PhysicsModule.cpp](../../Library/Modules/Physics/Private/Physics/PhysicsModule.cpp)

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

## 操舵の純関数

操舵はワールド座標系の希望速度（m/s）を返す。地上では XZ 面に射影し、速さを検証済みの `MaxSpeed` 以下に収める。長さ 0、NaN、Inf を含む入力も数値出力は有限である。乱数、距離帯の状態、経路の進捗、軌跡は引数で受け渡し、同じ入力と状態からは同じ結果を得る。[SteeringBehaviors.h](../../Library/Modules/AI/Private/AI/Steering/SteeringBehaviors.h)・[SteeringBehaviors.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringBehaviors.cpp)

| 関数 | 希望速度と状態の契約 |
|---|---|
| `Seek` / `Flee` | 目標へ最大速で向かう、または脅威から最大速で離れる。`Seek` は距離が 0.0001 m 未満なら停止する。`Flee` は位置が重なると現在の進行方向、静止中なら +Z を選ぶ。 |
| `Arrive` / `Pursue` | 到着速度は停止半径内で 0、外では `min(MaxSpeed, sqrt(2 * MaxAcceleration * BrakingFactor * (distance - StopRadius)))`。既定の制動係数は 0.8。`Pursue` は目標の等速移動を既定で最大 1.5 s 先まで予測し、その位置へ同じ曲線で近づく。 |
| `KeepDistance` | 水平距離の判定には要求の上下端をそのまま使う（既定 6～10 m、幅 0 も可）。上端を超えれば接近、下端を割れば後退し、帯の中央を目指す。停止半径は `min(0.1 m, (0.5 − ヒステリシスの割合) × 帯幅)` とし、帯の内側で止まれるようにする。越えた端から帯幅の一定割合（既定 25%）だけ内側に戻ると保持に移り、相手の速度に合わせる。 |
| `InitializeWander` / `Wander` | 個体の固定シードで乱数列を初期化し、正の有限な `dt` でだけ前方の円上の角度を揺らす。種 0 は `0x9e3779b9` に置き換える。既定速度は最大速の半分。 |
| `PathFollow` / `IsPathFollowFinished` | 初回は経路全体、その後は前回の弧長の近くで最近点を探し、進捗 `S` は前回値と最近点の弧長の大きい方にする。先読みの弦が途中の頂点から離れすぎる角では先読みを縮め、必要なら次の頂点を狙う。途中では現在と先の接線の内積で減速し、速度倍率の下限は既定 0.3。終点には `Arrive` で近づく。空経路、または残りの弧長と終点までの水平距離がともに停止半径内なら完了。 |
| `RecordTrail` / `FollowTrail` | 先頭個体の有限な位置を初回は即時、その後は 0.25 s ごとに 64 点のリングへ記録する。1 回の呼び出しで追加するのは最大 1 点。最新点から 3 次元の弧長を逆にたどり、指定距離に達した最初の記録点（足りなければ最古点）へ `Arrive` する。 |
| `Separation` / `Alignment` / `Cohesion` | 分離は水平半径内の相手が近いほど強く離れ、水平距離が 0.0001 m 未満の相手とは、ID の組で決まる軸に沿って互いに逆向きに離れる。位置と ID の件数が異なる場合や、どちらかの ID が零または両者が同じ場合は、近傍の添字で向きを決める。操舵系は各個体の所有 Entity の ObjectId を渡す。整列は近傍の平均速度に合わせ、結合は近傍の重心に `Arrive` する。有効な近傍が空なら零速度。 |
| `CombineWeighted` | 各速度の重み付き和を操舵面と速さの上限に収める。負または非有限の重みの寄与は 0。 |

**速度と旋回の上限。** `IsFiniteVector`、`SanitizeVector`、`ToPlane`、`PlanarLength`、`SafeNormalize`、`ClampLength` がベクトルの検査・射影・長さを扱う。`SanitizeLimits` は負または非有限な上限を項目の既定値に戻す。`ApplySteeringLimits` は射影して最大速に収めた現在速度を起点に、1 歩の速度ベクトルの変化量を `MaxAcceleration * dt` 以下にする。現在と希望の水平速がともに 0.001 m/s を超えるとき、水平の向きの変化も `EvaluateTurnRate(limits, speed) * dt` 以下にする。旋回率は現在の速さと最大速の比を使い、4 個のキーを線形補間する。時間刻みが非有限または 0 以下なら起点の速度を保つ。

**地上のコンテキスト操舵。** `ContextDirection` の 16 方向は +Z から +X 側へ 22.5° 刻み。`AddInterest` は希望速度との向きの一致度と速度比から興味を求める。`AddDangerFromClearance` は先読み距離に対して不足する空きの割合を、`AddDangerFromNeighbors` は近傍の近さと向きを危険に反映する。`ResolveContext` は最小危険値から `DangerTolerance`（既定 0.05）以内の方向のうち興味が最大のものを選ぶ。候補の最大興味が `MinInterest`（既定 0.0001）以下なら最も開いた方向を選ぶ。同点は小さい添字を優先する。

既定の `bKeepExactWhenClear` では、興味で選べて最小危険値が許容値以下、かつ希望方位の両隣が候補なら、希望方位を量子化せず上限内の速さを保つ。それ以外は選んだ方向で速さに `1 - Danger` を掛ける。全方向の危険が 1、または上限適用後の希望水平速が 0.0001 m/s 以下なら停止する。

## エージェントの登録と要求

`SteeringAgentComponent` の公開設定は次の通り。[SteeringAgentComponent.h](../../Library/Modules/AI/Public/AI/Steering/SteeringAgentComponent.h)

| PROPERTY | 既定値 | 用途 |
|---|---|---|
| `Dimension` | `Ground` | 操舵面（`Ground` / `Flight`） |
| `MaxSpeed` | 6 m/s | 操舵速度の上限 |
| `MaxAcceleration` | 8 m/s² | 操舵速度の変化率の上限 |
| `TurnRateBySpeed` | (6, 4, 3, 2) rad/s | 速度比 0、1/3、2/3、1 での旋回率 |
| `NavProfileName` | `Medium` | 地上の経路・通行照会と分離半径に使うプロファイル |
| `MotorProfilePath` | `AI/Locomotion/Wolf.json` | 四足運動の論理資産パス |
| `WanderSeed` | 1 | 徘徊要求の開始時に使う種 |

操舵系は `PhysicsModule::RegisterCharacter` と同じ形で、`SteeringAgentComponent` の非所有参照と `ComponentId` を登録簿に持つ。`Initialize` は個体を `AIModule` に登録し、運動プロファイルを取得して同じ Entity の CharacterBody への接続を試みる。`EndPlay`・`Finalize`・デストラクタはいずれもモーターの接続を外し、登録が残っていれば解除して経路枠を返す。[SteeringAgentComponent.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringAgentComponent.cpp)

各固定歩の開始時に、各 Component の所有 Entity を調べ、Engine の World に属する個体だけを操舵する。World に入る前に `AddComponent` した個体も、入った後の最初の固定歩から操舵の対象になる。所有 Entity の `ObjectId` と `EntityHandle` は固定歩ごとの近傍収集で所有者から読む。[SteeringSystem.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringSystem.cpp)

初期要求は `Idle`。GameThread から要求を一つ渡すと、受理時に要求を置き換えて非ゼロの `Revision` を更新する。不正な位置・距離・無効なハンドルを渡した場合は `false` を返し、前の要求を保つ。

| 要求 API | 引数と動作 |
|---|---|
| `RequestIdle()` / `RequestWander()` | 主行動の希望速度を零にする、または個体のシードから徘徊を始める。 |
| `RequestArrive(target, stopRadius)` / `RequestMoveTo(goal, stopRadius)` | ワールド位置へ直接到着する（既定 0.1 m）、またはナビ経路に沿って移動する（既定 0.3 m）。 |
| `RequestPursue(target)` / `RequestFlee(threat)` | Entity ハンドルで相手を指定し、予測位置を追う、または現在位置から離れる。 |
| `RequestKeepDistance(target, minDistance, maxDistance)` | 相手との水平距離を指定帯に保つ。距離は有限かつ非負で、下端は上端以下とする。 |
| `RequestFollowTrail(leader, followDistance)` | 相手の軌跡を指定距離だけ後ろから追う（既定 2 m）。 |

`GetRequest()` は現在の要求、`GetSteeringVelocity()` は直近の固定歩で確定した操舵速度を返す。`MoveTo` は成功・部分到達の折れ線を取り込み、経路が古ければ次の歩で再要求する。完走後、`Partial` は折れ線の終点へ `Arrive` する。`Succeeded` は終点と目的地の水平距離が 0.001 m 以下か、終点から目的地への `NavRaycast` が通れば目的地へ、それ以外は終点へ `Arrive` する。経路待ち・失敗・相手を解決できない場合は主行動の希望速度が零になる。どの要求にも近傍からの分離と速度変化の上限が適用される。[SteeringSystem.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringSystem.cpp)

## 近傍の格子と操舵の更新

`SteeringSystem::Compute` は正の有限な固定刻みごとに、`SteeringSystem::CanSteer` を満たす個体の位置と前回の操舵速度を登録順に集める。所有 Entity は階層内で有効かつ Tick が有効で、操舵 Component は有効かつ Tick が有効で破棄予定でないことが条件となる。解決した個体がこの条件を外れたら、近傍の格子から除き、操舵速度を零に戻す。位置・速度は X、Y、Z ごとの配列（SoA）に保持し、位置の X、Z から `Spatial::UniformGridHash` を作り直す。セル幅の既定は 8 m。全個体を同じ入力から解き終えてから各 Component に結果を書き戻すため、先に解いた個体の新しい速度が後の個体に混ざらない。

`QueryRadius(x, z, radius, out, excludeIndex)` は高さを無視し、倍精度で `dx² + dz² <= radius²` を満たす点の元の配列添字を昇順で返す。円周上の点も入り、自分の添字は指定して除ける。非有限の中心・半径や負の半径では結果は空。物理の AABB の木が衝突形状を扱うのに対し、この格子はエージェントだけを扱う。GR24 は `AIModule::GetNeighborHash()` で近傍を照会し、`AIModule::GetNeighborEntities()` の同じ添字から所有 Entity のハンドルを得る。`SteeringSystem` では、近傍の添字 `i` に対応する `GetNeighborEntities()[i]` と `GetNeighborObjectIds()[i]` は、その固定歩に所有 Entity から読んだ `EntityHandle` と `ObjectId` である。ECS の行がまだ無い個体も位置は格子に入り、ハンドルは無効になる。[UniformGridHash.h](../../Library/Modules/AI/Private/AI/Spatial/UniformGridHash.h)・[UniformGridHash.cpp](../../Library/Modules/AI/Private/AI/Spatial/UniformGridHash.cpp)

`Gather` は `Pursue`・`KeepDistance`・`Flee`・`FollowTrail` の相手をハンドルで解決し、位置と速度を読む。相手に `SteeringAgentComponent` があれば、`CanSteer` を満たすときは前回の操舵速度、満たさないときは零を読む。この判定で、相手を収集する順番にかかわらず同じ速度が得られる。相手に操舵 Component が無ければ、破棄予定でない `CharacterBodyComponent` の速度を使う。`Flee` と `FollowTrail` の計算が使うのは相手の位置だけである。[SteeringSystem.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringSystem.cpp)

操舵系は要求の希望速度と半径 `max(1 m, 2 * profile.Radius)` の分離速度を等しい重みで足す。地上ではさらに 16 方向のナビ照会（先読み 2～6 m）と半径 1.5 m の近傍で危険を評価し、最後に速度・旋回の上限を適用する。足元が未読み込みまたは通行不能ならナビ由来の危険は加えず、近傍の危険だけを使う。行動の選択は現在の要求の種類で決まり、行動木などの上位の決定は GR24 の範囲である。`Flight` は操舵の次元を切り替えるが、飛行する動物の運動と群れの制御は G13-GR22-FLIGHT-FLOCK の範囲である。

## 四足動物の運動と Animator への公開

`AnimalMotor` は同じ Entity の `CharacterBodyComponent::BeforeSimulation` を購読する。プレハブやシーンの読み込みでは `Initialize` が所有 Entity の `ObjectId` の採番に先行するため、ID が 0 の間は接続を保留する。操舵系は各固定歩の開始時に `CanSteer` を満たす個体で接続を確認し、採番後の最初の歩で Body に接続する。所有者の `ObjectId` か Body が変わったときは、接続の世代を進め、現在の Body の `ComponentId` が以前の接続先と一致すれば古い購読を外してから、新しい接続を結ぶ。コールバックは購読時の `ObjectId` で World から所有 Entity を引き、個体と Body の `ComponentId`、接続の世代、破棄状態、`CanSteer` を照合してからモーターを進める。[AnimalMotor.cpp](../../Library/Modules/AI/Private/AI/Motor/AnimalMotor.cpp)

1. `MakeIntent` は操舵の水平速が 0.001 m/s 未満なら移動入力を零にする。それ以上では速歩の上限（`Gaits[1].SpeedMax`）を超えたときだけ `bSprint` を立てる。操舵の単位方向に `min(speed / speedMax, 1)` を掛けて `LocomotionIntent::Move` に渡す。分母は通常なら速歩、スプリントなら襲歩（`Gaits[3].SpeedMax`）の上限。モデルは同じ上限を入力強度に掛けるので、平地でその場旋回をしていなければ、平滑化前の目標速は襲歩の上限まで操舵の水平速に一致する。
2. `QuadrupedLocomotionModel::Step` に Body の接地・地面法線・速度・足場速度・重力と、持ち主の向きを渡す。モデルが足場に対する観測速度、上下のしきい値、最短滞在時間から歩様を選ぶ。斜面、旋回、加減速、衝突の結果により実際の速さは目標と異なり得る。[QuadrupedLocomotionModel.cpp](../../Library/Core/Private/Locomotion/QuadrupedLocomotionModel.cpp)
3. Body の準備ができていてモデルが成功したら、`SetDesiredVelocity(Zero)` で速度入力の二重適用を防ぎ、`MoveDelta(output.PlanarDisplacement, output.DeltaYaw)` で同じ要求に水平変位と回転を渡す。両方を Body が受理したときだけ状態と出力を保存する。衝突と移動は CharacterBody が担うため、AI 独自の Kinematic 移動は持たない。歩様表と出力も Core のモデルで共有し、AI 用の `GaitTable` や独自の `LocomotionOutput` を重ねない。
4. `PostLateTick.ModuleLateTick` は持ち主、次に直下の子の Animator を探す。`SetDriveSignals` にモデルの `Speed`・`TurnRate`・`bGrounded` と Body の `Velocity`・`GroundNormal` を渡し、名前付きパラメータ `GaitId`、`GaitWeight0`～`GaitWeight3`、`PitchAngle`、`BankAngle` を設定する。歩様の混合と姿勢をアニメ側で使えるため、再生速度だけを渡すアダプタは設けない。このフレームの Animation 群はすでに終了している。

`LocomotionProfileCache` は論理パスごとに初回だけ資産を読み、Core の `QuadrupedLocomotionParams` を共有する。空パスは既定値、読み込み・解析失敗時は既定値をそのパスに保持する。`MotorProfilePath` のプロパティ適用時はそのパスで引き直し、モジュールの終了時にキャッシュを消す。配布するオオカミのプロファイルは `locomotion.v1` で、4 歩様の速度上限 1.8、4.5、9、15 m/s を含む値は仮の調整値である。[LocomotionProfileCache.cpp](../../Library/Modules/AI/Private/AI/Motor/LocomotionProfileCache.cpp)・[Wolf.json](../../Assets/AI/Locomotion/Wolf.json)

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

`IAIModule::SetSteeringDebugDrawEnabled` はナビの描画とは独立したスイッチで、既定は無効。`LateTick` でカメラから XZ 半径 60 m 以内の有効な個体に、上限適用後の操舵速度を緑、適用前の希望速度を黄で描く。目標は水色の球と個体からの線、`KeepDistance` の上下限は目標を中心とする 2 本の円、`MoveTo` の準備済み経路は青い折れ線で示す。要求の版が変わった直後は古い目標・帯・経路を描かない。Release ではスイッチは無効で `IsSteeringDebugDrawEnabled()` は常に `false`。[SteeringDebugDraw.cpp](../../Library/Modules/AI/Private/AI/Steering/SteeringDebugDraw.cpp)

この格子が扱う障害物は静的な XZ 足跡である。動く障害物の更新、実地形データと地形ポートの接続、ベイク済みナビ資産の生成・読み込みは対象外。HPA* による階層探索は実測で必要性が示された場合に限る。同じ XZ に複数の歩行面を持つ橋の上下や建物内部の階層は、この単層表現の対象外である。
