# BVH生データの解析（G2 / GR84）

## 接続範囲

BvhDecodeは入力byte列をBvhDocumentへ独立所有するpure parserである。ファイルI/O、骨格Resource、回転行列、リターゲット、軸や単位の変換、fps上書き、RootMotion、ループ化、NVSKELとAssetCookの接続は含めない。角度は元のdegree値、OFFSETと位置値は元の単位、Frame Timeは元の秒のまま保持する。

Jointは宣言順、親番号はrootだけUINT32_MAX。名前は厳密UTF8 byte列で、同名は拒否する。名前の正規化・大小文字統合・自動改名をしない。CHANNELSの0〜6個の既知成分を宣言順でglobal配列へ保持し、同一関節内の重複は拒否する。root以外の位置channelと部分回転も捨てない。変換可能性は後続の契約で判定する。

End Siteはjointごとに最大1個のOFFSETとして保存する。関節数やchannel数へ加算せず、End Siteが無い場合に仮の末端を補完しない。

## 入力profile

- HIERARCHY、1ROOT、MOTIONの順。ROOT/JOINTはOFFSETとCHANNELSを各1回、この順で持つ
- キーワードとchannel名はASCII大小文字を区別せず、joint名は区別する
- 名前は非空のsingle token。colonは保持する。空白/brace/二重引用符を含む名前、named End Site、comment、複数ROOT、未知directiveは対象外
- 空白はspace/tab/CR/LF。先頭UTF8 BOMだけ許す。UTF8不正、NUL、その他C0/C1制御文字、途中BOMは拒否する
- Frames: N / Frames : N、Frame Time: dt / Frame Time : dtを許す。Framesは1以上、全channel数も1以上
- MOTIONは非空行1本が1frame。行幅は全channel数と厳密一致し、折返しを数え合わせて救済しない。空行・末尾改行なしは許す
- 最終frame以後はASCII空白だけ。余分なframeや文字はTrailingInput

数値はlocale非依存のdecimal、小数、e/E指数をtoken全体で解析する。浮動値は先頭の+/-を許し、有限doubleだけを保持する。hex、NaN/Inf、overflow/underflow、部分変換を拒否する。Frame Timeは正で、(Frames-1)×Frame Timeも有限であることを要求する。

## 所有と失敗

DecodeBvhは全成功時だけoutを置換する。入力の寿命から独立し、copy/move後も値と名前を保持する。解析失敗と確保例外では以前のoutを保持し、確保例外は伝播する。返却値はstatusと入力byte位置。失敗位置のLine/Columnは1始まり、ColumnはUTF8 byte単位、CRLFは1改行。成功時Line/Columnは0。

## 安全上限

BvhDecodeLimitsの既定は入力64MiB、joint 1024、joint深さ256、frame 1,000,000、double値2^23個、名前512byte、数値token128byte。count/積/byte量を確保前に検査し、階層は明示stackで辿る。呼出側は上限を調整できるが、保存型の32bit上限とsize_tの積の検査は常に行う。NVSKELの128/256関節制限はここへ持ち込まず、骨格へ変換する境界で別に検証する。

## 検証

BvhDecodeTestは6回転順、位置/回転の混在順、JOINT位置、静止関節、分岐、End Site、UTF8/BOM/CRLF/tab、指数、所有、失敗時保持、257関節/深い階層、各上限の境界と超過、数値/構文/行幅/末尾の拒否をliteral期待値で検証する。CookedMeshTestのMEMBERで、新規exeは作らない。nativeの受入れcommit/runはPROGRESS.mdに記録する。

宣言順を保持する試験は回転行列の正しさを証明しない。GR84のZYX/XYZ行列1e-9、retarget/FK、Blenderとの実物照合、fps補正、CLIの反復byteは別の未完条件として残す。

参考の[公式Blender importer](https://raw.githubusercontent.com/blender/blender/main/scripts/addons_core/io_anim_bvh/import_bvh.py)でchannelの宣言順、frame行、秒、degree、End Siteを確認した。実装コードは再利用せず、寛容な自動改名や末端補完も採用しない。ライセンス制限付きの実BVHを公開fixtureへ含めず、試験データは手書きの合成値とする。

確保故障の注入は未実施。解析中の確保は局所documentだけに行い、成功時の置換がnothrow moveであることをstatic_assertで固定する。
