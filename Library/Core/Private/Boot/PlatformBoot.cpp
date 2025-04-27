#include "../../Public/Boot/PlatformBoot.h"
#include "Platform/Windows/WindowsApplicationFactory.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <iostream>

#pragma comment(lib, "Shlwapi.lib")

namespace NorvesLib {
namespace Core {
namespace Boot {

bool PlatformInitialize(const Container::String& commandLine)
{
    // コンソールの割り当て（デバッグビルドのみ）
#ifdef _DEBUG
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
    }
#endif

    // 実行ファイルのディレクトリを作業ディレクトリに設定
    Container::String execPath = GetExecutablePath();
    Container::String execDir = execPath.substr(0, execPath.find_last_of(TEXT('\\')));
    if (!SetWorkingDirectory(execDir))
    {
        std::wcerr << L"Failed to set working directory to: " << execDir.c_str() << std::endl;
        return false;
    }

    return true;
}

void PlatformShutdown()
{
    // コンソールのクリーンアップ（デバッグビルドのみ）
#ifdef _DEBUG
    FreeConsole();
#endif

    // その他のプラットフォーム固有のクリーンアップ処理
}

Container::VariableArray<Container::String> ParseCommandLine(const Container::String& commandLine)
{
    // WindowsのAPIを使ってコマンドライン引数を解析
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argc);
    
    Container::VariableArray<Container::String> args;
    if (argv != nullptr)
    {
        for (int i = 0; i < argc; i++)
        {
            args.push_back(Container::String(argv[i]));
        }
        LocalFree(argv);
    }
    
    return args;
}

Container::String GetExecutablePath()
{
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return Container::String(buffer);
}

bool SetWorkingDirectory(const Container::String& path)
{
    return SetCurrentDirectory(path.c_str()) != 0;
}

std::unique_ptr<IApplication> CreateDefaultApplication()
{
    // プラットフォームに応じたアプリケーション実装を選択
#ifdef _WIN32
    // Windows向けの実装
    return Platform::WindowsApplicationFactory::CreateWindowsApplication();
#elif defined(__APPLE__)
    // macOS向けの実装（将来の拡張用）
    #error "macOS platform is not supported yet"
    return nullptr;
#elif defined(__linux__)
    // Linux向けの実装（将来の拡張用）
    #error "Linux platform is not supported yet"
    return nullptr;
#else
    // 未サポートのプラットフォーム
    #error "Unsupported platform"
    return nullptr;
#endif
}

int RunApplication(const Container::VariableArray<Container::String>& args)
{
    // プラットフォーム初期化
    Container::String commandLine = args.size() > 0 ? args[0] : Container::String();
    if (!PlatformInitialize(commandLine))
    {
        return -1;
    }
    
    // アプリケーション作成と実行
    auto app = CreateDefaultApplication();
    if (!app)
    {
        return -1;
    }
    
    // 初期化
    if (!app->Initialize(args))
    {
        return -1;
    }
    
    // 実行
    int exitCode = app->Run();
    
    // 終了
    app->Shutdown();
    
    // プラットフォーム終了処理
    PlatformShutdown();
    
    return exitCode;
}

int RunApplication(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 未使用パラメータの警告を抑制
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;
    
    // コマンドライン引数をContainer::String型の配列に変換
    Container::VariableArray<Container::String> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argv != nullptr) 
    {
        for (int i = 0; i < argc; ++i) 
        {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }
    
    // 共通処理を呼び出し
    return RunApplication(args);
}

} // namespace Boot
} // namespace Core
} // namespace NorvesLib