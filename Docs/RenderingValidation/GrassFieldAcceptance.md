# 草の場の容量受入れ記録

本記録は grassland の設定による容量の見積もりと、cook 済み出力の実測を残す。
容量のしきい値による合否ゲートは設けない。常駐量はコードから求めた CPU データの内訳であり、
プロセスのワーキングセットやアロケータの実測値ではない。1 MiB = 1,048,576 B とする。

## 形式から求める容量

[NVGFLD v0](../Architecture/NVGFLDv0.md) と
[CookedGrassFieldFormat.h](../../Library/Core/Public/Asset/CookedGrassFieldFormat.h) を根拠とする。
チャンクの一辺を C m、LayerA / LayerB の一辺の解像度を RA / RB、
保存する空でないチャンク数を N、種別数を S とすると、RGBA8 の各画素は 4 B なので次の式になる。

```text
チャンク当たりの LayerA = 4 × RA²
チャンク当たりの LayerB = 4 × RB²
ペイロード P = N × 4 × (RA² + RB²)
チャンク表 = 48 × N
種別名表 = 64 × S
NVGFLD ファイル F = 128 + 48 × N + 64 × S + P
保存チャンクの面積 = N × C²
矩形の範囲 W m × H m を覆う場合の N = ceil(W / C) × ceil(H / C)
```

ヘッダ・チャンク表・種別名表の大きさはすべて 16 の倍数なので、v0 の通常の書き出しでは
ペイロード前の整列に追加バイトを要しない。空チャンクを省く場合は、実際に保存する N で計算する。
種別名は長さにかかわらず 1 件 64 B、最大 64 種。上の F はパッケージ外の NVGFLD 自体の大きさである。

[GrasslandGrassField.json](../../Assets/Vegetation/GrasslandGrassField.json) は
C = 32、RA = 64、RB = 32。種別名表は grassland の cook 済み出力と同じ S = 2 とする。
LayerA は 16,384 B、LayerB は 4,096 B、合計 20,480 B / チャンクとなる。

## 1 km² 相当の cook 済み容量

1,000 m 四方を欠けなく覆い、全チャンクが空でない場合を採る。
一辺は ceil(1,000 / 32) = 32 チャンクで、N = 1,024。
実際の外形は 1,024 m 四方、1,048,576 m² = 1.048576 km² となる。
厳密な 1 km² より面積が 4.8576% 大きい、チャンク単位の見積もりである。

| 内訳 | 大きさ |
| --- | ---: |
| LayerA 画素 | 16,777,216 B（16 MiB） |
| LayerB 画素 | 4,194,304 B（4 MiB） |
| ペイロード P | 20,971,520 B（20 MiB） |
| チャンク表 | 49,152 B |
| 種別名表 | 128 B |
| ヘッダ | 128 B |
| NVGFLD ファイル F | 21,020,928 B（約 20.047119 MiB） |
| 同じ entry 名・パッケージ配置で 1 entry に包む場合 | 21,021,136 B（約 20.047318 MiB） |

最後の行は grassland の実測と同じ 208 B の包みを加えた見積もりで、1 km² の実ファイルを生成して測った値ではない。
entry 名・件数・整列条件が変わるパッケージにはこの 208 B を一般化しない。

## 実行時の保持と読み込み中の一時領域

[GrassFieldComponent::BeginPlay](../../Library/Core/Private/Component/GrassFieldComponent.cpp) は
AssetSystem で cook 済みの場を解決し、ParseCookedGrassField で解析する。
続いて種別 JSON を読み、BuildGrassFieldFromCooked で実行時の場を作り、Vegetation に登録して描画データを公開する。

| 所有・用途 | 寿命と容量の扱い |
| --- | --- |
| AssetFileReader の読み込みバッファ | .nvpkg 全体を読む。ResolveAsset 内の一時領域。上の 1 entry 条件では約 20.047318 MiB。 |
| Package::LoadFromMemory のバッファ | 読み込んだパッケージ全体をさらにコピーする。読み込み元と同時に生存する区間は 2 個で 42,042,272 B（約 40.094635 MiB）。 |
| resolved.Blob / parsed.Field.Source | OpenEntry はコピー済みパッケージ内の範囲を共有し、entry 分だけの新たな画素コピーは作らない。解析結果がパッケージ全体の所有者を保持し、BeginPlay 終了まで生存する。 |
| 解析したチャンク表・種別名 | CookedGrassFieldData の配列と文字列。BeginPlay 内の一時領域。ディスク上の表を実行時オブジェクトへ解析するため、割当量を単純に 48N + 64S と同一視しない。 |
| 展開後の GrassField | SetChunk が各チャンクの LayerA / LayerB を別の所有配列へコピーする。画素の有効データは P = 20 MiB。m_Field が EndPlay まで所有する。cook 済み Source は保持しない。 |
| 描画用 GrassFieldRenderData | BuildRenderData が全チャンクの両レイヤを連結配列へコピーする。画素 P = 20 MiB に加えて、チャンク記録 16N = 16,384 B と種別データ 128S = 256 B を持つ。 |
| その他の常駐 | GrassField のチャンクマップのキー・ノード・バケット、各配列の管理情報と余剰容量、種別表と名前、場の設定・リビジョン・既定地面、Component のパス・ポインタ類と TerrainGroundHeightSource。 |

[AssetSystem::ResolveAsset](../../Library/Core/Private/Asset/AssetSystem.cpp) はパッケージをメンバにキャッシュしない。
[Package::LoadFromMemory / OpenEntry](../../Library/Core/Private/FileStream/Package.cpp) のコピーと共有所有により、
読み込み元バッファは ResolveAsset の終了時に解放され、コピー側は最後の blob が手放されるまで残る。
BeginPlay の終わりには resolved と parsed が破棄されるため、この Component の経路ではパッケージのバイト列は常駐しない。
種別 JSON の blob・JSON 文字列、読み込み用と並べ替え用の種別表、描画公開時のチャンクポインタのソート配列も一時領域である。

