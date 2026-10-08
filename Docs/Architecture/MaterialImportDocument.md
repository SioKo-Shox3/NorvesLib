# 元材質付きimport-plan（GR78 / GR79）

幾何ImportSettings（canonical52B）はそのままに、ImportSettingsDocumentが材質設定を値所有する。LoadImportSettingsDocumentは既存file入口と同じ最大1MiB/BOM/不在/失敗保持規則を使う。旧ParseSettingsとLoadImportSettingsFileは幾何-onlyの受理を維持し、新設定を黙って捨てない。

## JSON

- root.material: profile、arm、doubleSided、alphaMode、emissiveNitsPerUnitの資産共通設定
- root.materials[]: 素材selectorと同じ材質layer、任意surface
- selectorはname（非空で一意の元名）か、indexとexpectedName（明示必須、元が無名なら空文字）の組
- name/indexの併記、indexだけ、expectedNameだけ、重複field/未知field/不正値を拒否
- 素材単位profileを拒否。repair/lod/collision/clipの未対応設定の拒否も維持
- surfaceは非空の厳密UTF8名。SurfaceRegistryや存在しない名前のDefault解決はこの段階で捏造しない

## 所有と解決

SourceMaterialCatalogはglTF materials配列の元indexと元名だけを所有する。生成slot名から復元せず、material省略primitiveを仮想index0にしない。省略primitive向けのImplicitDefaultはplanで別に保持する。

ResolveMaterialImportPlanはARM/発光/surfaceの全selectorを1回のResolveMaterialSelectionへ渡す。同じ材質への異なる行の設定は、設定種別が違っていても二重targetとして拒否する。全行一致後、素材＞資産＞asset-set（発光換算）の優先順位を解決する。発光source値が未取得なので、換算不在だけではまだ拒否しない。

出力planは元index順で、元名・材質設定・SurfaceNameを深く所有する。JSON/document/catalogの破棄後も有効。失敗結果ではoutを更新しない。catalog行順と元index順を区別し、重複名の診断indexも元番号へ変換する。

重複名は全catalogから検出し、同名の入替は見分けられない。cook接続側が資産名付きで元データの改名を促す警告を出すため、group数と最初の組の元indexをplanに保持する。

## 未完の接続

このAPIは正式な値所有import-planまで。既存v0 cook/loose/NVSKEL0.2/CLIを新設定対応へ変更していない。次のv1 cook/fingerprintが同じplanを使い、未一致をcache hitで迂回させない。

SurfaceNameの正式保存・適用先はGR81のColliderComponent/PhysicsShapeProxy。保存先ができる前にCLI surface指定を受理して捨てたり、診断reportだけでruntime対応と扱ったりしない。Material128/NVSKEL予約を転用しない。

## 検証

MaterialImportDocumentTestは元名無し/Material_0、名前・index混在の二重target、曖昧/未一致/期待名不一致、Unicode raw/escape、catalog行入替、JSON寿命後の所有、layer優先順位、surface所有、失敗保持、sidecarのBOMと旧loader拒否を実JsonDocumentで検証する。Windows CIでの実行前にnative合格とはしない。
