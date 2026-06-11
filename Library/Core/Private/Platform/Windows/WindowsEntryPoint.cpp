#include "Boot/AppLauncher.h"
#include "Boot/EntryPoint.h"
#include "Boot/BootConfig.h"
#include "Container/Containers.h"
#include <Windows.h>
#include <shellapi.h>
#include <string>
#pragma comment(lib, "Shell32.lib")

namespace
{
    /**
     * @brief wchar_t 文字列を Container::String（TString<TCHAR>）に変換する
     *
     * TCHAR が wchar_t の場合は直接コピーし、
     * TCHAR が char（MBCS/UTF-8）の場合は WideCharToMultiByte で変換します。
     *
     * @param pWideText 変換元の wchar_t 文字列
     * @return 変換後の String
     */
    NorvesLib::Core::Container::String WideToString(const wchar_t *pWideText)
    {
        using namespace NorvesLib::Core::Container;

        if (!pWideText || pWideText[0] == L'\0')
        {
            return String{};
        }

#ifdef _UNICODE
        // Unicode ビルド: TCHAR = wchar_t なので直接コピー
        return String(pWideText);
#else
        // MBCS/UTF-8 ビルド: WideCharToMultiByte で UTF-8 変換
        const int requiredLength = WideCharToMultiByte(
            CP_UTF8,
            0,
            pWideText,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredLength <= 1)
        {
            return String{};
        }

        // null 終端の分を含めた領域を確保して変換
        std::string utf8(static_cast<std::string::size_type>(requiredLength - 1), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            pWideText,
            -1,
            utf8.data(),
            requiredLength,
            nullptr,
            nullptr);

        // String (TString<char>) へコピー
        return String(utf8.c_str());
#endif
    }

    /**
     * @brief Windows コマンドライン引数を BootConfig::Arguments に変換する
     *
     * CommandLineToArgvW で取得した LPWSTR 配列を
     * Container::String の VariableArray に変換します。
     * TCHAR が wchar_t の場合は直接コピー、char の場合は UTF-8 変換します。
     *
     * @return 変換済みの引数リスト
     */
    NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>
    ParseWindowsCommandLine()
    {
        using namespace NorvesLib::Core::Container;

        VariableArray<String> result;

        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
        {
            return result;
        }

        for (int i = 0; i < argc; ++i)
        {
            result.emplace_back(WideToString(argv[i]));
        }

        LocalFree(argv);
        return result;
    }
} // namespace

/**
 * @brief Windows GUI アプリケーションのエントリポイント
 *
 * ゲーム側の CreateApplicationBootConfig() を呼び出して BootConfig を構築し、
 * コマンドライン引数を設定してから LaunchApplication() を呼び出します。
 */
int WINAPI WinMain(
    HINSTANCE /* hInstance */,
    HINSTANCE /* hPrevInstance */,
    LPSTR /* lpCmdLine */,
    int /* nCmdShow */)
{
    NorvesLib::Core::Boot::BootConfig config = NorvesLib::Core::Boot::CreateApplicationBootConfig();
    config.Arguments = ParseWindowsCommandLine();
    return NorvesLib::Core::Boot::LaunchApplication(config);
}
