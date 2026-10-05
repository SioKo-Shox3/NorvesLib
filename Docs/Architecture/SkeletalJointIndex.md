# 厳密な関節名索引（G2 / GR84）

## 用途と既存経路

Private/AnimationのSkeletalJointIndexは、Core/AssetCookの厳密consumer向けに名前と元配列番号だけを所有する値型。SkeletonResourceのIdentity索引・空名を登録しない動作・重複名の先頭勝ち、Sampler、decoder/writer、wire、CLIは変更しない。BoneMapのJSON、関節間の対応、階層/rest互換、retargetはこの索引の成功で保証しない。

BVH側は既存UTF8名からSkeletalJointNameViewを渡す。target側はBuildSkeletalJointIndexFromJointsがnative TCHARのNameだけを読み、既存MeasureSkeletalNameEncoding/EncodeSkeletalWireNameのminor2を使ってUTF8へ変換する。親番号・inverse bind・Resourceの状態を索引へ持ち込まない。

## 一致と所有

名前は非空・妥当UTF8・NULなし・完全byte一致で一意であることを要求する。比較はunsigned byte列と長さで、hashだけの一致、大小文字統合、namespace除去、Unicode正規化、自動改名を行わない。Root/root、prefix、colon、合成済み/分解済みアクセントは別名となる。targetへBVHのsingle-token制約は適用せず、空白や改行もminor2 codecが許す名前として比較できる。

索引はUTF8 poolとoffset/length/元番号を所有し、名前順に整列する。検索で返す番号は整列後の行ではなく入力の元joint番号。入力を変更・破棄しても保持する。copy assignmentはcopy-and-swap、moveは元を空の有効状態に戻す。構築失敗と確保例外は旧outを保持し、検索失敗はoutIndexを変更しない。空の既定/ムーブ元索引への有効な名前検索はNotFoundとなる。

既定の上限はjoint 1024、個別名4096 UTF8 byte、総名1MiB。callerが明示変更できる。個別/合計のu32境界、count/加算/コンテナ上限を確保前に検査し、検索名にも構築時の個別上限を使う。nativeのcode-unit数と保存byte数を区別する。native adapterは検証後の一時UTF8配列から共通builderへ渡すため、名前bytesには一時的に二つの所有copyが存在する。

コピーした索引は名前のsnapshotであり、元骨格の変更を自動追従しない。元の順序が変わるならcallerが索引を再構築する。新しい骨格やrest poseへの適合性は別途検証する。

## 検証

SkeletalJointIndexTestは元配列順、UTF8/UTF16/UTF32/native TCHARと非BMP、case/prefix/colon/正規化差、空白/改行、空/NUL/不正encoding/重複/未知名、各上限の境界・超過、不正span、入力の破棄、copy/move/self-assignment、失敗時保持をliteralで検証する。CookedMeshTestのMEMBERで、実Windowsの結果はPROGRESS.mdに記録する。

既存runtimeを変更しないことはsource byte保全と既存89出力の直接比較で維持する。この単位でSamplerを新しく実行したとの主張はしない。Samplerのplain API共有・pose同値確認、BoneMap、retarget、Stage Bの作者時rest snapshotは残件である。確保故障注入は未実施で、局所構築とnothrow swapによる保持を静的に固定する。
