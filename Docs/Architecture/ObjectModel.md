# Object Model

## 目的

この文書は `Object` / `Entity` / `Component` の所有権、生成、破棄、Reflection の基本契約を定義します。

## 階層

- `World` は `Entity` を Inner として所有する。
- `Entity` は `Component` を Inner として所有する。
- `Outer` は所有者を指す非所有ポインタで、`Inner` 配列が所有関係を表す。
- `GetOuter()` を直接書き換えず、所有関係の変更は `AddInner()` / `RemoveInner()` 経由で行う。

## 所有権

- `AddInner(inner)` が `true` を返した時点で、`inner` の所有権は Outer に移る。
- 所有権移譲に成功した `inner` は呼び出し側で `delete` してはいけない。
- `RemoveInner(inner)` は Outer から外し、`Release()` によって最終破棄まで進める。
- `AddInner()` が `false` を返した場合、所有権は移っていないため、生成側が破棄する。

## ライフサイクル

通常生成の順序:

1. 生成側がオブジェクトを構築する。
2. 必要に応じて `Initialize()` を呼ぶ。
3. `AddInner()` 成功後、Outer が寿命を管理する。
4. `RemoveInner()` または Outer の破棄で `Release()` される。
5. refcount が 0 になると `Release()` 内で `Finalize()` され、削除される。

`Finalize()` は破棄前処理であり、通常の削除経路では `Release()` に集約する。Outer 側で `Finalize()` を明示呼び出ししてから `RemoveInner()` する二重経路は避ける。

## World API

- `World::SpawnObject<T>()` は成功時に World 所有の非所有ポインタを返す。
- `World::CreateComponent<T>(owner)` は成功時に owner 所有の非所有ポインタを返す。
- `World::GetRootEntities()` は World 直下の root Entity（World の直接 Inner）だけを返す。
- `World::GetObjectCount()` は World 直下の root Entity / 直接 Inner の数を返す。
- 返されたポインタを保持する場合は、Owner の寿命を超えて使わない。
- 即時破棄は `World::RemoveObject()` / `Entity::RemoveComponent()` を使う。
- 遅延破棄は `Entity::MarkForDestroy()` を使い、次回 `World::Tick()` で回収する。

## Reflection

- `PROPERTY` は実メンバを `TPropertyValue<T>` として保持する。
- Reflection の読み書きは typed getter 経由で実メンバへアクセスする。
- `ClassId` は `ClassRegistry` が一元発行し、ID 0 は `Object` 基底クラス用に予約する。
- `VariableContainer` は互換 API として残るが、通常の `PROPERTY` 値の一次ストレージではない。

## 禁止事項

- `AddInner()` 成功後の raw `delete`。
- `RemoveInner()` 前の不要な `Finalize()` 明示呼び出し。
- `Outer` を手動で差し替えること。
- `ClassId` を各クラスやテンプレート特殊化で独自発行すること。
