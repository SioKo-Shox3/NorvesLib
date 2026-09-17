# NEXT_FINDINGS — R2-P1評価

## R2-P1-001

- status: open
- source: R2-P1読み取り専用評価
- finding: CPU参照が太陽ディスクの放射輝度を立体角積分せず、そのまま散乱源へ掛けていたため、後続Hillaire 2020系LUTの照度単位と整合しなかった。
- fix: 可視太陽ディスク立体角を散乱源へ適用し、平行大気・単一散乱・float精度というP1参照の適用範囲と、球殻/多重散乱/地表反射を後続LUTへ委ねる契約をヘッダへ明記する。
- verify: `SkyAtmosphereModelTest` の再ビルド・CTest、差分の再評価。
