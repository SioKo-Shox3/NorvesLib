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

    /**
     * @brief 引数列に --bridge-port が含まれるかを判定する
     *
     * NorvesEditor 連携（Workstream L）でエディタが Game を起動するときは
     * --bridge-port <port> を付与する。このモードでは GUI 親プロセス
     * （Tauri）にコンソールが無く、OpenDebugConsole() の
     * AttachConsole(ATTACH_PARENT_PROCESS) が失敗して AllocConsole() に
     * フォールバックすると、プロセスの STD_OUTPUT_HANDLE が新しいコンソール
     * バッファへ差し替えられてしまう。その結果 READY <port> 行がエディタ側へ
     * 継承されたパイプに届かなくなる（boot-order 問題 B1）。
     *
     * そこで --bridge-port が指定されている場合のみ、LaunchApplication が
     * OpenDebugConsole を呼ぶ前にデバッグコンソールを無効化し、
     * STD_OUTPUT_HANDLE を継承パイプのまま維持する。エディタモードでは
     * デバッグコンソールは不要で、ログはファイルへ出力され続ける。
     *
     * ここでは存在判定のみを行い、値の検証や保持は行わない（実際の
     * ポート解釈は GameApplicationHandler::OnPreInitialize が担う）。
     *
     * @param args 解析済みのコマンドライン引数
     * @return --bridge-port が含まれていれば true
     */
    bool HasBridgePortArgument(
        const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> &args)
    {
        using namespace NorvesLib::Core::Container;

        const String optionName(TEXT("--bridge-port"));
        for (size_t i = 0; i < args.size(); ++i)
        {
            const String &arg = args[i];
            // 完全一致（--bridge-port <port>）か、インライン形式（--bridge-port=...）。
            if (arg == optionName)
            {
                return true;
            }
            if (arg.size() > optionName.size() &&
                arg.substr(0, optionName.size()).compare(optionName) == 0 &&
                arg[optionName.size()] == TEXT('='))
            {
                return true;
            }
        }
        return false;
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

    // エディタ連携（--bridge-port）時は READY 行を継承パイプへクリーンに届けるため、
    // デバッグコンソールに加えて stdout ログも無効化する。LaunchApplication が
    // OpenDebugConsole / ロガー初期化を行う前にここで決めることで、AllocConsole による
    // STD_OUTPUT_HANDLE 差し替えと、ログの stdout 混入（エディタの先頭行=READY 検出を
    // 壊す）の双方を防ぐ（boot-order 問題 B1 / N-1 の修正）。
    if (HasBridgePortArgument(config.Arguments))
    {
        config.bEnableDebugConsole = false;
        config.bLogToStdout = false;
    }

    return NorvesLib::Core::Boot::LaunchApplication(config);
}
