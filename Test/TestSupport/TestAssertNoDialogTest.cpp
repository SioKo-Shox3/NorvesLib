// assert の失敗時に対話窓を出さず非 0 で終了することを確かめる。
#undef NDEBUG
#include <cassert>

int main()
{
    assert(false && "TestAssertNoDialogTest");
    return 0;
}
