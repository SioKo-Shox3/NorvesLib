# Scripting

`Core::GEngine` は process-global `ScriptRuntime` を所有する。Application の `Engine::GEngine` は
`World` を所有し、初期化時に borrowed reference として runtime へ渡す。RenderThread は runtime や
live World へアクセスしない。

ScriptRuntime の mutating public API は Initialize で捕捉した GameThread owner だけで実行する。
owner 外は `WrongThread` を返し、destructor は private cleanup path で例外を外へ出さない。
initialized と cleanup-pending は別状態であり、pending resource は owner cleanup または destructor が
解放する。

Application 初期化は transaction であり、false と C++ exception は同一 ApplicationProcessor の
`Shutdown()` に集約する。Shutdown は到達済み phase を逆順に一度だけ解放する。ScriptRuntime cleanup が
失敗した場合は依存を破棄せず fail-fast する。

frame safe point は `OnUpdate` の後に Begin maintenance、SceneQuery 再構築の後に End maintenance を置く。
pause 中も maintenance は実行する。End maintenance は AngelScript の `asGC_ONE_STEP` を一回実行し、
成功 return ごとに `GcStepCount` を増やす。C++ exception と AngelScript callback exception は process
境界を越えない。

Memory lifecycle は本 phase の対象外である。global allocation と static/process lifetime の設計は別 phase
で扱う。hot reload candidate transaction と generation swap は Phase 4 の対象である。
