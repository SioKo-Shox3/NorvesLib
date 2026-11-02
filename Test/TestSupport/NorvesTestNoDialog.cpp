// テストの静的初期化より前に CRT のエラーを標準エラー出力へ送る。
#include <stdlib.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

// テスト側の静的初期化より先に効かせるため、意図して lib の初期化領域に置く。
#pragma warning(disable : 4073)
#pragma init_seg(lib)

namespace
{
    struct FNoDialogInitialization
    {
        FNoDialogInitialization()
        {
            _set_error_mode(_OUT_TO_STDERR);
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
        }
    };

    FNoDialogInitialization GNoDialogInitialization;
}
