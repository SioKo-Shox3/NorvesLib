#include "ImGuiModule/IImGuiView.h"
#include "ImGuiModule/ImGuiViewRegistry.h"

#include "CoreTypes.h"

#include <algorithm>

// ImGuiViewRegistry — ImGui view 登録レジストリの定義(Piece 2)。
//
// 外部(Game 等)が RegisterImGuiView/UnregisterImGuiView で IImGuiView を登録/解除し、
// ImGuiModule::Tick が GetRegisteredImGuiViews() を反復して各 view の OnImGui() を呼ぶ。
//
// 格納は非所有の借用ポインタ配列(Meyers シングルトン)。view の寿命は登録側が持ち、本
// レジストリは生存中ポインタを借りるだけ。登録/解除/反復はすべて GameThread
// (ImGuiModule::Tick と同一スレッド)前提で直列実行されるため、ロックは不要。
//
// 本 TU は imgui に依存しない(IImGuiView.h も imgui 非依存)。imgui を引くのは
// ImGuiModule.cpp と各 view 実装(Game 側 .cpp)に閉じる。

namespace NorvesLib::Modules::Gui
{
    namespace
    {
        /**
         * @brief 登録済み view の借用ポインタ配列(GameThread 単一スレッド・Meyers シングルトン)
         *
         * 非所有。寿命は登録側が持つ。最初の Register/Unregister/反復呼び出し時に構築される。
         */
        Core::Container::VariableArray<IImGuiView *> &GetViewRegistry()
        {
            static Core::Container::VariableArray<IImGuiView *> registry;
            return registry;
        }
    } // namespace

    void RegisterImGuiView(IImGuiView *view)
    {
        if (view == nullptr)
        {
            return;
        }

        Core::Container::VariableArray<IImGuiView *> &registry = GetViewRegistry();

        // 同一ポインタの二重登録を弾く(OnImGui の重複呼び出し防止)。
        if (std::find(registry.begin(), registry.end(), view) != registry.end())
        {
            return;
        }

        registry.push_back(view);
    }

    void UnregisterImGuiView(IImGuiView *view)
    {
        if (view == nullptr)
        {
            return;
        }

        Core::Container::VariableArray<IImGuiView *> &registry = GetViewRegistry();
        auto it = std::find(registry.begin(), registry.end(), view);
        if (it != registry.end())
        {
            registry.erase(it);
        }
        // 未登録(または既に解除済み)なら no-op。
    }

    Core::Container::VariableArray<IImGuiView *> &GetRegisteredImGuiViews()
    {
        return GetViewRegistry();
    }
} // namespace NorvesLib::Modules::Gui