[BuildGrassFieldFromCooked](../../Library/Core/Private/Vegetation/GrassFieldAsset.cpp) と
[GrassField::SetChunk / BuildRenderData](../../Library/Core/Private/Vegetation/GrassField.cpp) によると、
RGBA8 は圧縮解除して浮動小数点画素へ変換するのではなく、そのまま実行時配列へコピーされる。
LayerB.A はコピー先で 0 にする。種別表は cook 済み種別名の順に複製し、見つからない種別には既定値を補う。
地面は TerrainSystem を借用するアダプタで、草の場のために地形全体を複製しない。

[Vegetation::PublishRenderData](../../Library/Core/Private/Vegetation/Vegetation.cpp) が公開した描画データは、
RenderingCoordinator と FramePacket が TSharedPtr<const GrassFieldRenderData> で共有する。
同じ公開データをフレームごとに全量複製する構造ではない。内容が変わらなければ再構築を省略する。
場を更新した場合は、処理中のフレームが以前の描画データを保持する間、新旧のコピーが重なり得る。

変更のない場を 1 個登録した定常状態では、CPU の画素の有効データは正本と描画用の合計 40 MiB。
描画用のチャンク記録と種別データまで含めた計算可能な部分は 41,959,680 B（約 40.015869 MiB）となる。
これは実割当量の下限となる内訳であり、上表の「その他の常駐」と配列の余剰容量・アロケータ管理領域を加える必要がある。
GPU 資源、草ブレードの生成先、地形やエンジン全体の使用量を含めたプロセス総量はここでは測定していない。

読み込みの最大使用量も定常時の 40 MiB とは異なる。
ResolveAsset 内でパッケージ 2 個が重なる区間と、BeginPlay 内でパッケージ 1 個・解析表・
実行時画素 P・描画用画素 P が重なる区間を区別する。
後者の主要バイト列だけなら 62,964,176 B（約 60.047318 MiB）で、さらに解析表・管理情報・余剰容量などが加わる。
すべての一時領域が同時に最大になるとは限らないため、これらを足して実測ピークとは扱わない。

EndPlay は登録を解除し、空の描画データを公開してから m_Field と m_GroundSource を破棄する。
公開済みの古い描画データは、それを参照するフレームの寿命が終わるまで残り得る。

## grassland の cook 済み実測

| 項目 | 実測値 |
| --- | ---: |
| チャンク数・配置 | 256（16 × 16） |
| 外形・面積 | 512 m 四方、262,144 m² = 0.262144 km²（約 0.262 km²） |
| ペイロード | 5,242,880 B（5 MiB） |
| NVGFLD | 5,255,424 B |
| .nvpkg | 5,255,632 B |

NVGFLD は 128 + 48 × 256 + 64 × 2 + 5,242,880 = 5,255,424 B と一致する。
この表は cook 済みファイルの実測であり、実行中の常駐メモリの実測ではない。

## entry の分割方針と未確認事項

AssetSystem はパッケージ全体を読み、Package が収録 entry のハッシュを検査し、
ResolveAsset が選んだ entry のハッシュを再確認する。草の場の解析でもペイロードのハッシュを確認する。
NVGFLD v0 は entry 内の場全体を読み込むため、谷 1 つ（数 MB〜数十 MB）を超える場は領域ごとの entry に分ける方針とする。
本記録の見積もりは 1 entry。entry を同じ巨大パッケージに集めたままではパッケージ全体読み込みの負担が残るため、
パッケージの分け方も合わせて考える必要がある。容量の記録を合否ゲートにはしない。

起動による確認は保留で、見た目とシーン退出時の動作は未検証。
Debug または RelWithDebInfo の Game を `--scene=grassland --window-background` で開き、
草の場の読み込み・登録に関するログ、登録後のチャンク数・種別順、EndPlay での解除と再入場、
地形との高さの整合と草原の見た目を確認する。
現在の GrassFieldComponent / Vegetation には登録成功・解除成功を直接出すログがないため、
`GRASS_FIELD_COMPONENT_SKIPPED` 等の失敗ログがないことだけで成功とは判定しない。
成功を観測できる記録を用意したうえで、登録・解除の実動作を確認する必要がある。

## ビルド・CPU テストの確認

2026-11-05 に Debug で実行した。既存の build はこの作業ツリーを参照し、
NORVES_BUILD_TESTS=ON だったため、追加の configure は行っていない。
ビルドは各ターゲットを `cmake --build build --config Debug --target <target> -- /m:1` で実行した。

| ターゲット | 終了コード |
| --- | ---: |
| AssetCook | 0 |
| CookedMeshTest | 0 |
| SceneSerializerTest | 0 |
| Game | 0 |
| AssetSystemTest | 0 |
| MathGeometryTest | 0 |

AssetManifestTest と GrassFieldQueryTest の実行ファイルが未生成だったため、
それぞれの所属先である AssetSystemTest と MathGeometryTest もビルドした。

```powershell
ctest --test-dir build -C Debug -R "^(CookedGrassFieldTest|AssetCookGrassFieldSmoke|SceneFileRoundTripTest|GrassFieldQueryTest|CookedTerrainTest|AssetManifestTest)$" --no-tests=error --output-on-failure --timeout 600
```

```text
100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.35 sec
EXIT_CODE: 0
```

6 件とも Passed。
この結果はビルドと CPU テストの成功を示し、上記の起動確認・見た目・メモリ使用量の実測は含まない。
