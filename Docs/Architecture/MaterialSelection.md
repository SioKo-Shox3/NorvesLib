# 材質識別の共通契約（GR79 / GR78 / GR32）

ResolveMaterialSelection を唯一の照合処理とする。共通核に加え、GR32 の SkinnedMeshComponent::FindMaterialSlot を接続した。GR79 の ARM/発光設定と GR78 の SurfaceName への接続は後続工程。

- SourceMaterial: glTF materials 配列の元番号と元名。primitive の初出順や生成 slot 名から復元しない
- GeneratedSlot: runtime が保持する slot 番号と slot 名。元材質番号とは別の catalog
- catalog は 0..N-1 の identity を各1回含む。行順は自由で、出力は identity 番号でなく catalog の行番号
- 一意の非空元名は順番の変更に追従する。名前変更・不在・同名の曖昧性は拒否
- 番号指定には expectedName が必須。元が無名なら明示した空文字を許可する。TRELLIS.2 / Pixal3D の無名1材質と Blender 経由の Material_0 は同じ名前とは扱わない
- 名前と番号、名前同士、番号同士のどれでも同じ材質への二重指定を拒否する。設定値が等しくても拒否
- strict UTF-8 の全 byte を比較する。NUL、不正 UTF-8 は拒否。trim・大文字小文字・Unicode 正規化は行わない
- GeneratedSlot の一意の空名は既存 runtime の表現互換のため許可する。元材質の空名選択には使わない

## 重複名の警告

全 catalog（未使用の元材質を含む）で重複名群の数と最初の組の行番号を返す。複数の無名材質も重複である。名前選択を使わなくても警告情報を取得できる。同名材質同士の入れ替えは番号 + expectedName でも検出できない。

呼出側は GLB に同名材質がある場合、資産名を添えて「同名材質の入れ替わりは検出できません。元データで材質名を一意に付け直してください」と警告する。核はログ・資産名を所有しないため、警告の実表示は接続側の責任。

## メモリと失敗

scratch は N + 2S 個の uint32_t。入力名を含む全借用領域と書込領域の重複を拒否し、行番号出力は全件成功時だけ更新する。scratch は失敗時も変更し得る。出力の未使用末尾は保持する。呼出中は入力の寿命を保持し、並行変更してはならない。

計算量は catalog の名前順ソート、名前の二分探索、選択結果のソートを含む。番号から行への表は同じ scratch を再利用する。選択0件でも catalog と重複名を検査する。元材質0件を仮想 material 0 と扱わない。

## 検証範囲

MaterialSelectionTest を standalone と Core test bundle に登録。Linux の実共通核・実名前 codec による通常/O2 NDEBUG/ASan+UBSan（LSan除外）は検証対象。Windows Core 全体、JSON 入口、cook/runtime 接続、実 GLB 警告表示はこのテストの成功に含めない。

独立の総当たり型参照実装とのランダム比較で、成功判定・行出力・二重指定・失敗保持を確認。範囲外指定で直前の成功行が診断へ残る問題を修正し、未一致の CatalogRow は UINT32_MAX とする回帰を追加した。

## GR32 の slot 名 adapter

FindMaterialSlot は全 slot 名と検索名を native 文字幅から厳密UTF-8へ変換し、GeneratedSlot catalogを共通resolverへ渡す。旧 callback 型の独立した一致判定は廃止し、FindSkeletalMaterialSlot が成功行を slot identity に変換する。番号指定の SetSlotMaterial は従来通り slot 番号を受け取る。

旧meshの空 slot 表は Default 1件として扱う。一意の明示空名は保持する。同名、NUL入り検索名、不正Unicodeは -1 となり、名前指定の材質設定も失敗する。骨格wireと素材overrideの世代/fallback規則は変更しない。

公開helper FindUniqueSkeletalMaterialSlot(count, callback) は廃止した。リポ外で直接利用していた場合は、UTF-8 catalog を受ける FindSkeletalMaterialSlot へ移行して再ビルドする。component の公開 FindMaterialSlot / SetSlotMaterial の署名は維持する。
