# 地図の生成

`map_build.example.json` を地域に合わせてコピーし、撮影設定を用意して、リポジトリのルートから実行する。Python には NumPy と OpenCV（`cv2`）が必要。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Map/BuildMap.ps1 -Config <設定のJSON>
```

1. 撮影では `Game.exe --scene=<scene_id> --map-capture=<capture_spec> --window-background` を起動し、終了コードが 0 であることと、`capture_dir` の `capture_manifest.json` およびタイルを確認する。撮影設定の出力先を `capture_dir` と一致させる。既存の目録とタイルを使う場合は `-SkipCapture` を付ける。
2. `terrain` の地形スペックを一時的に cook し、目録・タイルと `compose_options` から `compose_dir` に地図を合成する。`terrain` には cook 済みの `.nvtrn` または `.nvpkg` も指定できる。
3. `asset_set` の `textures` と `raws` を cook し、`cook_dir` にパッケージと `manifest.json` を書く。一覧の `source_path` は `compose_dir` に合わせる。`cook_assets: false` の一覧は全体の cook には含まれない。

設定内の相対パスはリポジトリのルートから解決する。`-GameExe`、`-AssetCookExe`、`-Python` で実行ファイルを指定できる。撮影・合成・cook はそれぞれの入力内容の SHA-256 と出力を照合し、一致すれば飛ばす。撮影の印はシーン ID・撮影設定・Game の実行ファイルを含み、シーンの個々のアセットは含まない。アセットを変更したときは `-Force` を付ける。`-Force` は印が一致していても実行する。`-SkipCapture` と併用した場合、撮影は行わず合成と cook を実行する。
