# 表面の語彙と照会の契約

## 目的

足音、足跡、草の踏み倒し、濡れた毛、足 IK が、同じ足元の表面を名前で疎に参照する。コードは表面の ID とフラグを扱い、効果や音などの具体的な内容は消費者のデータから引く。表面の値は現時点では仮の値であり、後で調整する。

## 型と番号

`Scene/SurfaceTypes.h` が表面の型を定義する。

- `SurfaceId` は登録簿の添字を保持する `uint16_t` の型で、`Default()` は添字 0。
- `SurfaceFlags` は `Soft`、`Hard`、`Liquid`、`Vegetated`、`Wettable` の組み合わせで、`None` はフラグなし。
- `SurfaceDesc` は名前、親、フラグと、音の `Audio`、見た目の `Visual`、物理・足 IK 用の `Physical` を持つ。
- `SurfaceQuery` は位置、法線、任意のコライダー、半径と照会フラグを持つ。半径 0 は点を表す。
- `SurfaceSample` は主・副の表面と副の重み、濡れ、水たまりの深さ、草の密度、明示タグを使った場合のコライダーを持つ。

登録簿の `SurfaceId` と、地形の `GroundSample::SurfaceId`（`uint8_t` の層番号）、物理の結果の `uint8_t SurfaceId` は別物であり、暗黙に変換しない。地形の層から表面へは `TerrainMaterialDesc` の `Layers` と各層の `SurfaceName` の表を介する。地形の層番号を登録簿の表面に変換するのは地形の provider だけである。

保存するのは `String` の名前であり、`SurfaceId` の数値は保存しない。添字は読み込みで決まる。再読み込みでは同じ名前の添字を保ち、消えた名前は添字を再利用せず引退させる。引退した添字の `GetDesc` は `Default` の記述を返し、`GetName` は元の名前を返す。同じ名前が復帰すれば元の添字を使う。

## 登録簿とデータ

`SurfaceRegistry` は `Engine::Engine` のメンバで、`GetSurfaceRegistry()` から参照する。シングルトンは作らない。構築直後は `Default` のみを持ち、Game の起動時にアセットの根から `Data/Surfaces.json` を読む。ファイルを読めない場合は警告して `Default` のみで続行する。

`Assets/Data/Surfaces.json` は次の構造を持つ。添付の語彙は `Default` を含む 14 種。地形の層に使う `Soil`、`Gravel`、`Moss`、`Mud`、`Sand`、`Water` も同じ登録簿で引ける。

| 親 | 直接の子 |
|---|---|
| `Default` | `Grass_Tall`、`Dirt_Mud`、`Rock`、`Metal`、`Water_Shallow`、`Wood`、`Moss`、`Sand` |
| `Dirt_Mud` | `Soil`、`Mud` |
| `Rock` | `Concrete`、`Gravel` |
| `Water_Shallow` | `Water` |

```json
{
  "version": 1,
  "surfaces": [
    {
      "name": "Default",
      "flags": [],
      "audio": {"footstepSet": "default", "noiseLevelDb": -10, "transmissionLossDb": 0},
      "visual": {"footprint": "", "splash": "", "ripple": "", "grassTrample": 0, "wetFurTransfer": 0},
      "physical": {"friction": 0.6, "restitution": 0, "footSinkCm": 0}
    }
  ]
}
```

ルートは `version` と `surfaces`、各要素は `name`、`parent`、`flags`、`vegetationDensityThreshold`、`audio`、`visual`、`physical` を受け付ける。名前は 1〜64 バイトの英数字と `_`。`Default` は配列内の位置によらず添字 0 で、その親は省略、空文字列、または `Default` に限る。他の親の省略・空文字列は `Default` を意味する。フラグは上記の名前の配列。`audio` の数値は有限の `float` に収まる値、`visual` の `grassTrample`・`wetFurTransfer` と `physical` の `restitution` は 0〜1、`friction`・`footSinkCm` は 0 以上の有限の `float` に収まる値とする。テキストは最大 1 MiB、表面は引退した添字を含め最大 1024 個。

各表面の `vegetationDensityThreshold` は草の密度のしきい値で、省略時は 0.35。0〜1 の有限の数値を指定し、範囲を外れると `OutOfRange` で読み込みを拒否する。

`SurfaceRegistryLoadResult::Error` は、成功の `None` のほかに、次の `ESurfaceRegistryError` を返す。

