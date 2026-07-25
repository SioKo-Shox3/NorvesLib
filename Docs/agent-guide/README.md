# NorvesLib agent guide

詳細規約集。要点と参照は `CLAUDE.md` / `AGENTS.md`（working agreement）にあり、本ディレクトリはその詳細版。本体から移設した規約はここが全文の正本で、本体には索引だけを残す。

## 目次

- [architecture.md](./architecture.md) — 所有権モデル（Object/Resource・Inner/Outer・シングルトン禁止）、レンダリングとスレッド（FramePacket・RHI 境界）、ディレクトリ構成と危険地帯。
- [coding-style.md](./coding-style.md) — 必ず守る独自型ルール、スタイル・命名、行末コード/BOM の扱い。
- [build-and-verify.md](./build-and-verify.md) — ビルドとテストの標準コマンド、テスト追加の規約。
- [orchestration.md](./orchestration.md) — 役割分担、実装委譲とレビューのダブルチェック、デュアルメイン運用、ハーネスによる強制、工程（フェーズ）の流れ。
- [branching-and-commits.md](./branching-and-commits.md) — コミットメッセージの規約、プッシュ承認制、マージ後の片付け。
- [model-playbook.md](./model-playbook.md) — 役割ごとのモデル選定（相対ティア）と難易度ベースの昇格ルール。
- [model-evals.md](./model-evals.md) — モデル eval fixture の回し方。
