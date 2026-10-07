# GR12 姿勢評価の計測

- 日時: 2026-10-07 UTC
- コミット: be4ad346fdf3c85b4e0f3e04f8e27c3e4c4bb157
- 実行: [GitHub Actions 37698585325](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37698585325)、windows-2022、Release
- 入力: 合成52関節、180,000頂点（6万三角形相当）、4秒のclip、240frame。実素材ではない
- 比較: 旧Samplerの独立コピー対、準備済みSkeletalPoseBuilder。新経路はウォームアップ後。準備・読込の時間を含まない
- 旧経路: 10,977.421 ms（全240frame）
- 新経路: 6.718 ms（全240frame）

全頂点の位置・法線評価を伴う旧境界計算に対し、新経路の既定境界は関節AABBから保守的に作る。結果はこの入力と評価範囲での実測であり、ゲーム全体の速度倍率を示さない。境界の緩みやGPU影フィット、実物の見た目は未検証。

独立oracleとのpalette/readback比較、bounds包含、1000回のscratch/data-pointer・capacity安定、既存baseline30ケースは同runで通過。別テストSkinnedRenderPathContractTestの失敗を含むため、run全体はfailure。
