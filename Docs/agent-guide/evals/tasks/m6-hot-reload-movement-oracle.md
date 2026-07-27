# M6 hot-reload movement oracle を v1 の外部基準で固定する

`M6HotReloadMovementContract.ps1` だけを編集してください。`test.ps1` は編集してはいけません。

この contract は `ready_good`、`ready_bad`、`complete` の movement marker を検査します。現状は
`ready_bad.X == ready_bad.V2InitialX` と `complete.X == complete.V2InitialX` を確認していますが、
v2 初期 X が v1 の `ready_good.X` と同じであるという外部基準を確認していません。そのため、bad と
complete の X および `V2InitialX` を同じ別値へ変える協調変異を誤って受理します。

`ready_good.X` を v2 初期 X の外部基準として固定する不変条件を1つ追加してください。値を
hardcode してはいけません。既存の marker parse、v1/v2 anchor、Y の増加、および各 marker 内の
X と `V2InitialX` の一致契約を維持してください。generation に関する条件を追加してはいけません。

完了前に、fixture 内で次を実行してください。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File test.ps1
```
