#include "ImGuiModule/IImGuiView.h"
#include "ImGuiModule/ImGuiViewRegistry.h"

#include "CoreTypes.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <utility>

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

    namespace
    {
        // Display* のフォントへ載せる文字と撮影への載せ方（GameThread 単一スレッド）。
        Core::Container::AnsiString &GetDisplayTextStorage()
        {
            static Core::Container::AnsiString text;
            return text;
        }

        bool &GetOverlayInCaptureStorage()
        {
            static bool bInCapture = false;
            return bInCapture;
        }

        bool &GetImageInitializationStartedStorage()
        {
            static bool bStarted = false;
            return bStarted;
        }
    } // namespace

    Core::Container::VariableArray<ImGuiImageRegistration> &GetRegisteredImGuiImages()
    {
        static Core::Container::VariableArray<ImGuiImageRegistration> images;
        return images;
    }

    void BeginImGuiImageInitialization()
    {
        GetImageInitializationStartedStorage() = true;
    }

    uint64_t RegisterImGuiImage(const uint8_t *rgba8Pixels, uint32_t width, uint32_t height)
    {
        static uint64_t nextId = 2;
        if (GetImageInitializationStartedStorage() || rgba8Pixels == nullptr ||
            width == 0 || height == 0 || width > 4096 || height > 4096 || nextId < 2)
        {
            NORVES_LOG_WARNING("ImGui", "画像を登録できません（初期化開始後・画素または寸法が無効・ID 枯渇）");
            return 0;
        }

        ImGuiImageRegistration image;
        image.Id = nextId;
        image.Width = width;
        image.Height = height;
        // 辺の上限により、RGBA8 全体のサイズも 32bit の範囲に収まる。
        const uint32_t byteCount = width * height * 4u;
        image.Pixels.assign(rgba8Pixels, rgba8Pixels + byteCount);
        GetRegisteredImGuiImages().push_back(std::move(image));
        return nextId++;
    }

    bool IsImGuiImageReady(uint64_t imageId)
    {
        for (const ImGuiImageRegistration &image : GetRegisteredImGuiImages())
        {
            if (image.Id == imageId)
            {
                return image.bReady;
            }
        }
        return false;
    }

    void AddImGuiDisplayText(const char *utf8Text)
    {
        if (utf8Text != nullptr)
        {
            GetDisplayTextStorage() += utf8Text;
        }
    }

    const Core::Container::AnsiString &GetImGuiDisplayText()
    {
        return GetDisplayTextStorage();
    }

    void SetImGuiOverlayInCapture(bool bInCapture)
    {
        GetOverlayInCaptureStorage() = bInCapture;
    }

    bool IsImGuiOverlayInCapture()
    {
        return GetOverlayInCaptureStorage();
    }
} // namespace NorvesLib::Modules::Gui
