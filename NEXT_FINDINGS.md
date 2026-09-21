# NEXT_FINDINGS

- [R4-P4][blocking] GPU fixtureの配列view・octahedral UV・visibility weightingを受け入れ直す。独立評価は`NEEDS_WORK`。single-layer atlasはshaderの`sampler2DArray`/`image2DArray`とVulkan image view型が不一致、現行のUV写像は書き込みtexel中心とずれ、1 probe fixtureでは全cornerが同じprobeへclampされvisibility weightingを観測できないと指摘された。atlas最小array layer数と`AtlasUv`は修正中なので、直接GPU出力にVUIDがないこと、斜め方向のreadbackでUV/borderが使われることを確認する。
  - 2回のvisibility fixture実行はいずれも`visibility=1,1`で失敗。Claude Opus助言の見立てでは壁だけでなく履歴混合が主因: plane frameの非遮蔽distance atlasをseed frameがhysteresis 0.8で引き継ぎ、miss距離が残ってmean distance約2.4となりpoint distanceを上回る。現在の最小2-probe fixtureでprobe寄与差を測る場合、大きな遮蔽壁とseed履歴無効化を組み合わせ、atlas期待値ではseedを前frameと混ぜない。Frame slotは0,1,2と単調なまま、FrameNumberだけを1,3,4として直前フレーム照合を切る案を優先し、GPU validationとreadbackで確認する。2x1x1でweightedとunweightedの差が明確に出ない場合は2x2x2以上へ広げる。
  - RHI backendのview型実装変更はR4-P4 allowlist外。既定atlasを2 layerにする場合は未初期化layerのlayout/valueも直接validation logで確認する。編集後は`git diff --numstat`と`git diff --ignore-cr-at-eol --numstat`を一致させる。
