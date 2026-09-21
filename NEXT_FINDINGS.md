# R5-P13 所見の処理結果

- 転送先texture image・memory・viewを遅延解放レコードへ移し、device waitの成功またはdevice lostまでstaging資源と共に保持する。
- 非device-lostのteardown待機失敗では、allocator・command pool・device・instanceを破棄せず保持する。
- Validation error callbackの検出数をテスト合否へ接続し、合成エラーの捕捉と通常処理中のエラー0件を確認する。
- 診断文字列の改行escapeを修正し、無関係な診断出力の書式を維持する。
- 非blocking: 同一deviceの共有command pool操作と`WaitIdle`は呼出側で直列化する。並行呼出しの内部同期は保証対象外。
