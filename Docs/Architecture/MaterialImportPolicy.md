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