- 読み取り・形式: `ReadFailed`、`Empty`、`TooLarge`、`ParseError`、`InvalidRoot`、`UnsupportedVersion`。
- キー・値: `UnknownKey`、`DuplicateKey`、`InvalidType`、`NonFiniteNumber`、`OutOfRange`、`UnknownFlag`。
- 名前・親・件数: `InvalidName`、`DuplicateName`、`UnknownParent`、`CyclicParent`、`InvalidDefault`、`TooManySurfaces`。

読み込みを拒否した場合は以前の内容を保ち、成功した場合だけ内容を入れ替えて `GetRevision()` を増やす。`FindByName` は未知の名前を `Default` とし、ログ有効時は空でない未知の名前のハッシュごとに警告を 1 回出す。成功した読み込みで警告の記録は消える。`TryFindByName` は警告せず、見つからなければ `false` と `Default` を返す。

`ResolveFallback` は、渡された条件を満たす最初の記述を対象から親へたどる。見つからなければ `Default` を返す。親の値を子へ自動的にコピーする機能ではない。無効または引退した添字は `Default` から探す。

読み込みは GameThread で行い、参照操作と同時に実行しない。参照操作どうしは並行して呼べる。返された参照は読み込み成功時または登録簿の破棄時に失効する。

## 表面の照会

`SceneQuery::QuerySurface` は所有スレッドで呼び、表面の種別を次の順に決める。

1. 有効なコライダーについて、接続済み `IPhysicsSceneQueryProvider::GetColliderSurface` が返す明示タグ。
2. `ISurfaceProvider` の寄与。優先度の高い順に問い合わせ、種別を返した最初の provider を使う。同じ優先度は登録順で、優先度の目安は地形 200、草 100。
3. 種別が見つからない場合は `Default`。

コライダーのタグ取得が `Success` 以外の場合は provider へ進む。provider の `SampleSurface` が `Success` 以外の場合、その寄与は飛ばす。`SurfaceQueryFlags::SkipCollider` はコライダーのタグを、`SkipProviders` は provider の照会を省略する。

provider が返す 2 層の重みは、負または非有限の値を 0 として和が 1 になるよう正規化し、重い方を `Primary` にする。`SecondaryWeight` は 0〜0.5、主の重みは `1 - SecondaryWeight`。副がない場合は `Secondary` が `Default`、重みが 0。2 層が同じ表面なら 1 層にまとめ、両方の重みが 0 の場合も副の重みは 0 とする。

状態の `Wetness01`、`PuddleDepthM`、`VegetationDensity01` は種別を返さない provider を含め、`Success` の全 provider の最大値を使う。濡れと草の密度は 0〜1、水たまりの深さは 0 以上に収め、非有限の状態値は 0 とする。明示タグで種別が決まった場合も provider の状態を集める。

戻り値は種別が決まれば `Success`、決まらなければ `NoHit`、位置・法線・半径が非有限または半径が負なら `InvalidArgument`、別スレッドなら `WrongThread`。`NoHit` でも provider の状態の寄与は返り得る。入力不正や別スレッドでは出力を初期化し、provider の例外では出力を初期化して例外を投げ直す。

`BindSurfaceProvider` と `UnbindSurfaceProvider` は所有スレッドで行う。`SceneQuery` は provider を借用し、登録側が所有して解除まで生かす。同一 provider の重複登録は `AlreadyBound`、空の登録簿からの解除は `Unavailable`、別の provider の解除は `ProviderMismatch`。照会中に provider の登録を変更しない。

## 地形と草の provider

`Engine::Engine` は `Scene/TerrainSurfaceProvider.h` の 2 つの provider をメンバとして持ち、コンストラクタで `SceneQuery` に結ぶ。デストラクタで両方の登録を解除する。

- `TerrainSurfaceProvider`（優先度 200）は照会のたびに `TerrainSystem::FindTerrain` で高さ場を探す。照会位置を囲む 4 頂点で層ごとの重みを双線形補間し、上位 2 層を返す。層番号に対応する表面名は仮の地形材質の記述（`MakeProvisionalTerrainMaterialDesc`）から取り、登録簿で解決する。地形の外では濡れの状態だけを返す。
- `VegetationSurfaceProvider`（優先度 100）は有効な草の標本の密度を `VegetationDensity01` に返す。`Grass_Tall` が登録され、密度がその `vegetationDensityThreshold` を超えたときに種別も返す。明示タグと地形の層がともに種別を返さない場所では `Grass_Tall` が選ばれる。地形の上の濃い草は、地形の種別を保ったまま密度で読める。

