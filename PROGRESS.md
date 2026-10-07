# PROGRESS — NorvesLib

## G2全体の進捗（2026-10-07）

G2の実装とCPU/CLI受入れは完了。c1690c の CI run 37652726746 は全項目成功（Release CPU 74件、rig/retarget/Role Profile CLI、単体CLI旧出力・texture v1旧PS1のbyte互換、関連Debugケース）。GPU・DCC・実物の見た目は未検証として分離する。

- GR77/78/79/86/32: 取り込みと材質、明示縮約、256関節、submesh/paletteの経路を接続。surface_centroid、複数材質runtimeと対応外描画は明示保留
- GR82/83: 作者rest/frame検査、Skeleton/SkinnedMesh/ClipBank分離、Armature/clip-only、ANLY、非同期公開とdelegateを接続
- GR84: BVH/glTF共通補正、出力fps、周期、平面ルートと向きの分離、独立bankとCLI/asset-setを接続。再生時の適用はG3
- GR96: spec v2混在、依存追跡、増分、最小レポート、予算、jobs同値。詳細異常検出は後段

統合の確認: 2形式を識別する実wire検査2件（g++14）、owner配線8件（通常/最適化）、独立root-frame oracle12件をクラウドで通過。全体のネイティブbuildはWindows依存のため区切りのCIで確認する。

現在: main 573d7176 を統合中。NVMESH二形式・TextureHandle所有権・用途別cookの意味上の衝突を修正し、統合の区切りでまとめて検証する。未コミット・未公開。統合完了後にG3のGR12から着手する。

## 過去の履歴

完了済みの経緯とmain側の描画改善履歴は、内容を保持したまま[履歴一覧](Docs/History/2026-10-07-G2Integration/README.md)へ移しました。
