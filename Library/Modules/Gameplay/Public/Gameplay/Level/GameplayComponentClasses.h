// シーンの読み込みに必要な Gameplay の全 Component を登録する。
#pragma once

namespace NorvesLib::Modules::Gameplay::Level
{
    /// クラスは StaticClass() の初回呼び出しで登録されるので、シーンを読む前にこれを呼ぶ。
    /// モジュールの導入より前でも呼べる。同じクラスの再登録が可能なので、何度呼んでもよい。
    void RegisterGameplayComponentClasses();
} // namespace NorvesLib::Modules::Gameplay::Level