両方の provider は `ComputeGroundWetness01` で `Wetness01` を求める。環境が有効なら環境の濡れ、無効なら地形の濡れを基に、草の水気の偏りを反映する。草の標本がなければ偏りは中立の 0.5。CPU から水深を取得する手段がないため、両方とも `PuddleDepthM` は 0 を返す。

## 物理の結果

コライダーに明示した表面は、`PhysicsRaycastHit`、`PhysicsOverlapHit`、`PhysicsQueryHit` の `Surface` に入る。`RaycastClosest`、`RaycastAll`、`OverlapSphere`、`OverlapBox`、`OverlapCapsule`、`SweepSphere`、`SweepCapsule` と旧入口で共通とする。`PhysicsContactEvent` の `SelfSurface` は通知先、`OtherSurface` は相手のコライダーの表面を表す。いずれも明示なしなら `Default` を返す。

`ColliderComponent` は `SurfaceName` を `PROPERTY` として持ち、シーンには名前を保存する。`Initialize` で登録するときに名前を解決する。登録後は `SetSurface` で名前を設定したときと、シーン編集などで `SurfaceName` を適用したとき（`OnPropertyApplied`）に解決し直す。`GetSurface` はその時点で新しい値を返す。空の名前は明示を外す。名前の形式が不正なら `SetSurface` は `InvalidArgument` を返し、旧値を保つ。未知の名前は保持し、`FindByName` が `Default` を返す。ログ有効時の警告は登録簿の読み直しまで名前ごとに 1 回だけ出る。

登録簿の `GetRevision()` が変わると、次の `StepFixed` または `RefreshDynamicSnapshot` のうち、先に実行された方で名前を解決し直す。物理クエリは次のスナップショットの公開から新しい表面を返す。`GetColliderSurface` は公開スナップショットを参照する。明示なしなら `NoHit` を返し、`SceneQuery::QuerySurface` は provider へ進む。`Default` を明示した場合は `Success` を返す。

Entity を持たない静的コライダーでは、物理モジュールが `StaticColliderDesc::SurfaceName` の名前を保持し、チャンクの適用時に解決する。登録簿の改訂を検出したときも、同じ `ColliderHandle` のまま解決し直す。名前が空なら `Surface` に登録簿の添字、`bHasSurface` に明示の有無を渡す。`norves.static-colliders.v1` の `"surface"` は、文字列なら登録簿の名前、整数（0〜255）なら従来の地形の層番号を指定する。層番号は結果の `uint8_t SurfaceId` にだけ写す。

`Hit` と `OverlapBegin` は今のステップの proxy の表面を返す。`OverlapEnd` は両側を個別に扱い、その側の proxy が今のステップにもあれば現在の表面を、なければ最後に重なっていたときの表面を返す。

## 当たりのエフェクト

`HitEffectEmitter` はまず表面名を対応表と完全一致で照合する。`Engine` は `HitEffectEmitter::SetSurfaceRegistry` で登録簿を渡すため、名前が表になければ登録簿の `parent` を順にたどり、最初に一致した親のエフェクトを使う。登録簿を渡さないエミッターは完全一致だけを使う。どちらも一致がなければ表の既定のエフェクトを使う。

## 足ごとの接地

`Component/GroundContactComponent.h` の `GroundContactState` は、最大 4 本の足の結果 `Foot[4]`、体の結果 `Body`、足の数 `FootCount`、公開済みの物理スナップショットの番号 `StepSerial` を持つ。`GroundContactComponent` は PostPhysics 群で、新しい番号を読んだときだけ、体と各足から下向きに `RaycastClosest` を行い、当たった位置・法線・コライダーで `QuerySurface` を呼ぶ。足 IK はレイの結果を公開していないため、各足に専用のレイを 1 本使う。`SetIgnoredColliders` と `SetIgnoredBodies` で除外対象を指定でき、自分の Entity のコライダーに当たった場合も自動で除外する。

足音（再生は GR75）・足跡・草の踏み倒し（GR44）の消費者は、実装時にこの状態を読む。犬の足の水しぶき（`Component/FootSplashComponent.cpp`）は表面の種別を判定せず、アニメーションの足の接地通知を受けた瞬間に、自前の `QueryGround`、環境の濡れ（無効なら地形の濡れ）、地形の `SamplePuddlePotential` で放出量を決める。`GroundContactState` の更新は物理の固定ステップ基準で通知と時点が異なり、`Wetness01` には草の水気の偏りも入る。水しぶきの時点と量の式を保つため、ここでは従来の照会を使う。

## 後の段階

- 足音の再生（GR75）。
- 取り込んだ材質からの表面の継承（GR81）。
- 濡れた毛への結線。
- スクリプトへの公開。
