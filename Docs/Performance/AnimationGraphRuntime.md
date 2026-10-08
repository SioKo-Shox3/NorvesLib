# アニメーショングラフのCPU計測

## 条件

- 対象commit: `4ae627f4b75e51772a54fbf13dc7b8cba10b7dd6`
- [Windows CPU検証run](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37714129004)、pose job `113106590900`
- GitHub-hosted Windows 2022、Visual Studio 2022、x64 Release
- `NORVES_POSE_BENCHMARK=1`、`AnimGraphRuntimeTest.cpp::BenchmarkAnimGraph`
- 合成52関節。根に並進、残り51関節に2キーの回転チャンネルを持つ2資源を使用
- 各条件で100回Update/Evaluateした後、Updateを5000回、続いてEvaluateを5000回計測
- dt=1/60秒。4クリップ条件は1000秒の遷移を開始し、計測中も4ノードが寄与
- 初期化・JSONコンパイル・資源生成は計測外。FK・palette・頂点境界・GPU描画も含めない

## 観測値

| 条件 | Update 5000回 | Evaluate 5000回 | Update 1回換算 | Evaluate 1回換算 |
| --- | ---: | ---: | ---: | ---: |
| 2クリップBlend2 | 14.792 ms | 154.840 ms | 2.958 µs | 30.968 µs |
| 4クリップ・遷移中 | 7.399 ms | 417.037 ms | 1.480 µs | 83.407 µs |
| 加算レイヤー1層 | 13.374 ms | 152.819 ms | 2.675 µs | 30.564 µs |

各条件1回の測定で、ホスト個体・CPU周波数・同居負荷は固定していない。条件間の速さの序列を一般化するには、同一環境で反復し分布を確認する必要がある。

このfixtureのroot設定はNoneで、同期groupも指定しない。イベント・ルート抽出・位相同期は同じCPU bundleの機能回帰で検査するが、上表はその負荷の計測値ではない。実アセット、実ゲームの全フレーム時間、GPU表示品質は未測定。
