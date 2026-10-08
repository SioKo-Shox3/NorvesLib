# Cook出力package検証（G2-S6 / GR96）

ValidateCookOutputPackageは、callerが用意した期待AssetCookedReferenceと読み込み済みbyte列を照合する。成功時だけ全packageのsizeとFNV-1a64を返す。fileを開かず、path解決・source freshness・所有権・保存state・Cook/Skip/Error決定は扱わない。

## 検証範囲

- package V1、単一entry、期待entry名と数値FourCC、CookedVersion 0
- entryに記録されたhashとmanifestの数値CookedHash、および実payloadの再計算hash
- raw.v0は空payloadと任意のprintable FourCCを許容
- texture v0のRGBA8 sRGB/linear、RG8 linear、R8 linearを実parserのpixel/colorspaceで区別
- audioはPCM16、static meshはversion major 0、skeletalはversion minor 2
- skeletalのvertex/index/joint/clip/submesh/material slotの6数量を実parse結果と照合。metadata省略・旧4数量だけの参照は受理しない

全package hashにはheader/table/name/paddingも含む。paddingだけが異なる正常packageは検証に成功し、hashが異なる。過去recordとの比較は呼出側で行う。FNVは増分用で暗号学的な改ざん保証ではない。

## 呼出側に残る責務

SourceHash、logical key、variant、CookedPackageが指す実fileの関係は別途検証する。冗長なhash/FourCCの文字列表現は数値照合のauthorityにしない。期待参照を古いcacheから無検査に採用してはならない。

既存ModelCookCacheはこの段階では変更しない。今後の共通decisionが依存印・現在要求から導いた完全な出力一覧・安全なpath・各package全体印をまとめて検証する。

## 試験

実CookSingleAssetからraw/custom FourCC/空raw、texture4形式、audio、内包画像付きstatic model、skeletalを生成する。wrapper/payload/hash/type/format/6数量/失敗時出力保持とpadding差を検証する。既存の正常mesh v1とskeletal v0.0/v0.1 goldenをpackageに包み、parserでは成功するが現cook出力としては拒否することも確認する。

7ba94fed97aff8da26eb4950979c96d1171b197aの[Windows run37218137820](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37218137820)で18 CPU契約と単体7 CLIが成功。正常mesh v1・skeletal v0.0/v0.1の拒否、実全profile、派生画像3件も検証した。固定79出力とtexture v1の10出力のbyte互換を維持している。record比較や共通Skip決定の完了ではない。
