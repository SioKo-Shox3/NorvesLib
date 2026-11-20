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

登録簿の `SurfaceId` と、地形の `GroundSample::SurfaceId`（`uint8_t` の層番号）、物理の結果の `uint8_t SurfaceId` は別物であり、暗黙に変換しない。地形の層から表面へは `TerrainMaterialDesc` の `Layers` と各層の `SurfaceName` の表を介する。番号を表面へ橋渡しする場所は、今後実装する地形の provider と物理の表面の受け渡しの 2 か所に限る。

保存するのは `String` の名前であり、`SurfaceId` の数値は保存しない。添字は読み込みで決まる。再読み込みでは同じ名前の添字を保ち、消えた名前は添字を再利用せず引退させる。引退した添字の `GetDesc` は `Default` の記述を返し、`GetName` は元の名前を返す。同じ名前が復帰すれば元の添字を使う。

## 登録簿とデータ

`SurfaceRegistry` は `Engine::Engine` のメンバで、`GetSurfaceRegistry()` から参照する。シングルトンは作らない。構築直後は `Default` のみを持ち、Game の起動時にアセットの根から `Data/Surfaces.json` を読む。ファイルを読めない場合は警告して `Default` のみで続行する。

`Assets/Data/Surfaces.json` は次の構造を持つ。添付の語彙は `Default`、`Grass_Tall`、`Dirt_Mud`、`Rock`、`Concrete`、`Metal`、`Water_Shallow`、`Wood` の 8 種で、`Concrete` の親は `Rock`、それ以外の非 `Default` の親は `Default`。

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

ルートは `version` と `surfaces`、各要素は `name`、`parent`、`flags`、`audio`、`visual`、`physical` を受け付ける。名前は 1〜64 バイトの英数字と `_`。`Default` は配列内の位置によらず添字 0 で、その親は省略、空文字列、または `Default` に限る。他の親の省略・空文字列は `Default` を意味する。フラグは上記の名前の配列。`audio` の数値は有限の `float` に収まる値、`visual` の `grassTrample`・`wetFurTransfer` と `physical` の `restitution` は 0〜1、`friction`・`footSinkCm` は 0 以上の有限の `float` に収まる値とする。テキストは最大 1 MiB、表面は引退した添字を含め最大 1024 個。

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

## 物理の結果

`PhysicsRaycastHit`、`PhysicsOverlapHit`、`PhysicsQueryHit` は従来の `uint8_t SurfaceId` に加え、登録簿の `SurfaceId` 型の `Surface` を持つ。`Surface` は将来コライダーの明示表面を渡す欄であり、現在は常に `Default`。

## 後の段階

- コライダーの `SurfaceName` の PROPERTY と、物理の結果への表面の受け渡し。
- 地形・草・濡れの provider と、足音・足跡などの消費者。
- スクリプトへの公開。
