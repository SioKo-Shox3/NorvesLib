# NEXT_FINDINGS

## R4-P3 評価指摘

- [P1 / blocking] `DDGIProbePass` のパイプライン・buffer・descriptor set 作成は、Vulkan 実装が送出する例外を捕捉していない。資源準備を安全に失敗させ、DDGIを無効化して既存描画を継続するGPU契約テストを追加する。
- [P2 / blocking] GPUテストが `LightingPass` 接続を通らず、TLAS build後に別コマンドリストでdispatchし、複数フレームslotの再利用と描画fallbackを検証していない。`LightingPass` 経由の実GPU readback、同一コマンド列のTLAS build→dispatch、slot再利用、RT無効/資源例外時の出力比較を追加する。
