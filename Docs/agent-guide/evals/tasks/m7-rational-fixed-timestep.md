# M7 固定タイムステップを有理数で正確に進める

`FixedStepScheduler.cpp` だけを編集してください。`FixedStepSchedulerTest.cpp`、`CMakeLists.txt`、
`test.ps1` は編集してはいけません。

`AdvanceFixedStep(deltaNanoseconds, remainderScaledUnits)` を実装してください。返り値の
`FixedStepResult` は、この呼び出しで実行した固定 step 数、破棄した whole step 数、および次回へ
持ち越す scaled remainder を表します。

- 60 Hz を浮動小数点で近似せず、`nanoseconds * 60` を scaled unit とし、1 step を
  `1,000,000,000` scaled units とする。
- 1 呼び出しで実行する step は最大 8 回とする。
- pending の整数 whole step が上限を超えた場合、上限超過分は `DroppedSteps` に記録して破棄する。
  substep remainder は必ず保持する。
- catch-up 上限が各呼び出しで発動しない範囲では、異なる delta 列でも経過時間が同じなら、合計
  executed step 数と最終 remainder は決定的に同じになること。
- `INT64_MAX` の delta でも算術 overflow を起こしてはいけない。

次の境界値を満たしてください。

- `16,666,666 ns` は executed `0`、dropped `0`、remainder `999,999,960`。
- その remainder に `1 ns` を加えると executed `1`、dropped `0`、remainder `20`。
- 初期 remainder `0` の `50,000,001 ns` は executed `3`、dropped `0`、remainder `60`。
- 初期 remainder `0` の `337,500,000 ns` は executed `8`、dropped `12`、remainder `250,000,000`。
- 初期 remainder `0` の `INT64_MAX ns` は executed `8`、dropped `553402322203`、
  remainder `286548420`。
- catch-up 上限が発動しない小分け列では、合計 `1,000,000,000 ns` は executed `60`、dropped `0`、
  remainder `0`。

pause、thread、lifecycle、Public API、M8 物理、描画、Script はこの task の対象外です。完了前に
fixture 内で次を実行してください。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test.ps1
```
