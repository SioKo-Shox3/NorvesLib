#include "../../Public/Boot/PlatformBoot.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <iostream>

#pragma comment(lib, "Shlwapi.lib")

namespace NorvesLib {
namespace Core {
namespace Boot {

bool PlatformInitialize(const std::wstring& commandLine)
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
    std::wstring execPath = GetExecutablePath();
    std::wstring execDir = execPath.substr(0, execPath.find_last_of(L'\\'));
    if (!SetWorkingDirectory(execDir))
    {
        std::wcerr << L"Failed to set working directory to: " << execDir << std::endl;
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

std::vector<std::wstring> ParseCommandLine(const std::wstring& commandLine)
{
    // WindowsのAPIを使ってコマンドライン引数を解析
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argc);
    
    std::vector<std::wstring> args;
    if (argv != nullptr)
    {
        for (int i = 0; i < argc; i++)
        {
            args.push_back(argv[i]);
        }
        LocalFree(argv);
    }
    
    return args;
}

std::wstring GetExecutablePath()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    return std::wstring(buffer);
}

bool SetWorkingDirectory(const std::wstring& path)
{
    return SetCurrentDirectoryW(path.c_str()) != 0;
}

} // namespace Boot
} // namespace Core
} // namespace NorvesLib