#include "Rendering/TextureHandleCache.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::String;

namespace
{
    String ToCoreString(const char *text)
    {
#if defined(UNICODE)
        std::wstring wide;
        while (*text != '\0')
        {
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*text)));
            ++text;
        }
        return String(wide.c_str());
#else
        return String(text);
#endif
    }

    TextureHandle MakeTextureHandle(uint64_t id)
    {
        TextureHandle handle;
        handle.Id = id;
        return handle;
    }

    void TestMissingFindReturnsInvalid()
    {
        TextureHandleCache cache;
        assert(!cache.Find(ToCoreString("missing")).IsValid());
    }

    void TestStoreThenFind()
    {
        TextureHandleCache cache;
        const TextureHandle handle = MakeTextureHandle(10);

        cache.Store(ToCoreString("texture"), handle);

        assert(cache.Find(ToCoreString("texture")) == handle);
    }

    void TestStoreIfAbsentInsert()
    {
        TextureHandleCache cache;
        const TextureHandle candidate = MakeTextureHandle(20);
        TextureHandle resolved = TextureHandle::Invalid();

        assert(cache.StoreIfAbsent(ToCoreString("texture"), candidate, &resolved));

        assert(resolved == candidate);
        assert(cache.Find(ToCoreString("texture")) == candidate);
    }

    void TestStoreIfAbsentDuplicateKeepsExisting()
    {
        TextureHandleCache cache;
        const TextureHandle existing = MakeTextureHandle(30);
        const TextureHandle candidate = MakeTextureHandle(31);
        TextureHandle resolved = TextureHandle::Invalid();

        cache.Store(ToCoreString("texture"), existing);
        assert(!cache.StoreIfAbsent(ToCoreString("texture"), candidate, &resolved));

        assert(resolved == existing);
        assert(cache.Find(ToCoreString("texture")) == existing);
    }

    void TestClear()
    {
        TextureHandleCache cache;
        cache.Store(ToCoreString("texture"), MakeTextureHandle(40));

        cache.Clear();

        assert(!cache.Find(ToCoreString("texture")).IsValid());
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "TextureHandleCacheTest start\n";

    TestMissingFindReturnsInvalid();
    TestStoreThenFind();
    TestStoreIfAbsentInsert();
    TestStoreIfAbsentDuplicateKeepsExisting();
    TestClear();

    std::cout << "TextureHandleCacheTest passed\n";
    return 0;
}
