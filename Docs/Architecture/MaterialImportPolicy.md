# 材質取り込みの数値方針

## 適用範囲

MaterialImportPolicyは画像IO・JSON・RHIに依存しない数値処理。
現時点では設定parser/asset-set/CLI/runtimeへ接続していないため、既存cookerの出力や起動画面は変わらない。
AI生成かどうかは呼出元が明示する。GLBという拡張子だけで分類しない。
設定・診断・canonical/hashの配線と、NVMESH v1への保存、撮影での受入れは別の接続工程。

## ARM

基本はAO=ignore、roughness=auto、metallic=auto。AI生成profileだけmetallicもignoreにする。
素材ごとのMode指定でtexture/ignore/constant/autoを上書きできる。
ignoreの最終値はAO/roughness=1、metallic=0。constantは指定した最終値を使う。
Textureでは全画素へfactorを適用し、8bitへ最近傍量子化する。factor=1なら元の256値すべてを保つ。
AOは1+strength*(sample-1)、roughness/metallicはfactor*sample。factorは有限の[0,1]。

Autoは元のlinear UNORM8の256bin histogramから1%・99%点を求める。
定義はType 7、0-basedの位置(n-1)*pを挟む2順位の線形補間。n=1も同じ定義で扱う。
整数部と余りを分け、巨大なcountでも順位の積のoverflowを避ける。総数overflowと空histogramは拒否する。
判定は、両百分位へfactor/strengthを適用した差がAutoWidth以下かどうか。既定AutoWidth=4/255。
min/maxは診断値であり定数判定に使わない。折り畳む最終値は全画素の平均へfactorを適用した値。
テクスチャを使う結果でもmin/max/mean/p1/p99/有効幅を返すため、ログ側で根拠を表示できる。

## 発光

emissiveFactorの各成分は[0,1]、emissiveStrengthは非負の有限値。
strength>0かつfactorのいずれかが正なら、emissiveNitsPerUnitの明示指定を必須にする。
積の浮動小数underflowで未設定検査をすり抜けない。テクスチャの存在は判定に使わない。
factorの既定0ならemissiveTextureだけの材質は非発光として通る。strength=0も同様。
指定値は正の有限値。優先順位は素材 > 資産sidecar > asset-set。
高優先に存在した不正値はエラーであり、低優先の正常値へ黙ってfallbackしない。

Y=0.2126R+0.7152G+0.0722Bとし、色=factor/Y、nits=Y*strength*emissiveNitsPerUnitへ換算する。
これでcolor*nitsが元のfactor*strength*換算値に対応し、factorの明るさを失わない。色比は最大factorで正規化し、強度積はfrexp/scalbnで最後に合成する。最終値が保存可能な入力を途中のunderflowで拒否しない。
float化後も共有128B材質codecの発光制約（Y許容・raw/再正規化後の65504未満）を通す。
表現できない巨大値・underflowは拒否し、失敗時出力は保持する。

MissingNitsPerUnitの診断へ資産名・材質名・設定名を付けることと、asset-setの実設定読込は接続工程の必須条件。
数値処理がasset-set fallbackを受け取れることだけで、その接続が済んだとは扱わない。

## 検証

MaterialImportPolicyTestはTRELLIS型の広いBとAI既定ignore、Pixal型Gの外れ値、百分位補間/幅、巨大count、
texture override/256byteの恒等性、AOの式、発光textureのみ・strength0・換算欠落/優先順位・非有限・保存上限を検証する。
通常/O2-NDEBUG/ASan・UBSan（LSan除外）を実行し、MEMBER/CTestへ登録する。
実画像・CLI・runtime・撮影の確認はこれに含めない。

## 設定の階層解決と正規形

MaterialImportSettings は生成元profileを明示で受け取り、資産の設定に素材の指定を上書きする。
ARMは各channelの存在maskを持ち、未指定のchannelだけ既定値を継承する。
両面は Inherit と Auto、alphaは Inherit と FromSource を分ける。素材側のautoは親の強制指定を解除し、形状に応じた判定へ戻す。
両面Autoは位置溶接後の閉鎖性を使う後段処理の指定であり、単なるsource値復帰ではない。この値層では判定せずmodeを保持する。
発光換算は素材 > 資産 > asset-set。存在する不正値は上位指定で隠さず拒否する。
換算不在はこの段階で拒否せず、ImportEmission が source factor/strength を見て発光時だけ拒否する。

解決済み材質1件のcanonicalは67B。u32版1、u8 profile、3組の(u8 ARM mode、f64 constant、f64 auto幅)、
u8両面、u8 alpha、u8換算存在、f64 nits/unit。LE/IEEE binary64でpaddingを含めない。
未使用constant/幅、不在換算値は0、-0は+0に揃える。
既存hashへASCII NVMATERIALSETTINGS、u64 canonical長、canonical bytesをFNV-1a64で連結する。
材質列を処理するときはsource material index順を固定する。旧幾何52Bや旧v0経路には自動適用しない。

この部品は解決済み値の契約であり、JSONの素材選択、設定ファイル、asset-set driver、cook/cacheの実呼出しは未接続。
素材名・資産名を付けた診断は呼出元で補う必要がある。設定名はMaterialSettingsErrorKeyで取得する。

## 設定blockのJSON解析API（sidecar接続前）

ParseAssetMaterialSettings は material block の値を受け、profile: source / ai_generated と下記の設定値を解析する。
ParseMaterialSettingsLayer は同じ設定値を解析するが、profile の素材単位上書きは拒否する。

- arm: occlusion / roughness / metallic ごとに texture / ignore / auto / constant:<0..1> の文字列
- doubleSided: auto / force_true / force_false。auto は後段の閉鎖性判定を使う指定であり、継承はfield省略で表す
- alphaMode: from_source / force_opaque。継承はfield省略
- emissiveNitsPerUnit: 正の有限number。未指定は発光判定時まで保持する

block省略と空objectは有効、null・未知field・重複field・不正型/値・文字列の末尾余剰は拒否する。
constant値はASCII浮動小数（指数表記も可）として全消費し、空白・非有限・範囲外を拒否する。
解析が最後まで成功するまで出力profile/layerを変更しない。

素材の対象指定とsidecar全体の接続は別工程。既存ParseSettings/LoadImportSettingsFileは変更せず、非空material blockをまだ受理しない。
ARM tokenの純テストは実行済み。実JsonDocumentによるJSON契約はnative MEMBER/CTestへ登録したが、Windows.h依存のため実行は未検証。
