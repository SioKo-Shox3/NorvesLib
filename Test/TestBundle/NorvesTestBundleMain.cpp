// 束ねたテストの実行ファイルの入口。
//
// テストは次の順で選ぶ。
//   1. 引数 --test=<名前>（ctest の登録はこれを使う。引数からは取り除いてテストへ渡す）
//   2. 環境変数 NORVES_TEST_BUNDLE_MEMBER（この実行ファイルに無い名前なら無視する）
//   3. 束ねる先のテスト（ターゲットと同名。元の実行ファイルと同じ振る舞い）
// 選んだ名前は環境変数に入れる。テストが自分の実行ファイルを子プロセスとして起動したときも、
// 子が同じテストへ入るようにするため。
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define NORVES_TEST_BUNDLE_MEMBER(Name) int NorvesTestBundleRun_##Name(int argumentCount, char** arguments);
#include NORVES_TEST_BUNDLE_MEMBERS_FILE
#undef NORVES_TEST_BUNDLE_MEMBER

namespace
{
    struct BundledTest
    {
        const char* Name;
        int (*Run)(int argumentCount, char** arguments);
    };

#define NORVES_TEST_BUNDLE_MEMBER(Name) BundledTest{#Name, &NorvesTestBundleRun_##Name},
    const BundledTest GBundledTests[] = {
#include NORVES_TEST_BUNDLE_MEMBERS_FILE
    };
#undef NORVES_TEST_BUNDLE_MEMBER

    constexpr const char* TestArgumentPrefix = "--test=";
    constexpr const char* MemberEnvironmentName = "NORVES_TEST_BUNDLE_MEMBER";

    const BundledTest* FindTest(const char* name)
    {
        for (const BundledTest& test : GBundledTests)
        {
            if (std::strcmp(test.Name, name) == 0)
            {
                return &test;
            }
        }
        return nullptr;
    }

    void PrintTests()
    {
        std::fprintf(stderr, "available tests:\n");
        for (const BundledTest& test : GBundledTests)
        {
            std::fprintf(stderr, "  %s\n", test.Name);
        }
    }
} // namespace

int main(int argumentCount, char** arguments)
{
    const BundledTest* selected = nullptr;

    // --test=<名前> を探して引数から取り除く
    const size_t prefixLength = std::strlen(TestArgumentPrefix);
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::strncmp(arguments[index], TestArgumentPrefix, prefixLength) != 0)
        {
            continue;
        }
        const char* name = arguments[index] + prefixLength;
        selected = FindTest(name);
        if (selected == nullptr)
        {
            std::fprintf(stderr, "unknown test: %s\n", name);
            PrintTests();
            return 2;
        }
        for (int shift = index; shift < argumentCount; ++shift)
        {
            arguments[shift] = arguments[shift + 1];
        }
        --argumentCount;
        break;
    }

    if (selected == nullptr)
    {
        char* environmentName = nullptr;
        size_t environmentLength = 0;
        if (_dupenv_s(&environmentName, &environmentLength, MemberEnvironmentName) == 0 && environmentName != nullptr)
        {
            selected = FindTest(environmentName);
            std::free(environmentName);
        }
    }

    if (selected == nullptr)
    {
        selected = FindTest(NORVES_TEST_BUNDLE_DEFAULT);
    }
    if (selected == nullptr)
    {
        std::fprintf(stderr, "default test is missing: %s\n", NORVES_TEST_BUNDLE_DEFAULT);
        return 2;
    }

    _putenv_s(MemberEnvironmentName, selected->Name);
    return selected->Run(argumentCount, arguments);
}
