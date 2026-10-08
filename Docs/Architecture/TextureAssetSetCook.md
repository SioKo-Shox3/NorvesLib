# Native texture asset-set v1（G2-S6 / GR96）

AssetCook --asset-set spec.json --runtime-root new-output [--source-root source-directory] [--manifest new-output/custom.json]

texture v1の全件cookを行う最初の実行境界。SourceRoot省略時は起動時cwd。外側CLIの相対pathはcwd基準、spec内source_pathはSourceRoot基準。manifest省略時はRuntimeRoot/manifest.json。single-cook、inspect、sidecar、skip等のflag混在は拒否する。

## 公開契約

RuntimeRootは存在してはならない。空directory、file、junctionも拒否する。親directoryは既存で、ローカルdrive上の通常directoryが必要。出力root/manifestは現在のAssetSystemと同じASCII境界に限定し、UNC・reparse祖先・Windows予約device名・禁止文字・file/directory親子衝突を拒否する。source_pathはUTF8からnative pathへ変換し、存在とreparseを先行検査する。CLI引数の文字コードがUTF8でない場合も黙って読み替えない。

全preflight後、出力rootの隣に排他的なstage directoryを作り、既存CookSingleAssetでspec順にtextureをcookする。各単体manifestを再読込して参照を値所有し、最後に集約を書き込む。元のtexture cooker、source hash、package writerは変更しない。

集約はdisk byte再読込、全参照のcooked-only解決、texture payload parse、生成file数を検証する。公開は同volumeのMoveFileExW（REPLACE_EXISTING/COPY_ALLOWED無し）で行い、競合して現れた宛先を置換しない。これはdirectoryの原子的な可視化であり、電源断時の永続化保証ではない。

失敗時は公開せず、この呼出しが排他作成したstageだけを片付ける。削除できなければ場所を診断へ残す。クラッシュで残った他のstageを次回の処理が自動削除しない。

## PS5.1との互換

固定した旧2specの10生成物を生byte比較する。manifestのkey順、spec順、CRLF、indent、colon後空白2つ、BOM無し、末尾改行無しを専用serializerで保つ。既存BuildMergedManifestJsonは整形とsortが異なるため利用しない。

ASCII quote/backslashとapostrophe/山括弧/ampersandをPS5.1に合わせてescapeする。実Windows PowerShell5.1をhash固定し、全printable ASCIIをvariantに持つ実cook結果を同じConvertTo-Jsonで再生成して全byte比較する。基準: [PS5.1 ConvertTo-Json](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/convertto-json?view=powershell-5.1)、[Microsoft HttpEncoder](https://github.com/microsoft/referencesource/blob/main/System.Web/Util/HttpEncoder.cs)。

## 検証と次の境界

Windows CIで13CPU、既存7CLI/79出力一致、新規2spec各2回/10出力一致、ASCII probe、default/explicit SourceRoot、custom manifest、late画像不正、予約名、prefix衝突、既存root、junction、公開時競合を検証する。実CI前の純試験は実cook成功の代わりにしない。

既存RuntimeRootの置換・増分印・model/raw/audio batch・jobs/budget・CMakeのproduction caller切替はこの段階では未対応。次工程で既存出力の置換/復旧契約とC++中央の増分判定を追加し、glTF外部buffer/imageおよびsidecarの内容・不在状態を印へ含める。texture v1互換だけでGR96全体を完了にしない。非ASCIIのCLI/output rootも別の実Windows検証が必要。

ローカルdrive判定はdrive letterの表記だけでなくGetDriveTypeWで実種別を確認し、mapped network driveと不明なrootを拒否する。textureの診断用source名もnative pathからUTF8へ変換し、JSON内のUnicode filenameとUnicode cwdを実Windowsで検証する。CLIのnarrow argv文字コード制約は別に残る。

stage/親directoryを外部の悪意あるprocessが実行中に差し替えない、信頼された作業領域を前提とする。pathによるreparse検査は敵対的な同時substitutionへの完全な保護ではない。

2026-10-04実受入: commit79d019d8ae5dba5a27e23cd2cfac97437e84aacc / run37210701091で13CPU、単体7CLI/79出力、native2spec各2回、PS5.1全printable ASCII probeが成功。保存native artifact11306368465の10file296408686byteは旧PS1基準と全byte一致。25回のnative起動（9成功/16意図した拒否）に日本語source/cwd、junction、late画像不正、既存root・prefix衝突を含む。受入範囲は新規root v1に限る。
