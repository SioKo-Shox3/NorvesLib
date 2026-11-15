# 単体cookサービスとCLI境界（G2-S6）

Mainはargv/help/inspect/終了コードの外殻を持ち、CookSingleAssetがraw/texture/model/skeletal/audioの既存cook・出力検証・manifest/cache処理を呼ぶ。AssetCookOutputは従来のpackage/manifest/IOを持つ。

## 型の境界

公開SingleAssetCookRequestは独自AnsiString、filesystem path、公開骨格decode値、sidecar/skipの値だけを持ち、Core private headerに依存しない。CLIの明示指定有無の相互検査はCLI側で従来通り行い、サービスには正規化済みの値を渡す。

旧CookOptionsとstd文字列/vectorを用いるbodyはprivate互換境界に機械的に移動する。今回だけで全cookerの型体系を変更せず、新しい公開APIや後続batchコードへstdコンテナを広げない。auto引数は既存の実具体化（std::string error、AnsiString entryName）を宣言可能にした。template宣言だけを別TUに残さない。

共有ValidateCookOptionsは旧CLIの最後の検証bodyであり、argv明示flagを保ったCLI検査と、正規化値を受けるサービス境界の双方で使用する。request/legacy変換でpath・文字列・sidecar・skip・decode値を所有copyする。

## 互換性

- package header/alignment/hash/FourCC/コピー順、manifestのkey順/空白/改行/metadataを変更しない
- raw/texture/modelの単体manifestとaudio/embedded modelのmerged manifestは統合しない
- GLB画像viewは同じcook呼出し内の入力bufferを出力完了まで借用し、外へ返さない
- sidecar保護・alias検査・書込前後validation・診断streamを維持する
- --asset-set、一括manifest serialization、新しい増分判定は別工程

## 診断とmetadata

旧Mainの日本語診断5件は、BOM追加でMSVCの文字集合変換が変わらないよう既存UTF-8 byteをASCII escapeで固定する。共有metadata構造体はprivate headerに完全定義を置く。

2026-10-04受入: commit e8ac121ce8d2e9b92a5469ec321adb00b8a92ff3 / run37203280358で実Windows Release build、11CPU、7CLI、79生成物の全byte一致と診断5literal一致を確認した。candidate artifact11304570033のZIP SHA256は6c3e6c53e058c4c50c6a9dda4d4319d54730a2fb3912322789e9cab6974443a3。--asset-setと増分依存はこの受入に含まない。
