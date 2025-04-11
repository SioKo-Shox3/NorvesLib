#include <windows.h>
#include <vector>
#include <string>
#include <memory>

#include "Library/Core/Public/IApplication.h"

// アプリケーションのインスタンスを作成する関数の宣言
// 実際の実装はゲーム側で行う
extern std::unique_ptr<NorvesLib::IApplication> CreateApplication();

// Windowsアプリケーションのエントリーポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 未使用パラメータの警告を抑制
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // コマンドライン引数をwstring型のベクトルに変換
    std::vector<std::wstring> args;
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
    
    // アプリケーションインスタンスの作成
    auto app = CreateApplication();
    if (!app) 
    {
        MessageBoxW(NULL, L"アプリケーションの作成に失敗しました", L"エラー", MB_ICONERROR);
        return -1;
    }
    
    // アプリケーションの初期化と実行
    if (!app->Initialize(args)) 
    {
        MessageBoxW(NULL, L"アプリケーションの初期化に失敗しました", L"エラー", MB_ICONERROR);
        return -1;
    }
    
    // アプリケーションのメインループを実行
    int exitCode = app->Run();
    
    // アプリケーションの終了処理
    app->Shutdown();
    
    return exitCode;
}